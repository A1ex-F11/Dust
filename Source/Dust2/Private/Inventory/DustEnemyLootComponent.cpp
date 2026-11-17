#include "Inventory/DustEnemyLootComponent.h"

#include "../Combat/DustActorState.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Engine/World.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Inventory/WeaponPickupActor.h"
#include "Inventory/DustEnemyLootProfile.h"
#include "Mission/DustEncounterComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDustEnemyLoot, Log, All);

UDustEnemyLootComponent::UDustEnemyLootComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
}

void UDustEnemyLootComponent::InitializeComponent()
{
	Super::InitializeComponent();
	const FDustDeferredEnemySpawnEntry* Entry = UDustEncounterComponent::FindInitializingEnemyEntry(GetOwner());
	if (!Entry) return;

	if (Entry->bOverrideLoot)
	{
		WeaponDropChance = Entry->WeaponDropChance;
		HealthDropChance = Entry->HealthDropChance;
		MinimumTotalAmmo = Entry->MinimumTotalAmmo;
		MaximumTotalAmmo = Entry->MaximumTotalAmmo;
		HealthPickupClass = Entry->HealthPickupClass;
		LocalDropOffset = Entry->LocalDropOffset;
		return;
	}

	const UDustEnemyLootProfile* Profile = Entry->LootProfile.Get();
	if (!IsValid(Profile)) return;

	WeaponDropChance = Profile->WeaponDropChance;
	HealthDropChance = Profile->HealthDropChance;
	MinimumTotalAmmo = Profile->MinimumTotalAmmo;
	MaximumTotalAmmo = Profile->MaximumTotalAmmo;
	HealthPickupClass = Profile->HealthPickupClass;
}

bool UDustEnemyLootComponent::RollChance(float Chance)
{
	return FMath::IsFinite(Chance) && Chance > 0.0f &&
		(Chance >= 100.0f || FMath::FRand() * 100.0f < Chance);
}

bool UDustEnemyLootComponent::ResolveWeaponDrop(const UWeaponDefinitionData* Definition,
	TSubclassOf<AWeaponPickupActor>& OutPickupClass, TSubclassOf<AWeaponRuntime>& OutWeaponClass) const
{
	OutPickupClass = nullptr;
	OutWeaponClass = nullptr;
	if (!IsValid(Definition) || Definition->WeaponPickupClass.IsNull()) return false;
	UClass* PickupClass = Definition->WeaponPickupClass.LoadSynchronous();
	if (!PickupClass || !PickupClass->IsChildOf(AWeaponPickupActor::StaticClass()) ||
		PickupClass->HasAnyClassFlags(CLASS_Abstract)) return false;
	const AWeaponPickupActor* PickupDefaults = PickupClass->GetDefaultObject<AWeaponPickupActor>();
	const TSubclassOf<AWeaponRuntime> WeaponClass = PickupDefaults ? PickupDefaults->GetPickupWeaponClass() : nullptr;
	if (!WeaponClass || WeaponClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	OutPickupClass = PickupClass;
	OutWeaponClass = WeaponClass;
	return true;
}

void UDustEnemyLootComponent::NativeDropEnemyLoot()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || !DustActorState::IsDead(Owner)) return;
	const UDustEnemyWeaponComponent* EnemyWeapon = Owner->FindComponentByClass<UDustEnemyWeaponComponent>();
	DropOnce(EnemyWeapon ? EnemyWeapon->WeaponDefinition.Get() : nullptr);
}

void UDustEnemyLootComponent::DropOnce(UWeaponDefinitionData* Definition)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (bDropAttempted || !IsValid(World) || !IsValid(Owner) || Owner->IsActorBeingDestroyed()) return;
	bDropAttempted = true;
	const bool bDropWeapon = RollChance(WeaponDropChance);
	const bool bDropHealth = RollChance(HealthDropChance);
	const FRotator Rotation(0.0f, Owner->GetActorRotation().Yaw, 0.0f);
	const FVector BaseLocation = Owner->GetActorLocation() + Rotation.RotateVector(LocalDropOffset);
	const FVector SideOffset = Rotation.RotateVector(FVector(0.0f, 25.0f, 0.0f));
	if (BaseLocation.ContainsNaN() || SideOffset.ContainsNaN()) return;

	if (bDropWeapon)
	{
		TSubclassOf<AWeaponPickupActor> PickupClass;
		TSubclassOf<AWeaponRuntime> WeaponClass;
		if (!ResolveWeaponDrop(Definition, PickupClass, WeaponClass))
		{
			UE_LOG(LogDustEnemyLoot, Warning, TEXT("Configure Weapon Pickup Class on %s and a valid pickup weapon class on that pickup Blueprint."),
				*GetNameSafe(Definition));
		}
		else
		{
			const int32 Low = FMath::Max(1, FMath::Min(MinimumTotalAmmo, MaximumTotalAmmo));
			const int32 High = FMath::Max(Low, FMath::Max(MinimumTotalAmmo, MaximumTotalAmmo));
			const int32 TotalAmmo = FMath::RandRange(Low, High);
			const FTransform Transform(Rotation, BaseLocation - SideOffset);
			AWeaponPickupActor* Pickup = World->SpawnActorDeferred<AWeaponPickupActor>(
				PickupClass.Get(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (IsValid(Pickup))
			{
				if (Pickup->InitializeEnemyLoot(WeaponClass, TotalAmmo))
				{
					Pickup->FinishSpawning(Transform);
				}
				else
				{
					Pickup->Destroy();
				}
			}
		}
	}

	if (bDropHealth && HealthPickupClass && !HealthPickupClass->HasAnyClassFlags(CLASS_Abstract))
	{
		const FTransform Transform(Rotation, BaseLocation + SideOffset);
		AActor* Pickup = World->SpawnActorDeferred<AActor>(HealthPickupClass.Get(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (IsValid(Pickup)) Pickup->FinishSpawning(Transform);
	}
}
