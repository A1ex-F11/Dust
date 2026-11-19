#include "Runtime/WeaponKitPickup.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/WeaponKitInterfaces.h"
#include "Runtime/WeaponKitRuntime.h"

AWeaponKitPickup::AWeaponKitPickup()
{
	PrimaryActorTick.bCanEverTick = false;
}

TSubclassOf<AWeaponKitRuntime> AWeaponKitPickup::GetPickupWeaponClass_Implementation() const
{
	return PickupWeaponClass;
}

FWeaponKitPickupState AWeaponKitPickup::GetPickupSavedState_Implementation() const
{
	return PickupSavedState;
}

UPrimitiveComponent* AWeaponKitPickup::GetAutoPickupVolume_Implementation() const
{
	return AutoPickupVolume;
}

void AWeaponKitPickup::SetAutoPickupVolume(UPrimitiveComponent* Volume)
{
	AutoPickupVolume = Volume;
}

bool AWeaponKitPickup::CanBePickedUpBy_Implementation(const APawn* Pawn, bool bAutomatic) const
{
	UClass* WeaponClass = PickupWeaponClass.Get();
	const bool bCarrierAlive = !IsValid(Pawn) ||
		!Pawn->GetClass()->ImplementsInterface(UWeaponKitCarrier::StaticClass()) ||
		IWeaponKitCarrier::Execute_IsWeaponKitCarrierAlive(Pawn);
	if (!IsValid(Pawn) || !bCarrierAlive || IsActorBeingDestroyed() || bPickupClaimed ||
		!WeaponClass || WeaponClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	if (!bAutomatic)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	if (!World || (AutoPickupBlockedPawn.Get() == Pawn && World->GetTimeSeconds() < AutoPickupBlockedUntil))
	{
		return false;
	}
	const UPrimitiveComponent* Volume = IWeaponKitPickup::Execute_GetAutoPickupVolume(this);
	return Volume && Volume->IsRegistered() && Volume->IsOverlappingActor(Pawn);
}

bool AWeaponKitPickup::TryClaimPickup_Implementation(APawn* Pawn, bool bAutomatic)
{
	if (!IWeaponKitPickup::Execute_CanBePickedUpBy(this, Pawn, bAutomatic))
	{
		return false;
	}
	bPickupClaimed = true;
	return true;
}

void AWeaponKitPickup::ReleasePickupClaim_Implementation()
{
	bPickupClaimed = false;
}

void AWeaponKitPickup::InitializeDroppedWeapon_Implementation(
	TSubclassOf<AWeaponKitRuntime> WeaponClass,
	const FWeaponKitPickupState& State,
	APawn* DroppedBy,
	float AutoPickupBlockSeconds)
{
	PickupWeaponClass = WeaponClass;
	PickupSavedState = State;
	AutoPickupBlockedPawn = DroppedBy;
	if (const UWorld* World = GetWorld())
	{
		AutoPickupBlockedUntil = World->GetTimeSeconds() + FMath::Max(0.0f, AutoPickupBlockSeconds);
	}
	bPickupClaimed = false;
}
