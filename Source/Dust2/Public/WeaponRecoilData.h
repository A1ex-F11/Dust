// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "使用固定后坐序列", ToolTip = "开启后按数组逐发采样；关闭后使用随机上抬和左右范围。"))
	bool bUseFixedPattern;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈", AdvancedDisplay,
		meta = (DisplayName = "射击后坐与镜头分离", ToolTip = "开启后镜头仅提供画面反馈，射击方向偏移由武器单独计算。"))
	bool bUseIndependentShotRecoil = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "镜头反馈倍率", ClampMin = "0.0", UIMax = "5.0", ToolTip = "仅缩放镜头反馈；允许大于1，0关闭反馈，不改变射击方向。"))
	float CameraFeedbackScale = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "镜头回落速度", ToolTip = "镜头冲击回到正常视角的速度；越大回落越快。", ClampMin = "0.1"))
	float CameraReturnSpeed = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐恢复",
		meta = (DisplayName = "射击后坐恢复速度", ClampMin = "0.0", ToolTip = "停火超过恢复延迟后，每秒恢复的角度；停火达到序列重置时间则完整重置。"))
	float ShotRecoilRecoverySpeed = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "固定后坐序列", ToolTip = "X 为非负俯仰增量，Y 为带符号的偏航增量。", EditCondition = "bUseFixedPattern", EditConditionHides))
	TArray<FVector2D> FixedPattern;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹",
		meta = (DisplayName = "序列停火重置时间", ToolTip = "停火达到此秒数后从首项重新开始；0表示每枪清除前发射击偏移，适合手枪。", ClampMin = "0.0"))
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "平滑施加时长", ToolTip = "每发剩余镜头冲量平滑施加的秒数；0表示立即施加。", ClampMin = "0.0"))
	float SmoothKickDuration;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈", AdvancedDisplay,
		meta = (DisplayName = "立即施加比例", ToolTip = "单发冲量中立即施加的比例，剩余部分按平滑时长施加。", ClampMin = "0.0", ClampMax = "1.0"))
	float ImmediateKickFraction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐恢复",
		meta = (DisplayName = "恢复延迟", ToolTip = "最后一发后等待多少秒开始恢复后坐。", ClampMin = "0.0"))
	float RecoveryDelay;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐恢复",
		meta = (DisplayName = "恢复速度", ToolTip = "旧控制器后坐模式的自动恢复速度；分离模式使用射击后坐恢复速度和镜头回落速度。", ClampMin = "0.0", EditCondition = "!bUseIndependentShotRecoil", EditConditionHides))
	float RecoverySpeed;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐恢复", AdvancedDisplay,
		meta = (DisplayName = "俯仰恢复比例", ToolTip = "旧模式停火后自动回收上抬的比例；不改变分离模式弹道。", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "!bUseIndependentShotRecoil", EditConditionHides))
	float RecoveryPitchRatio;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="后坐轨迹", meta=(DisplayName="随机上抬最小值", ToolTip="关闭固定序列时，每发随机上抬的最小角度。", EditCondition="!bUseFixedPattern", EditConditionHides))
	float VerticalKickMin;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹", meta=(DisplayName="随机上抬最大值", ToolTip="关闭固定序列时，每发随机上抬的最大角度。", EditCondition="!bUseFixedPattern", EditConditionHides))
	float VerticalKickMax;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "后坐轨迹", meta=(DisplayName="随机左右最大值", ToolTip="关闭固定序列时，每发左右摆动的最大角度。", EditCondition="!bUseFixedPattern", EditConditionHides))
	float HorizontalKickMax;
	
};

