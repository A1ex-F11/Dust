#include "Misc/AutomationTest.h"
#include "WeaponAccuracyPreset.h"
#include "WeaponCameraFeedbackPreset.h"
#include "WeaponDefinitionData.h"
#include "WeaponPresetReferenceCleanup.h"
#include "WeaponRecoilData.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace WeaponPresetCleanupTestHelpers
{
	bool Near(float A, float B) { return FMath::IsNearlyEqual(A, B); }

	bool SamePosture(const FWeaponPostureSettings& A, const FWeaponPostureSettings& B)
	{
		return Near(A.MovingExtraAngle, B.MovingExtraAngle) && Near(A.SlowWalkExtraAngle, B.SlowWalkExtraAngle)
			&& Near(A.AirborneExtraAngle, B.AirborneExtraAngle) && Near(A.MovementThreshold, B.MovementThreshold);
	}

	bool SameProfile(const FWeaponSpreadProfile& A, const FWeaponSpreadProfile& B)
	{
		return A.bUseAdditiveStateSpread == B.bUseAdditiveStateSpread && Near(A.StationarySpreadAngle, B.StationarySpreadAngle)
			&& Near(A.MovingSpreadAngle, B.MovingSpreadAngle) && Near(A.JumpSpreadAngle, B.JumpSpreadAngle)
			&& Near(A.SlowWalkSpreadAngle, B.SlowWalkSpreadAngle) && Near(A.MovementSpreadSpeedThreshold, B.MovementSpreadSpeedThreshold)
			&& A.bUseFiringSpread == B.bUseFiringSpread && Near(A.SpreadPerShot, B.SpreadPerShot)
			&& Near(A.MaxFiringSpread, B.MaxFiringSpread) && Near(A.SpreadRecoveryDelay, B.SpreadRecoveryDelay)
			&& Near(A.SpreadRecoverySpeed, B.SpreadRecoverySpeed);
	}

	UWeaponAccuracyPreset* NewAccuracyPreset(float Moving, float SlowWalk, float Airborne, float Threshold)
	{
		UWeaponAccuracyPreset* Preset = NewObject<UWeaponAccuracyPreset>();
		Preset->Settings.MovingExtraAngle = Moving; Preset->Settings.SlowWalkExtraAngle = SlowWalk; Preset->Settings.AirborneExtraAngle = Airborne; Preset->Settings.MovementThreshold = Threshold;
		return Preset;
	}

	UWeaponCameraFeedbackPreset* NewCameraPreset(float ReturnSpeed, float SmoothKick, float Immediate)
	{
		UWeaponCameraFeedbackPreset* Preset = NewObject<UWeaponCameraFeedbackPreset>();
		Preset->ReturnSpeed = ReturnSpeed; Preset->SmoothKickDuration = SmoothKick; Preset->ImmediateKickFraction = Immediate;
		return Preset;
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresetCleanupPostureTest,
	"Dust2.WeaponPresetCleanup.PostureLocalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPresetCleanupPostureTest::RunTest(const FString& Parameters)
{
	using namespace WeaponPresetCleanupTestHelpers;
	// 1) Preset is effective; migrated compatibility data and flags must survive.
	{
		UWeaponAccuracyPreset* Preset = NewAccuracyPreset(3.5f, 1.25f, 7.5f, 22.0f);
		FWeaponIntuitiveAccuracy Accuracy;
		Accuracy.PosturePreset = Preset; Accuracy.bUsePostureSettings = true; Accuracy.bPreserveMigratedFiring = true;
		Accuracy.BaseSpreadAngle = 0.33f; Accuracy.ShotsToMaximum = 9; Accuracy.FullRecoverySeconds = 1.05f;
		Accuracy.MigratedFiring.bUseAdditiveStateSpread = true;
		Accuracy.MigratedFiring.SpreadPerShot = 0.31f; Accuracy.MigratedFiring.MaxFiringSpread = 1.4f;
		const FWeaponPostureSettings PresetBefore = Preset->Settings; const FWeaponSpreadProfile MigratedBefore = Accuracy.MigratedFiring;
		const FWeaponPostureSettings PostureBefore = Accuracy.GetPosture(10.0f);
		const FWeaponSpreadProfile ProfileBefore = Accuracy.BuildProfile(0.12f, 10.0f);
		TestEqual(TEXT("Posture/preset is effective"), PostureBefore.MovingExtraAngle, 3.5f);
		TestTrue(TEXT("Posture/preset localizes"), DustWeaponPresetCleanup::LocalizePosture(Accuracy, 10.0f));
		TestTrue(TEXT("Posture/preset posture kept"), SamePosture(PostureBefore, Accuracy.GetPosture(10.0f)));
		TestTrue(TEXT("Posture/preset profile kept"), SameProfile(ProfileBefore, Accuracy.BuildProfile(0.12f, 10.0f)));
		TestTrue(TEXT("Posture/preset local copy"), SamePosture(PostureBefore, Accuracy.PostureOverride));
		TestTrue(TEXT("Posture/preset detached"), Accuracy.PosturePreset == nullptr && Accuracy.bOverridePosture && Accuracy.bUsePostureSettings);
		TestTrue(TEXT("Posture/preset source unchanged"), SamePosture(PresetBefore, Preset->Settings));
		TestTrue(TEXT("Posture/migrated kept"), Accuracy.bPreserveMigratedFiring && SameProfile(MigratedBefore, Accuracy.MigratedFiring));
		TestTrue(TEXT("Posture/scalars kept"), Near(Accuracy.BaseSpreadAngle, 0.33f) && Accuracy.ShotsToMaximum == 9 && Near(Accuracy.FullRecoverySeconds, 1.05f));
		Accuracy.PostureOverride.SlowWalkExtraAngle = 33.25f;
		TestFalse(TEXT("Posture/second call is false"), DustWeaponPresetCleanup::LocalizePosture(Accuracy, 10.0f));
		TestEqual(TEXT("Posture/second call keeps manual value"), Accuracy.PostureOverride.SlowWalkExtraAngle, 33.25f);
	}
	// 2) Override is already active: detaching the preset must not change effective values.
	{
		UWeaponAccuracyPreset* Preset = NewAccuracyPreset(2.0f, 0.75f, 5.0f, 14.0f);
		FWeaponIntuitiveAccuracy Accuracy;
		Accuracy.PosturePreset = Preset; Accuracy.bUsePostureSettings = true; Accuracy.bOverridePosture = true;
		Accuracy.PostureOverride.MovingExtraAngle = 4.5f; Accuracy.PostureOverride.SlowWalkExtraAngle = 1.5f;
		Accuracy.PostureOverride.AirborneExtraAngle = 8.0f; Accuracy.PostureOverride.MovementThreshold = 18.0f;
		const FWeaponPostureSettings PresetBefore = Preset->Settings; const FWeaponPostureSettings PostureBefore = Accuracy.GetPosture(10.0f);
		const FWeaponSpreadProfile ProfileBefore = Accuracy.BuildProfile(0.12f, 10.0f);
		TestEqual(TEXT("Posture/override is effective"), PostureBefore.MovingExtraAngle, 4.5f);
		TestTrue(TEXT("Posture/override localizes"), DustWeaponPresetCleanup::LocalizePosture(Accuracy, 10.0f));
		TestTrue(TEXT("Posture/override values kept"), SamePosture(PostureBefore, Accuracy.GetPosture(10.0f)));
		TestTrue(TEXT("Posture/override profile kept"), SameProfile(ProfileBefore, Accuracy.BuildProfile(0.12f, 10.0f)));
		TestTrue(TEXT("Posture/override detached"), Accuracy.PosturePreset == nullptr && Accuracy.bOverridePosture);
		TestTrue(TEXT("Posture/override source unchanged"), SamePosture(PresetBefore, Preset->Settings));
	}
	// 3) Legacy flag is off but a stale preset is still serialized.
	{
		UWeaponAccuracyPreset* Preset = NewAccuracyPreset(3.0f, 1.0f, 6.0f, 20.0f);
		FWeaponIntuitiveAccuracy Accuracy;
		Accuracy.PosturePreset = Preset; Accuracy.bUsePostureSettings = false; Accuracy.bOverridePosture = false;
		Accuracy.PostureOverride.MovingExtraAngle = 99.0f;
		Accuracy.MovingExtraAngle = 2.75f; Accuracy.SlowWalkExtraAngle = 0.9f; Accuracy.AirborneExtraAngle = 6.5f;
		const FWeaponPostureSettings PresetBefore = Preset->Settings; const FWeaponPostureSettings PostureBefore = Accuracy.GetPosture(10.0f);
		const FWeaponSpreadProfile ProfileBefore = Accuracy.BuildProfile(0.12f, 10.0f);
		TestTrue(TEXT("Posture/stale preset localizes"), DustWeaponPresetCleanup::LocalizePosture(Accuracy, 10.0f));
		TestTrue(TEXT("Posture/stale preset values kept"), SamePosture(PostureBefore, Accuracy.GetPosture(10.0f)));
		TestTrue(TEXT("Posture/stale preset profile kept"), SameProfile(ProfileBefore, Accuracy.BuildProfile(0.12f, 10.0f)));
		TestTrue(TEXT("Posture/legacy detached"), Accuracy.PosturePreset == nullptr && Accuracy.bOverridePosture && Accuracy.bUsePostureSettings);
		TestTrue(TEXT("Posture/legacy source unchanged"), SamePosture(PresetBefore, Preset->Settings));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresetCleanupCameraTest,
	"Dust2.WeaponPresetCleanup.CameraLocalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPresetCleanupCameraTest::RunTest(const FString& Parameters)
{
	using namespace WeaponPresetCleanupTestHelpers;
	// 1) Preset is effective; unrelated recoil/camera data must keep its configuration.
	{
		UWeaponCameraFeedbackPreset* Preset = NewCameraPreset(9.5f, 0.12f, 0.6f);
		UWeaponRecoilData* Recoil = NewObject<UWeaponRecoilData>();
		Recoil->CameraFeedbackPreset = Preset; Recoil->bOverrideCameraFeedback = false;
		Recoil->CameraFeedbackScale = 0.37f; Recoil->AimingRecoilScale = 0.8f;
		Recoil->bUseFixedPattern = true; Recoil->FixedPattern = {FVector2D(0.4f, 0.1f), FVector2D(0.5f, -0.2f)};
		Recoil->PatternResetDelay = 0.65f; Recoil->FixedPatternPitchScale = 1.2f;
		Recoil->FixedPatternYawScale = 0.9f; Recoil->LoopStartIndex = 1;
		Recoil->ShotRecoilRecoverySpeed = 7.5f; Recoil->RecoveryDelay = 0.18f;
		Recoil->RecoverySpeed = 6.5f; Recoil->RecoveryPitchRatio = 0.55f;
		Recoil->CameraReturnSpeed = 13.5f; Recoil->SmoothKickDuration = 0.08f; Recoil->ImmediateKickFraction = 0.35f;
		const float ReturnBefore = Recoil->GetCameraReturnSpeed(); const float SmoothBefore = Recoil->GetSmoothKickDuration();
		const float ImmediateBefore = Recoil->GetImmediateKickFraction();
		TestEqual(TEXT("Camera/preset is effective"), ReturnBefore, 9.5f);
		TestTrue(TEXT("Camera/preset localizes"), DustWeaponPresetCleanup::LocalizeCamera(*Recoil));
		TestEqual(TEXT("Camera/preset return speed kept"), Recoil->GetCameraReturnSpeed(), ReturnBefore);
		TestEqual(TEXT("Camera/preset smooth kick kept"), Recoil->GetSmoothKickDuration(), SmoothBefore);
		TestEqual(TEXT("Camera/preset immediate fraction kept"), Recoil->GetImmediateKickFraction(), ImmediateBefore);
		TestTrue(TEXT("Camera/preset overrides written"),
			Near(Recoil->CameraFeedbackOverrides.ReturnSpeed, 9.5f) && Near(Recoil->CameraFeedbackOverrides.SmoothKickDuration, 0.12f)
			&& Near(Recoil->CameraFeedbackOverrides.ImmediateKickFraction, 0.6f));
		TestTrue(TEXT("Camera/preset detached"), Recoil->CameraFeedbackPreset == nullptr && Recoil->bOverrideCameraFeedback);
		TestTrue(TEXT("Camera/preset source unchanged"),
			Near(Preset->ReturnSpeed, 9.5f) && Near(Preset->SmoothKickDuration, 0.12f) && Near(Preset->ImmediateKickFraction, 0.6f));
		TestTrue(TEXT("Camera/fixed sequence kept"),
			Recoil->FixedPattern.Num() == 2 && Recoil->FixedPattern[0].Equals(FVector2D(0.4f, 0.1f))
			&& Recoil->FixedPattern[1].Equals(FVector2D(0.5f, -0.2f)) && Near(Recoil->PatternResetDelay, 0.65f)
			&& Near(Recoil->FixedPatternPitchScale, 1.2f) && Near(Recoil->FixedPatternYawScale, 0.9f) && Recoil->LoopStartIndex == 1);
		TestTrue(TEXT("Camera/aiming, scale and recovery kept"),
			Near(Recoil->AimingRecoilScale, 0.8f) && Near(Recoil->CameraFeedbackScale, 0.37f) && Near(Recoil->ShotRecoilRecoverySpeed, 7.5f)
			&& Near(Recoil->RecoveryDelay, 0.18f) && Near(Recoil->RecoverySpeed, 6.5f) && Near(Recoil->RecoveryPitchRatio, 0.55f));
		TestTrue(TEXT("Camera/legacy fallback kept"),
			Near(Recoil->CameraReturnSpeed, 13.5f) && Near(Recoil->SmoothKickDuration, 0.08f) && Near(Recoil->ImmediateKickFraction, 0.35f));
		Recoil->CameraFeedbackOverrides.ReturnSpeed = 21.0f;
		TestFalse(TEXT("Camera/second call is false"), DustWeaponPresetCleanup::LocalizeCamera(*Recoil));
		TestEqual(TEXT("Camera/second call keeps manual value"), Recoil->CameraFeedbackOverrides.ReturnSpeed, 21.0f);
	}
	// 2) Override is already enabled and takes priority over the preset.
	{
		UWeaponCameraFeedbackPreset* Preset = NewCameraPreset(2.0f, 0.3f, 0.1f);
		UWeaponRecoilData* Recoil = NewObject<UWeaponRecoilData>();
		Recoil->CameraFeedbackPreset = Preset; Recoil->bOverrideCameraFeedback = true;
		Recoil->CameraFeedbackOverrides.ReturnSpeed = 4.25f; Recoil->CameraFeedbackOverrides.SmoothKickDuration = 0.22f;
		Recoil->CameraFeedbackOverrides.ImmediateKickFraction = 0.75f;
		const float ReturnBefore = Recoil->GetCameraReturnSpeed(); const float SmoothBefore = Recoil->GetSmoothKickDuration();
		const float ImmediateBefore = Recoil->GetImmediateKickFraction();
		TestEqual(TEXT("Camera/override is effective"), ReturnBefore, 4.25f);
		TestTrue(TEXT("Camera/override localizes"), DustWeaponPresetCleanup::LocalizeCamera(*Recoil));
		TestEqual(TEXT("Camera/override return speed kept"), Recoil->GetCameraReturnSpeed(), ReturnBefore);
		TestEqual(TEXT("Camera/override smooth kick kept"), Recoil->GetSmoothKickDuration(), SmoothBefore);
		TestEqual(TEXT("Camera/override immediate fraction kept"), Recoil->GetImmediateKickFraction(), ImmediateBefore);
		TestTrue(TEXT("Camera/override detached"), Recoil->CameraFeedbackPreset == nullptr && Recoil->bOverrideCameraFeedback);
		TestTrue(TEXT("Camera/override source unchanged"),
			Near(Preset->ReturnSpeed, 2.0f) && Near(Preset->SmoothKickDuration, 0.3f) && Near(Preset->ImmediateKickFraction, 0.1f));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresetCleanupNoPresetTest,
	"Dust2.WeaponPresetCleanup.NoPresetIsNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPresetCleanupNoPresetTest::RunTest(const FString& Parameters)
{
	using namespace WeaponPresetCleanupTestHelpers;
	// No preset must return false and keep every local value, including override flags.
	{
		FWeaponIntuitiveAccuracy Accuracy;
		Accuracy.PosturePreset = nullptr; Accuracy.bOverridePosture = true; Accuracy.bUsePostureSettings = false;
		Accuracy.BaseSpreadAngle = 0.91f; Accuracy.MovingExtraAngle = 4.4f; Accuracy.SlowWalkExtraAngle = 1.9f;
		Accuracy.AirborneExtraAngle = 9.9f; Accuracy.MaxFiringExtraAngle = 1.3f; Accuracy.ShotsToMaximum = 5;
		Accuracy.FullRecoverySeconds = 0.77f; Accuracy.bPreserveMigratedFiring = true;
		Accuracy.PostureOverride.MovingExtraAngle = 7.7f; Accuracy.PostureOverride.SlowWalkExtraAngle = 2.2f;
		Accuracy.PostureOverride.AirborneExtraAngle = 11.0f; Accuracy.PostureOverride.MovementThreshold = 27.0f;
		Accuracy.MigratedFiring.bUseFiringSpread = true; Accuracy.MigratedFiring.SpreadPerShot = 0.44f; Accuracy.MigratedFiring.MaxFiringSpread = 2.1f;
		const FWeaponPostureSettings PostureBefore = Accuracy.PostureOverride; const FWeaponSpreadProfile MigratedBefore = Accuracy.MigratedFiring;
		TestFalse(TEXT("NoPreset/posture is false"), DustWeaponPresetCleanup::LocalizePosture(Accuracy, 15.0f));
		TestNull(TEXT("NoPreset/posture stays null"), Accuracy.PosturePreset.Get());
		TestTrue(TEXT("NoPreset/posture flags kept"), Accuracy.bOverridePosture && !Accuracy.bUsePostureSettings && Accuracy.bPreserveMigratedFiring);
		TestTrue(TEXT("NoPreset/posture local override kept"), SamePosture(PostureBefore, Accuracy.PostureOverride));
		TestTrue(TEXT("NoPreset/migrated profile kept"), SameProfile(MigratedBefore, Accuracy.MigratedFiring));
		TestTrue(TEXT("NoPreset/posture scalars kept"),
			Near(Accuracy.BaseSpreadAngle, 0.91f) && Near(Accuracy.MovingExtraAngle, 4.4f) && Near(Accuracy.SlowWalkExtraAngle, 1.9f)
			&& Near(Accuracy.AirborneExtraAngle, 9.9f) && Near(Accuracy.MaxFiringExtraAngle, 1.3f)
			&& Accuracy.ShotsToMaximum == 5 && Near(Accuracy.FullRecoverySeconds, 0.77f));
	}
	{
		UWeaponRecoilData* Recoil = NewObject<UWeaponRecoilData>();
		Recoil->CameraFeedbackPreset = nullptr; Recoil->bOverrideCameraFeedback = true;
		Recoil->CameraFeedbackOverrides.ReturnSpeed = 5.5f; Recoil->CameraFeedbackOverrides.SmoothKickDuration = 0.19f;
		Recoil->CameraFeedbackOverrides.ImmediateKickFraction = 0.65f;
		Recoil->CameraFeedbackScale = 0.66f; Recoil->AimingRecoilScale = 0.7f; Recoil->bUseFixedPattern = true;
		Recoil->FixedPattern = {FVector2D(0.2f, 0.05f)}; Recoil->RecoveryDelay = 0.25f;
		Recoil->RecoverySpeed = 5.5f; Recoil->RecoveryPitchRatio = 0.65f;
		Recoil->CameraReturnSpeed = 14.5f; Recoil->SmoothKickDuration = 0.11f; Recoil->ImmediateKickFraction = 0.45f;
		TestFalse(TEXT("NoPreset/camera is false"), DustWeaponPresetCleanup::LocalizeCamera(*Recoil));
		TestNull(TEXT("NoPreset/camera stays null"), Recoil->CameraFeedbackPreset.Get());
		TestTrue(TEXT("NoPreset/camera flag kept"), Recoil->bOverrideCameraFeedback);
		TestTrue(TEXT("NoPreset/camera local override kept"),
			Near(Recoil->CameraFeedbackOverrides.ReturnSpeed, 5.5f) && Near(Recoil->CameraFeedbackOverrides.SmoothKickDuration, 0.19f)
			&& Near(Recoil->CameraFeedbackOverrides.ImmediateKickFraction, 0.65f));
		TestTrue(TEXT("NoPreset/camera data kept"),
			Near(Recoil->CameraFeedbackScale, 0.66f) && Near(Recoil->AimingRecoilScale, 0.7f) && Recoil->bUseFixedPattern
			&& Recoil->FixedPattern.Num() == 1 && Recoil->FixedPattern[0].Equals(FVector2D(0.2f, 0.05f))
			&& Near(Recoil->RecoveryDelay, 0.25f) && Near(Recoil->RecoverySpeed, 5.5f) && Near(Recoil->RecoveryPitchRatio, 0.65f)
			&& Near(Recoil->CameraReturnSpeed, 14.5f) && Near(Recoil->SmoothKickDuration, 0.11f) && Near(Recoil->ImmediateKickFraction, 0.45f));
	}
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
