#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/DustWeaponLoadoutTransition.h"
#include <limits>

namespace
{
	/** Explicit tuning so assertions do not silently depend on shipped defaults. */
	FWeaponLoadoutDisplaySettings MakeLoadoutSettings()
	{
		FWeaponLoadoutDisplaySettings Settings;
		Settings.CurrentOffset = FVector2D(10.0f, 20.0f);
		Settings.ReserveOffset = FVector2D(-30.0f, -78.0f);
		Settings.CurrentScale = 1.1f;
		Settings.ReserveScale = 0.6f;
		Settings.CurrentOpacity = 0.9f;
		Settings.ReserveOpacity = 0.4f;
		Settings.TransitionSeconds = 0.2f;
		return Settings;
	}

	void InitLoadout(
		FWeaponLoadoutDisplayState& State,
		const FWeaponLoadoutDisplaySettings& Settings)
	{
		State.UpdateTrack(0, 1001, true, true, Settings, true);
		State.UpdateTrack(1, 2002, true, false, Settings, true);
	}

	bool PoseMatches(
		const FWeaponLoadoutPose& Pose,
		const FVector2D& Offset,
		float Scale,
		float Opacity)
	{
		return Pose.Offset.Equals(Offset, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(Pose.Scale, Scale, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(Pose.Opacity, Opacity, KINDA_SMALL_NUMBER);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutNormalSwapTest,
	"Dust2.UI.WeaponLoadout.NormalSwapMovesSameBrushes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutNormalSwapTest::RunTest(const FString& Parameters)
{
	const FWeaponLoadoutDisplaySettings Settings = MakeLoadoutSettings();
	FWeaponLoadoutDisplayState State;
	InitLoadout(State, Settings);

	TestFalse(TEXT("initial state is settled"), State.IsAnimating());
	TestEqual(TEXT("slot 1 keeps its brush"), State.Tracks[0].GetVisibleBrushId(), uint64(1001));
	TestEqual(TEXT("slot 2 keeps its brush"), State.Tracks[1].GetVisibleBrushId(), uint64(2002));

	// Switch: slot 1 becomes reserve, slot 2 becomes current. Brushes stay on their tracks.
	State.UpdateTrack(0, 1001, true, false, Settings, false);
	State.UpdateTrack(1, 2002, true, true, Settings, false);
	TestTrue(TEXT("both tracks animate the switch"), State.IsAnimating());
	TestEqual(TEXT("slot 1 still shows its own brush"), State.Tracks[0].GetVisibleBrushId(), uint64(1001));
	TestEqual(TEXT("slot 2 still shows its own brush"), State.Tracks[1].GetVisibleBrushId(), uint64(2002));

	State.Advance(0.1f);
	const FWeaponLoadoutPose Slot1Mid = State.Tracks[0].GetPose();
	const FWeaponLoadoutPose Slot2Mid = State.Tracks[1].GetPose();
	TestTrue(TEXT("slot 1 moves upward from the current pose"), Slot1Mid.Offset.Y < 20.0f);
	TestTrue(TEXT("slot 1 has not reached the reserve pose yet"), Slot1Mid.Offset.Y > -78.0f);
	TestTrue(TEXT("slot 2 moves downward from the reserve pose"), Slot2Mid.Offset.Y > -78.0f);
	TestTrue(TEXT("slot 2 has not reached the current pose yet"), Slot2Mid.Offset.Y < 20.0f);

	State.Advance(0.1f);
	TestFalse(TEXT("swap completes after the duration"), State.IsAnimating());
	TestTrue(TEXT("slot 1 lands exactly on the reserve pose"),
		PoseMatches(State.Tracks[0].GetPose(), FVector2D(-30.0f, -78.0f), 0.6f, 0.4f));
	TestTrue(TEXT("slot 2 lands exactly on the current pose"),
		PoseMatches(State.Tracks[1].GetPose(), FVector2D(10.0f, 20.0f), 1.1f, 0.9f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutDropCachedBrushTest,
	"Dust2.UI.WeaponLoadout.DroppedCurrentFadesCachedBrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutDropCachedBrushTest::RunTest(const FString& Parameters)
{
	const FWeaponLoadoutDisplaySettings Settings = MakeLoadoutSettings();
	FWeaponLoadoutDisplayState State;
	InitLoadout(State, Settings);

	// Current slot is emptied; only the reserve slot still has a weapon.
	State.UpdateTrack(0, 0, false, false, Settings, false);
	State.UpdateTrack(1, 2002, true, true, Settings, false);
	TestTrue(TEXT("dropped track still has a cached brush to fade"), State.Tracks[0].HasVisibleBrush());
	TestEqual(TEXT("dropped track keeps the dropped brush id"), State.Tracks[0].GetVisibleBrushId(), uint64(1001));

	State.Advance(0.1f);
	TestTrue(TEXT("dropped brush is still visible during the fade"), State.Tracks[0].HasVisibleBrush());
	TestTrue(TEXT("dropped track fades down"), State.Tracks[0].GetPose().Opacity < 0.9f);
	TestTrue(TEXT("reserve track moves down toward current"), State.Tracks[1].GetPose().Offset.Y > -78.0f);

	State.Advance(0.1f);
	TestFalse(TEXT("fade-out finishes"), State.IsAnimating());
	TestFalse(TEXT("cached brush is cleared after fading"), State.Tracks[0].HasVisibleBrush());
	TestEqual(TEXT("empty track has no brush id"), State.Tracks[0].GetVisibleBrushId(), uint64(0));
	TestEqual(TEXT("empty track is fully transparent"), State.Tracks[0].GetPose().Opacity, 0.0f, 0.0f);
	TestTrue(TEXT("former reserve track lands on current"),
		PoseMatches(State.Tracks[1].GetPose(), FVector2D(10.0f, 20.0f), 1.1f, 0.9f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutDuplicateNotificationTest,
	"Dust2.UI.WeaponLoadout.DuplicateNotificationDoesNotRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutDuplicateNotificationTest::RunTest(const FString& Parameters)
{
	const FWeaponLoadoutDisplaySettings Settings = MakeLoadoutSettings();
	FWeaponLoadoutDisplayState State;
	InitLoadout(State, Settings);

	State.UpdateTrack(0, 1001, true, false, Settings, false);
	State.Advance(0.05f);
	const float RemainingBefore = State.Tracks[0].GetRemainingSeconds();
	const FVector2D OffsetBefore = State.Tracks[0].GetPose().Offset;

	// Exact same notification while the transition is running.
	State.UpdateTrack(0, 1001, true, false, Settings, false);
	TestEqual(TEXT("duplicate notification keeps the remaining time"),
		State.Tracks[0].GetRemainingSeconds(), RemainingBefore, 0.0f);
	TestEqual(TEXT("duplicate notification does not reset the displayed offset"),
		State.Tracks[0].GetPose().Offset.Y, OffsetBefore.Y, 0.0);

	State.Advance(RemainingBefore);
	TestFalse(TEXT("transition completes normally"), State.IsAnimating());

	const FWeaponLoadoutPose SettledPose = State.Tracks[0].GetPose();
	State.UpdateTrack(0, 1001, true, false, Settings, false);
	TestFalse(TEXT("duplicate notification does not start a settled animation"), State.IsAnimating());
	TestTrue(TEXT("settled duplicate leaves the pose untouched"),
		PoseMatches(State.Tracks[0].GetPose(), SettledPose.Offset, SettledPose.Scale, SettledPose.Opacity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutRapidInterruptTest,
	"Dust2.UI.WeaponLoadout.InterruptedTransitionStartsFromCurrentPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutRapidInterruptTest::RunTest(const FString& Parameters)
{
	const FWeaponLoadoutDisplaySettings Settings = MakeLoadoutSettings();
	FWeaponLoadoutDisplayState State;
	InitLoadout(State, Settings);

	// Start moving slot 1 upward, then switch back before it arrives.
	State.UpdateTrack(0, 1001, true, false, Settings, false);
	State.Advance(0.07f);
	const FWeaponLoadoutPose PoseBeforeInterrupt = State.Tracks[0].GetPose();

	State.UpdateTrack(0, 1001, true, true, Settings, false);
	TestTrue(TEXT("interrupt restarts a transition"), State.IsAnimating());
	TestEqual(TEXT("interrupt keeps the visible offset"),
		State.Tracks[0].GetPose().Offset.Y, PoseBeforeInterrupt.Offset.Y, 0.0);
	TestEqual(TEXT("interrupt keeps the visible scale"),
		State.Tracks[0].GetPose().Scale, PoseBeforeInterrupt.Scale, 0.0f);
	TestEqual(TEXT("interrupt keeps the visible opacity"),
		State.Tracks[0].GetPose().Opacity, PoseBeforeInterrupt.Opacity, 0.0f);
	TestEqual(TEXT("interrupt starts a fresh full duration"),
		State.Tracks[0].GetRemainingSeconds(), Settings.TransitionSeconds, 0.0f);

	State.Advance(Settings.TransitionSeconds);
	TestTrue(TEXT("interrupted transition reaches the latest target"),
		PoseMatches(State.Tracks[0].GetPose(), FVector2D(10.0f, 20.0f), 1.1f, 0.9f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutReplacementInterruptTest,
	"Dust2.UI.WeaponLoadout.InterruptedReplacementKeepsVisibleBrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutReplacementInterruptTest::RunTest(const FString& Parameters)
{
	const FWeaponLoadoutDisplaySettings Settings = MakeLoadoutSettings();
	FWeaponLoadoutDisplayState State;
	InitLoadout(State, Settings);

	// Replace current brush 1001 with 1100. The old brush fades out first.
	State.UpdateTrack(0, 1100, true, true, Settings, false);
	TestEqual(TEXT("replacement keeps the old brush visible initially"),
		State.Tracks[0].GetVisibleBrushId(), uint64(1001));
	State.Advance(0.05f);
	TestEqual(TEXT("old brush still visible in the first half"),
		State.Tracks[0].GetVisibleBrushId(), uint64(1001));

	// Interrupt with a third brush before 1100 became visible. 1100 must never be
	// treated as the visible identity; the fade continues from brush 1001.
	State.UpdateTrack(0, 1200, true, true, Settings, false);
	TestEqual(TEXT("interrupt still shows the actually visible old brush"),
		State.Tracks[0].GetVisibleBrushId(), uint64(1001));
	TestEqual(TEXT("interrupt targets the latest brush"),
		State.Tracks[0].GetTargetBrushId(), uint64(1200));

	State.Advance(0.1f);
	TestEqual(TEXT("latest brush is committed at the midpoint"),
		State.Tracks[0].GetVisibleBrushId(), uint64(1200));
	TestEqual(TEXT("midpoint is fully transparent"), State.Tracks[0].GetPose().Opacity, 0.0f, 0.0f);

	State.Advance(0.1f);
	TestFalse(TEXT("replacement finishes within one duration"), State.IsAnimating());
	TestEqual(TEXT("latest brush remains after fade-in"), State.Tracks[0].GetVisibleBrushId(), uint64(1200));
	TestTrue(TEXT("replacement lands on the current pose"),
		PoseMatches(State.Tracks[0].GetPose(), FVector2D(10.0f, 20.0f), 1.1f, 0.9f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutReplacementTwoPhaseTest,
	"Dust2.UI.WeaponLoadout.ReplacementFadesOutThenInAndSparesOtherTrack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutReplacementTwoPhaseTest::RunTest(const FString& Parameters)
{
	const FWeaponLoadoutDisplaySettings Settings = MakeLoadoutSettings();
	FWeaponLoadoutDisplayState State;
	InitLoadout(State, Settings);

	const FWeaponLoadoutPose OtherTrackPose = State.Tracks[0].GetPose();

	// Replace the reserve brush; the other slot must not move.
	State.UpdateTrack(1, 2100, true, false, Settings, false);
	TestTrue(TEXT("only the replaced track animates"), State.Tracks[1].IsAnimating());
	TestFalse(TEXT("other track stays idle"), State.Tracks[0].IsAnimating());
	TestTrue(TEXT("other track keeps its pose"),
		PoseMatches(State.Tracks[0].GetPose(), OtherTrackPose.Offset, OtherTrackPose.Scale, OtherTrackPose.Opacity));
	TestEqual(TEXT("old reserve brush is the visible one"), State.Tracks[1].GetVisibleBrushId(), uint64(2002));

	State.Advance(0.1f);
	TestEqual(TEXT("midpoint swaps to the new brush"), State.Tracks[1].GetVisibleBrushId(), uint64(2100));
	TestEqual(TEXT("midpoint is invisible"), State.Tracks[1].GetPose().Opacity, 0.0f, 0.0f);

	State.Advance(0.1f);
	TestFalse(TEXT("replacement finishes in one duration"), State.Tracks[1].IsAnimating());
	TestEqual(TEXT("new reserve brush remains"), State.Tracks[1].GetVisibleBrushId(), uint64(2100));
	TestTrue(TEXT("new brush lands on the reserve pose"),
		PoseMatches(State.Tracks[1].GetPose(), FVector2D(-30.0f, -78.0f), 0.6f, 0.4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutZeroDurationTest,
	"Dust2.UI.WeaponLoadout.ZeroOrInvalidValuesSnapAndSanitize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutZeroDurationTest::RunTest(const FString& Parameters)
{
	FWeaponLoadoutDisplaySettings ZeroSettings = MakeLoadoutSettings();
	ZeroSettings.TransitionSeconds = 0.0f;

	FWeaponLoadoutDisplayState State;
	InitLoadout(State, ZeroSettings);
	State.UpdateTrack(0, 1001, true, false, ZeroSettings, false);
	TestFalse(TEXT("zero duration never animates"), State.IsAnimating());
	TestTrue(TEXT("zero duration lands exactly on the reserve pose"),
		PoseMatches(State.Tracks[0].GetPose(), FVector2D(-30.0f, -78.0f), 0.6f, 0.4f));

	FWeaponLoadoutDisplaySettings NegativeSettings = MakeLoadoutSettings();
	NegativeSettings.TransitionSeconds = -3.0f;
	FWeaponLoadoutDisplayState NegativeState;
	InitLoadout(NegativeState, NegativeSettings);
	NegativeState.UpdateTrack(0, 1001, true, false, NegativeSettings, false);
	TestFalse(TEXT("negative duration never animates"), NegativeState.IsAnimating());
	TestTrue(TEXT("negative duration lands on the reserve pose"),
		PoseMatches(NegativeState.Tracks[0].GetPose(), FVector2D(-30.0f, -78.0f), 0.6f, 0.4f));

	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	FWeaponLoadoutDisplaySettings BadSettings;
	BadSettings.CurrentOffset = FVector2D(NaN, Infinity);
	BadSettings.ReserveOffset = FVector2D(NaN, Infinity);
	BadSettings.CurrentScale = NaN;
	BadSettings.ReserveScale = Infinity;
	BadSettings.CurrentOpacity = NaN;
	BadSettings.ReserveOpacity = Infinity;
	BadSettings.TransitionSeconds = NaN;

	const FWeaponLoadoutDisplaySettings Safe = BadSettings.GetSanitized();
	TestEqual(TEXT("non-finite current offset X falls back"), Safe.CurrentOffset.X, 0.0, 0.0);
	TestEqual(TEXT("non-finite current offset Y falls back"), Safe.CurrentOffset.Y, 0.0, 0.0);
	TestEqual(TEXT("non-finite reserve offset Y falls back"), Safe.ReserveOffset.Y, -88.0, 0.0);
	TestEqual(TEXT("non-finite current scale falls back"), Safe.CurrentScale, 1.0f, 0.0f);
	TestEqual(TEXT("non-finite reserve scale falls back"), Safe.ReserveScale, 0.72f, 0.0f);
	TestEqual(TEXT("non-finite current opacity falls back"), Safe.CurrentOpacity, 1.0f, 0.0f);
	TestEqual(TEXT("non-finite reserve opacity falls back"), Safe.ReserveOpacity, 0.65f, 0.0f);
	TestEqual(TEXT("non-finite duration becomes zero"), Safe.TransitionSeconds, 0.0f, 0.0f);
	BadSettings.CurrentScale = -1.0f;
	BadSettings.ReserveScale = -2.0f;
	TestEqual(TEXT("negative current scale never mirrors the icon"),
		BadSettings.GetSanitized().CurrentScale, 0.0f, 0.0f);
	TestEqual(TEXT("negative reserve scale never mirrors the icon"),
		BadSettings.GetSanitized().ReserveScale, 0.0f, 0.0f);
	BadSettings.CurrentScale = NaN;
	BadSettings.ReserveScale = Infinity;

	FWeaponLoadoutDisplayState BadState;
	InitLoadout(BadState, BadSettings);
	BadState.UpdateTrack(0, 1001, true, false, BadSettings, false);
	TestFalse(TEXT("sanitized NaN duration snaps"), BadState.IsAnimating());
	TestTrue(TEXT("sanitized NaN duration lands on the reserve pose"),
		PoseMatches(BadState.Tracks[0].GetPose(), FVector2D(0.0f, -88.0f), 0.72f, 0.65f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponLoadoutResetTest,
	"Dust2.UI.WeaponLoadout.ResetClearsTracksAndNextUpdateSnaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponLoadoutResetTest::RunTest(const FString& Parameters)
{
	const FWeaponLoadoutDisplaySettings Settings = MakeLoadoutSettings();
	FWeaponLoadoutDisplayState State;
	InitLoadout(State, Settings);

	State.UpdateTrack(0, 1100, true, true, Settings, false);
	State.UpdateTrack(1, 2002, true, false, Settings, false);
	State.Advance(0.06f);
	TestTrue(TEXT("state is animating before reset"), State.IsAnimating());

	State.Reset();
	for (int32 SlotIndex = 0; SlotIndex < FWeaponLoadoutDisplayState::TrackCount; ++SlotIndex)
	{
		const FWeaponLoadoutTrackState& Track = State.Tracks[SlotIndex];
		TestFalse(TEXT("reset stops animation"), Track.IsAnimating());
		TestFalse(TEXT("reset clears initialization"), Track.IsInitialized());
		TestFalse(TEXT("reset clears the visible brush"), Track.HasVisibleBrush());
		TestEqual(TEXT("reset clears the brush id"), Track.GetVisibleBrushId(), uint64(0));
		TestEqual(TEXT("reset leaves the track transparent"), Track.GetPose().Opacity, 0.0f, 0.0f);
	}

	// The first non-immediate notification after reset still snaps directly.
	State.UpdateTrack(0, 1001, true, true, Settings, false);
	TestFalse(TEXT("first update after reset does not animate"), State.Tracks[0].IsAnimating());
	TestTrue(TEXT("first update after reset lands on current"),
		PoseMatches(State.Tracks[0].GetPose(), FVector2D(10.0f, 20.0f), 1.1f, 0.9f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
