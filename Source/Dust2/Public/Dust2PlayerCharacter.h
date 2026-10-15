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
class UWeaponPresentationComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnAimedWeaponPickupChanged, AActor*, AimedPickup);

UENUM(BlueprintType)
enum class EDustMovementGait : uint8
{
	Walk UMETA(DisplayName = "Walk"),
	Run UMETA(DisplayName = "Run"),
	SlowWalk UMETA(DisplayName = "SlowWalk")
};

UCLASS()
class DUST2_API ADust2PlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADust2PlayerCharacter();
	// Explicit opt-in after the Blueprint camera/HUD/animation setup is ready.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Aim")
	bool InitializeAimState(UWeaponPresentationComponent* Presentation);
	// Call on Started / Completed / Canceled, not every Triggered frame.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Aim")
	bool SetAimInputPressed(bool bPressed, bool bPresentationSwitching);
	// Leaves the held-input latch set: the player must release before re-aiming.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Aim")
	void CancelAim();
	UFUNCTION(BlueprintPure, Category = "Weapon|Aim")
	bool IsAiming() const { return bNativeAiming; }
	UFUNCTION(BlueprintPure, Category = "Weapon|Aim")
	bool IsAimInputHeld() const { return bAimInputHeld; }
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Aim")
	void ReceiveAimStateChanged(bool bAiming);

	// Call on Started / Completed / Canceled, not every Triggered frame.
	UFUNCTION(BlueprintCallable, Category = "Movement|Gait")
	void SetSprintHeld(bool bHeld);
	UFUNCTION(BlueprintCallable, Category = "Movement|Gait")
	void SetSlowWalkHeld(bool bHeld);
	UFUNCTION(BlueprintCallable, Category = "Movement|Gait")
	void ClearGaitIntent();
	UFUNCTION(BlueprintPure, Category = "Movement|Gait")
	EDustMovementGait GetCurrentGait() const { return CurrentGait; }
	UFUNCTION(BlueprintPure, Category = "Movement|Gait")
	bool IsSlowWalking() const { return CurrentGait == EDustMovementGait::SlowWalk; }
	UFUNCTION(BlueprintImplementableEvent, Category = "Movement|Gait")
	void ReceiveGaitChanged(EDustMovementGait NewGait);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim",
		meta = (DisplayName = "换弹时保持瞄准",
			ToolTip = "已在瞄准时开始换弹，保留瞄准视野和准星状态；松开右键仍退出。"))
	bool bKeepAimDuringReload = true;
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
	UFUNCTION(BlueprintCallable, Category = "Weapon|Pickup")
	bool TryBuildDropSnapshotNative(
		AWeaponRuntime* CurrentWeapon,
		AWeaponRuntime* Slot1Weapon,
		AWeaponRuntime* Slot2Weapon,
		FWeaponPickupState& OutState,
		bool& bOutOldInSlot1);
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
	void ResolveGaitIntent();
	bool bSprintHeld = false;
	bool bSlowWalkHeld = false;
	EDustMovementGait CurrentGait = EDustMovementGait::Walk;

	void SetNativeAimState(bool bAiming);
	void ObserveAimWeapon(AWeaponRuntime* Weapon);
	UFUNCTION()
	void HandleAimReloadStarted();
	UFUNCTION()
	void HandleAimWeaponDestroyed(AActor* DestroyedActor);
	UPROPERTY(Transient)
	TWeakObjectPtr<UWeaponPresentationComponent> AimPresentation;
	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> ObservedAimWeapon;
	bool bNativeAiming = false;
	bool bAimInputHeld = false;
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
