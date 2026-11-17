#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Kismet/KismetMathLibrary.h"
#include "Presentation/WeaponSwayRuntime.h"
#include "Presentation/WeaponPresentationComponent.h"
#include "Presentation/WeaponPresentationData.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Animation/AnimSequence.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
	FWeaponSwayVectorSettings AxisSettings(const FVector& Scale, const FVector& Speed)
	{
		FWeaponSwayVectorSettings Result;
		Result.Stiffness = FVector(0.8, 1.1, 1.4);
		Result.Damping = FVector(0.4, 0.5, 0.6);
		Result.InterpSpeed = Speed;
		Result.SmoothingSpeed = FVector(1.0, 1.0, 1.0);
		Result.ClampValue = FVector(10.0, 10.0, 10.0);
		Result.Scale = Scale;
		Result.AdsScale = 0.5f;
		return Result;
	}

	FWeaponSwaySettings MakeSettings()
	{
		FWeaponSwaySettings Result;
		Result.Position = AxisSettings(FVector(2.0, 3.0, 4.0), FVector(0.5, 2.0, 10.0));
		Result.Rotation = AxisSettings(FVector(1.0, 2.0, 3.0), FVector(3.0, 5.0, 7.0));
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSwayTargetMathTest,
	"Dust2.WeaponSway.TargetMappingADSClampAndRotationConvention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSwayTargetMathTest::RunTest(const FString& Parameters)
{
	FWeaponSwayRuntime Runtime;
	const FWeaponSwaySettings Settings = MakeSettings();
	TestTrue(TEXT("move target computed"), Runtime.ComputeMoveTarget(
		Settings, FVector(0.4, 0.2, -0.3), FVector(2.0, 3.0, 4.0), true, 1.0f));
	TestTrue(TEXT("position uses authored per-axis scale and ADS"),
		Runtime.Move.TargetLocation.Equals(FVector(0.4, 0.3, -0.6), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("rotation input preserves signed axis scales"),
		Runtime.Move.TargetRotation.Equals(FVector(1.0, 3.0, 6.0), KINDA_SMALL_NUMBER));

	FWeaponSwayRuntime AimRuntime;
	FWeaponSwaySettings AimSettings = MakeSettings();
	AimSettings.Position.SmoothingSpeed = FVector(1.0, 1.0, 1.0);
	AimSettings.Rotation.SmoothingSpeed = FVector(1.0, 1.0, 1.0);
	TestTrue(TEXT("aim target computed"), AimRuntime.ComputeAimTarget(
		AimSettings, FVector(4.0, -10.0, 2.0), FVector(20.0, 30.0, 40.0), true, 0.1f));
	TestTrue(TEXT("aim position hard-resets X then returns other axes to zero"),
		AimRuntime.Aim.TargetLocation.Equals(FVector(0.0, -9.0, 3.6), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("aim rotation accumulates then clamps roll pitch yaw axes"),
		AimRuntime.Aim.TargetRotation.Equals(FVector(9.0, 9.0, 9.0), KINDA_SMALL_NUMBER));
	AimRuntime.Aim.Rotation = FVector(11.0, 22.0, 33.0);
	const FRotator OutputRotation = AimRuntime.Aim.GetTransform().Rotator();
	TestTrue(TEXT("rotation vector X/Y/Z outputs roll/pitch/yaw"),
		FMath::IsNearlyEqual(OutputRotation.Roll, 11.0)
		&& FMath::IsNearlyEqual(OutputRotation.Pitch, 22.0)
		&& FMath::IsNearlyEqual(OutputRotation.Yaw, 33.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSwaySpringStateTest,
	"Dust2.WeaponSway.SpringStepMatchesBlueprintAndKeepsAxesIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSwaySpringStateTest::RunTest(const FString& Parameters)
{
	FWeaponSwayRuntime Runtime;
	const FWeaponSwaySettings Settings = MakeSettings();
	Runtime.Move.TargetLocation = FVector(1.0, 2.0, 3.0);
	Runtime.Move.TargetRotation = FVector(4.0, 5.0, 6.0);
	Runtime.Aim.TargetLocation = FVector(-1.0, -2.0, -3.0);
	Runtime.Aim.TargetRotation = FVector(-4.0, -5.0, -6.0);
	FFloatSpringState MoveLocationReference[3];
	FFloatSpringState MoveRotationReference[3];
	FFloatSpringState AimLocationReference[3];
	FFloatSpringState AimRotationReference[3];
	FVector ExpectedMoveLocation = FVector::ZeroVector;
	FVector ExpectedMoveRotation = FVector::ZeroVector;
	FVector ExpectedAimLocation = FVector::ZeroVector;
	FVector ExpectedAimRotation = FVector::ZeroVector;

	const float Dt = 0.25f;
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		TestTrue(TEXT("move spring advances"), Runtime.ComputeMove(Settings, Dt));
		TestTrue(TEXT("aim spring advances independently"), Runtime.ComputeAim(Settings, Dt));
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float MoveLocationDt = FMath::Min(static_cast<float>(Settings.Position.InterpSpeed[Axis]) * Dt, 1.0f);
			const float MoveRotationDt = FMath::Min(static_cast<float>(Settings.Rotation.InterpSpeed[Axis]) * Dt, 1.0f);
			ExpectedMoveLocation[Axis] = UKismetMathLibrary::FloatSpringInterp(
				ExpectedMoveLocation[Axis], Runtime.Move.TargetLocation[Axis], MoveLocationReference[Axis],
				Settings.Position.Stiffness[Axis], Settings.Position.Damping[Axis], MoveLocationDt, 1.0f, 1.0f);
			ExpectedMoveRotation[Axis] = UKismetMathLibrary::FloatSpringInterp(
				ExpectedMoveRotation[Axis], Runtime.Move.TargetRotation[Axis], MoveRotationReference[Axis],
				Settings.Rotation.Stiffness[Axis], Settings.Rotation.Damping[Axis], MoveRotationDt, 1.0f, 1.0f);
			ExpectedAimLocation[Axis] = UKismetMathLibrary::FloatSpringInterp(
				ExpectedAimLocation[Axis], Runtime.Aim.TargetLocation[Axis], AimLocationReference[Axis],
				Settings.Position.Stiffness[Axis], Settings.Position.Damping[Axis], MoveLocationDt, 1.0f, 1.0f);
			ExpectedAimRotation[Axis] = UKismetMathLibrary::FloatSpringInterp(
				ExpectedAimRotation[Axis], Runtime.Aim.TargetRotation[Axis], AimRotationReference[Axis],
				Settings.Rotation.Stiffness[Axis], Settings.Rotation.Damping[Axis], MoveRotationDt, 1.0f, 1.0f);
		}
	}
	TestTrue(TEXT("all move position spring states match independent reference"),
		Runtime.Move.Location.Equals(ExpectedMoveLocation, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("all move rotation spring states match independent reference"),
		Runtime.Move.Rotation.Equals(ExpectedMoveRotation, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("all aim position spring states match independent reference"),
		Runtime.Aim.Location.Equals(ExpectedAimLocation, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("all aim rotation spring states match independent reference"),
		Runtime.Aim.Rotation.Equals(ExpectedAimRotation, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("distinct move and aim targets do not alias state"),
		Runtime.Move.Location != Runtime.Aim.Location && Runtime.Move.Rotation != Runtime.Aim.Rotation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSwayResetAndInvalidInputTest,
	"Dust2.WeaponSway.ResetClearsAllTwelveSpringsAndInvalidStepsDoNotAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSwayResetAndInvalidInputTest::RunTest(const FString& Parameters)
{
	FWeaponSwayRuntime Runtime;
	const FWeaponSwaySettings Settings = MakeSettings();
	TestTrue(TEXT("seed move spring"), Runtime.ComputeMoveTarget(
		Settings, FVector(1.0, 2.0, 3.0), FVector(4.0, 5.0, 6.0), false, 0.016f));
	TestTrue(TEXT("seed move spring state"), Runtime.ComputeMove(Settings, 0.016f));
	TestTrue(TEXT("seed aim spring"), Runtime.ComputeAimTarget(
		Settings, FVector(3.0, 2.0, 1.0), FVector(6.0, 5.0, 4.0), false, 0.016f));
	TestTrue(TEXT("seed aim spring state"), Runtime.ComputeAim(Settings, 0.016f));
	const FVector MoveBefore = Runtime.Move.Location;
	const FVector AimBefore = Runtime.Aim.Rotation;
	TestFalse(TEXT("zero time is rejected"), Runtime.ComputeMove(Settings, 0.0f));
	TestFalse(TEXT("non-finite time is rejected"), Runtime.ComputeAim(Settings,
		std::numeric_limits<float>::infinity()));
	TestFalse(TEXT("non-finite target vector is rejected"), Runtime.ComputeMoveTarget(
		Settings, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), FVector::ZeroVector, false, 0.016f));
	TestTrue(TEXT("rejected inputs leave existing output state untouched"),
		Runtime.Move.Location.Equals(MoveBefore) && Runtime.Aim.Rotation.Equals(AimBefore));
	FWeaponSwaySettings HugeSettings = Settings;
	HugeSettings.Position.Scale.X = 1.0e40;
	const FVector TargetBefore = Runtime.Move.TargetLocation;
	TestFalse(TEXT("finite double values outside spring float range are rejected"), Runtime.ComputeMoveTarget(
		HugeSettings, FVector(1.0, 0.0, 0.0), FVector::ZeroVector, false, 0.016f));
	TestTrue(TEXT("overflow rejection does not change target state"), Runtime.Move.TargetLocation.Equals(TargetBefore));
	HugeSettings = Settings;
	HugeSettings.Rotation.Stiffness.X = 1.0e40;
	TestFalse(TEXT("unrepresentable stiffness is rejected before spring state changes"), Runtime.ComputeMove(HugeSettings, 0.016f));
	TestTrue(TEXT("bad spring configuration leaves current output untouched"), Runtime.Move.Location.Equals(MoveBefore));
	Runtime.Reset();
	TestTrue(TEXT("reset returns move and aim transforms to identity"),
		Runtime.Move.GetTransform().Equals(FTransform::Identity)
		&& Runtime.Aim.GetTransform().Equals(FTransform::Identity));
	FWeaponSwayRuntime Fresh;
	Runtime.Move.TargetLocation = Fresh.Move.TargetLocation = FVector(1.0, 2.0, 3.0);
	Runtime.Move.TargetRotation = Fresh.Move.TargetRotation = FVector(4.0, 5.0, 6.0);
	Runtime.Aim.TargetLocation = Fresh.Aim.TargetLocation = FVector(-1.0, -2.0, -3.0);
	Runtime.Aim.TargetRotation = Fresh.Aim.TargetRotation = FVector(-4.0, -5.0, -6.0);
	TestTrue(TEXT("reset springs behave like fresh independent state"), Runtime.ComputeMove(Settings, 0.016f));
	TestTrue(TEXT("fresh spring advances"), Fresh.ComputeMove(Settings, 0.016f));
	TestTrue(TEXT("reset aim spring advances"), Runtime.ComputeAim(Settings, 0.016f));
	TestTrue(TEXT("fresh aim spring advances"), Fresh.ComputeAim(Settings, 0.016f));
	TestTrue(TEXT("all twelve spring states reset"),
		Runtime.Move.Location.Equals(Fresh.Move.Location)
		&& Runtime.Move.Rotation.Equals(Fresh.Move.Rotation)
		&& Runtime.Aim.Location.Equals(Fresh.Aim.Location)
		&& Runtime.Aim.Rotation.Equals(Fresh.Aim.Rotation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSwayBindingCleanupTest,
	"Dust2.WeaponSway.BindingIdentityAndClearResetOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSwayBindingCleanupTest::RunTest(const FString& Parameters)
{
	UWorld::InitializationValues Values;
	Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
		ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("sway binding test world"), World)) return false;
	struct FWorldCleanup
	{
		UWorld* World;
		~FWorldCleanup() { World->DestroyWorld(false); }
	} Cleanup{World};
	AActor* Owner = World->SpawnActor<AActor>();
	AWeaponRuntime* Weapon = World->SpawnActor<AWeaponRuntime>();
	if (!TestNotNull(TEXT("owner spawned"), Owner) || !TestNotNull(TEXT("weapon spawned"), Weapon)) return false;
	USceneComponent* Root = NewObject<USceneComponent>(Owner);
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	UWeaponPresentationComponent* Presentation = NewObject<UWeaponPresentationComponent>(Owner);
	Presentation->RegisterComponent();
	UChildActorComponent* Slot = NewObject<UChildActorComponent>(Owner);
	Slot->SetupAttachment(Root);
	Slot->RegisterComponent();
	FObjectPropertyBase* BaseClass = FindFProperty<FObjectPropertyBase>(
		Presentation->GetClass(), TEXT("VisualWeaponBaseClass"));
	if (!TestNotNull(TEXT("base class property"), BaseClass)) return false;
	BaseClass->SetObjectPropertyValue_InContainer(Presentation, AActor::StaticClass());
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(Weapon);
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>(Definition);
	Definition->PresentationData = Data;
	Weapon->WeaponDefinition = Definition;
	Data->bEnablePlayerProfile = true;
	Data->PlayerProfile.VisualWeaponClass = AActor::StaticClass();
	Data->PlayerProfile.bUseViewmodelSettings = true;
	Data->PlayerProfile.ViewmodelSettings.IdlePose = NewObject<UAnimSequence>(Data);
	Data->PlayerProfile.ViewmodelSettings.MovementSway = MakeSettings();
	AActor* Visual = nullptr;
	FWeaponPresentationProfile OutProfile;
	int64 FirstId = 0;
	if (!TestTrue(TEXT("first real visual binding"),
		Presentation->TryBindVisualWeaponPresentation(Weapon, Slot, Visual, OutProfile, FirstId))) return false;
	TestTrue(TEXT("native sway initializes from current native data without TSV"),
		Presentation->InitializeNativeSway(Weapon, Visual, FirstId));
	int64 SecondId = 0;
	if (!TestTrue(TEXT("second binding creates a newer identity"),
		Presentation->TryBindVisualWeaponPresentation(Weapon, Slot, Visual, OutProfile, SecondId))) return false;
	TestTrue(TEXT("new owner identity initializes"), Presentation->InitializeNativeSway(Weapon, Visual, SecondId));
	Presentation->ComputeNativeMoveSwayTarget(FVector(1.0, 2.0, 3.0), FVector(4.0, 5.0, 6.0), false, 1.0f);
	Presentation->ComputeNativeMoveSway(0.016f);
	const FTransform BeforeStale = Presentation->NativeMoveSway;
	TestFalse(TEXT("native output has moved before clear"), BeforeStale.Equals(FTransform::Identity));
	TestFalse(TEXT("old binding cannot reinitialize or clear current sway"),
		Presentation->InitializeNativeSway(Weapon, Visual, FirstId));
	TestTrue(TEXT("stale request leaves new output untouched"), Presentation->NativeMoveSway.Equals(BeforeStale));
	Presentation->ClearVisualWeaponPresentation();
	TestTrue(TEXT("clear resets both native output transforms"),
		Presentation->NativeMoveSway.Equals(FTransform::Identity) && Presentation->NativeAimSway.Equals(FTransform::Identity));
	Presentation->ComputeNativeMoveSway(0.016f);
	TestTrue(TEXT("cleared state cannot advance again"), Presentation->NativeMoveSway.Equals(FTransform::Identity));
	return true;
}

#endif

