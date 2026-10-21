#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustEnemyReloadComponent.generated.h"

class AActor;
class AWeaponRuntime;
class UAnimInstance;
class UAnimMontage;
class UDustEnemyWeaponComponent;
class USkeletalMeshComponent;
class UWeaponDefinitionData;

/** One task entry owns one token. Stale reads/exits never affect a newer request. */
enum class EDustEnemyReloadStatus : uint8 { Running, Succeeded, Failed };

/**
 * Per-Pawn empty-magazine in-place reload coordinator. It never starts a behavior by itself:
 * StateTree tasks call TryStartReload/Update/Finish/Cancel.
 * The weapon runtime stays the only ammo authority; this component only relays one
 * reload transaction and verifies its identity (runtime + request id + token).
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent, DisplayName="敌人换弹"))
class DUST2_API UDustEnemyReloadComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDustEnemyReloadComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|换弹", meta=(DisplayName="失败或取消冷却（秒）", ClampMin="0.01"))
	float FailureCooldownSeconds = 2.0f;

	/** Enemy-only transaction policy: an empty magazine can be refilled to full without
	 * consuming reserve. It never edits the shared WeaponDefinition or player rules. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|换弹",
		meta=(DisplayName="敌人无限备弹（有限弹匣/空仓完整换弹）",
			ToolTip="默认开启：敌人空仓后可完整换满当前弹匣且不扣备弹；仍要求已空仓、Definition 为普通 Magazine 或空仓批量、战斗生命有效。关闭后恢复有限备弹旧规则。"))
	bool bUseInfiniteReserveAmmo = true;

	/** Used only when neither montage is configured; this is a logic-only verification mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|换弹", meta=(DisplayName="无Montage时逻辑换弹时长（秒）", ClampMin="0"))
	float LogicalReloadSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|换弹", meta=(DisplayName="换弹动画播放速率", ClampMin="0.01"))
	float ReloadAnimationPlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|换弹|动画", meta=(DisplayName="角色换弹Montage（可选）"))
	TObjectPtr<UAnimMontage> CharacterReloadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|换弹|动画", meta=(DisplayName="枪身换弹Montage（可选）"))
	TObjectPtr<UAnimMontage> WeaponReloadMontage;

	/** Call after InitializeEnemyVisualWeapon. Only this enemy's meshes (or its attached visual weapon) are accepted. */
	UFUNCTION(BlueprintCallable, Category="AI|换弹|动画")
	void SetReloadAnimationSources(USkeletalMeshComponent* CharacterMesh, USkeletalMeshComponent* WeaponMesh);

	/** Weapon/runtime/definition valid, magazine empty, supported style, adapter life current;
	 *  finite mode also needs reserve > 0, infinite mode captures a full-magazine refill. */
	UFUNCTION(BlueprintPure, Category="AI|换弹")
	bool CanReload() const;

	/** CanReload plus the failure/cancel cooldown. Deliberately does NOT include IsBusy(). */
	UFUNCTION(BlueprintPure, Category="AI|换弹")
	bool IsReloadReady() const;

	/** A reload transaction started by a task is currently owned by this component. */
	UFUNCTION(BlueprintPure, Category="AI|换弹")
	bool IsBusy() const { return ActiveReloadToken != 0; }

	UFUNCTION(BlueprintPure, Category="AI|换弹")
	bool IsReloading() const { return ActiveReloadToken != 0; }

	UDustEnemyWeaponComponent* GetWeaponComponent() const;
	AWeaponRuntime* GetWeaponRuntime() const;

	// ---- Reload transaction (one task entry = one token) ----
	uint64 TryStartReload();
	EDustEnemyReloadStatus UpdateReload(uint64 Token, float DeltaTime);
	/** Commits ammo once through WeaponRuntime::FinishReload when the identity still matches. */
	bool FinishReload(uint64 Token);
	/** Cancels only this token's transaction; a newer runtime request is never cancelled. */
	void CancelReload(uint64 Token);
	/** Hard stop used by weapon/adapter/EndPlay fallbacks: cancels this Pawn's owned reload. */
	void StopAllOperations();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|换弹|调试")
	FString LastReloadDiagnostic;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	friend struct FDustEnemyReloadTestAccess;

	// Visual sources (runtime only; never a player mesh).
	TWeakObjectPtr<USkeletalMeshComponent> CharacterMesh;
	TWeakObjectPtr<USkeletalMeshComponent> WeaponMesh;

	// Reload ownership.
	TWeakObjectPtr<UDustEnemyWeaponComponent> ActiveWeaponComponent;
	TWeakObjectPtr<AWeaponRuntime> ActiveRuntime;
	TWeakObjectPtr<UAnimInstance> CharacterAnimInstance;
	TWeakObjectPtr<UAnimInstance> WeaponAnimInstance;
	int32 CharacterMontageInstanceId = INDEX_NONE;
	int32 WeaponMontageInstanceId = INDEX_NONE;
	uint64 ReloadGeneration = 0;
	uint64 ActiveReloadToken = 0;
	int32 ActiveReloadRequestId = 0;
	// Per-request capture: playback/callback/watchdog never re-read live component/DataAsset config.
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveCharacterMontage;
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveWeaponMontage;
	TWeakObjectPtr<UWeaponDefinitionData> ActiveReloadDefinition;
	UPROPERTY(Transient) int32 ActiveReloadAmount = 0;
	UPROPERTY(Transient) float ActiveReloadPlayRate = 1.0f;
	UPROPERTY(Transient) bool bActiveReloadUsesEmptyBatch = false;
	// Captured at start: this request's source/amount cannot be changed by later config edits.
	UPROPERTY(Transient) bool bActiveReloadUsesInfiniteSource = false;
	bool bMontageMode = false;
	bool bHasCharacterMontage = false;
	bool bHasWeaponMontage = false;
	bool bCharacterMontageDone = true;
	bool bWeaponMontageDone = true;
	bool bWaitingForMontageCommit = false;
	bool bMontageInterrupted = false;
	float ReloadElapsedSeconds = 0.0f;
	float WatchdogSeconds = 0.0f;
	float LogicalRemainingSeconds = 0.0f;
	float NextReloadAllowedAt = 0.0f;

	bool IsCombatLifeValid() const;
	/** Same token + same runtime/weapon/definition + predicted request id + captured source
	 * mode + still reloading + life current. */
	bool IsReloadIdentityCurrent(uint64 Token) const;
	static int32 PredictNextReloadRequestId(const AWeaponRuntime* Runtime);
	/** Full refill for the infinite-source transaction; finite mode keeps min(missing, reserve). */
	int32 ComputeReloadFillAmount(const AWeaponRuntime& Runtime) const;
	int32 ComputeEmptyBatchReloadAmount(const AWeaponRuntime& Runtime) const;
	bool PlayReloadMontage(uint64 Token, UAnimInstance* AnimInstance, UAnimMontage* Montage, bool bCharacterPath);
	void StopOwnedMontages();
	void ClearReloadOperationState();
	/** Cancels only the captured runtime request when it is still the same; never a newer one. */
	void AbandonReloadOperation();
	void ApplyFailureCooldown();
	float ComputeWatchdogSeconds() const;
	void HandleCharacterMontageEnded(UAnimMontage* Montage, bool bInterrupted, uint64 Token);
	void HandleWeaponMontageEnded(UAnimMontage* Montage, bool bInterrupted, uint64 Token);
	void OnMontagePathEnded(uint64 Token, UAnimMontage* Montage, bool bInterrupted, bool bCharacterPath);
};
