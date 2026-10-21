#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "Components/StateTreeAIComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyReloadComponent.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Sight.h"
#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponPresentationData.h"
#include "WeaponRuntime.h"

// The adapter is normally initialized by BP/StateTree startup; this seam exposes only its
// existing private bindings/entry points so reload + deferred sight loss can be exercised
// without a StateTree asset. Production gates are unchanged.
struct FDustEnemyReloadTestAccess
{
	static void PrepareAdapter(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
		APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon, AActor* Target)
	{
		Adapter->Controller = Controller;
		Adapter->BoundPawn = Pawn;
		Adapter->Tree = Tree;
		Adapter->WeaponComponent = Weapon;
		Adapter->TargetClass = Target ? Target->GetClass() : nullptr;
		Adapter->Target = Target;
		Adapter->bSightCurrentlySensed = true;
		Adapter->bConfirmedTarget = true;
		Adapter->LastKnownLocation = Target ? Target->GetActorLocation() : FVector::ZeroVector;
		Adapter->LastConfirmedTime = Adapter->GetWorld() ? Adapter->GetWorld()->GetTimeSeconds() : 0.0f;
		Adapter->bInitialized = true;
	}

	static void NotifySightLoss(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor, const FVector& Location)
	{
		FAIStimulus Stimulus;
		Stimulus.Type = UAISense::GetSenseID<UAISense_Sight>();
		Stimulus.StimulusLocation = Location;
		Stimulus.MarkNoLongerSensed();
		Adapter->HandlePerception(Actor, Stimulus);
	}

	static void AcquireKnownTarget(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor, const FVector& Location)
	{
		Adapter->Acquire(Actor, Location);
	}

	// White-box only: force the two montage failure branches without a real skeletal mesh/AnimInstance.
	static void ForceMontageInterrupted(UDustEnemyReloadComponent* Reload)
	{
		Reload->bMontageMode = true;
		Reload->bMontageInterrupted = true;
		Reload->bWaitingForMontageCommit = false;
		Reload->WatchdogSeconds = 1000.0f;
	}
	static void ForceWatchdogExpired(UDustEnemyReloadComponent* Reload)
	{
		Reload->bMontageMode = true;
		Reload->bMontageInterrupted = false;
		Reload->bWaitingForMontageCommit = false;
		Reload->ReloadElapsedSeconds = 0.0f;
		Reload->WatchdogSeconds = 0.0f;
	}

	// White-box only: simulate a captured montage pair after a logic-mode start. This verifies
	// capture/identity semantics, not actual skeletal montage playback.
	static void ForceCapturedMontageMode(UDustEnemyReloadComponent* Reload, UAnimMontage* Character, UAnimMontage* Weapon)
	{
		Reload->ActiveCharacterMontage = Character;
		Reload->ActiveWeaponMontage = Weapon;
		Reload->bHasCharacterMontage = Character != nullptr;
		Reload->bHasWeaponMontage = Weapon != nullptr;
		Reload->bCharacterMontageDone = Character == nullptr;
		Reload->bWeaponMontageDone = Weapon == nullptr;
		Reload->bMontageMode = true;
		Reload->bMontageInterrupted = false;
		Reload->bWaitingForMontageCommit = false;
		Reload->WatchdogSeconds = 1000.0f;
	}

	static void NotifyCharacterMontageEnded(UDustEnemyReloadComponent* Reload, UAnimMontage* Montage,
		bool bInterrupted, uint64 Token)
	{
		Reload->HandleCharacterMontageEnded(Montage, bInterrupted, Token);
	}

	static void NotifyWeaponMontageEnded(UDustEnemyReloadComponent* Reload, UAnimMontage* Montage,
		bool bInterrupted, uint64 Token)
	{
		Reload->HandleWeaponMontageEnded(Montage, bInterrupted, Token);
	}

	static int32 GetActiveReloadAmount(const UDustEnemyReloadComponent* Reload) { return Reload->ActiveReloadAmount; }
	static bool UsesEmptyBatch(const UDustEnemyReloadComponent* Reload) { return Reload->bActiveReloadUsesEmptyBatch; }
	static bool UsesInfiniteSource(const UDustEnemyReloadComponent* Reload) { return Reload->bActiveReloadUsesInfiniteSource; }
	static UAnimMontage* GetActiveCharacterMontage(const UDustEnemyReloadComponent* Reload) { return Reload->ActiveCharacterMontage.Get(); }
	static UAnimMontage* GetActiveWeaponMontage(const UDustEnemyReloadComponent* Reload) { return Reload->ActiveWeaponMontage.Get(); }
};

namespace
{
	class FScopedReloadTestWorld
	{
	public:
		FScopedReloadTestWorld()
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

		~FScopedReloadTestWorld()
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

		UWeaponDefinitionData* MakeDefinition(int32 InitialAmmo, int32 ReserveAmmo, bool bPerRound = false,
			bool bEmptyBatch = false, int32 InMagazineCapacity = 30) const
		{
			UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
			UWeaponBallisticsData* Ballistics = NewObject<UWeaponBallisticsData>(Definition);
			Ballistics->DamagePerShot = 10.0f;
			Ballistics->ArmorPenetration = 0.0f;
			Ballistics->HeadDamageMultiplier = 2.0f;
			Ballistics->TraceDistance = 2000.0f;

			Definition->BallisticsData = Ballistics;
			Definition->ConfigurationVersion = 1;
			Definition->ReloadStyle = bEmptyBatch ? EWeaponReloadStyle::PerRoundOrEmptyBatch
				: (bPerRound ? EWeaponReloadStyle::PerRound : EWeaponReloadStyle::Magazine);
			Definition->MagazineCapacity = FMath::Max(1, InMagazineCapacity);
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

		/** Test asset created in-memory; no shared weapon definition is modified. */
		UWeaponPresentationData* AttachEnemyFixedPresentation(UWeaponDefinitionData* Definition,
			UAnimMontage* CharacterMontage = nullptr, UAnimMontage* WeaponMontage = nullptr) const
		{
			if (!Definition) return nullptr;
			UWeaponPresentationData* Presentation = NewObject<UWeaponPresentationData>(Definition);
			Presentation->bEnableEnemyProfile = true;
			Presentation->EnemyPresentation.VisualWeaponClass = AActor::StaticClass();
			Presentation->EnemyPresentation.CharacterReloadEmptyMontage = CharacterMontage;
			Presentation->EnemyPresentation.ReloadEmptyMontage = WeaponMontage;
			Definition->PresentationData = Presentation;
			return Presentation;
		}

		struct FRig
		{
			APawn* Pawn = nullptr;
			AAIController* Controller = nullptr;
			UStateTreeAIComponent* Tree = nullptr;
			UDustEnemyWeaponComponent* Weapon = nullptr;
			UDustEnemyCombatAdapterComponent* Adapter = nullptr;
			UDustEnemyReloadComponent* Reload = nullptr;
			ADust2PlayerCharacter* Target = nullptr;
		};

		FRig SpawnRig(UWeaponDefinitionData* Definition, float LogicalSeconds = 0.2f, float CooldownSeconds = 0.05f,
			UAnimMontage* CharacterMontage = nullptr, bool bInfiniteReserve = false) const
		{
			FRig Rig;
			if (!World || !Definition) return Rig;

			Rig.Controller = World->SpawnActor<AAIController>();
			Rig.Pawn = World->SpawnActor<APawn>();
			Rig.Target = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Rig.Controller || !Rig.Pawn || !Rig.Target) return Rig;
			Rig.Controller->Possess(Rig.Pawn);
			// Keep the target outside the close-range exception so a deferred reload loss really needs
			// the last-known-location search; this fixture does not model player geometry.
			Rig.Target->SetActorLocation(FVector(600.0f, 0.0f, 0.0f));

			Rig.Tree = NewObject<UStateTreeAIComponent>(Rig.Controller);
			Rig.Tree->SetStartLogicAutomatically(false);
			Rig.Controller->AddInstanceComponent(Rig.Tree);
			Rig.Tree->RegisterComponent();

			Rig.Weapon = NewObject<UDustEnemyWeaponComponent>(Rig.Pawn);
			Rig.Weapon->WeaponDefinition = Definition;
			Rig.Pawn->AddInstanceComponent(Rig.Weapon);
			Rig.Weapon->RegisterComponent();
			Rig.Weapon->SetComponentTickEnabled(false);

			Rig.Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Rig.Controller);
			Rig.Controller->AddInstanceComponent(Rig.Adapter);
			Rig.Adapter->RegisterComponent();
			FDustEnemyReloadTestAccess::PrepareAdapter(Rig.Adapter, Rig.Controller, Rig.Pawn, Rig.Tree, Rig.Weapon, Rig.Target);

			Rig.Reload = NewObject<UDustEnemyReloadComponent>(Rig.Pawn);
			// Legacy fixtures explicitly keep the original finite-reserve rules. Only the new
			// infinite tests opt in, so the old assertions remain unchanged.
			Rig.Reload->bUseInfiniteReserveAmmo = bInfiniteReserve;
			Rig.Reload->LogicalReloadSeconds = LogicalSeconds;
			Rig.Reload->FailureCooldownSeconds = CooldownSeconds;
			Rig.Reload->CharacterReloadMontage = CharacterMontage;
			Rig.Pawn->AddInstanceComponent(Rig.Reload);
			Rig.Reload->RegisterComponent();

			if (!Rig.Weapon->InitializeWeapon()) return FRig();
			return Rig;
		}

	private:
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadCommitTest,
	"Dust2.EnemyReload.LimitedReserveCommitOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadCommitTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload test world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
	if (!TestNotNull(TEXT("enemy pawn"), Rig.Pawn) || !TestNotNull(TEXT("reload component"), Rig.Reload)
		|| !TestNotNull(TEXT("weapon component"), Rig.Weapon)) return false;
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("private weapon runtime"), Runtime)) return false;

	// AK105-style regression: ordinary Magazine definitions keep the original empty-magazine
	// fixed/logic path and are neither per-round nor empty-batch.
	TestFalse(TEXT("magazine style is not per-round"), Definition->UsesPerRoundReload());
	TestFalse(TEXT("magazine style is not empty-batch"), Definition->UsesEmptyBatchReload());
	TestNull(TEXT("magazine style needs no fixed presentation action"), Rig.Reload->CharacterReloadMontage.Get());

	TestEqual(TEXT("starts with an empty magazine"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("starts with the configured finite reserve"), Runtime->GetReserveAmmo(), 30);
	TestTrue(TEXT("empty magazine + reserve + batch reload qualifies"), Rig.Reload->CanReload());
	TestTrue(TEXT("no cooldown before any failure"), Rig.Reload->IsReloadReady());

	const uint64 Token = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("reload transaction starts with a token"), Token != 0)) return false;
	TestTrue(TEXT("runtime enters its real reload state"), Runtime->IsReloading());
	TestTrue(TEXT("component owns a busy transaction"), Rig.Reload->IsBusy());
	TestFalse(TEXT("busy transaction cannot be replaced by a second start"), Rig.Reload->TryStartReload() != 0);
	TestEqual(TEXT("unfinished reload does not refill the magazine"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("unfinished reload does not consume reserve"), Runtime->GetReserveAmmo(), 30);

	TestTrue(TEXT("partial time keeps running"),
		Rig.Reload->UpdateReload(Token, 0.1f) == EDustEnemyReloadStatus::Running);
	TestEqual(TEXT("still no ammo before completion"), Runtime->GetCurrentAmmo(), 0);

	TestTrue(TEXT("logic timer completes only after its configured duration"),
		Rig.Reload->UpdateReload(Token, 0.2f) == EDustEnemyReloadStatus::Succeeded);
	TestEqual(TEXT("completion is not a commit by itself"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("completion does not consume reserve by itself"), Runtime->GetReserveAmmo(), 30);

	TestTrue(TEXT("finish commits the reload exactly once"), Rig.Reload->FinishReload(Token));
	TestEqual(TEXT("magazine is filled from limited reserve"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("reserve is deducted once"), Runtime->GetReserveAmmo(), 0);
	TestFalse(TEXT("commit releases the busy transaction"), Rig.Reload->IsBusy());
	TestFalse(TEXT("runtime left its reload state"), Runtime->IsReloading());
	TestFalse(TEXT("full magazine no longer qualifies"), Rig.Reload->CanReload());

	TestFalse(TEXT("the same token cannot commit a second time"), Rig.Reload->FinishReload(Token));
	TestEqual(TEXT("second commit call cannot duplicate ammo"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("second commit call cannot overdraw reserve"), Runtime->GetReserveAmmo(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadCancelTest,
	"Dust2.EnemyReload.CancelNoAmmoAndCooldown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadCancelTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload test world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
	if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("rig missing")); return false; }
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("private weapon runtime"), Runtime)) return false;

	const uint64 Token = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("transaction starts"), Token != 0)) return false;
	TestTrue(TEXT("partial progress"), Rig.Reload->UpdateReload(Token, 0.1f) == EDustEnemyReloadStatus::Running);

	Rig.Reload->CancelReload(Token);
	TestFalse(TEXT("cancel stops the runtime reload"), Runtime->IsReloading());
	TestEqual(TEXT("cancel never refills ammo"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("cancel never consumes reserve"), Runtime->GetReserveAmmo(), 30);
	TestFalse(TEXT("cancel releases busy"), Rig.Reload->IsBusy());
	TestFalse(TEXT("cancel applies failure cooldown"), Rig.Reload->IsReloadReady());
	TestTrue(TEXT("zero-delay restart is blocked by cooldown"), Rig.Reload->TryStartReload() == 0);

	Fixture.Advance(0.1f);
	TestTrue(TEXT("cooldown expires on world time"), Rig.Reload->IsReloadReady());
	const uint64 NextToken = Rig.Reload->TryStartReload();
	TestTrue(TEXT("a new transaction is allowed after cooldown"), NextToken != 0);
	if (NextToken != 0) Rig.Reload->CancelReload(NextToken);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadStaleIdentityTest,
	"Dust2.EnemyReload.StaleTokenAndRequestCannotTouchNewTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadStaleIdentityTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload test world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
	if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("rig missing")); return false; }
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("private weapon runtime"), Runtime)) return false;

	const uint64 OldToken = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("first transaction starts"), OldToken != 0)) return false;
	const int32 OldRequest = Runtime->GetReloadRequestId();
	Rig.Reload->CancelReload(OldToken);
	Fixture.Advance(0.1f);

	const uint64 NewToken = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("second transaction starts"), NewToken != 0)) return false;
	const int32 NewRequest = Runtime->GetReloadRequestId();
	TestTrue(TEXT("runtime request id advances"), NewRequest != OldRequest);

	TestFalse(TEXT("old token cannot commit the new transaction"), Rig.Reload->FinishReload(OldToken));
	TestTrue(TEXT("old token commit leaves the new transaction running"), Runtime->IsReloading());
	Rig.Reload->CancelReload(OldToken);
	TestTrue(TEXT("old token cannot cancel the new transaction"), Runtime->IsReloading());
	TestTrue(TEXT("component still owns the new transaction"), Rig.Reload->IsBusy());

	// Simulate a newer external request: component now stores a stale request id.
	Runtime->CancelReload();
	if (!TestTrue(TEXT("external newer transaction starts"), Runtime->TryStartReload())) return false;
	const int32 ExternalRequest = Runtime->GetReloadRequestId();
	TestTrue(TEXT("external request is newer than the task's"), ExternalRequest != NewRequest);

	TestTrue(TEXT("stale request update fails"), Rig.Reload->UpdateReload(NewToken, 0.3f) == EDustEnemyReloadStatus::Failed);
	TestTrue(TEXT("stale request does not cancel the newer runtime reload"), Runtime->IsReloading());
	TestEqual(TEXT("newer runtime request id is preserved"), Runtime->GetReloadRequestId(), ExternalRequest);
	TestEqual(TEXT("no ammo was committed by the stale request"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("reserve is untouched by the stale request"), Runtime->GetReserveAmmo(), 30);
	TestFalse(TEXT("stale ownership is released"), Rig.Reload->IsBusy());
	Runtime->CancelReload();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadRejectTest,
	"Dust2.EnemyReload.RejectsZeroReserveAndPerRoundStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadRejectTest::RunTest(const FString& Parameters)
{
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("zero reserve world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("zero reserve rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("zero reserve runtime"), Runtime)) return false;

		TestFalse(TEXT("empty magazine without reserve cannot reload"), Rig.Reload->CanReload());
		TestTrue(TEXT("zero reserve start is rejected"), Rig.Reload->TryStartReload() == 0);
		TestFalse(TEXT("zero reserve never starts a runtime reload"), Runtime->IsReloading());
	}

	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("per-round world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30, true);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("per-round rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("per-round runtime"), Runtime)) return false;

		TestTrue(TEXT("definition reports per-round reload"), Definition->UsesPerRoundReload());
		TestFalse(TEXT("per-round weapon is protected by the original Empty behavior"), Rig.Reload->CanReload());
		TestTrue(TEXT("per-round start is rejected"), Rig.Reload->TryStartReload() == 0);
		TestFalse(TEXT("per-round style never starts a batch reload"), Runtime->IsReloading());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadMontageSourceTest,
	"Dust2.EnemyReload.MissingMontageSourceFailsWithCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadMontageSourceTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload test world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
	FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, Montage);
	if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("rig missing")); return false; }
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("private weapon runtime"), Runtime)) return false;

	// No SetReloadAnimationSources call: a configured montage must fail, not use the logic timer.
	TestTrue(TEXT("weapon itself qualifies by ammo"), Rig.Reload->CanReload());
	TestTrue(TEXT("configured montage without mesh fails start"), Rig.Reload->TryStartReload() == 0);
	TestFalse(TEXT("no runtime reload is left behind"), Runtime->IsReloading());
	TestEqual(TEXT("no ammo change on failed start"), Runtime->GetCurrentAmmo(), 0);
	TestFalse(TEXT("failed start applies cooldown"), Rig.Reload->IsReloadReady());
	TestTrue(TEXT("immediate retry is blocked"), Rig.Reload->TryStartReload() == 0);

	Fixture.Advance(0.1f);
	TestTrue(TEXT("failure cooldown expires"), Rig.Reload->IsReloadReady());
	TestTrue(TEXT("missing mesh still fails after cooldown"), Rig.Reload->TryStartReload() == 0);
	TestFalse(TEXT("still no runtime reload"), Runtime->IsReloading());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadStopTest,
	"Dust2.EnemyReload.StopCancelsOwned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadStopTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload test world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
	if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("rig missing")); return false; }
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("private weapon runtime"), Runtime)) return false;

	const uint64 Token = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("transaction starts for stop test"), Token != 0)) return false;
	TestTrue(TEXT("stop path cancels a busy reload"), Runtime->IsReloading());

	Rig.Reload->StopAllOperations();
	TestFalse(TEXT("stop cancels the runtime reload"), Runtime->IsReloading());
	TestEqual(TEXT("stop never commits ammo"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("stop never consumes reserve"), Runtime->GetReserveAmmo(), 30);
	TestFalse(TEXT("stop releases busy"), Rig.Reload->IsBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadDeferredSightLossTest,
	"Dust2.EnemyReload.DeferredSightLossWhileReloading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadDeferredSightLossTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload test world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f);
	if (!Rig.Reload || !Rig.Adapter || !Rig.Weapon || !Rig.Target)
	{
		AddError(TEXT("rig missing"));
		return false;
	}
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("private weapon runtime"), Runtime)) return false;
	TestTrue(TEXT("legal known target is present"), Rig.Adapter->GetTarget() == Rig.Target);

	const uint64 Token = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("transaction starts"), Token != 0)) return false;

	// Sight is lost because the enemy itself is retreating/reloading: defer, keep the known target.
	FDustEnemyReloadTestAccess::NotifySightLoss(Rig.Adapter, Rig.Target, FVector(250.0f, 0.0f, 0.0f));
	TestTrue(TEXT("reload-driven sight loss is deferred"), Rig.Adapter->HasDeferredReloadSightLoss());
	TestTrue(TEXT("repeated initialization preserves this life"), Rig.Adapter->InitializeCombatAI(Rig.Weapon));
	TestTrue(TEXT("repeated initialization preserves pending search"), Rig.Adapter->HasDeferredReloadSightLoss());
	TestTrue(TEXT("known target object is retained during deferral"), Rig.Adapter->GetTarget() == Rig.Target);
	TestFalse(TEXT("no search starts while the reload is busy"), Rig.Adapter->IsSearching());
	TestTrue(TEXT("reload transaction is still active"), Rig.Reload->IsBusy());
	TestTrue(TEXT("deferred loss does not fail the reload update"),
		Rig.Reload->UpdateReload(Token, 0.05f) == EDustEnemyReloadStatus::Running);
	TestTrue(TEXT("runtime keeps reloading through the deferred sight loss"), Runtime->IsReloading());

	// Re-sensing the same target clears the deferred flag.
	FDustEnemyReloadTestAccess::AcquireKnownTarget(Rig.Adapter, Rig.Target, FVector(250.0f, 0.0f, 0.0f));
	TestFalse(TEXT("re-sensing the same target clears the deferred flag"), Rig.Adapter->HasDeferredReloadSightLoss());

	// Lose sight again, finish the transaction, then the adapter tick must hand off to the original search.
	FDustEnemyReloadTestAccess::NotifySightLoss(Rig.Adapter, Rig.Target, FVector(250.0f, 0.0f, 0.0f));
	TestTrue(TEXT("second loss deferred"), Rig.Adapter->HasDeferredReloadSightLoss());
	TestTrue(TEXT("reload completes while sight is deferred"),
		Rig.Reload->UpdateReload(Token, 0.3f) == EDustEnemyReloadStatus::Succeeded);
	TestTrue(TEXT("deferred reload still commits ammo"), Rig.Reload->FinishReload(Token));
	TestEqual(TEXT("deferred reload commits the magazine once"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("deferred reload deducts reserve once"), Runtime->GetReserveAmmo(), 0);
	TestFalse(TEXT("finish releases busy"), Rig.Reload->IsBusy());
	TestTrue(TEXT("deferral survives until the adapter tick"), Rig.Adapter->HasDeferredReloadSightLoss());

	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("adapter tick consumes the deferred loss"), Rig.Adapter->HasDeferredReloadSightLoss());
	TestTrue(TEXT("adapter tick starts the original last-known-location search"), Rig.Adapter->IsSearching());
	TestNull(TEXT("search hands off from the known target"), Rig.Adapter->GetTarget());
	Rig.Adapter->StopCombatAI(EDustEnemyAIStopReason::MissionEnd);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyReloadMontageAbortTest,
	"Dust2.EnemyReload.MontageInterruptAndWatchdogCancelOwnRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyReloadMontageAbortTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated reload test world"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 30);
	FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f);
	if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("rig missing")); return false; }
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("private weapon runtime"), Runtime)) return false;

	// Montage-path interruption must cancel our runtime transaction, not leave it stuck reloading.
	const uint64 InterruptToken = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("interrupt case starts"), InterruptToken != 0)) return false;
	FDustEnemyReloadTestAccess::ForceMontageInterrupted(Rig.Reload);
	TestTrue(TEXT("interrupted montage fails the update"),
		Rig.Reload->UpdateReload(InterruptToken, 0.01f) == EDustEnemyReloadStatus::Failed);
	TestFalse(TEXT("interruption cancels the same runtime request"), Runtime->IsReloading());
	TestEqual(TEXT("interruption never commits ammo"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("interruption never consumes reserve"), Runtime->GetReserveAmmo(), 30);
	TestFalse(TEXT("interruption releases busy"), Rig.Reload->IsBusy());
	TestFalse(TEXT("interruption applies cooldown"), Rig.Reload->IsReloadReady());

	Fixture.Advance(0.1f);
	// Watchdog expiry follows the same cancel-only rule.
	const uint64 WatchdogToken = Rig.Reload->TryStartReload();
	if (!TestTrue(TEXT("watchdog case starts after cooldown"), WatchdogToken != 0)) return false;
	FDustEnemyReloadTestAccess::ForceWatchdogExpired(Rig.Reload);
	TestTrue(TEXT("expired watchdog fails the update"),
		Rig.Reload->UpdateReload(WatchdogToken, 0.01f) == EDustEnemyReloadStatus::Failed);
	TestFalse(TEXT("watchdog cancels the same runtime request"), Runtime->IsReloading());
	TestEqual(TEXT("watchdog never commits ammo"), Runtime->GetCurrentAmmo(), 0);
	TestEqual(TEXT("watchdog never consumes reserve"), Runtime->GetReserveAmmo(), 30);
	TestFalse(TEXT("watchdog releases busy"), Rig.Reload->IsBusy());

	Fixture.Advance(0.1f);
	TestTrue(TEXT("cooldown after watchdog ends"), Rig.Reload->IsReloadReady());
	const uint64 RetryToken = Rig.Reload->TryStartReload();
	TestTrue(TEXT("a fresh transaction is allowed after cancellation"), RetryToken != 0);
	if (RetryToken != 0) Rig.Reload->CancelReload(RetryToken);
	TestEqual(TEXT("retry cancellation still leaves ammo untouched"), Runtime->GetCurrentAmmo(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyFixedReloadAmountTest,
	"Dust2.EnemyReload.FixedActionBatchAmounts8_3_1CommitWhenComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyFixedReloadAmountTest::RunTest(const FString& Parameters)
{
	for (int32 Reserve : { 8, 3, 1 })
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("fixed reload test world"), Fixture.Get())) return false;

		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, Reserve, false, true, 8);
		UAnimMontage* FixedCharacter = NewObject<UAnimMontage>(GetTransientPackage());
		UAnimMontage* FixedWeapon = NewObject<UAnimMontage>(GetTransientPackage());
		UWeaponPresentationData* Presentation = Fixture.AttachEnemyFixedPresentation(
			Definition, FixedCharacter, FixedWeapon);
		if (!TestNotNull(TEXT("fixed presentation asset"), Presentation)) return false;
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!TestNotNull(TEXT("fixed rig pawn"), Rig.Pawn) || !TestNotNull(TEXT("fixed rig reload"), Rig.Reload)
			|| !TestNotNull(TEXT("fixed rig weapon"), Rig.Weapon)) return false;
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("fixed runtime"), Runtime)) return false;

		TestTrue(TEXT("fixed profile applies through the existing guard"), Rig.Weapon->ApplyReloadPresentationFromProfile());
		TestTrue(TEXT("character action is the fixed profile action"), Rig.Reload->CharacterReloadMontage == FixedCharacter);
		TestTrue(TEXT("weapon action is the fixed profile action"), Rig.Reload->WeaponReloadMontage == FixedWeapon);
		TestTrue(TEXT("empty batch definition qualifies for every positive reserve"), Rig.Reload->CanReload());

		// This native rig has no skeletal mesh; verify the ammo transaction in logic-timer mode.
		// Fixed-action selection is covered by the Apply assertions and the capture test below.
		Rig.Reload->CharacterReloadMontage = nullptr;
		Rig.Reload->WeaponReloadMontage = nullptr;

		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("fixed reload starts"), Token != 0)) return false;
		TestEqual(TEXT("captured amount is min(missing, reserve)"), FDustEnemyReloadTestAccess::GetActiveReloadAmount(Rig.Reload), Reserve);
		TestTrue(TEXT("this request is explicitly batch"), FDustEnemyReloadTestAccess::UsesEmptyBatch(Rig.Reload));
		TestTrue(TEXT("runtime entered reload"), Runtime->IsReloading());
		TestFalse(TEXT("batch request refuses per-round commit"),
			Runtime->TryCommitReloadRound(Runtime->GetReloadRequestId()));
		TestEqual(TEXT("no ammo before natural completion"), Runtime->GetCurrentAmmo(), 0);
		TestEqual(TEXT("no reserve consumed before commit"), Runtime->GetReserveAmmo(), Reserve);

		TestTrue(TEXT("partial time keeps running"),
			Rig.Reload->UpdateReload(Token, 0.1f) == EDustEnemyReloadStatus::Running);
		TestEqual(TEXT("still no ammo before completion"), Runtime->GetCurrentAmmo(), 0);
		TestTrue(TEXT("logic duration completes"),
			Rig.Reload->UpdateReload(Token, 0.2f) == EDustEnemyReloadStatus::Succeeded);
		TestEqual(TEXT("completion alone does not commit"), Runtime->GetCurrentAmmo(), 0);

		TestTrue(TEXT("finish commits the captured batch"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("magazine receives exactly the captured amount"), Runtime->GetCurrentAmmo(), Reserve);
		TestEqual(TEXT("reserve is deducted exactly once"), Runtime->GetReserveAmmo(), 0);
		TestFalse(TEXT("finish clears batch state"), FDustEnemyReloadTestAccess::UsesEmptyBatch(Rig.Reload));
		TestFalse(TEXT("same token cannot commit twice"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("second finish cannot duplicate ammo"), Runtime->GetCurrentAmmo(), Reserve);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyFixedReloadRejectTest,
	"Dust2.EnemyReload.FixedActionRejectsWrongStateZeroReserveAndPerRound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyFixedReloadRejectTest::RunTest(const FString& Parameters)
{
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("non-full magazine world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(4, 8, false, true, 8);
		Fixture.AttachEnemyFixedPresentation(Definition,
			NewObject<UAnimMontage>(GetTransientPackage()), NewObject<UAnimMontage>(GetTransientPackage()));
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("non-full magazine rig missing")); return false; }
		TestTrue(TEXT("fixed presentation applies to a partially filled empty-batch gun"),
			Rig.Weapon->ApplyReloadPresentationFromProfile());
		TestFalse(TEXT("partially filled magazine never starts an empty batch"), Rig.Reload->CanReload());
		TestTrue(TEXT("start rejected for partial magazine"), Rig.Reload->TryStartReload() == 0);
	}

	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("zero reserve world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0, false, true, 8);
		Fixture.AttachEnemyFixedPresentation(Definition);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("zero reserve rig missing")); return false; }
		TestTrue(TEXT("fixed presentation applies with no reserve"), Rig.Weapon->ApplyReloadPresentationFromProfile());
		TestFalse(TEXT("zero reserve never qualifies"), Rig.Reload->CanReload());
		TestTrue(TEXT("zero reserve start rejected"), Rig.Reload->TryStartReload() == 0);
	}

	{
		// Fixed actions are optional: an empty-batch gun with no presentation asset still uses
		// the original logic-timer path.
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("no presentation world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 8, false, true, 8);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("no presentation rig missing")); return false; }
		TestTrue(TEXT("empty-batch gun qualifies without a presentation asset"), Rig.Reload->CanReload());
		const uint64 Token = Rig.Reload->TryStartReload();
		TestTrue(TEXT("empty-batch logic timer starts without a presentation asset"), Token != 0);
		if (Token != 0) Rig.Reload->CancelReload(Token);
	}

	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("per-round world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 3, true, false, 8);
		Fixture.AttachEnemyFixedPresentation(Definition,
			NewObject<UAnimMontage>(GetTransientPackage()), NewObject<UAnimMontage>(GetTransientPackage()));
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("per-round rig missing")); return false; }
		TestTrue(TEXT("fixed presentation applies to a pure per-round definition"),
			Rig.Weapon->ApplyReloadPresentationFromProfile());
		TestFalse(TEXT("pure per-round never qualifies"), Rig.Reload->CanReload());
		TestTrue(TEXT("pure per-round start rejected"), Rig.Reload->TryStartReload() == 0);
	}

	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("magazine world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 8, false, false, 8);
		Fixture.AttachEnemyFixedPresentation(Definition,
			NewObject<UAnimMontage>(GetTransientPackage()), NewObject<UAnimMontage>(GetTransientPackage()));
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("magazine rig missing")); return false; }
		TestTrue(TEXT("fixed presentation applies to an ordinary magazine"),
			Rig.Weapon->ApplyReloadPresentationFromProfile());
		TestTrue(TEXT("ordinary magazine supports the fixed reload path"), Rig.Reload->CanReload());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyFixedReloadCaptureTest,
	"Dust2.EnemyReload.FixedActionCapturesRequestAndCancelsWithoutPartialAmmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyFixedReloadCaptureTest::RunTest(const FString& Parameters)
{
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("capture test world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 3, false, true, 8);
		UAnimMontage* OldCharacterMontage = NewObject<UAnimMontage>(GetTransientPackage());
		UAnimMontage* OldWeaponMontage = NewObject<UAnimMontage>(GetTransientPackage());
		UAnimMontage* NewCharacterMontage = NewObject<UAnimMontage>(GetTransientPackage());
		UAnimMontage* NewWeaponMontage = NewObject<UAnimMontage>(GetTransientPackage());
		Fixture.AttachEnemyFixedPresentation(Definition, OldCharacterMontage, OldWeaponMontage);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("capture rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("capture runtime"), Runtime)) return false;
		TestTrue(TEXT("capture presentation applies"), Rig.Weapon->ApplyReloadPresentationFromProfile());
		TestTrue(TEXT("captured fixed character action is initialized"), Rig.Reload->CharacterReloadMontage == OldCharacterMontage);
		TestTrue(TEXT("captured fixed weapon action is initialized"), Rig.Reload->WeaponReloadMontage == OldWeaponMontage);

		// The native rig has no skeletal mesh, so start in logic-timer mode and then simulate
		// the captured pair. Callback validation must keep using that captured pair.
		Rig.Reload->CharacterReloadMontage = nullptr;
		Rig.Reload->WeaponReloadMontage = nullptr;
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("capture transaction starts"), Token != 0)) return false;

		FDustEnemyReloadTestAccess::ForceCapturedMontageMode(Rig.Reload, OldCharacterMontage, OldWeaponMontage);
		// The live fixed actions change after capture; callbacks must not re-read them.
		Rig.Reload->CharacterReloadMontage = NewCharacterMontage;
		Rig.Reload->WeaponReloadMontage = NewWeaponMontage;

		FDustEnemyReloadTestAccess::NotifyCharacterMontageEnded(Rig.Reload, NewCharacterMontage, false, Token);
		TestTrue(TEXT("new live configuration cannot complete the captured request"),
			Rig.Reload->UpdateReload(Token, 0.01f) == EDustEnemyReloadStatus::Running);
		TestEqual(TEXT("mismatched configuration commits no ammo"), Runtime->GetCurrentAmmo(), 0);

		FDustEnemyReloadTestAccess::NotifyCharacterMontageEnded(Rig.Reload, OldCharacterMontage, false, Token);
		TestTrue(TEXT("one captured action is not enough while the weapon action is pending"),
			Rig.Reload->UpdateReload(Token, 0.01f) == EDustEnemyReloadStatus::Running);
		FDustEnemyReloadTestAccess::NotifyWeaponMontageEnded(Rig.Reload, OldWeaponMontage, false, Token);
		TestTrue(TEXT("both captured actions end the transaction"),
			Rig.Reload->UpdateReload(Token, 0.01f) == EDustEnemyReloadStatus::Succeeded);
		TestEqual(TEXT("completion still waits for FinishReload"), Runtime->GetCurrentAmmo(), 0);
		TestTrue(TEXT("captured batch commits the captured amount"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("captured amount is committed once"), Runtime->GetCurrentAmmo(), 3);
		TestEqual(TEXT("captured amount deducts reserve once"), Runtime->GetReserveAmmo(), 0);
	}

	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("interrupt world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 3, false, true, 8);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("interrupt rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("interrupt runtime"), Runtime)) return false;

		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("interrupt transaction starts"), Token != 0)) return false;
		FDustEnemyReloadTestAccess::ForceCapturedMontageMode(Rig.Reload, Montage, nullptr);
		FDustEnemyReloadTestAccess::NotifyCharacterMontageEnded(Rig.Reload, Montage, true, Token);
		TestTrue(TEXT("interrupted completion fails"),
			Rig.Reload->UpdateReload(Token, 0.01f) == EDustEnemyReloadStatus::Failed);
		TestFalse(TEXT("interruption cancels the runtime request"), Runtime->IsReloading());
		TestEqual(TEXT("interruption never commits ammo"), Runtime->GetCurrentAmmo(), 0);
		TestEqual(TEXT("interruption never consumes reserve"), Runtime->GetReserveAmmo(), 3);
		TestFalse(TEXT("interruption releases busy"), Rig.Reload->IsBusy());
	}

	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("stale world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 3, false, true, 8);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("stale rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("stale runtime"), Runtime)) return false;

		UAnimMontage* OldMontage = NewObject<UAnimMontage>(GetTransientPackage());
		const uint64 OldToken = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("old transaction starts"), OldToken != 0)) return false;
		Rig.Reload->CancelReload(OldToken);
		Fixture.Advance(0.1f);

		const uint64 NewToken = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("new transaction starts"), NewToken != 0)) return false;
		FDustEnemyReloadTestAccess::NotifyCharacterMontageEnded(Rig.Reload, OldMontage, false, OldToken);
		TestTrue(TEXT("old token cannot complete the new transaction"),
			Rig.Reload->UpdateReload(NewToken, 0.01f) == EDustEnemyReloadStatus::Running);
		TestTrue(TEXT("new transaction still owns the runtime"), Runtime->IsReloading());
		Rig.Reload->CancelReload(NewToken);
		TestEqual(TEXT("stale callbacks leave ammo untouched"), Runtime->GetCurrentAmmo(), 0);
	}

	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("amount change world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 3, false, true, 8);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("amount change rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("amount change runtime"), Runtime)) return false;

		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("amount change transaction starts"), Token != 0)) return false;
		// Reserve changes while the captured batch amount is 3; committing would no longer match
		// the fixed reload action that was played.
		Runtime->AddReserveAmmo(1);
		TestTrue(TEXT("amount change still reaches completion"),
			Rig.Reload->UpdateReload(Token, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestFalse(TEXT("changed amount refuses to commit"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("changed amount never loads ammo"), Runtime->GetCurrentAmmo(), 0);
		TestEqual(TEXT("changed reserve is left intact"), Runtime->GetReserveAmmo(), 4);
		TestFalse(TEXT("changed amount releases busy"), Rig.Reload->IsBusy());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustWeaponRuntimeEmptyBatchTest,
	"Dust2.WeaponRuntime.EmptyBatchEntryAndLegacyReloadPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustWeaponRuntimeEmptyBatchTest::RunTest(const FString& Parameters)
{
	FScopedReloadTestWorld Fixture;
	if (!TestNotNull(TEXT("runtime batch test world"), Fixture.Get())) return false;
	AWeaponRuntime* Runtime = Fixture.Get()->SpawnActor<AWeaponRuntime>();
	if (!TestNotNull(TEXT("native runtime"), Runtime)) return false;

	// New explicit entry: partial reserve is committed in one batch, never per round.
	UWeaponDefinitionData* EmptyBatch = Fixture.MakeDefinition(0, 3, false, true, 8);
	Runtime->WeaponDefinition = EmptyBatch;
	Runtime->RestoreAmmo(0, 3);
	TestTrue(TEXT("definition is explicitly empty-batch"), EmptyBatch->UsesEmptyBatchReload());
	TestFalse(TEXT("wrong expected amount is rejected"), Runtime->TryStartEmptyBatchReload(2));
	TestFalse(TEXT("failed entry leaves no request"), Runtime->IsReloading());
	TestTrue(TEXT("partial reserve batch starts"), Runtime->TryStartEmptyBatchReload(3));
	TestFalse(TEXT("batch request refuses TryCommitReloadRound"),
		Runtime->TryCommitReloadRound(Runtime->GetReloadRequestId()));
	TestEqual(TEXT("no ammo before batch finish"), Runtime->GetCurrentAmmo(), 0);
	TestTrue(TEXT("batch finish succeeds"), Runtime->FinishReload());
	TestEqual(TEXT("partial reserve filled in one batch"), Runtime->GetCurrentAmmo(), 3);
	TestEqual(TEXT("partial batch consumes reserve"), Runtime->GetReserveAmmo(), 0);
	TestFalse(TEXT("non-empty magazine rejects the explicit batch entry"), Runtime->TryStartEmptyBatchReload(3));

	// The old player entry on the same empty-batch definition still uses per-round for a partial
	// reserve; only the new explicit entry forces one batch.
	Runtime->WeaponDefinition = EmptyBatch;
	Runtime->RestoreAmmo(0, 3);
	TestTrue(TEXT("old entry still starts the partial empty-batch definition"), Runtime->TryStartReload());
	TestTrue(TEXT("old entry still commits one round"), Runtime->TryCommitReloadRound(Runtime->GetReloadRequestId()));
	TestEqual(TEXT("old entry committed exactly one round"), Runtime->GetCurrentAmmo(), 1);
	TestTrue(TEXT("old entry per-round finish succeeds"), Runtime->FinishReload());
	TestEqual(TEXT("old entry per-round finish adds no second batch"), Runtime->GetCurrentAmmo(), 1);

	// The original player entry keeps per-round behavior for the same partial reserve.
	UWeaponDefinitionData* PerRound = Fixture.MakeDefinition(0, 3, true, false, 8);
	Runtime->WeaponDefinition = PerRound;
	Runtime->RestoreAmmo(0, 3);
	TestTrue(TEXT("per-round definition is not empty-batch"), !PerRound->UsesEmptyBatchReload());
	TestTrue(TEXT("old player entry starts per-round"), Runtime->TryStartReload());
	const int32 PerRoundRequest = Runtime->GetReloadRequestId();
	TestTrue(TEXT("per-round commit accepts the request"), Runtime->TryCommitReloadRound(PerRoundRequest));
	TestTrue(TEXT("per-round commit repeats"), Runtime->TryCommitReloadRound(PerRoundRequest));
	TestTrue(TEXT("per-round commit reaches reserve"), Runtime->TryCommitReloadRound(PerRoundRequest));
	TestEqual(TEXT("per-round committed one by one"), Runtime->GetCurrentAmmo(), 3);
	TestTrue(TEXT("per-round finish succeeds"), Runtime->FinishReload());
	TestEqual(TEXT("per-round finish does not add a second batch"), Runtime->GetCurrentAmmo(), 3);

	// Full-magazine empty batch through the old entry keeps its previous behavior.
	UWeaponDefinitionData* FullBatch = Fixture.MakeDefinition(0, 8, false, true, 8);
	Runtime->WeaponDefinition = FullBatch;
	Runtime->RestoreAmmo(0, 8);
	TestTrue(TEXT("old full empty batch starts"), Runtime->TryStartReload());
	TestFalse(TEXT("full empty batch also refuses per-round commit"),
		Runtime->TryCommitReloadRound(Runtime->GetReloadRequestId()));
	TestTrue(TEXT("full empty batch finishes"), Runtime->FinishReload());
	TestEqual(TEXT("full empty batch loads the magazine"), Runtime->GetCurrentAmmo(), 8);
	TestEqual(TEXT("full empty batch consumes reserve"), Runtime->GetReserveAmmo(), 0);

	// AK105-style ordinary Magazine path is unchanged.
	UWeaponDefinitionData* Magazine = Fixture.MakeDefinition(0, 30, false, false, 30);
	Runtime->WeaponDefinition = Magazine;
	Runtime->RestoreAmmo(0, 30);
	TestFalse(TEXT("magazine style is not per-round"), Magazine->UsesPerRoundReload());
	TestFalse(TEXT("magazine style is not empty-batch"), Magazine->UsesEmptyBatchReload());
	TestTrue(TEXT("magazine old entry starts"), Runtime->TryStartReload());
	TestFalse(TEXT("magazine never accepts per-round commit"),
		Runtime->TryCommitReloadRound(Runtime->GetReloadRequestId()));
	TestTrue(TEXT("magazine finish succeeds"), Runtime->FinishReload());
	TestEqual(TEXT("magazine refills normally"), Runtime->GetCurrentAmmo(), 30);
	TestEqual(TEXT("magazine consumes reserve normally"), Runtime->GetReserveAmmo(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyInfiniteReserveReloadTest,
	"Dust2.EnemyReload.InfiniteReserveEmptyFullTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyInfiniteReserveReloadTest::RunTest(const FString& Parameters)
{
	// Ordinary magazine, zero reserve: a complete reload still fills the whole magazine once
	// and never creates reserve ammo.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("infinite magazine world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0, false, false, 30);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, nullptr, true);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("infinite magazine rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("infinite magazine runtime"), Runtime)) return false;

		TestTrue(TEXT("infinite ordinary magazine qualifies with no reserve"), Rig.Reload->CanReload());
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("infinite ordinary reload starts"), Token != 0)) return false;
		TestTrue(TEXT("this request captured the unlimited source"),
			FDustEnemyReloadTestAccess::UsesInfiniteSource(Rig.Reload));
		TestFalse(TEXT("infinite request is not the finite empty-batch mode"),
			FDustEnemyReloadTestAccess::UsesEmptyBatch(Rig.Reload));
		TestEqual(TEXT("captured amount is the full magazine"),
			FDustEnemyReloadTestAccess::GetActiveReloadAmount(Rig.Reload), 30);
		TestTrue(TEXT("runtime exposes the unlimited request identity"), Runtime->IsInfiniteReloadSource());
		TestEqual(TEXT("nothing is loaded before completion"), Runtime->GetCurrentAmmo(), 0);
		TestEqual(TEXT("reserve stays zero before completion"), Runtime->GetReserveAmmo(), 0);
		TestTrue(TEXT("partial time keeps running"),
			Rig.Reload->UpdateReload(Token, 0.1f) == EDustEnemyReloadStatus::Running);
		TestEqual(TEXT("still nothing before completion"), Runtime->GetCurrentAmmo(), 0);
		TestTrue(TEXT("logic duration completes"),
			Rig.Reload->UpdateReload(Token, 0.2f) == EDustEnemyReloadStatus::Succeeded);
		TestEqual(TEXT("completion alone does not load"), Runtime->GetCurrentAmmo(), 0);
		TestTrue(TEXT("finish commits the full magazine"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("magazine is full"), Runtime->GetCurrentAmmo(), 30);
		TestEqual(TEXT("infinite source never creates reserve"), Runtime->GetReserveAmmo(), 0);
		TestFalse(TEXT("finish clears the captured source"), FDustEnemyReloadTestAccess::UsesInfiniteSource(Rig.Reload));
		TestFalse(TEXT("finish clears the runtime source flag"), Runtime->IsInfiniteReloadSource());
		TestFalse(TEXT("full magazine no longer qualifies"), Rig.Reload->CanReload());

		// Empty again and repeat: the same enemy can keep reloading forever.
		Runtime->RestoreAmmo(0, 0);
		TestTrue(TEXT("second empty cycle qualifies"), Rig.Reload->CanReload());
		const uint64 SecondToken = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("second infinite reload starts"), SecondToken != 0)) return false;
		TestTrue(TEXT("second cycle completes"),
			Rig.Reload->UpdateReload(SecondToken, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("second cycle commits"), Rig.Reload->FinishReload(SecondToken));
		TestEqual(TEXT("second cycle fills the magazine"), Runtime->GetCurrentAmmo(), 30);
		TestEqual(TEXT("second cycle still has no reserve"), Runtime->GetReserveAmmo(), 0);
	}

	// A non-zero reserve is left untouched by the unlimited source.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("infinite reserve world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 5, false, false, 30);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, nullptr, true);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("infinite reserve rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("infinite reserve runtime"), Runtime)) return false;
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("infinite reserve reload starts"), Token != 0)) return false;
		TestTrue(TEXT("reload completes"),
			Rig.Reload->UpdateReload(Token, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("reload commits"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("existing reserve is not consumed"), Runtime->GetReserveAmmo(), 5);
		TestEqual(TEXT("magazine is still full"), Runtime->GetCurrentAmmo(), 30);
	}

	// R08-style empty-batch definition: fixed full-magazine batch, zero or non-zero reserve.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("infinite R08 world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0, false, true, 8);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, nullptr, true);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("infinite R08 rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("infinite R08 runtime"), Runtime)) return false;
		TestTrue(TEXT("empty-batch definition qualifies with infinite source"), Rig.Reload->CanReload());
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("infinite R08 reload starts"), Token != 0)) return false;
		TestEqual(TEXT("captured R08 amount is the full magazine"),
			FDustEnemyReloadTestAccess::GetActiveReloadAmount(Rig.Reload), 8);
		TestTrue(TEXT("R08 completion reaches the task"),
			Rig.Reload->UpdateReload(Token, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("R08 full batch commits once"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("R08 magazine is full"), Runtime->GetCurrentAmmo(), 8);
		TestEqual(TEXT("R08 infinite source keeps reserve"), Runtime->GetReserveAmmo(), 0);

		Runtime->RestoreAmmo(0, 3);
		const uint64 ReserveToken = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("R08 with reserve starts under infinite mode"), ReserveToken != 0)) return false;
		TestTrue(TEXT("R08 with reserve completes"),
			Rig.Reload->UpdateReload(ReserveToken, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("R08 with reserve commits"), Rig.Reload->FinishReload(ReserveToken));
		TestEqual(TEXT("R08 fills full magazine"), Runtime->GetCurrentAmmo(), 8);
		TestEqual(TEXT("R08 leaves the existing reserve untouched"), Runtime->GetReserveAmmo(), 3);
	}

	// Cancel and interruption never load or deduct anything.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("infinite cancel world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0, false, false, 30);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, nullptr, true);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("infinite cancel rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("infinite cancel runtime"), Runtime)) return false;
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("cancel case starts"), Token != 0)) return false;
		TestTrue(TEXT("cancel case is partial"), Rig.Reload->UpdateReload(Token, 0.1f) == EDustEnemyReloadStatus::Running);
		Rig.Reload->CancelReload(Token);
		TestFalse(TEXT("cancel stops the runtime request"), Runtime->IsReloading());
		TestFalse(TEXT("cancel clears the runtime source flag"), Runtime->IsInfiniteReloadSource());
		TestFalse(TEXT("cancel clears the captured source"), FDustEnemyReloadTestAccess::UsesInfiniteSource(Rig.Reload));
		TestEqual(TEXT("cancel never loads"), Runtime->GetCurrentAmmo(), 0);
		TestEqual(TEXT("cancel never creates reserve"), Runtime->GetReserveAmmo(), 0);
		TestFalse(TEXT("cancel releases busy"), Rig.Reload->IsBusy());
	}

	// Changing the component policy or the shared definition after start cannot change the
	// captured amount/source; a stale old token still cannot commit a newer request.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("infinite config world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0, false, false, 30);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, nullptr, true);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("infinite config rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("infinite config runtime"), Runtime)) return false;
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("config-change case starts"), Token != 0)) return false;
		Rig.Reload->bUseInfiniteReserveAmmo = false;
		Definition->ReloadStyle = EWeaponReloadStyle::PerRound;
		TestTrue(TEXT("captured transaction reaches completion"),
			Rig.Reload->UpdateReload(Token, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("captured transaction still commits its full amount"),
			Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("config change cannot shrink the captured fill"), Runtime->GetCurrentAmmo(), 30);
		TestEqual(TEXT("config change cannot create reserve"), Runtime->GetReserveAmmo(), 0);

		// A stale token cannot finish the next request.
		Runtime->RestoreAmmo(0, 0);
		Rig.Reload->bUseInfiniteReserveAmmo = true;
		Definition->ReloadStyle = EWeaponReloadStyle::Magazine;
		const uint64 OldToken = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("old request starts"), OldToken != 0)) return false;
		Rig.Reload->CancelReload(OldToken);
		Fixture.Advance(0.1f);
		const uint64 NewToken = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("new request starts"), NewToken != 0)) return false;
		TestFalse(TEXT("old token cannot commit the new request"), Rig.Reload->FinishReload(OldToken));
		TestTrue(TEXT("new request still owns the runtime"), Runtime->IsReloading());
		TestTrue(TEXT("new request still completes"),
			Rig.Reload->UpdateReload(NewToken, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("new request commits"), Rig.Reload->FinishReload(NewToken));
		TestEqual(TEXT("new request fills once"), Runtime->GetCurrentAmmo(), 30);
	}

	// Finite mode and the player-facing bottom entry still only load actual reserve.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("finite small reserve world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 3, false, false, 30);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("finite small reserve rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("finite small reserve runtime"), Runtime)) return false;
		TestTrue(TEXT("finite small reserve still qualifies"), Rig.Reload->CanReload());
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("finite small reserve reload starts"), Token != 0)) return false;
		TestTrue(TEXT("finite small reserve completes"),
			Rig.Reload->UpdateReload(Token, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("finite small reserve commits"), Rig.Reload->FinishReload(Token));
		TestEqual(TEXT("finite mode loads only the actual reserve"), Runtime->GetCurrentAmmo(), 3);
		TestEqual(TEXT("finite mode consumes that reserve"), Runtime->GetReserveAmmo(), 0);
		TestFalse(TEXT("finite bottom entry has no unlimited source"), Runtime->IsInfiniteReloadSource());
	}

	// Pickup snapshots carry only normal finite ammo; restoring one never grants an unlimited
	// reload on a reserve-empty weapon.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("infinite pickup world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(0, 0, false, false, 30);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, nullptr, true);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("infinite pickup rig missing")); return false; }
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		if (!TestNotNull(TEXT("infinite pickup runtime"), Runtime)) return false;
		const uint64 Token = Rig.Reload->TryStartReload();
		if (!TestTrue(TEXT("pickup source reload starts"), Token != 0)) return false;
		TestTrue(TEXT("pickup source reload completes"),
			Rig.Reload->UpdateReload(Token, 0.3f) == EDustEnemyReloadStatus::Succeeded);
		TestTrue(TEXT("pickup source reload commits"), Rig.Reload->FinishReload(Token));
		Runtime->RestoreAmmo(25, 0);
		const FWeaponPickupState State = Runtime->CapturePickupRuntimeState(false);
		TestEqual(TEXT("pickup snapshot stores only the real reserve"), State.ReserveAmmo, 0);
		AWeaponRuntime* Restored = Fixture.Get()->SpawnActor<AWeaponRuntime>();
		if (!TestNotNull(TEXT("restored runtime"), Restored)) return false;
		Restored->WeaponDefinition = Definition;
		Restored->RestorePickupRuntimeState(State);
		TestEqual(TEXT("restored magazine keeps the real ammo"), Restored->GetCurrentAmmo(), 25);
		TestEqual(TEXT("restored weapon keeps real reserve"), Restored->GetReserveAmmo(), 0);
		TestFalse(TEXT("restored finite bottom still rejects reload with no reserve"),
			Restored->TryStartReload());
	}

	// Non-empty magazine is never refilled by either mode.
	{
		FScopedReloadTestWorld Fixture;
		if (!TestNotNull(TEXT("partial magazine world"), Fixture.Get())) return false;
		UWeaponDefinitionData* Definition = Fixture.MakeDefinition(4, 0, false, false, 30);
		FScopedReloadTestWorld::FRig Rig = Fixture.SpawnRig(Definition, 0.2f, 0.05f, nullptr, true);
		if (!Rig.Reload || !Rig.Weapon) { AddError(TEXT("partial magazine rig missing")); return false; }
		TestFalse(TEXT("infinite policy still requires an empty magazine"), Rig.Reload->CanReload());
		TestTrue(TEXT("partial magazine start is rejected"), Rig.Reload->TryStartReload() == 0);
	}
	return true;
}

#endif
