#include "Interfaces/WeaponKitInterfaces.h"

bool IWeaponKitCarrier::IsWeaponKitCarrierAlive_Implementation() const
{
	return true;
}

bool IWeaponKitCarrier::HasConsumedWeaponInitialDraw_Implementation(AWeaponKitRuntime*) const
{
	return false;
}

void IWeaponKitCarrier::ApplyWeaponInitialDrawConsumed_Implementation(AWeaponKitRuntime*)
{
}

bool IWeaponKitSlotHost::IsSlotHostReady_Implementation() const
{
	return false;
}

bool IWeaponKitSlotHost::IsHostedContentValid_Implementation() const
{
	return true;
}

AWeaponKitRuntime* IWeaponKitSlotHost::GetHostedWeapon_Implementation() const
{
	return nullptr;
}

bool IWeaponKitSlotHost::SpawnHostedWeapon_Implementation(TSubclassOf<AWeaponKitRuntime>)
{
	return false;
}

void IWeaponKitSlotHost::ClearHostedWeapon_Implementation()
{
}

TSubclassOf<AWeaponKitRuntime> IWeaponKitPickup::GetPickupWeaponClass_Implementation() const
{
	return nullptr;
}

FWeaponKitPickupState IWeaponKitPickup::GetPickupSavedState_Implementation() const
{
	return FWeaponKitPickupState();
}

bool IWeaponKitPickup::CanBePickedUpBy_Implementation(const APawn*, bool) const
{
	return false;
}

bool IWeaponKitPickup::TryClaimPickup_Implementation(APawn*, bool)
{
	return false;
}

void IWeaponKitPickup::ReleasePickupClaim_Implementation()
{
}

void IWeaponKitPickup::InitializeDroppedWeapon_Implementation(
	TSubclassOf<AWeaponKitRuntime>,
	const FWeaponKitPickupState&,
	APawn*,
	float)
{
}

UPrimitiveComponent* IWeaponKitPickup::GetAutoPickupVolume_Implementation() const
{
	return nullptr;
}
