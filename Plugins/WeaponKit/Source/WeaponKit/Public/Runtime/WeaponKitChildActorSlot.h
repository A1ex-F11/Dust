#pragma once

#include "CoreMinimal.h"
#include "Components/ChildActorComponent.h"
#include "Interfaces/WeaponKitInterfaces.h"
#include "Runtime/WeaponKitChildActorSlot.generated.h"

class AWeaponKitRuntime;

/** Default slot host. Inventory does not require this class; any IWeaponKitSlotHost works. */
UCLASS(ClassGroup = (WeaponKit), meta = (BlueprintSpawnableComponent))
class WEAPONKIT_API UWeaponKitChildActorSlot : public UChildActorComponent, public IWeaponKitSlotHost
{
	GENERATED_BODY()

public:
	virtual bool IsSlotHostReady_Implementation() const override;
	virtual bool IsHostedContentValid_Implementation() const override;
	virtual AWeaponKitRuntime* GetHostedWeapon_Implementation() const override;
	virtual bool SpawnHostedWeapon_Implementation(TSubclassOf<AWeaponKitRuntime> WeaponClass) override;
	virtual void ClearHostedWeapon_Implementation() override;
};
