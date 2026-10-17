// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponCameraFeedbackPreset.h"
#include "WeaponRecoilData.generated.h"

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "后坐与镜头设置"))
class DUST2_API UWeaponRecoilData : public UDataAsset
{
	GENERATED_BODY()
public:
	UWeaponRecoilData();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "右键后坐强度倍率", ClampMin = "0.0", ToolTip = "复用当前后坐序列，缩放右键时的瞄准视线冲量；1保持序列强度，0关闭。腰射和射速不变。"))
	float AimingRecoilScale = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "后坐轨迹")
	bool bUseFixedPattern;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈")
	bool bUseIndependentShotRecoil = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "镜头反馈倍率", ClampMin = "0.0", UIMax = "5.0", ToolTip = "仅缩放镜头反馈；允许大于1，0关闭反馈，不改变射击方向。"))
	float CameraFeedbackScale = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "镜头反馈预设", ToolTip = "可供多把枪共用的镜头回落和平滑参数。未指定预设时沿用旧序列化参数。"))
	TObjectPtr<UWeaponCameraFeedbackPreset> CameraFeedbackPreset = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "使用枪械独立镜头参数", ToolTip = "开启后，本枪使用下方参数覆盖共用预设。"))
	bool bOverrideCameraFeedback = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "枪械镜头参数", EditCondition = "bOverrideCameraFeedback", EditConditionHides))
	FWeaponCameraFeedbackParameters CameraFeedbackOverrides;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈")
	float CameraReturnSpeed = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐恢复",
		meta = (DisplayName = "射击后坐恢复速度", ClampMin = "0.0", ToolTip = "停火超过恢复延迟后，每秒恢复的角度；停火达到序列重置时间则完整重置。"))
	float ShotRecoilRecoverySpeed = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "固定后坐序列", ToolTip = "X 为非负俯仰增量，Y 为带符号的偏航增量。", EditCondition = "bUseFixedPattern", EditConditionHides))
	TArray<FVector2D> FixedPattern;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "序列停火重置时间", ToolTip = "停火达到此秒数后从首项重新开始；0表示每枪清除前发射击偏移，适合手枪。", ClampMin = "0.0", EditCondition = "bUseFixedPattern", EditConditionHides))
	float PatternResetDelay;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "上抬倍率", ClampMin = "0.0", ToolTip = "仅缩放固定轨迹的上抬量；1保持原值，0关闭上抬。", EditCondition = "bUseFixedPattern", EditConditionHides))
	float FixedPatternPitchScale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "左右倍率", ClampMin = "0.0", ToolTip = "仅缩放固定轨迹的左右量；1保持原值，0关闭左右偏移。", EditCondition = "bUseFixedPattern", EditConditionHides))
	float FixedPatternYawScale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹", AdvancedDisplay,
		meta = (DisplayName = "尾段循环起始序号", ToolTip = "-1 表示到达末项后保持末项；其他值会限制在有效序列范围内。", EditCondition = "bUseFixedPattern", EditConditionHides))
	int32 LoopStartIndex;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈")
	float SmoothKickDuration;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈")
	float ImmediateKickFraction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐恢复",
		meta = (DisplayName = "恢复延迟", ToolTip = "最后一发后等待多少秒开始恢复后坐。", ClampMin = "0.0"))
	float RecoveryDelay;
	UPROPERTY(BlueprintReadOnly, Category = "后坐恢复")
	float RecoverySpeed;
	UPROPERTY(BlueprintReadOnly, Category = "后坐恢复")
	float RecoveryPitchRatio;
	UPROPERTY(BlueprintReadOnly, Category = "后坐轨迹")
	float VerticalKickMin;
	UPROPERTY(BlueprintReadOnly, Category = "后坐轨迹")
	float VerticalKickMax;
	UPROPERTY(BlueprintReadOnly, Category = "后坐轨迹")
	float HorizontalKickMax;

	UFUNCTION(BlueprintCallable, Category = "镜头反馈", meta = (DisplayName = "迁移到匹配的镜头预设"))
	bool MigrateCameraFeedbackToPreset(UWeaponCameraFeedbackPreset* Preset);

	float GetCameraReturnSpeed() const;
	float GetSmoothKickDuration() const;
	float GetImmediateKickFraction() const;
	
};

