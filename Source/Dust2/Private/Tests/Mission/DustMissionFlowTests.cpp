#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustEncounterComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	struct FMissionWorld
	{
		UWorld* World = nullptr;
		ADust2GameMode* Mode = nullptr;

		FMissionWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Mode = World->SpawnActor<ADust2GameMode>();
		}
		~FMissionWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
		UDustEncounterComponent* AddEncounter()
		{
			AActor* Owner = World->SpawnActor<AActor>();
			if (!Owner) return nullptr;
			auto* Component = NewObject<UDustEncounterComponent>(Owner);
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}
		void GoToB()
		{
			Mode->TrySetMissionState(EMissionState::Resupply);
			Mode->TrySetMissionState(EMissionState::GoToB);
		}
	};
	constexpr auto MissionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionTransitions,
	"Dust2.Mission.TransitionsAndResultLock", MissionTestFlags)
bool FDustMissionTransitions::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	TestFalse(TEXT("Cannot skip initial supply"), F.Mode->TrySetMissionState(EMissionState::GoToB));
	TestTrue(TEXT("Supply accepted"), F.Mode->TrySetMissionState(EMissionState::Resupply));
	TestFalse(TEXT("Duplicate stage rejected"), F.Mode->TrySetMissionState(EMissionState::Resupply));
	TestFalse(TEXT("Invalid result rejected"), F.Mode->TryResolveMissionResult(static_cast<EMissionResult>(255)));
	TestTrue(TEXT("First result accepted"), F.Mode->TryResolveMissionResult(EMissionResult::PlayerDied));
	TestFalse(TEXT("Second result rejected"), F.Mode->TryResolveMissionResult(EMissionResult::Defused));
	TestFalse(TEXT("Result locks legal next stage"), F.Mode->TrySetMissionState(EMissionState::GoToB));
	F.Mode->NativeNotifyBReached();
	F.Mode->NativeNotifyBCleared();
	TestFalse(TEXT("Result locks new reached fact"), F.Mode->NativeHasReachedB());
	TestFalse(TEXT("Result locks new cleared fact"), F.Mode->NativeHasClearedB());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionBFacts,
	"Dust2.Mission.BFactsEitherOrderAndEarlyReports", MissionTestFlags)
bool FDustMissionBFacts::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	F.GoToB();
	F.Mode->NativeNotifyBReached();
	TestTrue(TEXT("Arrival alone waits"), F.Mode->GetMissionState() == EMissionState::GoToB);
	F.Mode->NativeNotifyBCleared();
	TestTrue(TEXT("Arrival then clear advances"), F.Mode->GetMissionState() == EMissionState::ReturnToA);
	F.Mode->NativeNotifyBCleared();
	F.Mode->NativeNotifyBReached();
	TestFalse(TEXT("Duplicate completion does not reapply"), F.Mode->NativeCheckBComplete());
	F.Mode->NativeResetMissionProgress();
	F.GoToB();
	F.Mode->NativeNotifyBCleared();
	TestTrue(TEXT("Clear alone waits"), F.Mode->GetMissionState() == EMissionState::GoToB);
	F.Mode->NativeNotifyBReached();
	TestTrue(TEXT("Clear then arrival advances"), F.Mode->GetMissionState() == EMissionState::ReturnToA);
	F.Mode->NativeResetMissionProgress();
	F.Mode->TrySetMissionState(EMissionState::Resupply);
	F.Mode->NativeNotifyBReached();
	F.Mode->NativeNotifyBCleared();
	TestTrue(TEXT("Early facts do not skip supply"), F.Mode->GetMissionState() == EMissionState::Resupply);
	TestTrue(TEXT("Entering B accepted"), F.Mode->TrySetMissionState(EMissionState::GoToB));
	TestTrue(TEXT("Entering B rechecks existing facts"), F.Mode->GetMissionState() == EMissionState::ReturnToA);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionPlant,
	"Dust2.Mission.PlantGuardsAndOneShot", MissionTestFlags)
bool FDustMissionPlant::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	AActor* Bomb = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("C4 stand-in"), Bomb)) return false;
	TestFalse(TEXT("Wrong stage rejected"), F.Mode->NativeTryPlantC4(Bomb, true));
	F.Mode->RestoreMissionState(EMissionState::ReturnToA);
	TestFalse(TEXT("Checkpoint required"), F.Mode->NativeTryPlantC4(Bomb, false));
	TestFalse(TEXT("Null C4 rejected"), F.Mode->NativeTryPlantC4(nullptr, true));
	AActor* Destroyed = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Destroyed C4 stand-in"), Destroyed)) return false;
	Destroyed->Destroy();
	TestFalse(TEXT("Destroyed C4 rejected"), F.Mode->NativeTryPlantC4(Destroyed, true));
	FMissionWorld Other;
	if (!TestNotNull(TEXT("Other world"), Other.World)) return false;
	AActor* ForeignBomb = Other.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Foreign C4"), ForeignBomb)) return false;
	TestFalse(TEXT("Foreign world C4 rejected"), F.Mode->NativeTryPlantC4(ForeignBomb, true));
	TestTrue(TEXT("Rejected requests leave phase intact"), F.Mode->GetMissionState() == EMissionState::ReturnToA);
	TestTrue(TEXT("Valid request accepted"), F.Mode->NativeTryPlantC4(Bomb, true));
	TestTrue(TEXT("Stage committed"), F.Mode->GetMissionState() == EMissionState::C4Planted);
	TestFalse(TEXT("Repeated request rejected"), F.Mode->NativeTryPlantC4(Bomb, true));
	F.Mode->RestoreMissionState(EMissionState::ReturnToA);
	F.Mode->TryResolveMissionResult(EMissionResult::PlayerDied);
	TestFalse(TEXT("Cannot plant after result"), F.Mode->NativeTryPlantC4(Bomb, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionRestore,
	"Dust2.Mission.RestoreAndFreshReset", MissionTestFlags)
bool FDustMissionRestore::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	F.Mode->NativeRegisterEnemyDeath(TEXT("Enemy_A"));
	F.Mode->TryResolveMissionResult(EMissionResult::PlayerDied);
	F.Mode->RestoreMissionState(EMissionState::ReturnToA);
	TestTrue(TEXT("Later checkpoint reconstructs B facts"), F.Mode->NativeHasReachedB() && F.Mode->NativeHasClearedB());
	TestTrue(TEXT("Restore unlocks result"), F.Mode->TryResolveMissionResult(EMissionResult::Defused));
	F.Mode->RestoreMissionState(static_cast<EMissionState>(255));
	TestTrue(TEXT("Invalid restore leaves phase"), F.Mode->GetMissionState() == EMissionState::ReturnToA);
	TestFalse(TEXT("Invalid restore leaves result lock"), F.Mode->TryResolveMissionResult(EMissionResult::Exploded));
	F.Mode->RestoreMissionState(EMissionState::Resupply);
	TestFalse(TEXT("Early checkpoint clears arrival"), F.Mode->NativeHasReachedB());
	TestFalse(TEXT("Early checkpoint clears clear fact"), F.Mode->NativeHasClearedB());
	TestEqual(TEXT("Restore stage does not discard ID data"), F.Mode->NativeGetDeadEnemyIds().Num(), 1);
	F.Mode->NativeResetMissionProgress();
	TestTrue(TEXT("Fresh stage"), F.Mode->GetMissionState() == EMissionState::NotStarted);
	TestEqual(TEXT("Fresh reset discards old IDs"), F.Mode->NativeGetDeadEnemyIds().Num(), 0);
	TestTrue(TEXT("Fresh result unlocked"), F.Mode->TryResolveMissionResult(EMissionResult::PlayerDied));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionIds,
	"Dust2.Mission.DeadIdsReplaceDeduplicateAndIgnoreNone", MissionTestFlags)
bool FDustMissionIds::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	F.Mode->NativeRegisterEnemyDeath(NAME_None);
	F.Mode->NativeRegisterEnemyDeath(TEXT("Enemy_A"));
	F.Mode->NativeRegisterEnemyDeath(TEXT("Enemy_A"));
	TestEqual(TEXT("Unique nonempty registration"), F.Mode->NativeGetDeadEnemyIds().Num(), 1);
	F.Mode->NativeRestoreDeadEnemyIds({FName(TEXT("Enemy_B")), NAME_None, FName(TEXT("Enemy_B"))});
	const TArray<FName> Saved = F.Mode->NativeGetDeadEnemyIds();
	TestEqual(TEXT("Restore deduplicates"), Saved.Num(), 1);
	TestTrue(TEXT("Restore replaces previous checkpoint IDs"), Saved.Contains(TEXT("Enemy_B")) && !Saved.Contains(TEXT("Enemy_A")));
	F.Mode->NativeRestoreDeadEnemyIds({});
	TestEqual(TEXT("Empty snapshot clears IDs"), F.Mode->NativeGetDeadEnemyIds().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEncounterDeaths,
	"Dust2.Mission.EncounterMembershipAndSingleClear", MissionTestFlags)
bool FDustEncounterDeaths::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	auto* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Encounter"), Encounter)) return false;
	AActor* A = F.World->SpawnActor<AActor>();
	AActor* B = F.World->SpawnActor<AActor>();
	AActor* Unknown = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B) || !TestNotNull(TEXT("Unknown"), Unknown)) return false;
	Encounter->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	TestTrue(TEXT("Initialize"), Encounter->NativeInitializeEncounter({A, A, nullptr, B, Encounter->GetOwner()}));
	TestEqual(TEXT("Membership deduplicated and owner excluded"), Encounter->NativeGetRemainingEnemyCount(), 2);
	TestFalse(TEXT("Unknown death ignored"), Encounter->NativeNotifyEnemyDied(Unknown));
	TestTrue(TEXT("First registered death"), Encounter->NativeNotifyEnemyDied(A));
	TestFalse(TEXT("Duplicate death ignored"), Encounter->NativeNotifyEnemyDied(A));
	TestFalse(TEXT("Not cleared early"), F.Mode->NativeHasClearedB());
	B->Destroy();
	TestTrue(TEXT("Registered identity can be removed during destruction"), Encounter->NativeNotifyEnemyDied(B));
	TestTrue(TEXT("Last normal death broadcasts"), F.Mode->NativeHasClearedB());
	TestTrue(TEXT("Cleared fact latched"), Encounter->NativeIsEncounterCleared());
	F.Mode->NativeResetMissionProgress();
	TestFalse(TEXT("Cannot reset live tracker by reinitializing"), Encounter->NativeInitializeEncounter({Unknown}));
	TestFalse(TEXT("Repeated last death ignored"), Encounter->NativeNotifyEnemyDied(B));
	TestFalse(TEXT("No second broadcast"), F.Mode->NativeHasClearedB());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEncounterSilent,
	"Dust2.Mission.EncounterSilentRestore", MissionTestFlags)
bool FDustEncounterSilent::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	auto* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Encounter"), Encounter)) return false;
	AActor* A = F.World->SpawnActor<AActor>();
	AActor* B = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B)) return false;
	Encounter->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	TestFalse(TEXT("Before initialization, BP still owns config pruning"), Encounter->NativeRemoveEnemySilently(A));
	TestTrue(TEXT("Initialize current config"), Encounter->NativeInitializeEncounter({A, B}));
	TestTrue(TEXT("Normal first death"), Encounter->NativeNotifyEnemyDied(A));
	TestTrue(TEXT("Last restored enemy removed silently"), Encounter->NativeRemoveEnemySilently(B));
	TestTrue(TEXT("Empty fact available without event"), Encounter->NativeIsEncounterCleared());
	TestFalse(TEXT("Silent removal did not broadcast"), F.Mode->NativeHasClearedB());
	TestFalse(TEXT("Late death of silent member rejected"), Encounter->NativeNotifyEnemyDied(B));
	TestFalse(TEXT("No delayed clear broadcast"), F.Mode->NativeHasClearedB());
	Encounter->GetOwner()->Destroy();
	TestEqual(TEXT("EndPlay clears membership"), Encounter->NativeGetRemainingEnemyCount(), 0);
	TestFalse(TEXT("EndPlay rejects initialization"), Encounter->NativeInitializeEncounter({A}));
	TestFalse(TEXT("EndPlay did not broadcast"), F.Mode->NativeHasClearedB());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEncounterInitialEmpty,
	"Dust2.Mission.EncounterInitialEmptyAndForeignWorld", MissionTestFlags)
bool FDustEncounterInitialEmpty::RunTest(const FString&)
{
	FMissionWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	auto* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Encounter"), Encounter)) return false;
	FMissionWorld Other;
	if (!TestNotNull(TEXT("Other world"), Other.World)) return false;
	AActor* Foreign = Other.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Foreign actor"), Foreign)) return false;
	Encounter->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	TestTrue(TEXT("Initialize excludes unusable members"), Encounter->NativeInitializeEncounter({nullptr, Foreign}));
	TestEqual(TEXT("No foreign membership"), Encounter->NativeGetRemainingEnemyCount(), 0);
	TestTrue(TEXT("Empty fact available"), Encounter->NativeIsEncounterCleared());
	TestFalse(TEXT("Initial empty does not broadcast"), F.Mode->NativeHasClearedB());
	TestFalse(TEXT("Null death ignored"), Encounter->NativeNotifyEnemyDied(nullptr));
	TestFalse(TEXT("Foreign death ignored"), Encounter->NativeNotifyEnemyDied(Foreign));
	return true;
}

#endif
