#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Identity/DustStableIdComponent.h"
#include "Inventory/DustPickupPersistenceComponent.h"
#include "Inventory/DustPickupPersistenceSubsystem.h"

struct FDustPickupPersistenceTestAccess
{
	static void SetPersistence(UDustPickupPersistenceComponent* Component,
		UDustPickupPersistenceSubsystem* Persistence)
	{
		if (Component) Component->PersistenceOverride = Persistence;
	}
};

namespace
{
	constexpr auto StableIdTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FStableIdWorld
	{
		UWorld* World = nullptr;

		FStableIdWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DustStableIdTestWorld"), nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FStableIdWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}

		AActor* AddActor()
		{
			return World ? World->SpawnActor<AActor>() : nullptr;
		}

		UDustStableIdComponent* AddStableId(AActor* Owner)
		{
			if (!Owner) return nullptr;
			auto* Component = NewObject<UDustStableIdComponent>(Owner);
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}

		UDustPickupPersistenceComponent* AddPickup(AActor* Owner)
		{
			if (!Owner) return nullptr;
			auto* Component = NewObject<UDustPickupPersistenceComponent>(Owner);
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}

		FName LevelId() const
		{
			return World
				? FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()))
				: NAME_None;
		}
	};

	UDustPickupPersistenceSubsystem* NewPickupPersistence()
	{
		UGameInstance* Instance = NewObject<UGameInstance>();
		return NewObject<UDustPickupPersistenceSubsystem>(Instance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustStableIdGeneratedDistinct,
	"Dust2.StableId.GeneratedDistinct", StableIdTestFlags)
bool FDustStableIdGeneratedDistinct::RunTest(const FString&)
{
	const FName A = UDustStableIdComponent::GenerateStableId(TEXT("Enemy"));
	const FName B = UDustStableIdComponent::GenerateStableId(TEXT("Enemy"));
	TestFalse(TEXT("Generated id is not empty"), A.IsNone());
	TestTrue(TEXT("Generated id keeps prefix"), A.ToString().StartsWith(TEXT("Enemy_")));
	TestNotEqual(TEXT("Two generated ids are distinct"), A, B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustStableIdEditorRepairAndRuntimeNoGen,
	"Dust2.StableId.ExplicitRepairAndRuntimeNoAutoGeneration", StableIdTestFlags)
bool FDustStableIdEditorRepairAndRuntimeNoGen::RunTest(const FString&)
{
	FStableIdWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;

	AActor* Owner = F.AddActor();
	UDustStableIdComponent* Component = F.AddStableId(Owner);
	if (!TestNotNull(TEXT("Stable id component"), Component)) return false;

	TestTrue(TEXT("Runtime registration did not auto-generate"), Component->NativeGetStableId().IsNone());
	TestTrue(TEXT("Explicit ensure fills missing"), Component->NativeEnsureStableId());
	const FName First = Component->NativeGetStableId();
	TestFalse(TEXT("Ensure produced an id"), First.IsNone());
	TestTrue(TEXT("Ensure is idempotent"), Component->NativeEnsureStableId());
	TestEqual(TEXT("Existing id is stable"), Component->NativeGetStableId(), First);

	TestTrue(TEXT("Explicit regenerate"), Component->NativeRegenerateStableId());
	const FName Second = Component->NativeGetStableId();
	TestNotEqual(TEXT("Regenerate produces a new id"), First, Second);

	static_cast<UObject*>(Component)->PostDuplicate(EDuplicateMode::PIE);
	TestEqual(TEXT("PIE duplication keeps id"), Component->NativeGetStableId(), Second);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustStableIdDeferredResolution,
	"Dust2.StableId.DeferredTagAndResolver", StableIdTestFlags)
bool FDustStableIdDeferredResolution::RunTest(const FString&)
{
	FStableIdWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;

	AActor* Owner = F.AddActor();
	if (!TestNotNull(TEXT("Owner"), Owner)) return false;
	UDustStableIdComponent* Component = F.AddStableId(Owner);
	if (!TestNotNull(TEXT("Stable id component"), Component)) return false;

	TestTrue(TEXT("No id before deferred assignment"), Component->NativeGetStableId().IsNone());
	UDustStableIdComponent::SetDeferredStableId(Owner, TEXT("Enemy_A"));
	TestEqual(TEXT("Resolver reads pending entry id"), UDustStableIdComponent::ResolveStableIdForActor(Owner),
		FName(TEXT("Enemy_A")));
	TestEqual(TEXT("Component getter sees pending id"), Component->NativeGetStableId(),
		FName(TEXT("Enemy_A")));

	Component->NativeSetStableId(TEXT("Enemy_B"));
	TestEqual(TEXT("Explicit set replaces pending id"), Component->NativeGetStableId(),
		FName(TEXT("Enemy_B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupStableIdPrecedence,
	"Dust2.StableId.PickupOldIdWinsAndStableFallback", StableIdTestFlags)
bool FDustPickupStableIdPrecedence::RunTest(const FString&)
{
	FStableIdWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;

	AActor* Owner = F.AddActor();
	UDustStableIdComponent* Stable = F.AddStableId(Owner);
	UDustPickupPersistenceComponent* Pickup = F.AddPickup(Owner);
	if (!TestNotNull(TEXT("Stable"), Stable) || !TestNotNull(TEXT("Pickup"), Pickup)) return false;

	Stable->NativeSetStableId(TEXT("Stable_Pickup"));
	TestEqual(TEXT("Stable id is the fallback"), Pickup->NativeGetEffectivePickupId(),
		FName(TEXT("Stable_Pickup")));

	Pickup->PickupId = TEXT("Legacy_Manual");
	TestEqual(TEXT("Old hand id wins"), Pickup->NativeGetEffectivePickupId(),
		FName(TEXT("Legacy_Manual")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPickupStableIdConsumedAndDynamic,
	"Dust2.StableId.ConsumedStableIdAndDynamicNone", StableIdTestFlags)
bool FDustPickupStableIdConsumedAndDynamic::RunTest(const FString&)
{
	FStableIdWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	UDustPickupPersistenceSubsystem* Persistence = NewPickupPersistence();
	if (!TestNotNull(TEXT("Persistence"), Persistence)) return false;

	AActor* PlacedOwner = F.AddActor();
	UDustStableIdComponent* Stable = F.AddStableId(PlacedOwner);
	UDustPickupPersistenceComponent* Placed = F.AddPickup(PlacedOwner);
	if (!TestNotNull(TEXT("Stable"), Stable) || !TestNotNull(TEXT("Placed pickup"), Placed)) return false;
	FDustPickupPersistenceTestAccess::SetPersistence(Placed, Persistence);

	Stable->NativeSetStableId(TEXT("Preset_Ammo_01"));
	Persistence->RecordConsumed(F.LevelId(), TEXT("Preset_Ammo_01"));
	TestFalse(TEXT("Consumed stable id is unavailable after reload"), Placed->NativeIsPickupAvailable());

	AActor* DynamicOwner = F.AddActor();
	UDustPickupPersistenceComponent* Dynamic = F.AddPickup(DynamicOwner);
	if (!TestNotNull(TEXT("Dynamic pickup"), Dynamic)) return false;
	FDustPickupPersistenceTestAccess::SetPersistence(Dynamic, Persistence);

	TestTrue(TEXT("Dynamic drop has no effective id"), Dynamic->NativeGetEffectivePickupId().IsNone());
	TestTrue(TEXT("Dynamic drop stays available"), Dynamic->NativeIsPickupAvailable());
	TestFalse(TEXT("Dynamic drop success is not persisted"), Dynamic->RecordSuccessfulPickup());
	TestFalse(TEXT("Dynamic drop did not add a record"),
		Persistence->IsConsumed(F.LevelId(), NAME_None));
	return true;
}

#endif
