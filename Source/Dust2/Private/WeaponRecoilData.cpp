// Fill out your copyright notice in the Description page of Project Settings.


#include "WeaponRecoilData.h"
#include "Math/UnrealMathUtility.h"

UWeaponRecoilData::UWeaponRecoilData()
	:bUseFixedPattern(false)
	, PatternResetDelay(0.25f)
	, FixedPatternPitchScale(1.0f)
	, FixedPatternYawScale(1.0f)
	, LoopStartIndex(-1)
	, SmoothKickDuration(0.05f)
	, ImmediateKickFraction(0.25f)
	, RecoveryDelay(0.2f)
	, RecoverySpeed(1.0f)
	, RecoveryPitchRatio(0.35f)
	, VerticalKickMin(0.35f)
	, VerticalKickMax(0.50f)
	, HorizontalKickMax(0.08f)
{

}

bool UWeaponRecoilData::MigrateCameraFeedbackToPreset(UWeaponCameraFeedbackPreset* Preset)
{
	if (!Preset || bOverrideCameraFeedback ||
		!FMath::IsFinite(CameraReturnSpeed) || !FMath::IsFinite(SmoothKickDuration) ||
		!FMath::IsFinite(ImmediateKickFraction) || !FMath::IsFinite(Preset->ReturnSpeed) ||
		!FMath::IsFinite(Preset->SmoothKickDuration) || !FMath::IsFinite(Preset->ImmediateKickFraction))
	{
		return false;
	}

	constexpr float MatchTolerance = 0.0001f;
	if (!FMath::IsNearlyEqual(CameraReturnSpeed, Preset->ReturnSpeed, MatchTolerance) ||
		!FMath::IsNearlyEqual(SmoothKickDuration, Preset->SmoothKickDuration, MatchTolerance) ||
		!FMath::IsNearlyEqual(ImmediateKickFraction, Preset->ImmediateKickFraction, MatchTolerance))
	{
		return false;
	}

	CameraFeedbackPreset = Preset;
	return true;
}

float UWeaponRecoilData::GetCameraReturnSpeed() const
{
	if (bOverrideCameraFeedback)
	{
		return CameraFeedbackOverrides.ReturnSpeed;
	}
	return CameraFeedbackPreset ? CameraFeedbackPreset->ReturnSpeed : CameraReturnSpeed;
}

float UWeaponRecoilData::GetSmoothKickDuration() const
{
	if (bOverrideCameraFeedback)
	{
		return CameraFeedbackOverrides.SmoothKickDuration;
	}
	return CameraFeedbackPreset ? CameraFeedbackPreset->SmoothKickDuration : SmoothKickDuration;
}

float UWeaponRecoilData::GetImmediateKickFraction() const
{
	if (bOverrideCameraFeedback)
	{
		return CameraFeedbackOverrides.ImmediateKickFraction;
	}
	return CameraFeedbackPreset ? CameraFeedbackPreset->ImmediateKickFraction : ImmediateKickFraction;
}

