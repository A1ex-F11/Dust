#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/DustCrosshairSettingsSession.h"
#include "UI/DynamicCrosshairWidget.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCrosshairDraftCancelTest,
	"Dust2.UI.CrosshairSettings.CancelRestoresOriginalOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustCrosshairDraftCancelTest::RunTest(const FString& Parameters)
{
	UDynamicCrosshairWidget* Live = NewObject<UDynamicCrosshairWidget>();
	UDynamicCrosshairWidget* Preview = NewObject<UDynamicCrosshairWidget>();
	UDustCrosshairSettingsSession* Session = NewObject<UDustCrosshairSettingsSession>();
	FCrosshairStyleSettings Original;
	Original.LineLength = 12.0f;
	Live->ApplyStyleOverride(Original);
	TestTrue(TEXT("valid editing widgets"), Session->BeginEditing(Live, Preview));
	FCrosshairStyleSettings Draft = Original;
	Draft.LineLength = 24.0f;
	TestTrue(TEXT("preview updates"), Session->SetDraftStyle(Draft));
	TestEqual(TEXT("live widget previews the draft"), Live->GetResolvedStyleSettings().LineLength, 24.0f);
	TestEqual(TEXT("preview widget matches"), Preview->GetResolvedStyleSettings().LineLength, 24.0f);
	Session->CancelEditing();
	TestTrue(TEXT("original override state restored"), Live->bUseStyleOverride);
	TestEqual(TEXT("original style restored"), Live->GetResolvedStyleSettings().LineLength, 12.0f);
	TestFalse(TEXT("cancel closes session"), Session->IsEditing());
	TestFalse(TEXT("closed session refuses edits"), Session->SetDraftStyle(Draft));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCrosshairDefaultsPreviewTest,
	"Dust2.UI.CrosshairSettings.DefaultAssetIsNeverModified",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustCrosshairDefaultsPreviewTest::RunTest(const FString& Parameters)
{
	UDynamicCrosshairWidget* Live = NewObject<UDynamicCrosshairWidget>();
	UDynamicCrosshairWidget* Preview = NewObject<UDynamicCrosshairWidget>();
	UCrosshairStyleData* Asset = NewObject<UCrosshairStyleData>();
	Asset->Settings.LineLength = 9.0f;
	Live->SetStyleAsset(Asset);
	UDustCrosshairSettingsSession* Session = NewObject<UDustCrosshairSettingsSession>();
	TestFalse(TEXT("preview cannot be gameplay widget"), Session->BeginEditing(Live, Live));
	TestTrue(TEXT("session begins"), Session->BeginEditing(Live, Preview));
	FCrosshairStyleSettings Draft = Session->GetDraftStyle();
	Draft.LineLength = 30.0f;
	Draft.SpreadDisplayScale = 2.0f;
	Session->SetDraftStyle(Draft);
	Session->SetPreviewExpansion(10.0f);
	TestTrue(TEXT("preview expansion follows display strength"),
		Preview->GetCrosshairGap().Equals(Draft.MinimumGap + FVector2D(20.0, 20.0)));
	TestEqual(TEXT("shared asset untouched"), Asset->Settings.LineLength, 9.0f);
	Session->RestoreDefaults();
	TestFalse(TEXT("default follows asset without override"), Live->bUseStyleOverride);
	TestEqual(TEXT("default draft uses preset"), Session->GetDraftStyle().LineLength, 9.0f);
	Session->CancelEditing();
	TestFalse(TEXT("cancel restores absence of override"), Live->bUseStyleOverride);
	TestEqual(TEXT("preset stays unchanged"), Asset->Settings.LineLength, 9.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCrosshairPreferencesRoundTripTest,
	"Dust2.UI.CrosshairSettings.PreferencesMemoryRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustCrosshairPreferencesRoundTripTest::RunTest(const FString& Parameters)
{
	UDustCrosshairPreferences* Original = NewObject<UDustCrosshairPreferences>();
	Original->bUseOverride = true;
	Original->Style.LineLength = 16.0f;
	Original->Style.MinimumGap = FVector2D(4.0, 6.0);
	Original->Style.CrosshairColor = FLinearColor(0.2f, 0.8f, 0.4f, 0.6f);
	Original->Style.bDynamicSpread = false;
	Original->Style.SpreadDisplayScale = 2.0f;
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("serialize preferences in memory"), UGameplayStatics::SaveGameToMemory(Original, Bytes)))
		return false;
	const UDustCrosshairPreferences* Loaded = Cast<UDustCrosshairPreferences>(
		UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("load preferences from memory"), Loaded)) return false;
	TestEqual(TEXT("format version survives"), Loaded->Version, 1);
	TestTrue(TEXT("override flag survives"), Loaded->bUseOverride);
	TestEqual(TEXT("length survives"), Loaded->Style.LineLength, 16.0f);
	TestTrue(TEXT("gap survives"), Loaded->Style.MinimumGap.Equals(Original->Style.MinimumGap));
	TestTrue(TEXT("color and alpha survive"), Loaded->Style.CrosshairColor.Equals(Original->Style.CrosshairColor));
	TestFalse(TEXT("dynamic toggle survives"), Loaded->Style.bDynamicSpread);
	TestEqual(TEXT("display strength survives"), Loaded->Style.SpreadDisplayScale, 2.0f);
	return true;
}
#endif
