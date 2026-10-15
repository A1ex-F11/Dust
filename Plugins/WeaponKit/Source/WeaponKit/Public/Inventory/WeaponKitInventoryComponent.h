#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/WeaponKitTypes.h"
#include "UObject/ScriptInterface.h"
#include "Interfaces/WeaponKitInterfaces.h"
#include "Runtime/WeaponKitRuntime.h"
#include "Inventory/WeaponKitInventoryComponent.generated.h"

class UWeaponKitInteractionScanner;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponKitInventoryChanged, FWeaponKitInventorySnapshot, Snapshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponKitBeforeWeaponChange, AWeaponKitRuntime*, OldWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponKitEquippedChanged, AWeaponKitRuntime*, NewWeapon);

/**
 * Configurable weapon slots. Hosts implement IWeaponKitSlotHost.
 * Pickups implement IWeaponKitPickup. The optional scanner is queried only after a drop.
 */
UCLASS(ClassGroup = (WeaponKit), meta = (BlueprintSpawnableComponent))
class WEAPONKIT_API UWeaponKitInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponKitInventoryComponent();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool InitializeInventory(const TArray<TScriptInterface<IWeaponKitSlotHost>>& SlotHosts, int32 InitialSlotIndex);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool PrepareForExternalRestore();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool RestoreLoadout(const TArray<TScriptInterface<IWeaponKitSlotHost>>& SlotHosts, const FWeaponKitLoadoutSave& Save);

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Inventory")
	AWeaponKitRuntime* GetCurrentWeapon() const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Inventory")
	AWeaponKitRuntime* GetSlotWeapon(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Inventory")
	int32 GetSlotCount() const { return SlotHosts.Num(); }

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Inventory")
	int32 GetActiveSlotIndex() const { return ActiveSlotIndex; }

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Inventory")
	bool IsInventoryBusy() const { return bInventoryBusy; }

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Inventory")
	bool ShouldRefreshEquippedPresentation() const { return bEquippedPresentationRefreshRequested; }

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool TryPickup(AActor* Source, bool bAllowReplaceCurrent);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool TryEquipSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool TryEquipWeapon(AWeaponKitRuntime* NewWeapon);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool TrySwitchWeapon();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Inventory")
	bool TryDropCurrent();

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Inventory")
	FWeaponKitInventorySnapshot GetSnapshot() const;

	UPROPERTY(BlueprintAssignable, Category = "WeaponKit|Inventory")
	FOnWeaponKitInventoryChanged OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "WeaponKit|Inventory")
	FOnWeaponKitBeforeWeaponChange OnBeforeWeaponChange;

	UPROPERTY(BlueprintAssignable, Category = "WeaponKit|Inventory")
	FOnWeaponKitEquippedChanged OnEquippedWeaponChanged;

	/** Used only to find a replacement after a successful drop. Pickup selection itself stays on the scanner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Inventory")
	TObjectPtr<UWeaponKitInteractionScanner> InteractionScanner;

	UPROPERTY(EditDefaultsOnly, Category = "WeaponKit|Inventory|Drop")
	TMap<TSubclassOf<AWeaponKitRuntime>, TSubclassOf<AActor>> PickupClassByWeaponClass;

	UPROPERTY(EditDefaultsOnly, Category = "WeaponKit|Inventory|Drop", meta = (ClampMin = "0.0"))
	float DropForwardDistance = 200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "WeaponKit|Inventory|Drop", meta = (ClampMin = "0.0"))
	float DropUpOffset = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "WeaponKit|Inventory|Drop", meta = (ClampMin = "0.0"))
	float AutoPickupBlockSeconds = 0.75f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool IsOperational() const;
	bool ValidateHosts(const TArray<TScriptInterface<IWeaponKitSlotHost>>& Hosts) const;
	bool HostsMatch(const TArray<TScriptInterface<IWeaponKitSlotHost>>& Hosts) const;
	bool ReadSlotWeapons(TArray<AWeaponKitRuntime*>& OutWeapons) const;
	void CancelCurrentWeaponActions(AWeaponKitRuntime* OldWeapon, bool bNotify);
	void SetWeaponActorActive(AWeaponKitRuntime* Weapon, bool bActive) const;
	void CommitCurrentWeapon(AWeaponKitRuntime* NewWeapon, bool bRefreshPresentation = true);
	void BroadcastInventoryChanged();
	bool CreateWeaponInSlot(const TScriptInterface<IWeaponKitSlotHost>& Slot, TSubclassOf<AWeaponKitRuntime> WeaponClass, const FWeaponKitPickupState& State, AWeaponKitRuntime*& OutWeapon);
	AActor* SpawnStagedPickup(TSubclassOf<AWeaponKitRuntime> WeaponClass, const FWeaponKitPickupState& State) const;
	void LaunchStagedPickup(AActor* Pickup) const;
	void ConsumePickupSource(AActor* Source);
	void ReleasePickupClaim(AActor* Source) const;
	bool TryReplaceCurrentFromPickup(AActor* Source, TSubclassOf<AWeaponKitRuntime> IncomingClass, const FWeaponKitPickupState& IncomingState);
	int32 FindSlotIndex(const AWeaponKitRuntime* Weapon) const;
	int32 FindNextOccupiedSlot(int32 FromIndex) const;
	int32 CountOccupiedSlots() const;
	bool IsCarrierAlive() const;
	bool HasConsumedInitialDraw(AWeaponKitRuntime* Weapon) const;
	void ApplyInitialDrawConsumed(AWeaponKitRuntime* Weapon) const;
	static bool IsUsableWeaponClass(TSubclassOf<AWeaponKitRuntime> WeaponClass);
	static bool IsUsablePickupActorClass(TSubclassOf<AActor> PickupClass);

	UPROPERTY(Transient)
	TArray<TScriptInterface<IWeaponKitSlotHost>> SlotHosts;

	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponKitRuntime> CurrentWeapon;

	int32 ActiveSlotIndex = INDEX_NONE;
	bool bInventoryBusy = false;
	bool bInitialized = false;
	bool bEquippedPresentationRefreshRequested = false;
	bool bExternalRestorePresentationPending = false;
	bool bCheckpointLoadoutPendingInitialization = false;
	bool bCheckpointRestoreFailed = false;
};
