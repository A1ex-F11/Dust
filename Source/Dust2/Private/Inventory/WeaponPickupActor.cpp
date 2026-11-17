#include "Inventory/WeaponPickupActor.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

AWeaponPickupActor::AWeaponPickupActor()
{
    PrimaryActorTick.bCanEverTick = false;
}

TSubclassOf<AWeaponRuntime> AWeaponPickupActor::GetPickupWeaponClass() const
{
    return PickupWeaponClass;
}

FWeaponPickupState AWeaponPickupActor::GetPickupSavedState() const
{
    return PickupSavedState;
}

const UPrimitiveComponent* AWeaponPickupActor::FindAutoPickupVolume() const
{
    TInlineComponentArray<UPrimitiveComponent*> Components;
    GetComponents(Components);
    for (const UPrimitiveComponent* Component : Components)
    {
        if (IsValid(Component) && Component->GetFName() == AutoPickupComponentName)
        {
            return Component;
        }
    }
    return nullptr;
}

bool AWeaponPickupActor::CanBePickedUpBy(const APawn* Player, bool bAutomatic) const
{
    if (!NativeCanAttemptPickup(Player) || bPickupClaimed || !PickupWeaponClass ||
        PickupWeaponClass->HasAnyClassFlags(CLASS_Abstract))
    {
        return false;
    }
    if (!bAutomatic)
    {
        return true;
    }
    const UWorld* World = GetWorld();
    if (!World || (AutoPickupBlockedPawn.Get() == Player &&
        World->GetTimeSeconds() < AutoPickupBlockedUntil))
    {
        return false;
    }
    const UPrimitiveComponent* Volume = FindAutoPickupVolume();
    return Volume && Volume->IsRegistered() && Volume->IsOverlappingActor(Player);
}

bool AWeaponPickupActor::TryClaimPickup(APawn* Player, bool bAutomatic)
{
    if (!CanBePickedUpBy(Player, bAutomatic))
    {
        return false;
    }
    bPickupClaimed = true;
    return true;
}

void AWeaponPickupActor::ReleasePickupClaim()
{
    bPickupClaimed = false;
}

bool AWeaponPickupActor::InitializeEnemyLoot(
    TSubclassOf<AWeaponRuntime> WeaponClass, int32 TotalAmmo)
{
    if (!WeaponClass || WeaponClass->HasAnyClassFlags(CLASS_Abstract) || TotalAmmo <= 0)
    {
        return false;
    }
    const AWeaponRuntime* Defaults = WeaponClass.GetDefaultObject();
    if (!Defaults) return false;
    const UWeaponDefinitionData* Definition = Defaults->WeaponDefinition;
    const int32 Capacity = Definition ? Definition->MagazineCapacity : Defaults->GetMagazineCapacity();
    if (Capacity <= 0) return false;

    FWeaponPickupState State;
    State.bHasSavedState = true;
    State.CurrentAmmo = FMath::Min(TotalAmmo, Capacity);
    State.ReserveAmmo = TotalAmmo - State.CurrentAmmo;
    State.FireMode = Definition ? Definition->GetInitialFireMode() : Defaults->GetFireMode();
    InitializeDroppedWeapon(WeaponClass, State, nullptr, 0.0f);
    bEnemyLoot = true;
    EnemyLootAmmoReward = TotalAmmo;
    return true;
}

void AWeaponPickupActor::InitializeDroppedWeapon(
    TSubclassOf<AWeaponRuntime> WeaponClass,
    const FWeaponPickupState& State,
    APawn* DroppedBy,
    float AutoPickupBlockSeconds)
{
    bEnemyLoot = false;
    EnemyLootAmmoReward = 0;
    PickupWeaponClass = WeaponClass;
    PickupSavedState = State;
    AutoPickupBlockedPawn = DroppedBy;
    if (const UWorld* World = GetWorld())
    {
        AutoPickupBlockedUntil = World->GetTimeSeconds() +
            FMath::Max(0.0f, AutoPickupBlockSeconds);
    }
    bPickupClaimed = false;
}
