#include "UI/DustWeaponLoadoutWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Weapons/WeaponRuntime.h"

void UDustWeaponLoadoutWidget::UpdateWeaponLoadout(
	AWeaponRuntime* Slot1Weapon,
	AWeaponRuntime* Slot2Weapon,
	AWeaponRuntime* CurrentWeapon,
	bool bImmediate)
{
	Slot1Weapon = IsValid(Slot1Weapon) ? Slot1Weapon : nullptr;
	Slot2Weapon = IsValid(Slot2Weapon) ? Slot2Weapon : nullptr;
	CurrentWeapon = IsValid(CurrentWeapon)
		&& (CurrentWeapon == Slot1Weapon || CurrentWeapon == Slot2Weapon) ? CurrentWeapon : nullptr;
	const bool bFirstUpdate = !bInitialized;
	const bool bSnap = bImmediate || bFirstUpdate;
	const FWeaponLoadoutDisplaySettings Settings = BuildDisplaySettings();

	AWeaponRuntime* const SlotWeapons[2] = { Slot1Weapon, Slot2Weapon };
	for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
	{
		AWeaponRuntime* const Weapon = SlotWeapons[SlotIndex];
		const uint64 BrushToken = ResolveBrushToken(Weapon);
		const bool bPresent = Weapon != nullptr;
		const bool bIsCurrent = bPresent && CurrentWeapon != nullptr && Weapon == CurrentWeapon;
		DisplayState.UpdateTrack(SlotIndex, BrushToken, bPresent, bIsCurrent, Settings, bSnap);
		ApplyTrackPresentation(SlotIndex);
	}

	const uint64 NewCurrentToken = ResolveBrushToken(CurrentWeapon);
	const bool bCurrentChanged = bFirstUpdate || NewCurrentToken != CurrentWeaponBrushToken;
	CurrentWeaponBrushToken = NewCurrentToken;
	if (bCurrentChanged || bSnap)
	{
		ApplyCurrentWeaponPresentation(CurrentWeapon, bSnap);
	}

	bInitialized = true;
	PruneDisplayCache();
}

void UDustWeaponLoadoutWidget::ResetWeaponLoadout()
{
	DisplayState.Reset();

	for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
	{
		bBrushApplied[SlotIndex] = false;
		AppliedBrushTokens[SlotIndex] = 0;
		AppliedTextures[SlotIndex] = nullptr;
		ApplyTrackPresentation(SlotIndex);
	}

	WeaponBrushTokens.Empty();
	BrushVisuals.Empty();
	NextBrushToken = 1;
	CurrentWeaponBrushToken = 0;
	bInitialized = false;

	bDetailsFading = false;
	DetailsFadeElapsedSeconds = 0.0f;
	DetailsFadeDurationSeconds = 0.0f;
	DetailsFadeStartOpacity = 0.0f;

	if (TXT_WeaponName)
	{
		TXT_WeaponName->SetText(FText::GetEmpty());
	}
	if (WeaponDetails)
	{
		// Nothing is currently represented; the next non-null update fades it in.
		WeaponDetails->SetRenderOpacity(0.0f);
	}
}

void UDustWeaponLoadoutWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// No active display work: skip all animation math.
	if (!DisplayState.IsAnimating() && !bDetailsFading)
	{
		return;
	}

	if (DisplayState.IsAnimating())
	{
		DisplayState.Advance(InDeltaTime);
		ApplyTrackPresentation(0);
		ApplyTrackPresentation(1);
	}

	AdvanceDetailsFade(InDeltaTime);
	PruneDisplayCache();
}

void UDustWeaponLoadoutWidget::NativeDestruct()
{
	ResetWeaponLoadout();
	Super::NativeDestruct();
}

void UDustWeaponLoadoutWidget::PruneDisplayCache()
{
	TSet<uint64> Retained;
	for (const FWeaponLoadoutTrackState& Track : DisplayState.Tracks)
	{
		Retained.Add(Track.GetVisibleBrushId());
		Retained.Add(Track.GetTargetBrushId());
	}
	Retained.Add(CurrentWeaponBrushToken);
	for (auto It = BrushVisuals.CreateIterator(); It; ++It)
	{
		if (!Retained.Contains(It.Key())) It.RemoveCurrent();
	}
	for (auto It = WeaponBrushTokens.CreateIterator(); It; ++It)
	{
		if (!Retained.Contains(It.Value())) It.RemoveCurrent();
	}
}

FWeaponLoadoutDisplaySettings UDustWeaponLoadoutWidget::BuildDisplaySettings() const
{
	FWeaponLoadoutDisplaySettings Settings;
	Settings.CurrentOffset = CurrentOffset;
	Settings.ReserveOffset = ReserveOffset;
	Settings.CurrentScale = CurrentScale;
	Settings.ReserveScale = ReserveScale;
	Settings.CurrentOpacity = CurrentOpacity;
	Settings.ReserveOpacity = ReserveOpacity;
	Settings.TransitionSeconds = TransitionSeconds;
	return Settings;
}

float UDustWeaponLoadoutWidget::GetEffectiveTransitionSeconds() const
{
	return FMath::IsFinite(TransitionSeconds)
		? FMath::Max(0.0f, TransitionSeconds)
		: 0.0f;
}

uint64 UDustWeaponLoadoutWidget::ResolveBrushToken(AWeaponRuntime* Weapon)
{
	if (!IsValid(Weapon))
	{
		return 0;
	}

	const FObjectKey Key(Weapon);
	uint64 Token = 0;
	if (const uint64* ExistingToken = WeaponBrushTokens.Find(Key))
	{
		Token = *ExistingToken;
	}
	else
	{
		Token = NextBrushToken++;
		WeaponBrushTokens.Add(Key, Token);
	}

	FWeaponLoadoutVisual Visual;
	const TSubclassOf<AWeaponRuntime> WeaponClass = Weapon->GetClass();
	if (const TObjectPtr<UTexture2D>* Icon = WeaponIcons.Find(WeaponClass))
	{
		Visual.Icon = Icon->Get();
	}

	// Refresh every notification so edits to WeaponIcons are picked up without a reset.
	BrushVisuals.FindOrAdd(Token) = MoveTemp(Visual);
	return Token;
}

UWidget* UDustWeaponLoadoutWidget::GetSlotRoot(int32 SlotIndex) const
{
	if (SlotIndex == 0)
	{
		return WeaponSlot1Root.Get();
	}
	if (SlotIndex == 1)
	{
		return WeaponSlot2Root.Get();
	}
	return nullptr;
}

UImage* UDustWeaponLoadoutWidget::GetSlotImage(int32 SlotIndex) const
{
	if (SlotIndex == 0)
	{
		return IMG_WeaponSlot1.Get();
	}
	if (SlotIndex == 1)
	{
		return IMG_WeaponSlot2.Get();
	}
	return nullptr;
}

UWidget* UDustWeaponLoadoutWidget::GetTrackTransformTarget(int32 SlotIndex) const
{
	if (UWidget* Root = GetSlotRoot(SlotIndex))
	{
		return Root;
	}
	// Optional bindings: a root-less WBP still gets opacity/scale on its Image.
	return GetSlotImage(SlotIndex);
}

void UDustWeaponLoadoutWidget::ApplyTrackPresentation(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= FWeaponLoadoutDisplayState::TrackCount)
	{
		return;
	}

	const FWeaponLoadoutTrackState& Track = DisplayState.Tracks[SlotIndex];
	const FWeaponLoadoutPose& Pose = Track.GetPose();

	if (UWidget* TransformTarget = GetTrackTransformTarget(SlotIndex))
	{
		TransformTarget->SetRenderTranslation(Pose.Offset);
		TransformTarget->SetRenderScale(FVector2D(Pose.Scale, Pose.Scale));
		TransformTarget->SetRenderOpacity(FMath::Clamp(Pose.Opacity, 0.0f, 1.0f));
	}

	ApplyTrackBrush(SlotIndex, Track.GetVisibleBrushId());
}

void UDustWeaponLoadoutWidget::ApplyTrackBrush(int32 SlotIndex, uint64 BrushId)
{
	UImage* Image = GetSlotImage(SlotIndex);
	if (!Image)
	{
		return;
	}

	const FWeaponLoadoutVisual* Visual = BrushId != 0 ? BrushVisuals.Find(BrushId) : nullptr;
	UTexture2D* Texture = Visual ? Visual->Icon.Get() : nullptr;

	if (bBrushApplied[SlotIndex]
		&& AppliedBrushTokens[SlotIndex] == BrushId
		&& AppliedTextures[SlotIndex].Get() == Texture)
	{
		return;
	}

	bBrushApplied[SlotIndex] = true;
	AppliedBrushTokens[SlotIndex] = BrushId;
	AppliedTextures[SlotIndex] = Texture;

	if (!Texture)
	{
		// Missing icon: keep the state animating but never draw a placeholder gun.
		Image->SetBrushFromTexture(nullptr);
		Image->SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	Image->SetBrushFromTexture(Texture, true);

	// bMatchSize only runs when the resource changes, so also fix a brush that
	// already used this texture with a stretched ImageSize.
	FSlateBrush Brush = Image->GetBrush();
	const FVector2D TextureSize(
		static_cast<float>(Texture->GetSizeX()),
		static_cast<float>(Texture->GetSizeY()));
	if (!Brush.ImageSize.Equals(TextureSize))
	{
		Brush.ImageSize = TextureSize;
		Image->SetBrush(Brush);
	}

	Image->SetVisibility(ESlateVisibility::Visible);
}

void UDustWeaponLoadoutWidget::ApplyCurrentWeaponPresentation(
	AWeaponRuntime* CurrentWeapon,
	bool bImmediate)
{
	if (!IsValid(CurrentWeapon))
	{
		if (TXT_WeaponName)
		{
			TXT_WeaponName->SetText(FText::GetEmpty());
		}
		bDetailsFading = false;
		DetailsFadeElapsedSeconds = 0.0f;
		DetailsFadeDurationSeconds = 0.0f;
		if (WeaponDetails)
		{
			WeaponDetails->SetRenderOpacity(0.0f);
		}
		return;
	}

	const FText WeaponName =
		AWeaponRuntime::GetDisplayNameForWeaponClass(CurrentWeapon->GetClass());
	if (TXT_WeaponName)
	{
		TXT_WeaponName->SetText(WeaponName);
	}

	if (!WeaponDetails)
	{
		bDetailsFading = false;
		DetailsFadeElapsedSeconds = 0.0f;
		DetailsFadeDurationSeconds = 0.0f;
		return;
	}

	const float Duration = GetEffectiveTransitionSeconds();
	if (bImmediate || Duration <= 0.0f)
	{
		bDetailsFading = false;
		DetailsFadeElapsedSeconds = 0.0f;
		DetailsFadeDurationSeconds = 0.0f;
		WeaponDetails->SetRenderOpacity(1.0f);
		return;
	}

	// Preserve an interrupted fade; a fresh change starts invisible with the new data.
	DetailsFadeStartOpacity = bDetailsFading ? WeaponDetails->GetRenderOpacity() : 0.0f;
	bDetailsFading = true;
	DetailsFadeElapsedSeconds = 0.0f;
	DetailsFadeDurationSeconds = Duration;
	WeaponDetails->SetRenderOpacity(DetailsFadeStartOpacity);
}

void UDustWeaponLoadoutWidget::AdvanceDetailsFade(float DeltaSeconds)
{
	if (!bDetailsFading)
	{
		return;
	}

	const float SafeDelta = FMath::IsFinite(DeltaSeconds)
		? FMath::Max(0.0f, DeltaSeconds)
		: 0.0f;
	DetailsFadeElapsedSeconds = FMath::Min(
		DetailsFadeDurationSeconds,
		DetailsFadeElapsedSeconds + SafeDelta);

	const float LinearAlpha = DetailsFadeDurationSeconds > 0.0f
		? DetailsFadeElapsedSeconds / DetailsFadeDurationSeconds
		: 1.0f;
	const float ClampedAlpha = FMath::Clamp(LinearAlpha, 0.0f, 1.0f);
	const float SmoothAlpha = ClampedAlpha * ClampedAlpha * (3.0f - 2.0f * ClampedAlpha);

	if (WeaponDetails)
	{
		WeaponDetails->SetRenderOpacity(FMath::Lerp(DetailsFadeStartOpacity, 1.0f, SmoothAlpha));
	}

	if (DetailsFadeElapsedSeconds >= DetailsFadeDurationSeconds)
	{
		bDetailsFading = false;
		if (WeaponDetails)
		{
			WeaponDetails->SetRenderOpacity(1.0f);
		}
	}
}
