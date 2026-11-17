// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Weapons/WeaponRuntime.h"
#include "Combat/WeaponShotResolution.h"
#include "Player/DustMovementGait.h"
#include "Perception/AISightTargetInterface.h"
#include "Dust2PlayerCharacter.generated.h"
class AWeaponRuntime;
class UWeaponInventoryComponent;
struct FDustNativeRouteCheckpoint;
class UChildActorComponent;
class UCameraRecoilRecoveryComponent;
class UCameraComponent;
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

	// ---- Native player action scheduler (explicit opt-in) ----------------------
	// Call once after the inventory bridge, recoil component, presentation and shooting
	// camera exist. Invalid prerequisites leave the old Blueprint chain untouched.
	// Repeated calls with the same components are idempotent; swapping components keeps
	// the inventory bridge and never duplicates delegate bindings.
	UFUNCTION(BlueprintCallable, Category = "武器|动作",
		meta = (DisplayName = "Initialize Player Actions Native"))
	bool InitializePlayerActionsNative(
		UWeaponPresentationComponent* Presentation,
		UCameraComponent* ShootingCamera);

	UFUNCTION(BlueprintCallable, Category = "武器|射击",
		meta = (DisplayName = "Native Player Fire Pressed"))
	bool NativePlayerFirePressed();
	UFUNCTION(BlueprintCallable, Category = "武器|射击",
		meta = (DisplayName = "Native Player Fire Triggered"))
	void NativePlayerFireTriggered();
	UFUNCTION(BlueprintCallable, Category = "武器|射击",
		meta = (DisplayName = "Native Player Fire Released"))
	void NativePlayerFireReleased();
	UFUNCTION(BlueprintCallable, Category = "武器|射击",
		meta = (DisplayName = "Native Player Cycle Fire Mode"))
	void NativePlayerCycleFireMode();
	// Success path only: play impact feedback, the existing tracer chain and HUD refresh.
	// Never re-commit ammo, recoil or the shot.
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|射击")
	void ReceivePlayerShotResolvedNative(
		AWeaponRuntime* Weapon, const FWeaponShotResolution& ShotResult);
	// Only the first allowed empty request of one press.
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|射击")
	void ReceivePlayerDryFireNative(AWeaponRuntime* Weapon);
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|射击")
	void ReceivePlayerFireReleasedNative(AWeaponRuntime* Weapon);
	// Presentation only: held after a committed shot, cleared by release or interruption.
	UFUNCTION(BlueprintPure, Category = "武器|动画")
	bool IsFireAnimationActive() const { return bFireAnimationActive; }
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|动画")
	void ReceiveFireAnimationStateChanged(bool bFiring);
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|射击")
	void ReceivePlayerFireModeChangedNative(
		AWeaponRuntime* Weapon, EFireMode PreviousMode);

	// A rejected start changes nothing: the old still-available action is preserved.
	UFUNCTION(BlueprintCallable, Category = "武器|换弹",
		meta = (DisplayName = "Native Player Start Reload"))
	bool NativePlayerStartReload(float FallbackCompletionSeconds = 2.0f);
	// Call right after the Blueprint sets bReloadCompletionFromMontage True/False.
	UFUNCTION(BlueprintCallable, Category = "武器|换弹",
		meta = (DisplayName = "Native Player Set Reload Animation Driven"))
	bool NativePlayerSetReloadAnimationDriven(
		AWeaponRuntime* FormalWeapon, int32 ReloadRequestId, bool bAnimationDriven);
	UFUNCTION(BlueprintCallable, Category = "武器|换弹",
		meta = (DisplayName = "Native Player Complete Reload"))
	bool NativePlayerCompleteReload(
		AWeaponRuntime* FormalWeapon, int32 ReloadRequestId);
	UFUNCTION(BlueprintCallable, Category = "武器|换弹",
		meta = (DisplayName = "Native Player Cancel Reload"))
	void NativePlayerCancelReload();
	UFUNCTION(BlueprintImplementableEvent, Category = "武器|换弹")
	void ReceivePlayerAmmoChangedNative(AWeaponRuntime* Weapon);

	// Switch/drop, first death, restore, EndPlay and optional manual cleanup.
	// Preserves the existing aim and inventory handling.
	UFUNCTION(BlueprintCallable, Category = "武器|动作",
		meta = (DisplayName = "Native Player Cancel Actions"))
	void NativePlayerCancelActions();

#if WITH_DEV_AUTOMATION_TESTS
	// Test-only observations, never stored or exposed to Blueprint.
	bool IsPlayerActionsNativeEnabled() const { return bPlayerActionsNativeEnabled; }
	bool IsBurstGroupActiveNative() const { return BurstWeapon.IsValid() && BurstCommittedShots > 0; }
	int32 GetBurstCommittedShotsNative() const { return BurstCommittedShots; }
	bool IsReloadFallbackTimerActiveNative() const { return bReloadFallbackTimerActive; }
	bool IsReloadAnimationDrivenNative() const { return bReloadAnimationDriven; }
	int32 GetActiveReloadRequestIdNative() const { return ActiveReloadRequestId; }
	int32 GetNativeCommittedShotCount() const { return NativeCommittedShotCount; }
	int32 GetNativeDryFireCount() const { return NativeDryFireCount; }
	int32 GetNativeShotResolvedNotifiedCount() const { return NativeShotResolvedNotifiedCount; }
	int32 GetNativeAmmoChangedCount() const { return NativeAmmoChangedCount; }
	int32 GetNativeFireModeChangedCount() const { return NativeFireModeChangedCount; }
#endif

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
		meta = (DisplayName = "Initialize Player Inventory Bridge"))
	bool InitializePlayerInventoryBridge(
		UWeaponInventoryComponent* Inventory,
		UChildActorComponent* Slot1,
		UChildActorComponent* Slot2,
		AWeaponRuntime* InitialCurrent,
		UCameraRecoilRecoveryComponent* RecoilRecovery);

	// True means this opening belongs to a native retry and should skip the startup menu.
	UFUNCTION(BlueprintCallable, Category = "任务路线")
	bool TryQueueNativeMissionCheckpointResume();

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
	bool CaptureNativeRouteLoadout(FDustNativeRouteCheckpoint& Checkpoint) const;
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
	AActor* FindNearbyWeaponPickupNative(bool bAutomaticOnly, AActor* ExcludedPickup,
		bool bAllowNearestFallback = true) const;

private:
	void ResumeNativeMissionCheckpoint();
	FTimerHandle NativeMissionCheckpointResumeTimer;

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

	// ---- Native player action scheduler internals ------------------------------
	enum class ENativePlayerFireOutcome : uint8
	{
		NotReady,
		Committed,
		Empty,
		Cooldown,
		Reloading,
		Busy
	};

	bool EnsurePlayerActionsPresentationBound(
		UWeaponPresentationComponent* Presentation);
	void UnbindPlayerActionsPresentation();
	void ObservePlayerActionsWeapon(AWeaponRuntime* Weapon);
	void UnbindObservedPlayerActionsWeapon();
	void ShutdownPlayerActionsNative();

	bool IsNativePlayerFireReady(AWeaponRuntime* Weapon,
		UWeaponPresentationComponent*& OutPresentation) const;
	ENativePlayerFireOutcome DispatchNativePlayerShot(AWeaponRuntime* ExpectedWeapon, bool bAllowDryFire = false);
	void NotifyNativeDryFire(AWeaponRuntime* Weapon);
	void StartNativePressFire(AWeaponRuntime* Weapon);
	void TryBufferSemiAutoShot(AWeaponRuntime* Weapon,
		uint64 ActionsSerialAtAttempt);
	void HandleSemiAutoBufferElapsed(uint64 Generation,
		TWeakObjectPtr<AWeaponRuntime> WeaponWeak);
	void ClearPendingSemiAutoBuffer();
	void StartBurstGroupNative(AWeaponRuntime* Weapon);
	void ScheduleNextBurstShotNative();
	void HandleBurstShotTimer();
	void StopBurstGroupNative(bool bApplyGroupRecovery);
	void PruneBurstRecoveryNative();

	bool StartReloadFallbackTimer(AWeaponRuntime* Weapon, int32 RequestId,
		float Seconds);
	void ClearReloadFallbackTimer();
	void HandleReloadFallbackElapsed(uint64 Serial,
		TWeakObjectPtr<AWeaponRuntime> Weapon, int32 RequestId);
	void ClearReloadContextNative();
	bool CompleteNativeReloadInternal(AWeaponRuntime* Weapon, int32 RequestId,
		bool bFromAnimation);

	UFUNCTION()
	void HandlePlayerActionsWeaponReloadStarted();
	UFUNCTION()
	void HandlePresentationReloadCompleted(
		AWeaponRuntime* FormalWeapon, int32 ReloadRequestId);
	UFUNCTION()
	void HandlePresentationReloadRoundCommitted(
		AWeaponRuntime* FormalWeapon, int32 ReloadRequestId);

	UPROPERTY(Transient)
	TWeakObjectPtr<UWeaponPresentationComponent> NativeActionsPresentation;
	UPROPERTY(Transient)
	TWeakObjectPtr<UCameraComponent> NativeActionsCamera;
	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> ObservedPlayerActionsWeapon;
	// A burst group's recovery belongs to the weapon that fired it, so switching away and
	// back can neither transfer the wait nor clear it to bypass recovery.
	TMap<TWeakObjectPtr<AWeaponRuntime>, float> BurstRecoveryDeadlines;

	void SetFireAnimationActive(bool bActive);
	bool bFireAnimationActive = false;
	bool bPlayerActionsNativeEnabled = false;
	uint64 NativeActionsSerial = 0;
	bool bFireDispatchGuard = false;
	bool bCycleFireModeGuard = false;
	bool bPlayerActionsCancelling = false;
	bool bReloadCancelGuard = false;
	bool bReloadEntryGuard = false;
	bool bSuppressFirstAutoTrigger = false;
	bool bDryFireNotifiedThisPress = false;
	bool bSemiAutoAttemptedThisPress = false;

	// One transient semi-automatic press buffer. It is never written into a pickup or
	// checkpoint snapshot; the runtime stays the only owner of ammo/recoil/ballistics.
	// A separate generation makes a stale timer callback inert.
	bool bPendingSemiAutoBuffer = false;
	TWeakObjectPtr<AWeaponRuntime> PendingSemiAutoBufferWeapon;
	uint64 PendingSemiAutoBufferGeneration = 0;
	uint64 PendingSemiAutoBufferActionsSerial = 0;
	float PendingSemiAutoBufferDeadline = 0.0f;
	FTimerHandle PendingSemiAutoBufferTimerHandle;

	TWeakObjectPtr<AWeaponRuntime> BurstWeapon;
	int32 BurstCommittedShots = 0;
	FTimerHandle BurstTimerHandle;

	TWeakObjectPtr<AWeaponRuntime> ReloadWeapon;
	int32 ActiveReloadRequestId = 0;
	uint64 ReloadSerial = 0;
	bool bReloadStarting = false;
	bool bReloadAnimationDriven = false;
	bool bReloadFallbackTimerActive = false;
	float PendingReloadFallbackSeconds = 2.0f;
	uint64 ReloadFallbackTimerSerial = 0;
	FTimerHandle ReloadFallbackTimerHandle;

#if WITH_DEV_AUTOMATION_TESTS
	int32 NativeCommittedShotCount = 0;
	int32 NativeDryFireCount = 0;
	int32 NativeShotResolvedNotifiedCount = 0;
	int32 NativeAmmoChangedCount = 0;
	int32 NativeFireModeChangedCount = 0;
	friend struct FDustPlayerActionsTestAccess;
#endif
};
