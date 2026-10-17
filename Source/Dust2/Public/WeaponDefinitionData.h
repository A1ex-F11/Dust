// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FireMode.h"
#include "WeaponRecoilData.h"
#include "WeaponAccuracyPreset.h"
#include "WeaponDefinitionData.generated.h"

class UWeaponBallisticsData;

USTRUCT(BlueprintType)
struct DUST2_API FWeaponSpreadProfile
{
	GENERATED_BODY()
	// Runtime semantic flag; old serialized profiles keep their absolute-angle interpretation.
	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	bool bUseAdditiveStateSpread = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="静止散布角度", ClampMin="0.0"))
	float StationarySpreadAngle = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="移动散布角度", ClampMin="0.0"))
	float MovingSpreadAngle = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="跳跃散布角度", ClampMin="0.0"))
	float JumpSpreadAngle = 6.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="慢走散布角度", ClampMin="0.0"))
	float SlowWalkSpreadAngle = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="移动散布速度阈值", ClampMin="0.0"))
	float MovementSpreadSpeedThreshold = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="启用连续射击散布"))
	bool bUseFiringSpread = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="每发增加散布", ClampMin="0.0"))
	float SpreadPerShot = 0.02f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="连续射击散布上限", ClampMin="0.0"))
	float MaxFiringSpread = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="连续射击散布恢复延迟", ClampMin="0.0"))
	float SpreadRecoveryDelay = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="连续射击散布恢复速度", ClampMin="0.0"))
	float SpreadRecoverySpeed = 1.5f;
};

/** Author-facing tuning: the runtime derives per-shot growth and recovery rate. */
USTRUCT(BlueprintType)
struct DUST2_API FWeaponIntuitiveAccuracy
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="基础散布", ClampMin="0.0", ClampMax="89.0", ToolTip="站稳且射击累计恢复完的圆锥半角，单位度；是姿态叠加的下限。"))
	float BaseSpreadAngle = 0.2f;
	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float MovingExtraAngle = 2.0f;
	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float SlowWalkExtraAngle = 0.5f;
	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float AirborneExtraAngle = 6.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="最大射击额外散布", ClampMin="0.0", ToolTip="仅连续射击累计项的上限；0关闭射击累计，不改变固定后坐轨迹。"))
	float MaxFiringExtraAngle = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="连续到顶参考发数", ClampMin="1", ClampMax="1000", ToolTip="按当前射击模式的最短相邻间隔反推单发增量，已计入两枪间恢复。慢点射/三连发组间等待会改变实际到顶发数；恢复时间不大于相邻间隔时无法跨发累计。"))
	int32 ShotsToMaximum = 12;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="满累计恢复秒数", ClampMin="0.01", ToolTip="停火后从最大射击累计恢复到0的秒数；无等待延迟，不包含固定后坐和基础姿态项。"))
	float FullRecoverySeconds = 1.2f;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="姿态精度预设"))
	TObjectPtr<UWeaponAccuracyPreset> PosturePreset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="覆盖姿态预设", EditCondition="PosturePreset != nullptr", EditConditionHides))
	bool bOverridePosture = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="独立姿态参数", EditCondition="PosturePreset == nullptr || bOverridePosture", EditConditionHides))
	FWeaponPostureSettings PostureOverride;
	// Compatibility data is serialized, never a second editable configuration.
	UPROPERTY(BlueprintReadOnly, Category="兼容数据") bool bUsePostureSettings = false;
	UPROPERTY(BlueprintReadOnly, Category="兼容数据") bool bPreserveMigratedFiring = false;
	UPROPERTY(BlueprintReadOnly, Category="兼容数据") FWeaponSpreadProfile MigratedFiring;
	FWeaponPostureSettings GetPosture(float LegacyThreshold) const;
	void MigratePosture(float LegacyThreshold);
	static FWeaponIntuitiveAccuracy FromLegacy(const FWeaponSpreadProfile& Profile, float Interval);
	FWeaponSpreadProfile BuildProfile(float ShotInterval, float MovementThreshold) const;
};

/** A single editable gameplay configuration for one weapon. */
UCLASS(BlueprintType, meta = (DisplayName = "枪械玩法配置"))
class DUST2_API UWeaponDefinitionData : public UDataAsset
{
	GENERATED_BODY()

public:
	UWeaponDefinitionData();
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
	void MigrateConfiguration();
	EFireMode GetInitialFireMode() const;
	bool UsesPerRoundReload() const;
	bool UsesEmptyBatchReload() const;
	bool HasUnifiedConfiguration() const { return ConfigurationVersion >= 1; }
	UPROPERTY(BlueprintReadOnly, Category="兼容数据") int32 ConfigurationVersion = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="枪械配置|弹药与装填", meta=(DisplayName="装填方式"))
	EWeaponReloadStyle ReloadStyle = EWeaponReloadStyle::Magazine;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	bool bUseIntuitiveHipAccuracy = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="枪械配置|精度",
		meta=(DisplayName="腰射精度"))
	FWeaponIntuitiveAccuracy HipAccuracy;
	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	bool bUseIntuitiveAimAccuracy = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="枪械配置|精度",
		meta=(DisplayName="瞄准精度", EditCondition="bUseAimingSpread", EditConditionHides))
	FWeaponIntuitiveAccuracy AimAccuracy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="枪械配置|精度",
		meta=(DisplayName="启用独立瞄准散布", ToolTip="开启后右键使用下方配置；关闭时腰射与瞄准沿用原有散布。旧资产默认关闭，不自动覆盖原数值。"))
	bool bUseAimingSpread = false;
	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	FWeaponSpreadProfile AimingSpread;
	// Serialized legacy field only. ADS now always follows the input view direction.
	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	bool bIgnoreShotRecoilWhenAiming = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|基础与开火",
		meta = (DisplayName = "武器展示名", ToolTip = "用于界面和武器展示的名称。迁移时复制原武器蓝图中的实际名称。"))
	FText WeaponDisplayName;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	EFireMode DefaultFireMode = EFireMode::SemiAutomatic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|基础与开火",
		meta = (DisplayName = "开火模式（首项为初始）", ToolTip = "按列表顺序循环；第一项是新武器初始模式，存档恢复优先。"))
	TArray<EFireMode> AllowedFireModes = { EFireMode::SemiAutomatic };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|基础与开火",
		meta = (DisplayName = "射击间隔", ClampMin = "0.0", ToolTip = "两次普通射击之间的最短间隔，单位为秒。迁移时复制原武器的实际值。"))
	float FireInterval = 0.15f;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "枪械配置|基础与开火",
		meta = (DisplayName = "连发间隔", ClampMin = "0.0", ToolTip = "三连发中相邻子弹之间的间隔，单位为秒。非三连发武器通常设为0。"))
	float BurstShotInterval = 0.0f;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "枪械配置|基础与开火",
		meta = (DisplayName = "连发恢复间隔", ClampMin = "0.0", ToolTip = "三连发结束后允许下一轮连发前等待的时间，单位为秒。"))
	float BurstRecoveryInterval = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|弹药与装填",
		meta = (DisplayName = "弹匣容量", ClampMin = "1", ToolTip = "弹匣可容纳的最大子弹数。"))
	int32 MagazineCapacity = 30;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "枪械配置|弹药与装填",
		meta = (DisplayName = "初始装弹数", ClampMin = "0", ToolTip = "新武器首次生成时装入弹匣的子弹数；不是运行中的当前弹药。"))
	int32 InitialMagazineAmmo = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|弹药与装填",
		meta = (DisplayName = "初始备弹数", ClampMin = "0", ToolTip = "新武器首次生成时携带的备用子弹数；不是运行中的当前备弹。"))
	int32 InitialReserveAmmo = 90;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "枪械配置|伤害与射程",
		meta = (DisplayName = "伤害与弹道参数", ToolTip = "展开后可在此编辑单发伤害、护甲穿透、爆头倍率和射线距离；迁移时复制原弹道资产的实际值。"))
	TObjectPtr<UWeaponBallisticsData> BallisticsData;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float StationarySpreadAngle = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float MovingSpreadAngle = 3.0f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float JumpSpreadAngle = 12.0f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float SlowWalkSpreadAngle = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float MovementSpreadSpeedThreshold = 10.0f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	bool bUseFiringSpread = false;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float SpreadPerShot = 0.15f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float MaxFiringSpread = 0.8f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float SpreadRecoveryDelay = 0.2f;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	float SpreadRecoverySpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "枪械配置|后坐与镜头",
		meta = (DisplayName = "后坐参数", ToolTip = "展开后可编辑后坐序列、单发冲量、镜头反馈和平滑恢复；迁移时复制原后坐资产的实际值。"))
	TObjectPtr<UWeaponRecoilData> RecoilData;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "枪械配置|弹药与装填",
		meta = (DisplayName = "开始换弹时锁定装弹数量", ToolTip = "换弹开始时固定本次预计装入的子弹数，避免换弹途中备用弹变化导致数量改变。"))
	bool bLockReloadAmountAtStart = false;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	bool bReloadOneRoundAtATime = false;

	UPROPERTY(BlueprintReadOnly, Category="兼容数据")
	bool bUseBatchReloadForFullEmptyReload = false;
};
