
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include"FireMode.h"
#include"WeaponRecoilData.h"
#include "WeaponDefinitionData.h"
#include "WeaponRuntime.generated.h"

class UWeaponDefinitionData;

// W06: immediate result of the same authoritative fire attempt; no cached status.
UENUM(BlueprintType)
enum class EWeaponFireAttemptResult : uint8
{
	Committed UMETA(DisplayName = "成功开火"),
	Reloading UMETA(DisplayName = "正在换弹"),
	NoWorld UMETA(DisplayName = "无有效世界"),
	Cooldown UMETA(DisplayName = "射速冷却"),
	Empty UMETA(DisplayName = "弹匣为空")
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFireCommitted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReloadStarted);
USTRUCT(BlueprintType)
struct FWeaponPickupState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Pickup")
	bool bHasSavedState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Pickup")
	int32 CurrentAmmo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Pickup")
	int32 ReserveAmmo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Pickup")
	EFireMode FireMode = EFireMode::SemiAutomatic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Pickup")
	bool bInitialDrawConsumed = false;

	// Drop-local snapshot only, not checkpoint data. Both aim profiles keep their own clock.
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	bool bHasFiringSpreadSnapshot = false;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	float CapturedWorldTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	float HipAccumulatedSpread = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	float AimAccumulatedSpread = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	float HipSecondsSinceShot = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	float AimSecondsSinceShot = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	bool bHasHipSpreadShot = false;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Pickup")
	bool bHasAimSpreadShot = false;
};

UCLASS()
class DUST2_API AWeaponRuntime : public AActor
{
	GENERATED_BODY()
public:
	AWeaponRuntime();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器配置", meta = (DisplayName = "枪械玩法配置", ToolTip = "绑定后玩法数值从此资产读取；未绑定时保留旧蓝图参数。"))
	TObjectPtr<UWeaponDefinitionData> WeaponDefinition;
	UFUNCTION(BlueprintCallable, Category = "武器配置", meta = (DisplayName = "应用枪械配置（保留当前弹药）", ToolTip = "刷新静态配置，不补满弹药，不覆盖当前开火模式。"))
	void ApplyDefinitionSettings();
	UFUNCTION(BlueprintCallable, Category = "武器配置", meta = (DisplayName = "复制现有数值到配置", ToolTip = "迁移用：复制当前蓝图默认值，不引用候选调参文本。"))
	void CaptureDefinition(UWeaponDefinitionData* Destination) const;
	UFUNCTION(BlueprintPure, Category = "武器配置", meta = (DisplayName = "获取当前连续射击散布", ToolTip = "返回额外散布角度，停火时按配置恢复。"))
	float GetCurrentFiringSpread() const;
	// W08: current cone half-angle for the next shot; read-only, no random sampling.
	UFUNCTION(BlueprintPure, Category = "Spread", meta = (DisplayName = "获取当前总散布角", ToolTip = "度数半角：移动状态基础散布与当前射击累计散布之和，不改变弹道或状态。"))
	float GetCurrentTotalSpreadAngle(float CurrentHorizontalSpeed, bool bIsFalling, bool bIsCrouched, bool bIsSlowWalking) const;
	UFUNCTION(BlueprintPure, Category = "Weapon")
	EFireMode GetFireMode() const;
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void RestoreFireMode(EFireMode SavedFireMode);
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void CycleFireMode();
	UFUNCTION(BlueprintPure, Category = "Reload")
	bool IsReloading() const;
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool TryStartReload();
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool FinishReload();
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool CancelReload();
	UFUNCTION(BlueprintPure, Category = "Reload")
	int32 GetReloadRequestId() const;
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool TryCommitReloadRound(int32 ExpectedReloadRequestId);
	UPROPERTY(BlueprintAssignable, Category = "Reload")
	FOnReloadStarted OnReloadStarted;
	UFUNCTION(BlueprintPure, category = "Ammo")
	int32 GetCurrentAmmo()const;
	UFUNCTION(BlueprintPure, category = "Ammo")
	int32 GetReserveAmmo()const;
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	bool TryConsumeAmmo();
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	bool AddReserveAmmo(int32 Amount);
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	bool ReloadAmmo();
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	void RestoreAmmo(int32 NewCurrentAmmo, int32 NewReserveAmmo);
	UFUNCTION(BlueprintCallable, Category = "Fire")
	bool TryCommitFire();
	UFUNCTION(BlueprintCallable, Category = "Fire", meta = (DisplayName = "尝试提交开火（返回原因）"))
	EWeaponFireAttemptResult TryCommitFireWithResult();
	UFUNCTION(BlueprintPure, Category = "Fire")
	float GetEffectiveFireInterval() const;
	UPROPERTY(BlueprintAssignable, Category = "Fire")
	FOnFireCommitted OnFireCommitted;
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void ResetRecoilPattern();
	// AI observes the same next-shot recoil without advancing the pattern or consuming a kick.
	UFUNCTION(BlueprintPure, Category = "Recoil", meta=(DisplayName="获取当前独立射击后坐"))
	FVector2D GetCurrentIndependentShotRecoil() const;
	UFUNCTION(BlueprintPure, Category = "Recoil", meta=(DisplayName="获取本发已提交后坐"))
	FVector2D GetCommittedIndependentShotRecoil() const;
	// Switching clears directional recoil as before, but cannot erase random spread.
	void ResetRecoilForWeaponChange();
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void GenerateRecoilKick(float& PitchKick, float& YawKick, bool& bGenerated);
	UFUNCTION(BlueprintCallable, Category = "Spread")
	void GenerateShotDirection(
		const FVector& TraceDirection,
		float CurrentHorizontalSpeed,
		bool bIsFalling,
		bool bIsCrouched,
		bool bIsSlowWalking,
		FVector& ShotDirection,
		bool& bGenerated);
	UFUNCTION(BlueprintPure, Category = "Weapon|Display")
	static FText GetDisplayNameForWeaponClass(
		TSubclassOf<AWeaponRuntime> WeaponClass);
	UFUNCTION(BlueprintCallable, Category = "Weapon|Pickup")
	void RestorePickupRuntimeState(const FWeaponPickupState& State);
	UFUNCTION(BlueprintPure, Category = "Weapon|Pickup")
	FWeaponPickupState CapturePickupRuntimeState(
		bool bInitialDrawConsumed) const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (EditCondition = "WeaponDefinition == nullptr"))
	EFireMode CurrentFireMode = EFireMode::SemiAutomatic;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (EditCondition = "WeaponDefinition == nullptr"))
	TArray<EFireMode> AllowedFireModes;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (EditCondition = "WeaponDefinition == nullptr"))
	int32 CurrentAmmo;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (EditCondition = "WeaponDefinition == nullptr"))
	int32 MagazineCapacity;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (EditCondition = "WeaponDefinition == nullptr"))
	int32 ReserveAmmo;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Reload")
	bool bIsReloading;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fire", meta = (EditCondition = "WeaponDefinition == nullptr"))
	float FireInterval;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Fire")
	float NextFireTime;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fire",
		meta = (ClampMin = "0.0"))
	float BurstShotInterval;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (EditCondition = "WeaponDefinition == nullptr"))
	TObjectPtr<UWeaponRecoilData>RecoilData;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (EditCondition = "WeaponDefinition == nullptr"))
	float StationarySpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (EditCondition = "WeaponDefinition == nullptr"))
	float MovementSpreadSpeedThreshold;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (EditCondition = "WeaponDefinition == nullptr"))
	float MovingSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (EditCondition = "WeaponDefinition == nullptr"))
	float JumpSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (EditCondition = "WeaponDefinition == nullptr"))
	float SlowWalkSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reload", meta = (EditCondition = "WeaponDefinition == nullptr"))
	bool bLockReloadAmountAtStart = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reload",
		meta = (DisplayName = "逐颗装入弹药"))
	bool bReloadOneRoundAtATime = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reload",
		meta = (DisplayName = "满数量空仓换弹一次装入"))
	bool bUseBatchReloadForFullEmptyReload = false;
	UPROPERTY(Transient)
	bool bCurrentReloadUsesPerRound = false;
	UPROPERTY(Transient)
	int32 PendingReloadAmount = 0;
	UPROPERTY(Transient)
	int32 ReloadRequestId = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Display",
		meta = (DisplayName = "武器展示名"))
	FText WeaponDisplayName;

private:
	friend class ADust2PlayerCharacter;
	void SetOwnerAimState(bool bAiming) { bOwnerAiming = bAiming; }
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
	float AccumulatedFiringSpread = 0.0f;
	float CommittedFiringSpread = 0.0f;
	float LastSpreadShotTime = 0.0f;
	bool bHasSpreadShot = false;
	bool bRestoredAmmo = false;
	bool bRestoredFireMode = false;
	bool bInitialDefinitionApplied = false;
	void AdvanceRecoilPatternIndex(int32 PatternSize);
	void ClearPendingRecoilKick();

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
