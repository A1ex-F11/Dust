#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/WeaponKitTypes.h"
#include "Data/WeaponKitRecoilData.h"
#include "Data/WeaponKitDefinitionData.generated.h"

class UWeaponKitBallisticsData;

/** One weapon's static gameplay configuration. Hip and aimed spread are the same profile type. */
UCLASS(BlueprintType, meta = (DisplayName = "枪械玩法配置"))
class WEAPONKIT_API UWeaponKitDefinitionData : public UDataAsset
{
	GENERATED_BODY()

public:
	UWeaponKitDefinitionData();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|基础", meta = (DisplayName = "武器展示名", ToolTip = "用于界面和武器展示的名称。"))
	FText WeaponDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|开火", meta = (DisplayName = "默认开火模式", ToolTip = "武器生成时使用的开火模式；必须包含在允许的开火模式中。"))
	EWeaponKitFireMode DefaultFireMode = EWeaponKitFireMode::SemiAutomatic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|开火", meta = (DisplayName = "允许的开火模式", ToolTip = "玩家可以在这把枪上切换的开火模式。"))
	TArray<EWeaponKitFireMode> AllowedFireModes = { EWeaponKitFireMode::SemiAutomatic };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|开火", meta = (DisplayName = "射击间隔", ClampMin = "0.0", ToolTip = "两次普通射击之间的最短间隔，单位为秒。"))
	float FireInterval = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|开火", meta = (DisplayName = "连发间隔", ClampMin = "0.0", ToolTip = "三连发中相邻子弹之间的间隔，单位为秒。非三连发武器通常设为0。"))
	float BurstShotInterval = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|开火", meta = (DisplayName = "连发恢复间隔", ClampMin = "0.0", ToolTip = "三连发结束后允许下一轮连发前等待的时间，单位为秒。"))
	float BurstRecoveryInterval = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|弹药", meta = (DisplayName = "弹匣容量", ClampMin = "1", ToolTip = "弹匣可容纳的最大子弹数。"))
	int32 MagazineCapacity = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|弹药", meta = (DisplayName = "初始装弹数", ClampMin = "0", ToolTip = "新武器首次生成时装入弹匣的子弹数；不是运行中的当前弹药。"))
	int32 InitialMagazineAmmo = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|弹药", meta = (DisplayName = "初始备弹数", ClampMin = "0", ToolTip = "新武器首次生成时携带的备用子弹数；不是运行中的当前备弹。"))
	int32 InitialReserveAmmo = 90;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "枪械配置|伤害与弹道", meta = (DisplayName = "伤害与弹道参数", ToolTip = "单发伤害、护甲穿透、爆头倍率和射线距离。"))
	TObjectPtr<UWeaponKitBallisticsData> BallisticsData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|腰射散布", meta = (DisplayName = "腰射散布"))
	FWeaponKitSpreadProfile HipFire;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|瞄准散布", meta = (DisplayName = "使用独立瞄准散布", ToolTip = "开启后右键使用瞄准散布；关闭时瞄准沿用腰射散布。"))
	bool bUseSeparateAimingSpread = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|瞄准散布", meta = (DisplayName = "右键瞄准散布", EditCondition = "bUseSeparateAimingSpread"))
	FWeaponKitSpreadProfile Aiming;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "枪械配置|后坐", meta = (DisplayName = "后坐参数", ToolTip = "后坐序列、单发冲量、镜头反馈和平滑恢复。"))
	TObjectPtr<UWeaponKitRecoilData> RecoilData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|换弹", meta = (DisplayName = "开始换弹时锁定装弹数量", ToolTip = "换弹开始时固定本次预计装入的子弹数，避免换弹途中备用弹变化导致数量改变。"))
	bool bLockReloadAmountAtStart = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|换弹", meta = (DisplayName = "逐颗装入弹药", ToolTip = "开启后按每发换弹通知逐颗装入弹药。"))
	bool bReloadOneRoundAtATime = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "枪械配置|换弹", meta = (DisplayName = "满弹匣空仓换弹一次装入", ToolTip = "开启逐颗装弹时，若空仓且备用弹足以装满弹匣，则允许一次装入全部计划弹药。"))
	bool bUseBatchReloadForFullEmptyReload = false;

	const FWeaponKitSpreadProfile& GetSpreadProfile(bool bAiming) const;
};
