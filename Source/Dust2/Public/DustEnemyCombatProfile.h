#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DustEnemyCombatProfile.generated.h"

/** Editable difficulty and combat pacing settings for an enemy. */
UCLASS(BlueprintType, meta = (DisplayName = "敌人战斗难度配置"))
class DUST2_API UDustEnemyCombatProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|反应",
		meta = (DisplayName = "发现后的反应时间（秒）", ClampMin = "0.0"))
	float ReactionDelay = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|瞄准",
		meta = (DisplayName = "稳定瞄准时间（秒）", ClampMin = "0.0"))
	float SettledAimSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|瞄准",
		meta = (DisplayName = "目标跟踪速度（度/秒）", ClampMin = "0.0"))
	float TrackingSpeedDegPerSecond = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|压枪",
		meta = (DisplayName = "压枪补偿比例", ClampMin = "0.0", ClampMax = "1.0"))
	float CompensationFraction = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|压枪",
		meta = (DisplayName = "压枪修正速度（度/秒）", ClampMin = "0.0"))
	float CompensationSpeedDegPerSecond = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|压枪",
		meta = (DisplayName = "压枪反应延迟（秒）", ClampMin = "0.0"))
	float CompensationDelay = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|压枪",
		meta = (DisplayName = "压枪漂移角度（度）", ClampMin = "0.0"))
	float CompensationDriftDegrees = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|压枪",
		meta = (DisplayName = "漂移响应速度", ClampMin = "0.0"))
	float DriftResponseSpeed = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|射击",
		meta = (DisplayName = "每轮射击数", ClampMin = "1"))
	int32 BurstShotCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|射击",
		meta = (DisplayName = "射击轮次间隔（秒）", ClampMin = "0.0"))
	float BurstPauseSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|瞄准",
		meta = (DisplayName = "瞄准高度偏移（厘米）", ToolTip = "从角色 ActorLocation 向上偏移后作为 AI 的基础瞄准点。"))
	float AimHeightOffset = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|移动",
		meta = (DisplayName = "判定站稳的最高速度（厘米/秒）", ClampMin = "0.0"))
	float RequiredSettledSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "敌人战斗|移动",
		meta = (DisplayName = "交火距离（厘米）", ClampMin = "0.0"))
	float EngagementRange = 1200.0f;
};
