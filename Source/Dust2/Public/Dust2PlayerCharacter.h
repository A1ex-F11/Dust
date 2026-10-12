// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "WeaponRuntime.h"
#include "Dust2PlayerCharacter.generated.h"
class AWeaponRuntime;
class UWeaponInventoryComponent;
class UChildActorComponent;
class UCameraRecoilRecoveryComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnAimedWeaponPickupChanged, AActor*, AimedPickup);
UCLASS()
class DUST2_API ADust2PlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADust2PlayerCharacter();
	// Replace the three Blueprint inventory bindings and initialization call.
	// Blueprint presentation handlers must be implemented before calling this.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Inventory",
		meta = (DisplayName = "Initialize Player Inventory Bridge"))
	bool InitializePlayerInventoryBridge(
		UWeaponInventoryComponent* Inventory,
		UChildActorComponent* Slot1,
		UChildActorComponent* Slot2,
		AWeaponRuntime* InitialCurrent,
		UCameraRecoilRecoveryComponent* RecoilRecovery);

	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Inventory")
	void ReceiveBeforeInventoryWeaponChange(AWeaponRuntime* OldWeapon);

	// Refresh permission is captured inside the synchronous inventory event.
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Inventory")
	void ReceiveInventoryWeaponChanged(
		AWeaponRuntime* NewWeapon, bool bRefreshPresentation);
	UFUNCTION(BlueprintPure,Category="Health")
	float GetMaxHealth() const;
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const;
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const;
	UFUNCTION(BlueprintCallable,Category="Health")
	bool ApplyDamage(float Amount, bool& bDiedNow);
	UFUNCTION(BlueprintCallable,Category="Health")
	bool ApplyHeal(float Amount);
	UFUNCTION(BlueprintCallable,Category="Health")
	void RestoreHealth(float SavedHealth);
	UFUNCTION(BlueprintNativeEvent, BlueprintPure,
		Category = "Weapon|Reload")
	bool CanStartReloadRequest(AWeaponRuntime* FormalWeapon) const;
	virtual bool CanStartReloadRequest_Implementation(
		AWeaponRuntime* FormalWeapon) const;
	UFUNCTION(BlueprintCallable, Category = "Weapon|Pickup")
	void RegisterNearbyWeaponPickup(AActor* Pickup);
	UFUNCTION(BlueprintCallable, Category = "Weapon|Pickup")
	void UnregisterNearbyWeaponPickup(AActor* Pickup);
	UFUNCTION(BlueprintPure, Category = "Weapon|Pickup")
	AActor* GetNearbyWeaponPickup() const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Pickup")
	AActor* GetAimedWeaponPickup() const;
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Pickup")
	FOnAimedWeaponPickupChanged OnAimedWeaponPickupChanged;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weapon|Pickup")
	bool HasConsumedPickupInitialDraw(AWeaponRuntime* FormalWeapon) const;
	virtual bool HasConsumedPickupInitialDraw_Implementation(
		AWeaponRuntime* FormalWeapon) const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weapon|Pickup")
	void ApplyPickupInitialDrawConsumed(AWeaponRuntime* FormalWeapon);
	virtual void ApplyPickupInitialDrawConsumed_Implementation(
		AWeaponRuntime* FormalWeapon);

	UFUNCTION(BlueprintPure, Category = "Weapon|Pickup")
	AActor* GetNearbyWeaponPickupForAutoFill(AActor* ExcludedPickup) const;
protected:
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Health")
	float MaxHealth;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Health")
	float CurrentHealth;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Health")
	bool bDeathRequested;
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> NearbyWeaponPickups;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Pickup",
		meta = (ClampMin = "1.0"))
	float ManualPickupMaxDistance = 300.0f;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool HasValidNearbyWeaponPickup() const;
	void RefreshAimedWeaponPickup();

	FTimerHandle PickupFocusTimerHandle;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastAimedPickup;

	UPROPERTY(Transient)
	bool bHadAimedPickup = false;
	AActor* FindNearbyWeaponPickupNative(
	bool bAutomaticOnly, AActor* ExcludedPickup) const;

private:
	UFUNCTION()
	void HandleInventoryBridgeChanged(AWeaponRuntime* Slot1Weapon,
		AWeaponRuntime* Slot2Weapon, AWeaponRuntime* CurrentWeapon);
	UFUNCTION()
	void HandleInventoryBridgeBeforeChange(AWeaponRuntime* OldWeapon);
	UFUNCTION()
	void HandleInventoryBridgeEquipped(AWeaponRuntime* NewWeapon);
	void UnbindInventoryBridge();
	UPROPERTY(Transient)
	TObjectPtr<UWeaponInventoryComponent> BridgedInventory;
	UPROPERTY(Transient)
	TObjectPtr<UCameraRecoilRecoveryComponent> BridgedRecoilRecovery;
	bool bInventoryBridgeInitializing = false;
};
