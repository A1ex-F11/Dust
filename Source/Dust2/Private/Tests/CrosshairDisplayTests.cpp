#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CrosshairDisplayMath.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairTransitionDeadlineTest,
	"Dust2.Crosshair.TransitionDeadlineAndSmoothStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairTransitionDeadlineTest::RunTest(const FString& Parameters)
{
	FCrosshairDisplayTransitionState State;
	State.Initialize(FVector2D::ZeroVector);
	State.BeginTransition(1.0f);

	const FVector2D Midpoint = State.Advance(FVector2D(20.0f, 10.0f), 0.5f, 12.0f);
	TestTrue(TEXT("smoothstep reaches its midpoint halfway through"),
		Midpoint.Equals(FVector2D(10.0f, 5.0f), 0.0001f));

	const FVector2D AtDeadline = State.Advance(FVector2D(20.0f, 10.0f), 0.5f, 12.0f);
	TestTrue(TEXT("deadline lands exactly on the target"),
		AtDeadline.Equals(FVector2D(20.0f, 10.0f), 0.0f));
	TestFalse(TEXT("transition ends at its deadline"), State.IsTransitioning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairMovingTargetTest,
	"Dust2.Crosshair.TransitionTracksMovingTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairMovingTargetTest::RunTest(const FString& Parameters)
{
	FCrosshairDisplayTransitionState State;
	State.Initialize(FVector2D::ZeroVector);
	State.BeginTransition(1.0f);
	State.Advance(FVector2D(20.0f, 20.0f), 0.5f, 12.0f);

	const FVector2D UpdatedTarget = State.Advance(FVector2D(40.0f, 10.0f), 0.25f, 12.0f);
	TestTrue(TEXT("in-flight transition follows the newest target"),
		UpdatedTarget.Equals(FVector2D(33.75f, 8.4375f), 0.0001f));

	const FVector2D AtDeadline = State.Advance(FVector2D(30.0f, 4.0f), 0.25f, 12.0f);
	TestTrue(TEXT("moving endpoint is exact at deadline"),
		AtDeadline.Equals(FVector2D(30.0f, 4.0f), 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairRestartTransitionTest,
	"Dust2.Crosshair.RestartStartsAtCurrentGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairRestartTransitionTest::RunTest(const FString& Parameters)
{
	FCrosshairDisplayTransitionState State;
	State.Initialize(FVector2D::ZeroVector);
	State.BeginTransition(1.0f);
	State.Advance(FVector2D(10.0f, 10.0f), 0.5f, 12.0f);

	const FVector2D BeforeRestart = State.GetCurrentGap();
	State.BeginTransition(1.0f);
	TestTrue(TEXT("restart preserves the displayed gap"),
		State.GetCurrentGap().Equals(BeforeRestart, 0.0f));

	const FVector2D AfterRestart = State.Advance(FVector2D(20.0f, 20.0f), 0.5f, 12.0f);
	TestTrue(TEXT("restarted smoothstep begins from the current display"),
		AfterRestart.Equals(FVector2D(12.5f, 12.5f), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairConstrainAndFinishTest,
	"Dust2.Crosshair.ConstrainRemainingAndFinish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairConstrainAndFinishTest::RunTest(const FString& Parameters)
{
	FCrosshairDisplayTransitionState State;
	State.Initialize(FVector2D::ZeroVector);
	State.BeginTransition(1.0f);
	State.Advance(FVector2D(20.0f, 20.0f), 0.5f, 12.0f);

	TestTrue(TEXT("shorter animation deadline constrains remaining time"),
		State.ConstrainRemainingTime(0.2f));
	TestTrue(TEXT("remaining time becomes the shorter deadline"),
		FMath::IsNearlyEqual(State.GetRemainingTime(), 0.2f, 0.0001f));
	const FVector2D HalfwayToNewDeadline = State.Advance(FVector2D(30.0f, 30.0f), 0.1f, 12.0f);
	TestTrue(TEXT("constrained transition restarts at displayed gap"),
		HalfwayToNewDeadline.Equals(FVector2D(20.0f, 20.0f), 0.0001f));
	TestTrue(TEXT("cannot extend a transition with a longer cap"),
		!State.ConstrainRemainingTime(1.0f));

	State.FinishTransition(FVector2D(12.0f, 8.0f));
	TestTrue(TEXT("finish immediately applies the target"),
		State.GetCurrentGap().Equals(FVector2D(12.0f, 8.0f), 0.0f));
	TestFalse(TEXT("finish clears transition state"), State.IsTransitioning());
	TestEqual(TEXT("finished transition has no remaining time"), State.GetRemainingTime(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairInvalidInputTest,
	"Dust2.Crosshair.InvalidInputsStayFiniteAndNonNegative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairInvalidInputTest::RunTest(const FString& Parameters)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();

	FCrosshairDisplayTransitionState State;
	State.Initialize(FVector2D(NaN, Infinity));
	TestTrue(TEXT("invalid initial gap becomes finite zero"),
		State.GetCurrentGap().Equals(FVector2D::ZeroVector, 0.0f));

	State.BeginTransition(NaN);
	const FVector2D Snapped = State.Advance(FVector2D(-2.0f, 5.0f), NaN, Infinity);
	TestTrue(TEXT("invalid duration snaps on next advance and negative gap clamps"),
		Snapped.Equals(FVector2D(0.0f, 5.0f), 0.0f));

	State.Advance(FVector2D(Infinity, NaN), -1.0f, -4.0f);
	TestTrue(TEXT("invalid target and negative rates preserve a safe gap"),
		State.GetCurrentGap().Equals(FVector2D(0.0f, 5.0f), 0.0f));

	State.BeginTransition(1.0f);
	State.Advance(FVector2D(10.0f, 10.0f), -1.0f, 12.0f);
	TestTrue(TEXT("negative time does not move transition backward"),
		State.GetCurrentGap().Equals(FVector2D(0.0f, 5.0f), 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairFramePartitionTest,
	"Dust2.Crosshair.ExponentialSmoothingIsFramePartitionInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairFramePartitionTest::RunTest(const FString& Parameters)
{
	FCrosshairDisplayTransitionState SingleStep;
	FCrosshairDisplayTransitionState TenSteps;
	SingleStep.Initialize(FVector2D(1.0f, 4.0f));
	TenSteps.Initialize(FVector2D(1.0f, 4.0f));

	const FVector2D Target(30.0f, 15.0f);
	const FVector2D OneSecond = SingleStep.Advance(Target, 1.0f, 5.0f);
	for (int32 Index = 0; Index < 10; ++Index)
	{
		TenSteps.Advance(Target, 0.1f, 5.0f);
	}

	TestTrue(TEXT("constant target gives equivalent one-step and split-step results"),
		OneSecond.Equals(TenSteps.GetCurrentGap(), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCrosshairVisualGainTest,
    "Dust2.Crosshair.Display.FiringFeedbackDoesNotAmplifyBase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairVisualGainTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Scale one preserves true total angle"), FCrosshairDisplayAngle::WithFiringFeedback(4, 1, 1), 4.0f);
    TestEqual(TEXT("Scale two doubles only bloom"), FCrosshairDisplayAngle::WithFiringFeedback(4, 1, 2), 5.0f);
    TestEqual(TEXT("Movement without bloom is unchanged"), FCrosshairDisplayAngle::WithFiringFeedback(12, 0, 3), 12.0f);
    TestEqual(TEXT("Display cone bounded"), FCrosshairDisplayAngle::WithFiringFeedback(80, 50, 3), 89.0f);
    TestEqual(TEXT("Bad multiplier defaults to true precision"), FCrosshairDisplayAngle::WithFiringFeedback(4, 1,
        std::numeric_limits<float>::quiet_NaN()), 4.0f);
    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
