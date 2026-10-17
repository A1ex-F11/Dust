#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "FireMode.h"
#include "WeaponAccuracyPreset.h"
#include "WeaponCameraFeedbackPreset.h"
#include "WeaponDefinitionData.h"
#include "WeaponRecoilData.h"

namespace
{
	static UWeaponDefinitionData* MakeLegacyDefinition()
	{
		UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
		Definition->ConfigurationVersion = 0;
		Definition->AllowedFireModes = { EFireMode::SemiAutomatic, EFireMode::FullAutomatic, EFireMode::Burst };
		Definition->DefaultFireMode = EFireMode::SemiAutomatic;
		Definition->MagazineCapacity = 30;
		Definition->InitialMagazineAmmo = 17;
		Definition->InitialReserveAmmo = 63;
		Definition->StationarySpreadAngle = 10.0f;
		Definition->MovingSpreadAngle = 4.0f;
		Definition->SlowWalkSpreadAngle = 7.0f;
		Definition->JumpSpreadAngle = 15.0f;
		Definition->MovementSpreadSpeedThreshold = 12.0f;
		return Definition;
	}

	static bool NearlyEqual(float A, float B)
	{
		return FMath::IsNearlyEqual(A, B, 0.0001f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponConfigurationFusionMigrationTest,
	"Dust2.ConfigurationFusion.FireModeReloadAndAmmoMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponConfigurationFusionMigrationTest::RunTest(const FString& Parameters)
{
	UWeaponDefinitionData* Definition = MakeLegacyDefinition();
	Definition->DefaultFireMode = EFireMode::FullAutomatic;
	Definition->AllowedFireModes = { EFireMode::SemiAutomatic, EFireMode::FullAutomatic, EFireMode::Burst };
	Definition->bLockReloadAmountAtStart = true;
	Definition->bReloadOneRoundAtATime = false;
	Definition->bUseBatchReloadForFullEmptyReload = true;

	Definition->MigrateConfiguration();
	TestTrue(TEXT("The prior default becomes the first selectable fire mode"),
		Definition->GetInitialFireMode() == EFireMode::FullAutomatic);
	TestTrue(TEXT("Rotating the list preserves the old circular mode order"),
		Definition->AllowedFireModes.Num() == 3 &&
		Definition->AllowedFireModes[0] == EFireMode::FullAutomatic &&
		Definition->AllowedFireModes[1] == EFireMode::Burst &&
		Definition->AllowedFireModes[2] == EFireMode::SemiAutomatic);

	TestFalse(TEXT("Magazine reload remains magazine reload"), Definition->UsesPerRoundReload());
	TestFalse(TEXT("The legacy batch flag has no effect without per-round reload"), Definition->UsesEmptyBatchReload());
	TestEqual(TEXT("Capacity remains an independent value"), Definition->MagazineCapacity, 30);
	TestEqual(TEXT("A deliberately partial initial magazine is preserved"), Definition->InitialMagazineAmmo, 17);
	TestEqual(TEXT("Initial reserve ammo is preserved"), Definition->InitialReserveAmmo, 63);
	TestTrue(TEXT("Reload amount locking remains preserved"), Definition->bLockReloadAmountAtStart);

	UWeaponDefinitionData* PerRound = MakeLegacyDefinition();
	PerRound->bReloadOneRoundAtATime = true;
	PerRound->bUseBatchReloadForFullEmptyReload = false;
	PerRound->MigrateConfiguration();
	TestTrue(TEXT("逐颗 legacy reload maps to per-round style"), PerRound->UsesPerRoundReload());
	TestFalse(TEXT("Per-round style does not enable empty-magazine batch loading"), PerRound->UsesEmptyBatchReload());

	UWeaponDefinitionData* PerRoundWithEmptyBatch = MakeLegacyDefinition();
	PerRoundWithEmptyBatch->bReloadOneRoundAtATime = true;
	PerRoundWithEmptyBatch->bUseBatchReloadForFullEmptyReload = true;
	PerRoundWithEmptyBatch->MigrateConfiguration();
	TestTrue(TEXT("Per-round with the empty-magazine exception remains per-round"),
		PerRoundWithEmptyBatch->UsesPerRoundReload());
	TestTrue(TEXT("The empty-magazine batch exception is preserved"),
		PerRoundWithEmptyBatch->UsesEmptyBatchReload());

	Definition->HipAccuracy.BaseSpreadAngle = 3.25f;
	Definition->ReloadStyle = EWeaponReloadStyle::PerRound;
	Definition->MigrateConfiguration();
	TestTrue(TEXT("Migration is idempotent and does not overwrite new accuracy edits"),
		NearlyEqual(Definition->HipAccuracy.BaseSpreadAngle, 3.25f));
	TestTrue(TEXT("Migration is idempotent and does not overwrite the edited reload mode"),
		Definition->ReloadStyle == EWeaponReloadStyle::PerRound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponConfigurationFusionSharedPostureTest,
	"Dust2.ConfigurationFusion.SharedPostureAndPerWeaponOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponConfigurationFusionSharedPostureTest::RunTest(const FString& Parameters)
{
	UWeaponAccuracyPreset* SharedPreset = NewObject<UWeaponAccuracyPreset>(GetTransientPackage());
	SharedPreset->Settings.MovingExtraAngle = 2.25f;
	SharedPreset->Settings.SlowWalkExtraAngle = 0.6f;
	SharedPreset->Settings.AirborneExtraAngle = 8.0f;
	SharedPreset->Settings.MovementThreshold = 14.0f;

	UWeaponDefinitionData* Rifle = MakeLegacyDefinition();
	UWeaponDefinitionData* SMG = MakeLegacyDefinition();
	Rifle->bUseIntuitiveHipAccuracy = true;
	SMG->bUseIntuitiveHipAccuracy = true;
	Rifle->HipAccuracy.PosturePreset = SharedPreset;
	SMG->HipAccuracy.PosturePreset = SharedPreset;
	Rifle->MigrateConfiguration();
	SMG->MigrateConfiguration();

	const FWeaponPostureSettings RifleInitial = Rifle->HipAccuracy.GetPosture(10.0f);
	const FWeaponPostureSettings SMGInitial = SMG->HipAccuracy.GetPosture(10.0f);
	TestTrue(TEXT("Both weapons resolve the shared movement rule"),
		NearlyEqual(RifleInitial.MovingExtraAngle, 2.25f) && NearlyEqual(SMGInitial.MovingExtraAngle, 2.25f));

	SharedPreset->Settings.MovingExtraAngle = 3.5f;
	const FWeaponPostureSettings RifleUpdated = Rifle->HipAccuracy.GetPosture(10.0f);
	const FWeaponPostureSettings SMGUpdated = SMG->HipAccuracy.GetPosture(10.0f);
	TestTrue(TEXT("Editing the shared preset updates both weapons"),
		NearlyEqual(RifleUpdated.MovingExtraAngle, 3.5f) && NearlyEqual(SMGUpdated.MovingExtraAngle, 3.5f));

	Rifle->HipAccuracy.bOverridePosture = true;
	Rifle->HipAccuracy.PostureOverride.MovingExtraAngle = 1.1f;
	Rifle->HipAccuracy.PostureOverride.SlowWalkExtraAngle = 0.2f;
	Rifle->HipAccuracy.PostureOverride.AirborneExtraAngle = 5.0f;
	Rifle->HipAccuracy.PostureOverride.MovementThreshold = 9.0f;
	SharedPreset->Settings.MovingExtraAngle = 4.25f;

	const FWeaponPostureSettings RifleOverride = Rifle->HipAccuracy.GetPosture(10.0f);
	const FWeaponPostureSettings SMGStillShared = SMG->HipAccuracy.GetPosture(10.0f);
	TestTrue(TEXT("The per-weapon override isolates the rifle from later shared edits"),
		NearlyEqual(RifleOverride.MovingExtraAngle, 1.1f));
	TestTrue(TEXT("The other weapon continues to follow the shared preset"),
		NearlyEqual(SMGStillShared.MovingExtraAngle, 4.25f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponConfigurationFusionLegacyAccuracyTest,
	"Dust2.ConfigurationFusion.LegacyFiringTimingAndSniperPosture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponConfigurationFusionLegacyAccuracyTest::RunTest(const FString& Parameters)
{
	FWeaponSpreadProfile Legacy;
	Legacy.StationarySpreadAngle = 10.0f;
	Legacy.MovingSpreadAngle = 4.0f;
	Legacy.SlowWalkSpreadAngle = 8.0f;
	Legacy.JumpSpreadAngle = 15.0f;
	Legacy.MovementSpreadSpeedThreshold = 13.0f;
	Legacy.bUseFiringSpread = true;
	Legacy.SpreadPerShot = 0.42f;
	Legacy.MaxFiringSpread = 2.4f;
	Legacy.SpreadRecoveryDelay = 0.17f;
	Legacy.SpreadRecoverySpeed = 3.2f;

	const FWeaponIntuitiveAccuracy Migrated = FWeaponIntuitiveAccuracy::FromLegacy(Legacy, 0.31f);
	const FWeaponPostureSettings Posture = Migrated.GetPosture(13.0f);
	const FWeaponSpreadProfile Resolved = Migrated.BuildProfile(0.31f, 13.0f);

	TestTrue(TEXT("A scoped weapon's smaller legacy moving value becomes no extra penalty, never a negative one"),
		Posture.MovingExtraAngle >= 0.0f && NearlyEqual(Posture.MovingExtraAngle, 0.0f));
	TestTrue(TEXT("Resolved moving spread cannot become narrower than the scoped base spread"),
		Resolved.bUseAdditiveStateSpread &&
		Resolved.StationarySpreadAngle + Resolved.MovingSpreadAngle >= Resolved.StationarySpreadAngle);
	TestTrue(TEXT("Legacy firing accumulation remains active with its original per-shot growth"),
		Resolved.bUseFiringSpread && NearlyEqual(Resolved.SpreadPerShot, Legacy.SpreadPerShot));
	TestTrue(TEXT("Legacy firing cap and recovery delay remain unchanged"),
		NearlyEqual(Resolved.MaxFiringSpread, Legacy.MaxFiringSpread) &&
		NearlyEqual(Resolved.SpreadRecoveryDelay, Legacy.SpreadRecoveryDelay));
	TestTrue(TEXT("Legacy firing recovery speed remains unchanged"),
		NearlyEqual(Resolved.SpreadRecoverySpeed, Legacy.SpreadRecoverySpeed));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponConfigurationFusionEditorCacheTest,
	"Dust2.ConfigurationFusion.EditorAccuracyEditsRetireLegacyFiringCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponConfigurationFusionEditorCacheTest::RunTest(const FString& Parameters)
{
	UWeaponDefinitionData* Definition = MakeLegacyDefinition();
	Definition->ConfigurationVersion = 1;
	Definition->HipAccuracy.bPreserveMigratedFiring = true;

	FProperty* HipMember = FindFProperty<FProperty>(
		UWeaponDefinitionData::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, HipAccuracy));
	FProperty* ShotKnob = FindFProperty<FProperty>(
		FWeaponIntuitiveAccuracy::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, ShotsToMaximum));
	FProperty* PoseKnob = FindFProperty<FProperty>(
		FWeaponIntuitiveAccuracy::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, MovingExtraAngle));
	if (!TestNotNull(TEXT("Hip accuracy reflected member found"), HipMember) ||
		!TestNotNull(TEXT("Firing control reflected property found"), ShotKnob) ||
		!TestNotNull(TEXT("Posture control reflected property found"), PoseKnob))
	{
		return false;
	}

	FPropertyChangedEvent FiringEvent(ShotKnob);
	FiringEvent.SetActiveMemberProperty(HipMember);
	Definition->PostEditChangeProperty(FiringEvent);
	TestFalse(TEXT("Changing the firing controls retires the exact legacy firing cache"),
		Definition->HipAccuracy.bPreserveMigratedFiring);

	Definition->HipAccuracy.bPreserveMigratedFiring = true;
	FPropertyChangedEvent PostureEvent(PoseKnob);
	PostureEvent.SetActiveMemberProperty(HipMember);
	Definition->PostEditChangeProperty(PostureEvent);
	TestTrue(TEXT("Changing only posture does not discard preserved firing timing"),
		Definition->HipAccuracy.bPreserveMigratedFiring);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponConfigurationFusionCameraPresetTest,
	"Dust2.ConfigurationFusion.SharedCameraPresetAndOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponConfigurationFusionCameraPresetTest::RunTest(const FString& Parameters)
{
	UWeaponCameraFeedbackPreset* SharedPreset = NewObject<UWeaponCameraFeedbackPreset>(GetTransientPackage());
	SharedPreset->ReturnSpeed = 18.0f;
	SharedPreset->SmoothKickDuration = 0.08f;
	SharedPreset->ImmediateKickFraction = 0.35f;

	UWeaponRecoilData* Rifle = NewObject<UWeaponRecoilData>(GetTransientPackage());
	UWeaponRecoilData* SMG = NewObject<UWeaponRecoilData>(GetTransientPackage());
	Rifle->CameraFeedbackPreset = SharedPreset;
	SMG->CameraFeedbackPreset = SharedPreset;
	TestTrue(TEXT("Both weapons read shared camera feedback settings"),
		NearlyEqual(Rifle->GetCameraReturnSpeed(), 18.0f) &&
		NearlyEqual(SMG->GetSmoothKickDuration(), 0.08f) &&
		NearlyEqual(SMG->GetImmediateKickFraction(), 0.35f));

	SharedPreset->ReturnSpeed = 24.0f;
	TestTrue(TEXT("Editing the shared preset changes both weapon results"),
		NearlyEqual(Rifle->GetCameraReturnSpeed(), 24.0f) &&
		NearlyEqual(SMG->GetCameraReturnSpeed(), 24.0f));

	Rifle->bOverrideCameraFeedback = true;
	Rifle->CameraFeedbackOverrides.ReturnSpeed = 6.0f;
	Rifle->CameraFeedbackOverrides.SmoothKickDuration = 0.02f;
	Rifle->CameraFeedbackOverrides.ImmediateKickFraction = 0.1f;
	SharedPreset->ReturnSpeed = 30.0f;
	TestTrue(TEXT("The overriding weapon is isolated while the other retains the shared preset"),
		NearlyEqual(Rifle->GetCameraReturnSpeed(), 6.0f) &&
		NearlyEqual(Rifle->GetSmoothKickDuration(), 0.02f) &&
		NearlyEqual(Rifle->GetImmediateKickFraction(), 0.1f) &&
		NearlyEqual(SMG->GetCameraReturnSpeed(), 30.0f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
