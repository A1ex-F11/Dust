#include "WeaponPickupActor.h"
#include "Components/PrimitiveComponent.h"
#include "Dust2PlayerCharacter.h"
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
    const ADust2PlayerCharacter* Character = Cast<ADust2PlayerCharacter>(Player);
    if (!IsValid(Character) || Character->IsDead() || IsActorBeingDestroyed() ||
        bPickupClaimed || !PickupWeaponClass ||
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

void AWeaponPickupActor::InitializeDroppedWeapon(
    TSubclassOf<AWeaponRuntime> WeaponClass,
    const FWeaponPickupState& State,
    APawn* DroppedBy,
    float AutoPickupBlockSeconds)
{
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
