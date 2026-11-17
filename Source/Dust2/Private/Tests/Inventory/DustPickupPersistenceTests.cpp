#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Inventory/DustPickupPersistenceComponent.h"
#include "Inventory/DustPickupActorBase.h"
#include "Inventory/DustPickupPersistenceSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	UDustPickupPersistenceSubsystem* NewPickupSession()
	{
		UGameInstance* Instance = NewObject<UGameInstance>();
		return NewObject<UDustPickupPersistenceSubsystem>(Instance);
	}
	constexpr auto PickupTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FPickupWorld
	{
		UWorld* World = nullptr;
		FPickupWorld()
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
		}
		~FPickupWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
		UDustPickupPersistenceComponent* AddPickup()
		{
			AActor* Owner = World->SpawnActor<ADustPickupActorBase>();
			if (!Owner) return nullptr;
			auto* Component = NewObject<UDustPickupPersistenceComponent>(Owner);
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupRecord,
	"Dust2.PickupPersistence.RecordAndDeduplicate", PickupTestFlags)
bool FDustPickupRecord::RunTest(const FString&)
{
	auto* Session = NewPickupSession();
	const FName Map(TEXT("/Game/Code/Map/Dust2"));
	const FName Item(TEXT("Spawn_Ammo_01"));
	TestFalse(TEXT("Uncollected item available"), Session->IsConsumed(Map, Item));
	TestTrue(TEXT("First successful collection recorded"), Session->RecordConsumed(Map, Item));
	TestTrue(TEXT("Same ID after reload remains consumed"), Session->IsConsumed(Map, Item));
	TestFalse(TEXT("Duplicate success does not add another record"), Session->RecordConsumed(Map, Item));
	TestFalse(TEXT("Other pickup unaffected"), Session->IsConsumed(Map, TEXT("Spawn_Ammo_02")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupMapIsolation,
	"Dust2.PickupPersistence.MapAndSessionIsolation", PickupTestFlags)
bool FDustPickupMapIsolation::RunTest(const FString&)
{
	auto* Session = NewPickupSession();
	const FName Item(TEXT("Spawn_Gun_01"));
	Session->RecordConsumed(TEXT("/Game/Map_A"), Item);
	TestFalse(TEXT("Same ID in another map is independent"), Session->IsConsumed(TEXT("/Game/Map_B"), Item));
	TestFalse(TEXT("Another game instance starts empty"), NewPickupSession()->IsConsumed(TEXT("/Game/Map_A"), Item));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupEmptyIds,
	"Dust2.PickupPersistence.EmptyIdsDoNotPersist", PickupTestFlags)
bool FDustPickupEmptyIds::RunTest(const FString&)
{
	auto* Session = NewPickupSession();
	TestFalse(TEXT("Unkeyed runtime drop not recorded"), Session->RecordConsumed(TEXT("/Game/Map_A"), NAME_None));
	TestFalse(TEXT("Missing map not recorded"), Session->RecordConsumed(NAME_None, TEXT("Item")));
	TestFalse(TEXT("Missing map not consumed"), Session->IsConsumed(NAME_None, TEXT("Item")));
	TestFalse(TEXT("Unkeyed drop remains untracked"), Session->IsConsumed(TEXT("/Game/Map_A"), NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupCheckpointRollback,
	"Dust2.PickupPersistence.CheckpointRollback", PickupTestFlags)
bool FDustPickupCheckpointRollback::RunTest(const FString&)
{
	auto* Session = NewPickupSession();
	const FName Map(TEXT("/Game/Map_A"));
	const FName OtherMap(TEXT("/Game/Map_B"));
	const FName Gun(TEXT("Gun"));
	const FName Ammo(TEXT("Ammo"));

	Session->RecordConsumed(OtherMap, Gun);
	const TArray<FName> EmptySave = Session->GetConsumedPickupIds(Map);
	Session->RecordConsumed(Map, Gun);
	Session->RestoreConsumedPickupIds(Map, EmptySave);
	TestFalse(TEXT("Gun picked up after empty-handed checkpoint returns"), Session->IsConsumed(Map, Gun));
	TestTrue(TEXT("Other map's consumed gun is unchanged"), Session->IsConsumed(OtherMap, Gun));

	Session->RecordConsumed(Map, Gun);
	const TArray<FName> EquippedSave = Session->GetConsumedPickupIds(Map);
	Session->RecordConsumed(Map, Ammo);
	Session->RestoreConsumedPickupIds(Map, EquippedSave);
	TestTrue(TEXT("Gun saved in player's slots remains consumed"), Session->IsConsumed(Map, Gun));
	TestFalse(TEXT("Ammo picked up after checkpoint returns"), Session->IsConsumed(Map, Ammo));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupFreshRun,
	"Dust2.PickupPersistence.FreshRunReset", PickupTestFlags)
bool FDustPickupFreshRun::RunTest(const FString&)
{
	auto* Session = NewPickupSession();
	Session->RecordConsumed(TEXT("/Game/Map_A"), TEXT("Ammo"));
	Session->RecordConsumed(TEXT("/Game/Map_B"), TEXT("Health"));
	Session->NativeResetConsumedPickups();
	TestFalse(TEXT("Fresh run restores map A item"), Session->IsConsumed(TEXT("/Game/Map_A"), TEXT("Ammo")));
	TestFalse(TEXT("Fresh run restores map B item"), Session->IsConsumed(TEXT("/Game/Map_B"), TEXT("Health")));
	TestTrue(TEXT("New run may collect again"), Session->RecordConsumed(TEXT("/Game/Map_A"), TEXT("Ammo")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupPieIdentity,
	"Dust2.PickupPersistence.PIEPackageIdentity", PickupTestFlags)
bool FDustPickupPieIdentity::RunTest(const FString&)
{
	auto* Session = NewPickupSession();
	const FName A(*UWorld::RemovePIEPrefix(TEXT("/Game/Code/Map/UEDPIE_0_Dust2")));
	const FName B(*UWorld::RemovePIEPrefix(TEXT("/Game/Code/Map/UEDPIE_1_Dust2")));
	Session->RecordConsumed(A, TEXT("Spawn_Gun"));
	TestEqual(TEXT("PIE identity normalized"), A, FName(TEXT("/Game/Code/Map/Dust2")));
	TestTrue(TEXT("Same map package identity after reload"), Session->IsConsumed(B, TEXT("Spawn_Gun")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupUntrackedAndRejected,
	"Dust2.PickupPersistence.UntrackedAvailabilityWithoutGrant", PickupTestFlags)
bool FDustPickupUntrackedAndRejected::RunTest(const FString&)
{
	FPickupWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* Pickup = F.AddPickup();
	if (!TestNotNull(TEXT("Pickup"), Pickup)) return false;
	TestTrue(TEXT("Unkeyed drop is available"), Pickup->NativeIsPickupAvailable());
	TestTrue(TEXT("Availability checks do not consume"), Pickup->NativeIsPickupAvailable());
	TestFalse(TEXT("Unkeyed success is not a persistent record"), Pickup->RecordSuccessfulPickup());
	TestFalse(TEXT("Successful local consumption blocks duplicate collection"), Pickup->NativeIsPickupAvailable());
	TestFalse(TEXT("Repeated consumption ignored"), Pickup->RecordSuccessfulPickup());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupConsumeOwner,
	"Dust2.PickupPersistence.SuccessRemovesOwner", PickupTestFlags)
bool FDustPickupConsumeOwner::RunTest(const FString&)
{
	FPickupWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* Pickup = F.AddPickup();
	if (!TestNotNull(TEXT("Pickup"), Pickup)) return false;
	AActor* Owner = Pickup->GetOwner();
	CastChecked<ADustPickupActorBase>(Owner)->NativeCommitSuccessfulPickup();
	TestTrue(TEXT("Accepted pickup destroys owner"), Owner->IsActorBeingDestroyed());
	TestFalse(TEXT("Removed pickup unavailable"), Pickup->NativeIsPickupAvailable());
	return true;
}

#endif
