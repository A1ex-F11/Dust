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
#include "TimerManager.h"

// Tests exercise the real cancellation/qualification methods without starting a
// substitute tree. Actual ST_EnemyCombat and navigation remain PIE acceptance.
struct FDustEnemyCombatAdapterTestAccess
{
	static void Prepare(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
		APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon)
	{
		Adapter->Controller = Controller;
		Adapter->BoundPawn = Pawn;
		Adapter->Tree = Tree;
		Adapter->WeaponComponent = Weapon;
		Adapter->bInitialized = true;
	}
	static bool Eligible(UDustEnemyCombatAdapterComponent* A, AActor* Actor) { return A->IsEligibleTarget(Actor); }
	static void Search(UDustEnemyCombatAdapterComponent* A, AActor* Player, uint64 Serial, uint32 Move)
	{
		A->Target.Reset(); A->SearchTarget = Player; A->bSearching = true;
		A->Generation = Serial; A->SearchMoveId = FAIRequestID(Move);
	}
	static uint64 Serial(UDustEnemyCombatAdapterComponent* A) { return A->Generation; }
	static void NewCombat(UDustEnemyCombatAdapterComponent* A, AActor* Player)
	{
		A->ClearSearch(); A->Target = Player;
	}
	static void Finish(UDustEnemyCombatAdapterComponent* A, uint64 Serial) { A->FinishSearch(Serial); }
	static void Move(UDustEnemyCombatAdapterComponent* A, uint32 Id)
	{
		A->HandleMoveFinished(FAIRequestID(Id), FPathFollowingResult(EPathFollowingResult::Success, 0));
	}
	static FTimerHandle& Timer(UDustEnemyCombatAdapterComponent* A) { return A->SearchTimer; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyLifecycleCancellationTest,
	"Dust2.AI.Lifecycle.SearchCancellationAndStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyLifecycleCancellationTest::RunTest(const FString& Parameters)
{
	UWorld::InitializationValues Values;
	Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(true).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AAIController* Controller = World->SpawnActor<AAIController>();
	APawn* Pawn = World->SpawnActor<APawn>();
	ADust2PlayerCharacter* Player = World->SpawnActor<ADust2PlayerCharacter>();
	AActor* Other = World->SpawnActor<AActor>();
	if (!Controller || !Pawn || !Player || !Other)
	{
		AddError(TEXT("Could not spawn test actors"));
		World->DestroyWorld(false);
		if (GEngine) GEngine->DestroyWorldContext(World);
		return false;
	}
	Controller->Possess(Pawn);
	UStateTreeAIComponent* Tree = NewObject<UStateTreeAIComponent>(Controller);
	Tree->SetStartLogicAutomatically(false);
	Controller->AddInstanceComponent(Tree); Tree->RegisterComponent();
	UDustEnemyWeaponComponent* Weapon = NewObject<UDustEnemyWeaponComponent>(Pawn);
	Pawn->AddInstanceComponent(Weapon); Weapon->RegisterComponent();
	UDustEnemyCombatAdapterComponent* Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
	Controller->AddInstanceComponent(Adapter); Adapter->RegisterComponent();
	FDustEnemyCombatAdapterTestAccess::Prepare(Adapter, Controller, Pawn, Tree, Weapon);
	TestTrue(TEXT("Native player qualifies"), FDustEnemyCombatAdapterTestAccess::Eligible(Adapter, Player));
	TestFalse(TEXT("Unrelated live Actor must not qualify"), FDustEnemyCombatAdapterTestAccess::Eligible(Adapter, Other));
	TestFalse(TEXT("Self Pawn must not qualify"), FDustEnemyCombatAdapterTestAccess::Eligible(Adapter, Pawn));

	FDustEnemyCombatAdapterTestAccess::Search(Adapter, Player, 42, 123);
	FDustEnemyCombatAdapterTestAccess::Finish(Adapter, 41);
	TestTrue(TEXT("Old timer cannot finish newer search"), Adapter->IsSearching());
	TestEqual(TEXT("Old timer cannot invalidate generation"), FDustEnemyCombatAdapterTestAccess::Serial(Adapter), uint64(42));
	FDustEnemyCombatAdapterTestAccess::Move(Adapter, 122);
	TestFalse(TEXT("Old move completion cannot start wait timer"), World->GetTimerManager().IsTimerActive(FDustEnemyCombatAdapterTestAccess::Timer(Adapter)));
	FDustEnemyCombatAdapterTestAccess::NewCombat(Adapter, Player);
	FDustEnemyCombatAdapterTestAccess::Finish(Adapter, 42);
	FDustEnemyCombatAdapterTestAccess::Move(Adapter, 123);
	TestTrue(TEXT("Late search callbacks preserve reacquired player"), Adapter->GetTarget() == Player);
	TestFalse(TEXT("Late search callbacks cannot restart search timers"), World->GetTimerManager().IsTimerActive(FDustEnemyCombatAdapterTestAccess::Timer(Adapter)));
	FDustEnemyCombatAdapterTestAccess::Search(Adapter, Player, 44, 125);
	FDustEnemyCombatAdapterTestAccess::Move(Adapter, 125);
	TestTrue(TEXT("Matching move completion starts wait timer"), World->GetTimerManager().IsTimerActive(FDustEnemyCombatAdapterTestAccess::Timer(Adapter)));
	Adapter->StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
	const uint64 StoppedSerial = FDustEnemyCombatAdapterTestAccess::Serial(Adapter);
	TestFalse(TEXT("Stop cancels the wait timer"), World->GetTimerManager().IsTimerActive(FDustEnemyCombatAdapterTestAccess::Timer(Adapter)));
	TestFalse(TEXT("Stop leaves no active life"), Adapter->IsInitialized());
	TestNull(TEXT("Stopped life exposes no target"), Adapter->GetTarget());
	FDustEnemyCombatAdapterTestAccess::Move(Adapter, 125);
	FDustEnemyCombatAdapterTestAccess::Finish(Adapter, 44);
	Adapter->StopCombatAI(EDustEnemyAIStopReason::MissionEnd);
	TestEqual(TEXT("Repeated Stop and late callbacks are inert"), FDustEnemyCombatAdapterTestAccess::Serial(Adapter), StoppedSerial);
	TestTrue(TEXT("Repeated Stop preserves first reason"), Adapter->LastStopReason == EDustEnemyAIStopReason::EnemyDeath);
	World->DestroyWorld(false);
	if (GEngine) GEngine->DestroyWorldContext(World);
	return true;
}
#endif
