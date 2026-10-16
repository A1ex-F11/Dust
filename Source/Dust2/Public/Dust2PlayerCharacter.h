// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "WeaponRuntime.h"
#include "DustMovementGait.h"
#include "Dust2PlayerCharacter.generated.h"
class AWeaponRuntime;
class UWeaponInventoryComponent;
class UChildActorComponent;
class UCameraRecoilRecoveryComponent;
class UWeaponPresentationComponent;
class UDustMovementAudioComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnAimedWeaponPickupChanged, AActor*, AimedPickup);


UCLASS()
class DUST2_API ADust2PlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADust2PlayerCharacter();
	virtual void OnJumped_Implementation() override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Audio")
	TObjectPtr<UDustMovementAudioComponent> MovementAudio;
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
	UFUNCTION(BlueprintPure, Category = "Weapon|Aim")
	bool HasPendingSprintAim() const { return bPendingSprintAim; }

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
	// The animation instance reports the evaluated sprint pose weight.
	// True is accepted only after the logical gait has left Run.
	UFUNCTION(BlueprintCallable, Category = "Movement|Gait")
	void ReportSprintExitPresentationReady(bool bReady);
	// Use as the common gate before every direct or automatic fire entry.
	UFUNCTION(BlueprintPure, Category = "Movement|Gait")
	bool CanProcessSprintAction() const;

	// W07 G10: fire while sprinting. Call on IA Fire Started only.
	// Returns true when the shared sprint-pose exit gate is open; the caller
	// may run its normal fire chain. Otherwise a valid press is cached until
	// the animation reports zero sprint weight, or rejected.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	bool NotifyFirePressed();
	// Call on IA Fire Completed / Canceled. Rule A: drops any cached shot.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void NotifyFireReleased();
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void ClearPendingSprintFire();
	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	bool HasPendingSprintFire() const { return bPendingSprintFire; }
	UFUNCTION(BlueprintPure, Category = "Movement|Gait")
	bool IsSprintBlockedForFire() const { return bPendingSprintFire || bFireInputHeld; }
	// Fired once per cached press, after the animation exit gate and weapon are ready.
	// Wire it to the same chain IA Fire Started uses when NotifyFirePressed is true.
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Fire")
	void ReceivePendingSprintFireReady(AWeaponRuntime* Weapon);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire",
		meta = (ClampMin = "0.0", ToolTip = "Optional minimum wait measured from the queued press. The explicit sprint-pose exit report and weapon readiness are also required."))
	float SprintExitFireDelay = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Gait",
		meta = (ClampMin = "-1.0", ClampMax = "1.0", ToolTip = "Run requires dot(move input, actor forward) >= this. 0.7 is about 45 degrees either side of forward. Below it, Shift stays held but the gait is Walk."))
	float SprintMinForwardInput = 0.7f;
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
	enum class EDustGaitModifier : uint8
	{
		None,
		Sprint,
		SlowWalk
	};

	void ResolveGaitIntent();
	bool bSprintHeld = false;
	bool bSlowWalkHeld = false;
	// When both modifiers are held, the most recently pressed one wins.
	EDustGaitModifier LastPressedGaitModifier = EDustGaitModifier::None;
	EDustMovementGait CurrentGait = EDustMovementGait::Walk;
	void ApplyGait(EDustMovementGait NewGait);
	bool IsSprintDirectionAllowed() const;
	void UpdateSprintDirectionWatch();
	FTimerHandle SprintDirectionTimerHandle;

	void TryFlushPendingSprintFire();
	void TryFlushPendingSprintAim();
	void ClearPendingSprintAim();
	void FlushPendingSprintActions();
	void SchedulePendingSprintActionFlush();
	bool bSprintExitPresentationReady = true;
	FTimerHandle PendingSprintActionFlushTimerHandle;
	bool bPendingSprintAim = false;
	TWeakObjectPtr<AWeaponRuntime> PendingSprintAimWeapon;
	bool bFireInputHeld = false;
	bool bPendingSprintFire = false;
	TWeakObjectPtr<AWeaponRuntime> PendingSprintFireWeapon;
	float PendingSprintFireReadyTime = 0.0f;
	FTimerHandle PendingSprintFireTimerHandle;

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
