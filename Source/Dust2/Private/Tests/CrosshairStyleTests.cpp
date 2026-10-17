#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CrosshairStyleData.h"
#include "DynamicCrosshairWidget.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairStyleOverrideIsolationTest,
	"Dust2.Crosshair.Style.RuntimeOverrideDoesNotMutateSharedAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairStyleOverrideIsolationTest::RunTest(const FString& Parameters)
{
	UCrosshairStyleData* Asset = NewObject<UCrosshairStyleData>();
	UDynamicCrosshairWidget* Widget = NewObject<UDynamicCrosshairWidget>();
	if (!TestNotNull(TEXT("Style asset"), Asset) || !TestNotNull(TEXT("Crosshair widget"), Widget))
	{
		return false;
	}

	Asset->Settings.MinimumGap = FVector2D(4.0f, 5.0f);
	Asset->Settings.LineLength = 9.0f;
	Widget->SetStyleAsset(Asset);

	FCrosshairStyleSettings RuntimeOverride;
	RuntimeOverride.MinimumGap = FVector2D(2.0f, 6.0f);
	RuntimeOverride.LineLength = 12.0f;
	Widget->ApplyStyleOverride(RuntimeOverride);
	const FCrosshairStyleSettings ActiveOverride = Widget->GetResolvedStyleSettings();
	TestTrue(TEXT("Instance override wins over the asset"),
		ActiveOverride.MinimumGap.Equals(FVector2D(2.0f, 6.0f), 0.0f));
	TestEqual(TEXT("Override line length is active"), ActiveOverride.LineLength, 12.0f);
	TestTrue(TEXT("Runtime override leaves shared asset unchanged"),
		Asset->Settings.MinimumGap.Equals(FVector2D(4.0f, 5.0f), 0.0f));
	TestEqual(TEXT("Shared asset line length is unchanged"), Asset->Settings.LineLength, 9.0f);

	Widget->ClearStyleOverride();
	TestTrue(TEXT("Clearing override restores asset settings"),
		Widget->GetResolvedStyleSettings().MinimumGap.Equals(FVector2D(4.0f, 5.0f), 0.0f));
	Widget->SetStyleAsset(nullptr);
	Widget->MinimumGap = FVector2D(6.0f, 7.0f);
	Widget->LineLength = 10.0f;
	TestTrue(TEXT("Without override or asset, legacy fields remain the fallback"),
		Widget->GetResolvedStyleSettings().MinimumGap.Equals(FVector2D(6.0f, 7.0f), 0.0f));
	TestEqual(TEXT("Legacy line length remains compatible"),
		Widget->GetResolvedStyleSettings().LineLength, 10.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairStyleStaticGapTest,
	"Dust2.Crosshair.Style.StaticStyleIgnoresDynamicGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairStyleStaticGapTest::RunTest(const FString& Parameters)
{
	UDynamicCrosshairWidget* Widget = NewObject<UDynamicCrosshairWidget>();
	if (!TestNotNull(TEXT("Crosshair widget"), Widget)) return false;

	FCrosshairStyleSettings Style;
	Style.MinimumGap = FVector2D(3.0f, 4.0f);
	Style.bDynamicSpread = false;
	Widget->ApplyStyleOverride(Style);
	Widget->SetCrosshairGap(FVector2D(80.0f, 90.0f));

	const FVector2D Minimum = Widget->GetCrosshairMinimumGap();
	TestTrue(TEXT("Static style resolves any projected target to its minimum"),
		Widget->ResolveStyleTargetGap(FVector2D(80.0f, 90.0f)).Equals(Minimum, 0.0f));
	TestTrue(TEXT("Static style getter remains at minimum after a dynamic input"),
		Widget->GetCrosshairGap().Equals(Minimum, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairStyleSanitizationTest,
	"Dust2.Crosshair.Style.InvalidSettingsAreSanitized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairStyleSanitizationTest::RunTest(const FString& Parameters)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	FCrosshairStyleSettings Style;
	Style.MinimumGap = FVector2D(-2.0f, NaN);
	Style.LineLength = -3.0f;
	Style.LineThickness = Infinity;
	Style.CrosshairColor = FLinearColor(-1.0f, 0.4f, Infinity, 2.0f);
	Style.SpreadDisplayScale = Infinity;

	const FCrosshairStyleSettings Safe = Style.GetSanitized();
	TestTrue(TEXT("Gap components are finite and non-negative"),
		Safe.MinimumGap.Equals(FVector2D(0.0f, 3.0f), 0.0f));
	TestEqual(TEXT("Negative line length clamps to zero"), Safe.LineLength, 0.0f);
	TestEqual(TEXT("Invalid thickness uses the default"), Safe.LineThickness, 1.75f);
	TestTrue(TEXT("Color channels clamp to the unit range"),
		Safe.CrosshairColor.Equals(FLinearColor(0.0f, 0.4f, 1.0f, 1.0f), 0.0001f));
	TestEqual(TEXT("Invalid scale uses its default"), Safe.SpreadDisplayScale, 1.0f);

	Style.SpreadDisplayScale = 20.0f;
	TestEqual(TEXT("Display scale clamps to its supported maximum"),
		Style.GetSanitized().SpreadDisplayScale, 10.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCrosshairStyleTargetScalingTest,
	"Dust2.Crosshair.Style.TargetScalingIsAppliedExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairStyleTargetScalingTest::RunTest(const FString& Parameters)
{
	UDynamicCrosshairWidget* Widget = NewObject<UDynamicCrosshairWidget>();
	if (!TestNotNull(TEXT("Crosshair widget"), Widget)) return false;

	FCrosshairStyleSettings Style;
	Style.MinimumGap = FVector2D(3.0f, 4.0f);
	Style.SpreadDisplayScale = 2.0f;
	Widget->ApplyStyleOverride(Style);

	const FVector2D Resolved = Widget->ResolveStyleTargetGap(FVector2D(7.0f, 13.0f));
	TestTrue(TEXT("Target is scaled above the minimum"),
		Resolved.Equals(FVector2D(11.0f, 22.0f), 0.0f));
	Widget->SetCrosshairGap(Resolved);
	TestTrue(TEXT("Set and get preserve the already-scaled target"),
		Widget->GetCrosshairGap().Equals(Resolved, 0.0f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
