
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include"FireMode.h"
#include"WeaponRecoilData.h"
#include "WeaponDefinitionData.h"
#include "WeaponShotResolution.h"
#include "WeaponRuntime.generated.h"

class UWeaponDefinitionData;

// W06: immediate result of the same authoritative fire attempt; no cached status.
UENUM(BlueprintType, meta=(DisplayName="开火提交结果"))
enum class EWeaponFireAttemptResult : uint8
{
	Committed UMETA(DisplayName = "成功开火"),
	Reloading UMETA(DisplayName = "正在换弹"),
	NoWorld UMETA(DisplayName = "无有效世界"),
	Cooldown UMETA(DisplayName = "射速冷却"),
	Empty UMETA(DisplayName = "弹匣为空"),
	Busy UMETA(DisplayName = "开火处理中")
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFireCommitted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReloadStarted);
USTRUCT(BlueprintType, meta=(DisplayName="武器拾取状态"))
struct FWeaponPickupState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拾取", meta=(DisplayName="已保存运行时状态", ToolTip="为真时，拾取武器恢复下列保存的运行时弹药、开火模式和拔枪状态。"))
	bool bHasSavedState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拾取", meta=(DisplayName="当前弹药", ToolTip="拾取物保存的运行时当前弹药量，不是武器定义中的初始弹药量。"))
	int32 CurrentAmmo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拾取", meta=(DisplayName="备用弹药", ToolTip="拾取物保存的运行时备用弹药量，不是武器定义中的初始备用弹药量。"))
	int32 ReserveAmmo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拾取", meta=(DisplayName="开火模式"))
	EFireMode FireMode = EFireMode::SemiAutomatic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拾取", meta=(DisplayName="初始拔枪已消耗"))
	bool bInitialDrawConsumed = false;

	// Drop-local snapshot only, not checkpoint data. Both aim profiles keep their own clock.
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	bool bHasFiringSpreadSnapshot = false;
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	float CapturedWorldTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	float HipAccumulatedSpread = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	float AimAccumulatedSpread = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	float HipSecondsSinceShot = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	float AimSecondsSinceShot = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	bool bHasHipSpreadShot = false;
	UPROPERTY(BlueprintReadOnly, Category = "武器|拾取")
	bool bHasAimSpreadShot = false;
};

UCLASS(meta=(DisplayName="武器运行时"))
class DUST2_API AWeaponRuntime : public AActor
{
	GENERATED_BODY()
public:
	AWeaponRuntime();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|配置", meta = (DisplayName = "枪械玩法配置", ToolTip = "绑定后玩法数值从此资产读取；未绑定时保留旧蓝图参数。"))
	TObjectPtr<UWeaponDefinitionData> WeaponDefinition;
	UFUNCTION(BlueprintCallable, Category = "武器|配置", meta = (DisplayName = "应用枪械配置（保留当前弹药）", ToolTip = "刷新静态配置，不补满弹药，不覆盖当前开火模式。"))
	void ApplyDefinitionSettings();
	UFUNCTION(BlueprintCallable, Category = "武器|配置", meta = (DisplayName = "复制现有数值到配置", ToolTip = "迁移用：复制当前蓝图默认值，不引用候选调参文本。"))
	void CaptureDefinition(UWeaponDefinitionData* Destination) const;
	UFUNCTION(BlueprintPure, Category = "武器|配置", meta = (DisplayName = "获取当前连续射击散布", ToolTip = "返回额外散布角度，停火时按配置恢复。"))
	float GetCurrentFiringSpread() const;
	// W08: current cone half-angle for the next shot; read-only, no random sampling.
	UFUNCTION(BlueprintPure, Category = "武器|精度", meta = (DisplayName = "获取当前总散布角", ToolTip = "度数半角：移动状态基础散布与当前射击累计散布之和，不改变弹道或状态。"))
	float GetCurrentTotalSpreadAngle(float CurrentHorizontalSpeed, bool bIsFalling, bool bIsCrouched, bool bIsSlowWalking) const;
	UFUNCTION(BlueprintPure, Category = "武器")
	EFireMode GetFireMode() const;
	UFUNCTION(BlueprintCallable, Category = "武器")
	void RestoreFireMode(EFireMode SavedFireMode);
	UFUNCTION(BlueprintCallable, Category = "武器")
	void CycleFireMode();
	UFUNCTION(BlueprintPure, Category = "武器|换弹")
	bool IsReloading() const;
	UFUNCTION(BlueprintCallable, Category = "武器|换弹")
	bool TryStartReload();
	/**
	 * W09: explicit one-shot batch request for an explicitly empty-batch-capable definition.
	 * ExpectedAmount must equal min(MagazineCapacity - CurrentAmmo, ReserveAmmo) and be > 0.
	 * This request locks the amount and disables per-round commits; it never changes the
	 * static bReloadOneRoundAtATime / bUseBatchReloadForFullEmptyReload rules.
	 */
	bool TryStartEmptyBatchReload(int32 ExpectedAmount);
	/**
	 * Native-only enemy transaction: empty magazine, unlimited reserve source, one full-magazine
	 * commit on Finish. ReserveAmmo is never read or changed by this request. Supports ordinary
	 * Magazine and EmptyBatch definitions; pure per-round/sniper definitions are rejected.
	 */
	bool TryStartInfiniteEmptyReload();
	/** Native-only identity flag for the reload coordinator; true only for the active request. */
	bool IsInfiniteReloadSource() const { return bIsReloading && bCurrentReloadUsesInfiniteSource; }
	UFUNCTION(BlueprintCallable, Category = "武器|换弹")
	bool FinishReload();
	UFUNCTION(BlueprintCallable, Category = "武器|换弹")
	bool CancelReload();
	UFUNCTION(BlueprintPure, Category = "武器|换弹")
	int32 GetReloadRequestId() const;
	UFUNCTION(BlueprintCallable, Category = "武器|换弹")
	bool TryCommitReloadRound(int32 ExpectedReloadRequestId);
	UPROPERTY(BlueprintAssignable, Category = "武器|换弹")
	FOnReloadStarted OnReloadStarted;
	UFUNCTION(BlueprintPure, category = "Ammo")
	int32 GetCurrentAmmo()const;
	UFUNCTION(BlueprintPure, category = "Ammo")
	int32 GetReserveAmmo()const;
	// Native-only: BP_WeaponBase already owns a Blueprint function with this name.
	int32 GetMagazineCapacity()const;
	UFUNCTION(BlueprintCallable, Category = "武器|弹药")
	bool TryConsumeAmmo();
	UFUNCTION(BlueprintCallable, Category = "武器|弹药")
	bool AddReserveAmmo(int32 Amount);
	UFUNCTION(BlueprintCallable, Category = "武器|弹药")
	bool ReloadAmmo();
	UFUNCTION(BlueprintCallable, Category = "武器|弹药")
	void RestoreAmmo(int32 NewCurrentAmmo, int32 NewReserveAmmo);
	UFUNCTION(BlueprintCallable, Category = "武器|射击")
	bool TryCommitFire();
	UFUNCTION(BlueprintCallable, Category = "武器|射击", meta = (DisplayName = "尝试提交开火（返回原因）"))
	EWeaponFireAttemptResult TryCommitFireWithResult();
	/** Resolve exactly one already committed shot. Pass GenerateShotDirection's final direction.
	 * Does not consume ammo, resample spread or replay recoil. Legacy mode/input scheduling stays in BP. */
	UFUNCTION(BlueprintCallable, Category = "武器|射击", meta = (DisplayName = "解析本发命中与伤害（C++）"))
	FWeaponShotResolution ResolveCommittedShotNative(const FVector& TraceStart, const FVector& ShotDirection);
	UFUNCTION(BlueprintPure, Category = "武器|射击")
	float GetEffectiveFireInterval() const;
	UPROPERTY(BlueprintAssignable, Category = "武器|射击")
	FOnFireCommitted OnFireCommitted;
	UFUNCTION(BlueprintCallable, Category = "武器|后坐力")
	void ResetRecoilPattern();
	// AI observes the same next-shot recoil without advancing the pattern or consuming a kick.
	UFUNCTION(BlueprintPure, Category = "武器|后坐力", meta=(DisplayName="获取当前独立射击后坐"))
	FVector2D GetCurrentIndependentShotRecoil() const;
	UFUNCTION(BlueprintPure, Category = "武器|后坐力", meta=(DisplayName="获取本发已提交后坐"))
	FVector2D GetCommittedIndependentShotRecoil() const;
	// Switching clears directional recoil as before, but cannot erase random spread.
	void ResetRecoilForWeaponChange();
	UFUNCTION(BlueprintCallable, Category = "武器|后坐力")
	void GenerateRecoilKick(float& PitchKick, float& YawKick, bool& bGenerated);
	UFUNCTION(BlueprintCallable, Category = "武器|精度")
	void GenerateShotDirection(
		const FVector& TraceDirection,
		float CurrentHorizontalSpeed,
		bool bIsFalling,
		bool bIsCrouched,
		bool bIsSlowWalking,
		FVector& ShotDirection,
		bool& bGenerated);
	UFUNCTION(BlueprintPure, Category = "武器|显示")
	static FText GetDisplayNameForWeaponClass(
		TSubclassOf<AWeaponRuntime> WeaponClass);
	UFUNCTION(BlueprintCallable, Category = "武器|拾取")
	void RestorePickupRuntimeState(const FWeaponPickupState& State);
	UFUNCTION(BlueprintPure, Category = "武器|拾取")
	FWeaponPickupState CapturePickupRuntimeState(
		bool bInitialDrawConsumed) const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="当前开火模式"))
	EFireMode CurrentFireMode = EFireMode::SemiAutomatic;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="允许的开火模式"))
	TArray<EFireMode> AllowedFireModes;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|弹药", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="当前弹药", ToolTip="仅未绑定武器定义时使用的初始弹匣弹药量。"))
	int32 CurrentAmmo;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|弹药", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="弹匣容量"))
	int32 MagazineCapacity;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|弹药", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="备用弹药", ToolTip="仅未绑定武器定义时使用的初始备用弹药量。"))
	int32 ReserveAmmo;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "武器|换弹")
	bool bIsReloading;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|射击", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="射击间隔"))
	float FireInterval;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "武器|射击")
	float NextFireTime;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|射击",
		meta = (ClampMin = "0.0", DisplayName="连发组内间隔", ToolTip="三连发组内相邻子弹的间隔，单位秒；设为0时沿用射击间隔。"))
	float BurstShotInterval;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|后坐力", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="后坐力数据"))
	TObjectPtr<UWeaponRecoilData>RecoilData;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|精度", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="静止散布角"))
	float StationarySpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|精度", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="移动散布速度阈值"))
	float MovementSpreadSpeedThreshold;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|精度", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="移动散布角"))
	float MovingSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|精度", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="跳跃散布角"))
	float JumpSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|精度", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="慢走散布角"))
	float SlowWalkSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|换弹", meta = (EditCondition = "WeaponDefinition == nullptr", DisplayName="开始换弹时锁定装弹量"))
	bool bLockReloadAmountAtStart = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|换弹",
		meta = (DisplayName = "逐颗装入弹药"))
	bool bReloadOneRoundAtATime = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|换弹",
		meta = (DisplayName = "满数量空仓换弹一次装入"))
	bool bUseBatchReloadForFullEmptyReload = false;
	UPROPERTY(Transient)
	bool bCurrentReloadUsesPerRound = false;
	// W09: set only by TryStartEmptyBatchReload for this one request; Finish/Cancel clear it.
	UPROPERTY(Transient)
	bool bCurrentReloadUsesEmptyBatch = false;
	// W09: set only by TryStartInfiniteEmptyReload for this one request; Finish/Cancel clear it.
	UPROPERTY(Transient)
	bool bCurrentReloadUsesInfiniteSource = false;
	UPROPERTY(Transient)
	int32 PendingReloadAmount = 0;
	UPROPERTY(Transient)
	int32 ReloadRequestId = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|显示",
		meta = (DisplayName = "武器展示名"))
	FText WeaponDisplayName;

private:
	bool bHasPendingNativeShot = false;
	bool bCommittingFire = false;
	bool bResolvingNativeShot = false;
	friend class ADust2PlayerCharacter;
	void SetOwnerAimState(bool bAiming) { bOwnerAiming = bAiming; }
	/**
	 * Per-runtime transient recoil copy: source gameplay recoil + accuracy camera fields.
	 * Reused across ApplyDefinitionSettings calls; never saved and never mutates the shared asset.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UWeaponRecoilData> RuntimeRecoilData;
	FWeaponSpreadProfile GetSpreadProfile(bool bAiming) const;
	float GetFiringSpreadForProfile(bool bAiming) const;
	bool bOwnerAiming = false;
	FWeaponSpreadProfile CommittedSpreadProfile;
	bool bHasCommittedSpreadProfile = false;
	bool bCommittedIgnoreShotRecoil = false;
	float AccumulatedAimFiringSpread = 0.0f;
	float LastAimSpreadShotTime = 0.0f;
	bool bHasAimSpreadShot = false;
	void CacheCommittedRecoil(float SuccessfulShotTime);
	void CacheCommittedFiringSpread(float SuccessfulShotTime);
	/** Successful commit only: report one gunshot noise at the holder's commit-time location. */
	void ReportCommittedGunshotNoise();
	float AccumulatedFiringSpread = 0.0f;
	float CommittedFiringSpread = 0.0f;
	float LastSpreadShotTime = 0.0f;
	bool bHasSpreadShot = false;
	bool bRestoredAmmo = false;
	bool bRestoredFireMode = false;
	bool bInitialDefinitionApplied = false;
	void AdvanceRecoilPatternIndex(int32 PatternSize);
	void ClearPendingRecoilKick();
	/** Shared request publication; keeps request id wrap and OnReloadStarted order identical.
	 *  All capture flags are written before the broadcast. */
	void StartReloadInternal(int32 InPendingAmount, bool bInPerRound, bool bInEmptyBatch,
		bool bInInfiniteSource = false);

	int32 RecoilPatternIndex = 0;
	float LastSuccessfulShotTime = 0.0f;
	bool bHasSuccessfulShotTime = false;
	float CachedRecoilPitchKick = 0.0f;
	float CachedRecoilYawKick = 0.0f;
	bool bHasPendingRecoilKick = false;
	FVector2D AccumulatedShotRecoil = FVector2D::ZeroVector;
	FVector2D CommittedShotRecoil = FVector2D::ZeroVector;
	bool bHasCommittedShotDirection = false;
};
