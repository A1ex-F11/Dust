// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponBallisticsData.generated.h"

/**
 *
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "伤害与射程设置"))
class DUST2_API UWeaponBallisticsData :public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|伤害", meta = (DisplayName = "每发伤害", ToolTip = "单发基础伤害。", ClampMin = "0.0"))
	float DamagePerShot = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|伤害", meta = (DisplayName = "穿甲参数", ToolTip = "传给现有伤害接口的穿甲值，沿用当前伤害算法。", ClampMin = "0.0"))
	float ArmorPenetration = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|伤害", meta = (DisplayName = "爆头伤害倍率", ToolTip = "命中头部时使用的伤害倍率。", ClampMin = "0.0"))
	float HeadDamageMultiplier = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|伤害", meta = (DisplayName = "最大射程", Units = "cm", ToolTip = "射线最大距离。米制编辑控件显示为m；底层仍保存厘米，旧资产无需转换。", ClampMin = "0.0"))
	float TraceDistance = 0.0f;
};
