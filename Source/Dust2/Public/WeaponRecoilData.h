// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponRecoilData.generated.h"

UCLASS(BlueprintType)
class DUST2_API UWeaponRecoilData : public UDataAsset
{
	GENERATED_BODY()
public:
	UWeaponRecoilData();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Pattern",
		meta = (DisplayName = "使用固定后坐序列"))
	bool bUseFixedPattern;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Independent",
		meta = (DisplayName = "射击后坐与镜头分离"))
	bool bUseIndependentShotRecoil = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Independent",
		meta = (DisplayName = "镜头反馈倍率", ClampMin = "0.0", UIMax = "5.0", ToolTip = "仅缩放镜头反馈；允许大于1，0关闭反馈，不改变射击方向。"))
	float CameraFeedbackScale = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Independent",
		meta = (DisplayName = "镜头回落速度", ClampMin = "0.1"))
	float CameraReturnSpeed = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Independent",
		meta = (DisplayName = "射击后坐恢复速度", ClampMin = "0.0", ToolTip = "停火超过恢复延迟后，每秒恢复的角度；停火达到序列重置时间则完整重置。"))
	float ShotRecoilRecoverySpeed = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Pattern",
		meta = (DisplayName = "固定后坐序列", ToolTip = "X 为非负俯仰增量，Y 为带符号的偏航增量。"))
	TArray<FVector2D> FixedPattern;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Pattern",
		meta = (DisplayName = "序列停火重置时间", ClampMin = "0.0"))
	float PatternResetDelay;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Pattern",
		meta = (DisplayName = "上抬倍率", ClampMin = "0.0", ToolTip = "仅缩放固定轨迹的上抬量；1保持原值，0关闭上抬。"))
	float FixedPatternPitchScale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Pattern",
		meta = (DisplayName = "左右倍率", ClampMin = "0.0", ToolTip = "仅缩放固定轨迹的左右量；1保持原值，0关闭左右偏移。"))
	float FixedPatternYawScale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Pattern",
		meta = (DisplayName = "尾段循环起始序号", ToolTip = "-1 表示到达末项后保持末项；其他值会限制在有效序列范围内。"))
	int32 LoopStartIndex;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Smoothing",
		meta = (DisplayName = "平滑施加时长", ClampMin = "0.0"))
	float SmoothKickDuration;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Smoothing",
		meta = (DisplayName = "立即施加比例", ClampMin = "0.0", ClampMax = "1.0"))
	float ImmediateKickFraction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Recovery",
		meta = (DisplayName = "恢复延迟", ClampMin = "0.0"))
	float RecoveryDelay;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Recovery",
		meta = (DisplayName = "恢复速度", ClampMin = "0.0"))
	float RecoverySpeed;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Recovery",
		meta = (DisplayName = "俯仰恢复比例", ClampMin = "0.0", ClampMax = "1.0"))
	float RecoveryPitchRatio;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recoil")
	float VerticalKickMin;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float VerticalKickMax;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float HorizontalKickMax;

};
