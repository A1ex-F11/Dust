#include "Runtime/WeaponKitChildActorSlot.h"
#include "Runtime/WeaponKitRuntime.h"

bool UWeaponKitChildActorSlot::IsSlotHostReady_Implementation() const
{
	return IsRegistered() && IsValid(GetOwner());
}

bool UWeaponKitChildActorSlot::IsHostedContentValid_Implementation() const
{
	AActor* Child = GetChildActor();
	return !IsValid(Child) || IsValid(Cast<AWeaponKitRuntime>(Child));
}

AWeaponKitRuntime* UWeaponKitChildActorSlot::GetHostedWeapon_Implementation() const
{
	return Cast<AWeaponKitRuntime>(GetChildActor());
}

bool UWeaponKitChildActorSlot::SpawnHostedWeapon_Implementation(TSubclassOf<AWeaponKitRuntime> WeaponClass)
{
	UClass* Class = WeaponClass.Get();
	if (!Class || !Class->IsChildOf(AWeaponKitRuntime::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	SetChildActorClass(nullptr);
	SetChildActorClass(WeaponClass);
	return IsValid(Cast<AWeaponKitRuntime>(GetChildActor()));
}

void UWeaponKitChildActorSlot::ClearHostedWeapon_Implementation()
{
	SetChildActorClass(nullptr);
}
