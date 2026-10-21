#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyStateTreeTasks.h"
#include "DustEnemyReloadComponent.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"
#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponRecoilData.h"
#include "WeaponRuntime.h"

#include <limits>

struct FDustEnemyCombatAwarenessTestAccess
{
	static void Prepare(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
		APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon)
	{
		Adapter->Controller = Controller;
		Adapter->BoundPawn = Pawn;
		Adapter->Tree = Tree;
		Adapter->WeaponComponent = Weapon;
		Adapter->PathFollowing = Controller ? Controller->GetPathFollowingComponent() : nullptr;
		Adapter->bInitialized = true;
		Adapter->bStopped = false;
	}

	static void SetKnown(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor,
		const FVector& Location, bool bConfirmed, bool bInGrace)
	{
		const UWorld* World = Adapter->GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		Adapter->Target = Actor;
		Adapter->bSightCurrentlySensed = bConfirmed;
		Adapter->bConfirmedTarget = bConfirmed;
		Adapter->bInLossGrace = bInGrace;
		Adapter->bDeferredReloadSightLoss = false;
		Adapter->GraceStartTime = Now;
		Adapter->LastConfirmedTime = Now;
		Adapter->LastKnownLocation = Location;
		Adapter->QuickReacquireTarget = bInGrace ? Actor : nullptr;
		Adapter->QuickReacquireExpiry = bInGrace ? Now + 3.0f : 0.0f;
		Adapter->bQuickReacquireAvailable = false;
		Adapter->NextSpacingAllowedAt = 0.0f;
	}

	static void ClearKnown(UDustEnemyCombatAdapterComponent* Adapter)
	{
		Adapter->Target.Reset();
		Adapter->bSightCurrentlySensed = false;
		Adapter->bConfirmedTarget = false;
		Adapter->bInLossGrace = false;
		Adapter->bDeferredReloadSightLoss = false;
		Adapter->bSearching = false;
		Adapter->QuickReacquireTarget.Reset();
		Adapter->QuickReacquireExpiry = 0.0f;
		Adapter->bQuickReacquireAvailable = false;
	}

	static void NotifySightLoss(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor, const FVector& Location)
	{
		FAIStimulus Stimulus;
		Stimulus.Type = UAISense::GetSenseID<UAISense_Sight>();
		Stimulus.StimulusLocation = Location;
		Stimulus.MarkNoLongerSensed();
		Adapter->HandlePerception(Actor, Stimulus);
	}

	static void Acquire(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor, const FVector& Location)
	{
		Adapter->Acquire(Actor, Location);
	}

	static void SetGraceStart(UDustEnemyCombatAdapterComponent* Adapter, float Time)
	{
		Adapter->GraceStartTime = Time;
	}

	static void SetCooldown(UDustEnemyCombatAdapterComponent* Adapter, float AllowedAt)
	{
		Adapter->NextSpacingAllowedAt = AllowedAt;
	}

	static void InjectSpacing(UDustEnemyCombatAdapterComponent* Adapter, uint64 Token, uint32 RequestId, float Elapsed)
	{
		Adapter->ActiveSpacingToken = Token;
		++Adapter->SpacingGeneration;
		Adapter->SpacingMoveId = FAIRequestID(RequestId);
		Adapter->ImmediateSpacingMoveId = FAIRequestID::InvalidRequest;
		Adapter->SpacingTarget = Adapter->Target;
		Adapter->SpacingDestination = Adapter->Target.IsValid()
			? Adapter->Target->GetActorLocation() : FVector::ZeroVector;
		Adapter->SpacingElapsedSeconds = Elapsed;
		Adapter->SpacingStatus = EDustCreateSpaceStatus::Running;
		Adapter->bCreateSpaceActive = true;
		Adapter->bIssuingSpacingMove = false;
	}

	static EDustCreateSpaceStatus SpacingStatus(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->SpacingStatus;
	}

	static void MoveFinished(UDustEnemyCombatAdapterComponent* Adapter, uint32 RequestId, bool bSuccess)
	{
		Adapter->HandleMoveFinished(FAIRequestID(RequestId),
			FPathFollowingResult(bSuccess ? EPathFollowingResult::Success : EPathFollowingResult::Aborted, 0));
	}

	static void PrepareSearch(UDustEnemyCombatAdapterComponent* Adapter, AActor* SearchTarget, uint64 Serial, uint32 RequestId)
	{
		Adapter->ClearSearch();
		Adapter->Target.Reset();
		Adapter->bSightCurrentlySensed = false;
		Adapter->bConfirmedTarget = false;
		Adapter->bInLossGrace = false;
		Adapter->SearchTarget = SearchTarget;
		Adapter->bSearching = true;
		Adapter->Generation = Serial;
		Adapter->SearchMoveId = FAIRequestID(RequestId);
	}

	static FTimerHandle& SearchTimer(UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->SearchTimer;
	}

	static void NotifyTargetDestroyed(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor)
	{
		Adapter->HandleTargetDestroyed(Actor);
	}

	static float StepDistance(float Current, float Desired, float MaxStep)
	{
		return UDustEnemyCombatAdapterComponent::ComputeCreateSpaceStepDistance(Current, Desired, MaxStep);
	}

	static bool ApplySpeed(UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->ApplySpacingMovementSpeed();
	}

	static void RestoreSpeed(UDustEnemyCombatAdapterComponent* Adapter)
	{
		Adapter->RestoreSpacingMovementSpeed();
	}
};

namespace
{
	class FDustAwarenessTestWorld
	{
	public:
		explicit FDustAwarenessTestWorld(bool bCreatePhysicsScene = false)
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(bCreatePhysicsScene)
				.CreateNavigation(false)
				.CreateAISystem(true)
				.ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		~FDustAwarenessTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World = nullptr;
			}
		}

		UWorld* Get() const { return World; }

		void AdvanceWorld(float Seconds) const
		{
			if (!World || !FMath::IsFinite(Seconds) || Seconds <= 0.0f) return;
			World->Tick(LEVELTICK_All, Seconds);
		}

		struct FRig
		{
			AAIController* Controller = nullptr;
			APawn* Pawn = nullptr;
			ADust2PlayerCharacter* Target = nullptr;
			UStateTreeAIComponent* Tree = nullptr;
			UDustEnemyWeaponComponent* Weapon = nullptr;
			UDustEnemyCombatAdapterComponent* Adapter = nullptr;
			USceneComponent* Muzzle = nullptr;
		};

		UWeaponDefinitionData* MakeDefinition(int32 InitialAmmo) const
		{
			UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
			UWeaponBallisticsData* Ballistics = NewObject<UWeaponBallisticsData>(Definition);
			Ballistics->DamagePerShot = 10.0f;
			Ballistics->ArmorPenetration = 0.0f;
			Ballistics->HeadDamageMultiplier = 2.0f;
			Ballistics->TraceDistance = 2000.0f;
			Definition->BallisticsData = Ballistics;
			Definition->MagazineCapacity = FMath::Max(1, InitialAmmo);
			Definition->InitialMagazineAmmo = InitialAmmo;
			Definition->InitialReserveAmmo = 0;
			Definition->FireInterval = 0.05f;
			Definition->AllowedFireModes = { EFireMode::FullAutomatic };
			Definition->DefaultFireMode = EFireMode::FullAutomatic;
			Definition->StationarySpreadAngle = 0.0f;
			Definition->MovingSpreadAngle = 0.0f;
			Definition->JumpSpreadAngle = 0.0f;
			Definition->MovementSpreadSpeedThreshold = 0.0f;
			UWeaponRecoilData* Recoil = NewObject<UWeaponRecoilData>(Definition);
			Recoil->bUseFixedPattern = false;
			Recoil->bUseIndependentShotRecoil = false;
			Recoil->PatternResetDelay = 0.7f;
			Recoil->RecoveryDelay = 0.05f;
			Recoil->ShotRecoilRecoverySpeed = 5.0f;
			Recoil->LoopStartIndex = -1;
			Recoil->FixedPattern = { FVector2D(1.0f, 0.0f) };
			Definition->RecoilData = Recoil;
			return Definition;
		}

		UDustEnemyCombatProfile* MakeProfile(float ReactionDelay = 0.5f) const
		{
			UDustEnemyCombatProfile* Profile = NewObject<UDustEnemyCombatProfile>(GetTransientPackage());
			Profile->ReactionDelay = ReactionDelay;
			Profile->SettledAimSeconds = 0.0f;
			Profile->TrackingSpeedDegPerSecond = 360.0f;
			Profile->CompensationDelay = 0.0f;
			Profile->RequiredSettledSpeed = 0.0f;
			Profile->EngagementRange = 2000.0f;
			return Profile;
		}

		FRig SpawnRig(float ReactionDelay = 0.5f, bool bWithMuzzle = true, int32 Ammo = 3, int32 Reserve = 0) const
		{
			FRig Rig;
			if (!World) return Rig;
			Rig.Controller = World->SpawnActor<AAIController>();
			Rig.Pawn = World->SpawnActor<APawn>();
			Rig.Target = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Rig.Controller || !Rig.Pawn || !Rig.Target) return FRig();
			Rig.Controller->Possess(Rig.Pawn);
			USceneComponent* PawnRoot = NewObject<USceneComponent>(Rig.Pawn, TEXT("AwarenessPawnRoot"));
			Rig.Pawn->SetRootComponent(PawnRoot);
			PawnRoot->RegisterComponent();
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
			Rig.Weapon->WeaponDefinition = MakeDefinition(Ammo);
			Rig.Weapon->WeaponDefinition->InitialReserveAmmo = Reserve;
			Rig.Weapon->CombatProfile = MakeProfile(ReactionDelay);
			Rig.Pawn->AddInstanceComponent(Rig.Weapon);
			Rig.Weapon->RegisterComponent();
			Rig.Weapon->SetComponentTickEnabled(false);
			if (!Rig.Weapon->InitializeWeapon()) return FRig();

			Rig.Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Rig.Controller);
			Rig.Controller->AddInstanceComponent(Rig.Adapter);
			Rig.Adapter->RegisterComponent();
			FDustEnemyCombatAwarenessTestAccess::Prepare(Rig.Adapter, Rig.Controller, Rig.Pawn, Rig.Tree, Rig.Weapon);

			if (bWithMuzzle && Rig.Pawn->GetRootComponent())
			{
				Rig.Muzzle = NewObject<USceneComponent>(Rig.Pawn, TEXT("AwarenessMuzzle"));
				Rig.Muzzle->SetupAttachment(Rig.Pawn->GetRootComponent());
				Rig.Muzzle->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
				Rig.Pawn->AddInstanceComponent(Rig.Muzzle);
				Rig.Muzzle->RegisterComponent();
				Rig.Weapon->SetMuzzleSource(Rig.Muzzle, NAME_None);
			}
			return Rig;
		}

		void AdvanceWeapon(const FRig& Rig, float Seconds) const
		{
			if (!World || !Rig.Weapon || !FMath::IsFinite(Seconds) || Seconds <= 0.0f) return;
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / 0.05f));
			const float StepSeconds = Seconds / Steps;
			for (int32 Index = 0; Index < Steps; ++Index)
			{
				World->Tick(LEVELTICK_All, StepSeconds);
				Rig.Weapon->TickComponent(StepSeconds, LEVELTICK_All, nullptr);
			}
		}

		AActor* SpawnWall(const FVector& Location) const
		{
			if (!World) return nullptr;
			AActor* Wall = World->SpawnActor<AActor>();
			if (!Wall) return nullptr;
			UBoxComponent* Collision = NewObject<UBoxComponent>(Wall, TEXT("AwarenessWall"));
			Collision->SetBoxExtent(FVector(20.0f, 200.0f, 200.0f));
			Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Collision->SetCollisionObjectType(ECC_WorldStatic);
			Collision->SetCollisionResponseToAllChannels(ECR_Block);
			Wall->SetRootComponent(Collision);
			Collision->RegisterComponent();
			Wall->SetActorLocation(Location);
			return Wall;
		}

		AActor* SpawnPlainActor(const FVector& Location) const
		{
			if (!World) return nullptr;
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor) return nullptr;
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("AwarenessPlainRoot"));
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Actor;
		}

	private:
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAwarenessGraceSearchTest,
	"Dust2.EnemyAwareness.LossGraceThenSearchAndStopCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyAwarenessGraceSearchTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("awareness world"), Fixture.Get())) return false;
	const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("controller"), Rig.Controller) || !TestNotNull(TEXT("pawn"), Rig.Pawn)
		|| !TestNotNull(TEXT("target"), Rig.Target) || !TestNotNull(TEXT("adapter"), Rig.Adapter)
		|| !TestNotNull(TEXT("weapon"), Rig.Weapon)) return false;
	const FVector TargetLocation = Rig.Target->GetActorLocation();
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, TargetLocation, true, false);
	TestTrue(TEXT("confirmed target is exposed"), Rig.Adapter->GetConfirmedTarget() == Rig.Target);
	TestTrue(TEXT("confirmed target is a legal known target"), Rig.Adapter->GetTarget() == Rig.Target);
	TestTrue(TEXT("combat logic can run"), Rig.Adapter->CanRunCombatLogic());

	FDustEnemyCombatAwarenessTestAccess::NotifySightLoss(Rig.Adapter, Rig.Target, TargetLocation);
	TestTrue(TEXT("ordinary loss opens a real grace"), Rig.Adapter->NeedsReacquire());
	TestFalse(TEXT("grace is not a confirmed target"), Rig.Adapter->HasConfirmedTarget());
	TestNull(TEXT("grace exposes no confirmed actor"), Rig.Adapter->GetConfirmedTarget());
	TestTrue(TEXT("grace keeps the legal known target"), Rig.Adapter->GetTarget() == Rig.Target);
	TestFalse(TEXT("grace does not start the search immediately"), Rig.Adapter->IsSearching());
	TestTrue(TEXT("combat logic still sees the known target during grace"), Rig.Adapter->CanRunCombatLogic());

	FDustEnemyCombatAwarenessTestAccess::SetGraceStart(Rig.Adapter, Fixture.Get()->GetTimeSeconds() - 1.0f);
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("expired grace clears the known target"), Rig.Adapter->GetTarget() != nullptr);
	TestFalse(TEXT("expired grace clears confirmation"), Rig.Adapter->HasConfirmedTarget());
	TestFalse(TEXT("expired grace no longer needs reacquire"), Rig.Adapter->NeedsReacquire());
	TestTrue(TEXT("expired grace enters the original search"), Rig.Adapter->IsSearching());
	TestFalse(TEXT("expired grace cannot run combat logic"), Rig.Adapter->CanRunCombatLogic());

	Rig.Adapter->StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
	TestNull(TEXT("stop clears the known target"), Rig.Adapter->GetTarget());
	TestFalse(TEXT("stop clears reacquire"), Rig.Adapter->NeedsReacquire());
	TestFalse(TEXT("stop clears search"), Rig.Adapter->IsSearching());
	TestFalse(TEXT("stop clears spacing"), Rig.Adapter->CanCreateSpace());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAwarenessCloseRangeTest,
	"Dust2.EnemyAwareness.CloseKnownBehindAndUnknownNotScanned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyAwarenessCloseRangeTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("awareness world"), Fixture.Get())) return false;

	{
		const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
		if (!Rig.Adapter || !Rig.Pawn || !Rig.Target) { AddError(TEXT("close rig missing")); return false; }
		const FVector Behind(0.0f, -300.0f, 0.0f);
		Rig.Target->SetActorLocation(Behind);
		FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, Behind, false, true);
		Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestTrue(TEXT("known close target behind the pawn is confirmed without FOV"),
			Rig.Adapter->GetConfirmedTarget() == Rig.Target);
		TestFalse(TEXT("close confirmation ends the grace"), Rig.Adapter->NeedsReacquire());
	}

	{
		const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
		if (!Rig.Adapter || !Rig.Pawn) { AddError(TEXT("unknown rig missing")); return false; }
		// No known target: a close actor behind the pawn must not be discovered by the adapter.
		Rig.Target->SetActorLocation(FVector(0.0f, -300.0f, 0.0f));
		TestFalse(TEXT("no actor is known before any sight event"), Rig.Adapter->GetTarget() != nullptr);
		Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestFalse(TEXT("the adapter never scans an unknown actor behind the pawn"),
			Rig.Adapter->GetTarget() != nullptr);
		TestFalse(TEXT("the adapter does not confirm an unknown actor"), Rig.Adapter->HasConfirmedTarget());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAwarenessWallFreezeTest,
	"Dust2.EnemyAwareness.WallBlocksFireAndFreezesLastKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyAwarenessWallFreezeTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture(true);
	if (!TestNotNull(TEXT("awareness physics world"), Fixture.Get())) return false;
	const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!Rig.Adapter || !Rig.Pawn || !Rig.Target || !Rig.Weapon) { AddError(TEXT("wall rig missing")); return false; }
	const FVector VisibleLocation(0.0f, -300.0f, 0.0f);
	Rig.Target->SetActorLocation(VisibleLocation);
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, VisibleLocation, true, false);
	if (!TestTrue(TEXT("confirmed target can start an engagement"), Rig.Weapon->StartEngagement(Rig.Target))) return false;
	TestTrue(TEXT("weapon is engaging before the wall check"), Rig.Weapon->IsEngaging());
	if (!TestNotNull(TEXT("blocking wall"), Fixture.SpawnWall(FVector(0.0f, -150.0f, 50.0f)))) return false;
	TestNull(TEXT("a new wall hides the confirmed actor even before the next adapter tick"),
		Rig.Adapter->GetConfirmedTarget());

	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("wall blocks confirmation immediately"), Rig.Adapter->HasConfirmedTarget());
	TestTrue(TEXT("wall loss keeps the known target in grace"), Rig.Adapter->NeedsReacquire());
	TestFalse(TEXT("wall loss stops shooting"), Rig.Weapon->IsEngaging());
	TestTrue(TEXT("wall loss freezes the last known location"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(VisibleLocation, 1.0f));

	Rig.Target->SetActorLocation(FVector(0.0f, -700.0f, 0.0f));
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("hidden target is never followed by its live location"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(VisibleLocation, 1.0f));
	TestFalse(TEXT("hidden target is not exposed as a confirmed actor"),
		Rig.Adapter->GetConfirmedTarget() != nullptr);

	FDustEnemyCombatAwarenessTestAccess::SetGraceStart(Rig.Adapter, Fixture.Get()->GetTimeSeconds() - 1.0f);
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("wall grace expiry starts the last-known search"), Rig.Adapter->IsSearching());
	TestNull(TEXT("search no longer exposes the hidden actor"), Rig.Adapter->GetTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyQuickReacquireTest,
	"Dust2.EnemyAwareness.FirstReactionAndFastSameTargetReacquire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyQuickReacquireTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("awareness world"), Fixture.Get())) return false;
	const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig(0.5f, true, 4);
	if (!Rig.Adapter || !Rig.Weapon || !Rig.Target) { AddError(TEXT("quick rig missing")); return false; }
	const FVector TargetLocation = Rig.Target->GetActorLocation();
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, TargetLocation, true, false);
	if (!TestTrue(TEXT("first engagement starts"), Rig.Weapon->StartEngagement(Rig.Target))) return false;
	Fixture.AdvanceWeapon(Rig, 0.2f);
	TestEqual(TEXT("first acquisition keeps the full reaction delay"), Rig.Weapon->SuccessfulShots, 0);
	Fixture.AdvanceWeapon(Rig, 0.4f);
	TestTrue(TEXT("first acquisition fires after the full reaction"), Rig.Weapon->SuccessfulShots > 0);
	Rig.Weapon->StopEngagement();

	FDustEnemyCombatAwarenessTestAccess::NotifySightLoss(Rig.Adapter, Rig.Target, TargetLocation);
	TestTrue(TEXT("short loss opens reacquire"), Rig.Adapter->NeedsReacquire());
	FDustEnemyCombatAwarenessTestAccess::Acquire(Rig.Adapter, Rig.Target, TargetLocation);
	TestTrue(TEXT("same target is confirmed again"), Rig.Adapter->HasConfirmedTarget());
	TestFalse(TEXT("reacquire ends the grace"), Rig.Adapter->NeedsReacquire());

	const int32 ShotsBefore = Rig.Weapon->SuccessfulShots;
	if (!TestTrue(TEXT("reacquire starts a new engagement"), Rig.Weapon->StartEngagement(Rig.Target))) return false;
	Fixture.AdvanceWeapon(Rig, 0.05f);
	TestEqual(TEXT("fast reacquire does not fire before its short reaction"), Rig.Weapon->SuccessfulShots, ShotsBefore);
	Fixture.AdvanceWeapon(Rig, 0.07f);
	TestEqual(TEXT("fast reacquire uses the short reaction window"), Rig.Weapon->SuccessfulShots, ShotsBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyBodyAlignmentTest,
	"Dust2.EnemyAwareness.BodyMustFaceBeforeAmmoCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyBodyAlignmentTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("awareness world"), Fixture.Get())) return false;
	const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig(0.0f, true, 3);
	if (!Rig.Adapter || !Rig.Weapon || !Rig.Pawn || !Rig.Target) { AddError(TEXT("alignment rig missing")); return false; }
	Rig.Weapon->CombatProfile->TrackingSpeedDegPerSecond = 180.0f;
	Rig.Weapon->WeaponDefinition->FireInterval = 0.2f;
	const FVector Behind(0.0f, -300.0f, 0.0f);
	Rig.Target->SetActorLocation(Behind);
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, Behind, true, false);
	if (!TestTrue(TEXT("back-facing engagement still starts"), Rig.Weapon->StartEngagement(Rig.Target))) return false;
	const int32 AmmoBefore = Rig.Weapon->GetWeaponRuntime()->GetCurrentAmmo();

	Fixture.AdvanceWeapon(Rig, 1.0f);
	TestEqual(TEXT("tracking turns but the body has not committed ammo"), Rig.Weapon->SuccessfulShots, 0);
	TestEqual(TEXT("ammo is preserved while the body faces away"),
		Rig.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), AmmoBefore);
	TestTrue(TEXT("an unaligned body never stops and restarts the engagement"), Rig.Weapon->IsEngaging());

	Rig.Pawn->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
	Fixture.AdvanceWeapon(Rig, 0.1f);
	TestEqual(TEXT("ammo is committed only after the actual body faces the target"),
		Rig.Weapon->SuccessfulShots, 1);
	TestEqual(TEXT("a committed aligned shot consumes one round"),
		Rig.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), AmmoBefore - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCreateSpaceFailureTest,
	"Dust2.EnemyCreateSpace.NoNavFailsWithCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyCreateSpaceFailureTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("awareness world"), Fixture.Get())) return false;
	const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!Rig.Adapter || !Rig.Target) { AddError(TEXT("spacing rig missing")); return false; }
	Rig.Target->SetActorLocation(FVector(150.0f, 0.0f, 0.0f));
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, Rig.Target->GetActorLocation(), true, false);
	TestTrue(TEXT("close confirmed target admits a spacing query"), Rig.Adapter->CanCreateSpace());
	const uint64 Token = Rig.Adapter->BeginCreateSpace();
	TestEqual(TEXT("no navigation must not fabricate a successful move"), Token, uint64(0));
	TestFalse(TEXT("a failed candidate selection owns no move"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("failure applies the cooldown"), Rig.Adapter->CanCreateSpace());

	Fixture.AdvanceWorld(3.1f);
	TestTrue(TEXT("cooldown expires on world time"), Rig.Adapter->CanCreateSpace());
	TestEqual(TEXT("the second attempt still refuses a fake nav success"),
		Rig.Adapter->BeginCreateSpace(), uint64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCreateSpaceOwnershipTest,
	"Dust2.EnemyCreateSpace.TimeoutAndStaleRequestIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyCreateSpaceOwnershipTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("awareness world"), Fixture.Get())) return false;
	const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!Rig.Adapter || !Rig.Target) { AddError(TEXT("spacing ownership rig missing")); return false; }
	Rig.Target->SetActorLocation(FVector(150.0f, 0.0f, 0.0f));
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, Rig.Target->GetActorLocation(), true, false);

	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 5, 100, 2.9f);
	TestTrue(TEXT("expired spacing times out"),
		Rig.Adapter->UpdateCreateSpace(5, 0.2f) == EDustCreateSpaceStatus::Failed);
	TestFalse(TEXT("timeout releases its move"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("timeout applies the cooldown"), Rig.Adapter->CanCreateSpace());

	FDustEnemyCombatAwarenessTestAccess::SetCooldown(Rig.Adapter, 0.0f);
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 6, 200, 0.0f);
	FDustEnemyCombatAwarenessTestAccess::MoveFinished(Rig.Adapter, 199, true);
	TestTrue(TEXT("an unrelated request id cannot complete the spacing move"),
		FDustEnemyCombatAwarenessTestAccess::SpacingStatus(Rig.Adapter) == EDustCreateSpaceStatus::Running);
	FDustEnemyCombatAwarenessTestAccess::MoveFinished(Rig.Adapter, 200, true);
	TestTrue(TEXT("the matching request id resolves the spacing move"),
		FDustEnemyCombatAwarenessTestAccess::SpacingStatus(Rig.Adapter) == EDustCreateSpaceStatus::Succeeded);
	Rig.Adapter->CompleteCreateSpace(6);
	TestFalse(TEXT("completion releases the move"), Rig.Adapter->IsCreateSpaceActive());

	// A stale completion after a newer search exists must not finish that search.
	FDustEnemyCombatAwarenessTestAccess::SetCooldown(Rig.Adapter, 0.0f);
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 7, 300, 0.0f);
	Rig.Adapter->CancelCreateSpace(7);
	// An old task Exit holds token 7; a newer move (token 8) must survive that stale cancel.
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 8, 400, 0.0f);
	Rig.Adapter->CancelCreateSpace(7);
	TestTrue(TEXT("an old Exit token cannot cancel the newer spacing move"),
		Rig.Adapter->IsCreateSpaceActive());
	TestTrue(TEXT("the newer spacing move still owns its running status"),
		FDustEnemyCombatAwarenessTestAccess::SpacingStatus(Rig.Adapter) == EDustCreateSpaceStatus::Running);
	Rig.Adapter->CancelCreateSpace(8);

	FDustEnemyCombatAwarenessTestAccess::SetCooldown(Rig.Adapter, 0.0f);
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 9, 600, 0.0f);
	Rig.Adapter->CancelCreateSpace(9);
	FDustEnemyCombatAwarenessTestAccess::PrepareSearch(Rig.Adapter, Rig.Target, 42, 500);
	FDustEnemyCombatAwarenessTestAccess::MoveFinished(Rig.Adapter, 600, true);
	TestFalse(TEXT("a stale spacing completion cannot finish the newer search"),
		Fixture.Get()->GetTimerManager().IsTimerActive(FDustEnemyCombatAwarenessTestAccess::SearchTimer(Rig.Adapter)));
	FDustEnemyCombatAwarenessTestAccess::MoveFinished(Rig.Adapter, 500, true);
	TestTrue(TEXT("the newer search still owns its matching completion"),
		Fixture.Get()->GetTimerManager().IsTimerActive(FDustEnemyCombatAwarenessTestAccess::SearchTimer(Rig.Adapter)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAwarenessStopAndDeathTest,
	"Dust2.EnemyAwareness.StopAndDeathClearGraceAndSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyAwarenessStopAndDeathTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("awareness world"), Fixture.Get())) return false;
	{
		const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
		if (!Rig.Adapter || !Rig.Target) { AddError(TEXT("stop rig missing")); return false; }
		FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target,
			Rig.Target->GetActorLocation(), false, true);
		FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 9, 900, 0.0f);
		TestTrue(TEXT("grace exists before stop"), Rig.Adapter->NeedsReacquire());
		Rig.Adapter->StopCombatAI(EDustEnemyAIStopReason::LostControl);
		TestNull(TEXT("stop clears target knowledge"), Rig.Adapter->GetTarget());
		TestFalse(TEXT("stop clears grace"), Rig.Adapter->NeedsReacquire());
		TestFalse(TEXT("stop clears its spacing move"), Rig.Adapter->IsCreateSpaceActive());
		TestFalse(TEXT("stop clears spacing admission"), Rig.Adapter->CanCreateSpace());
	}
	{
		const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig();
		if (!Rig.Adapter || !Rig.Target) { AddError(TEXT("death rig missing")); return false; }
		FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target,
			Rig.Target->GetActorLocation(), false, true);
		FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 10, 1000, 0.0f);
		FDustEnemyCombatAwarenessTestAccess::NotifyTargetDestroyed(Rig.Adapter, Rig.Target);
		TestNull(TEXT("target death clears the known target"), Rig.Adapter->GetTarget());
		TestFalse(TEXT("target death clears grace"), Rig.Adapter->NeedsReacquire());
		TestFalse(TEXT("target death cancels spacing"), Rig.Adapter->IsCreateSpaceActive());
		TestFalse(TEXT("target death does not start a search"), Rig.Adapter->IsSearching());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAwarenessSourceAndFreezeTest,
	"Dust2.EnemyAwareness.NormalSightRefreshCloseExitAndRepeatedLossFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyAwarenessSourceAndFreezeTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	const auto Rig = Fixture.SpawnRig();
	if (!Rig.Adapter || !Rig.Target) { AddError(TEXT("source rig missing")); return false; }
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target,
		Rig.Target->GetActorLocation(), true, false);
	Rig.Target->SetActorLocation(FVector(800.0f, 0.0f, 0.0f));
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("ordinary Sight refreshes observations beyond the close exception"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(Rig.Target->GetActorLocation(), 1.0f));
	Rig.Target->SetActorLocation(FVector(-300.0f, 0.0f, 0.0f));
	FDustEnemyCombatAwarenessTestAccess::NotifySightLoss(Rig.Adapter, Rig.Target, FVector(9999.0f));
	TestTrue(TEXT("a near known unobstructed target does not suffer a false loss event"),
		Rig.Adapter->HasConfirmedTarget());
	const FVector Frozen = Rig.Adapter->GetLastKnownTargetLocation();
	Rig.Target->SetActorLocation(FVector(-800.0f, 0.0f, 0.0f));
	TestNull(TEXT("near confirmation does not grant far omnidirectional vision between ticks"),
		Rig.Adapter->GetConfirmedTarget());
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("leaving close range without normal Sight opens grace"), Rig.Adapter->NeedsReacquire());
	FDustEnemyCombatAwarenessTestAccess::SetGraceStart(Rig.Adapter, Fixture.Get()->GetTimeSeconds() - 1.0f);
	FDustEnemyCombatAwarenessTestAccess::NotifySightLoss(Rig.Adapter, Rig.Target, FVector(7777.0f));
	TestTrue(TEXT("failed stimulus never supplies a new observation"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(Frozen, 1.0f));
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("repeated failed events do not restart the grace clock"), Rig.Adapter->IsSearching());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAwarenessPhysicalReloadLossTest,
	"Dust2.EnemyAwareness.PhysicalWallLossDefersSearchUntilReloadCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyAwarenessPhysicalReloadLossTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture(true);
	const auto Rig = Fixture.SpawnRig(0.0f, true, 0, 6);
	if (!Rig.Adapter || !Rig.Target || !Rig.Weapon) { AddError(TEXT("reload rig missing")); return false; }
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target,
		Rig.Target->GetActorLocation(), true, false);
	UDustEnemyReloadComponent* Reload = NewObject<UDustEnemyReloadComponent>(Rig.Pawn);
	// This regression is about limited reserve deferral, so it explicitly keeps finite mode.
	Reload->bUseInfiniteReserveAmmo = false;
	Reload->LogicalReloadSeconds = 2.0f;
	Rig.Pawn->AddInstanceComponent(Reload);
	Reload->RegisterComponent();
	const uint64 Token = Reload->TryStartReload();
	if (!TestTrue(TEXT("real logical reload transaction starts"), Token != 0)) return false;
	if (!TestNotNull(TEXT("wall"), Fixture.SpawnWall(FVector(300.0f, 0.0f, 50.0f)))) return false;
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("physical trace loss uses reload deferral without a perception event"),
		Rig.Adapter->HasDeferredReloadSightLoss());
	Fixture.AdvanceWorld(1.0f);
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("passing the normal grace does not start a search"), Rig.Adapter->IsSearching());
	TestTrue(TEXT("passing the normal grace does not cancel the reload"), Reload->IsBusy());
	TestTrue(TEXT("reload updates through occlusion"),
		Reload->UpdateReload(Token, 2.1f) == EDustEnemyReloadStatus::Succeeded);
	TestTrue(TEXT("the real request commits"), Reload->FinishReload(Token));
	TestEqual(TEXT("one capacity is committed"), Rig.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), 1);
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("search starts only after the reload ends"), Rig.Adapter->IsSearching());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCreateSpaceMovingFireTest,
	"Dust2.EnemyCreateSpace.ConfirmedMovingEngagementAndRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyCreateSpaceMovingFireTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("moving-fire world"), Fixture.Get())) return false;
	const FDustAwarenessTestWorld::FRig Rig = Fixture.SpawnRig(0.0f, true, 6);
	if (!Rig.Adapter || !Rig.Weapon || !Rig.Pawn || !Rig.Target) { AddError(TEXT("moving-fire rig missing")); return false; }
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	if (!TestNotNull(TEXT("moving-fire runtime"), Runtime)) return false;
	UFloatingPawnMovement* Movement = NewObject<UFloatingPawnMovement>(Rig.Pawn);
	Rig.Pawn->AddInstanceComponent(Movement);
	Movement->SetUpdatedComponent(Rig.Pawn->GetRootComponent());
	Movement->RegisterComponent();
	Movement->SetComponentTickEnabled(false);
	Movement->Velocity = FVector(-200.0f, 0.0f, 0.0f);
	TestTrue(TEXT("the real pawn velocity is above its standing threshold"), Rig.Pawn->GetVelocity().Size2D() > 100.0f);
	Rig.Weapon->CombatProfile->SettledAimSeconds = 1.0f;
	Rig.Weapon->CombatProfile->TrackingSpeedDegPerSecond = 720.0f;
	Rig.Weapon->CombatProfile->RequiredSettledSpeed = 0.0f;
	Rig.Weapon->CombatProfile->BurstShotCount = 3;
	Rig.Weapon->CombatProfile->MaxBurstShotCount = 6;
	Rig.Weapon->WeaponDefinition->FireInterval = 0.2f;
	Runtime->ApplyDefinitionSettings();
	Rig.Target->SetActorLocation(FVector(600.0f, 0.0f, 0.0f));
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target,
		Rig.Target->GetActorLocation(), true, false);
	// Body already faces +X, the same direction as the confirmed player.
	Rig.Pawn->SetActorRotation(FRotator::ZeroRotator);
	if (!TestTrue(TEXT("normal engagement starts"), Rig.Weapon->StartEngagement(Rig.Target))) return false;
	const int32 AmmoBefore = Runtime->GetCurrentAmmo();
	Fixture.AdvanceWeapon(Rig, 0.4f);
	TestEqual(TEXT("ordinary Engage keeps the standing-still gate"), Rig.Weapon->SuccessfulShots, 0);
	TestEqual(TEXT("standing gate never consumes ammo"), Runtime->GetCurrentAmmo(), AmmoBefore);

	// This injection represents the Create Space owner already holding one token for the same
	// target; the weapon-side allowance and firing are the real code under test.
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 101, 900, 0.0f);
	TestTrue(TEXT("spacing owns the confirmed target"), Rig.Adapter->IsSpacingTarget(Rig.Target));
	TestTrue(TEXT("spacing keeps the same engagement"), Rig.Weapon->IsEngaging());
	Fixture.AdvanceWeapon(Rig, 0.3f);
	TestTrue(TEXT("a spacing-owned aligned engagement commits shots while moving"), Rig.Weapon->SuccessfulShots > 0);
	TestTrue(TEXT("a spacing shot consumes real ammo"), Runtime->GetCurrentAmmo() < AmmoBefore);
	TestTrue(TEXT("full-auto keeps its sampled random volley target"),
		Rig.Weapon->CurrentVolleyShotTarget >= 3 && Rig.Weapon->CurrentVolleyShotTarget <= 6);

	// Unaligned during the same spacing: tracking/focus continue but ammo is not committed.
	Rig.Pawn->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
	const int32 ShotsBeforeTurn = Rig.Weapon->SuccessfulShots;
	const int32 AmmoBeforeTurn = Runtime->GetCurrentAmmo();
	Fixture.AdvanceWeapon(Rig, 0.3f);
	TestEqual(TEXT("a body that faces away cannot spend ammo even while spacing"),
		Rig.Weapon->SuccessfulShots, ShotsBeforeTurn);
	TestEqual(TEXT("unaligned spacing preserves ammo"), Runtime->GetCurrentAmmo(), AmmoBeforeTurn);
	TestTrue(TEXT("unaligned spacing never restarts the engagement"), Rig.Weapon->IsEngaging());

	// A stale task Exit cannot cancel a newer spacing request or its engagement.
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 202, 901, 0.0f);
	Rig.Adapter->CancelCreateSpace(101);
	TestTrue(TEXT("an old Exit token cannot cancel the newer spacing"), Rig.Adapter->IsCreateSpaceActive());
	TestTrue(TEXT("an old Exit token cannot stop the newer engagement"), Rig.Weapon->IsEngaging());

	// No reload component/task owns this particular test request: an empty unsupported move must release.
	while (Runtime->GetCurrentAmmo() > 0) Runtime->TryConsumeAmmo();
	TestTrue(TEXT("empty magazine without a reload owner releases the move"),
		Rig.Adapter->UpdateCreateSpace(202, 0.1f) == EDustCreateSpaceStatus::Failed);
	TestFalse(TEXT("empty magazine releases the spacing token"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("empty magazine stops the spacing engagement"), Rig.Weapon->IsEngaging());

	// Explicit cancel also releases movement/shooting.
	Runtime->RestoreAmmo(3, 0);
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 303, 902, 0.0f);
	TestTrue(TEXT("cancel rig re-engages before the cancel"), Rig.Weapon->StartEngagement(Rig.Target));
	Rig.Adapter->CancelCreateSpace(303);
	TestFalse(TEXT("cancel releases its spacing"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("cancel stops the spacing engagement"), Rig.Weapon->IsEngaging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCreateSpaceStepTest,
	"Dust2.EnemyCreateSpace.StepClosesGapButNeverExceedsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyCreateSpaceStepTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("closes the exact gap toward 600"), FDustEnemyCombatAwarenessTestAccess::StepDistance(300.0f, 600.0f, 400.0f), 300.0f);
	TestEqual(TEXT("never exceeds the single-move cap"), FDustEnemyCombatAwarenessTestAccess::StepDistance(100.0f, 600.0f, 400.0f), 400.0f);
	TestEqual(TEXT("small remaining gap is used as the step"), FDustEnemyCombatAwarenessTestAccess::StepDistance(550.0f, 600.0f, 400.0f), 50.0f);
	TestEqual(TEXT("already at the desired distance issues no move"), FDustEnemyCombatAwarenessTestAccess::StepDistance(600.0f, 600.0f, 400.0f), 0.0f);
	TestEqual(TEXT("beyond the desired distance issues no move"), FDustEnemyCombatAwarenessTestAccess::StepDistance(700.0f, 600.0f, 400.0f), 0.0f);
	const float NaNDesired = FDustEnemyCombatAwarenessTestAccess::StepDistance(100.0f,
		std::numeric_limits<float>::quiet_NaN(), 400.0f);
	TestTrue(TEXT("a non-finite desired distance falls back to a finite result"), FMath::IsFinite(NaNDesired));
	TestEqual(TEXT("non-finite desired distance uses the coded default"), NaNDesired, 400.0f);
	const float NaNMaxStep = FDustEnemyCombatAwarenessTestAccess::StepDistance(100.0f, 600.0f,
		std::numeric_limits<float>::quiet_NaN());
	TestTrue(TEXT("a non-finite max step falls back to a finite result"), FMath::IsFinite(NaNMaxStep));
	TestEqual(TEXT("non-finite max step uses the coded default"), NaNMaxStep, 400.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCreateSpaceMoveSpeedTest,
	"Dust2.EnemyCreateSpace.TemporarySpeedRestoredWithoutOverwritingOthers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyCreateSpaceMoveSpeedTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	if (!TestNotNull(TEXT("speed world"), Fixture.Get())) return false;
	AAIController* Controller = Fixture.Get()->SpawnActor<AAIController>();
	ACharacter* Character = Fixture.Get()->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("speed controller"), Controller) || !TestNotNull(TEXT("speed character"), Character)) return false;
	Controller->Possess(Character);
	UDustEnemyCombatAdapterComponent* Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
	Controller->AddInstanceComponent(Adapter);
	Adapter->RegisterComponent();
	FDustEnemyCombatAwarenessTestAccess::Prepare(Adapter, Controller, Character, nullptr, nullptr);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!TestNotNull(TEXT("character movement"), Movement)) return false;
	const float OriginalSpeed = Movement->MaxWalkSpeed;
	TestTrue(TEXT("spacing can take temporary speed ownership"), FDustEnemyCombatAwarenessTestAccess::ApplySpeed(Adapter));
	TestEqual(TEXT("spacing applies the configured retreat speed"), Movement->MaxWalkSpeed, 200.0f);
	FDustEnemyCombatAwarenessTestAccess::RestoreSpeed(Adapter);
	TestEqual(TEXT("spacing restores the original speed"), Movement->MaxWalkSpeed, OriginalSpeed);

	TestTrue(TEXT("second spacing applies speed again"), FDustEnemyCombatAwarenessTestAccess::ApplySpeed(Adapter));
	Movement->MaxWalkSpeed = 350.0f; // another system changes speed mid-move
	FDustEnemyCombatAwarenessTestAccess::RestoreSpeed(Adapter);
	TestEqual(TEXT("restore never overwrites a newer external speed"), Movement->MaxWalkSpeed, 350.0f);
	return true;
}


// These tests inject path result identities, not successful navigation. They exercise the
// same coordinator called by the native StateTree task plus real reload transactions.
namespace
{
	UDustEnemyReloadComponent* AddMobileReload(const FDustAwarenessTestWorld::FRig& Rig, float Seconds)
	{
		UDustEnemyReloadComponent* Reload = NewObject<UDustEnemyReloadComponent>(Rig.Pawn);
		Reload->LogicalReloadSeconds = Seconds;
		Reload->FailureCooldownSeconds = 0.01f;
		Rig.Pawn->AddInstanceComponent(Reload);
		Reload->RegisterComponent();
		return Reload;
	}
	FDustEnemyCreateSpaceTaskInstanceData BindMobileTask(const FDustAwarenessTestWorld::FRig& Rig,
		UDustEnemyReloadComponent* Reload, uint64 MoveToken)
	{
		FDustEnemyCreateSpaceTaskInstanceData Data;
		Data.Adapter = Rig.Adapter;
		Data.StartedAdapter = Rig.Adapter;
		Data.StartedWeapon = Rig.Weapon;
		Data.StartedReload = Reload;
		Data.StartedTarget = Rig.Target;
		Data.StartedRuntime = Rig.Weapon->GetWeaponRuntime();
		Data.MoveToken = MoveToken;
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyMobileReloadCompletionTest,
	"Dust2.EnemyCreateSpace.ArrivalOrPathFailureKeepsOwnedReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyMobileReloadCompletionTest::RunTest(const FString& Parameters)
{
	for (bool bArrival : {false, true})
	{
		FDustAwarenessTestWorld Fixture;
		const auto Rig = Fixture.SpawnRig(0.0f, true, 2, 0);
		if (!Rig.Adapter || !Rig.Weapon || !Rig.Target) return false;
		FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, Rig.Target->GetActorLocation(), true, false);
		UDustEnemyReloadComponent* Reload = AddMobileReload(Rig, 0.5f);
		AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
		Runtime->RestoreAmmo(0, 0);
		FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 701, 1500, 0.0f);
		auto Data = BindMobileTask(Rig, Reload, 701);
		if (bArrival)
		{
			Data.ReloadToken = Reload->TryStartReload();
			Data.ReloadRequestId = Runtime->GetReloadRequestId();
			if (!TestTrue(TEXT("an empty magazine can start its real reload while spacing is active"), Data.ReloadToken != 0)) return false;
			TestTrue(TEXT("movement ownership and reload coexist"), Rig.Adapter->IsCreateSpaceActive() && Reload->IsBusy());
		}
		TestEqual(TEXT("start never fills the magazine"), Runtime->GetCurrentAmmo(), 0);
		if (bArrival) FDustEnemyCombatAwarenessTestAccess::MoveFinished(Rig.Adapter, 1500, true);
		// Arrival is injected; the other case has no valid path request and fails naturally.
		TestTrue(TEXT("task stays active for reload after movement ends"),
			Data.TickOwnedOperations(0.1f) == EStateTreeRunStatus::Running);
		TestFalse(TEXT("movement token is released at arrival/failure"), Rig.Adapter->IsCreateSpaceActive());
		TestTrue(TEXT("task starts or preserves its reload when movement ends"), Reload->IsBusy());
		TestEqual(TEXT("partial reload still has an empty magazine"), Runtime->GetCurrentAmmo(), 0);
		const EStateTreeRunStatus Result = Data.TickOwnedOperations(0.5f);
		TestTrue(TEXT("both operations end before the task returns"), Result != EStateTreeRunStatus::Running);
		TestEqual(TEXT("completed reload fills one whole magazine"), Runtime->GetCurrentAmmo(), 2);
		TestEqual(TEXT("moving reload does not create reserve"), Runtime->GetReserveAmmo(), 0);
		TestFalse(TEXT("completed request releases reload ownership"), Reload->IsBusy());
		Data.CancelOwnedOperations();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyMobileReloadExitTest,
	"Dust2.EnemyCreateSpace.ExitDeathAndOldTokensProtectNewReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyMobileReloadExitTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture;
	const auto Rig = Fixture.SpawnRig(0.0f, true, 2, 0);
	if (!Rig.Adapter || !Rig.Weapon || !Rig.Target) return false;
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, Rig.Target->GetActorLocation(), true, false);
	UDustEnemyReloadComponent* Reload = AddMobileReload(Rig, 2.0f);
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	Runtime->RestoreAmmo(0, 0);
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 801, 1600, 0.0f);
	auto Data = BindMobileTask(Rig, Reload, 801);
	Data.ReloadToken = Reload->TryStartReload();
	Data.ReloadRequestId = Runtime->GetReloadRequestId();
	if (!TestTrue(TEXT("owned moving reload starts"), Data.ReloadToken != 0)) return false;
	auto OldExit = Data;
	Data.CancelOwnedOperations();
	TestFalse(TEXT("external Exit cancels its own movement"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("external Exit cancels its own reload"), Reload->IsBusy());
	TestEqual(TEXT("interrupted moving reload never commits"), Runtime->GetCurrentAmmo(), 0);
	Fixture.AdvanceWorld(0.1f);
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 802, 1601, 0.0f);
	const uint64 NewReload = Reload->TryStartReload();
	if (!TestTrue(TEXT("new reload starts after cancellation cooldown"), NewReload != 0)) return false;
	OldExit.CancelOwnedOperations();
	TestTrue(TEXT("old task Exit preserves newer movement"), Rig.Adapter->IsCreateSpaceActive());
	TestTrue(TEXT("old task Exit preserves newer reload"), Reload->IsBusy() && Runtime->IsReloading());
	Rig.Adapter->StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
	TestFalse(TEXT("death cancels the owned movement"), Rig.Adapter->IsCreateSpaceActive());
	TestFalse(TEXT("death cancels the reload fallback"), Reload->IsBusy());
	TestEqual(TEXT("death never fills the interrupted magazine"), Runtime->GetCurrentAmmo(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyMobileReloadWallTest,
	"Dust2.EnemyCreateSpace.WallStopsMovementButPreservesStartedReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyMobileReloadWallTest::RunTest(const FString& Parameters)
{
	FDustAwarenessTestWorld Fixture(true);
	const auto Rig = Fixture.SpawnRig(0.0f, true, 2, 0);
	if (!Rig.Adapter || !Rig.Weapon || !Rig.Target) return false;
	FDustEnemyCombatAwarenessTestAccess::SetKnown(Rig.Adapter, Rig.Target, Rig.Target->GetActorLocation(), true, false);
	UDustEnemyReloadComponent* Reload = AddMobileReload(Rig, 2.0f);
	AWeaponRuntime* Runtime = Rig.Weapon->GetWeaponRuntime();
	Runtime->RestoreAmmo(0, 0);
	FDustEnemyCombatAwarenessTestAccess::InjectSpacing(Rig.Adapter, 901, 1700, 0.0f);
	auto Data = BindMobileTask(Rig, Reload, 901);
	Data.ReloadToken = Reload->TryStartReload();
	Data.ReloadRequestId = Runtime->GetReloadRequestId();
	if (!TestTrue(TEXT("reload starts before the wall appears"), Data.ReloadToken != 0)) return false;
	if (!TestNotNull(TEXT("physical wall"), Fixture.SpawnWall(FVector(300.0f, 0.0f, 50.0f)))) return false;
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("wall releases movement ownership"), Rig.Adapter->IsCreateSpaceActive());
	TestTrue(TEXT("wall preserves started reload"), Reload->IsBusy());
	TestTrue(TEXT("search is deferred by the real reload"), Rig.Adapter->HasDeferredReloadSightLoss());
	TestTrue(TEXT("task stays alive after its move was cancelled"), Data.TickOwnedOperations(0.1f) == EStateTreeRunStatus::Running);
	Fixture.AdvanceWorld(1.0f);
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("normal loss grace does not prematurely start search"), Rig.Adapter->IsSearching());
	TestTrue(TEXT("reload survives past the normal grace"), Reload->IsBusy());
	Data.TickOwnedOperations(2.0f);
	TestEqual(TEXT("standing completion fills the magazine"), Runtime->GetCurrentAmmo(), 2);
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("search starts after reload completion"), Rig.Adapter->IsSearching());
	Data.CancelOwnedOperations();
	return true;
}

#endif
