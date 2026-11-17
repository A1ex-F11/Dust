#include "UI/DustCrosshairSettingsSession.h"

#include "UI/DynamicCrosshairWidget.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const FString PreferencesSlot(TEXT("Dust2_CrosshairPreferences"));
	constexpr int32 PreferencesUser = 0;
}

FCrosshairStyleSettings UDustCrosshairSettingsSession::ReadDefaults(const UDynamicCrosshairWidget* Widget)
{
	if (Widget->StyleAsset) return Widget->StyleAsset->Settings.GetSanitized();
	FCrosshairStyleSettings Style;
	Style.MinimumGap = Widget->MinimumGap;
	Style.LineLength = Widget->LineLength;
	Style.LineThickness = Widget->LineThickness;
	Style.CrosshairColor = Widget->CrosshairColor;
	return Style.GetSanitized();
}

bool UDustCrosshairSettingsSession::BeginEditing(
	UDynamicCrosshairWidget* LiveCrosshair, UDynamicCrosshairWidget* PreviewCrosshair)
{
	if (!IsValid(LiveCrosshair) || !IsValid(PreviewCrosshair) || LiveCrosshair == PreviewCrosshair)
		return false;
	CancelEditing();
	Live = LiveCrosshair;
	Preview = PreviewCrosshair;
	bOriginalOverride = LiveCrosshair->bUseStyleOverride;
	OriginalOverride = LiveCrosshair->StyleOverride;
	DefaultStyle = ReadDefaults(LiveCrosshair);
	DraftStyle = LiveCrosshair->GetResolvedStyleSettings();
	bDraftOverride = bOriginalOverride;
	PreviewExpansion = 0.0f;
	bEditing = true;
	PushDraft();
	return true;
}

bool UDustCrosshairSettingsSession::SetDraftStyle(FCrosshairStyleSettings Style)
{
	if (!bEditing || !Live.IsValid()) return false;
	DraftStyle = Style.GetSanitized();
	bDraftOverride = true;
	PushDraft();
	return true;
}

void UDustCrosshairSettingsSession::RestoreDefaults()
{
	if (!bEditing || !Live.IsValid()) return;
	DraftStyle = DefaultStyle;
	bDraftOverride = false;
	PushDraft();
}

void UDustCrosshairSettingsSession::SetPreviewExpansion(float ExtraGap)
{
	PreviewExpansion = FMath::IsFinite(ExtraGap) ? FMath::Clamp(ExtraGap, 0.0f, 256.0f) : 0.0f;
	PushDraft();
}

void UDustCrosshairSettingsSession::PushDraft()
{
	if (!bEditing) return;
	if (UDynamicCrosshairWidget* Widget = Live.Get())
	{
		if (bDraftOverride) Widget->ApplyStyleOverride(DraftStyle);
		else Widget->ClearStyleOverride();
	}
	if (UDynamicCrosshairWidget* Widget = Preview.Get())
	{
		Widget->ApplyStyleOverride(DraftStyle);
		const FVector2D Projected = DraftStyle.MinimumGap + FVector2D(PreviewExpansion, PreviewExpansion);
		Widget->SetCrosshairGap(Widget->ResolveStyleTargetGap(Projected));
	}
}

bool UDustCrosshairSettingsSession::ApplyAndSave()
{
	if (!bEditing || !Live.IsValid()) return false;
	if (UGameplayStatics::DoesSaveGameExist(PreferencesSlot, PreferencesUser))
	{
		const UDustCrosshairPreferences* Previous = Cast<UDustCrosshairPreferences>(
			UGameplayStatics::LoadGameFromSlot(PreferencesSlot, PreferencesUser));
		// Do not silently overwrite corrupt, unrelated, or newer-format data.
		if (!Previous || Previous->Version != 1) return false;
	}
	UDustCrosshairPreferences* Saved = Cast<UDustCrosshairPreferences>(
		UGameplayStatics::CreateSaveGameObject(UDustCrosshairPreferences::StaticClass()));
	if (!Saved) return false;
	Saved->bUseOverride = bDraftOverride;
	Saved->Style = DraftStyle.GetSanitized();
	if (!UGameplayStatics::SaveGameToSlot(Saved, PreferencesSlot, PreferencesUser)) return false;
	bEditing = false;
	ReleaseWidgets();
	return true;
}

void UDustCrosshairSettingsSession::CancelEditing()
{
	if (bEditing)
	{
		if (UDynamicCrosshairWidget* Widget = Live.Get())
		{
			if (bOriginalOverride) Widget->ApplyStyleOverride(OriginalOverride);
			else Widget->ClearStyleOverride();
		}
	}
	bEditing = false;
	ReleaseWidgets();
}

void UDustCrosshairSettingsSession::ReleaseWidgets()
{
	Live.Reset();
	Preview.Reset();
}

bool UDustCrosshairSettingsSession::LoadSavedStyle(UDynamicCrosshairWidget* LiveCrosshair)
{
	if (!IsValid(LiveCrosshair)
		|| !UGameplayStatics::DoesSaveGameExist(PreferencesSlot, PreferencesUser)) return false;
	const UDustCrosshairPreferences* Saved = Cast<UDustCrosshairPreferences>(
		UGameplayStatics::LoadGameFromSlot(PreferencesSlot, PreferencesUser));
	if (!Saved || Saved->Version != 1) return false;
	if (Saved->bUseOverride) LiveCrosshair->ApplyStyleOverride(Saved->Style.GetSanitized());
	else LiveCrosshair->ClearStyleOverride();
	return true;
}
