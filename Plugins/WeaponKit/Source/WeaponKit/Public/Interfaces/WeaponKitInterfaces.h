#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponKitTypes.h"
#include "Runtime/WeaponKitRuntime.h"
#include "Interfaces/WeaponKitInterfaces.generated.h"

class APawn;
class UPrimitiveComponent;

/**
 * Optional queries on the pawn that carries weapons.
 * Owners that do not implement this interface are treated as alive, with no initial-draw memory.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class WEAPONKIT_API UWeaponKitCarrier : public UInterface
{
	GENERATED_BODY()
};

class WEAPONKIT_API IWeaponKitCarrier
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Carrier")
	bool IsWeaponKitCarrierAlive() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Carrier")
	bool HasConsumedWeaponInitialDraw(AWeaponKitRuntime* Weapon) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Carrier")
	void ApplyWeaponInitialDrawConsumed(AWeaponKitRuntime* Weapon);
};

/** Something that can host one runtime weapon. Child-actor slots are one implementation, not the only one. */
UINTERFACE(MinimalAPI, BlueprintType)
class WEAPONKIT_API UWeaponKitSlotHost : public UInterface
{
	GENERATED_BODY()
};

class WEAPONKIT_API IWeaponKitSlotHost
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Slot")
	bool IsSlotHostReady() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Slot")
	bool IsHostedContentValid() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Slot")
	AWeaponKitRuntime* GetHostedWeapon() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Slot")
	bool SpawnHostedWeapon(TSubclassOf<AWeaponKitRuntime> WeaponClass);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Slot")
	void ClearHostedWeapon();
};

/** A world pickup. Inventory talks to this interface and does not require a specific actor class. */
UINTERFACE(MinimalAPI, BlueprintType)
class WEAPONKIT_API UWeaponKitPickup : public UInterface
{
	GENERATED_BODY()
};

class WEAPONKIT_API IWeaponKitPickup
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Pickup")
	TSubclassOf<AWeaponKitRuntime> GetPickupWeaponClass() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Pickup")
	FWeaponKitPickupState GetPickupSavedState() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Pickup")
	bool CanBePickedUpBy(const APawn* Pawn, bool bAutomatic) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Pickup")
	bool TryClaimPickup(APawn* Pawn, bool bAutomatic);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Pickup")
	void ReleasePickupClaim();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Pickup")
	void InitializeDroppedWeapon(
		TSubclassOf<AWeaponKitRuntime> WeaponClass,
		const FWeaponKitPickupState& State,
		APawn* DroppedBy,
		float AutoPickupBlockSeconds);

	/** Overlap volume used for automatic pickup. Null disables automatic pickup. No component-name search. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeaponKit|Pickup")
	UPrimitiveComponent* GetAutoPickupVolume() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "WeaponKit|Pickup")
	void LaunchDroppedWeapon(FVector ForwardDirection);
};
