// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponCameraFeedbackPreset.h"
#include "WeaponRecoilData.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "后坐力配置"))
class DUST2_API UWeaponRecoilData : public UDataAsset
{
	GENERATED_BODY()
public:
	UWeaponRecoilData();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "瞄准后坐倍率", ClampMin = "0.0", ToolTip = "复用当前后坐序列，缩放右键时的瞄准视线冲量；1保持序列强度，0关闭。腰射和射速不变。"))
	float AimingRecoilScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "启用固定后坐序列", DisplayPriority = "0", ToolTip = "开启后使用本枪保存的固定序列；序列参数在高级设置中。关闭不会删除已有序列。"))
	bool bUseFixedPattern;
	// Camera fields stay serialized for old assets, but move to UWeaponAccuracyData
	// for daily editing. Runtime reads the private transient copy built by ApplyDefinitionSettings.
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	bool bUseIndependentShotRecoil = false;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	float CameraFeedbackScale = 0.25f;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	TObjectPtr<UWeaponCameraFeedbackPreset> CameraFeedbackPreset = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	bool bOverrideCameraFeedback = false;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	FWeaponCameraFeedbackParameters CameraFeedbackOverrides;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	float CameraReturnSpeed = 12.0f;
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "射击后坐恢复速度", Units = "deg/s", ClampMin = "0.0", ToolTip = "停火超过恢复延迟后，每秒恢复的角度；停火达到序列重置时间则完整重置。"))
	float ShotRecoilRecoverySpeed = 8.0f;
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "固定后坐序列", ToolTip = "X 为非负俯仰增量，Y 为带符号的偏航增量。", EditCondition = "bUseFixedPattern", EditConditionHides, HideEditConditionToggle))
	TArray<FVector2D> FixedPattern;
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "序列停火重置时间", Units = "s", ToolTip = "停火达到此秒数后从首项重新开始；0表示每枪清除前发射击偏移，适合手枪。", ClampMin = "0.0", EditCondition = "bUseFixedPattern", EditConditionHides, HideEditConditionToggle))
	float PatternResetDelay;
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "上抬倍率", ClampMin = "0.0", ToolTip = "仅缩放固定轨迹的上抬量；1保持原值，0关闭上抬。", EditCondition = "bUseFixedPattern", EditConditionHides, HideEditConditionToggle))
	float FixedPatternPitchScale;
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "左右倍率", ClampMin = "0.0", ToolTip = "仅缩放固定轨迹的左右量；1保持原值，0关闭左右偏移。", EditCondition = "bUseFixedPattern", EditConditionHides, HideEditConditionToggle))
	float FixedPatternYawScale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐力", AdvancedDisplay,
		meta = (DisplayName = "尾段循环起始序号", ToolTip = "-1 表示到达末项后保持末项；其他值会限制在有效序列范围内。", EditCondition = "bUseFixedPattern", EditConditionHides, HideEditConditionToggle))
	int32 LoopStartIndex;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	float SmoothKickDuration;
	UPROPERTY(BlueprintReadOnly, Category = "镜头反馈|兼容数据")
	float ImmediateKickFraction;
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "后坐力",
		meta = (DisplayName = "恢复延迟", Units = "s", ToolTip = "最后一发后等待多少秒开始恢复后坐。", ClampMin = "0.0"))
	float RecoveryDelay;
	UPROPERTY(BlueprintReadOnly, Category = "后坐力")
	float RecoverySpeed;
	UPROPERTY(BlueprintReadOnly, Category = "后坐力")
	float RecoveryPitchRatio;
	UPROPERTY(BlueprintReadOnly, Category = "后坐力")
	float VerticalKickMin;
	UPROPERTY(BlueprintReadOnly, Category = "后坐力")
	float VerticalKickMax;
	UPROPERTY(BlueprintReadOnly, Category = "后坐力")
	float HorizontalKickMax;

	UFUNCTION(BlueprintCallable, Category = "武器|镜头", meta = (DisplayName = "迁移到匹配的镜头预设"))
	bool MigrateCameraFeedbackToPreset(UWeaponCameraFeedbackPreset* Preset);

	float GetCameraReturnSpeed() const;
	float GetSmoothKickDuration() const;
	float GetImmediateKickFraction() const;

};

