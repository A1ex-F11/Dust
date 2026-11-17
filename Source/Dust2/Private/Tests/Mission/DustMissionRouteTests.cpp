#if WITH_DEV_AUTOMATION_TESTS

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Identity/DustStableIdComponent.h"
#include "Inventory/DustPickupPersistenceSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustEncounterComponent.h"
#include "Mission/DustGameSessionSubsystem.h"
#include "Mission/DustMissionRoute.h"
#include "Player/Dust2PlayerCharacter.h"

struct FDustMissionRouteTestAccess
{
	static void Prime(ADustMissionRoute& Route, ADust2GameMode* Mode, ADust2PlayerCharacter* Player)
	{
		Route.Mode = Mode;
		Route.Player = Player;
		Route.CurrentIndex = 0;
		Route.BindCurrentStep();
	}

	static void Enter(ADustMissionRoute& Route, UDustEncounterComponent* Region)
	{
		ADust2PlayerCharacter* Player = Route.Player.Get();
		Route.HandleEntered(Region->GetRouteEntryBox(), Player, Player->GetCapsuleComponent(), 0, false, FHitResult{});
	}

	static bool Validate(const ADustMissionRoute& Route, int32 FirstActiveStep = 0, bool bResumeOnlyC4 = false)
	{
		return Route.ValidateSteps(FirstActiveStep, bResumeOnlyC4);
	}
	static FName MapId(const ADustMissionRoute& Route) { return Route.GetMapId(); }
	static int32 Resume(const ADustMissionRoute& Route, const FDustRouteCheckpoint& Checkpoint)
	{
		return Route.ResolveResumeIndex(Checkpoint);
	}
};

namespace
{
	constexpr auto RouteTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FMissionRouteWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		ADustMissionRoute* Route = World ? World->SpawnActor<ADustMissionRoute>() : nullptr;
		ADust2GameMode* Mode = World ? World->SpawnActor<ADust2GameMode>() : nullptr;
		ADust2PlayerCharacter* Player = World ? World->SpawnActor<ADust2PlayerCharacter>() : nullptr;

		~FMissionRouteWorld()
		{
			if (World) World->DestroyWorld(false);
		}

		UDustEncounterComponent* MakeRegion(AActor*& Enemy) const
		{
			AActor* Region = World->SpawnActor<AActor>();
			Enemy = World->SpawnActor<AActor>();
			if (!Region || !Enemy) return nullptr;
			auto* Encounter = NewObject<UDustEncounterComponent>(Region);
			auto* Box = NewObject<UBoxComponent>(Region);
			Region->AddInstanceComponent(Encounter);
			Region->AddInstanceComponent(Box);
			Encounter->RegisterComponent();
			Box->RegisterComponent();
			Encounter->RouteEntryBox.OverrideComponent = Box;
			if (!Encounter->NativeInitializeEncounter({Enemy})) return nullptr;
			return Encounter;
		}

		void AddStep(FName StepId, UDustEncounterComponent* Region, bool bEnter, bool bClear) const
		{
			FDustMissionRouteStep& Step = Route->Steps.AddDefaulted_GetRef();
			Step.StepId = StepId;
			Step.Region = Region->GetOwner();
			Step.bRequireArrival = bEnter;
			Step.bRequireClear = bClear;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionRouteResumeNormalStartup,
	"Dust2.MissionRoute.ResumeKeepsNormalStartupWithoutPendingCheckpoint", RouteTestFlags)
bool FDustMissionRouteResumeNormalStartup::RunTest(const FString&)
{
	FMissionRouteWorld F;
	if (!TestNotNull(TEXT("Player"), F.Player)) return false;
	TestFalse(TEXT("Normal startup keeps its menu branch"), F.Player->TryQueueNativeMissionCheckpointResume());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionRouteOrder, "Dust2.MissionRoute.ArrivalClearAndFreshEntry", RouteTestFlags)
bool FDustMissionRouteOrder::RunTest(const FString&)
{
	FMissionRouteWorld F;
	if (!TestTrue(TEXT("World actors"), F.Route && F.Mode && F.Player)) return false;
	AActor* EnemyA = nullptr;
	AActor* EnemyB = nullptr;
	AActor* EnemyC = nullptr;
	auto* A = F.MakeRegion(EnemyA);
	auto* B = F.MakeRegion(EnemyB);
	auto* C = F.MakeRegion(EnemyC);
	if (!TestTrue(TEXT("Initialized regions"), A && B && C)) return false;
	const FName StepA(TEXT("A"));
	const FName StepB(TEXT("B"));
	const FName ClearC(TEXT("ClearC"));
	const FName EnterC(TEXT("EnterC"));
	F.AddStep(StepA, A, true, true);
	F.AddStep(StepB, B, true, true);
	F.AddStep(ClearC, C, false, true);
	F.AddStep(EnterC, C, false, false);
	FDustMissionRouteTestAccess::Prime(*F.Route, F.Mode, F.Player);

	FDustMissionRouteTestAccess::Enter(*F.Route, A);
	TestEqual(TEXT("Arrival alone waits for clear"), F.Route->GetCurrentRouteStepId(), StepA);
	A->NativeNotifyEnemyDied(EnemyA);
	TestEqual(TEXT("Clear advances a step with prior arrival"), F.Route->GetCurrentRouteStepId(), StepB);
	B->NativeNotifyEnemyDied(EnemyB);
	TestEqual(TEXT("Clear alone waits for arrival"), F.Route->GetCurrentRouteStepId(), StepB);
	FDustMissionRouteTestAccess::Enter(*F.Route, B);
	TestEqual(TEXT("Arrival advances a previously cleared region"), F.Route->GetCurrentRouteStepId(), ClearC);
	FDustMissionRouteTestAccess::Enter(*F.Route, C);
	C->NativeNotifyEnemyDied(EnemyC);
	TestEqual(TEXT("An entry before clear does not satisfy the next step"), F.Route->GetCurrentRouteStepId(), EnterC);
	FDustMissionRouteTestAccess::Enter(*F.Route, C);
	TestTrue(TEXT("A fresh entry completes the route"), F.Route->GetCurrentRouteStepId().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionRouteRetry, "Dust2.MissionRoute.StableResumeAndFailedAction", RouteTestFlags)
bool FDustMissionRouteRetry::RunTest(const FString&)
{
	FMissionRouteWorld F;
	if (!TestTrue(TEXT("World actors"), F.Route && F.Mode && F.Player)) return false;
	AActor* Enemy = nullptr;
	AActor* OtherEnemy = nullptr;
	auto* A = F.MakeRegion(Enemy);
	auto* B = F.MakeRegion(OtherEnemy);
	if (!TestTrue(TEXT("Initialized regions"), A && B)) return false;
	const FName Open(TEXT("Open"));
	const FName Clear(TEXT("Clear"));
	F.AddStep(Open, A, true, false);
	F.AddStep(Clear, B, false, true);
	F.Route->Steps[0].bActivateNextRegion = true;
	F.Route->Steps[0].NextRegion = B->GetOwner();
	FDustRouteCheckpoint Checkpoint;
	TestEqual(TEXT("Legacy checkpoint begins at the first step"), FDustMissionRouteTestAccess::Resume(*F.Route, Checkpoint), 0);
	Checkpoint.MapId = FDustMissionRouteTestAccess::MapId(*F.Route);
	Checkpoint.ResumeStepId = Clear;
	TestEqual(TEXT("Saved step is found by its ID"), FDustMissionRouteTestAccess::Resume(*F.Route, Checkpoint), 1);
	F.Route->Steps.Swap(0, 1);
	TestEqual(TEXT("ID still works after reordering"), FDustMissionRouteTestAccess::Resume(*F.Route, Checkpoint), 0);
	F.Route->Steps.Swap(0, 1);
	Checkpoint.MapId = FName(TEXT("/Game/Other"));
	TestEqual(TEXT("Different map cannot use this checkpoint"), FDustMissionRouteTestAccess::Resume(*F.Route, Checkpoint), INDEX_NONE);

	AddExpectedMessage(TEXT("could not finish step"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	FDustMissionRouteTestAccess::Prime(*F.Route, F.Mode, F.Player);
	FDustMissionRouteTestAccess::Enter(*F.Route, A);
	F.Route->RetryCurrentRouteStep();
	TestEqual(TEXT("Rejected activation does not advance even on retry"), F.Route->GetCurrentRouteStepId(), Open);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustNativeRouteCheckpointTest,
	"Dust2.MissionRoute.NativeCheckpointSession", RouteTestFlags)
bool FDustNativeRouteCheckpointTest::RunTest(const FString&)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	auto* Session = NewObject<UDustGameSessionSubsystem>(Instance);
	const FName MapId(TEXT("/Game/RouteTest"));
	const FName StepId(TEXT("AfterSave"));
	const FName GunId(TEXT("Gun"));
	FDustNativeRouteCheckpoint Checkpoint;
	Checkpoint.Route.MapId = MapId;
	Checkpoint.Route.ResumeStepId = StepId;
	Checkpoint.Route.ConsumedPickupIds.Add(GunId);
	Checkpoint.Health = 75.0f;
	Checkpoint.Slot1.CurrentAmmo = 12;
	Checkpoint.Slot1.ReserveAmmo = 30;
	Checkpoint.Slot1.bInitialDrawConsumed = true;
	Session->SaveRouteCheckpoint(MoveTemp(Checkpoint));

	TestNull(TEXT("Saving alone does not arm a retry"), Session->GetPendingRouteCheckpoint(MapId));
	Session->SetRouteRetryPending(true);
	TestNull(TEXT("A different map cannot read the pending snapshot"),
		Session->GetPendingRouteCheckpoint(FName(TEXT("/Game/Other"))));
	const FDustNativeRouteCheckpoint* Pending = Session->GetPendingRouteCheckpoint(MapId);
	if (!TestNotNull(TEXT("The same map has a pending snapshot"), Pending)) return false;
	TestEqual(TEXT("Stored resume ID"), Pending->Route.ResumeStepId, StepId);
	TestEqual(TEXT("Stored health"), Pending->Health, 75.0f);
	TestEqual(TEXT("Stored magazine"), Pending->Slot1.CurrentAmmo, 12);
	TestTrue(TEXT("Stored first draw"), Pending->Slot1.bInitialDrawConsumed);
	TestTrue(TEXT("Stored pickup consumption"), Pending->Route.ConsumedPickupIds.Contains(GunId));
	Session->SetRouteRetryPending(false);
	TestNotNull(TEXT("Completing recovery retains the checkpoint"), Session->GetRouteCheckpoint(MapId));

	auto* Pickups = NewObject<UDustPickupPersistenceSubsystem>(Instance);
	const FName NewGunId(TEXT("NewGun"));
	Pickups->RestoreConsumedPickupIds(MapId, Pending->Route.ConsumedPickupIds);
	Pickups->RecordConsumed(MapId, NewGunId);
	Session->SetRouteRetryPending(true);
	Pending = Session->GetPendingRouteCheckpoint(MapId);
	if (!TestNotNull(TEXT("The second retry still reads the original snapshot"), Pending)) return false;
	Pickups->RestoreConsumedPickupIds(MapId, Pending->Route.ConsumedPickupIds);
	TestFalse(TEXT("A gun collected after the first retry returns to its pickup point"),
		Pickups->IsConsumed(MapId, NewGunId));
	TestTrue(TEXT("The gun collected before the checkpoint stays consumed"), Pickups->IsConsumed(MapId, GunId));
	TestEqual(TEXT("The second retry restores the original loadout data"), Pending->Slot1.CurrentAmmo, 12);

	Session->ResetRouteCheckpoint();
	TestNull(TEXT("New run clears the checkpoint"), Session->GetRouteCheckpoint(MapId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustRouteSelectedActions,
	"Dust2.MissionRoute.ClearSaveAndMultipleDoors", RouteTestFlags)
bool FDustRouteSelectedActions::RunTest(const FString&)
{
	FMissionRouteWorld F;
	if (!TestTrue(TEXT("World actors"), F.Route && F.Player)) return false;
	AActor* Enemy = nullptr;
	UDustEncounterComponent* Region = F.MakeRegion(Enemy);
	if (!TestNotNull(TEXT("Region"), Region)) return false;
	AActor* RegionActor = Region->GetOwner();
	auto* Arrow = NewObject<UArrowComponent>(RegionActor);
	RegionActor->AddInstanceComponent(Arrow);
	Arrow->RegisterComponent();
	Region->RouteRespawnArrow.OverrideComponent = Arrow;

	F.AddStep(TEXT("ClearSave"), Region, false, true);
	FDustMissionRouteStep& Step = F.Route->Steps[0];
	Step.bSaveCheckpoint = true;
	Step.Door = F.World->SpawnActor<AActor>();
	Step.DoorId = TEXT("Door_01");
	Step.MigrateLegacyActions();
	Step.MigrateLegacyActions();
	if (!TestEqual(TEXT("Existing door migrates only once"), Step.Doors.Num(), 1)) return false;
	TestTrue(TEXT("Existing door action is selected"), Step.bOpenDoors);
	TestEqual(TEXT("Existing door ID is retained"), Step.Doors[0].DoorId, FName(TEXT("Door_01")));

	FDustMissionRouteStep Legacy;
	Legacy.ActivateRegion = RegionActor;
	Legacy.C4 = F.World->SpawnActor<AActor>();
	AActor* LegacyC4 = Legacy.C4.Get();
	Legacy.MigrateLegacyActions();
	TestTrue(TEXT("Existing next region migrates"), Legacy.bActivateNextRegion && Legacy.NextRegion == RegionActor);
	TestTrue(TEXT("Existing C4 migrates"), Legacy.bStartC4 && Legacy.C4Actor == LegacyC4);

	FDustRouteDoor& SecondDoor = Step.Doors.AddDefaulted_GetRef();
	SecondDoor.Door = F.World->SpawnActor<AActor>();
	SecondDoor.DoorId = TEXT("Door_02");
	TestFalse(TEXT("Clear and save does not require a Box"), Step.RequiresArrival());
	TestTrue(TEXT("Clear, save and two doors are valid together"), FDustMissionRouteTestAccess::Validate(*F.Route));
	SecondDoor.DoorId = TEXT("Door_01");
	AddExpectedMessage(TEXT("duplicate Door Id"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("Duplicate door ID is rejected"), FDustMissionRouteTestAccess::Validate(*F.Route));
	Step.bOpenDoors = false;
	TestTrue(TEXT("Unselected doors do not affect the step"), FDustMissionRouteTestAccess::Validate(*F.Route));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustRouteRetrySkipsCompletedActions,
	"Dust2.MissionRoute.RetrySkipsCompletedActionValidation", RouteTestFlags)
bool FDustRouteRetrySkipsCompletedActions::RunTest(const FString&)
{
	FMissionRouteWorld F;
	if (!TestNotNull(TEXT("Route"), F.Route)) return false;
	AActor* Enemy = nullptr;
	UDustEncounterComponent* Region = F.MakeRegion(Enemy);
	if (!TestNotNull(TEXT("Region"), Region)) return false;

	F.AddStep(TEXT("LeaveSpawn"), Region, true, false);
	F.AddStep(TEXT("Clear_01"), Region, false, true);
	FDustMissionRouteStep& LeaveSpawn = F.Route->Steps[0];
	LeaveSpawn.bOpenDoors = true;
	LeaveSpawn.Doors.AddDefaulted_GetRef().DoorId = TEXT("Door_01");

	AddExpectedMessage(TEXT("each door needs a door actor"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("A fresh route still requires its first door"), FDustMissionRouteTestAccess::Validate(*F.Route));
	TestTrue(TEXT("Retry does not require the completed step's door"),
		FDustMissionRouteTestAccess::Validate(*F.Route, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustRouteRetryC4Validation,
	"Dust2.MissionRoute.RetryC4SkipsAlreadyAppliedDoor", RouteTestFlags)
bool FDustRouteRetryC4Validation::RunTest(const FString&)
{
	FMissionRouteWorld F;
	if (!TestNotNull(TEXT("Route"), F.Route)) return false;
	AActor* Enemy = nullptr;
	UDustEncounterComponent* Region = F.MakeRegion(Enemy);
	if (!TestNotNull(TEXT("Region"), Region)) return false;

	F.AddStep(TEXT("Clear_05"), Region, false, true);
	FDustMissionRouteStep& Step = F.Route->Steps[0];
	Step.bStartC4 = true;
	Step.C4Actor = F.World->SpawnActor<AActor>();
	if (!TestTrue(TEXT("C4 actor available"), IsValid(Step.C4Actor))) return false;
	Step.bOpenDoors = true;
	Step.Doors.AddDefaulted_GetRef().DoorId = TEXT("Door_07");

	AddExpectedMessage(TEXT("each door needs a door actor"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("Normal entry still validates the door"), FDustMissionRouteTestAccess::Validate(*F.Route));
	TestTrue(TEXT("C4 retry accepts a previously opened door that has disappeared"),
		FDustMissionRouteTestAccess::Validate(*F.Route, 0, true));

	Step.C4Actor = nullptr;
	AddExpectedMessage(TEXT("select a C4 actor"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("C4 retry still needs its target"),
		FDustMissionRouteTestAccess::Validate(*F.Route, 0, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustRoutePreplacedRecovery,
	"Dust2.MissionRoute.RestorePreplacedWithoutClearEvent", RouteTestFlags)
bool FDustRoutePreplacedRecovery::RunTest(const FString&)
{
	FMissionRouteWorld F;
	if (!TestTrue(TEXT("World actors"), F.Route && F.Mode && F.Player)) return false;
	AActor* Enemy = nullptr;
	UDustEncounterComponent* Region = F.MakeRegion(Enemy);
	if (!TestTrue(TEXT("Preplaced enemy registered"), Region && Enemy)) return false;
	const FName StepId(TEXT("Clear"));
	const FName EnemyId(TEXT("Preplaced_01"));
	auto* StableId = NewObject<UDustStableIdComponent>(Enemy);
	Enemy->AddInstanceComponent(StableId);
	StableId->RegisterComponent();
	StableId->NativeSetStableId(EnemyId);
	F.AddStep(StepId, Region, false, true);
	FDustMissionRouteTestAccess::Prime(*F.Route, F.Mode, F.Player);

	TestTrue(TEXT("Preplaced checkpoint cleanup succeeds"),
		Region->RestoreRouteCheckpointEnemies({EnemyId}, true));
	TestTrue(TEXT("Saved dead actor no longer counts against the region"), Region->NativeIsEncounterCleared());
	TestEqual(TEXT("Silent cleanup does not advance the route"), F.Route->GetCurrentRouteStepId(), StepId);
	return true;
}

#endif
