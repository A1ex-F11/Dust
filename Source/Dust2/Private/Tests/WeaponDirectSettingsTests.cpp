// Fill out your copyright notice in the Description page of Project Settings.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WeaponAccuracyData.h"
#include "WeaponAccuracyPreset.h"
#include "WeaponCameraFeedbackPreset.h"

namespace WeaponDirectSettingsTests
{
	constexpr float Tolerance = 1.e-5f;

	void CheckClose(FAutomationTestBase& Test, const TCHAR* What, float Actual, float Expected, float Epsilon = Tolerance)
	{
		if (!FMath::IsNearlyEqual(Actual, Expected, Epsilon))
		{
			Test.AddError(FString::Printf(TEXT("%s: expected %.6f, got %.6f"), What, Expected, Actual));
		}
	}

	bool SamePosture(const FWeaponPostureSettings& A, const FWeaponPostureSettings& B)
	{
		return FMath::IsNearlyEqual(A.MovingExtraAngle, B.MovingExtraAngle, Tolerance)
			&& FMath::IsNearlyEqual(A.SlowWalkExtraAngle, B.SlowWalkExtraAngle, Tolerance)
			&& FMath::IsNearlyEqual(A.AirborneExtraAngle, B.AirborneExtraAngle, Tolerance)
			&& FMath::IsNearlyEqual(A.MovementThreshold, B.MovementThreshold, Tolerance);
	}

	bool SameProfile(const FWeaponSpreadProfile& A, const FWeaponSpreadProfile& B)
	{
		return A.bUseAdditiveStateSpread == B.bUseAdditiveStateSpread
			&& A.bUseFiringSpread == B.bUseFiringSpread
			&& FMath::IsNearlyEqual(A.StationarySpreadAngle, B.StationarySpreadAngle, Tolerance)
			&& FMath::IsNearlyEqual(A.MovingSpreadAngle, B.MovingSpreadAngle, Tolerance)
			&& FMath::IsNearlyEqual(A.JumpSpreadAngle, B.JumpSpreadAngle, Tolerance)
			&& FMath::IsNearlyEqual(A.SlowWalkSpreadAngle, B.SlowWalkSpreadAngle, Tolerance)
			&& FMath::IsNearlyEqual(A.MovementSpreadSpeedThreshold, B.MovementSpreadSpeedThreshold, Tolerance)
			&& FMath::IsNearlyEqual(A.SpreadPerShot, B.SpreadPerShot, Tolerance)
			&& FMath::IsNearlyEqual(A.MaxFiringSpread, B.MaxFiringSpread, Tolerance)
			&& FMath::IsNearlyEqual(A.SpreadRecoveryDelay, B.SpreadRecoveryDelay, Tolerance)
			&& FMath::IsNearlyEqual(A.SpreadRecoverySpeed, B.SpreadRecoverySpeed, Tolerance);
	}

	struct FCameraValues
	{
		float ReturnSpeed;
		float SmoothKickDuration;
		float ImmediateKickFraction;
	};

	FCameraValues ReadCamera(const UWeaponAccuracyData& Accuracy)
	{
		return { Accuracy.GetCameraReturnSpeed(), Accuracy.GetSmoothKickDuration(), Accuracy.GetImmediateKickFraction() };
	}

	bool SameCamera(const FCameraValues& A, const FCameraValues& B)
	{
		return FMath::IsNearlyEqual(A.ReturnSpeed, B.ReturnSpeed, Tolerance)
			&& FMath::IsNearlyEqual(A.SmoothKickDuration, B.SmoothKickDuration, Tolerance)
			&& FMath::IsNearlyEqual(A.ImmediateKickFraction, B.ImmediateKickFraction, Tolerance);
	}

	FWeaponSpreadProfile MakeLegacyFiringProfile()
	{
		FWeaponSpreadProfile Profile;
		Profile.bUseAdditiveStateSpread = true;
		Profile.StationarySpreadAngle = 0.11f;
		Profile.MovingSpreadAngle = 2.2f;
		Profile.JumpSpreadAngle = 7.5f;
		Profile.SlowWalkSpreadAngle = 0.75f;
		Profile.MovementSpreadSpeedThreshold = 13.0f;
		Profile.bUseFiringSpread = true;
		Profile.SpreadPerShot = 0.031f;
		Profile.MaxFiringSpread = 0.42f;
		Profile.SpreadRecoveryDelay = 0.23f;
		Profile.SpreadRecoverySpeed = 1.9f;
		return Profile;
	}

	UWeaponAccuracyData* MakeAccuracy()
	{
		return NewObject<UWeaponAccuracyData>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponDirectNormalizePresetTest,
	"Dust2.Weapon.DirectSettings.NormalizePresetEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponDirectNormalizePresetTest::RunTest(const FString& Parameters)
{
	using namespace WeaponDirectSettingsTests;

	UWeaponAccuracyData* Accuracy = MakeAccuracy();
	UWeaponAccuracyPreset* PosturePreset = NewObject<UWeaponAccuracyPreset>();
	PosturePreset->Settings.MovingExtraAngle = 3.5f;
	PosturePreset->Settings.SlowWalkExtraAngle = 0.9f;
	PosturePreset->Settings.AirborneExtraAngle = 8.5f;
	PosturePreset->Settings.MovementThreshold = 10.0f;
	const FWeaponPostureSettings PresetSnapshot = PosturePreset->Settings;

	UWeaponCameraFeedbackPreset* CameraPreset = NewObject<UWeaponCameraFeedbackPreset>();
	CameraPreset->ReturnSpeed = 7.5f;
	CameraPreset->SmoothKickDuration = 0.08f;
	CameraPreset->ImmediateKickFraction = 0.4f;

	Accuracy->CameraFeedbackScale = 0.6f;
	Accuracy->CameraFeedbackPreset = CameraPreset;
	Accuracy->bOverrideCameraFeedback = false;

	Accuracy->HipAccuracy.PosturePreset = PosturePreset;
	Accuracy->HipAccuracy.bUsePostureSettings = true;
	Accuracy->HipAccuracy.bOverridePosture = false;
	Accuracy->HipAccuracy.BaseSpreadAngle = 0.35f;
	Accuracy->HipAccuracy.MaxFiringExtraAngle = 1.1f;
	Accuracy->HipAccuracy.ShotsToMaximum = 15;
	Accuracy->HipAccuracy.FullRecoverySeconds = 1.4f;
	Accuracy->HipAccuracy.bPreserveMigratedFiring = true;
	Accuracy->HipAccuracy.MigratedFiring = MakeLegacyFiringProfile();

	Accuracy->bUseAimingSpread = true;
	Accuracy->AimAccuracy.PosturePreset = PosturePreset;
	Accuracy->AimAccuracy.bUsePostureSettings = true;
	Accuracy->AimAccuracy.bOverridePosture = true;
	Accuracy->AimAccuracy.PostureOverride.MovingExtraAngle = 5.5f;
	Accuracy->AimAccuracy.PostureOverride.SlowWalkExtraAngle = 0.7f;
	Accuracy->AimAccuracy.PostureOverride.AirborneExtraAngle = 9.5f;
	Accuracy->AimAccuracy.PostureOverride.MovementThreshold = 10.0f;
	Accuracy->AimAccuracy.BaseSpreadAngle = 0.15f;

	const FWeaponPostureSettings HipPostureBefore = Accuracy->HipAccuracy.GetPosture(10.0f);
	const FWeaponPostureSettings AimPostureBefore = Accuracy->AimAccuracy.GetPosture(10.0f);
	const FWeaponSpreadProfile HipProfileBefore = Accuracy->HipAccuracy.BuildProfile(0.15f, 10.0f);
	const FWeaponSpreadProfile AimProfileBefore = Accuracy->AimAccuracy.BuildProfile(0.15f, 10.0f);
	const FCameraValues CameraBefore = ReadCamera(*Accuracy);
	const FWeaponSpreadProfile FiringSnapshot = Accuracy->HipAccuracy.MigratedFiring;

	Accuracy->NormalizeDirectParameters();

	TestTrue(TEXT("Hip 转为直接姿态覆盖"),
		Accuracy->HipAccuracy.bUsePostureSettings && Accuracy->HipAccuracy.bOverridePosture && Accuracy->HipAccuracy.PosturePreset == nullptr);
	TestTrue(TEXT("Aim 转为直接姿态覆盖"),
		Accuracy->AimAccuracy.bUsePostureSettings && Accuracy->AimAccuracy.bOverridePosture && Accuracy->AimAccuracy.PosturePreset == nullptr);
	TestTrue(TEXT("镜头反馈转为直接覆盖"),
		Accuracy->bOverrideCameraFeedback && Accuracy->CameraFeedbackPreset == nullptr);

	TestTrue(TEXT("Hip 覆盖值等于转换前生效姿态"), SamePosture(Accuracy->HipAccuracy.PostureOverride, HipPostureBefore));
	TestTrue(TEXT("Aim 覆盖值等于转换前生效姿态"), SamePosture(Accuracy->AimAccuracy.PostureOverride, AimPostureBefore));
	TestTrue(TEXT("Hip GetPosture 转换前后等价"), SamePosture(Accuracy->HipAccuracy.GetPosture(10.0f), HipPostureBefore));
	TestTrue(TEXT("Aim GetPosture 转换前后等价"), SamePosture(Accuracy->AimAccuracy.GetPosture(10.0f), AimPostureBefore));
	TestTrue(TEXT("Hip BuildProfile 转换前后等价"), SameProfile(Accuracy->HipAccuracy.BuildProfile(0.15f, 10.0f), HipProfileBefore));
	TestTrue(TEXT("Aim BuildProfile 转换前后等价"), SameProfile(Accuracy->AimAccuracy.BuildProfile(0.15f, 10.0f), AimProfileBefore));
	TestTrue(TEXT("镜头生效值转换前后等价"), SameCamera(ReadCamera(*Accuracy), CameraBefore));
	CheckClose(*this, TEXT("CameraFeedbackOverrides.ReturnSpeed"), Accuracy->CameraFeedbackOverrides.ReturnSpeed, CameraBefore.ReturnSpeed);
	CheckClose(*this, TEXT("CameraFeedbackOverrides.SmoothKickDuration"), Accuracy->CameraFeedbackOverrides.SmoothKickDuration, CameraBefore.SmoothKickDuration);
	CheckClose(*this, TEXT("CameraFeedbackOverrides.ImmediateKickFraction"), Accuracy->CameraFeedbackOverrides.ImmediateKickFraction, CameraBefore.ImmediateKickFraction);

	CheckClose(*this, TEXT("CameraFeedbackScale 保持"), Accuracy->CameraFeedbackScale, 0.6f);
	CheckClose(*this, TEXT("Hip.BaseSpreadAngle 保持"), Accuracy->HipAccuracy.BaseSpreadAngle, 0.35f);
	CheckClose(*this, TEXT("Hip.MaxFiringExtraAngle 保持"), Accuracy->HipAccuracy.MaxFiringExtraAngle, 1.1f);
	TestEqual(TEXT("Hip.ShotsToMaximum 保持"), Accuracy->HipAccuracy.ShotsToMaximum, 15);
	CheckClose(*this, TEXT("Hip.FullRecoverySeconds 保持"), Accuracy->HipAccuracy.FullRecoverySeconds, 1.4f);
	TestTrue(TEXT("MigratedFiring 未变"), SameProfile(Accuracy->HipAccuracy.MigratedFiring, FiringSnapshot));
	TestTrue(TEXT("bPreserveMigratedFiring 保持"), Accuracy->HipAccuracy.bPreserveMigratedFiring);

	TestTrue(TEXT("共用姿态预设对象未变"), SamePosture(PosturePreset->Settings, PresetSnapshot));
	CheckClose(*this, TEXT("共用镜头预设 ReturnSpeed 未变"), CameraPreset->ReturnSpeed, 7.5f);
	CheckClose(*this, TEXT("共用镜头预设 SmoothKickDuration 未变"), CameraPreset->SmoothKickDuration, 0.08f);
	CheckClose(*this, TEXT("共用镜头预设 ImmediateKickFraction 未变"), CameraPreset->ImmediateKickFraction, 0.4f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponDirectNormalizeIdempotentTest,
	"Dust2.Weapon.DirectSettings.NormalizeKeepsLocalOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponDirectNormalizeIdempotentTest::RunTest(const FString& Parameters)
{
	using namespace WeaponDirectSettingsTests;

	UWeaponAccuracyData* Accuracy = MakeAccuracy();
	UWeaponCameraFeedbackPreset* CameraPreset = NewObject<UWeaponCameraFeedbackPreset>();
	CameraPreset->ReturnSpeed = 7.5f;
	CameraPreset->SmoothKickDuration = 0.08f;
	CameraPreset->ImmediateKickFraction = 0.4f;

	UWeaponAccuracyPreset* PosturePreset = NewObject<UWeaponAccuracyPreset>();
	PosturePreset->Settings.MovingExtraAngle = 3.5f;
	PosturePreset->Settings.SlowWalkExtraAngle = 0.9f;
	PosturePreset->Settings.AirborneExtraAngle = 8.5f;
	PosturePreset->Settings.MovementThreshold = 10.0f;

	Accuracy->CameraFeedbackPreset = CameraPreset;
	Accuracy->bOverrideCameraFeedback = true;
	Accuracy->CameraFeedbackOverrides.ReturnSpeed = 3.0f;
	Accuracy->CameraFeedbackOverrides.SmoothKickDuration = 0.12f;
	Accuracy->CameraFeedbackOverrides.ImmediateKickFraction = 0.9f;

	Accuracy->HipAccuracy.PosturePreset = PosturePreset;
	Accuracy->HipAccuracy.bUsePostureSettings = true;
	Accuracy->HipAccuracy.bOverridePosture = true;
	Accuracy->HipAccuracy.PostureOverride.MovingExtraAngle = 1.25f;
	Accuracy->HipAccuracy.PostureOverride.SlowWalkExtraAngle = 0.3f;
	Accuracy->HipAccuracy.PostureOverride.AirborneExtraAngle = 2.5f;
	Accuracy->HipAccuracy.PostureOverride.MovementThreshold = 10.0f;

	const FWeaponPostureSettings HipOverrideBefore = Accuracy->HipAccuracy.GetPosture(10.0f);
	Accuracy->NormalizeDirectParameters();

	CheckClose(*this, TEXT("已开覆盖时复制覆盖而非预设 ReturnSpeed"), Accuracy->CameraFeedbackOverrides.ReturnSpeed, 3.0f);
	CheckClose(*this, TEXT("已开覆盖时复制覆盖而非预设 SmoothKickDuration"), Accuracy->CameraFeedbackOverrides.SmoothKickDuration, 0.12f);
	CheckClose(*this, TEXT("已开覆盖时复制覆盖而非预设 ImmediateKickFraction"), Accuracy->CameraFeedbackOverrides.ImmediateKickFraction, 0.9f);
	CheckClose(*this, TEXT("已开覆盖时姿态取覆盖 MovingExtraAngle"), Accuracy->HipAccuracy.PostureOverride.MovingExtraAngle, 1.25f);
	TestTrue(TEXT("覆盖值保留且预设引用清空"),
		Accuracy->CameraFeedbackPreset == nullptr && Accuracy->HipAccuracy.PosturePreset == nullptr
		&& SamePosture(Accuracy->HipAccuracy.PostureOverride, HipOverrideBefore));

	Accuracy->CameraFeedbackOverrides.ReturnSpeed = 5.0f;
	Accuracy->HipAccuracy.PostureOverride.MovingExtraAngle = 4.75f;
	Accuracy->NormalizeDirectParameters();

	CheckClose(*this, TEXT("再次转换不重置本地编辑 ReturnSpeed"), Accuracy->GetCameraReturnSpeed(), 5.0f);
	CheckClose(*this, TEXT("再次转换不重置本地编辑姿态"), Accuracy->HipAccuracy.GetPosture(10.0f).MovingExtraAngle, 4.75f);
	CheckClose(*this, TEXT("再次转换后覆盖 ReturnSpeed"), Accuracy->CameraFeedbackOverrides.ReturnSpeed, 5.0f);
	CheckClose(*this, TEXT("再次转换后覆盖姿态"), Accuracy->HipAccuracy.PostureOverride.MovingExtraAngle, 4.75f);
	CheckClose(*this, TEXT("共用镜头预设对象未变"), CameraPreset->ReturnSpeed, 7.5f);
	TestTrue(TEXT("镜头已无预设引用"), Accuracy->CameraFeedbackPreset == nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponDirectNormalizeLegacyTest,
	"Dust2.Weapon.DirectSettings.NormalizeLegacyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponDirectNormalizeLegacyTest::RunTest(const FString& Parameters)
{
	using namespace WeaponDirectSettingsTests;

	UWeaponAccuracyData* Accuracy = MakeAccuracy();
	Accuracy->CameraFeedbackScale = 0.45f;
	Accuracy->CameraFeedbackPreset = nullptr;
	Accuracy->bOverrideCameraFeedback = false;
	Accuracy->CameraReturnSpeed = 33.0f;
	Accuracy->SmoothKickDuration = 0.17f;
	Accuracy->ImmediateKickFraction = 0.6f;

	Accuracy->HipAccuracy.PosturePreset = nullptr;
	Accuracy->HipAccuracy.bUsePostureSettings = false;
	Accuracy->HipAccuracy.bOverridePosture = false;
	Accuracy->HipAccuracy.MovingExtraAngle = 2.75f;
	Accuracy->HipAccuracy.SlowWalkExtraAngle = 0.65f;
	Accuracy->HipAccuracy.AirborneExtraAngle = 7.25f;
	Accuracy->HipAccuracy.BaseSpreadAngle = 0.28f;
	Accuracy->HipAccuracy.MaxFiringExtraAngle = 0.85f;
	Accuracy->HipAccuracy.ShotsToMaximum = 14;
	Accuracy->HipAccuracy.FullRecoverySeconds = 1.35f;
	Accuracy->HipAccuracy.bPreserveMigratedFiring = true;
	Accuracy->HipAccuracy.MigratedFiring = MakeLegacyFiringProfile();

	Accuracy->bUseAimingSpread = true;
	Accuracy->AimAccuracy.PosturePreset = nullptr;
	Accuracy->AimAccuracy.bUsePostureSettings = false;
	Accuracy->AimAccuracy.MovingExtraAngle = 4.25f;
	Accuracy->AimAccuracy.SlowWalkExtraAngle = 0.85f;
	Accuracy->AimAccuracy.AirborneExtraAngle = 9.5f;
	Accuracy->AimAccuracy.BaseSpreadAngle = 0.22f;

	const FWeaponPostureSettings HipBefore = Accuracy->HipAccuracy.GetPosture(23.0f);
	const FWeaponSpreadProfile FiringSnapshot = Accuracy->HipAccuracy.MigratedFiring;

	Accuracy->NormalizeDirectParameters(23.0f);

	TestTrue(TEXT("无预设姿态转为覆盖"), Accuracy->HipAccuracy.bUsePostureSettings && Accuracy->HipAccuracy.bOverridePosture);
	TestTrue(TEXT("Hip 覆盖等于旧 GetPosture(23)"), SamePosture(Accuracy->HipAccuracy.PostureOverride, HipBefore));
	CheckClose(*this, TEXT("Hip 旧姿态 MovingExtraAngle 精确保留"), Accuracy->HipAccuracy.PostureOverride.MovingExtraAngle, 2.75f, 0.0f);
	CheckClose(*this, TEXT("Hip 旧姿态 SlowWalkExtraAngle 精确保留"), Accuracy->HipAccuracy.PostureOverride.SlowWalkExtraAngle, 0.65f, 0.0f);
	CheckClose(*this, TEXT("Hip 旧姿态 AirborneExtraAngle 精确保留"), Accuracy->HipAccuracy.PostureOverride.AirborneExtraAngle, 7.25f, 0.0f);
	CheckClose(*this, TEXT("Hip 非默认阈值 23 精确保留"), Accuracy->HipAccuracy.PostureOverride.MovementThreshold, 23.0f, 0.0f);
	CheckClose(*this, TEXT("Hip 旧 float 字段本身未改"), Accuracy->HipAccuracy.MovingExtraAngle, 2.75f, 0.0f);
	CheckClose(*this, TEXT("Aim 非默认阈值 23 精确保留"), Accuracy->AimAccuracy.PostureOverride.MovementThreshold, 23.0f, 0.0f);
	TestTrue(TEXT("Aim 旧姿态转覆盖"), Accuracy->AimAccuracy.bUsePostureSettings && Accuracy->AimAccuracy.bOverridePosture);

	TestTrue(TEXT("无预设镜头转为覆盖"), Accuracy->bOverrideCameraFeedback && Accuracy->CameraFeedbackPreset == nullptr);
	CheckClose(*this, TEXT("旧 fallback ReturnSpeed 转覆盖"), Accuracy->CameraFeedbackOverrides.ReturnSpeed, 33.0f);
	CheckClose(*this, TEXT("旧 fallback SmoothKickDuration 转覆盖"), Accuracy->CameraFeedbackOverrides.SmoothKickDuration, 0.17f);
	CheckClose(*this, TEXT("旧 fallback ImmediateKickFraction 转覆盖"), Accuracy->CameraFeedbackOverrides.ImmediateKickFraction, 0.6f);
	CheckClose(*this, TEXT("转换后 GetCameraReturnSpeed"), Accuracy->GetCameraReturnSpeed(), 33.0f);
	CheckClose(*this, TEXT("转换后 GetSmoothKickDuration"), Accuracy->GetSmoothKickDuration(), 0.17f);
	CheckClose(*this, TEXT("转换后 GetImmediateKickFraction"), Accuracy->GetImmediateKickFraction(), 0.6f);

	CheckClose(*this, TEXT("CameraFeedbackScale 保持"), Accuracy->CameraFeedbackScale, 0.45f, 0.0f);
	CheckClose(*this, TEXT("Hip.BaseSpreadAngle 保持"), Accuracy->HipAccuracy.BaseSpreadAngle, 0.28f, 0.0f);
	CheckClose(*this, TEXT("Hip.MaxFiringExtraAngle 保持"), Accuracy->HipAccuracy.MaxFiringExtraAngle, 0.85f, 0.0f);
	TestEqual(TEXT("Hip.ShotsToMaximum 保持"), Accuracy->HipAccuracy.ShotsToMaximum, 14);
	CheckClose(*this, TEXT("Hip.FullRecoverySeconds 保持"), Accuracy->HipAccuracy.FullRecoverySeconds, 1.35f, 0.0f);
	CheckClose(*this, TEXT("Aim.BaseSpreadAngle 保持"), Accuracy->AimAccuracy.BaseSpreadAngle, 0.22f, 0.0f);
	TestTrue(TEXT("MigratedFiring 全部字段未变"), SameProfile(Accuracy->HipAccuracy.MigratedFiring, FiringSnapshot));
	TestTrue(TEXT("bPreserveMigratedFiring 保持"), Accuracy->HipAccuracy.bPreserveMigratedFiring);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
