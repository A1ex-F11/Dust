#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AIController.h"
#include "Components/StateTreeAIComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"

#include <limits>

/**
 * W10 footstep-hearing regression access.
 *
 * Setup mirrors DustEnemyCombatAdapterTests.cpp: a bare game world (AI system on, navigation off),
 * a possessed plain Pawn, a StateTreeAIComponent with StartLogicAutomatically(false) and no asset,
 * a plain weapon component, and an adapter prepared through its private fields. HandleHearing is
 * driven directly; the world is never ticked, so timers and AI cannot step behind the assertions.
 *
 * All private members are reached through this friend struct. Tests deliberately never call
 * FinishSearch on a current generation (that would ClearSearch + ResumeIdle against a Tree with no
 * asset); stale-serial calls are used to cover the late-callback contract instead.
 */
struct FDustEnemyHearingTestAccess
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

	// ---- stimulus construction -------------------------------------------------------------

	static FAIStimulus MakeHearingStimulus(const FVector& Location, float Strength = 1.0f,
		FName Tag = FName(TEXT("Footstep")),
		FAIStimulus::FResult Result = FAIStimulus::SensingSucceeded)
	{
		UAISense_Hearing* Hearing = GetMutableDefault<UAISense_Hearing>();
		FAIStimulus Stimulus(*Hearing, Strength, Location, FVector::ZeroVector, Result, Tag);
		// Wide by default: aged stimuli stay "successful, not expired" unless a test narrows it.
		Stimulus.SetExpirationAge(60.0f);
		return Stimulus;
	}

	static FAIStimulus MakeSightStimulus(const FVector& Location)
	{
		UAISense_Sight* Sight = GetMutableDefault<UAISense_Sight>();
		FAIStimulus Stimulus(*Sight, 1.0f, Location, FVector::ZeroVector,
			FAIStimulus::SensingSucceeded, FName(TEXT("Footstep")));
		Stimulus.SetExpirationAge(60.0f);
		return Stimulus;
	}

	// ---- driving the handler ---------------------------------------------------------------

	static void Hear(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source, const FAIStimulus& Stimulus)
	{
		Adapter->HandleHearing(Source, Stimulus);
	}

	static void Noise(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source, const FVector& Location,
		float Strength = 1.0f)
	{
		Adapter->HandleHearing(Source, MakeHearingStimulus(Location, Strength));
	}

    static bool CanRedirect(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source, const FVector& Location)
    {
        return Adapter->CanRedirectHearing(Source, Location, Now(Adapter));
    }
    static void SetRedirects(UDustEnemyCombatAdapterComponent* Adapter, int32 Count)
    {
        Adapter->HearingRedirectCount = Count;
    }
    static void SeedHearing(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source, const FVector& Location)
    {
        Adapter->ClearSearch();
        Adapter->SearchTarget = Source;
        Adapter->bSearching = true;
        Adapter->bHearingSearch = true;
        Adapter->HearingSearchPriority = 1;
        Adapter->LastKnownLocation = Location;
        Adapter->HearingDeadline = Now(Adapter) + Adapter->HearingMaxSeconds;
        Adapter->NextHearingUpdateAt = Now(Adapter) + Adapter->HearingUpdateInterval;
        Adapter->bRetryUsed = true;
    }

    static bool CanRedirectPriority(UDustEnemyCombatAdapterComponent* Adapter, AActor* Source,
        const FVector& Location, int32 Priority)
    {
        return Adapter->CanRedirectHearing(Source, Location, Now(Adapter), Priority);
    }
    static void SetPriority(UDustEnemyCombatAdapterComponent* Adapter, int32 Priority)
    {
        Adapter->HearingSearchPriority = Priority;
    }

	// ---- new hearing state -----------------------------------------------------------------

	static bool Hearing(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bHearingSearch; }
	static float Deadline(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->HearingDeadline; }
	static float NextUpdate(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->NextHearingUpdateAt; }
	static float AllowedAt(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->NextHearingAllowedAt; }
	static int32 Redirects(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->HearingRedirectCount; }
	static float MaxSeconds(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->HearingMaxSeconds; }

	static void SetEnabled(UDustEnemyCombatAdapterComponent* Adapter, bool bEnabled)
	{
		Adapter->bEnableHearingInvestigation = bEnabled;
	}
	static void SetDeadline(UDustEnemyCombatAdapterComponent* Adapter, float Value) { Adapter->HearingDeadline = Value; }
	static void SetAllowedAt(UDustEnemyCombatAdapterComponent* Adapter, float Value) { Adapter->NextHearingAllowedAt = Value; }
	static void SetUpdateDistance(UDustEnemyCombatAdapterComponent* Adapter, float Meters)
	{
		Adapter->HearingUpdateDistanceMeters = Meters;
	}

	// ---- retained search/target state ------------------------------------------------------

	static AActor* SearchSource(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->SearchTarget.Get(); }
	static uint64 Serial(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->Generation; }
	static void SetSerial(UDustEnemyCombatAdapterComponent* Adapter, uint64 Value) { Adapter->Generation = Value; }
	static void SetSearchSource(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor) { Adapter->SearchTarget = Actor; }
	static bool SearchingFlag(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bSearching; }
	static void SetSearchingFlag(UDustEnemyCombatAdapterComponent* Adapter, bool bValue) { Adapter->bSearching = bValue; }
	static AActor* KnownTarget(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->Target.Get(); }
	static void SetKnownTarget(UDustEnemyCombatAdapterComponent* Adapter, AActor* Actor) { Adapter->Target = Actor; }
	static void ResetKnownTarget(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->Target.Reset(); }
	static bool IsConfirmedFlag(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bConfirmedTarget; }
	static bool RetryUsed(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bRetryUsed; }
	static void SetRetryUsed(UDustEnemyCombatAdapterComponent* Adapter, bool bValue) { Adapter->bRetryUsed = bValue; }
	static bool Stopped(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bStopped; }
	static bool Initialized(UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bInitialized; }

	static void ClearSearch(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->ClearSearch(); }
	static void Finish(UDustEnemyCombatAdapterComponent* Adapter, uint64 Serial)
	{
		Adapter->FinishSearch(Serial);
	}
	static void SetMoveId(UDustEnemyCombatAdapterComponent* Adapter, uint32 Id)
	{
		Adapter->SearchMoveId = FAIRequestID(Id);
	}
	static void MoveFinished(UDustEnemyCombatAdapterComponent* Adapter, uint32 Id, bool bSuccess)
	{
		Adapter->HandleMoveFinished(FAIRequestID(Id),
			FPathFollowingResult(bSuccess ? EPathFollowingResult::Success : EPathFollowingResult::Aborted, 0));
	}

	// ---- time and timers -------------------------------------------------------------------

	static float Now(UDustEnemyCombatAdapterComponent* Adapter)
	{
		const UWorld* World = Adapter->GetWorld();
		return World ? World->GetTimeSeconds() : 0.0f;
	}

	/** Moves the throttle window into the past without ticking the world. */
	static void AllowUpdate(UDustEnemyCombatAdapterComponent* Adapter)
	{
		Adapter->NextHearingUpdateAt = Now(Adapter) - 1.0f;
	}

	static bool SearchTimerActive(UDustEnemyCombatAdapterComponent* Adapter)
	{
		UWorld* World = Adapter->GetWorld();
		return World && World->GetTimerManager().IsTimerActive(Adapter->SearchTimer);
	}
};

namespace
{
	struct FHearingTestRig
	{
		UWorld* World = nullptr;
		AAIController* Controller = nullptr;
		APawn* Pawn = nullptr;
		ADust2PlayerCharacter* Player = nullptr;
		AActor* PlainActor = nullptr;
		UStateTreeAIComponent* Tree = nullptr;
		UDustEnemyWeaponComponent* Weapon = nullptr;
		UDustEnemyCombatAdapterComponent* Adapter = nullptr;

		bool Build()
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
			if (!World) return false;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			Controller = World->SpawnActor<AAIController>();
			Pawn = World->SpawnActor<APawn>();
			Player = World->SpawnActor<ADust2PlayerCharacter>();
			PlainActor = World->SpawnActor<AActor>();
			if (!Controller || !Pawn || !Player || !PlainActor) return false;
			Controller->Possess(Pawn);

			Tree = NewObject<UStateTreeAIComponent>(Controller);
			Tree->SetStartLogicAutomatically(false);
			Controller->AddInstanceComponent(Tree);
			Tree->RegisterComponent();

			Weapon = NewObject<UDustEnemyWeaponComponent>(Pawn);
			Pawn->AddInstanceComponent(Weapon);
			Weapon->RegisterComponent();

			Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
			Controller->AddInstanceComponent(Adapter);
			Adapter->RegisterComponent();
			FDustEnemyHearingTestAccess::Prepare(Adapter, Controller, Pawn, Tree, Weapon);
			return true;
		}

		void Destroy()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World = nullptr;
			}
		}
	};
}

// ---------------------------------------------------------------------------------------------
// 1. Stimulus/source gates, and sight knowledge/search must not be replaced by a footstep.
// ---------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyHearingStimulusGateTest,
	"Dust2.AI.Hearing.StimulusGateAndSightPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyHearingStimulusGateTest::RunTest(const FString& Parameters)
{
	FHearingTestRig Rig;
	if (!Rig.Build())
	{
		AddError(TEXT("Could not build the hearing gate rig"));
		Rig.Destroy();
		return false;
	}

	UDustEnemyCombatAdapterComponent* Adapter = Rig.Adapter;
	ADust2PlayerCharacter* Player = Rig.Player;
	const FVector NoiseLocation(300.0f, 0.0f, 0.0f);

	auto ExpectRejected = [this, Adapter](const FAIStimulus& Stimulus, AActor* Source, const TCHAR* What)
	{
		FDustEnemyHearingTestAccess::Hear(Adapter, Source, Stimulus);
		TestFalse(*FString::Printf(TEXT("%s: must not start a hearing investigation"), What),
			FDustEnemyHearingTestAccess::Hearing(Adapter));
		TestNull(*FString::Printf(TEXT("%s: must not claim a search source"), What),
			FDustEnemyHearingTestAccess::SearchSource(Adapter));
		TestFalse(*FString::Printf(TEXT("%s: must not make the adapter search"), What),
			Adapter->IsSearching());
		TestTrue(*FString::Printf(TEXT("%s: must not move the last known location"), What),
			Adapter->GetLastKnownTargetLocation().Equals(FVector::ZeroVector, 0.01f));
	};

	FDustEnemyHearingTestAccess::SetEnabled(Adapter, false);
	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation), Player,
		TEXT("Hearing investigation disabled"));
	FDustEnemyHearingTestAccess::SetEnabled(Adapter, true);

	ExpectRejected(FDustEnemyHearingTestAccess::MakeSightStimulus(NoiseLocation), Player,
		TEXT("Sight stimulus"));
	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation, 1.0f,
		FName(TEXT("UnsupportedNoise"))), Player, TEXT("Unsupported noise tag"));
	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation, 1.0f,
		FName(TEXT("Footstep")), FAIStimulus::SensingFailed), Player, TEXT("Failed sensing"));
	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation, 0.0f), Player,
		TEXT("Zero strength"));
	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation, -1.0f), Player,
		TEXT("Negative strength"));

	FAIStimulus Stale = FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation);
	Stale.SetExpirationAge(1.0f);
	Stale.AgeStimulus(2.0f); // 2s old: past HearingMaxStimulusAge (1.5) and past its own expiry
	ExpectRejected(Stale, Player, TEXT("Stale stimulus"));

	FAIStimulus Expired = FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation);
	Expired.MarkExpired();
	ExpectRejected(Expired, Player, TEXT("Stimulus marked expired"));

	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(FAISystem::InvalidLocation), Player,
		TEXT("Invalid location"));
	const float NaNFloat = std::numeric_limits<float>::quiet_NaN();
    FVector InvalidNoiseLocation = FVector::ZeroVector;
    InvalidNoiseLocation.X = NaNFloat; // Assign after construction: avoid FVector constructor diagnostics sanitizing the probe.
    ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(InvalidNoiseLocation), Player,
        TEXT("Non-finite location"));

	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation), Rig.PlainActor,
		TEXT("Ineligible non-player source"));
	ExpectRejected(FDustEnemyHearingTestAccess::MakeHearingStimulus(NoiseLocation), Rig.Pawn,
		TEXT("Self pawn source"));

	// A legal footstep starts an investigation that never becomes target knowledge.
	const float BeforeStart = FDustEnemyHearingTestAccess::Now(Adapter);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, NoiseLocation);
	TestTrue(TEXT("A successful footstep starts a hearing investigation"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestTrue(TEXT("The noise source becomes the search source"),
		FDustEnemyHearingTestAccess::SearchSource(Adapter) == Player);
	TestTrue(TEXT("The stimulus location is stored as the event location"),
		Adapter->GetLastKnownTargetLocation().Equals(NoiseLocation, 0.01f));
	TestNull(TEXT("Hearing never creates a known target"), Adapter->GetTarget());
	TestNull(TEXT("Hearing never creates a confirmed target"), Adapter->GetConfirmedTarget());
	TestFalse(TEXT("Hearing never confirms a target"),
		FDustEnemyHearingTestAccess::IsConfirmedFlag(Adapter));
	TestTrue(TEXT("The adapter reports an active search"), Adapter->IsSearching());
	TestEqual(TEXT("Redirect count starts at zero"),
		FDustEnemyHearingTestAccess::Redirects(Adapter), 0);
	TestTrue(TEXT("The total budget starts from HearingMaxSeconds"),
		FDustEnemyHearingTestAccess::Deadline(Adapter)
			>= BeforeStart + FDustEnemyHearingTestAccess::MaxSeconds(Adapter) - 0.01f);

	// Existing target knowledge wins over hearing.
	FDustEnemyHearingTestAccess::ClearSearch(Adapter);
	FDustEnemyHearingTestAccess::SetAllowedAt(Adapter, 0.0f);
	FDustEnemyHearingTestAccess::SetKnownTarget(Adapter, Player);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, NoiseLocation);
	TestFalse(TEXT("An existing known target blocks hearing investigation"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestTrue(TEXT("The known target survives the ignored noise"),
		FDustEnemyHearingTestAccess::KnownTarget(Adapter) == Player);
	FDustEnemyHearingTestAccess::ResetKnownTarget(Adapter);

	// An active visual search wins over hearing.
	FDustEnemyHearingTestAccess::ClearSearch(Adapter);
	FDustEnemyHearingTestAccess::SetAllowedAt(Adapter, 0.0f);
	FDustEnemyHearingTestAccess::SetSearchSource(Adapter, Player);
	FDustEnemyHearingTestAccess::SetSearchingFlag(Adapter, true);
	FDustEnemyHearingTestAccess::SetSerial(Adapter, 99);
	TestTrue(TEXT("Visual search state is prepared"),
		FDustEnemyHearingTestAccess::SearchingFlag(Adapter));
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, NoiseLocation);
	TestFalse(TEXT("An active visual search blocks hearing investigation"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestTrue(TEXT("The visual search keeps its source"),
		FDustEnemyHearingTestAccess::SearchSource(Adapter) == Player);
	TestEqual(TEXT("The visual search keeps its generation"),
		FDustEnemyHearingTestAccess::Serial(Adapter), uint64(99));
	FDustEnemyHearingTestAccess::ClearSearch(Adapter);

	Rig.Destroy();
	return true;
}

// ---------------------------------------------------------------------------------------------
// 2. The investigation follows event locations, never the player's live position, and never
//    creates something the weapon may treat as a confirmed target.
// ---------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyHearingEventLocationTest,
	"Dust2.AI.Hearing.EventLocationNotLivePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyHearingEventLocationTest::RunTest(const FString& Parameters)
{
	FHearingTestRig Rig;
	if (!Rig.Build())
	{
		AddError(TEXT("Could not build the hearing location rig"));
		Rig.Destroy();
		return false;
	}

	UDustEnemyCombatAdapterComponent* Adapter = Rig.Adapter;
	ADust2PlayerCharacter* Player = Rig.Player;
	Rig.Player->SetActorLocation(FVector(2000.0f, 0.0f, 0.0f));

	const FVector NoiseA(300.0f, 0.0f, 0.0f);
	FDustEnemyHearingTestAccess::SetAllowedAt(Adapter, 0.0f);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, NoiseA);
	TestTrue(TEXT("The footstep starts an investigation"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestTrue(TEXT("The noise source is the search source"),
		FDustEnemyHearingTestAccess::SearchSource(Adapter) == Player);
	TestTrue(TEXT("The search location is the event location, not the player"),
		Adapter->GetLastKnownTargetLocation().Equals(NoiseA, 0.01f));
	TestTrue(TEXT("The player is far from the event"),
		!Adapter->GetLastKnownTargetLocation().Equals(Player->GetActorLocation(), 1.0f));

	TestNull(TEXT("A heard event is not a target"), Adapter->GetTarget());
	TestNull(TEXT("A heard event is not a confirmed/shootable target"), Adapter->GetConfirmedTarget());
	TestFalse(TEXT("A heard event never confirms the target"), Adapter->HasConfirmedTarget());
	TestFalse(TEXT("The confirmed flag stays false"), FDustEnemyHearingTestAccess::IsConfirmedFlag(Adapter));
	TestTrue(TEXT("The adapter is searching the event"), Adapter->IsSearching());

	// Moving the player must not move the stored event location.
	Rig.Player->SetActorLocation(FVector(-1200.0f, 500.0f, 0.0f));
	TestTrue(TEXT("Player movement never moves the heard event location"),
		Adapter->GetLastKnownTargetLocation().Equals(NoiseA, 0.01f));

    // Actual navigation/redirection is a PIE acceptance item. This no-navigation rig only
    // checks the pure admission decision; it must not pretend a second failed MoveTo succeeded.
    FDustEnemyHearingTestAccess::AllowUpdate(Adapter);
    TestTrue(TEXT("A sufficiently displaced new event is eligible for redirection"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Player, FVector(900.0f, 0.0f, 0.0f)));

	Rig.Destroy();
	return true;
}

// ---------------------------------------------------------------------------------------------
// 3. Repeated footsteps: interval/min-displacement/redirect-count limits, no deadline refresh,
//    and a failed-move retry latch that redirects must not reset.
// ---------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyHearingThrottleTest,
	"Dust2.AI.Hearing.ThrottleRedirectsAndDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyHearingThrottleTest::RunTest(const FString& Parameters)
{
    FHearingTestRig Rig;
    if (!Rig.Build())
    {
        AddError(TEXT("Could not build the hearing throttle rig"));
        Rig.Destroy();
        return false;
    }
    auto* Adapter = Rig.Adapter;
    const FVector Start(100.0f, 0.0f, 0.0f);
    FDustEnemyHearingTestAccess::SeedHearing(Adapter, Rig.Player, Start);
    const float Deadline = FDustEnemyHearingTestAccess::Deadline(Adapter);
    const uint64 Serial = FDustEnemyHearingTestAccess::Serial(Adapter);
    TestFalse(TEXT("An event inside the update interval is throttled"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(400.0f,0.0f,0.0f)));
    FDustEnemyHearingTestAccess::AllowUpdate(Adapter);
    TestFalse(TEXT("A 1.2m displacement is below the default 2m threshold"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(120.0f,0.0f,0.0f)));
    TestTrue(TEXT("A 3.2m displacement is eligible after the interval"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(320.0f,0.0f,0.0f)));
    TestFalse(TEXT("Another source cannot replace the current investigation"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.PlainActor, Start + FVector(320.0f,0.0f,0.0f)));
    FDustEnemyHearingTestAccess::SetRedirects(Adapter, 2);
    TestFalse(TEXT("The default two-redirect budget blocks further updates"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(640.0f,0.0f,0.0f)));
    FDustEnemyHearingTestAccess::SetRedirects(Adapter, 1);
    TestTrue(TEXT("One remaining redirect permits a displaced clue"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(640.0f,0.0f,0.0f)));
    FDustEnemyHearingTestAccess::SetUpdateDistance(Adapter, 5.0f);
    TestFalse(TEXT("A configured 5m threshold rejects 3m"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(300.0f,0.0f,0.0f)));
    TestTrue(TEXT("A configured 5m threshold admits 6m"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(600.0f,0.0f,0.0f)));
    TestEqual(TEXT("Policy queries cannot extend the investigation deadline"),
        FDustEnemyHearingTestAccess::Deadline(Adapter), Deadline);
    TestEqual(TEXT("Policy queries cannot change request ownership"),
        FDustEnemyHearingTestAccess::Serial(Adapter), Serial);
    TestTrue(TEXT("Policy queries preserve the existing failed-move retry latch"),
        FDustEnemyHearingTestAccess::RetryUsed(Adapter));
    FDustEnemyHearingTestAccess::SetDeadline(Adapter, FDustEnemyHearingTestAccess::Now(Adapter));
    TestFalse(TEXT("A reached absolute deadline blocks redirection"),
        FDustEnemyHearingTestAccess::CanRedirect(Adapter, Rig.Player, Start + FVector(600.0f,0.0f,0.0f)));
    Rig.Destroy();
    return true;
}

// ---------------------------------------------------------------------------------------------
// 4. Clear/Stop invalidate the old generation, stale move/finish callbacks stay inert, and
//    cooldown/deadline states do not admit or revive an investigation.
// ---------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyHearingClearAndStopTest,
	"Dust2.AI.Hearing.ClearStopCooldownAndDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyHearingClearAndStopTest::RunTest(const FString& Parameters)
{
	FHearingTestRig Rig;
	if (!Rig.Build())
	{
		AddError(TEXT("Could not build the hearing clear/stop rig"));
		Rig.Destroy();
		return false;
	}

	UDustEnemyCombatAdapterComponent* Adapter = Rig.Adapter;
	ADust2PlayerCharacter* Player = Rig.Player;
	const FVector NoiseA(300.0f, 0.0f, 0.0f);

	FDustEnemyHearingTestAccess::SetAllowedAt(Adapter, 0.0f);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, NoiseA);
	const uint64 FirstSerial = FDustEnemyHearingTestAccess::Serial(Adapter);
	TestTrue(TEXT("First investigation started"), FDustEnemyHearingTestAccess::Hearing(Adapter));
	FDustEnemyHearingTestAccess::SetMoveId(Adapter, 123);

	FDustEnemyHearingTestAccess::ClearSearch(Adapter);
	TestTrue(TEXT("ClearSearch bumps the generation"),
		FDustEnemyHearingTestAccess::Serial(Adapter) != FirstSerial);
	TestFalse(TEXT("ClearSearch clears the hearing flag"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestEqual(TEXT("ClearSearch clears the deadline"),
		FDustEnemyHearingTestAccess::Deadline(Adapter), 0.0f);
	TestEqual(TEXT("ClearSearch clears the next update time"),
		FDustEnemyHearingTestAccess::NextUpdate(Adapter), 0.0f);
	TestEqual(TEXT("ClearSearch clears the redirect count"),
		FDustEnemyHearingTestAccess::Redirects(Adapter), 0);
	TestNull(TEXT("ClearSearch releases the search source"),
		FDustEnemyHearingTestAccess::SearchSource(Adapter));
	TestFalse(TEXT("ClearSearch stops the search timer"),
		FDustEnemyHearingTestAccess::SearchTimerActive(Adapter));
	TestFalse(TEXT("ClearSearch ends the search state"), Adapter->IsSearching());

	// A newer investigation must survive the old move id and the old finish serial.
	FDustEnemyHearingTestAccess::SetAllowedAt(Adapter, 0.0f);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, FVector(700.0f, 0.0f, 0.0f));
	const uint64 NewSerial = FDustEnemyHearingTestAccess::Serial(Adapter);
	TestTrue(TEXT("A newer investigation started"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	FDustEnemyHearingTestAccess::MoveFinished(Adapter, 123, true);
	TestTrue(TEXT("A stale move completion cannot resolve the newer investigation"),
		FDustEnemyHearingTestAccess::Hearing(Adapter)
			&& FDustEnemyHearingTestAccess::Serial(Adapter) == NewSerial);
	FDustEnemyHearingTestAccess::Finish(Adapter, FirstSerial);
	TestTrue(TEXT("A stale FinishSearch cannot clear the newer investigation"),
		FDustEnemyHearingTestAccess::Hearing(Adapter)
			&& FDustEnemyHearingTestAccess::Serial(Adapter) == NewSerial);

	// Cooldown: a fresh event is ignored until NextHearingAllowedAt.
	FDustEnemyHearingTestAccess::ClearSearch(Adapter);
	FDustEnemyHearingTestAccess::SetAllowedAt(
		Adapter, FDustEnemyHearingTestAccess::Now(Adapter) + 2.0f);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, FVector(1100.0f, 0.0f, 0.0f));
	TestFalse(TEXT("Hearing cooldown rejects a new event"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestFalse(TEXT("A rejected cooldown event does not make the adapter search"), Adapter->IsSearching());
	FDustEnemyHearingTestAccess::SetAllowedAt(
		Adapter, FDustEnemyHearingTestAccess::Now(Adapter) - 1.0f);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, FVector(1300.0f, 0.0f, 0.0f));
	TestTrue(TEXT("An expired cooldown admits a new event"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));

	// A deadline already reached must not move the investigation, spend a redirect, or extend time.
	const FVector BeforeDeadline = Adapter->GetLastKnownTargetLocation();
	const int32 RedirectsBeforeDeadline = FDustEnemyHearingTestAccess::Redirects(Adapter);
	FDustEnemyHearingTestAccess::AllowUpdate(Adapter);
	FDustEnemyHearingTestAccess::SetDeadline(
		Adapter, FDustEnemyHearingTestAccess::Now(Adapter) - 0.5f);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, FVector(2500.0f, 0.0f, 0.0f));
	TestTrue(TEXT("A reached deadline cannot move the investigation"),
		Adapter->GetLastKnownTargetLocation().Equals(BeforeDeadline, 0.01f));
	TestEqual(TEXT("A reached deadline cannot consume a redirect"),
		FDustEnemyHearingTestAccess::Redirects(Adapter), RedirectsBeforeDeadline);
	TestTrue(TEXT("A reached deadline is never extended"),
		FDustEnemyHearingTestAccess::Deadline(Adapter) <= FDustEnemyHearingTestAccess::Now(Adapter));

	// Stop is the permanent cleanup: hearing state and cooldown are both reset.
	FDustEnemyHearingTestAccess::SetAllowedAt(
		Adapter, FDustEnemyHearingTestAccess::Now(Adapter) + 5.0f);
	Adapter->StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
	TestTrue(TEXT("Stop latches the stopped state"), FDustEnemyHearingTestAccess::Stopped(Adapter));
	TestFalse(TEXT("Stop clears initialized"), FDustEnemyHearingTestAccess::Initialized(Adapter));
	TestFalse(TEXT("Stop clears the hearing flag"), FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestEqual(TEXT("Stop clears the hearing deadline"),
		FDustEnemyHearingTestAccess::Deadline(Adapter), 0.0f);
	TestEqual(TEXT("Stop clears the hearing next-update time"),
		FDustEnemyHearingTestAccess::NextUpdate(Adapter), 0.0f);
	TestEqual(TEXT("Stop clears the hearing redirect count"),
		FDustEnemyHearingTestAccess::Redirects(Adapter), 0);
	TestEqual(TEXT("Stop clears the hearing cooldown"),
		FDustEnemyHearingTestAccess::AllowedAt(Adapter), 0.0f);
	TestNull(TEXT("Stop releases the search source"),
		FDustEnemyHearingTestAccess::SearchSource(Adapter));
	TestFalse(TEXT("Stop stops the search timer"),
		FDustEnemyHearingTestAccess::SearchTimerActive(Adapter));
	TestFalse(TEXT("Stop leaves no search state"), Adapter->IsSearching());

	const uint64 StoppedSerial = FDustEnemyHearingTestAccess::Serial(Adapter);
	FDustEnemyHearingTestAccess::Noise(Adapter, Player, FVector(1500.0f, 0.0f, 0.0f));
	TestFalse(TEXT("A stopped adapter ignores new footsteps"),
		FDustEnemyHearingTestAccess::Hearing(Adapter));
	TestEqual(TEXT("A stopped adapter does not advance its generation"),
		FDustEnemyHearingTestAccess::Serial(Adapter), StoppedSerial);
	FDustEnemyHearingTestAccess::Finish(Adapter, NewSerial);
	FDustEnemyHearingTestAccess::MoveFinished(Adapter, 123, true);
	TestEqual(TEXT("Late search callbacks are inert after Stop"),
		FDustEnemyHearingTestAccess::Serial(Adapter), StoppedSerial);

	Rig.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyGunshotPriorityTest,
    "Dust2.AI.Hearing.GunshotPriorityWithoutDeadlineExtension",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyGunshotPriorityTest::RunTest(const FString& Parameters)
{
    FHearingTestRig Rig;
    if (!Rig.Build())
    {
        AddError(TEXT("Could not build hearing priority rig"));
        Rig.Destroy();
        return false;
    }
    auto* Adapter = Rig.Adapter;
    const FVector Location(100.0f, 0.0f, 0.0f);
    const FAIStimulus Gunshot = FDustEnemyHearingTestAccess::MakeHearingStimulus(
        Location, 1.0f, FName(TEXT("Gunshot")));
    FDustEnemyHearingTestAccess::Hear(Adapter, Rig.Player, Gunshot);
    TestTrue(TEXT("A valid committed-gunshot stimulus starts investigation"),
        FDustEnemyHearingTestAccess::Hearing(Adapter));
    TestNull(TEXT("A gunshot clue alone never creates a shooting target"), Adapter->GetConfirmedTarget());
    FDustEnemyHearingTestAccess::SeedHearing(Adapter, Rig.Player, Location);
    FDustEnemyHearingTestAccess::SetRedirects(Adapter, 2);
    const float Deadline = FDustEnemyHearingTestAccess::Deadline(Adapter);
    TestTrue(TEXT("Gunshot upgrades a footstep even during throttle and after ordinary redirect budget"),
        FDustEnemyHearingTestAccess::CanRedirectPriority(Adapter, Rig.Player, Location, 2));
    FDustEnemyHearingTestAccess::SetPriority(Adapter, 2);
    FDustEnemyHearingTestAccess::AllowUpdate(Adapter);
    TestFalse(TEXT("Footsteps cannot downgrade a gunshot investigation"),
        FDustEnemyHearingTestAccess::CanRedirectPriority(Adapter, Rig.Player, Location + FVector(500,0,0), 1));
    TestFalse(TEXT("A second gunshot is not another priority upgrade and respects the redirect budget"),
        FDustEnemyHearingTestAccess::CanRedirectPriority(Adapter, Rig.Player, Location + FVector(500,0,0), 2));
    FDustEnemyHearingTestAccess::SetRedirects(Adapter, 1);
    TestTrue(TEXT("Equal priority may update with remaining ordinary budget"),
        FDustEnemyHearingTestAccess::CanRedirectPriority(Adapter, Rig.Player, Location + FVector(500,0,0), 2));
    TestEqual(TEXT("Priority admission never extends the original deadline"),
        FDustEnemyHearingTestAccess::Deadline(Adapter), Deadline);
    FDustEnemyHearingTestAccess::SetDeadline(Adapter, FDustEnemyHearingTestAccess::Now(Adapter));
    TestFalse(TEXT("Priority never revives an expired investigation"),
        FDustEnemyHearingTestAccess::CanRedirectPriority(Adapter, Rig.Player, Location + FVector(500,0,0), 2));
    Rig.Destroy();
    return true;
}

#endif
