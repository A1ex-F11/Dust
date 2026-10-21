// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "WeaponRuntime.h"
#include "DustMovementGait.h"
#include "Perception/AISightTargetInterface.h"
#include "Dust2PlayerCharacter.generated.h"
class AWeaponRuntime;
class UWeaponInventoryComponent;
class UChildActorComponent;
class UCameraRecoilRecoveryComponent;
class UWeaponPresentationComponent;
class UDustMovementAudioComponent;
class UDustRagdollDeathComponent;
class UDustDeathCameraComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnAimedWeaponPickupChanged, AActor*, AimedPickup);


UCLASS(meta=(DisplayName="玩家角色"))
class DUST2_API ADust2PlayerCharacter : public ACharacter, public IAISightTargetInterface
{
	GENERATED_BODY()

public:
	ADust2PlayerCharacter();
	virtual UAISense_Sight::EVisibilityResult CanBeSeenFrom(const FCanBeSeenFromContext& Context,
		FVector& OutSeenLocation, int32& OutNumberOfLoSChecksPerformed,
		int32& OutNumberOfAsyncLosCheckRequested, float& OutSightStrength,
		int32* UserData = nullptr, const FOnPendingVisibilityQueryProcessedDelegate* Delegate = nullptr) override;
	virtual void OnJumped_Implementation() override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "移动|声音")
	TObjectPtr<UDustMovementAudioComponent> MovementAudio;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="死亡")
	TObjectPtr<UDustRagdollDeathComponent> DeathPresentation;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="死亡")
	TObjectPtr<UDustDeathCameraComponent> DeathCamera;
	// Explicit opt-in after the Blueprint camera/HUD/animation setup is ready.
	UFUNCTION(BlueprintCallable, Category = "武器|瞄准")
	bool InitializeAimState(UWeaponPresentationComponent* Presentation);
	// Call on Started / Completed / Canceled, not every Triggered frame.
	UFUNCTION(BlueprintCallable, Category = "武器|瞄准")
	bool SetAimInputPressed(bool bPressed, bool bPresentationSwitching);
	// Leaves the held-input latch set: the player must release before re-aiming.
	UFUNCTION(BlueprintCallable, Category = "武器|瞄准")
	void CancelAim();
	UFUNCTION(BlueprintPure, Category = "武器|瞄准")
	bool IsAiming() const { return bNativeAiming; }
	UFUNCTION(BlueprintPure, Category = "武器|瞄准")
	bool IsAimInputHeld() const { return bAimInputHeld; }
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|瞄准")
	void ReceiveAimStateChanged(bool bAiming);
	UFUNCTION(BlueprintPure, Category = "武器|瞄准")
	bool HasPendingSprintAim() const { return bPendingSprintAim; }

	// Call on Started / Completed / Canceled, not every Triggered frame.
	UFUNCTION(BlueprintCallable, Category = "移动|步态")
	void SetSprintHeld(bool bHeld);
	UFUNCTION(BlueprintCallable, Category = "移动|步态")
	void SetSlowWalkHeld(bool bHeld);
	UFUNCTION(BlueprintCallable, Category = "移动|步态")
	void ClearGaitIntent();
	UFUNCTION(BlueprintPure, Category = "移动|步态")
	EDustMovementGait GetCurrentGait() const { return CurrentGait; }
	UFUNCTION(BlueprintPure, Category = "移动|步态")
	bool IsSlowWalking() const { return CurrentGait == EDustMovementGait::SlowWalk; }
	UFUNCTION(BlueprintImplementableEvent, Category = "移动|步态")
	void ReceiveGaitChanged(EDustMovementGait NewGait);
	// The animation instance reports the evaluated sprint pose weight.
	// True is accepted only after the logical gait has left Run.
	UFUNCTION(BlueprintCallable, Category = "移动|步态")
	void ReportSprintExitPresentationReady(bool bReady);
	// Use as the common gate before every direct or automatic fire entry.
	UFUNCTION(BlueprintPure, Category = "移动|步态")
	bool CanProcessSprintAction() const;

	// W07 G10: fire while sprinting. Call on IA Fire Started only.
	// Returns true when the shared sprint-pose exit gate is open; the caller
	// may run its normal fire chain. Otherwise a valid press is cached until
	// the animation reports zero sprint weight, or rejected.
	UFUNCTION(BlueprintCallable, Category = "武器|射击")
	bool NotifyFirePressed();
	// Call on IA Fire Completed / Canceled. Rule A: drops any cached shot.
	UFUNCTION(BlueprintCallable, Category = "武器|射击")
	void NotifyFireReleased();
	UFUNCTION(BlueprintCallable, Category = "武器|射击")
	void ClearPendingSprintFire();
	UFUNCTION(BlueprintPure, Category = "武器|射击")
	bool HasPendingSprintFire() const { return bPendingSprintFire; }
	UFUNCTION(BlueprintPure, Category = "移动|步态")
	bool IsSprintBlockedForFire() const { return bPendingSprintFire || bFireInputHeld; }
	// Fired once per cached press, after the animation exit gate and weapon are ready.
	// Wire it to the same chain IA Fire Started uses when NotifyFirePressed is true.
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|射击")
	void ReceivePendingSprintFireReady(AWeaponRuntime* Weapon);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|射击",
		meta = (ClampMin = "0.0", ToolTip = "Optional minimum wait measured from the queued press. The explicit sprint-pose exit report and weapon readiness are also required.", DisplayName="冲刺退出后开火等待时间"))
	float SprintExitFireDelay = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "移动|步态",
		meta = (ClampMin = "-1.0", ClampMax = "1.0", ToolTip = "Run requires dot(move input, actor forward) >= this. 0.7 is about 45 degrees either side of forward. Below it, Shift stays held but the gait is Walk.", DisplayName="奔跑前向输入阈值"))
	float SprintMinForwardInput = 0.7f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|瞄准",
		meta = (DisplayName = "换弹时保持瞄准",
			ToolTip = "已在瞄准时开始换弹，保留瞄准视野和准星状态；松开右键仍退出。"))
	bool bKeepAimDuringReload = true;
	// Replace the three Blueprint inventory bindings and initialization call.
	// Blueprint presentation handlers must be implemented before calling this.
	UFUNCTION(BlueprintCallable, Category = "武器|背包",
		meta = (DisplayName = "初始化玩家背包接口"))
	bool InitializePlayerInventoryBridge(
		UWeaponInventoryComponent* Inventory,
		UChildActorComponent* Slot1,
		UChildActorComponent* Slot2,
		AWeaponRuntime* InitialCurrent,
		UCameraRecoilRecoveryComponent* RecoilRecovery);

	UFUNCTION(BlueprintImplementableEvent, Category = "武器|背包")
	void ReceiveBeforeInventoryWeaponChange(AWeaponRuntime* OldWeapon);

	// Refresh permission is captured inside the synchronous inventory event.
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|背包")
	void ReceiveInventoryWeaponChanged(
		AWeaponRuntime* NewWeapon, bool bRefreshPresentation);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "生命值",
		meta = (DisplayName = "无敌", ToolTip = "开启后受到伤害不扣血，方便观察和调试。不会恢复已经损失的生命。"))
	bool bInvulnerable = false;

	UFUNCTION(BlueprintPure,Category="生命值")
	float GetMaxHealth() const;
	UFUNCTION(BlueprintPure, Category = "生命值")
	float GetCurrentHealth() const;
	UFUNCTION(BlueprintPure, Category = "生命值")
	bool IsDead() const;
	UFUNCTION(BlueprintCallable,Category="生命值")
	bool ApplyDamage(float Amount, bool& bDiedNow);
	UFUNCTION(BlueprintCallable,Category="生命值")
	bool ApplyHeal(float Amount);
	UFUNCTION(BlueprintCallable,Category="生命值")
	void RestoreHealth(float SavedHealth);
	UFUNCTION(BlueprintNativeEvent, BlueprintPure,
		Category = "武器|换弹")
	bool CanStartReloadRequest(AWeaponRuntime* FormalWeapon) const;
	virtual bool CanStartReloadRequest_Implementation(
		AWeaponRuntime* FormalWeapon) const;
	UFUNCTION(BlueprintCallable, Category = "武器|拾取")
	void RegisterNearbyWeaponPickup(AActor* Pickup);
	UFUNCTION(BlueprintCallable, Category = "武器|拾取")
	void UnregisterNearbyWeaponPickup(AActor* Pickup);
	UFUNCTION(BlueprintPure, Category = "武器|拾取")
	AActor* GetNearbyWeaponPickup() const;
	UFUNCTION(BlueprintPure, Category = "武器|拾取")
	AActor* GetAimedWeaponPickup() const;
	UPROPERTY(BlueprintAssignable, Category = "武器|拾取")
	FOnAimedWeaponPickupChanged OnAimedWeaponPickupChanged;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "武器|拾取")
	bool HasConsumedPickupInitialDraw(AWeaponRuntime* FormalWeapon) const;
	virtual bool HasConsumedPickupInitialDraw_Implementation(
		AWeaponRuntime* FormalWeapon) const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "武器|拾取")
	void ApplyPickupInitialDrawConsumed(AWeaponRuntime* FormalWeapon);
	virtual void ApplyPickupInitialDrawConsumed_Implementation(
		AWeaponRuntime* FormalWeapon);
	UFUNCTION(BlueprintCallable, Category = "武器|拾取")
	bool TryBuildDropSnapshotNative(
		AWeaponRuntime* CurrentWeapon,
		AWeaponRuntime* Slot1Weapon,
		AWeaponRuntime* Slot2Weapon,
		FWeaponPickupState& OutState,
		bool& bOutOldInSlot1);
	UFUNCTION(BlueprintPure, Category = "武器|拾取")
	AActor* GetNearbyWeaponPickupForAutoFill(AActor* ExcludedPickup) const;
protected:
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "生命值")
	float MaxHealth;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "生命值")
	float CurrentHealth;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "生命值")
	bool bDeathRequested;
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> NearbyWeaponPickups;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|拾取",
		meta = (ClampMin = "1.0", DisplayName="手动拾取最大距离"))
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
