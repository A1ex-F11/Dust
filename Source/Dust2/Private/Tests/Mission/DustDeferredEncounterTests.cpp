#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AI/DustEnemyPatrolComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Identity/DustStableIdComponent.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustEncounterComponent.h"
#include "Mission/DustEncounterPersistenceSubsystem.h"

struct FDustEncounterTestAccess
{
	static void SetPersistence(UDustEncounterComponent* Component,
		UDustEncounterPersistenceSubsystem* Persistence)
	{
		if (Component) Component->PersistenceOverride = Persistence;
	}
};

namespace
{
	constexpr auto DeferredEncounterTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UDustEncounterPersistenceSubsystem* NewEncounterPersistence()
	{
		UGameInstance* Instance = NewObject<UGameInstance>();
		return NewObject<UDustEncounterPersistenceSubsystem>(Instance);
	}

	struct FDeferredEncounterWorld
	{
		UWorld* World = nullptr;
		ADust2GameMode* Mode = nullptr;

		FDeferredEncounterWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DustDeferredEncounterTestWorld"),
				nullptr, false, ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Mode = World->SpawnActor<ADust2GameMode>();
		}

		~FDeferredEncounterWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}

		UDustEncounterComponent* AddEncounter()
		{
			if (!World) return nullptr;
			AActor* Owner = World->SpawnActor<AActor>();
			if (!Owner) return nullptr;
			auto* Component = NewObject<UDustEncounterComponent>(Owner);
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}

		AActor* AddPoint(const FVector& Location = FVector::ZeroVector)
		{
			if (!World) return nullptr;
			AActor* Point = World->SpawnActor<AActor>();
			if (Point) Point->SetActorLocation(Location);
			return Point;
		}

		int32 CountPawns() const
		{
			int32 Count = 0;
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				if (IsValid(*It)) ++Count;
			}
			return Count;
		}

		APawn* FindFirstPawn() const
		{
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				if (IsValid(*It)) return *It;
			}
			return nullptr;
		}

		FName LevelId() const
		{
			return World
				? FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()))
				: NAME_None;
		}
	};

	FDustDeferredEnemySpawnEntry MakeEnemyEntry(
		FName EntryId, AActor* SpawnPoint, TSubclassOf<APawn> PawnClass = APawn::StaticClass())
	{
		FDustDeferredEnemySpawnEntry Entry;
		Entry.EntryId = EntryId;
		Entry.PawnClass = PawnClass;
		Entry.SpawnPoint = SpawnPoint;
		return Entry;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeferredDuplicateActivation,
	"Dust2.DeferredEncounter.ActivationIsOneShot", DeferredEncounterTestFlags)
bool FDustDeferredDuplicateActivation::RunTest(const FString&)
{
	FDeferredEncounterWorld F;
	if (!TestNotNull(TEXT("World"), F.World) || !TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	UDustEncounterPersistenceSubsystem* Persistence = NewEncounterPersistence();
	UDustEncounterComponent* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Persistence"), Persistence) || !TestNotNull(TEXT("Encounter"), Encounter)) return false;

	FDustEncounterTestAccess::SetPersistence(Encounter, Persistence);
	Encounter->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	Encounter->bEnableDeferredRegion = true;
	Encounter->DeferredRegion.RegionId = TEXT("Region_B");
	Encounter->DeferredRegion.EnemySpawns.Add(
		MakeEnemyEntry(TEXT("Enemy_B_01"), F.AddPoint(FVector(100.0f, 0.0f, 0.0f))));

	TestTrue(TEXT("First activation succeeds"), Encounter->NativeActivateDeferredRegion());
	TestFalse(TEXT("Second activation is rejected"), Encounter->NativeActivateDeferredRegion());
	TestEqual(TEXT("Only one spawned pawn"), F.CountPawns(), 1);
	TestEqual(TEXT("Tracker has one member"), Encounter->NativeGetRemainingEnemyCount(), 1);
	TestTrue(TEXT("Region recorded"), Persistence->IsRegionActivated(F.LevelId(), TEXT("Region_B")));
	TestFalse(TEXT("No early clear broadcast"), F.Mode->NativeHasClearedB());

	APawn* Pawn = F.FindFirstPawn();
	if (!TestNotNull(TEXT("Spawned pawn"), Pawn)) return false;
	TestTrue(TEXT("Spawned pawn has stable id"), UDustStableIdComponent::ResolveStableIdForActor(Pawn) ==
		FName(TEXT("Enemy_B_01")));
	TestTrue(TEXT("Last death broadcasts once"), Encounter->NativeNotifyEnemyDied(Pawn));
	TestTrue(TEXT("Clear event reached GameMode"), F.Mode->NativeHasClearedB());
	TestFalse(TEXT("Repeated death is ignored"), Encounter->NativeNotifyEnemyDied(Pawn));
	TestTrue(TEXT("Tracker stays cleared"), Encounter->NativeIsEncounterCleared());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeferredEmptyRegion,
	"Dust2.DeferredEncounter.EmptyRegionDoesNotBroadcast", DeferredEncounterTestFlags)
bool FDustDeferredEmptyRegion::RunTest(const FString&)
{
	FDeferredEncounterWorld F;
	if (!TestNotNull(TEXT("World"), F.World) || !TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	UDustEncounterPersistenceSubsystem* Persistence = NewEncounterPersistence();
	UDustEncounterComponent* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Persistence"), Persistence) || !TestNotNull(TEXT("Encounter"), Encounter)) return false;

	FDustEncounterTestAccess::SetPersistence(Encounter, Persistence);
	Encounter->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	Encounter->bEnableDeferredRegion = true;
	Encounter->DeferredRegion.RegionId = TEXT("Region_Empty");

	TestTrue(TEXT("Empty activation commits"), Encounter->NativeActivateDeferredRegion());
	TestFalse(TEXT("Second empty activation is rejected"), Encounter->NativeActivateDeferredRegion());
	TestTrue(TEXT("Empty tracker reports cleared fact"), Encounter->NativeIsEncounterCleared());
	TestFalse(TEXT("Empty tracker never broadcasts"), F.Mode->NativeHasClearedB());
	TestEqual(TEXT("No pawns spawned"), F.CountPawns(), 0);

	UDustEncounterComponent* Restored = F.AddEncounter();
	if (!TestNotNull(TEXT("Restored encounter"), Restored)) return false;
	FDustEncounterTestAccess::SetPersistence(Restored, Persistence);
	Restored->bEnableDeferredRegion = true;
	Restored->DeferredRegion.RegionId = TEXT("Region_EmptyRestore");
	Restored->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	Persistence->RecordRegionActivated(F.LevelId(), TEXT("Region_EmptyRestore"));

	TestTrue(TEXT("Empty active region restores"), Restored->NativeRestoreDeferredRegion());
	TestTrue(TEXT("Restored empty tracker reports cleared"), Restored->NativeIsEncounterCleared());
	TestFalse(TEXT("Restored empty region never broadcasts"), F.Mode->NativeHasClearedB());
	TestEqual(TEXT("Restored empty region spawns no pawn"), F.CountPawns(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeferredRestoreSelective,
	"Dust2.DeferredEncounter.RestoreSkipsDeadIdsAndInactiveRegions", DeferredEncounterTestFlags)
bool FDustDeferredRestoreSelective::RunTest(const FString&)
{
	FDeferredEncounterWorld F;
	if (!TestNotNull(TEXT("World"), F.World) || !TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	UDustEncounterPersistenceSubsystem* Persistence = NewEncounterPersistence();
	UDustEncounterComponent* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Persistence"), Persistence) || !TestNotNull(TEXT("Encounter"), Encounter)) return false;

	FDustEncounterTestAccess::SetPersistence(Encounter, Persistence);
	Encounter->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	Encounter->bEnableDeferredRegion = true;
	Encounter->DeferredRegion.RegionId = TEXT("Region_Retry");
	Encounter->DeferredRegion.EnemySpawns.Add(
		MakeEnemyEntry(TEXT("Enemy_Dead"), F.AddPoint(FVector(100.0f, 0.0f, 0.0f))));
	Encounter->DeferredRegion.EnemySpawns.Add(
		MakeEnemyEntry(TEXT("Enemy_Live"), F.AddPoint(FVector(200.0f, 0.0f, 0.0f))));

	Persistence->RecordRegionActivated(F.LevelId(), TEXT("Region_Retry"));
	F.Mode->NativeRegisterEnemyDeath(TEXT("Enemy_Dead"));

	TestTrue(TEXT("Restore succeeds for active region"), Encounter->NativeRestoreDeferredRegion());
	TestEqual(TEXT("Only live entry spawns"), F.CountPawns(), 1);
	TestEqual(TEXT("Only live member tracked"), Encounter->NativeGetRemainingEnemyCount(), 1);
	TestFalse(TEXT("Restore does not broadcast"), F.Mode->NativeHasClearedB());

	APawn* Pawn = F.FindFirstPawn();
	if (!TestNotNull(TEXT("Live pawn"), Pawn)) return false;
	TestEqual(TEXT("Live entry keeps its identity"), UDustStableIdComponent::ResolveStableIdForActor(Pawn),
		FName(TEXT("Enemy_Live")));
	TestFalse(TEXT("A second restore is rejected"), Encounter->NativeRestoreDeferredRegion());
	TestEqual(TEXT("Second restore does not duplicate"), F.CountPawns(), 1);

	UDustEncounterComponent* NeverActive = F.AddEncounter();
	FDustEncounterTestAccess::SetPersistence(NeverActive, Persistence);
	NeverActive->bEnableDeferredRegion = true;
	NeverActive->DeferredRegion.RegionId = TEXT("Region_NeverActive");
	NeverActive->DeferredRegion.EnemySpawns.Add(
		MakeEnemyEntry(TEXT("Enemy_NeverActive"), F.AddPoint(FVector(300.0f, 0.0f, 0.0f))));
	TestFalse(TEXT("Inactive region does not spawn"), NeverActive->NativeRestoreDeferredRegion());
	TestEqual(TEXT("Inactive restore adds no pawn"), F.CountPawns(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeferredEntryIdRepair,
	"Dust2.DeferredEncounter.EntryIdsAreStableAndDistinct", DeferredEncounterTestFlags)
bool FDustDeferredEntryIdRepair::RunTest(const FString&)
{
	FDeferredEncounterWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	UDustEncounterPersistenceSubsystem* Persistence = NewEncounterPersistence();
	UDustEncounterComponent* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Persistence"), Persistence) || !TestNotNull(TEXT("Encounter"), Encounter)) return false;

	FDustEncounterTestAccess::SetPersistence(Encounter, Persistence);
	Encounter->bEnableDeferredRegion = true;
	Encounter->DeferredRegion.RegionId = NAME_None;
	FDustDeferredEnemySpawnEntry DuplicateA;
	DuplicateA.EntryId = TEXT("Duplicate");
	FDustDeferredEnemySpawnEntry DuplicateB;
	DuplicateB.EntryId = TEXT("Duplicate");
	Encounter->DeferredRegion.EnemySpawns.Add(DuplicateA);
	Encounter->DeferredRegion.EnemySpawns.Add(DuplicateB);
	FDustDeferredEnemySpawnEntry Missing;
	Encounter->DeferredRegion.EnemySpawns.Add(Missing);

	TestTrue(TEXT("Explicit repair succeeds"), Encounter->NativeEnsureSpawnEntryIds());
	TestTrue(TEXT("Region id is present"), !Encounter->DeferredRegion.RegionId.IsNone());

	TArray<FName> RepairedIds;
	for (const FDustDeferredEnemySpawnEntry& Entry : Encounter->DeferredRegion.EnemySpawns)
	{
		TestFalse(TEXT("Entry id was filled"), Entry.EntryId.IsNone());
		TestFalse(TEXT("Entry id is unique"), RepairedIds.Contains(Entry.EntryId));
		RepairedIds.Add(Entry.EntryId);
	}

	const FName RepairedRegionId = Encounter->DeferredRegion.RegionId;
	TestTrue(TEXT("Second repair succeeds"), Encounter->NativeEnsureSpawnEntryIds());
	for (int32 Index = 0; Index < RepairedIds.Num(); ++Index)
	{
		TestEqual(TEXT("Existing entry id is stable"), Encounter->DeferredRegion.EnemySpawns[Index].EntryId,
			RepairedIds[Index]);
	}
	TestEqual(TEXT("Existing region id is stable"), Encounter->DeferredRegion.RegionId, RepairedRegionId);
	TestEqual(TEXT("Region getter matches config"), Encounter->NativeGetDeferredRegionId(), RepairedRegionId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeferredLegacyPreplacedStillWorks,
	"Dust2.DeferredEncounter.LegacyPreplacedCompatibility", DeferredEncounterTestFlags)
bool FDustDeferredLegacyPreplacedStillWorks::RunTest(const FString&)
{
	FDeferredEncounterWorld F;
	if (!TestNotNull(TEXT("World"), F.World) || !TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	UDustEncounterPersistenceSubsystem* Persistence = NewEncounterPersistence();
	UDustEncounterComponent* Encounter = F.AddEncounter();
	if (!TestNotNull(TEXT("Persistence"), Persistence) || !TestNotNull(TEXT("Encounter"), Encounter)) return false;

	FDustEncounterTestAccess::SetPersistence(Encounter, Persistence);
	Encounter->OnNativeEncounterCleared.AddUniqueDynamic(F.Mode, &ADust2GameMode::NativeNotifyBCleared);
	AActor* A = F.AddPoint();
	AActor* B = F.AddPoint();
	if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B)) return false;

	TestFalse(TEXT("Deferred activation is off by default"), Encounter->NativeActivateDeferredRegion());
	TestTrue(TEXT("Legacy one-time initialization still works"), Encounter->NativeInitializeEncounter({A, B}));
	TestEqual(TEXT("Legacy membership preserved"), Encounter->NativeGetRemainingEnemyCount(), 2);
	TestTrue(TEXT("Legacy first death"), Encounter->NativeNotifyEnemyDied(A));
	TestFalse(TEXT("No early clear"), F.Mode->NativeHasClearedB());
	TestTrue(TEXT("Legacy last death broadcasts"), Encounter->NativeNotifyEnemyDied(B));
	TestTrue(TEXT("Legacy clear event reached GameMode"), F.Mode->NativeHasClearedB());
	TestFalse(TEXT("Legacy tracker cannot reinitialize"), Encounter->NativeInitializeEncounter({A}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustSciFiObjectiveProgress,
	"Dust2.DeferredEncounter.SciFiObjectiveProgress", DeferredEncounterTestFlags)
bool FDustSciFiObjectiveProgress::RunTest(const FString&)
{
	UDustEncounterPersistenceSubsystem* Persistence = NewEncounterPersistence();
	const FText Area3 = FText::FromString(TEXT("Area 3"));
	TestTrue(TEXT("Forward progress commits"), Persistence->AdvanceSciFiObjective(3, Area3));
	TestFalse(TEXT("Replaying an earlier area does not roll progress back"),
		Persistence->AdvanceSciFiObjective(2, FText::GetEmpty()));
	TestEqual(TEXT("Progress retained"), Persistence->GetSciFiObjectiveStep(), 3);
	TestTrue(TEXT("Text retained"), Persistence->GetSciFiObjectiveText().EqualTo(Area3));
	TestFalse(TEXT("Out-of-range step rejected"), Persistence->AdvanceSciFiObjective(7, FText::GetEmpty()));
	Persistence->NativeResetActivatedRegions();
	TestEqual(TEXT("New run clears progress"), Persistence->GetSciFiObjectiveStep(), 0);
	TestTrue(TEXT("New run clears text"), Persistence->GetSciFiObjectiveText().IsEmpty());
	return true;
}

#endif
