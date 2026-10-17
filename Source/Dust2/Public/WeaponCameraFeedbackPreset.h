// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponCameraFeedbackPreset.generated.h"

USTRUCT(BlueprintType)
struct DUST2_API FWeaponCameraFeedbackParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "镜头回落速度", ClampMin = "0.1", ToolTip = "镜头冲击回到正常视角的速度；越大回落越快。"))
	float ReturnSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "平滑施加时长", ClampMin = "0.0", ToolTip = "每发剩余镜头冲量平滑施加的秒数；0表示立即施加。"))
	float SmoothKickDuration = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "立即施加比例", ClampMin = "0.0", ClampMax = "1.0", ToolTip = "单发冲量中立即施加的比例，剩余部分按平滑时长施加。"))
	float ImmediateKickFraction = 0.25f;
};

UCLASS(BlueprintType, meta = (DisplayName = "镜头反馈预设"))
class DUST2_API UWeaponCameraFeedbackPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "镜头回落速度", ClampMin = "0.1", ToolTip = "镜头冲击回到正常视角的速度；越大回落越快。"))
	float ReturnSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "平滑施加时长", ClampMin = "0.0", ToolTip = "每发剩余镜头冲量平滑施加的秒数；0表示立即施加。"))
	float SmoothKickDuration = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "立即施加比例", ClampMin = "0.0", ClampMax = "1.0", ToolTip = "单发冲量中立即施加的比例，剩余部分按平滑时长施加。"))
	float ImmediateKickFraction = 0.25f;
};
