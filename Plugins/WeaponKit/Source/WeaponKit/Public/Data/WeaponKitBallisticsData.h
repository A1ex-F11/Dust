#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/WeaponKitBallisticsData.generated.h"

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "伤害与射程设置"))
class WEAPONKIT_API UWeaponKitBallisticsData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "伤害与射程", meta = (DisplayName = "每发伤害", ToolTip = "单发基础伤害。", ClampMin = "0.0"))
	float DamagePerShot = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "伤害与射程", meta = (DisplayName = "穿甲参数", ToolTip = "传给伤害结算的穿甲值。命中哪个部位、乘什么倍率，由伤害区域表决定。", ClampMin = "0.0"))
	float ArmorPenetration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "伤害与射程", meta = (DisplayName = "爆头伤害倍率", ToolTip = "提供给头部区域的倍率。骨骼是否算头部由伤害区域表决定，这里不写死骨骼名。", ClampMin = "0.0"))
	float HeadDamageMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "伤害与射程", meta = (DisplayName = "最大射程（厘米）", ToolTip = "命中检测射线的最大长度。", ClampMin = "0.0"))
	float TraceDistance = 0.0f;
};
