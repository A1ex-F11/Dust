#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include <initializer_list>
#include "FireMode.h"
#include "WeaponAccuracyData.h"
#include "WeaponCameraFeedbackPreset.h"
#include "WeaponDefinitionData.h"
#include "WeaponRecoilData.h"
#include "WeaponRuntime.h"

namespace
{
	class FWeaponThreeAssetTestWorld
	{
	public:
		FWeaponThreeAssetTestWorld()
		{
			UWorld::InitializationValues InitValues;
			InitValues
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false);

			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				NAME_None,
				nullptr,
				false,
				ERHIFeatureLevel::Num,
				&InitValues);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			if (World)
			{
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		~FWeaponThreeAssetTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World = nullptr;
			}
		}

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};

	static bool NearlyEqual(float A, float B)
	{
		return FMath::IsNearlyEqual(A, B, 0.0001f);
	}

	static AWeaponRuntime* SpawnWeapon(
		FAutomationTestBase& Test,
		UWorld* World,
		UWeaponDefinitionData* Definition)
	{
		if (!World)
		{
			Test.AddError(TEXT("Transient weapon three-asset test world is unavailable."));
			return nullptr;
		}
		AWeaponRuntime* Weapon = World->SpawnActorDeferred<AWeaponRuntime>(
			AWeaponRuntime::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Weapon)
		{
			Test.AddError(TEXT("AWeaponRuntime could not be deferred-spawned."));
			return nullptr;
		}
		Weapon->WeaponDefinition = Definition;
		Weapon->FinishSpawning(FTransform::Identity);
		return Weapon;
	}

	static UWeaponRecoilData* GetRuntimeRecoil(AWeaponRuntime* Weapon)
	{
		if (!Weapon)
		{
			return nullptr;
		}
		FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
			Weapon->GetClass(), TEXT("RecoilData"));
		return Property
			? Cast<UWeaponRecoilData>(Property->GetObjectPropertyValue_InContainer(Weapon))
			: nullptr;
	}

	static UWeaponDefinitionData* MakeDefinition()
	{
		UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
		Definition->ConfigurationVersion = 1;
		Definition->AllowedFireModes = { EFireMode::FullAutomatic };
		Definition->DefaultFireMode = EFireMode::FullAutomatic;
		Definition->MagazineCapacity = 30;
		Definition->InitialMagazineAmmo = 30;
		Definition->InitialReserveAmmo = 90;
		Definition->FireInterval = 0.1f;
		Definition->BurstRecoveryInterval = 0.3f;
		Definition->RecoilData->bUseFixedPattern = true;
		Definition->RecoilData->bUseIndependentShotRecoil = true;
		Definition->RecoilData->FixedPattern = { FVector2D(0.7f, -0.1f), FVector2D(0.9f, 0.15f) };
		Definition->RecoilData->PatternResetDelay = 0.35f;
		Definition->RecoilData->ShotRecoilRecoverySpeed = 8.0f;
		Definition->RecoilData->RecoveryDelay = 0.2f;
		return Definition;
	}

	static FWeaponIntuitiveAccuracy MakeNonDefaultAccuracy()
	{
		FWeaponIntuitiveAccuracy Accuracy;
		Accuracy.BaseSpreadAngle = 0.65f;
		Accuracy.MaxFiringExtraAngle = 1.7f;
		Accuracy.ShotsToMaximum = 8;
		Accuracy.FullRecoverySeconds = 0.8f;
		Accuracy.bUsePostureSettings = true;
		Accuracy.PostureOverride.MovingExtraAngle = 2.4f;
		Accuracy.PostureOverride.SlowWalkExtraAngle = 1.1f;
		Accuracy.PostureOverride.AirborneExtraAngle = 5.5f;
		Accuracy.PostureOverride.MovementThreshold = 9.0f;
		return Accuracy;
	}

	static UWeaponAccuracyData* MakeAccuracyAsset()
	{
		UWeaponAccuracyData* Accuracy = NewObject<UWeaponAccuracyData>(GetTransientPackage());
		Accuracy->HipAccuracy = MakeNonDefaultAccuracy();
		Accuracy->AimAccuracy = MakeNonDefaultAccuracy();
		Accuracy->AimAccuracy.BaseSpreadAngle = 0.1f;
		Accuracy->AimAccuracy.MaxFiringExtraAngle = 0.3f;
		Accuracy->bUseAimingSpread = true;
		return Accuracy;
	}

	static bool ProfilesEqual(const FWeaponSpreadProfile& A, const FWeaponSpreadProfile& B)
	{
		return A.bUseAdditiveStateSpread == B.bUseAdditiveStateSpread
			&& NearlyEqual(A.StationarySpreadAngle, B.StationarySpreadAngle)
			&& NearlyEqual(A.MovingSpreadAngle, B.MovingSpreadAngle)
			&& NearlyEqual(A.JumpSpreadAngle, B.JumpSpreadAngle)
			&& NearlyEqual(A.SlowWalkSpreadAngle, B.SlowWalkSpreadAngle)
			&& NearlyEqual(A.MovementSpreadSpeedThreshold, B.MovementSpreadSpeedThreshold)
			&& A.bUseFiringSpread == B.bUseFiringSpread
			&& NearlyEqual(A.SpreadPerShot, B.SpreadPerShot)
			&& NearlyEqual(A.MaxFiringSpread, B.MaxFiringSpread)
			&& NearlyEqual(A.SpreadRecoveryDelay, B.SpreadRecoveryDelay)
			&& NearlyEqual(A.SpreadRecoverySpeed, B.SpreadRecoverySpeed);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetLegacyFallbackTest,
	"Dust2.WeaponThreeAsset.LegacyFallbackWithoutAccuracyAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetLegacyFallbackTest::RunTest(const FString& Parameters)
{
	FWeaponThreeAssetTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponDefinitionData* Definition = MakeDefinition();
	Definition->bUseIntuitiveHipAccuracy = false;
	Definition->StationarySpreadAngle = 4.0f;
	Definition->MovingSpreadAngle = 2.0f;
	Definition->SlowWalkSpreadAngle = 1.0f;
	Definition->JumpSpreadAngle = 9.0f;
	Definition->MovementSpreadSpeedThreshold = 10.0f;
	AWeaponRuntime* Weapon = SpawnWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Legacy definition weapon spawned"), Weapon))
	{
		return false;
	}

	TestTrue(TEXT("Without AccuracyData the runtime still reads the shared recoil asset"),
		GetRuntimeRecoil(Weapon) == Definition->RecoilData.Get());
	TestTrue(TEXT("Legacy stationary fallback remains in use"),
		NearlyEqual(Weapon->GetCurrentTotalSpreadAngle(0.0f, false, false, false), 4.0f));
	TestTrue(TEXT("Legacy moving fallback remains in use"),
		NearlyEqual(Weapon->GetCurrentTotalSpreadAngle(100.0f, false, false, false), 2.0f));
	TestTrue(TEXT("Legacy slow-walk fallback remains in use"),
		NearlyEqual(Weapon->GetCurrentTotalSpreadAngle(100.0f, false, false, true), 1.0f));
	TestTrue(TEXT("Legacy airborne fallback remains in use"),
		NearlyEqual(Weapon->GetCurrentTotalSpreadAngle(0.0f, true, false, false), 9.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetAccuracyEquivalenceTest,
	"Dust2.WeaponThreeAsset.NewAccuracyMatchesLegacyValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetAccuracyEquivalenceTest::RunTest(const FString& Parameters)
{
	FWeaponThreeAssetTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponAccuracyData* Accuracy = MakeAccuracyAsset();
	UWeaponDefinitionData* SplitDefinition = MakeDefinition();
	SplitDefinition->bUseIntuitiveHipAccuracy = true;
	SplitDefinition->HipAccuracy.BaseSpreadAngle = 8.0f; // Must be ignored once AccuracyData exists.
	SplitDefinition->AccuracyData = Accuracy;

	UWeaponDefinitionData* LegacyDefinition = MakeDefinition();
	LegacyDefinition->bUseIntuitiveHipAccuracy = true;
	LegacyDefinition->HipAccuracy = Accuracy->HipAccuracy;
	LegacyDefinition->AimAccuracy = Accuracy->AimAccuracy;
	LegacyDefinition->bUseAimingSpread = true;

	AWeaponRuntime* SplitWeapon = SpawnWeapon(*this, TestWorld.Get(), SplitDefinition);
	AWeaponRuntime* LegacyWeapon = SpawnWeapon(*this, TestWorld.Get(), LegacyDefinition);
	if (!TestNotNull(TEXT("Split accuracy weapon spawned"), SplitWeapon)
		|| !TestNotNull(TEXT("Legacy accuracy weapon spawned"), LegacyWeapon))
	{
		return false;
	}

	TestTrue(TEXT("AccuracyData takes precedence over the old HipAccuracy field"),
		!NearlyEqual(SplitWeapon->GetCurrentTotalSpreadAngle(0.0f, false, false, false), 8.0f));
	for (const float Speed : { 0.0f, 100.0f })
	{
		for (const bool bFalling : { false, true })
		{
			for (const bool bSlowWalking : { false, true })
			{
				TestTrue(TEXT("Hip spread stays numerically identical after the split"),
					NearlyEqual(
						SplitWeapon->GetCurrentTotalSpreadAngle(Speed, bFalling, false, bSlowWalking),
						LegacyWeapon->GetCurrentTotalSpreadAngle(Speed, bFalling, false, bSlowWalking)));
			}
		}
	}

	const FWeaponSpreadProfile SplitAim = Accuracy->AimAccuracy.BuildProfile(0.1f, 9.0f);
	const FWeaponSpreadProfile LegacyAim = LegacyDefinition->AimAccuracy.BuildProfile(0.1f, 9.0f);
	TestTrue(TEXT("Aim profile values stay equal in the new accuracy asset"),
		ProfilesEqual(SplitAim, LegacyAim));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetPreservedTimingTest,
	"Dust2.WeaponThreeAsset.PreservedLegacyTimingSurvivesNewAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetPreservedTimingTest::RunTest(const FString& Parameters)
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
	UWeaponAccuracyData* Accuracy = NewObject<UWeaponAccuracyData>(GetTransientPackage());
	Accuracy->HipAccuracy = Migrated;
	Accuracy->AimAccuracy = Migrated;
	Accuracy->bUseAimingSpread = true;

	TestTrue(TEXT("Legacy timing cache survives the accuracy asset copy"),
		Accuracy->HipAccuracy.bPreserveMigratedFiring);
	TestTrue(TEXT("Legacy exact hip profile is unchanged after the split"),
		ProfilesEqual(Migrated.BuildProfile(0.31f, 13.0f),
			Accuracy->HipAccuracy.BuildProfile(0.31f, 13.0f)));
	TestTrue(TEXT("Legacy exact aim profile is unchanged after the split"),
		ProfilesEqual(Migrated.BuildProfile(0.31f, 13.0f),
			Accuracy->AimAccuracy.BuildProfile(0.31f, 13.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetCameraPriorityTest,
	"Dust2.WeaponThreeAsset.CameraPresetAndOverridePriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetCameraPriorityTest::RunTest(const FString& Parameters)
{
	UWeaponCameraFeedbackPreset* Preset = NewObject<UWeaponCameraFeedbackPreset>(GetTransientPackage());
	Preset->ReturnSpeed = 18.0f;
	Preset->SmoothKickDuration = 0.08f;
	Preset->ImmediateKickFraction = 0.35f;

	UWeaponAccuracyData* Accuracy = MakeAccuracyAsset();
	TestTrue(TEXT("No preset and no override uses the old fallback values"),
		NearlyEqual(Accuracy->GetCameraReturnSpeed(), 12.0f)
		&& NearlyEqual(Accuracy->GetSmoothKickDuration(), 0.05f)
		&& NearlyEqual(Accuracy->GetImmediateKickFraction(), 0.25f));

	Accuracy->CameraFeedbackPreset = Preset;
	TestTrue(TEXT("Preset wins over the old fallback values"),
		NearlyEqual(Accuracy->GetCameraReturnSpeed(), 18.0f)
		&& NearlyEqual(Accuracy->GetSmoothKickDuration(), 0.08f)
		&& NearlyEqual(Accuracy->GetImmediateKickFraction(), 0.35f));

	Accuracy->bOverrideCameraFeedback = true;
	Accuracy->CameraFeedbackOverrides.ReturnSpeed = 6.0f;
	Accuracy->CameraFeedbackOverrides.SmoothKickDuration = 0.02f;
	Accuracy->CameraFeedbackOverrides.ImmediateKickFraction = 0.1f;
	Preset->ReturnSpeed = 24.0f;
	TestTrue(TEXT("Explicit override wins over a later shared preset edit"),
		NearlyEqual(Accuracy->GetCameraReturnSpeed(), 6.0f)
		&& NearlyEqual(Accuracy->GetSmoothKickDuration(), 0.02f)
		&& NearlyEqual(Accuracy->GetImmediateKickFraction(), 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetRuntimeIsolationTest,
	"Dust2.WeaponThreeAsset.TwoRuntimesShareSourceWithoutPollution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetRuntimeIsolationTest::RunTest(const FString& Parameters)
{
	FWeaponThreeAssetTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponAccuracyData* Accuracy = MakeAccuracyAsset();
	Accuracy->CameraFeedbackScale = 0.4f;
	Accuracy->CameraReturnSpeed = 15.0f;
	UWeaponDefinitionData* Definition = MakeDefinition();
	Definition->AccuracyData = Accuracy;
	Definition->RecoilData->CameraFeedbackScale = 0.9f;

	AWeaponRuntime* First = SpawnWeapon(*this, TestWorld.Get(), Definition);
	AWeaponRuntime* Second = SpawnWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("First runtime spawned"), First)
		|| !TestNotNull(TEXT("Second runtime spawned"), Second))
	{
		return false;
	}

	UWeaponRecoilData* FirstRecoil = GetRuntimeRecoil(First);
	UWeaponRecoilData* SecondRecoil = GetRuntimeRecoil(Second);
	TestNotNull(TEXT("First runtime owns a private recoil copy"), FirstRecoil);
	TestNotNull(TEXT("Second runtime owns a private recoil copy"), SecondRecoil);
	if (!FirstRecoil || !SecondRecoil)
	{
		return false;
	}
	TestTrue(TEXT("The shared source asset is not used directly"),
		FirstRecoil != Definition->RecoilData.Get() && SecondRecoil != Definition->RecoilData.Get());
	TestTrue(TEXT("Each runtime gets its own private copy"), FirstRecoil != SecondRecoil);
	TestTrue(TEXT("Gameplay recoil fields survive the camera overlay"),
		FirstRecoil->bUseFixedPattern && FirstRecoil->bUseIndependentShotRecoil
		&& FirstRecoil->FixedPattern.Num() == 2);
	TestTrue(TEXT("The accuracy camera values reach the runtime copy"),
		NearlyEqual(FirstRecoil->GetCameraReturnSpeed(), 15.0f)
		&& NearlyEqual(FirstRecoil->CameraFeedbackScale, 0.4f));

	FirstRecoil->CameraFeedbackScale = 99.0f;
	Second->ApplyDefinitionSettings();
	UWeaponRecoilData* SecondAfterApply = GetRuntimeRecoil(Second);
	TestTrue(TEXT("Editing one runtime copy does not pollute the other"),
		SecondAfterApply && NearlyEqual(SecondAfterApply->CameraFeedbackScale, 0.4f));
	TestTrue(TEXT("The shared accuracy asset is never written"),
		NearlyEqual(Accuracy->CameraFeedbackScale, 0.4f));
	TestTrue(TEXT("The shared recoil asset is never written"),
		NearlyEqual(Definition->RecoilData->CameraFeedbackScale, 0.9f));

	First->ApplyDefinitionSettings();
	TestTrue(TEXT("Repeated Apply keeps the same private recoil object"),
		GetRuntimeRecoil(First) == FirstRecoil);
	TestTrue(TEXT("Repeated Apply restores values from the shared source, not from the copy"),
		NearlyEqual(FirstRecoil->CameraFeedbackScale, 0.4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetCaptureTest,
	"Dust2.WeaponThreeAsset.CapturePreservesEffectiveAccuracyWithoutWritingSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetCaptureTest::RunTest(const FString& Parameters)
{
	FWeaponThreeAssetTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponAccuracyData* Accuracy = MakeAccuracyAsset();
	Accuracy->CameraFeedbackScale = 0.4f;
	Accuracy->CameraReturnSpeed = 15.0f;
	UWeaponDefinitionData* Definition = MakeDefinition();
	Definition->AccuracyData = Accuracy;
	AWeaponRuntime* Weapon = SpawnWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Weapon spawned for capture"), Weapon))
	{
		return false;
	}

	UWeaponRecoilData* RuntimeRecoil = GetRuntimeRecoil(Weapon);
	UWeaponDefinitionData* Captured = NewObject<UWeaponDefinitionData>(GetTransientPackage());
	Weapon->CaptureDefinition(Captured);

	TestNotNull(TEXT("Capture stores an accuracy asset reference"), Captured->AccuracyData.Get());
	TestNotNull(TEXT("Capture stores a recoil asset"), Captured->RecoilData.Get());
	if (!Captured->AccuracyData || !Captured->RecoilData)
	{
		return false;
	}
	TestTrue(TEXT("Capture duplicates accuracy instead of sharing the source"),
		Captured->AccuracyData.Get() != Accuracy);
	TestTrue(TEXT("Captured accuracy keeps the effective camera values"),
		NearlyEqual(Captured->AccuracyData->CameraFeedbackScale, 0.4f)
		&& NearlyEqual(Captured->AccuracyData->CameraReturnSpeed, 15.0f)
		&& Captured->AccuracyData->bUseAimingSpread);
	TestTrue(TEXT("Captured accuracy keeps the non-default hip and aim profiles"),
		NearlyEqual(Captured->AccuracyData->HipAccuracy.BaseSpreadAngle, 0.65f)
		&& NearlyEqual(Captured->AccuracyData->AimAccuracy.BaseSpreadAngle, 0.1f));

	TestFalse(TEXT("Captured accuracy is saveable rather than transient"), Captured->AccuracyData->HasAnyFlags(RF_Transient));
	TestFalse(TEXT("Captured recoil is saveable rather than transient"), Captured->RecoilData->HasAnyFlags(RF_Transient));

	TestTrue(TEXT("Captured recoil is not the runtime private copy"),
		Captured->RecoilData.Get() != RuntimeRecoil);
	TestTrue(TEXT("Captured recoil is not the shared source"),
		Captured->RecoilData.Get() != Definition->RecoilData.Get());
	TestTrue(TEXT("Captured recoil keeps the full gameplay sequence"),
		Captured->RecoilData->bUseFixedPattern
		&& Captured->RecoilData->FixedPattern.Num() == 2);

	TestTrue(TEXT("Capture never writes back to the shared accuracy asset"),
		NearlyEqual(Accuracy->CameraReturnSpeed, 15.0f)
		&& NearlyEqual(Accuracy->HipAccuracy.BaseSpreadAngle, 0.65f));
	TestTrue(TEXT("Capture never writes back to the shared recoil asset"),
		NearlyEqual(Definition->RecoilData->CameraFeedbackScale, 0.25f));

	UWeaponDefinitionData* CapturedAgain = NewObject<UWeaponDefinitionData>(GetTransientPackage());
	Weapon->CaptureDefinition(CapturedAgain);
	TestTrue(TEXT("Repeated Capture still produces effective accuracy data"),
		CapturedAgain->AccuracyData.Get() != nullptr
		&& NearlyEqual(CapturedAgain->AccuracyData->CameraFeedbackScale, 0.4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetSplitPlanTest,
	"Dust2.WeaponThreeAsset.SplitPlanNamingCollisionAndIdempotency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetSplitPlanTest::RunTest(const FString& Parameters)
{
	const auto NothingOccupied = [](const FString&) { return false; };
	FWeaponThreeAssetSplitPlan Plan;
	FString Error;

	TestTrue(TEXT("A fresh definition plans both assets"),
		BuildWeaponThreeAssetSplitPlan(
			TEXT("/Game/Code/Texts/AK105/DA_AK105Definition"),
			TEXT("DA_AK105Definition"),
			false, false, NothingOccupied, Plan, Error));
	TestTrue(TEXT("Accuracy target shares the definition directory and strips the suffix"),
		Plan.AccuracyPackageName == TEXT("/Game/Code/Texts/AK105/DA_AK105Accuracy")
		&& Plan.AccuracyAssetName == TEXT("DA_AK105Accuracy"));
	TestTrue(TEXT("Recoil target shares the definition directory and strips the suffix"),
		Plan.RecoilPackageName == TEXT("/Game/Code/Texts/AK105/DA_AK105Recoil")
		&& Plan.RecoilAssetName == TEXT("DA_AK105Recoil"));
	TestTrue(TEXT("Both assets are scheduled on a fresh definition"),
		Plan.bCreateAccuracy && Plan.bCreateRecoil);

	FWeaponThreeAssetSplitPlan AlreadySplit;
	TestTrue(TEXT("Both external references are an idempotent no-op"),
		BuildWeaponThreeAssetSplitPlan(
			TEXT("/Game/Code/Texts/AK105/DA_AK105Definition"),
			TEXT("DA_AK105Definition"),
			true, true, NothingOccupied, AlreadySplit, Error));
	TestFalse(TEXT("No accuracy asset is planned for an already migrated reference"),
		AlreadySplit.bCreateAccuracy);
	TestFalse(TEXT("No recoil asset is planned for an already migrated reference"),
		AlreadySplit.bCreateRecoil);

	FWeaponThreeAssetSplitPlan Partial;
	TestTrue(TEXT("Partial migration plans only the missing recoil asset"),
		BuildWeaponThreeAssetSplitPlan(
			TEXT("/Game/Code/Texts/AK105/DA_AK105Definition"),
			TEXT("DA_AK105Definition"),
			true, false, NothingOccupied, Partial, Error));
	TestFalse(TEXT("An existing external accuracy asset is not overwritten"), Partial.bCreateAccuracy);
	TestTrue(TEXT("The missing recoil asset is still planned"), Partial.bCreateRecoil);

	const auto AccuracyOccupied = [](const FString& PackageName)
	{
		return PackageName.Contains(TEXT("Accuracy"));
	};
	FWeaponThreeAssetSplitPlan Collision;
	TestFalse(TEXT("An occupied target package aborts the whole split before any reference change"),
		BuildWeaponThreeAssetSplitPlan(
			TEXT("/Game/Code/Texts/AK105/DA_AK105Definition"),
			TEXT("DA_AK105Definition"),
			false, false, AccuracyOccupied, Collision, Error));
	TestTrue(TEXT("The collision error names the existing package"), Error.Find(TEXT("Accuracy")) != INDEX_NONE);

	FWeaponThreeAssetSplitPlan Invalid;
	TestFalse(TEXT("Empty definition identity is rejected"),
		BuildWeaponThreeAssetSplitPlan(FString(), FString(), false, false, NothingOccupied, Invalid, Error));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponThreeAssetAccuracyEditTest,
	"Dust2.WeaponThreeAsset.AccuracyEditsRetireLegacyFiringCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponThreeAssetAccuracyEditTest::RunTest(const FString& Parameters)
{
	UWeaponAccuracyData* Accuracy = NewObject<UWeaponAccuracyData>(GetTransientPackage());
	Accuracy->HipAccuracy.bPreserveMigratedFiring = true;

	FProperty* HipMember = FindFProperty<FProperty>(
		UWeaponAccuracyData::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UWeaponAccuracyData, HipAccuracy));
	FProperty* AimMember = FindFProperty<FProperty>(
		UWeaponAccuracyData::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UWeaponAccuracyData, AimAccuracy));
	FProperty* ShotKnob = FindFProperty<FProperty>(
		FWeaponIntuitiveAccuracy::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, ShotsToMaximum));
	FProperty* PoseKnob = FindFProperty<FProperty>(
		FWeaponIntuitiveAccuracy::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, MovingExtraAngle));
	if (!TestNotNull(TEXT("Hip accuracy reflected member found"), HipMember)
		|| !TestNotNull(TEXT("Aim accuracy reflected member found"), AimMember)
		|| !TestNotNull(TEXT("Firing control reflected property found"), ShotKnob)
		|| !TestNotNull(TEXT("Posture control reflected property found"), PoseKnob))
	{
		return false;
	}

	FPropertyChangedEvent FiringEvent(ShotKnob);
	FiringEvent.SetActiveMemberProperty(HipMember);
	Accuracy->PostEditChangeProperty(FiringEvent);
	TestFalse(TEXT("Editing the four firing controls retires the legacy timing cache"),
		Accuracy->HipAccuracy.bPreserveMigratedFiring);

	Accuracy->HipAccuracy.bPreserveMigratedFiring = true;
	FPropertyChangedEvent PostureEvent(PoseKnob);
	PostureEvent.SetActiveMemberProperty(HipMember);
	Accuracy->PostEditChangeProperty(PostureEvent);
	TestTrue(TEXT("Editing only posture keeps the legacy timing cache"),
		Accuracy->HipAccuracy.bPreserveMigratedFiring);

	Accuracy->AimAccuracy.bPreserveMigratedFiring = true;
	FPropertyChangedEvent AimFiringEvent(ShotKnob);
	AimFiringEvent.SetActiveMemberProperty(AimMember);
	Accuracy->PostEditChangeProperty(AimFiringEvent);
	TestFalse(TEXT("Aim accuracy follows the same retirement rule"),
		Accuracy->AimAccuracy.bPreserveMigratedFiring);
	return true;
}
#endif

#endif // WITH_DEV_AUTOMATION_TESTS
