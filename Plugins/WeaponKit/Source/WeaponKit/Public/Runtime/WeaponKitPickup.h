#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/WeaponKitInterfaces.h"
#include "Runtime/WeaponKitPickup.generated.h"

class APawn;
class UPrimitiveComponent;

UCLASS(Blueprintable)
class WEAPONKIT_API AWeaponKitPickup : public AActor, public IWeaponKitPickup
{
	GENERATED_BODY()

public:
	AWeaponKitPickup();

	virtual TSubclassOf<AWeaponKitRuntime> GetPickupWeaponClass_Implementation() const override;
	virtual FWeaponKitPickupState GetPickupSavedState_Implementation() const override;
	virtual bool CanBePickedUpBy_Implementation(const APawn* Pawn, bool bAutomatic) const override;
	virtual bool TryClaimPickup_Implementation(APawn* Pawn, bool bAutomatic) override;
	virtual void ReleasePickupClaim_Implementation() override;
	virtual void InitializeDroppedWeapon_Implementation(
		TSubclassOf<AWeaponKitRuntime> WeaponClass,
		const FWeaponKitPickupState& State,
		APawn* DroppedBy,
		float AutoPickupBlockSeconds) override;
	virtual UPrimitiveComponent* GetAutoPickupVolume_Implementation() const override;

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Pickup")
	void SetAutoPickupVolume(UPrimitiveComponent* Volume);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WeaponKit|Pickup")
	TSubclassOf<AWeaponKitRuntime> PickupWeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WeaponKit|Pickup")
	FWeaponKitPickupState PickupSavedState;

	/** Assign the overlap primitive directly. The kit does not search components by name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WeaponKit|Pickup")
	TObjectPtr<UPrimitiveComponent> AutoPickupVolume;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> AutoPickupBlockedPawn;

	double AutoPickupBlockedUntil = 0.0;
	bool bPickupClaimed = false;
};
