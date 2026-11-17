#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AIController.h"
#include "Components/StateTreeAIComponent.h"
#include "Player/Dust2PlayerCharacter.h"
#include "AI/DustEnemyCombatAdapterComponent.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"

#include <limits>

/**
 * Bare-world coverage for sight-loss and gunshot search temporary MaxWalkSpeed ownership.
 *
 * These tests drive the real adapter search state and helpers with injected move identities.
 * They deliberately do not start StateTree logic or navigation: actual MoveTo, slowdown timing
 * in PIE, the run animation and the search/patrol handoff remain PIE acceptance items.
 */
struct FDustEnemySearchMovementTestAccess
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

	static float Now(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		const UWorld* World = Adapter ? Adapter->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0f;
	}

	/** Injects an already-current sight-loss search without depending on MoveTo or the tree. */
	static void InjectSightSearch(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source,
		const FVector& FrozenLocation, uint64 Serial, float Deadline)
	{
		Adapter->ClearSearch();
		Adapter->Target.Reset();
		Adapter->bSightCurrentlySensed = false;
		Adapter->bConfirmedTarget = false;
		Adapter->bInLossGrace = false;
		Adapter->bDeferredReloadSightLoss = false;
		Adapter->SearchTarget = Source;
		Adapter->bSearching = true;
		Adapter->bHearingSearch = false;
		Adapter->HearingSearchPriority = 0;
		Adapter->Generation = Serial;
		Adapter->LastKnownLocation = FrozenLocation;
		Adapter->SearchDeadline = Deadline;
		Adapter->bRetryUsed = false;
		Adapter->SearchMoveId = FAIRequestID(7000);
		Adapter->ImmediateMoveId = FAIRequestID::InvalidRequest;
	}

	static void SetKnownTarget(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source,
		const FVector& FrozenLocation)
	{
		Adapter->Target = Source;
		Adapter->bSightCurrentlySensed = true;
		Adapter->bConfirmedTarget = true;
		Adapter->LastKnownLocation = FrozenLocation;
		Adapter->LastConfirmedTime = Now(Adapter);
	}

	static void LoseKnownTarget(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source,
		const FVector& FrozenLocation)
	{
		Adapter->Lose(Source, FrozenLocation, false);
	}

	static void Clear(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->ClearSearch(); }
	static bool Apply(UDustEnemyCombatAdapterComponent* Adapter, bool bNearGoal)
	{
		return Adapter->ApplySearchMovementSpeed(bNearGoal);
	}
	static void Update(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->UpdateSearchMovementSpeed(); }
	static void Restore(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->RestoreSearchMovementSpeed(); }
	static void Resolve(UDustEnemyCombatAdapterComponent* Adapter, uint64 Serial, bool bSucceeded)
	{
		Adapter->ResolveSearchMove(Serial, bSucceeded);
	}
	static void Hear(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source, const FAIStimulus& Stimulus)
	{
		Adapter->HandleHearing(Source, Stimulus);
	}

	static bool SpeedApplied(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bSearchSpeedApplied; }
	static bool Slowed(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bSearchSpeedSlowed; }
	static float AppliedSpeed(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->SearchAppliedMaxWalkSpeed; }
	static float SavedSpeed(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->SearchSavedMaxWalkSpeed; }
	static UCharacterMovementComponent* OwnedMovement(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->SearchMovement.Get();
	}
	static float SearchDeadline(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->SearchDeadline; }
	static void SetSearchDeadline(UDustEnemyCombatAdapterComponent* Adapter, float Value) { Adapter->SearchDeadline = Value; }
	static uint64 Generation(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->Generation; }
	static bool RetryUsed(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bRetryUsed; }
	static void SetRetryUsed(UDustEnemyCombatAdapterComponent* Adapter, bool bUsed) { Adapter->bRetryUsed = bUsed; }
	static bool SearchTimerActive(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		UWorld* World = Adapter ? Adapter->GetWorld() : nullptr;
		return World && World->GetTimerManager().IsTimerActive(Adapter->SearchTimer);
	}
	static bool Hearing(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bHearingSearch; }
	static float HearingDeadline(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->HearingDeadline; }
	static int32 HearingPriority(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->HearingSearchPriority; }
	static void SetHearingUpdateAt(UDustEnemyCombatAdapterComponent* Adapter, float Value) { Adapter->NextHearingUpdateAt = Value; }
	static void SetHearingRedirects(UDustEnemyCombatAdapterComponent* Adapter, int32 Value) { Adapter->HearingRedirectCount = Value; }
	/** Keeps FinishSearch from starting the no-asset test StateTree; the real cleanup still runs. */
	static void GuardIdleTree(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->bCreateSpaceActive = true; }

	static FAIStimulus MakeHearingStimulus(const FVector& Location, FName Tag)
	{
		UAISense_Hearing* HearingSense = GetMutableDefault<UAISense_Hearing>();
		FAIStimulus Stimulus(*HearingSense, 1.0f, Location, FVector::ZeroVector,
			FAIStimulus::SensingSucceeded, Tag);
		Stimulus.SetExpirationAge(60.0f);
		return Stimulus;
	}

	static FAIStimulus MakeFootstep(const FVector& Location)
	{
		return MakeHearingStimulus(Location, FName(TEXT("Footstep")));
	}

	static FAIStimulus MakeGunshot(const FVector& Location)
	{
		return MakeHearingStimulus(Location, FName(TEXT("Gunshot")));
	}
};

namespace
{
	struct FSearchMovementTestWorld
	{
		UWorld* World = nullptr;

		FSearchMovementTestWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.CreateNavigation(false)
				.CreateAISystem(true)
				.ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FSearchMovementTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World = nullptr;
			}
		}

		struct FRig
		{
			AAIController* Controller = nullptr;
			ACharacter* Pawn = nullptr;
			ADust2PlayerCharacter* Target = nullptr;
			ACharacter* Other = nullptr;
			UStateTreeAIComponent* Tree = nullptr;
			UDustEnemyWeaponComponent* Weapon = nullptr;
			UDustEnemyCombatAdapterComponent* Adapter = nullptr;
			UCharacterMovementComponent* Movement = nullptr;
		};

		FRig SpawnRig() const
		{
			FRig Rig;
			if (!World) return Rig;
			Rig.Controller = World->SpawnActor<AAIController>();
			Rig.Pawn = World->SpawnActor<ACharacter>();
			Rig.Target = World->SpawnActor<ADust2PlayerCharacter>();
			Rig.Other = World->SpawnActor<ACharacter>();
			if (!Rig.Controller || !Rig.Pawn || !Rig.Target || !Rig.Other) return FRig();
			Rig.Controller->Possess(Rig.Pawn);
			Rig.Pawn->SetActorLocation(FVector::ZeroVector);
			Rig.Target->SetActorLocation(FVector(2000.0f, 0.0f, 0.0f));
			Rig.Other->SetActorLocation(FVector(-300.0f, 0.0f, 0.0f));

			Rig.Movement = Rig.Pawn->GetCharacterMovement();
			UCharacterMovementComponent* OtherMovement = Rig.Other->GetCharacterMovement();
			if (!Rig.Movement || !OtherMovement) return FRig();
			Rig.Movement->SetMovementMode(MOVE_None);
			Rig.Movement->GravityScale = 0.0f;
			Rig.Movement->MaxWalkSpeed = 600.0f;
			OtherMovement->SetMovementMode(MOVE_None);
			OtherMovement->GravityScale = 0.0f;
			OtherMovement->MaxWalkSpeed = 350.0f;
			if (UCharacterMovementComponent* TargetMovement = Rig.Target->GetCharacterMovement())
			{
				TargetMovement->SetMovementMode(MOVE_None);
				TargetMovement->GravityScale = 0.0f;
			}

			Rig.Tree = NewObject<UStateTreeAIComponent>(Rig.Controller);
			Rig.Tree->SetStartLogicAutomatically(false);
			Rig.Controller->AddInstanceComponent(Rig.Tree);
			Rig.Tree->RegisterComponent();

			Rig.Weapon = NewObject<UDustEnemyWeaponComponent>(Rig.Pawn);
			Rig.Pawn->AddInstanceComponent(Rig.Weapon);
			Rig.Weapon->RegisterComponent();
			Rig.Weapon->SetComponentTickEnabled(false);
			Rig.Weapon->PrimaryComponentTick.bCanEverTick = false;

			Rig.Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Rig.Controller);
			Rig.Controller->AddInstanceComponent(Rig.Adapter);
			Rig.Adapter->RegisterComponent();
			FDustEnemySearchMovementTestAccess::Prepare(Rig.Adapter, Rig.Controller, Rig.Pawn, Rig.Tree, Rig.Weapon);
			return Rig;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemySearchRunWalkTest,
	"Dust2.AI.SearchMovement.RunThenWalkIsOneWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemySearchRunWalkTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("search movement world"), Fixture.World)) return false;
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("pawn"), Rig.Pawn) || !TestNotNull(TEXT("target"), Rig.Target)
		|| !TestNotNull(TEXT("adapter"), Rig.Adapter) || !TestNotNull(TEXT("movement"), Rig.Movement)) return false;

	const float OriginalSpeed = Rig.Movement->MaxWalkSpeed;
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(1000.0f, 0.0f, 0.0f), 11,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 12.0f);
	Rig.Target->SetActorLocation(FVector(-4000.0f, 0.0f, 0.0f));
	TestTrue(TEXT("The search keeps the frozen last-seen point, not the live target"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(FVector(1000.0f, 0.0f, 0.0f), 0.01f));
	TestTrue(TEXT("A sight-loss search can take the controlled movement's speed"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	TestEqual(TEXT("A far search point starts at the run speed"), Rig.Movement->MaxWalkSpeed, 450.0f);
	TestEqual(TEXT("The original speed is captured before the apply"),
		FDustEnemySearchMovementTestAccess::SavedSpeed(Rig.Adapter), OriginalSpeed);
	TestTrue(TEXT("Only the controlled CharacterMovement is owned"),
		FDustEnemySearchMovementTestAccess::OwnedMovement(Rig.Adapter) == Rig.Movement);
	TestFalse(TEXT("A far apply is not yet the near phase"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));

	Rig.Pawn->SetActorLocation(FVector(650.0f, 0.0f, 0.0f)); // 350 cm from the frozen point
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Outside the slowdown distance the run speed is unchanged"),
		Rig.Movement->MaxWalkSpeed, 450.0f);
	TestFalse(TEXT("Outside the slowdown distance no near phase starts"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));

	Rig.Pawn->SetActorLocation(FVector(710.0f, 0.0f, 0.0f)); // 290 cm: inside 300
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("The tick switches to the near-goal walk speed once"),
		Rig.Movement->MaxWalkSpeed, 200.0f);
	TestTrue(TEXT("The near phase is recorded"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));
	TestEqual(TEXT("The applied speed now matches the walk phase"),
		FDustEnemySearchMovementTestAccess::AppliedSpeed(Rig.Adapter), 200.0f);

	Rig.Pawn->SetActorLocation(FVector::ZeroVector); // 1000 cm again
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Leaving the slowdown distance never returns to the run speed"),
		Rig.Movement->MaxWalkSpeed, 200.0f);
	TestTrue(TEXT("The one-way phase stays at walk"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));

	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("ClearSearch restores the captured speed"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestFalse(TEXT("ClearSearch drops the speed claim"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestEqual(TEXT("ClearSearch also clears the sight deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);

	// A move that starts inside the slowdown distance starts directly in the near phase.
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(200.0f, 0.0f, 0.0f), 12,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 12.0f);
	TestTrue(TEXT("A near point can start with temporary speed"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, true));
	TestEqual(TEXT("A near point starts at the walk speed"), Rig.Movement->MaxWalkSpeed, 200.0f);
	TestTrue(TEXT("A near start is already in the near phase"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("The second search also restores the original speed"),
		Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemySearchCleanupTest,
	"Dust2.AI.SearchMovement.ClearResolutionAndDeadlineCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemySearchCleanupTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("search cleanup world"), Fixture.World)) return false;
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("cleanup pawn"), Rig.Pawn) || !TestNotNull(TEXT("cleanup adapter"), Rig.Adapter)
		|| !TestNotNull(TEXT("cleanup movement"), Rig.Movement)) return false;
	const float OriginalSpeed = Rig.Movement->MaxWalkSpeed;

	// ClearSearch is the shared cleanup primitive for timeout, reacquire and life end.
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(900.0f, 0.0f, 0.0f), 21,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 9.0f);
	FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false);
	TestEqual(TEXT("Cleanup starts from an owned run speed"), Rig.Movement->MaxWalkSpeed, 450.0f);
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("ClearSearch restores the pre-search speed"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestFalse(TEXT("ClearSearch clears the applied flag"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestNull(TEXT("ClearSearch releases the owned movement"),
		FDustEnemySearchMovementTestAccess::OwnedMovement(Rig.Adapter));

	// The single failed-move retry restores speed while waiting and keeps the same deadline.
	const float FailureDeadline = FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 7.0f;
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(900.0f, 0.0f, 0.0f), 22, FailureDeadline);
	FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false);
	FDustEnemySearchMovementTestAccess::Resolve(Rig.Adapter, 22, false);
	TestEqual(TEXT("A failed search move restores the original speed"),
		Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestFalse(TEXT("A failed move releases the speed claim"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestTrue(TEXT("The first failure keeps the existing one-retry budget"),
		FDustEnemySearchMovementTestAccess::RetryUsed(Rig.Adapter));
	TestTrue(TEXT("The retry is scheduled while speed is already restored"),
		FDustEnemySearchMovementTestAccess::SearchTimerActive(Rig.Adapter));
	TestEqual(TEXT("The failure/retry path never extends the sight deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), FailureDeadline);
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestFalse(TEXT("Cleanup cancels the pending retry"),
		FDustEnemySearchMovementTestAccess::SearchTimerActive(Rig.Adapter));

	// Arrival switches to the wait without leaving the temporary speed behind.
	const float ArrivalDeadline = FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 8.0f;
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(900.0f, 0.0f, 0.0f), 23, ArrivalDeadline);
	FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, true);
	TestEqual(TEXT("Arrival starts from an owned walk speed"), Rig.Movement->MaxWalkSpeed, 200.0f);
	FDustEnemySearchMovementTestAccess::Resolve(Rig.Adapter, 23, true);
	TestEqual(TEXT("Arrival restores the original speed before waiting"),
		Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestTrue(TEXT("Arrival starts the existing wait timer"),
		FDustEnemySearchMovementTestAccess::SearchTimerActive(Rig.Adapter));
	TestEqual(TEXT("Arrival keeps the original sight deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), ArrivalDeadline);
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(900.0f, 0.0f, 0.0f), 24, ArrivalDeadline);
	FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false);
	Rig.Adapter->StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
	TestEqual(TEXT("Life stop restores the pre-search speed"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestFalse(TEXT("Life stop releases the search speed claim"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestEqual(TEXT("Life stop clears the sight-search deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemySearchExternalSpeedTest,
	"Dust2.AI.SearchMovement.ExternalSpeedSurvivesTickCleanupAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemySearchExternalSpeedTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("external speed world"), Fixture.World)) return false;
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("external speed pawn"), Rig.Pawn) || !TestNotNull(TEXT("external speed adapter"), Rig.Adapter)
		|| !TestNotNull(TEXT("external speed movement"), Rig.Movement)) return false;

	// External takeover while running: the near-phase tick must not write over the newer value.
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(1000.0f, 0.0f, 0.0f), 31,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 12.0f);
	FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false);
	TestEqual(TEXT("The search owns its run speed before the external write"),
		Rig.Movement->MaxWalkSpeed, 450.0f);
	Rig.Pawn->SetActorLocation(FVector(950.0f, 0.0f, 0.0f));
	Rig.Movement->MaxWalkSpeed = 360.0f; // another system takes ownership
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("A later tick never overwrites an external speed"), Rig.Movement->MaxWalkSpeed, 360.0f);
	TestFalse(TEXT("External takeover drops our speed claim"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestFalse(TEXT("External takeover also clears the near-phase claim"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));
	TestNull(TEXT("External takeover releases the movement pointer"),
		FDustEnemySearchMovementTestAccess::OwnedMovement(Rig.Adapter));
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("A second later tick still leaves the external speed alone"),
		Rig.Movement->MaxWalkSpeed, 360.0f);
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("Cleanup after takeover preserves the external speed"),
		Rig.Movement->MaxWalkSpeed, 360.0f);

	// A retry in the same search must respect an external takeover even without a Tick.
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(1000.0f, 0.0f, 0.0f), 32,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 12.0f);
	FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false);
	TestEqual(TEXT("The retry fixture applies its run speed"), Rig.Movement->MaxWalkSpeed, 450.0f);
	Rig.Movement->MaxWalkSpeed = 360.0f;
	FDustEnemySearchMovementTestAccess::Restore(Rig.Adapter);
	TestEqual(TEXT("Failure cleanup leaves a newer external speed untouched"),
		Rig.Movement->MaxWalkSpeed, 360.0f);
	TestFalse(TEXT("A retry cannot take speed back from another system"), FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	TestEqual(TEXT("The retried move preserves the external speed"), Rig.Movement->MaxWalkSpeed, 360.0f);
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("Retry cleanup preserves the external value"),
		Rig.Movement->MaxWalkSpeed, 360.0f);
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(1000.0f, 0.0f, 0.0f), 33,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 12.0f);
	TestTrue(TEXT("A new search can acquire speed after the old claim is cleared"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, true));
	Rig.Movement->MaxWalkSpeed = 310.0f;
	FDustEnemySearchMovementTestAccess::Update(Rig.Adapter);
	TestFalse(TEXT("External takeover is detected in the slow phase too"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestFalse(TEXT("A slow-phase takeover also blocks retry ownership"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("Slow-phase cleanup preserves external speed"), Rig.Movement->MaxWalkSpeed, 310.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyFootstepSearchNoSpeedTest,
	"Dust2.AI.SearchMovement.FootstepNeverOwnsSpeedOrSightDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyFootstepSearchNoSpeedTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("hearing search world"), Fixture.World)) return false;
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("hearing pawn"), Rig.Pawn) || !TestNotNull(TEXT("hearing target"), Rig.Target)
		|| !TestNotNull(TEXT("hearing adapter"), Rig.Adapter) || !TestNotNull(TEXT("hearing movement"), Rig.Movement)) return false;

	const float OriginalSpeed = Rig.Movement->MaxWalkSpeed;
	const FVector NoiseLocation(400.0f, 0.0f, 0.0f);
	FDustEnemySearchMovementTestAccess::Hear(Rig.Adapter, Rig.Target,
		FDustEnemySearchMovementTestAccess::MakeFootstep(NoiseLocation));
	TestTrue(TEXT("The footstep starts the existing hearing investigation"),
		FDustEnemySearchMovementTestAccess::Hearing(Rig.Adapter));
	TestTrue(TEXT("The adapter reports a search"), Rig.Adapter->IsSearching());
	TestEqual(TEXT("A footstep never changes the Pawn's speed"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestFalse(TEXT("A footstep never takes speed ownership"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestEqual(TEXT("A footstep never starts the sight-search deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);
	const float HearingDeadline = FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter);
	TestTrue(TEXT("The existing hearing deadline still starts"),
		HearingDeadline >= FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + Rig.Adapter->HearingMaxSeconds - 0.01f);
	TestTrue(TEXT("The search location is the frozen event location, not the live player"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(NoiseLocation, 0.01f));

	Rig.Target->SetActorLocation(FVector(-2000.0f, 0.0f, 0.0f));
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("A hearing search tick never applies search speed"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestEqual(TEXT("A hearing search never creates a sight deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);
	TestEqual(TEXT("A hearing search keeps its own deadline unchanged"),
		FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter), HearingDeadline);
	TestTrue(TEXT("The hearing location stays frozen while the player moves"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(NoiseLocation, 0.01f));

	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestFalse(TEXT("ClearSearch ends the hearing search"),
		FDustEnemySearchMovementTestAccess::Hearing(Rig.Adapter));
	TestEqual(TEXT("ClearSearch clears the hearing deadline"),
		FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter), 0.0f);
	TestEqual(TEXT("ClearSearch leaves the Pawn's speed alone"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyGunshotSearchSpeedTest,
	"Dust2.AI.SearchMovement.GunshotUsesSearchSpeedWithoutSightDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyGunshotSearchSpeedTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("gunshot speed world"), Fixture.World)) return false;
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("gunshot speed pawn"), Rig.Pawn) || !TestNotNull(TEXT("gunshot speed target"), Rig.Target)
		|| !TestNotNull(TEXT("gunshot speed adapter"), Rig.Adapter) || !TestNotNull(TEXT("gunshot speed movement"), Rig.Movement)) return false;

	const float OriginalSpeed = Rig.Movement->MaxWalkSpeed;
	const FVector FootstepLocation(500.0f, 0.0f, 0.0f);
	FDustEnemySearchMovementTestAccess::Hear(Rig.Adapter, Rig.Target,
		FDustEnemySearchMovementTestAccess::MakeFootstep(FootstepLocation));
	TestTrue(TEXT("The initial footstep investigation starts"), FDustEnemySearchMovementTestAccess::Hearing(Rig.Adapter));
	TestEqual(TEXT("Footsteps keep priority 1"), FDustEnemySearchMovementTestAccess::HearingPriority(Rig.Adapter), 1);
	TestEqual(TEXT("The footstep does not own speed"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestFalse(TEXT("The footstep has no speed claim"), FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	const float FootstepDeadline = FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter);
	const uint64 FootstepSerial = FDustEnemySearchMovementTestAccess::Generation(Rig.Adapter);
	// No navigation exists in this fixture. Model a still-moving footstep request, whose
	// failure budget has not been consumed, before testing the real gunshot upgrade handler.
	FDustEnemySearchMovementTestAccess::SetRetryUsed(Rig.Adapter, false);

	const FVector GunshotLocation(1100.0f, 0.0f, 0.0f);
	FDustEnemySearchMovementTestAccess::Hear(Rig.Adapter, Rig.Target,
		FDustEnemySearchMovementTestAccess::MakeGunshot(GunshotLocation));
	TestTrue(TEXT("A gunshot upgrades the active footstep investigation"), FDustEnemySearchMovementTestAccess::Hearing(Rig.Adapter));
	TestEqual(TEXT("Gunshots use priority 2"), FDustEnemySearchMovementTestAccess::HearingPriority(Rig.Adapter), 2);
	TestTrue(TEXT("The upgrade invalidates the old move generation"),
		FDustEnemySearchMovementTestAccess::Generation(Rig.Adapter) != FootstepSerial);
	TestEqual(TEXT("The upgrade keeps the original hearing deadline"),
		FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter), FootstepDeadline);
	TestTrue(TEXT("The upgrade tracks the new frozen gunshot event location"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(GunshotLocation, 0.01f));
	TestEqual(TEXT("A gunshot investigation never creates the sight-search deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);

	// The bare world has no nav path, so the real StartSearchMove restores after its failure.
	// Apply directly to prove gunshot eligibility owns the same run/walk speeds as sight search.
	TestTrue(TEXT("A priority-2 investigation can take temporary speed"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	TestEqual(TEXT("A far gunshot starts at the existing run speed"), Rig.Movement->MaxWalkSpeed, 450.0f);
	TestEqual(TEXT("Gunshot speed ownership never creates a sight deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);
	TestEqual(TEXT("Gunshot speed ownership leaves the hearing deadline frozen"),
		FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter), FootstepDeadline);
	TestTrue(TEXT("A near gunshot can switch to the existing walk speed"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, true));
	TestEqual(TEXT("The near gunshot uses the existing 200 walk speed"), Rig.Movement->MaxWalkSpeed, 200.0f);
	TestTrue(TEXT("The near gunshot records the one-way slow phase"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("Gunshot cleanup restores the original speed"), Rig.Movement->MaxWalkSpeed, OriginalSpeed);
	TestEqual(TEXT("Gunshot cleanup clears the sight deadline"), FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);
	TestEqual(TEXT("Gunshot cleanup clears the hearing deadline"), FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter), 0.0f);

	const FVector GunshotStart(300.0f, 0.0f, 0.0f);
	FDustEnemySearchMovementTestAccess::Hear(Rig.Adapter, Rig.Target,
		FDustEnemySearchMovementTestAccess::MakeGunshot(GunshotStart));
	const uint64 GunshotSerial = FDustEnemySearchMovementTestAccess::Generation(Rig.Adapter);
	const float GunshotDeadline = FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter);
	FDustEnemySearchMovementTestAccess::SetHearingUpdateAt(Rig.Adapter,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) - 1.0f);
	FDustEnemySearchMovementTestAccess::SetHearingRedirects(Rig.Adapter, 0);
	FDustEnemySearchMovementTestAccess::Hear(Rig.Adapter, Rig.Target,
		FDustEnemySearchMovementTestAccess::MakeFootstep(FVector(900.0f, 0.0f, 0.0f)));
	TestEqual(TEXT("A later footstep cannot downgrade the gunshot priority"),
		FDustEnemySearchMovementTestAccess::HearingPriority(Rig.Adapter), 2);
	TestTrue(TEXT("A rejected downgrade keeps the frozen gunshot location"),
		Rig.Adapter->GetLastKnownTargetLocation().Equals(GunshotStart, 0.01f));
	TestEqual(TEXT("A rejected downgrade preserves the request generation"),
		FDustEnemySearchMovementTestAccess::Generation(Rig.Adapter), GunshotSerial);
	TestEqual(TEXT("A rejected downgrade never extends the hearing deadline"),
		FDustEnemySearchMovementTestAccess::HearingDeadline(Rig.Adapter), GunshotDeadline);
	TestTrue(TEXT("The remaining gunshot still owns search speed"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	TestEqual(TEXT("The rejected footstep leaves the gunshot run speed in place"), Rig.Movement->MaxWalkSpeed, 450.0f);
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemySearchDeadlineTest,
	"Dust2.AI.SearchMovement.SightDeadlineStartsOnceAndExpiresIntoCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemySearchDeadlineTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("search deadline world"), Fixture.World)) return false;
	// Advance the empty world so the injected expired deadline remains a positive absolute time.
	Fixture.World->Tick(LEVELTICK_All, 2.0f);
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("deadline pawn"), Rig.Pawn) || !TestNotNull(TEXT("deadline target"), Rig.Target)
		|| !TestNotNull(TEXT("deadline adapter"), Rig.Adapter) || !TestNotNull(TEXT("deadline movement"), Rig.Movement)) return false;

	Rig.Adapter->SearchMaxSeconds = std::numeric_limits<float>::quiet_NaN();
	const FVector FrozenLocation(1000.0f, 0.0f, 0.0f);
	FDustEnemySearchMovementTestAccess::SetKnownTarget(Rig.Adapter, Rig.Target, FrozenLocation);
	const float BeforeLoss = FDustEnemySearchMovementTestAccess::Now(Rig.Adapter);
	FDustEnemySearchMovementTestAccess::LoseKnownTarget(Rig.Adapter, Rig.Target, FrozenLocation);
	TestTrue(TEXT("Sight loss starts the existing search"), Rig.Adapter->IsSearching());
	const float Deadline = FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter);
	TestTrue(TEXT("The sight deadline is finite even with a non-finite configured limit"),
		FMath::IsFinite(Deadline));
	TestTrue(TEXT("A non-finite search limit falls back to the coded 12 seconds"),
		FMath::IsNearlyEqual(Deadline - BeforeLoss, 12.0f, 0.05f));

	// Timeout cannot start the no-asset test StateTree, but it must run the real cleanup.
	FDustEnemySearchMovementTestAccess::GuardIdleTree(Rig.Adapter);
	TestTrue(TEXT("A timeout fixture owns temporary speed"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	TestEqual(TEXT("The timeout fixture starts at run speed"), Rig.Movement->MaxWalkSpeed, 450.0f);
	FDustEnemySearchMovementTestAccess::SetSearchDeadline(Rig.Adapter,
		FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) * 0.5f);
	const uint64 BeforeExpiry = FDustEnemySearchMovementTestAccess::Generation(Rig.Adapter);
	Rig.Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("The expired sight search ends"), Rig.Adapter->IsSearching());
	TestEqual(TEXT("Expiry clears the sight deadline"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Rig.Adapter), 0.0f);
	TestEqual(TEXT("Expiry restores the temporary speed"), Rig.Movement->MaxWalkSpeed, 600.0f);
	TestFalse(TEXT("Expiry releases the speed claim"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Rig.Adapter));
	TestTrue(TEXT("Expiry invalidates the finished search generation"),
		FDustEnemySearchMovementTestAccess::Generation(Rig.Adapter) != BeforeExpiry);
	TestFalse(TEXT("Expiry cancels any pending search timer"),
		FDustEnemySearchMovementTestAccess::SearchTimerActive(Rig.Adapter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemySearchInvalidValuesTest,
	"Dust2.AI.SearchMovement.NonFiniteTuningUsesFiniteDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemySearchInvalidValuesTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("invalid values world"), Fixture.World)) return false;
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("invalid values pawn"), Rig.Pawn) || !TestNotNull(TEXT("invalid values adapter"), Rig.Adapter)
		|| !TestNotNull(TEXT("invalid values movement"), Rig.Movement)) return false;

	Rig.Adapter->SearchRunSpeed = std::numeric_limits<float>::quiet_NaN();
	Rig.Adapter->SearchWalkSpeed = std::numeric_limits<float>::infinity();
	Rig.Adapter->SearchSlowdownDistance = std::numeric_limits<float>::quiet_NaN();
	Rig.Adapter->SearchAcceptanceRadius = 500.0f;
	Rig.Pawn->SetActorLocation(FVector(400.0f, 0.0f, 0.0f));
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector::ZeroVector, 61, FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 12.0f);
	TestTrue(TEXT("Non-finite speeds still allow temporary ownership"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	TestEqual(TEXT("A non-finite run speed falls back to 450"), Rig.Movement->MaxWalkSpeed, 450.0f);
	TestTrue(TEXT("The applied run speed stays finite"),
		FMath::IsFinite(FDustEnemySearchMovementTestAccess::AppliedSpeed(Rig.Adapter)));

	// The default slowdown is 300, but the effective threshold must not be below acceptance.
	FDustEnemySearchMovementTestAccess::Update(Rig.Adapter);
	TestEqual(TEXT("A non-finite slowdown uses 300 and is clamped up to the 500 acceptance"),
		Rig.Movement->MaxWalkSpeed, 200.0f);
	TestTrue(TEXT("The near phase is recorded with the sanitized walk speed"),
		FDustEnemySearchMovementTestAccess::Slowed(Rig.Adapter));
	TestEqual(TEXT("A non-finite walk speed falls back to 200"), Rig.Movement->MaxWalkSpeed, 200.0f);
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("Sanitized ownership still restores the original speed"),
		Rig.Movement->MaxWalkSpeed, 600.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemySearchMovementIdentityTest,
	"Dust2.AI.SearchMovement.OnlyControlledMovementIsOwned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemySearchMovementIdentityTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("movement identity world"), Fixture.World)) return false;
	const FSearchMovementTestWorld::FRig Rig = Fixture.SpawnRig();
	if (!TestNotNull(TEXT("identity pawn"), Rig.Pawn) || !TestNotNull(TEXT("identity other"), Rig.Other)
		|| !TestNotNull(TEXT("identity adapter"), Rig.Adapter) || !TestNotNull(TEXT("identity movement"), Rig.Movement)) return false;

	UCharacterMovementComponent* OtherMovement = Rig.Other->GetCharacterMovement();
	if (!TestNotNull(TEXT("other movement"), OtherMovement)) return false;
	const float OtherOriginal = OtherMovement->MaxWalkSpeed;
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Rig.Adapter, Rig.Target,
		FVector(1000.0f, 0.0f, 0.0f), 71, FDustEnemySearchMovementTestAccess::Now(Rig.Adapter) + 12.0f);
	TestTrue(TEXT("Identity fixture applies run speed"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, false));
	TestEqual(TEXT("The controlled pawn receives the run speed"), Rig.Movement->MaxWalkSpeed, 450.0f);
	TestEqual(TEXT("An unrelated character keeps its speed"), OtherMovement->MaxWalkSpeed, OtherOriginal);
	TestTrue(TEXT("The captured movement is the controlled pawn's component"),
		FDustEnemySearchMovementTestAccess::OwnedMovement(Rig.Adapter) == Rig.Movement);

	TestTrue(TEXT("Identity fixture switches to walk"),
		FDustEnemySearchMovementTestAccess::Apply(Rig.Adapter, true));
	TestEqual(TEXT("The controlled pawn receives the walk speed"), Rig.Movement->MaxWalkSpeed, 200.0f);
	TestEqual(TEXT("The unrelated character is still untouched"), OtherMovement->MaxWalkSpeed, OtherOriginal);
	OtherMovement->MaxWalkSpeed = 320.0f; // an external change on another pawn
	FDustEnemySearchMovementTestAccess::Clear(Rig.Adapter);
	TestEqual(TEXT("Cleanup restores only the controlled pawn"), Rig.Movement->MaxWalkSpeed, 600.0f);
	TestEqual(TEXT("Cleanup never rewrites another pawn's newer speed"), OtherMovement->MaxWalkSpeed, 320.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemySearchNoCharacterTest,
	"Dust2.AI.SearchMovement.PawnWithoutCharacterMovementKeepsSearching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemySearchNoCharacterTest::RunTest(const FString& Parameters)
{
	FSearchMovementTestWorld Fixture;
	if (!TestNotNull(TEXT("no-character world"), Fixture.World)) return false;
	AAIController* Controller = Fixture.World->SpawnActor<AAIController>();
	APawn* Pawn = Fixture.World->SpawnActor<APawn>();
	ADust2PlayerCharacter* Target = Fixture.World->SpawnActor<ADust2PlayerCharacter>();
	if (!TestNotNull(TEXT("no-character controller"), Controller) || !TestNotNull(TEXT("no-character pawn"), Pawn)
		|| !TestNotNull(TEXT("no-character target"), Target)) return false;
	Controller->Possess(Pawn);
	UStateTreeAIComponent* Tree = NewObject<UStateTreeAIComponent>(Controller);
	Tree->SetStartLogicAutomatically(false);
	Controller->AddInstanceComponent(Tree);
	Tree->RegisterComponent();
	UDustEnemyWeaponComponent* Weapon = NewObject<UDustEnemyWeaponComponent>(Pawn);
	Pawn->AddInstanceComponent(Weapon);
	Weapon->RegisterComponent();
	UDustEnemyCombatAdapterComponent* Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
	Controller->AddInstanceComponent(Adapter);
	Adapter->RegisterComponent();
	FDustEnemySearchMovementTestAccess::Prepare(Adapter, Controller, Pawn, Tree, Weapon);

	const float Deadline = FDustEnemySearchMovementTestAccess::Now(Adapter) + 12.0f;
	FDustEnemySearchMovementTestAccess::InjectSightSearch(Adapter, Target,
		FVector(500.0f, 0.0f, 0.0f), 81, Deadline);
	TestFalse(TEXT("A Pawn without CharacterMovement has no temporary speed to own"),
		FDustEnemySearchMovementTestAccess::Apply(Adapter, false));
	TestFalse(TEXT("No speed claim is recorded without CharacterMovement"),
		FDustEnemySearchMovementTestAccess::SpeedApplied(Adapter));
	TestEqual(TEXT("The original search deadline is still present"),
		FDustEnemySearchMovementTestAccess::SearchDeadline(Adapter), Deadline);
	Adapter->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("The original search still runs without temporary speed"), Adapter->IsSearching());
	FDustEnemySearchMovementTestAccess::Clear(Adapter);
	return true;
}

#endif
