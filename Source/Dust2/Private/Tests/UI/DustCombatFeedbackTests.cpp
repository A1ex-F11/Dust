#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/DustCombatFeedbackMath.h"
#include <limits>

namespace
{
	const float CombatFeedbackTestNaN = std::numeric_limits<float>::quiet_NaN();
	const float CombatFeedbackTestInfinity = std::numeric_limits<float>::infinity();

	FVector2D ExpectedArmDirection(int32 ArmIndex)
	{
		const float Radians = FMath::DegreesToRadians(
			FDustCombatTaperGeometry::GetArmAngleDegrees(ArmIndex));
		return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackInvalidInputTest,
	"Dust2.CombatFeedback.InvalidDamageAndElapsedAreIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackInvalidInputTest::RunTest(const FString& Parameters)
{
	FDustCombatFeedbackState State;

	TestFalse(TEXT("zero damage never flashes"), State.NotifyConfirmedHit(0.0f, false, false));
	TestFalse(TEXT("negative damage never flashes"), State.NotifyConfirmedHit(-4.0f, true, false));
	TestFalse(TEXT("NaN damage never flashes"), State.NotifyConfirmedHit(CombatFeedbackTestNaN, false, false));
	TestFalse(TEXT("infinite damage never flashes"), State.NotifyConfirmedHit(CombatFeedbackTestInfinity, true, true));
	TestFalse(TEXT("a kill flag with zero damage still adds nothing"),
		State.NotifyConfirmedHit(0.0f, false, true));
	TestFalse(TEXT("no marker was started"), State.Center.IsMarkerActive());
	TestFalse(TEXT("no skull was started"), State.Center.IsSkullActive());
	TestEqual(TEXT("no feed row was created"), State.Feed.Num(), 0);

	TestTrue(TEXT("finite positive damage is accepted"), State.NotifyConfirmedHit(12.0f, false, false));
	TestTrue(TEXT("marker is active"), State.Center.IsMarkerActive());
	State.Advance(0.05f);
	const float RemainingBeforeInvalidDeltas = State.Center.GetMarkerRemainingSeconds();

	State.Advance(CombatFeedbackTestNaN);
	State.Advance(CombatFeedbackTestInfinity);
	State.Advance(-1.0f);
	State.Advance(0.0f);
	TestEqual(TEXT("invalid or negative elapsed never moves a timer"),
		State.Center.GetMarkerRemainingSeconds(), RemainingBeforeInvalidDeltas, 0.0f);
	TestTrue(TEXT("remaining time stays non-negative"),
		State.Center.GetMarkerRemainingSeconds() >= 0.0f);

	State.Advance(1.0f);
	TestFalse(TEXT("marker expires and stops drawing"), State.Center.IsMarkerActive());
	TestEqual(TEXT("expired marker reports zero remaining"),
		State.Center.GetMarkerRemainingSeconds(), 0.0f, 0.0f);
	TestEqual(TEXT("expired marker alpha is zero"), State.Center.GetMarkerAlpha(), 0.0f, 0.0f);

	FDustCombatFeedbackTuning BadTuning;
	BadTuning.MarkerFlashSeconds = CombatFeedbackTestNaN;
	BadTuning.SkullSeconds = CombatFeedbackTestInfinity;
	BadTuning.FeedShiftSeconds = -2.0f;
	const FDustCombatFeedbackTuning SafeTuning = BadTuning.Sanitized();
	TestTrue(TEXT("NaN marker duration falls back to the finite default"),
		FMath::IsNearlyEqual(SafeTuning.MarkerFlashSeconds, 0.12f, 0.0001f));
	TestTrue(TEXT("infinite skull duration is bounded"),
		FMath::IsFinite(SafeTuning.SkullSeconds)
		&& SafeTuning.SkullSeconds <= FDustCombatFeedbackTuning::MaxTimerSeconds);
	TestEqual(TEXT("negative feed motion clamps to zero"), SafeTuning.FeedShiftSeconds, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackHitSelectionTest,
	"Dust2.CombatFeedback.HitColorComesFromHeadBodyMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackHitSelectionTest::RunTest(const FString& Parameters)
{
	const FLinearColor BodyColor(0.80f, 0.81f, 0.82f, 1.0f);
	const FLinearColor HeadColor(0.91f, 0.10f, 0.11f, 1.0f);

	const FDustCombatHitSelection Body = FDustCombatHitSelection::Resolve(
		false, BodyColor, HeadColor);
	TestTrue(TEXT("body selection names the body region"),
		Body.Region == EDustCombatHitRegion::Body);
	TestTrue(TEXT("body selection uses the body color"), Body.Color.Equals(BodyColor, 0.0f));

	const FDustCombatHitSelection Head = FDustCombatHitSelection::Resolve(
		true, BodyColor, HeadColor);
	TestTrue(TEXT("head selection names the head region"),
		Head.Region == EDustCombatHitRegion::Head);
	TestTrue(TEXT("head selection uses the head color"), Head.Color.Equals(HeadColor, 0.0f));

	FDustCombatFeedbackState State;
	TestTrue(TEXT("non-lethal headshot is confirmed"),
		State.NotifyConfirmedHit(20.0f, true, false));
	TestTrue(TEXT("marker stores the head region"), State.Center.IsMarkerHeadshot());
	TestFalse(TEXT("non-lethal headshot starts no skull"), State.Center.IsSkullActive());
	TestEqual(TEXT("non-lethal hit adds no feed row"), State.Feed.Num(), 0);

	TestTrue(TEXT("lethal headshot is confirmed"),
		State.NotifyConfirmedHit(20.0f, true, true));
	TestTrue(TEXT("lethal headshot still selects the head marker color"),
		State.Center.IsMarkerHeadshot());
	TestTrue(TEXT("lethal headshot starts the skull"), State.Center.IsSkullActive());
	TestEqual(TEXT("lethal hit creates exactly one feed row"), State.Feed.Num(), 1);
	TestTrue(TEXT("headshot kill row is the headshot type"),
		State.Feed.GetRows()[0].Event == EDustCombatFeedEvent::HeadshotKill);

	FDustCombatFeedbackState BodyKillState;
	TestTrue(TEXT("lethal body hit is confirmed"),
		BodyKillState.NotifyConfirmedHit(30.0f, false, true));
	TestEqual(TEXT("body kill creates exactly one row, not a normal-hit row"),
		BodyKillState.Feed.Num(), 1);
	TestTrue(TEXT("body kill row is the body-kill type"),
		BodyKillState.Feed.GetRows()[0].Event == EDustCombatFeedEvent::BodyKill);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackIndependentSkullTest,
	"Dust2.CombatFeedback.SkullTimerSurvivesOrdinaryHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackIndependentSkullTest::RunTest(const FString& Parameters)
{
	FDustCombatFeedbackState State;
	TestTrue(TEXT("killing headshot is confirmed"),
		State.NotifyConfirmedHit(100.0f, true, true));
	TestTrue(TEXT("skull timer starts"), State.Center.IsSkullActive());
	TestTrue(TEXT("skull remembers the head region"), State.Center.IsSkullHeadshot());

	State.Advance(0.05f);
	const float SkullRemainingBefore = State.Center.GetSkullRemainingSeconds();
	TestTrue(TEXT("ordinary body hit is confirmed"),
		State.NotifyConfirmedHit(8.0f, false, false));

	TestFalse(TEXT("ordinary hit recolors the marker to body"),
		State.Center.IsMarkerHeadshot());
	TestTrue(TEXT("ordinary hit leaves the skull's head metadata intact"),
		State.Center.IsSkullHeadshot());
	TestEqual(TEXT("ordinary hit does not restart or shorten the skull"),
		State.Center.GetSkullRemainingSeconds(), SkullRemainingBefore, 0.0f);
	TestTrue(TEXT("ordinary hit still refreshes the marker"),
		State.Center.IsMarkerActive());
	TestEqual(TEXT("ordinary hit never adds a feed row"), State.Feed.Num(), 1);

	State.Advance(0.30f);
	TestFalse(TEXT("skull expires on its independent 0.30 s clock"), State.Center.IsSkullActive());
	TestEqual(TEXT("expired skull alpha is zero"), State.Center.GetSkullAlpha(), 0.0f, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackFeedCapacityTest,
	"Dust2.CombatFeedback.FeedKeepsLatestFiveUntilExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackFeedCapacityTest::RunTest(const FString& Parameters)
{
	FDustCombatFeedbackState State;

	State.NotifyConfirmedHit(10.0f, false, true); // BodyKill (oldest, will be discarded)
	State.NotifyConfirmedHit(10.0f, true, true);  // HeadshotKill
	State.NotifyPlayerKilled();                   // PlayerKilled
	State.NotifyConfirmedHit(10.0f, false, true); // BodyKill
	State.NotifyConfirmedHit(10.0f, true, true);  // HeadshotKill
	State.NotifyPlayerKilled();                   // PlayerKilled (sixth push)

	TestEqual(TEXT("hard capacity is five"), State.Feed.Num(), 5);
	const TArray<FDustCombatFeedRow>& Rows = State.Feed.GetRows();
	TestTrue(TEXT("newest row is index zero"),
		Rows[0].Event == EDustCombatFeedEvent::PlayerKilled);
	TestTrue(TEXT("second newest retained"),
		Rows[1].Event == EDustCombatFeedEvent::HeadshotKill);
	TestTrue(TEXT("third row retained"),
		Rows[2].Event == EDustCombatFeedEvent::BodyKill);
	TestTrue(TEXT("fourth row retained"),
		Rows[3].Event == EDustCombatFeedEvent::PlayerKilled);
	TestTrue(TEXT("oldest surviving row is the fifth push"),
		Rows[4].Event == EDustCombatFeedEvent::HeadshotKill);

	State.Advance(0.15f);
	TestEqual(TEXT("five rows survive the slide animation"), State.Feed.Num(), 5);
	TestFalse(TEXT("slide animation has converged"), State.Feed.IsAnimating());
	for (int32 Index = 0; Index < State.Feed.Num(); ++Index)
	{
		TestEqual(TEXT("row settles exactly on its slot index"),
			State.Feed.GetRows()[Index].AnimatedIndex,
			static_cast<float>(Index), 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackFeedExpiryTest,
	"Dust2.CombatFeedback.FeedExpiresIndependentlyAfterThreeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackFeedExpiryTest::RunTest(const FString& Parameters)
{
	FDustCombatFeedbackState State;
	State.NotifyConfirmedHit(10.0f, false, true);
	State.Advance(2.0f);
	State.NotifyConfirmedHit(10.0f, true, true);
	State.Advance(0.5f);
	TestEqual(TEXT("old and new rows coexist before the old deadline"), State.Feed.Num(), 2);
	State.Advance(0.5f);
	TestEqual(TEXT("new insertion does not extend the old three-second lifetime"), State.Feed.Num(), 1);
	TestTrue(TEXT("only the newer headshot row remains"),
		State.Feed.GetRows()[0].Event == EDustCombatFeedEvent::HeadshotKill);
	State.Advance(2.0f);
	TestEqual(TEXT("newer row expires on its own deadline"), State.Feed.Num(), 0);
	State.NotifyPlayerKilled();
	State.Advance(-1.0f);
	State.Advance(CombatFeedbackTestNaN);
	TestEqual(TEXT("invalid deltas never expire a row"), State.Feed.Num(), 1);
	State.Advance(3.0f);
	TestEqual(TEXT("death row uses the same lifetime"), State.Feed.Num(), 0);
	State.NotifyConfirmedHit(10.0f, false, true);
	State.Reset();
	State.NotifyConfirmedHit(10.0f, false, true);
	State.Advance(2.5f);
	TestEqual(TEXT("reset does not age a new row"), State.Feed.Num(), 1);
	State.Advance(100.0f);
	TestEqual(TEXT("a large frame delta expires rows safely"), State.Feed.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackFeedMotionTest,
	"Dust2.CombatFeedback.InsertedRowsMoveAndConverge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackFeedMotionTest::RunTest(const FString& Parameters)
{
	FDustCombatFeedbackState State;
	State.NotifyConfirmedHit(10.0f, false, true);
	State.NotifyConfirmedHit(10.0f, false, true);

	const TArray<FDustCombatFeedRow>& Rows = State.Feed.GetRows();
	TestEqual(TEXT("two rows exist"), Rows.Num(), 2);
	TestEqual(TEXT("newest row appears at its final slot"),
		Rows[0].AnimatedIndex, 0.0f, 0.0f);
	TestEqual(TEXT("previous newest starts at its old slot"),
		Rows[1].AnimatedIndex, 0.0f, 0.0f);

	State.Advance(0.075f);
	TestEqual(TEXT("half of the 0.15 s smoothstep is halfway down"),
		Rows[1].AnimatedIndex, 0.5f, 0.0001f);

	State.Advance(0.075f);
	TestEqual(TEXT("existing row converges exactly at the deadline"),
		Rows[1].AnimatedIndex, 1.0f, 0.0f);
	TestEqual(TEXT("newest row stays at index zero"),
		Rows[0].AnimatedIndex, 0.0f, 0.0f);
	TestFalse(TEXT("no motion remains after convergence"), State.Feed.IsAnimating());

	// A zero-length motion configuration must snap rather than leave rows between slots.
	FDustCombatFeedbackState Instant;
	FDustCombatFeedbackTuning InstantTuning;
	InstantTuning.FeedShiftSeconds = 0.0f;
	Instant.SetTuning(InstantTuning);
	Instant.NotifyConfirmedHit(10.0f, false, true);
	Instant.NotifyConfirmedHit(10.0f, true, true);
	TestEqual(TEXT("zero-duration shift snaps the old row immediately"),
		Instant.Feed.GetRows()[1].AnimatedIndex, 1.0f, 0.0f);
	TestFalse(TEXT("zero-duration shift is never animating"), Instant.Feed.IsAnimating());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackFeedPresentationTest,
	"Dust2.CombatFeedback.FeedUsesExactTextAndSelectionMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackFeedPresentationTest::RunTest(const FString& Parameters)
{
	const FLinearColor Cyan(0.10f, 0.85f, 1.0f, 1.0f);
	const FLinearColor Red(1.0f, 0.12f, 0.12f, 1.0f);
	const FLinearColor DeathRed(1.0f, 0.20f, 0.20f, 1.0f);

	const FDustCombatFeedPresentation Body = ResolveDustCombatFeedPresentation(
		EDustCombatFeedEvent::BodyKill, Cyan, Red, DeathRed);
	TestTrue(TEXT("body kill exact string"),
		Body.Text == FString(TEXT("你击杀了一个敌人")));
	TestTrue(TEXT("body kill uses a procedural skull"),
		Body.Icon == EDustCombatFeedIcon::Skull);
	TestTrue(TEXT("body kill uses the cyan accent"), Body.Accent.Equals(Cyan, 0.0f));

	const FDustCombatFeedPresentation Headshot = ResolveDustCombatFeedPresentation(
		EDustCombatFeedEvent::HeadshotKill, Cyan, Red, DeathRed);
	TestTrue(TEXT("headshot kill exact string"),
		Headshot.Text == FString(TEXT("你爆头击杀了一名敌人")));
	TestTrue(TEXT("headshot kill uses a skull with a target mark"),
		Headshot.Icon == EDustCombatFeedIcon::HeadshotSkull);
	TestTrue(TEXT("headshot kill uses the red accent"), Headshot.Accent.Equals(Red, 0.0f));

	const FDustCombatFeedPresentation Player = ResolveDustCombatFeedPresentation(
		EDustCombatFeedEvent::PlayerKilled, Cyan, Red, DeathRed);
	TestTrue(TEXT("player death exact string"),
		Player.Text == FString(TEXT("你被敌人击杀了")));
	TestTrue(TEXT("player death uses a red skull without the headshot target"),
		Player.Icon == EDustCombatFeedIcon::Skull);
	TestTrue(TEXT("player death uses its death accent"), Player.Accent.Equals(DeathRed, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackPlayerKilledTest,
	"Dust2.CombatFeedback.PlayerKilledRetainsFeedAndClearsCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackPlayerKilledTest::RunTest(const FString& Parameters)
{
	FDustCombatFeedbackState State;
	State.NotifyConfirmedHit(40.0f, true, true);
	TestTrue(TEXT("kill marker is active before death"), State.Center.IsMarkerActive());
	TestTrue(TEXT("kill skull is active before death"), State.Center.IsSkullActive());

	State.NotifyPlayerKilled();
	TestFalse(TEXT("player death clears the marker"), State.Center.IsMarkerActive());
	TestFalse(TEXT("player death clears the skull"), State.Center.IsSkullActive());
	TestEqual(TEXT("death appends without resetting history"), State.Feed.Num(), 2);
	TestTrue(TEXT("death row is newest"),
		State.Feed.GetRows()[0].Event == EDustCombatFeedEvent::PlayerKilled);
	TestTrue(TEXT("existing kill row shifts down but survives"),
		State.Feed.GetRows()[1].Event == EDustCombatFeedEvent::HeadshotKill);

	State.Reset();
	TestFalse(TEXT("reset clears the marker"), State.Center.IsMarkerActive());
	TestFalse(TEXT("reset clears the skull"), State.Center.IsSkullActive());
	TestEqual(TEXT("reset clears every feed row"), State.Feed.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatFeedbackTaperTest,
	"Dust2.CombatFeedback.TaperHasFourArmsAndThinInnerEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatFeedbackTaperTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("geometry owns four arms"), FDustCombatTaperGeometry::ArmCount, 4);

	FDustCombatTaperArm Arms[FDustCombatTaperGeometry::ArmCount];
	FDustCombatTaperGeometry::MakeArms(
		FVector2D(20.0f, 10.0f), 12.0f, 1.2f, 3.4f, Arms);

	const FVector2D ExpectedInner = FVector2D(20.0f, 10.0f) * FMath::Sqrt(0.5f);
	TestTrue(TEXT("45 degree arm inner point follows the elliptical X/Y gap"),
		Arms[0].Inner.Equals(ExpectedInner, 0.0001f));

	for (int32 Index = 0; Index < FDustCombatTaperGeometry::ArmCount; ++Index)
	{
		const FDustCombatTaperArm& Arm = Arms[Index];
		TestTrue(TEXT("arm direction points at its 45/135/225/315 angle"),
			Arm.GetDirection().Equals(ExpectedArmDirection(Index), 0.0001f));
		TestTrue(TEXT("gap translation does not change arm length"),
			FMath::IsNearlyEqual(Arm.GetLength(), 12.0f, 0.0001f));
		TestTrue(TEXT("inner end is thinner than the outer end"),
			Arm.InnerHalfThickness < Arm.OuterHalfThickness);

		FVector2D Quad[4];
		Arm.GetQuad(Quad);
		const float InnerWidth = FVector2D::Distance(Quad[0], Quad[3]);
		const float OuterWidth = FVector2D::Distance(Quad[1], Quad[2]);
		TestTrue(TEXT("quad inner edge matches the thin inner thickness"),
			FMath::IsNearlyEqual(InnerWidth, 1.2f, 0.0001f));
		TestTrue(TEXT("quad outer edge matches the thick outer thickness"),
			FMath::IsNearlyEqual(OuterWidth, 3.4f, 0.0001f));
	}

	// Moving the gap translates the same-length arm outward instead of stretching it.
	const FDustCombatTaperArm NearArm = FDustCombatTaperGeometry::MakeArm(
		0, FVector2D(5.0f, 5.0f), 12.0f, 1.2f, 3.4f);
	const FDustCombatTaperArm FarArm = FDustCombatTaperGeometry::MakeArm(
		0, FVector2D(50.0f, 50.0f), 12.0f, 1.2f, 3.4f);
	TestTrue(TEXT("near arm keeps its length"),
		FMath::IsNearlyEqual(NearArm.GetLength(), 12.0f, 0.0001f));
	TestTrue(TEXT("far arm keeps its length"),
		FMath::IsNearlyEqual(FarArm.GetLength(), 12.0f, 0.0001f));
	TestTrue(TEXT("far arm translates outward"),
		FarArm.Inner.SizeSquared() > NearArm.Inner.SizeSquared());

	// Extreme/non-finite inputs stay finite and preserve thin-inside ordering.
	const FDustCombatTaperArm Extreme = FDustCombatTaperGeometry::MakeArm(
		2,
		FVector2D(CombatFeedbackTestNaN, CombatFeedbackTestInfinity),
		CombatFeedbackTestInfinity,
		-1.0f,
		CombatFeedbackTestInfinity);
	TestTrue(TEXT("extreme gap/size stays finite"),
		FMath::IsFinite(Extreme.Inner.X) && FMath::IsFinite(Extreme.Inner.Y)
		&& FMath::IsFinite(Extreme.Outer.X) && FMath::IsFinite(Extreme.Outer.Y));
	TestTrue(TEXT("negative inner thickness is clamped, not inverted"),
		Extreme.InnerHalfThickness <= Extreme.OuterHalfThickness);

	const float MaxFloat = TNumericLimits<float>::Max();
	const FDustCombatTaperArm Huge = FDustCombatTaperGeometry::MakeArm(
		1, FVector2D(MaxFloat, MaxFloat), MaxFloat, MaxFloat, MaxFloat);
	TestTrue(TEXT("finite extreme gap/size is clamped to a finite bounded arm"),
		FMath::IsFinite(Huge.Inner.X) && FMath::IsFinite(Huge.Inner.Y)
		&& FMath::IsFinite(Huge.Outer.X) && FMath::IsFinite(Huge.Outer.Y)
		&& FMath::IsFinite(Huge.InnerHalfThickness)
		&& FMath::IsFinite(Huge.OuterHalfThickness));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
