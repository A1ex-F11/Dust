// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/Function.h"
#include "WeaponDefinitionData.h"
#include "WeaponAccuracyData.generated.h"

/**
 * Second of the three weapon assets: precision plus camera feedback.
 *
 * FWeaponIntuitiveAccuracy stays in WeaponDefinitionData.h so legacy Definition
 * serialization keeps the exact same struct. This asset only owns the new copies.
 */
UCLASS(BlueprintType, meta = (DisplayName = "精度与镜头配置"))
class DUST2_API UWeaponAccuracyData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "腰射精度",
		meta = (ShowOnlyInnerProperties, DisplayName = "腰射精度"))
	FWeaponIntuitiveAccuracy HipAccuracy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "瞄准精度",
		meta = (ShowOnlyInnerProperties, DisplayName = "瞄准精度", EditCondition = "bUseAimingSpread", EditConditionHides, HideEditConditionToggle))
	FWeaponIntuitiveAccuracy AimAccuracy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "瞄准精度",
		meta = (DisplayName = "启用独立瞄准散布", ToolTip = "开启后右键使用瞄准精度；关闭时腰射与瞄准沿用腰射精度。旧资产默认关闭。"))
	bool bUseAimingSpread = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (DisplayName = "镜头反馈倍率", ClampMin = "0.0", UIMax = "5.0", ToolTip = "仅缩放镜头反馈；允许大于1，0关闭反馈，不改变射击方向。"))
	float CameraFeedbackScale = 0.25f;

	// Hidden serialized inputs retained for lossless conversion of existing assets.
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UWeaponCameraFeedbackPreset> CameraFeedbackPreset = nullptr;
	UPROPERTY(BlueprintReadOnly)
	bool bOverrideCameraFeedback = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "镜头反馈",
		meta = (ShowOnlyInnerProperties, DisplayName = "镜头参数"))
	FWeaponCameraFeedbackParameters CameraFeedbackOverrides;
	UPROPERTY(BlueprintReadOnly)
	float CameraReturnSpeed = 12.0f;
	UPROPERTY(BlueprintReadOnly)
	float SmoothKickDuration = 0.05f;
	UPROPERTY(BlueprintReadOnly)
	float ImmediateKickFraction = 0.25f;

	/** Copy current effective preset/override values into this asset's local fields. */
	void NormalizeDirectParameters(float LegacyMovementThreshold = 10.0f);
	virtual void PostLoad() override;

	float GetCameraReturnSpeed() const;
	float GetSmoothKickDuration() const;
	float GetImmediateKickFraction() const;

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};

/** Pure result of naming/idempotency planning for the Definition split button. */
struct DUST2_API FWeaponThreeAssetSplitPlan
{
	bool bCreateAccuracy = false;
	bool bCreateRecoil = false;
	FString AccuracyPackageName;
	FString AccuracyAssetName;
	FString RecoilPackageName;
	FString RecoilAssetName;
};

/**
 * Pure helper for "拆分为精度与后坐资产". It never touches packages; the editor
 * caller supplies the occupancy query so existing assets are never overwritten.
 *
 * Already-external references are idempotent: only the missing asset is planned.
 * Empty package/asset names and occupied targets fail without changing any reference.
 */
DUST2_API bool BuildWeaponThreeAssetSplitPlan(
	const FString& DefinitionPackageName,
	const FString& DefinitionAssetName,
	bool bAccuracyAlreadyExternal,
	bool bRecoilAlreadyExternal,
	const TFunctionRef<bool(const FString&)>& IsPackageOccupied,
	FWeaponThreeAssetSplitPlan& OutPlan,
	FString& OutError);
