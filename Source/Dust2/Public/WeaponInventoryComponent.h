#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponRuntime.h"
#include "WeaponInventoryComponent.generated.h"

class AWeaponPickupActor;
class UChildActorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnInventoryChanged,
	AWeaponRuntime*, Slot1Weapon,
	AWeaponRuntime*, Slot2Weapon,
	AWeaponRuntime*, CurrentWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnBeforeWeaponChange,
	AWeaponRuntime*, OldWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnEquippedWeaponChanged,
	AWeaponRuntime*, NewWeapon);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class DUST2_API UWeaponInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponInventoryComponent();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory")
	bool InitializeNativeInventory(
		UChildActorComponent* Slot1,
		UChildActorComponent* Slot2,
		AWeaponRuntime* InitialCurrent);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory")
	bool PrepareForExternalRestore();

	UFUNCTION(BlueprintPure, Category = "Weapon|Inventory")
	AWeaponRuntime* GetCurrentWeaponNative() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Inventory")
	AWeaponRuntime* GetSlotWeaponNative(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Inventory")
	bool IsInventoryBusy() const;

	// Query only while handling the synchronous OnEquippedWeaponChanged event.
	UFUNCTION(BlueprintPure, Category = "Weapon|Inventory")
	bool ShouldRefreshEquippedPresentation() const;

	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory")
	bool TryPickupNative(AWeaponPickupActor* Source, bool bAllowReplaceCurrent);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory")
	bool TryEquipWeaponNative(AWeaponRuntime* NewWeapon);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory")
	bool TrySwitchWeaponNative();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory")
	bool TryDropCurrentNative();

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Inventory")
	FOnInventoryChanged OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Inventory")
	FOnBeforeWeaponChange OnBeforeWeaponChange;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Inventory")
	FOnEquippedWeaponChanged OnEquippedWeaponChanged;
	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory")
	bool SyncLegacyWeaponReferences();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool IsOperational() const;
	bool ReadSlotWeapons(
		AWeaponRuntime*& OutSlot1,
		AWeaponRuntime*& OutSlot2) const;
	UChildActorComponent* GetSlotComponent(int32 SlotIndex) const;
	void CancelCurrentWeaponActions(AWeaponRuntime* OldWeapon, bool bNotifyBlueprint);
	void SetWeaponActorActive(AWeaponRuntime* Weapon, bool bActive) const;
	void CommitCurrentWeapon(
		AWeaponRuntime* NewWeapon,
		bool bRefreshPresentation = true);
	void BroadcastInventoryChanged();
	bool CreateWeaponInSlot(
		UChildActorComponent* Slot,
		TSubclassOf<AWeaponRuntime> WeaponClass,
		const FWeaponPickupState& State,
		AWeaponRuntime*& OutWeapon);
	AWeaponPickupActor* SpawnStagedPickup(
		TSubclassOf<AWeaponRuntime> WeaponClass,
		const FWeaponPickupState& State) const;
	void LaunchStagedPickup(AWeaponPickupActor* Pickup) const;
	void ConsumePickupSource(AWeaponPickupActor* Source);
	void ReleasePickupClaim(AWeaponPickupActor* Source) const;
	bool TryReplaceCurrentFromPickup(
		AWeaponPickupActor* Source,
		TSubclassOf<AWeaponRuntime> IncomingClass,
		const FWeaponPickupState& IncomingState);

	UPROPERTY(Transient)
	TObjectPtr<UChildActorComponent> Slot1Component;

	UPROPERTY(Transient)
	TObjectPtr<UChildActorComponent> Slot2Component;

	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> CurrentWeapon;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Inventory|Drop")
	TMap<TSubclassOf<AWeaponRuntime>, TSubclassOf<AWeaponPickupActor>>
	PickupClassByWeaponClass;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Inventory|Drop",
		meta = (ClampMin = "0.0"))
	float DropForwardDistance = 200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Inventory|Drop",
		meta = (ClampMin = "0.0"))
	float DropUpOffset = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Inventory|Drop",
		meta = (ClampMin = "0.0"))
	float AutoPickupBlockSeconds = 0.75f;

	bool bInventoryBusy = false;
	bool bInitialized = false;
	bool bEquippedPresentationRefreshRequested = false;
};
