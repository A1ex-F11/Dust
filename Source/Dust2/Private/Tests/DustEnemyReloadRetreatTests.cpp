#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Components/StateTreeAIComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyReloadComponent.h"
#include "DustEnemyStateTreeTasks.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponRuntime.h"

// The adapter is normally bound by BP/StateTree startup. This seam only prepares its existing
// private bindings so the reload-retreat gate can be exercised without a StateTree asset or a
// navigation mesh. Codex owns the one-line friend addition in the adapter header.
struct FDustEnemyReloadRetreatTestAccess
{
	static void PrepareAdapter(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
		APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon, AActor* Target)
	{
		Adapter->Controller = Controller;
		Adapter->BoundPawn = Pawn;
		Adapter->Tree = Tree;
		Adapter->WeaponComponent = Weapon;
		Adapter->PathFollowing = Controller ? Controller->GetPathFollowingComponent() : nullptr;
		Adapter->TargetClass = Target ? Target->GetClass() : nullptr;
		Adapter->Target = Target;
		Adapter->bSightCurrentlySensed = true;
		Adapter->bConfirmedTarget = true;
		Adapter->bInLossGrace = false;
		Adapter->bDeferredReloadSightLoss = false;
		Adapter->LastKnownLocation = Target ? Target->GetActorLocation() : FVector::ZeroVector;
		Adapter->LastConfirmedTime = Adapter->GetWorld() ? Adapter->GetWorld()->GetTimeSeconds() : 0.0f;
		Adapter->bInitialized = true;
		Adapter->bStopped = false;
		Adapter->NextSpacingAllowedAt = 0.0f;
	}

	static void SetConfirmedTargetLocation(UDustEnemyCombatAdapterComponent* Adapter, AActor* Target,
		const FVector& Location)
	{
		if (IsValid(Target)) Target->SetActorLocation(Location);
		Adapter->Target = Target;
		Adapter->bSightCurrentlySensed = true;
		Adapter->bConfirmedTarget = true;
		Adapter->bInLossGrace = false;
		Adapter->bDeferredReloadSightLoss = false;
		Adapter->LastKnownLocation = Location;
		Adapter->LastConfirmedTime = Adapter->GetWorld() ? Adapter->GetWorld()->GetTimeSeconds() : 0.0f;
	}

	static void SetUnconfirmed(UDustEnemyCombatAdapterComponent* Adapter, AActor* Target)
	{
		const UWorld* World = Adapter->GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		Adapter->Target = Target;
		Adapter->bSightCurrentlySensed = false;
		Adapter->bConfirmedTarget = false;
		Adapter->bInLossGrace = true;
		Adapter->bDeferredReloadSightLoss = false;
		Adapter->GraceStartTime = Now;
		Adapter->QuickReacquireTarget = Target;
		Adapter->QuickReacquireExpiry = Now + 3.0f;
		Adapter->bQuickReacquireAvailable = false;
	}

	static void SetCooldown(UDustEnemyCombatAdapterComponent* Adapter, float AllowedAt)
	{
		Adapter->NextSpacingAllowedAt = AllowedAt;
	}

	// White-box replacement for a real MoveTo: tests only assert that a second movement blocks
	// the reload entry and that stale cleanup cannot release it. No navigation success is faked.
	static void InjectSpacing(UDustEnemyCombatAdapterComponent* Adapter, uint64 Token, uint32 RequestId)
	{
		Adapter->ActiveSpacingToken = Token;
		++Adapter->SpacingGeneration;
		Adapter->SpacingMoveId = FAIRequestID(RequestId);
		Adapter->ImmediateSpacingMoveId = FAIRequestID::InvalidRequest;
		Adapter->SpacingTarget = Adapter->Target;
		Adapter->SpacingDestination = Adapter->Target.IsValid()
			? Adapter->Target->GetActorLocation() : FVector::ZeroVector;
		Adapter->SpacingElapsedSeconds = 0.0f;
		Adapter->SpacingStatus = EDustCreateSpaceStatus::Running;
		Adapter->bCreateSpaceActive = true;
		Adapter->bIssuingSpacingMove = false;
	}

	static uint64 ActiveSpacingToken(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->ActiveSpacingToken;
	}
};

namespace
{
	class FReloadRetreatTestWorld
	{
	public:
		FReloadRetreatTestWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(true)
				.ShouldSimulatePhysics(false);

			World = UWorld::CreateWorld(
				EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		~FReloadRetreatTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World = nullptr;
			}
		}

		UWorld* Get() const { return World; }

		void Advance(float Seconds) const
		{
			if (!World || !FMath::IsFinite(Seconds) || Seconds <= 0.0f) return;
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / 0.05f));
			const float StepSeconds = Seconds / Steps;
			for (int32 Index = 0; Index < Steps; ++Index) World->Tick(LEVELTICK_All, StepSeconds);
		}

		UWeaponDefinitionData* MakeDefinition(int32 InitialAmmo, int32 ReserveAmmo, int32 Capacity = 30) const
		{
			UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
			UWeaponBallisticsData* Ballistics = NewObject<UWeaponBallisticsData>(Definition);
			Ballistics->DamagePerShot = 10.0f;
			Ballistics->ArmorPenetration = 0.0f;
			Ballistics->HeadDamageMultiplier = 2.0f;
			Ballistics->TraceDistance = 2000.0f;

			Definition->BallisticsData = Ballistics;
			Definition->ConfigurationVersion = 1;
			Definition->ReloadStyle = EWeaponReloadStyle::Magazine;
			Definition->MagazineCapacity = FMath::Max(1, Capacity);
			Definition->InitialMagazineAmmo = InitialAmmo;
			Definition->InitialReserveAmmo = ReserveAmmo;
			Definition->FireInterval = 0.05f;
			Definition->AllowedFireModes = { EFireMode::FullAutomatic };
			Definition->DefaultFireMode = EFireMode::FullAutomatic;
			Definition->StationarySpreadAngle = 0.0f;
			Definition->MovingSpreadAngle = 0.0f;
			Definition->JumpSpreadAngle = 0.0f;
			Definition->MovementSpreadSpeedThreshold = 0.0f;
			return Definition;
		}

		UDustEnemyCombatProfile* MakeProfile() const
		{
			UDustEnemyCombatProfile* Profile = NewObject<UDustEnemyCombatProfile>(GetTransientPackage());
			Profile->ReactionDelay = 0.0f;
			Profile->CreateSpaceTriggerDistance = 350.0f;
			Profile->DesiredEngagementDistance = 600.0f;
			Profile->CreateSpaceCooldownSeconds = 3.0f;
			Profile->CloseCombatSenseRange = 500.0f;
			return Profile;
		}

		struct FRig
		{
			AAIController* Controller = nullptr;
			APawn* Pawn = nullptr;
			ADust2PlayerCharacter* Target = nullptr;
			UStateTreeAIComponent* Tree = nullptr;
			UDustEnemyWeaponComponent* Weapon = nullptr;
			UDustEnemyCombatAdapterComponent* Adapter = nullptr;
			UDustEnemyReloadComponent* Reload = nullptr;
		};

		FRig SpawnRig(UWeaponDefinitionData* Definition, bool bInfiniteReserve = false,
			float LogicalSeconds = 10.0f) const
		{
			FRig Rig;
			if (!World || !Definition) return Rig;

			Rig.Controller = World->SpawnActor<AAIController>();
			Rig.Pawn = World->SpawnActor<APawn>();
			Rig.Target = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Rig.Controller || !Rig.Pawn || !Rig.Target) return FRig();
			Rig.Controller->Possess(Rig.Pawn);
			Rig.Target->SetActorLocation(FVector(600.0f, 0.0f, 0.0f));
			if (UCharacterMovementComponent* TargetMovement = Rig.Target->GetCharacterMovement())
			{
				TargetMovement->GravityScale = 0.0f;
				TargetMovement->SetMovementMode(MOVE_None);
			}

			Rig.Tree = NewObject<UStateTreeAIComponent>(Rig.Controller);
			Rig.Tree->SetStartLogicAutomatically(false);
			Rig.Controller->AddInstanceComponent(Rig.Tree);
			Rig.Tree->RegisterComponent();

			Rig.Weapon = NewObject<UDustEnemyWeaponComponent>(Rig.Pawn);
			Rig.Weapon->WeaponDefinition = Definition;
			Rig.Weapon->CombatProfile = MakeProfile();
			Rig.Pawn->AddInstanceComponent(Rig.Weapon);
			Rig.Weapon->RegisterComponent();
			Rig.Weapon->SetComponentTickEnabled(false);

			Rig.Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Rig.Controller);
			Rig.Controller->AddInstanceComponent(Rig.Adapter);
			Rig.Adapter->RegisterComponent();
			FDustEnemyReloadRetreatTestAccess::PrepareAdapter(
				Rig.Adapter, Rig.Controller, Rig.Pawn, Rig.Tree, Rig.Weapon, Rig.Target);

			Rig.Reload = NewObject<UDustEnemyReloadComponent>(Rig.Pawn);
			Rig.Reload->bUseInfiniteReserveAmmo = bInfiniteReserve;
			Rig.Reload->LogicalReloadSeconds = LogicalSeconds;
			Rig.Reload->FailureCooldownSeconds = 0.05f;
			Rig.Pawn->AddInstanceComponent(Rig.Reload);
			Rig.Reload->RegisterComponent();

			if (!Rig.Weapon->InitializeWeapon()) return FRig();
			return Rig;
		}

	private:
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadRetreatGateTest,
	"Dust2.EnemyReloadRetreat.EntryGateRequiresActiveReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadRetreatGateTest::RunTest(const FString& Parameters)
{
	FReloadRetreatTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload-retreat world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FReloadRetreatTestWorld::FRig Rig = Fixture.SpawnRig(Definition, false, 10.0f);
	if (!TestNotNull(TEXT("enemy pawn"), Rig.Pawn) || !TestNotNull(TEXT("reload component"), Rig.Reload)
		|| !TestNotNull(TEXT("weapon component"), Rig.Weapon) || !TestNotNull(TEXT("adapter"), Rig.Adapter)
		|| !TestNotNull(TEXT("player target"), Rig.Target)) return false;
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("weapon runtime"), Runtime)) return false;

	TestEqual(TEXT("fixture starts with an empty magazine"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("fixture keeps the finite reserve"), Runtime->GetReserveAmmo(), 30);

	// The reload-only entry must not be usable without an actual reload transaction.
	TestFalse(TEXT("no reload -> reload-retreat query is false"), Rig.Adapter->CanCreateSpaceForReload());
	TestTrue(TEXT("no reload -> reload-retreat begin returns no token"),
		Rig.Adapter->BeginCreateSpaceForReload() == 0);
	TestFalse(TEXT("no reload -> no spacing is active"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("no reload -> reload component stays idle"), Rig.Reload->IsBusy());
	TestFalse(TEXT("no reload -> runtime is not reloading"), Runtime->IsReloading());

	const uint64 ReloadToken = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("real reload transaction starts"), ReloadToken != 0)) return false;
	const int32 ReloadRequestId = Runtime->GetReloadRequestId();
	TestTrue(TEXT("reload stays busy for the gate checks"), Rig.Reload->IsBusy());
	TestTrue(TEXT("runtime is actually reloading"), Runtime->IsReloading());
	TestEqual(TEXT("reload-retreat entry explicitly allows an empty magazine"), Runtime->GetCurrentAmmo(), 0);

	// Far player: the trigger distance is not met.
	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(600.0f, 0.0f, 0.0f));
	TestFalse(TEXT("far confirmed player -> reload-retreat query is false"),
		Rig.Adapter->CanCreateSpaceForReload());

	// Close player: the trigger distance is met while the real reload is busy.
	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("close confirmed player -> reload-retreat query is true"),
		Rig.Adapter->CanCreateSpaceForReload());

	// Another spacing move already owns the adapter.
	FDustEnemyReloadRetreatTestAccess::InjectSpacing(Rig.Adapter, 501, 1500);
	TestTrue(TEXT("injected spacing is active"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("existing spacing -> reload-retreat query is false"),
		Rig.Adapter->CanCreateSpaceForReload());
	Rig.Adapter->CancelCreateSpace(501);
	TestFalse(TEXT("injected spacing is released"), Rig.Adapter->IsCreateSpaceActive());
	FDustEnemyReloadRetreatTestAccess::SetCooldown(Rig.Adapter, 0.0f);

	// Shared spacing cooldown still blocks the reload-only entry.
	FDustEnemyReloadRetreatTestAccess::SetCooldown(
		Rig.Adapter, Fixture.Get()->GetTimeSeconds() + 30.0f);
	TestFalse(TEXT("spacing cooldown -> reload-retreat query is false"),
		Rig.Adapter->CanCreateSpaceForReload());
	FDustEnemyReloadRetreatTestAccess::SetCooldown(Rig.Adapter, 0.0f);
	TestTrue(TEXT("cooldown clear -> reload-retreat query is true"),
		Rig.Adapter->CanCreateSpaceForReload());

	// The pure gate never touched the reload transaction or restarted shooting.
	TestTrue(TEXT("gate checks never cancel the reload"), Rig.Reload->IsBusy());
	TestTrue(TEXT("gate checks keep the runtime reloading"), Runtime->IsReloading());
	TestEqual(TEXT("gate checks keep the same request id"), Runtime->GetReloadRequestId(), ReloadRequestId);
	TestFalse(TEXT("gate checks never start an engagement"), Rig.Weapon->IsEngaging());
	Rig.Reload->CancelReload(ReloadToken);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadRetreatNoNavTest,
	"Dust2.EnemyReloadRetreat.NoNavDoesNotCancelOrRestartReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadRetreatNoNavTest::RunTest(const FString& Parameters)
{
	FReloadRetreatTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload-retreat world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FReloadRetreatTestWorld::FRig Rig = Fixture.SpawnRig(Definition, false, 10.0f);
	if (!Rig.Reload || !Rig.Weapon || !Rig.Adapter || !Rig.Target || !Rig.Pawn)
	{
		AddError(TEXT("reload-retreat rig missing"));
		return false;
	}
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("weapon runtime"), Runtime)) return false;

	FDustEnemyReloadTaskInstanceData Data;
	Data.ReloadComponent = Rig.Reload;
	if (!TestTrue(TEXT("task entry starts exactly one reload"), Data.StartOwnedReload())) return false;
	TestTrue(TEXT("task captured the controller adapter"), Data.StartedAdapter == Rig.Adapter);
	TestTrue(TEXT("task captured the reload component"), Data.StartedComponent == Rig.Reload);
	TestTrue(TEXT("task captured the weapon component"), Data.StartedWeaponComponent == Rig.Weapon);
	TestTrue(TEXT("task captured the weapon runtime"), Data.StartedRuntime.Get() == Runtime);
	const uint64 EntryReloadToken = Data.ReloadToken;
	const int32 EntryRequestId = Data.ReloadRequestId;
	if (!TestTrue(TEXT("task owns the reload token"), EntryReloadToken != 0)) return false;
	if (!TestTrue(TEXT("task owns a positive request id"), EntryRequestId > 0)) return false;
	TestTrue(TEXT("runtime is reloading"), Runtime->IsReloading());
	TestFalse(TEXT("entry did not start a retreat yet"), Data.bRetreatAttempted);
	TestEqual(TEXT("entry owns no retreat token"), Data.RetreatToken, uint64(0));

	// A far player must not consume the single retreat attempt.
	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(600.0f, 0.0f, 0.0f));
	TestTrue(TEXT("far tick keeps the reload running"),
		Data.TickOwnedReload(0.1f) == EStateTreeRunStatus::Running);
	TestFalse(TEXT("far player does not consume the retreat attempt"), Data.bRetreatAttempted);
	TestEqual(TEXT("far player starts no retreat token"), Data.RetreatToken, uint64(0));
	TestTrue(TEXT("far tick keeps the reload busy"), Rig.Reload->IsBusy());

	// The player approaches: the real reload is busy but this isolated world has no navigation.
	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("close tick keeps the reload running"),
		Data.TickOwnedReload(0.1f) == EStateTreeRunStatus::Running);
	TestTrue(TEXT("close player consumes the single retreat attempt"), Data.bRetreatAttempted);
	TestEqual(TEXT("no navigation means no retreat token"), Data.RetreatToken, uint64(0));
	TestTrue(TEXT("no-destination failure does not cancel the reload"), Rig.Reload->IsBusy());
	TestTrue(TEXT("no-destination failure keeps the runtime reloading"), Runtime->IsReloading());
	TestEqual(TEXT("no-destination failure keeps the reload token"), Data.ReloadToken, EntryReloadToken);
	TestEqual(TEXT("no-destination failure keeps the request id"),
		Runtime->GetReloadRequestId(), EntryRequestId);
	TestEqual(TEXT("no-destination failure commits no ammo"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("no-destination failure consumes no reserve"), Runtime->GetReserveAmmo(), 30);
	TestFalse(TEXT("no-destination failure starts no engagement"), Rig.Weapon->IsEngaging());

	TestTrue(TEXT("next tick keeps the reload running"),
		Data.TickOwnedReload(0.1f) == EStateTreeRunStatus::Running);
	TestTrue(TEXT("failed attempt is not retried every tick"), Data.bRetreatAttempted);
	TestEqual(TEXT("failed attempt still owns no retreat token"), Data.RetreatToken, uint64(0));
	TestTrue(TEXT("failed retreat did not restart the reload"), Rig.Reload->IsBusy());

	TestTrue(TEXT("finite reload still completes after the failed retreat"),
		Data.TickOwnedReload(10.0f) == EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("finite transaction commits the full magazine"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("finite transaction consumes the reserve once"), Runtime->GetReserveAmmo(), 0);
	TestFalse(TEXT("completion releases reload ownership"), Rig.Reload->IsBusy());
	TestFalse(TEXT("completion leaves the runtime reload state"), Runtime->IsReloading());
	TestFalse(TEXT("completion restarts no shooting"), Rig.Weapon->IsEngaging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadRetreatCompletionTest,
	"Dust2.EnemyReloadRetreat.CompletionCancelsOwnedRetreat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadRetreatCompletionTest::RunTest(const FString& Parameters)
{
	FReloadRetreatTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload-retreat world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FReloadRetreatTestWorld::FRig Rig = Fixture.SpawnRig(Definition, false, 10.0f);
	if (!Rig.Reload || !Rig.Weapon || !Rig.Adapter || !Rig.Target || !Rig.Pawn)
	{
		AddError(TEXT("reload-retreat rig missing"));
		return false;
	}
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("weapon runtime"), Runtime)) return false;

	FDustEnemyReloadTaskInstanceData Data;
	Data.ReloadComponent = Rig.Reload;
	if (!TestTrue(TEXT("task entry starts the reload"), Data.StartOwnedReload())) return false;

	// Bind a live spacing token to the entry. This asserts release semantics, not a real path.
	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(100.0f, 0.0f, 0.0f));
	FDustEnemyReloadRetreatTestAccess::InjectSpacing(Rig.Adapter, 701, 1600);
	Data.RetreatToken = 701;
	TestTrue(TEXT("retreat token is active before completion"), Rig.Adapter->IsCreateSpaceActive());

	TestTrue(TEXT("reload completion wins the tick"),
		Data.TickOwnedReload(10.0f) == EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("completion commits the magazine once"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("completion consumes the reserve once"), Runtime->GetReserveAmmo(), 0);
	TestEqual(TEXT("completion clears the retreat token"), Data.RetreatToken, uint64(0));
	TestFalse(TEXT("completion releases the owned spacing"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("completion does not extend the reloading state"), Rig.Reload->IsBusy());
	TestFalse(TEXT("completion restarts no shooting"), Rig.Weapon->IsEngaging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadRetreatStaleCleanupTest,
	"Dust2.EnemyReloadRetreat.StaleCleanupProtectsNewRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadRetreatStaleCleanupTest::RunTest(const FString& Parameters)
{
	FReloadRetreatTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload-retreat world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FReloadRetreatTestWorld::FRig Rig = Fixture.SpawnRig(Definition, false, 10.0f);
	if (!Rig.Reload || !Rig.Weapon || !Rig.Adapter || !Rig.Target || !Rig.Pawn)
	{
		AddError(TEXT("reload-retreat rig missing"));
		return false;
	}
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("weapon runtime"), Runtime)) return false;

	FDustEnemyReloadTaskInstanceData Data;
	Data.ReloadComponent = Rig.Reload;
	if (!TestTrue(TEXT("old entry starts its reload"), Data.StartOwnedReload())) return false;
	const uint64 OldReloadToken = Data.ReloadToken;
	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(100.0f, 0.0f, 0.0f));
	FDustEnemyReloadRetreatTestAccess::InjectSpacing(Rig.Adapter, 801, 1700);
	Data.RetreatToken = 801;
	FDustEnemyReloadTaskInstanceData OldEntry = Data;

	// A newer move replaces the old token before the old entry is cleaned up.
	FDustEnemyReloadRetreatTestAccess::InjectSpacing(Rig.Adapter, 802, 1701);
	Data.CancelOwnedReloadAndRetreat();
	TestFalse(TEXT("old cleanup cancels its own reload"), Rig.Reload->IsBusy());
	TestTrue(TEXT("newer move survives the old cleanup"), Rig.Adapter->IsCreateSpaceActive());
	TestEqual(TEXT("newer move token is preserved"),
		FDustEnemyReloadRetreatTestAccess::ActiveSpacingToken(Rig.Adapter), uint64(802));

	// Start a newer reload after the original cancel cooldown.
	Fixture.Advance(0.1f);
	FDustEnemyReloadTaskInstanceData NewData;
	NewData.ReloadComponent = Rig.Reload;
	if (!TestTrue(TEXT("new entry starts a newer reload"), NewData.StartOwnedReload())) return false;
	TestTrue(TEXT("newer reload token differs"), NewData.ReloadToken != OldReloadToken);
	NewData.RetreatToken = 802;

	// The old entry's late Exit must not touch either newer request.
	OldEntry.CancelOwnedReloadAndRetreat();
	TestTrue(TEXT("old Exit preserves the newer move"), Rig.Adapter->IsCreateSpaceActive());
	TestTrue(TEXT("old Exit preserves the newer reload"), Rig.Reload->IsBusy());
	TestTrue(TEXT("old Exit keeps the newer runtime reload"), Runtime->IsReloading());
	TestEqual(TEXT("old Exit keeps the newer request id"),
		Runtime->GetReloadRequestId(), NewData.ReloadRequestId);
	TestEqual(TEXT("old Exit keeps the newer move token"),
		FDustEnemyReloadRetreatTestAccess::ActiveSpacingToken(Rig.Adapter), uint64(802));

	// The original lifecycle Stop still cancels both requests that this pawn owns.
	Rig.Adapter->StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
	TestFalse(TEXT("death cancels the owned move"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("death cancels the owned reload"), Rig.Reload->IsBusy());
	TestFalse(TEXT("death leaves the runtime idle"), Runtime->IsReloading());
	TestEqual(TEXT("death commits no ammo"), Runtime->GetCurrentAmmo(), 0);
	NewData.CancelOwnedReloadAndRetreat();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadRetreatLostConfirmationTest,
	"Dust2.EnemyReloadRetreat.LostConfirmationReleasesMoveAndKeepsReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadRetreatLostConfirmationTest::RunTest(const FString& Parameters)
{
	FReloadRetreatTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload-retreat world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FReloadRetreatTestWorld::FRig Rig = Fixture.SpawnRig(Definition, false, 10.0f);
	if (!Rig.Reload || !Rig.Weapon || !Rig.Adapter || !Rig.Target || !Rig.Pawn)
	{
		AddError(TEXT("reload-retreat rig missing"));
		return false;
	}
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("weapon runtime"), Runtime)) return false;

	FDustEnemyReloadTaskInstanceData Data;
	Data.ReloadComponent = Rig.Reload;
	if (!TestTrue(TEXT("task entry starts the reload"), Data.StartOwnedReload())) return false;
	const int32 ReloadRequestId = Data.ReloadRequestId;
	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(100.0f, 0.0f, 0.0f));
	FDustEnemyReloadRetreatTestAccess::InjectSpacing(Rig.Adapter, 702, 1602);
	Data.RetreatToken = 702;
	TestTrue(TEXT("retreat token is active before the confirmation loss"),
		Rig.Adapter->IsCreateSpaceActive());

	// The real adapter gate drops confirmation; UpdateCreateSpace releases the move while the
	// reload transaction must keep running.
	FDustEnemyReloadRetreatTestAccess::SetUnconfirmed(Rig.Adapter, Rig.Target);
	TestFalse(TEXT("unconfirmed target fails the reload-retreat gate"),
		Rig.Adapter->CanCreateSpaceForReload());
	TestTrue(TEXT("lost-confirmation tick keeps the reload running"),
		Data.TickOwnedReload(0.1f) == EStateTreeRunStatus::Running);
	TestEqual(TEXT("lost confirmation clears the owned retreat token"), Data.RetreatToken, uint64(0));
	TestFalse(TEXT("lost confirmation releases the move"), Rig.Adapter->IsCreateSpaceActive());
	TestTrue(TEXT("lost confirmation does not cancel the reload"), Rig.Reload->IsBusy());
	TestTrue(TEXT("lost confirmation keeps the runtime reloading"), Runtime->IsReloading());
	TestEqual(TEXT("lost confirmation keeps the reload request id"),
		Runtime->GetReloadRequestId(), ReloadRequestId);
	TestEqual(TEXT("lost confirmation commits no ammo"), Runtime->GetCurrentAmmo(), 0);

	TestTrue(TEXT("reload still completes after the move was released"),
		Data.TickOwnedReload(10.0f) == EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("reload commits the magazine once"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("reload consumes the reserve once"), Runtime->GetReserveAmmo(), 0);
	TestFalse(TEXT("completion restarts no shooting"), Rig.Weapon->IsEngaging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadRetreatInfiniteReserveTest,
	"Dust2.EnemyReloadRetreat.InfiniteReserveTransactionUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadRetreatInfiniteReserveTest::RunTest(const FString& Parameters)
{
	FReloadRetreatTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload-retreat world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0, 30);
	FReloadRetreatTestWorld::FRig Rig = Fixture.SpawnRig(Definition, true, 10.0f);
	if (!Rig.Reload || !Rig.Weapon || !Rig.Adapter || !Rig.Target || !Rig.Pawn)
	{
		AddError(TEXT("infinite-reserve rig missing"));
		return false;
	}
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("weapon runtime"), Runtime)) return false;
	TestTrue(TEXT("infinite-reserve empty magazine qualifies"), Rig.Reload->CanReload());

	FDustEnemyReloadTaskInstanceData Data;
	Data.ReloadComponent = Rig.Reload;
	if (!TestTrue(TEXT("infinite-reserve task entry starts"), Data.StartOwnedReload())) return false;

	FDustEnemyReloadRetreatTestAccess::SetConfirmedTargetLocation(
		Rig.Adapter, Rig.Target, FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("infinite-reserve tick keeps the reload running"),
		Data.TickOwnedReload(0.1f) == EStateTreeRunStatus::Running);
	TestTrue(TEXT("infinite-reserve approach consumes the attempt"), Data.bRetreatAttempted);
	TestEqual(TEXT("no navigation means no retreat token"), Data.RetreatToken, uint64(0));
	TestTrue(TEXT("infinite-reserve reload survives the failed retreat"), Rig.Reload->IsBusy());
	TestEqual(TEXT("infinite-reserve still has an empty magazine"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("infinite-reserve creates no reserve"), Runtime->GetReserveAmmo(), 0);

	TestTrue(TEXT("infinite-reserve reload still completes"),
		Data.TickOwnedReload(10.0f) == EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("infinite-reserve fills the whole magazine"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("infinite-reserve never invents reserve ammo"), Runtime->GetReserveAmmo(), 0);
	TestFalse(TEXT("infinite-reserve completion releases reload ownership"), Rig.Reload->IsBusy());
	TestFalse(TEXT("infinite-reserve completion restarts no shooting"), Rig.Weapon->IsEngaging());
	return true;
}

#endif
