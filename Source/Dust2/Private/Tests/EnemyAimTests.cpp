#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>
#include "DustEnemyAimMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAimCompensationFractionTest,
	"Dust2.EnemyAim.CompensationFraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAimCompensationFractionTest::RunTest(const FString& Parameters)
{
	const FVector2D Recoil(10.0f, 4.0f);
	const FVector2D LowSkill = FEnemyAimMath::StepCompensation(
		FVector2D::ZeroVector, Recoil, 1.0f, 0.25f, 100.0f);
	const FVector2D HighSkill = FEnemyAimMath::StepCompensation(
		FVector2D::ZeroVector, Recoil, 1.0f, 1.0f, 100.0f);

	TestTrue(TEXT("lower fraction produces lower pitch compensation"), LowSkill.X < HighSkill.X);
	TestTrue(TEXT("lower fraction produces lower yaw compensation"), LowSkill.Y < HighSkill.Y);
	TestTrue(TEXT("low compensation follows the configured fraction"),
		LowSkill.Equals(FVector2D(2.5f, 1.0f), 0.001f));
	TestTrue(TEXT("high compensation follows the recoil target"),
		HighSkill.Equals(Recoil, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAimCompensationSpeedTest,
	"Dust2.EnemyAim.CompensationSpeedBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAimCompensationSpeedTest::RunTest(const FString& Parameters)
{
	const FVector2D Result = FEnemyAimMath::StepCompensation(
		FVector2D::ZeroVector, FVector2D(8.0f, -6.0f), 0.1f, 1.0f, 2.0f);
	TestTrue(TEXT("compensation advances by no more than speed times delta"),
		Result.Size() <= 0.2001f);
	TestTrue(TEXT("compensation moves toward the desired recoil"),
		FVector2D::DotProduct(Result, FVector2D(8.0f, -6.0f)) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAimTrackingYawWrapTest,
	"Dust2.EnemyAim.TrackingWrapsYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAimTrackingYawWrapTest::RunTest(const FString& Parameters)
{
	const FRotator Result = FEnemyAimMath::StepTracking(
		FRotator(0.0f, 179.0f, 0.0f), FRotator(0.0f, -179.0f, 0.0f), 0.1f, 20.0f);
	const float MovedDegrees = FMath::FindDeltaAngleDegrees(179.0f, Result.Yaw);

	TestTrue(TEXT("tracking takes the short route across the yaw boundary"),
		FMath::IsNearlyEqual(MovedDegrees, 2.0f, 0.01f));
	TestTrue(TEXT("result stays finite"),
		FMath::IsFinite(Result.Pitch) && FMath::IsFinite(Result.Yaw) && FMath::IsFinite(Result.Roll));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAimFiniteInputTest,
	"Dust2.EnemyAim.InvalidNumbersStayFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAimFiniteInputTest::RunTest(const FString& Parameters)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const FVector2D Compensation = FEnemyAimMath::StepCompensation(
		FVector2D(NaN, 3.0f), FVector2D(5.0f, NaN), NaN, NaN, 30.0f);
	const FRotator Tracking = FEnemyAimMath::StepTracking(
		FRotator(NaN, 20.0f, 0.0f), FRotator(10.0f, NaN, NaN), 0.1f, 120.0f);
	const FVector2D Recovery = FEnemyAimMath::Recover(
		FVector2D(NaN, -4.0f), 0.2f, NaN);

	TestTrue(TEXT("compensation output remains finite"),
		FMath::IsFinite(Compensation.X) && FMath::IsFinite(Compensation.Y));
	TestTrue(TEXT("tracking output remains finite"),
		FMath::IsFinite(Tracking.Pitch) && FMath::IsFinite(Tracking.Yaw) && FMath::IsFinite(Tracking.Roll));
	TestTrue(TEXT("recovery output remains finite"),
		FMath::IsFinite(Recovery.X) && FMath::IsFinite(Recovery.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAimRecoveryNoOvershootTest,
	"Dust2.EnemyAim.RecoveryDoesNotOvershoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAimRecoveryNoOvershootTest::RunTest(const FString& Parameters)
{
	const FVector2D Result = FEnemyAimMath::Recover(
		FVector2D(0.3f, -0.4f), 1.0f, 1.0f);
	TestTrue(TEXT("recovery reaches zero when the speed budget is sufficient"),
		Result.IsNearlyZero(0.001f));

	const FVector2D Limited = FEnemyAimMath::Recover(
		FVector2D(0.3f, -0.4f), 0.1f, 1.0f);
	TestTrue(TEXT("limited recovery stays between the offset and zero"),
		FVector2D::DotProduct(Limited, FVector2D(0.3f, -0.4f)) >= 0.0f &&
		Limited.Size() <= FVector2D(0.3f, -0.4f).Size());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAimTracksDownwardTest,
	"Dust2.EnemyAim.TracksDownward", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyAimTracksDownwardTest::RunTest(const FString& Parameters)
{
	const FRotator Result = FEnemyAimMath::StepTracking(FRotator::ZeroRotator, FRotator(-20, 0, 0), 0.1f, 100.0f);
	TestTrue(TEXT("negative pitch follows downward without wrapping upward"), FMath::IsNearlyEqual(Result.Pitch, -10.0, 0.001));
	return true;
}
#endif
