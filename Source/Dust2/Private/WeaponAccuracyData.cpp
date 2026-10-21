// Fill out your copyright notice in the Description page of Project Settings.

#include "WeaponAccuracyData.h"
#include "WeaponCameraFeedbackPreset.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

namespace
{
	FString MakeSplitAssetName(const FString& DefinitionAssetName, const TCHAR* Suffix)
	{
		static const FString DefinitionSuffix(TEXT("Definition"));
		FString Base = DefinitionAssetName.EndsWith(DefinitionSuffix, ESearchCase::CaseSensitive)
			? DefinitionAssetName.LeftChop(DefinitionSuffix.Len())
			: DefinitionAssetName;
		if (Base.IsEmpty())
		{
			Base = DefinitionAssetName;
		}
		return Base + Suffix;
	}
}

bool BuildWeaponThreeAssetSplitPlan(
	const FString& DefinitionPackageName,
	const FString& DefinitionAssetName,
	bool bAccuracyAlreadyExternal,
	bool bRecoilAlreadyExternal,
	const TFunctionRef<bool(const FString&)>& IsPackageOccupied,
	FWeaponThreeAssetSplitPlan& OutPlan,
	FString& OutError)
{
	OutPlan = FWeaponThreeAssetSplitPlan{};
	OutError.Empty();

	if (DefinitionPackageName.IsEmpty() || DefinitionAssetName.IsEmpty())
	{
		OutError = TEXT("武器定义缺少有效的包名或资产名，无法导出精度/后坐资产。");
		return false;
	}
	if (!FPackageName::IsValidLongPackageName(DefinitionPackageName))
	{
		OutError = FString::Printf(TEXT("武器定义包名无效：%s"), *DefinitionPackageName);
		return false;
	}

	OutPlan.bCreateAccuracy = !bAccuracyAlreadyExternal;
	OutPlan.bCreateRecoil = !bRecoilAlreadyExternal;

	const FString AccuracyAssetName = MakeSplitAssetName(DefinitionAssetName, TEXT("Accuracy"));
	const FString RecoilAssetName = MakeSplitAssetName(DefinitionAssetName, TEXT("Recoil"));
	OutPlan.AccuracyAssetName = AccuracyAssetName;
	OutPlan.RecoilAssetName = RecoilAssetName;

	if (!OutPlan.bCreateAccuracy && !OutPlan.bCreateRecoil)
	{
		// Idempotent: both references already live in separate assets, nothing to write.
		return true;
	}

	if (AccuracyAssetName == DefinitionAssetName || RecoilAssetName == DefinitionAssetName)
	{
		OutError = TEXT("导出资产名与武器定义资产同名，拒绝覆盖定义本身。");
		return false;
	}

	const FString Directory = FPackageName::GetLongPackagePath(DefinitionPackageName);
	if (Directory.IsEmpty())
	{
		OutError = FString::Printf(TEXT("无法从武器定义包名取得同目录：%s"), *DefinitionPackageName);
		return false;
	}

	const FString AccuracyPackageName = FString::Printf(TEXT("%s/%s"), *Directory, *AccuracyAssetName);
	const FString RecoilPackageName = FString::Printf(TEXT("%s/%s"), *Directory, *RecoilAssetName);
	if (!FPackageName::IsValidLongPackageName(AccuracyPackageName)
		|| !FPackageName::IsValidLongPackageName(RecoilPackageName))
	{
		OutError = FString::Printf(TEXT("导出包名无效：%s / %s"), *AccuracyPackageName, *RecoilPackageName);
		return false;
	}

	if (!FPackageName::IsValidObjectPath(AccuracyPackageName + TEXT(".") + AccuracyAssetName)
		|| !FPackageName::IsValidObjectPath(RecoilPackageName + TEXT(".") + RecoilAssetName))
	{
		OutError = TEXT("导出资产路径无效，未创建资产。");
		return false;
	}

	if (OutPlan.bCreateAccuracy && IsPackageOccupied(AccuracyPackageName))
	{
		OutError = FString::Printf(TEXT("目标精度资产包已存在，拒绝覆盖：%s"), *AccuracyPackageName);
		return false;
	}
	if (OutPlan.bCreateRecoil && IsPackageOccupied(RecoilPackageName))
	{
		OutError = FString::Printf(TEXT("目标后坐资产包已存在，拒绝覆盖：%s"), *RecoilPackageName);
		return false;
	}

	OutPlan.AccuracyPackageName = AccuracyPackageName;
	OutPlan.RecoilPackageName = RecoilPackageName;
	return true;
}

void UWeaponAccuracyData::NormalizeDirectParameters(float LegacyMovementThreshold)
{
	const auto LocalizePosture = [LegacyMovementThreshold](FWeaponIntuitiveAccuracy& Accuracy)
	{
		const FWeaponPostureSettings Effective = Accuracy.GetPosture(LegacyMovementThreshold);
		Accuracy.PostureOverride = Effective;
		Accuracy.bUsePostureSettings = true;
		Accuracy.bOverridePosture = true;
		Accuracy.PosturePreset = nullptr;
	};
	LocalizePosture(HipAccuracy);
	LocalizePosture(AimAccuracy);

	// Read all effective values before changing the old selector/override state.
	FWeaponCameraFeedbackParameters EffectiveCamera;
	EffectiveCamera.ReturnSpeed = GetCameraReturnSpeed();
	EffectiveCamera.SmoothKickDuration = GetSmoothKickDuration();
	EffectiveCamera.ImmediateKickFraction = GetImmediateKickFraction();
	CameraFeedbackOverrides = EffectiveCamera;
	bOverrideCameraFeedback = true;
	CameraFeedbackPreset = nullptr;
}

void UWeaponAccuracyData::PostLoad()
{
	Super::PostLoad();
	NormalizeDirectParameters();
}

float UWeaponAccuracyData::GetCameraReturnSpeed() const
{
	if (bOverrideCameraFeedback)
	{
		return CameraFeedbackOverrides.ReturnSpeed;
	}
	return CameraFeedbackPreset ? CameraFeedbackPreset->ReturnSpeed : CameraReturnSpeed;
}

float UWeaponAccuracyData::GetSmoothKickDuration() const
{
	if (bOverrideCameraFeedback)
	{
		return CameraFeedbackOverrides.SmoothKickDuration;
	}
	return CameraFeedbackPreset ? CameraFeedbackPreset->SmoothKickDuration : SmoothKickDuration;
}

float UWeaponAccuracyData::GetImmediateKickFraction() const
{
	if (bOverrideCameraFeedback)
	{
		return CameraFeedbackOverrides.ImmediateKickFraction;
	}
	return CameraFeedbackPreset ? CameraFeedbackPreset->ImmediateKickFraction : ImmediateKickFraction;
}

#if WITH_EDITOR
void UWeaponAccuracyData::PreEditChange(FProperty* PropertyAboutToChange)
{
	NormalizeDirectParameters();
	Super::PreEditChange(PropertyAboutToChange);
}

void UWeaponAccuracyData::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	const FName Field = Event.Property ? Event.Property->GetFName() : NAME_None;
	const FName Member = Event.MemberProperty ? Event.MemberProperty->GetFName() : NAME_None;
	// Only editing the four author-facing firing knobs retires exact legacy timing.
	if (Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, BaseSpreadAngle)
		|| Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, MaxFiringExtraAngle)
		|| Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, ShotsToMaximum)
		|| Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, FullRecoverySeconds))
	{
		if (Member == GET_MEMBER_NAME_CHECKED(UWeaponAccuracyData, HipAccuracy))
		{
			HipAccuracy.bPreserveMigratedFiring = false;
		}
		if (Member == GET_MEMBER_NAME_CHECKED(UWeaponAccuracyData, AimAccuracy))
		{
			AimAccuracy.bPreserveMigratedFiring = false;
		}
	}
	Super::PostEditChangeProperty(Event);
}
#endif
