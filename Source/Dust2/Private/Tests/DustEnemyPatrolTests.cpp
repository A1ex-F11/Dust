#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DustEnemyPatrolComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Navigation/PathFollowingComponent.h"

struct FDustEnemyPatrolTestAccess
{
	static int32 Next(UDustEnemyPatrolComponent* C) { return C->FindNextValidPoint(); }
	static void Pending(UDustEnemyPatrolComponent* C, uint64 Token, int32 Point, uint32 Request)
	{
		C->ActiveToken = Token; C->Generation = Token; C->ActivePointIndex = Point;
		C->MoveId = FAIRequestID(Request); C->Status = EDustPatrolMoveStatus::Running;
	}
	static uint64 Token(UDustEnemyPatrolComponent* C) { return C->ActiveToken; }
	static EDustPatrolMoveStatus Status(UDustEnemyPatrolComponent* C) { return C->Status; }
	static void Finish(UDustEnemyPatrolComponent* C, uint32 Request, EPathFollowingResult::Type Result)
	{
		C->HandleMoveFinished(FAIRequestID(Request), FPathFollowingResult(Result, 0));
	}
};

// Route filtering and stale request ownership use the real component methods.
// Navigation, StateTree asset transitions, and real movement remain PIE checks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyPatrolOwnershipTest,
	"Dust2.AI.Patrol.RouteAndRequestOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyPatrolOwnershipTest::RunTest(const FString& Parameters)
{
	UWorld::InitializationValues Values;
	Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AActor* Owner = World->SpawnActor<AActor>();
	AActor* First = World->SpawnActor<AActor>();
	AActor* Second = World->SpawnActor<AActor>();
	if (!Owner || !First || !Second)
	{
		AddError(TEXT("Could not spawn patrol test actors"));
		World->DestroyWorld(false);
		if (GEngine) GEngine->DestroyWorldContext(World);
		return false;
	}
	UDustEnemyPatrolComponent* Patrol = NewObject<UDustEnemyPatrolComponent>(Owner);
	Owner->AddInstanceComponent(Patrol); Patrol->RegisterComponent();
	TestFalse(TEXT("An empty route cannot patrol"), Patrol->HasPatrolRoute());
	Patrol->PatrolPoints = {nullptr, First, Second};
	TestEqual(TEXT("Null route entries are skipped"), FDustEnemyPatrolTestAccess::Next(Patrol), 1);
	Patrol->bPatrolEnabled = false;
	TestFalse(TEXT("Disabled routes cannot patrol"), Patrol->HasPatrolRoute());
	Patrol->bPatrolEnabled = true;
	TestEqual(TEXT("No initialized AI cannot start a move"), Patrol->BeginPatrolMove(), uint64(0));

	FDustEnemyPatrolTestAccess::Pending(Patrol, 11, 1, 101);
	FDustEnemyPatrolTestAccess::Finish(Patrol, 100, EPathFollowingResult::Success);
	TestTrue(TEXT("Old completion cannot resolve a new move"), FDustEnemyPatrolTestAccess::Status(Patrol) == EDustPatrolMoveStatus::Running);
	Patrol->CancelPatrolMove(10);
	TestEqual(TEXT("Old task exit cannot cancel new token"), FDustEnemyPatrolTestAccess::Token(Patrol), uint64(11));
	FDustEnemyPatrolTestAccess::Finish(Patrol, 101, EPathFollowingResult::Success);
	Patrol->CompletePatrolMove(11);
	TestEqual(TEXT("Completed step advances to the next point"), FDustEnemyPatrolTestAccess::Next(Patrol), 2);

	FDustEnemyPatrolTestAccess::Pending(Patrol, 21, 2, 201);
	Patrol->CancelPatrolMove(21);
	FDustEnemyPatrolTestAccess::Finish(Patrol, 201, EPathFollowingResult::Success);
	TestEqual(TEXT("Canceled completion cannot advance route"), FDustEnemyPatrolTestAccess::Next(Patrol), 2);
	FDustEnemyPatrolTestAccess::Pending(Patrol, 31, 2, 301);
	FDustEnemyPatrolTestAccess::Finish(Patrol, 301, EPathFollowingResult::Blocked);
	Patrol->CompletePatrolMove(31);
	TestEqual(TEXT("Failed step skips forward and wraps past null"), FDustEnemyPatrolTestAccess::Next(Patrol), 1);
	First->Destroy(); Second->Destroy();
	TestFalse(TEXT("Destroyed route points are unavailable"), Patrol->HasPatrolRoute());
	World->DestroyWorld(false);
	if (GEngine) GEngine->DestroyWorldContext(World);
	return true;
}
#endif
