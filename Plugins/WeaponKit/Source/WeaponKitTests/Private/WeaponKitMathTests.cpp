#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Damage/WeaponKitDamageLibrary.h"
#include "Math/FiringSpreadMath.h"
#include "Math/RecoilKickEnvelope.h"
#include "Math/ShotRecoilMath.h"
#include "Math/ShotTracerGeometry.h"
#include "Tracer/WeaponKitShotTracerComponent.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitSpreadMathTest,
	"WeaponKit.Spread.MathBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitSpreadMathTest::RunTest(const FString& Parameter)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Negative spread inputs clamp to zero"), FMath::IsNearlyZero(FWeaponKitFiringSpreadMath::AddShot(-0.4f, -0.2f, 5.0f)));
	TestTrue(TEXT("NaN accumulated spread is safe"), FMath::IsNearlyZero(FWeaponKitFiringSpreadMath::AddShot(NaN, 0.4f, 5.0f)));
	TestTrue(TEXT("NaN per-shot spread is safe"), FMath::IsNearlyZero(FWeaponKitFiringSpreadMath::AddShot(0.4f, NaN, 5.0f)));
	TestTrue(TEXT("NaN maximum is safe"), FMath::IsNearlyZero(FWeaponKitFiringSpreadMath::AddShot(0.4f, 0.4f, NaN)));
	TestTrue(TEXT("Spread addition clamps at its maximum"), FMath::IsNearlyEqual(FWeaponKitFiringSpreadMath::AddShot(0.7f, 0.5f, 0.8f), 0.8f));
	TestTrue(TEXT("Recovery does not begin at or before its delay"),
		FMath::IsNearlyEqual(FWeaponKitFiringSpreadMath::Recover(0.8f, 0.5f, 0.5f, 2.0f, 1.0f), 0.8f) &&
		FMath::IsNearlyEqual(FWeaponKitFiringSpreadMath::Recover(0.8f, 0.4f, 0.5f, 2.0f, 1.0f), 0.8f));
	TestTrue(TEXT("Recovery after the delay uses only elapsed recovery time"),
		FMath::IsNearlyEqual(FWeaponKitFiringSpreadMath::Recover(0.8f, 0.75f, 0.5f, 2.0f, 1.0f), 0.3f, 0.0001f));
	TestTrue(TEXT("Recovery result never goes below zero"), FMath::IsNearlyZero(FWeaponKitFiringSpreadMath::Recover(0.8f, 10.0f, 0.5f, 2.0f, 1.0f)));
	TestTrue(TEXT("NaN and negative recovery inputs are safe"), FMath::IsNearlyZero(FWeaponKitFiringSpreadMath::Recover(NaN, NaN, -1.0f, NaN, 1.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitShotRecoilMathTest,
	"WeaponKit.Recoil.ShotMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitShotRecoilMathTest::RunTest(const FString& Parameter)
{
	const FVector2D Offset(4.0f, -2.0f);
	TestTrue(TEXT("Recovery waits for its delay"), FWeaponKitShotRecoilMath::Recover(Offset, 0.1f, 0.2f, 8.0f).Equals(Offset));
	const FVector2D Recovered = FWeaponKitShotRecoilMath::Recover(Offset, 0.7f, 0.2f, 8.0f);
	TestTrue(TEXT("Pitch recovers at the configured speed"), Recovered.X <= 0.001f);
	TestTrue(TEXT("Yaw recovers only as far as its own offset"), FMath::IsNearlyZero(Recovered.Y));
	TestTrue(TEXT("Yaw offset turns the shot"), FWeaponKitShotRecoilMath::Apply(FVector::ForwardVector, FVector2D(0.0f, 90.0f)).Equals(FVector::RightVector, 0.001f));
	TestTrue(TEXT("Pitch is clamped"), FWeaponKitShotRecoilMath::Apply(FVector::ForwardVector, FVector2D(90.0f, 0.0f)).Rotation().Pitch <= 89.01f);
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Non-finite offset is rejected"), FWeaponKitShotRecoilMath::Apply(FVector::ForwardVector, FVector2D(NaN, 0.0f)).IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitKickEnvelopeTest,
	"WeaponKit.Recoil.KickEnvelopeIntegral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitKickEnvelopeTest::RunTest(const FString& Parameter)
{
	FWeaponKitRecoilKickEnvelope Stepped;
	Stepped.Total = FVector2D(3.0f, -1.0f);
	Stepped.Duration = 1.0f;
	FVector2D Sum = FVector2D::ZeroVector;
	for (int32 Step = 0; Step < 5; ++Step)
	{
		Sum += Stepped.Advance(0.1f);
	}

	FWeaponKitRecoilKickEnvelope Once;
	Once.Total = FVector2D(3.0f, -1.0f);
	Once.Duration = 1.0f;
	const FVector2D Single = Once.Advance(0.5f);
	TestTrue(TEXT("Subdivision matches one step at the same time"), Sum.Equals(Single, 0.001f));

	FVector2D Rest = FVector2D::ZeroVector;
	for (int32 Step = 0; Step < 5; ++Step)
	{
		Rest += Stepped.Advance(0.1f);
	}
	TestTrue(TEXT("Full duration applies the whole kick"), (Sum + Rest).Equals(Stepped.Total, 0.001f));
	TestTrue(TEXT("Envelope completes"), Stepped.IsComplete());
	TestTrue(TEXT("Nothing remains"), Stepped.GetRemaining().IsNearlyZero(0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitTracerGeometryTest,
	"WeaponKit.Tracer.FlightAndSegmentBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitTracerGeometryTest::RunTest(const FString& Parameter)
{
	float Distance = 0.0f;
	float Duration = 0.0f;
	float Length = 0.0f;
	TestTrue(TEXT("Normal flight"), FWeaponKitShotTracerGeometry::BuildFlight(FVector::ZeroVector, FVector(3000, 0, 0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestTrue(TEXT("Flight duration"), FMath::IsNearlyEqual(Duration, 0.1f));
	TestTrue(TEXT("Long shot"), FWeaponKitShotTracerGeometry::BuildFlight(FVector::ZeroVector, FVector(100000, 0, 0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestTrue(TEXT("Cosmetic duration capped"), FMath::IsNearlyEqual(Duration, 0.15f));
	for (const float Age : {0.0f, 0.00001f, 0.05f, 0.15f, 1.0f})
	{
		const FVector2D Segment = FWeaponKitShotTracerGeometry::Segment(Distance, Duration, Length, Age);
		TestTrue(TEXT("Tail never behind muzzle"), Segment.X - Segment.Y * 0.5f >= -0.01f);
		TestTrue(TEXT("Head never past impact"), Segment.X + Segment.Y * 0.5f <= Distance + 0.01f);
		TestTrue(TEXT("Visible segment length bounded"), Segment.Y >= 0.0f && Segment.Y <= Length + 0.01f);
	}
	TestTrue(TEXT("Close hit"), FWeaponKitShotTracerGeometry::BuildFlight(FVector::ZeroVector, FVector(2, 0, 0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestEqual(TEXT("Length clipped for close hit"), Length, 2.0f);
	TestTrue(TEXT("One-frame minimum cosmetic duration"), Duration >= 1.0f / 60.0f);
	TestFalse(TEXT("Zero distance rejected"), FWeaponKitShotTracerGeometry::BuildFlight(FVector::ZeroVector, FVector::ZeroVector, 30000, 0.15f, 80, Distance, Duration, Length));
	TestFalse(TEXT("Invalid speed rejected"), FWeaponKitShotTracerGeometry::BuildFlight(FVector::ZeroVector, FVector(10, 0, 0), -1, 0.15f, 80, Distance, Duration, Length));
	TestFalse(TEXT("Nonfinite endpoint rejected"), FWeaponKitShotTracerGeometry::BuildFlight(
		FVector::ZeroVector, FVector(std::numeric_limits<double>::infinity(), 0, 0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestEqual(TEXT("Failed flight output cleared"), Distance, 0.0f);

	UWeaponKitShotTracerComponent* Component = NewObject<UWeaponKitShotTracerComponent>();
	TestFalse(TEXT("Unconfigured component fails safely"), Component->PlayShotTracer(FVector::ZeroVector, FVector(100, 0, 0)));
	TestTrue(TEXT("Default distance parameter"), Component->DistanceParameter == FName(TEXT("User.TracerDistance")));
	Component->DistanceParameter = TEXT("User.CustomDistance");
	TestTrue(TEXT("Distance parameter can be replaced"), Component->DistanceParameter == FName(TEXT("User.CustomDistance")));
	Component->ClearShotTracers();
	Component->ClearShotTracers();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitDamageRegionTest,
	"WeaponKit.Damage.RegionMultipliers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitDamageRegionTest::RunTest(const FString& Parameter)
{
	const TArray<FWeaponKitDamageRegion> HeadBody = UWeaponKitDamageLibrary::MakeHeadBodyRegions(FName(TEXT("head")), 4.0f);
	const float Tolerance = 0.001f;
	TestTrue(TEXT("Body without armor returns 36"), FMath::IsNearlyEqual(
		UWeaponKitDamageLibrary::CalculateFinalDamage(36.0f, FName(TEXT("spine_04")), 0.775f, HeadBody, false, false), 36.0f, Tolerance));
	TestTrue(TEXT("Body with armor returns 27.9"), FMath::IsNearlyEqual(
		UWeaponKitDamageLibrary::CalculateFinalDamage(36.0f, FName(TEXT("spine_04")), 0.775f, HeadBody, false, true), 27.9f, Tolerance));
	TestTrue(TEXT("Configured head bone without helmet returns 144"), FMath::IsNearlyEqual(
		UWeaponKitDamageLibrary::CalculateFinalDamage(36.0f, FName(TEXT("head")), 0.775f, HeadBody, false, false), 144.0f, Tolerance));
	TestTrue(TEXT("Configured head bone with helmet returns 111.6"), FMath::IsNearlyEqual(
		UWeaponKitDamageLibrary::CalculateFinalDamage(36.0f, FName(TEXT("head")), 0.775f, HeadBody, true, false), 111.6f, Tolerance));
	TestTrue(TEXT("Zero damage returns 0"), FMath::IsNearlyZero(
		UWeaponKitDamageLibrary::CalculateFinalDamage(0.0f, FName(TEXT("head")), 0.775f, HeadBody, true, false)));
	TestTrue(TEXT("Negative damage returns 0"), FMath::IsNearlyZero(
		UWeaponKitDamageLibrary::CalculateFinalDamage(-10.0f, FName(TEXT("head")), 0.775f, HeadBody, true, false)));

	FWeaponKitDamageRegion Skull;
	Skull.RegionId = TEXT("Skull");
	Skull.BoneNames = { FName(TEXT("skull")) };
	Skull.DamageMultiplier = 2.0f;
	const TArray<FWeaponKitDamageRegion> Custom = { Skull };
	TestTrue(TEXT("Listed bone uses its own multiplier and ignores armor flags"), FMath::IsNearlyEqual(
		UWeaponKitDamageLibrary::CalculateFinalDamage(36.0f, FName(TEXT("skull")), 0.5f, Custom, true, true), 72.0f, Tolerance));
	TestTrue(TEXT("The name head is not special unless a region lists it"), FMath::IsNearlyEqual(
		UWeaponKitDamageLibrary::CalculateFinalDamage(36.0f, FName(TEXT("head")), 0.5f, Custom, false, true), 18.0f, Tolerance));
	return true;
}

#endif
