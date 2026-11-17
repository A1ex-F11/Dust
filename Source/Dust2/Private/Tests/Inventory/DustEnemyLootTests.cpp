#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CameraRecoilRecoveryComponent.h"
#include "Presentation/WeaponPresentationComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Player/Dust2PlayerCharacter.h"
#include "Inventory/DustEnemyLootComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Inventory/WeaponInventoryComponent.h"
#include "Inventory/WeaponPickupActor.h"

struct FDustEnemyLootTestAccess
{
	static void Drop(UDustEnemyLootComponent* Component, UWeaponDefinitionData* Definition)
	{ Component->DropOnce(Definition); }
	static bool Attempted(UDustEnemyLootComponent* Component) { return Component->bDropAttempted; }
	static bool HasMapping(UDustEnemyLootComponent* Component, UWeaponDefinitionData* Definition)
	{
		TSubclassOf<AWeaponPickupActor> PickupClass;
		TSubclassOf<AWeaponRuntime> WeaponClass;
		return Component->ResolveWeaponDrop(Definition, PickupClass, WeaponClass);
	}
	static bool Resolve(UDustEnemyLootComponent* Component, UWeaponDefinitionData* Definition,
		TSubclassOf<AWeaponPickupActor>& PickupClass, TSubclassOf<AWeaponRuntime>& WeaponClass)
	{ return Component->ResolveWeaponDrop(Definition, PickupClass, WeaponClass); }
	static bool Roll(float Chance) { return UDustEnemyLootComponent::RollChance(Chance); }
};

namespace
{
	constexpr auto LootTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	// Restore the native test class defaults; never load or change project Blueprint assets.
	struct FScopedPickupDefaults
	{
		AWeaponPickupActor* PickupDefaults = GetMutableDefault<AWeaponPickupActor>();
		TSubclassOf<AWeaponRuntime> PreviousClass = PickupDefaults->GetPickupWeaponClass();
		FWeaponPickupState PreviousState = PickupDefaults->GetPickupSavedState();
		FScopedPickupDefaults()
		{ PickupDefaults->InitializeDroppedWeapon(AWeaponRuntime::StaticClass(), FWeaponPickupState(), nullptr, 0.0f); }
		~FScopedPickupDefaults()
		{ PickupDefaults->InitializeDroppedWeapon(PreviousClass, PreviousState, nullptr, 0.0f); }
	};
	struct FLootWorld
	{
		UWorld* World = nullptr;
		FLootWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
		~FLootWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
		int32 ActorCount() const
		{
			int32 Count = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
				if (IsValid(*It) && !It->IsActorBeingDestroyed()) ++Count;
			return Count;
		}
		AWeaponPickupActor* FirstWeaponPickup() const
		{
			for (TActorIterator<AWeaponPickupActor> It(World); It; ++It)
				if (IsValid(*It) && !It->IsActorBeingDestroyed()) return *It;
			return nullptr;
		}
		UDustEnemyLootComponent* AddLoot()
		{
			AActor* Owner = World->SpawnActor<AActor>();
			if (!Owner) return nullptr;
			auto* Loot = NewObject<UDustEnemyLootComponent>(Owner);
			Owner->AddInstanceComponent(Loot);
			Loot->RegisterComponent();
			return Loot;
		}
	};

	struct FLootInventoryWorld : FLootWorld
	{
		ADust2PlayerCharacter* Player = nullptr;
		UWeaponInventoryComponent* Inventory = nullptr;
		UChildActorComponent* Slot1 = nullptr;
		UChildActorComponent* Slot2 = nullptr;
		bool bReady = false;
		FLootInventoryWorld(bool bFirst, bool bSecond, int32 CurrentSlot = 0)
		{
			if (!World) return;
			Player = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Player) return;
			Player->RestoreHealth(100.0f);
			Inventory = NewObject<UWeaponInventoryComponent>(Player);
			Inventory->RegisterComponent();
			auto* Recoil = NewObject<UCameraRecoilRecoveryComponent>(Player);
			Recoil->RegisterComponent();
			Slot1 = NewObject<UChildActorComponent>(Player);
			Slot2 = NewObject<UChildActorComponent>(Player);
			if (bFirst) Slot1->SetChildActorClass(AWeaponRuntime::StaticClass());
			if (bSecond) Slot2->SetChildActorClass(AWeaponRuntime::StaticClass());
			Slot1->RegisterComponent();
			Slot2->RegisterComponent();
			AWeaponRuntime* Initial = CurrentSlot == 0 ? Cast<AWeaponRuntime>(Slot1->GetChildActor()) :
				(CurrentSlot == 1 ? Cast<AWeaponRuntime>(Slot2->GetChildActor()) : nullptr);
			bReady = Player->InitializePlayerInventoryBridge(Inventory, Slot1, Slot2, Initial, Recoil);
		}
		AWeaponPickupActor* Pickup(int32 Ammo, bool bEnemyReward = true)
		{
			auto* Result = World->SpawnActor<AWeaponPickupActor>();
			if (!Result) return nullptr;
			auto* Root = NewObject<USceneComponent>(Result);
			Result->SetRootComponent(Root);
			Root->RegisterComponent();
			Result->SetActorLocation(Player->GetActorLocation() + FVector(100.0f, 0.0f, 0.0f));
			if (bEnemyReward)
			{
				if (!Result->InitializeEnemyLoot(AWeaponRuntime::StaticClass(), Ammo)) return nullptr;
			}
			else
			{
				FWeaponPickupState Saved;
				Saved.bHasSavedState = true;
				Saved.CurrentAmmo = Ammo;
				Saved.ReserveAmmo = 3;
				Result->InitializeDroppedWeapon(AWeaponRuntime::StaticClass(), Saved, Player, 0.0f);
			}
			Player->RegisterNearbyWeaponPickup(Result);
			return Result;
		}
		bool EnableAutoPickup(AWeaponPickupActor* Pickup)
		{
			Player->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
			auto* Volume = NewObject<USphereComponent>(Pickup, TEXT("AutoPickupCollision"));
			Volume->SetupAttachment(Pickup->GetRootComponent());
			Volume->SetSphereRadius(250.0f);
			Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Volume->SetCollisionResponseToAllChannels(ECR_Overlap);
			Volume->SetGenerateOverlapEvents(true);
			Volume->RegisterComponent();
			Volume->UpdateOverlaps();
			return Volume->IsOverlappingActor(Player);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustInitialDrawRestoreBridgeTest,
	"Dust2.Inventory.NativeInitialDrawRestoreBridge", LootTestFlags)
bool FDustInitialDrawRestoreBridgeTest::RunTest(const FString&)
{
	FLootInventoryWorld F(true, true);
	if (!TestTrue(TEXT("Inventory ready"), F.bReady)) return false;
	auto* Presentation = NewObject<UWeaponPresentationComponent>(F.Player);
	F.Player->AddInstanceComponent(Presentation);
	Presentation->RegisterComponent();
	AWeaponRuntime* First = F.Inventory->GetSlotWeaponNative(0);
	AWeaponRuntime* Second = F.Inventory->GetSlotWeaponNative(1);
	if (!First || !Second) return false;
	F.Player->ApplyPickupInitialDrawConsumed(First);
	TestTrue(TEXT("native capture reads the active presentation store"),
		F.Player->HasConsumedPickupInitialDraw(First));
	TestFalse(TEXT("other slot retains its first draw"),
		F.Player->HasConsumedPickupInitialDraw(Second));

	TestTrue(TEXT("checkpoint rebuild succeeds before action initialization"),
		F.Inventory->RestoreCheckpointWeapons(F.Slot1, F.Slot2,
			AWeaponRuntime::StaticClass(), 10, 20, EFireMode::SemiAutomatic, true,
			AWeaponRuntime::StaticClass(), 12, 18, EFireMode::SemiAutomatic, false, true));
	AWeaponRuntime* Rebuilt = Cast<AWeaponRuntime>(F.Slot1->GetChildActor());
	TestTrue(TEXT("new weapon receives the saved mark through the native hook"),
		Rebuilt && F.Player->HasConsumedPickupInitialDraw(Rebuilt));
	TestFalse(TEXT("unconsumed second slot stays unmarked"),
		F.Player->HasConsumedPickupInitialDraw(Cast<AWeaponRuntime>(F.Slot2->GetChildActor())));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupFocusTest,
	"Dust2.Inventory.PickupFocusRequiresDirectRegisteredHit", LootTestFlags)
bool FDustPickupFocusTest::RunTest(const FString&)
{
	FLootInventoryWorld F(false, false);
	if (!TestTrue(TEXT("Inventory ready"), F.bReady)) return false;
	AWeaponPickupActor* Pickup = F.Pickup(12);
	if (!TestNotNull(TEXT("Registered pickup"), Pickup)) return false;

	FVector Eye;
	FRotator View;
	F.Player->GetActorEyesViewPoint(Eye, View);
	Pickup->SetActorLocation(Eye + View.Vector() * 100.0f);
	USphereComponent* Shape = NewObject<USphereComponent>(Pickup);
	Shape->SetupAttachment(Pickup->GetRootComponent());
	Shape->SetSphereRadius(10.0f);
	Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shape->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Shape->RegisterComponent();
	TestTrue(TEXT("Direct registered hit has focus"), F.Player->GetAimedWeaponPickup() == Pickup);
	F.Player->UnregisterNearbyWeaponPickup(Pickup);
	TestNull(TEXT("Unregistered direct hit has no focus"), F.Player->GetAimedWeaponPickup());

	F.Player->RegisterNearbyWeaponPickup(Pickup);
	Pickup->AddActorWorldOffset(View.RotateVector(FVector(0, 50, 0)));
	TestNull(TEXT("Looking beside pickup clears focus"), F.Player->GetAimedWeaponPickup());
	TestTrue(TEXT("Nearby visible fallback is still eligible"), F.Player->GetNearbyWeaponPickup() == Pickup);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootAmmoSnapshot,
	"Dust2.Loot.FixedAmmoSnapshotAndOrigin", LootTestFlags)
bool FDustLootAmmoSnapshot::RunTest(const FString&)
{
	FLootWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* Pickup = F.World->SpawnActor<AWeaponPickupActor>();
	if (!TestNotNull(TEXT("Pickup"), Pickup)) return false;
	TestFalse(TEXT("No class rejected"), Pickup->InitializeEnemyLoot(nullptr, 12));
	TestFalse(TEXT("Zero ammo rejected"), Pickup->InitializeEnemyLoot(AWeaponRuntime::StaticClass(), 0));
	TestTrue(TEXT("Enemy reward initialized"), Pickup->InitializeEnemyLoot(AWeaponRuntime::StaticClass(), 47));
	const FWeaponPickupState State = Pickup->GetPickupSavedState();
	TestTrue(TEXT("Never uses default initial ammo"), State.bHasSavedState);
	TestEqual(TEXT("Total ammo conserved"), State.CurrentAmmo + State.ReserveAmmo, 47);
	TestEqual(TEXT("Magazine bounded"), State.CurrentAmmo, 30);
	TestEqual(TEXT("Remainder in reserve"), State.ReserveAmmo, 17);
	TestEqual(TEXT("Fixed reward amount"), Pickup->GetEnemyLootAmmoRewardNative(), 47);
	TestEqual(TEXT("Repeated queries do not reroll"), Pickup->GetPickupSavedState().ReserveAmmo, 17);
	Pickup->InitializeDroppedWeapon(AWeaponRuntime::StaticClass(), State, nullptr, 0.0f);
	TestEqual(TEXT("Player drop is no longer an enemy reward"), Pickup->GetEnemyLootAmmoRewardNative(), 0);
	TestEqual(TEXT("Player drop retains actual ammo"), Pickup->GetPickupSavedState().ReserveAmmo, 17);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootChanceAndMapping,
	"Dust2.Loot.ChanceBoundariesAndDefinitionPickupAssociation", LootTestFlags)
bool FDustLootChanceAndMapping::RunTest(const FString&)
{
	TestFalse(TEXT("Zero chance"), FDustEnemyLootTestAccess::Roll(0.0f));
	TestFalse(TEXT("Negative chance"), FDustEnemyLootTestAccess::Roll(-1.0f));
	TestTrue(TEXT("Certain chance"), FDustEnemyLootTestAccess::Roll(100.0f));
	TestTrue(TEXT("Overbound saturates"), FDustEnemyLootTestAccess::Roll(101.0f));
	FScopedPickupDefaults Defaults;
	FLootWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* Loot = F.AddLoot();
	if (!TestNotNull(TEXT("Loot"), Loot)) return false;
	auto* A = NewObject<UWeaponDefinitionData>(Loot);
	auto* B = NewObject<UWeaponDefinitionData>(Loot);
	FDustEnemyLootWeaponEntry Entry;
	Entry.EnemyDefinition = A;
	Entry.WeaponClass = AWeaponRuntime::StaticClass();
	Entry.PickupClass = AWeaponPickupActor::StaticClass();
	Loot->WeaponDropMapping.Add(Entry);
	TestFalse(TEXT("Old enemy mapping is not a second source of truth"), FDustEnemyLootTestAccess::HasMapping(Loot, A));
	A->WeaponPickupClass = AWeaponPickupActor::StaticClass();
	TSubclassOf<AWeaponPickupActor> PickupClass;
	TSubclassOf<AWeaponRuntime> WeaponClass;
	TestTrue(TEXT("One definition association resolves both classes"), FDustEnemyLootTestAccess::Resolve(Loot, A, PickupClass, WeaponClass));
	TestTrue(TEXT("Pickup comes from selected definition"), PickupClass.Get() == AWeaponPickupActor::StaticClass());
	TestTrue(TEXT("Formal class comes from pickup defaults"), WeaponClass.Get() == AWeaponRuntime::StaticClass());
	TestFalse(TEXT("Unconfigured different definition has no inferred fallback"), FDustEnemyLootTestAccess::HasMapping(Loot, B));
	Defaults.PickupDefaults->InitializeDroppedWeapon(nullptr, FWeaponPickupState(), nullptr, 0.0f);
	TestFalse(TEXT("Invalid pickup weapon class is rejected"), FDustEnemyLootTestAccess::Resolve(Loot, A, PickupClass, WeaponClass));
	TestTrue(TEXT("Failure clears both outputs"), !PickupClass && !WeaponClass);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootOnceAndBoth,
	"Dust2.Loot.IndependentBothDropsAndOnce", LootTestFlags)
bool FDustLootOnceAndBoth::RunTest(const FString&)
{
	FScopedPickupDefaults Defaults;
	FLootWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* Loot = F.AddLoot();
	if (!TestNotNull(TEXT("Loot"), Loot)) return false;
	auto* Definition = NewObject<UWeaponDefinitionData>(Loot);
	Definition->WeaponPickupClass = AWeaponPickupActor::StaticClass();
	Loot->WeaponDropChance = 100.0f;
	Loot->HealthDropChance = 100.0f;
	Loot->HealthPickupClass = AActor::StaticClass();
	Loot->MinimumTotalAmmo = Loot->MaximumTotalAmmo = 12;
	const int32 Before = F.ActorCount();
	FDustEnemyLootTestAccess::Drop(Loot, Definition);
	TestEqual(TEXT("Independent rolls permit two items"), F.ActorCount(), Before + 2);
	auto* Pickup = F.FirstWeaponPickup();
	if (!TestNotNull(TEXT("Spawned weapon pickup"), Pickup)) return false;
	TestEqual(TEXT("Spawned pickup keeps fixed reward metadata"), Pickup->GetEnemyLootAmmoRewardNative(), 12);
	FDustEnemyLootTestAccess::Drop(Loot, Definition);
	TestEqual(TEXT("Duplicate death does not spawn again"), F.ActorCount(), Before + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootNoDropAndLivingGuard,
	"Dust2.Loot.NoDropIsFinalAndLivingGuard", LootTestFlags)
bool FDustLootNoDropAndLivingGuard::RunTest(const FString&)
{
	FLootWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* Loot = F.AddLoot();
	if (!TestNotNull(TEXT("Loot"), Loot)) return false;
	Loot->NativeDropEnemyLoot();
	TestFalse(TEXT("Living owner cannot consume death attempt"), FDustEnemyLootTestAccess::Attempted(Loot));
	Loot->WeaponDropChance = Loot->HealthDropChance = 0.0f;
	const int32 Before = F.ActorCount();
	FDustEnemyLootTestAccess::Drop(Loot, nullptr);
	TestTrue(TEXT("No-drop still consumes the death attempt"), FDustEnemyLootTestAccess::Attempted(Loot));
	Loot->HealthDropChance = 100.0f;
	Loot->HealthPickupClass = AActor::StaticClass();
	FDustEnemyLootTestAccess::Drop(Loot, nullptr);
	TestEqual(TEXT("No reroll after a no-drop result"), F.ActorCount(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootHealthIndependent,
	"Dust2.Loot.HealthIndependentOfWeaponMapping", LootTestFlags)
bool FDustLootHealthIndependent::RunTest(const FString&)
{
	FLootWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* Loot = F.AddLoot();
	if (!TestNotNull(TEXT("Loot"), Loot)) return false;
	Loot->WeaponDropChance = 0.0f;
	Loot->HealthDropChance = 100.0f;
	Loot->HealthPickupClass = AActor::StaticClass();
	const int32 Before = F.ActorCount();
	FDustEnemyLootTestAccess::Drop(Loot, nullptr);
	TestEqual(TEXT("Health does not require weapon configuration"), F.ActorCount(), Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootFullInventoryConversion,
	"Dust2.Loot.FullInventoryPrefersCurrentMatchingWeapon", LootTestFlags)
bool FDustLootFullInventoryConversion::RunTest(const FString&)
{
	FLootInventoryWorld F(true, true, 1);
	if (!TestTrue(TEXT("Inventory ready"), F.bReady)) return false;
	auto* First = Cast<AWeaponRuntime>(F.Slot1->GetChildActor());
	auto* Current = Cast<AWeaponRuntime>(F.Slot2->GetChildActor());
	if (!TestNotNull(TEXT("First"), First) || !TestNotNull(TEXT("Current"), Current)) return false;
	First->RestoreAmmo(7, 30);
	Current->RestoreAmmo(9, 40);
	auto* Pickup = F.Pickup(12);
	if (!TestNotNull(TEXT("Pickup"), Pickup)) return false;
	if (!TestTrue(TEXT("Automatic pickup overlap"), F.EnableAutoPickup(Pickup))) return false;
	TestTrue(TEXT("Same type converted with full slots before automatic rejection"), F.Inventory->TryPickupNative(Pickup, false));
	TestEqual(TEXT("Only current matching weapon gains reserve"), Current->GetReserveAmmo(), 52);
	TestEqual(TEXT("Other matching slot unaffected"), First->GetReserveAmmo(), 30);
	TestEqual(TEXT("Magazine unchanged"), Current->GetCurrentAmmo(), 9);
	TestTrue(TEXT("Equipped instance unchanged"), F.Inventory->GetCurrentWeaponNative() == Current);
	TestTrue(TEXT("Reward consumed once"), Pickup->IsActorBeingDestroyed());
	TestFalse(TEXT("Repeated pickup rejected"), F.Inventory->TryPickupNative(Pickup, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootEmptySlotConversion,
	"Dust2.Loot.MatchingWeaponConvertsBeforeFillingEmptySlot", LootTestFlags)
bool FDustLootEmptySlotConversion::RunTest(const FString&)
{
	FLootInventoryWorld F(true, false);
	if (!TestTrue(TEXT("Inventory ready"), F.bReady)) return false;
	auto* Weapon = Cast<AWeaponRuntime>(F.Slot1->GetChildActor());
	if (!TestNotNull(TEXT("Weapon"), Weapon)) return false;
	Weapon->RestoreAmmo(4, 10);
	auto* Pickup = F.Pickup(8);
	if (!TestNotNull(TEXT("Pickup"), Pickup)) return false;
	TestTrue(TEXT("Matching reward accepted"), F.Inventory->TryPickupNative(Pickup, true));
	TestEqual(TEXT("Reserve increased"), Weapon->GetReserveAmmo(), 18);
	TestNull(TEXT("Empty slot stays empty"), F.Slot2->GetChildActor());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootFreshWeaponAmmo,
	"Dust2.Loot.NoOwnedWeaponReceivesExactRewardAmmo", LootTestFlags)
bool FDustLootFreshWeaponAmmo::RunTest(const FString&)
{
	FLootInventoryWorld F(false, false);
	if (!TestTrue(TEXT("Inventory ready"), F.bReady)) return false;
	auto* Pickup = F.Pickup(12);
	if (!TestNotNull(TEXT("Pickup"), Pickup)) return false;
	TestTrue(TEXT("New weapon accepted"), F.Inventory->TryPickupNative(Pickup, true));
	auto* Weapon = F.Inventory->GetSlotWeaponNative(0);
	if (!TestNotNull(TEXT("Created weapon"), Weapon)) return false;
	TestEqual(TEXT("No default ammo giveaway"), Weapon->GetCurrentAmmo() + Weapon->GetReserveAmmo(), 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustLootOverflowKeepsPickup,
	"Dust2.Loot.RejectedRewardKeepsFixedPickup", LootTestFlags)
bool FDustLootOverflowKeepsPickup::RunTest(const FString&)
{
	FLootInventoryWorld F(true, false);
	if (!TestTrue(TEXT("Inventory ready"), F.bReady)) return false;
	auto* Weapon = F.Inventory->GetSlotWeaponNative(0);
	if (!TestNotNull(TEXT("Weapon"), Weapon)) return false;
	Weapon->RestoreAmmo(1, MAX_int32);
	auto* Pickup = F.Pickup(12);
	if (!TestNotNull(TEXT("Pickup"), Pickup)) return false;
	TestFalse(TEXT("Overflow rejected"), F.Inventory->TryPickupNative(Pickup, true));
	TestFalse(TEXT("Pickup still present"), Pickup->IsActorBeingDestroyed());
	TestEqual(TEXT("Failed attempt does not reroll"), Pickup->GetEnemyLootAmmoRewardNative(), 12);
	Weapon->RestoreAmmo(1, 0);
	TestTrue(TEXT("Same item may succeed later"), F.Inventory->TryPickupNative(Pickup, true));
	TestEqual(TEXT("Original fixed reward preserved"), Weapon->GetReserveAmmo(), 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerDropNotConverted,
	"Dust2.Loot.PlayerDropRetainsExistingPickupBehavior", LootTestFlags)
bool FDustPlayerDropNotConverted::RunTest(const FString&)
{
	FLootInventoryWorld F(true, false);
	if (!TestTrue(TEXT("Inventory ready"), F.bReady)) return false;
	auto* Owned = F.Inventory->GetSlotWeaponNative(0);
	if (!TestNotNull(TEXT("Owned weapon"), Owned)) return false;
	Owned->RestoreAmmo(5, 20);
	auto* Pickup = F.Pickup(7, false);
	if (!TestNotNull(TEXT("Player dropped pickup"), Pickup)) return false;
	TestTrue(TEXT("Ordinary drop uses existing free-slot path"), F.Inventory->TryPickupNative(Pickup, true));
	auto* Second = F.Inventory->GetSlotWeaponNative(1);
	if (!TestNotNull(TEXT("Second slot"), Second)) return false;
	TestEqual(TEXT("Owned reserve not farmed"), Owned->GetReserveAmmo(), 20);
	TestEqual(TEXT("Actual drop snapshot retained"), Second->GetCurrentAmmo() + Second->GetReserveAmmo(), 10);
	return true;
}

#endif
