#include "UI/DustCombatFeedbackWidget.h"

#include "UI/DynamicCrosshairWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Layout/Geometry.h"
#include "Layout/Clipping.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Textures/SlateShaderResource.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"

namespace
{
	constexpr int32 SkullHeadSides = 16;
	constexpr int32 SkullEyeSides = 10;

	FLinearColor CombatPaintSanitizeColor(const FLinearColor& Color, const FLinearColor& Fallback)
	{
		const auto SafeChannel = [](float Value, float DefaultValue)
		{
			return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : DefaultValue;
		};

		const FLinearColor SafeFallback(
			SafeChannel(Fallback.R, 1.0f),
			SafeChannel(Fallback.G, 1.0f),
			SafeChannel(Fallback.B, 1.0f),
			SafeChannel(Fallback.A, 1.0f));

		return FLinearColor(
			SafeChannel(Color.R, SafeFallback.R),
			SafeChannel(Color.G, SafeFallback.G),
			SafeChannel(Color.B, SafeFallback.B),
			SafeChannel(Color.A, SafeFallback.A));
	}

	FVector2D CombatPaintSanitizeGap(const FVector2D& Gap, const FVector2D& Fallback)
	{
		const auto SafeComponent = [](float Value, float DefaultValue)
		{
			if (!FMath::IsFinite(Value))
			{
				return FMath::IsFinite(DefaultValue) ? FMath::Max(0.0f, DefaultValue) : 0.0f;
			}
			return FMath::Max(0.0f, Value);
		};

		return FVector2D(
			SafeComponent(Gap.X, Fallback.X),
			SafeComponent(Gap.Y, Fallback.Y));
	}

	int32 CombatPaintNextPaintLayer(int32 Layer)
	{
		return Layer < MAX_int32 ? Layer + 1 : Layer;
	}

	FColor CombatPaintPackColor(const FLinearColor& Color)
	{
		return CombatPaintSanitizeColor(Color, FLinearColor::White).ToFColor(true);
	}

	/** Local-space quad -> window-space Slate vertices, matching how custom verts are drawn. */
	void CombatPaintAddQuad(
		const FGeometry& Geometry,
		TArray<FSlateVertex>& Verts,
		TArray<SlateIndex>& Indices,
		const FVector2D OutQuad[4],
		const FColor& Color)
	{
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const SlateIndex Base = static_cast<SlateIndex>(Verts.Num());

		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Verts.Add(FSlateVertex::Make(
				Transform,
				FVector2f(
					static_cast<float>(OutQuad[Corner].X),
					static_cast<float>(OutQuad[Corner].Y)),
				FVector2f(0.5f, 0.5f),
				Color));
		}

		Indices.Add(Base + 0);
		Indices.Add(Base + 1);
		Indices.Add(Base + 2);
		Indices.Add(Base + 0);
		Indices.Add(Base + 2);
		Indices.Add(Base + 3);
	}

	void CombatPaintAddDisc(
		const FGeometry& Geometry,
		TArray<FSlateVertex>& Verts,
		TArray<SlateIndex>& Indices,
		const FVector2D& Center,
		float Radius,
		int32 Sides,
		const FColor& Color)
	{
		if (Radius <= 0.0f || Sides < 3)
		{
			return;
		}

		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const SlateIndex Base = static_cast<SlateIndex>(Verts.Num());
		const int32 SafeSides = FMath::Clamp(Sides, 3, 64);

		Verts.Add(FSlateVertex::Make(
			Transform,
			FVector2f(static_cast<float>(Center.X), static_cast<float>(Center.Y)),
			FVector2f(0.5f, 0.5f),
			Color));

		for (int32 Side = 0; Side < SafeSides; ++Side)
		{
			const float Angle = 2.0f * PI * static_cast<float>(Side) / static_cast<float>(SafeSides);
			const FVector2D Point = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			Verts.Add(FSlateVertex::Make(
				Transform,
				FVector2f(static_cast<float>(Point.X), static_cast<float>(Point.Y)),
				FVector2f(0.5f, 0.5f),
				Color));
		}

		for (int32 Side = 0; Side < SafeSides; ++Side)
		{
			const SlateIndex Current = Base + 1 + static_cast<SlateIndex>(Side);
			const SlateIndex Next = Base + 1 + static_cast<SlateIndex>((Side + 1) % SafeSides);
			Indices.Add(Base);
			Indices.Add(Current);
			Indices.Add(Next);
		}
	}

	void CombatPaintAddTriangle(
		const FGeometry& Geometry,
		TArray<FSlateVertex>& Verts,
		TArray<SlateIndex>& Indices,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		const FColor& Color)
	{
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const SlateIndex Base = static_cast<SlateIndex>(Verts.Num());
		const FVector2D Points[3] = { A, B, C };

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			Verts.Add(FSlateVertex::Make(
				Transform,
				FVector2f(
					static_cast<float>(Points[Corner].X),
					static_cast<float>(Points[Corner].Y)),
				FVector2f(0.5f, 0.5f),
				Color));
		}

		Indices.Add(Base + 0);
		Indices.Add(Base + 1);
		Indices.Add(Base + 2);
	}

	void CombatPaintDrawVertices(
		FSlateWindowElementList& OutDrawElements,
		int32 Layer,
		const FSlateResourceHandle& Handle,
		TArray<FSlateVertex>& Verts,
		const TArray<SlateIndex>& Indices)
	{
		if (Handle.IsValid() && Verts.Num() > 0 && Indices.Num() > 0)
		{
			// Custom vertices use atlas UVs directly, unlike ordinary Slate boxes.
			const FSlateShaderResourceProxy* Proxy = Handle.GetResourceProxy();
			const FVector2f WhiteUV = Proxy->StartUV + Proxy->SizeUV * 0.5f;
			for (FSlateVertex& Vertex : Verts)
			{
				Vertex.TexCoords[0] = WhiteUV.X;
				Vertex.TexCoords[1] = WhiteUV.Y;
			}
			FSlateDrawElement::MakeCustomVerts(
				OutDrawElements,
				static_cast<uint32>(Layer),
				Handle,
				Verts,
				Indices,
				nullptr,
				0,
				0);
		}
	}

	void CombatPaintDrawBox(
		FSlateWindowElementList& OutDrawElements,
		int32 Layer,
		const FGeometry& Geometry,
		const FVector2D& Position,
		const FVector2D& Size,
		const FSlateBrush* Brush,
		const FLinearColor& Color)
	{
		if (!Brush || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return;
		}

		const FPaintGeometry PaintGeometry = Geometry.ToPaintGeometry(
			Size, FSlateLayoutTransform(Position));
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			static_cast<uint32>(Layer),
			PaintGeometry,
			Brush,
			ESlateDrawEffect::None,
			Color);
	}

	void CombatPaintDrawText(
		FSlateWindowElementList& OutDrawElements,
		int32 Layer,
		const FGeometry& Geometry,
		const FVector2D& Position,
		const FVector2D& Size,
		const FString& Text,
		const FSlateFontInfo& Font,
		const FLinearColor& Color)
	{
		if (Text.IsEmpty() || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return;
		}

		const FPaintGeometry PaintGeometry = Geometry.ToPaintGeometry(
			Size, FSlateLayoutTransform(Position));
		OutDrawElements.PushClip(FSlateClippingZone(PaintGeometry));
		FSlateDrawElement::MakeText(
			OutDrawElements,
			static_cast<uint32>(Layer),
			PaintGeometry,
			Text,
			Font,
			ESlateDrawEffect::None,
			Color);
		OutDrawElements.PopClip();
	}

	/** Small skull with dark eye sockets, nose and two jaw teeth. */
	void CombatPaintDrawSkull(
		FSlateWindowElementList& OutDrawElements,
		int32 Layer,
		const FGeometry& Geometry,
		const FSlateResourceHandle& Handle,
		const FSlateBrush* Brush,
		const FVector2D& Center,
		float Diameter,
		const FLinearColor& Fill,
		const FLinearColor& Hole)
	{
		const float Radius = FMath::Max(3.0f, Diameter * 0.5f);
		const FVector2D HeadCenter(Center.X, Center.Y - Radius * 0.18f);
		const FColor FillColor = CombatPaintPackColor(Fill);
		const FColor HoleColor = CombatPaintPackColor(Hole);

		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		Verts.Reserve(64);
		Indices.Reserve(96);

		CombatPaintAddDisc(Geometry, Verts, Indices, HeadCenter, Radius * 0.78f, SkullHeadSides, FillColor);
		CombatPaintAddDisc(
			Geometry, Verts, Indices,
			FVector2D(Center.X - Radius * 0.30f, Center.Y - Radius * 0.24f),
			Radius * 0.20f, SkullEyeSides, HoleColor);
		CombatPaintAddDisc(
			Geometry, Verts, Indices,
			FVector2D(Center.X + Radius * 0.30f, Center.Y - Radius * 0.24f),
			Radius * 0.20f, SkullEyeSides, HoleColor);
		CombatPaintAddTriangle(
			Geometry, Verts, Indices,
			FVector2D(Center.X, Center.Y + Radius * 0.02f),
			FVector2D(Center.X - Radius * 0.11f, Center.Y + Radius * 0.24f),
			FVector2D(Center.X + Radius * 0.11f, Center.Y + Radius * 0.24f),
			HoleColor);
		CombatPaintDrawVertices(OutDrawElements, Layer, Handle, Verts, Indices);

		CombatPaintDrawBox(
			OutDrawElements, Layer, Geometry,
			FVector2D(Center.X - Radius * 0.46f, Center.Y + Radius * 0.34f),
			FVector2D(Radius * 0.92f, Radius * 0.50f),
			Brush, Fill);

		CombatPaintDrawBox(
			OutDrawElements, Layer, Geometry,
			FVector2D(Center.X - Radius * 0.19f, Center.Y + Radius * 0.36f),
			FVector2D(Radius * 0.07f, Radius * 0.46f),
			Brush, Hole);
		CombatPaintDrawBox(
			OutDrawElements, Layer, Geometry,
			FVector2D(Center.X + Radius * 0.12f, Center.Y + Radius * 0.36f),
			FVector2D(Radius * 0.07f, Radius * 0.46f),
			Brush, Hole);
	}

	/** Target ring with four cardinal ticks, used for the player-death row. */
	void CombatPaintDrawTarget(
		FSlateWindowElementList& OutDrawElements,
		int32 Layer,
		const FGeometry& Geometry,
		const FSlateResourceHandle& Handle,
		const FSlateBrush* Brush,
		const FVector2D& Center,
		float Diameter,
		const FLinearColor& Fill,
		const FLinearColor& Hole)
	{
		const float Radius = FMath::Max(3.0f, Diameter * 0.5f);
		const FColor FillColor = CombatPaintPackColor(Fill);
		const FColor HoleColor = CombatPaintPackColor(Hole);

		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		Verts.Reserve(48);
		Indices.Reserve(72);

		CombatPaintAddDisc(Geometry, Verts, Indices, Center, Radius, SkullHeadSides, FillColor);
		CombatPaintAddDisc(Geometry, Verts, Indices, Center, Radius * 0.58f, SkullHeadSides, HoleColor);
		CombatPaintAddDisc(Geometry, Verts, Indices, Center, Radius * 0.20f, SkullEyeSides, FillColor);
		CombatPaintDrawVertices(OutDrawElements, Layer, Handle, Verts, Indices);

		const float TickWidth = FMath::Max(1.0f, Radius * 0.18f);
		const float TickLength = FMath::Max(1.0f, Radius * 0.30f);
		CombatPaintDrawBox(
			OutDrawElements, Layer, Geometry,
			FVector2D(Center.X - TickWidth * 0.5f, Center.Y - Radius - TickLength * 0.45f),
			FVector2D(TickWidth, TickLength), Brush, Fill);
		CombatPaintDrawBox(
			OutDrawElements, Layer, Geometry,
			FVector2D(Center.X - TickWidth * 0.5f, Center.Y + Radius - TickLength * 0.55f),
			FVector2D(TickWidth, TickLength), Brush, Fill);
		CombatPaintDrawBox(
			OutDrawElements, Layer, Geometry,
			FVector2D(Center.X - Radius - TickLength * 0.45f, Center.Y - TickWidth * 0.5f),
			FVector2D(TickLength, TickWidth), Brush, Fill);
		CombatPaintDrawBox(
			OutDrawElements, Layer, Geometry,
			FVector2D(Center.X + Radius - TickLength * 0.55f, Center.Y - TickWidth * 0.5f),
			FVector2D(TickLength, TickWidth), Brush, Fill);
	}
}

UDustCombatFeedbackWidget::UDustCombatFeedbackWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FeedFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15.0f);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UDustCombatFeedbackWidget::SetCrosshairSource(UDynamicCrosshairWidget* Source)
{
	CrosshairSource = Source;
	if (Source)
	{
		LastCrosshairGap = CombatPaintSanitizeGap(Source->GetCrosshairGap(), FallbackCrosshairGap);
	}
	InvalidateFeedbackPaint();
}

void UDustCombatFeedbackWidget::NotifyConfirmedHit(float ActualDamage, bool bHeadshot, bool bKilled)
{
	SyncTuning();
	if (!Feedback.NotifyConfirmedHit(ActualDamage, bHeadshot, bKilled))
	{
		return;
	}

	// A killing hit plays only the kill sound; ordinary hits use HitSound.
	PlayLocalFeedbackSound(bKilled ? KillSound.Get() : HitSound.Get());
	InvalidateFeedbackPaint();
}

void UDustCombatFeedbackWidget::NotifyPlayerKilled()
{
	SyncTuning();
	Feedback.NotifyPlayerKilled();
	InvalidateFeedbackPaint();
}

void UDustCombatFeedbackWidget::ClearCenterFeedback()
{
	Feedback.Center.Reset();
	InvalidateFeedbackPaint();
}

void UDustCombatFeedbackWidget::ResetFeedback()
{
	Feedback.Reset();
	InvalidateFeedbackPaint();
}

void UDustCombatFeedbackWidget::SetCinematicBarsVisible(bool bVisible)
{
	if (bCinematicBarsVisible != bVisible)
	{
		bCinematicBarsVisible = bVisible;
		InvalidateFeedbackPaint();
	}
}

void UDustCombatFeedbackWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SyncTuning();
	RefreshCrosshairGap();
	InvalidateFeedbackPaint();
}

void UDustCombatFeedbackWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	SyncTuning();
	const bool bHadActiveFeedback = Feedback.Center.IsMarkerActive()
		|| Feedback.Center.IsSkullActive()
		|| Feedback.Feed.Num() > 0;

	// FDustCombatCenterFeedbackState / FDustCombatFeedState ignore invalid or
	// negative deltas internally; the widget never forward-runs a timer.
	Feedback.Advance(InDeltaTime);
	RefreshCrosshairGap();

	const bool bHasActiveFeedback = Feedback.Center.IsMarkerActive()
		|| Feedback.Center.IsSkullActive()
		|| Feedback.Feed.Num() > 0;
	if (bHadActiveFeedback || bHasActiveFeedback)
	{
		InvalidateFeedbackPaint();
	}
}

void UDustCombatFeedbackWidget::NativeDestruct()
{
	ResetFeedback();
	CrosshairSource.Reset();
	Super::NativeDestruct();
}

FDustCombatFeedbackTuning UDustCombatFeedbackWidget::BuildTuning() const
{
	FDustCombatFeedbackTuning Result;
	Result.MarkerFlashSeconds = MarkerFlashSeconds;
	Result.SkullSeconds = SkullSeconds;
	Result.FeedShiftSeconds = FeedShiftSeconds;
	return Result;
}

void UDustCombatFeedbackWidget::SyncTuning()
{
	Feedback.SetTuning(BuildTuning());
}

void UDustCombatFeedbackWidget::RefreshCrosshairGap()
{
	if (!CrosshairSource.IsValid())
	{
		return;
	}

	// Deliberately no IsVisible()/Collapsed check: ADS may hide the source widget,
	// but its current gap is still the authoritative spacing for this overlay.
	FVector2D SafeGap = CombatPaintSanitizeGap(
		CrosshairSource->GetCrosshairGap(), LastCrosshairGap);
	const FGeometry& SourceGeometry = CrosshairSource->GetCachedGeometry();
	const FGeometry& OverlayGeometry = GetCachedGeometry();
	if (SourceGeometry.GetLocalSize().X > 0.0 && OverlayGeometry.GetLocalSize().X > 0.0)
	{
		const FVector2D Origin = OverlayGeometry.AbsoluteToLocal(
			SourceGeometry.LocalToAbsolute(FVector2D::ZeroVector));
		const FVector2D X = OverlayGeometry.AbsoluteToLocal(
			SourceGeometry.LocalToAbsolute(FVector2D(SafeGap.X, 0.0))) - Origin;
		const FVector2D Y = OverlayGeometry.AbsoluteToLocal(
			SourceGeometry.LocalToAbsolute(FVector2D(0.0, SafeGap.Y))) - Origin;
		SafeGap = CombatPaintSanitizeGap(FVector2D(X.Size(), Y.Size()), SafeGap);
	}
	if (!SafeGap.Equals(LastCrosshairGap, KINDA_SMALL_NUMBER))
	{
		LastCrosshairGap = SafeGap;
		if (Feedback.Center.IsMarkerActive() || Feedback.Center.IsSkullActive())
			InvalidateFeedbackPaint();
	}
}

void UDustCombatFeedbackWidget::InvalidateFeedbackPaint()
{
	if (TSharedPtr<SWidget> CachedWidget = GetCachedWidget())
	{
		CachedWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UDustCombatFeedbackWidget::PlayLocalFeedbackSound(USoundBase* Sound) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

FSlateFontInfo UDustCombatFeedbackWidget::GetSafeFeedFont() const
{
	if (FeedFont.FontObject == nullptr
		|| !FMath::IsFinite(FeedFont.Size)
		|| FeedFont.Size <= 0.0f)
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15.0f);
	}
	return FeedFont;
}

int32 UDustCombatFeedbackWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	int32 ResultLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	const FVector2f ViewportSize(AllottedGeometry.GetLocalSize());
	if (!FMath::IsFinite(ViewportSize.X) || !FMath::IsFinite(ViewportSize.Y)
		|| ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
	{
		return ResultLayer;
	}

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	if (!WhiteBrush)
	{
		return ResultLayer;
	}
	const FSlateResourceHandle WhiteHandle = WhiteBrush->GetRenderingResource();

	const FVector2D MarkerCenter(ViewportSize.X * 0.5f, ViewportSize.Y * 0.5f);
	const FVector2D Gap = CombatPaintSanitizeGap(LastCrosshairGap, FallbackCrosshairGap);
	const float SafeArmLength = FDustCombatTaperGeometry::SanitizeNonNegative(MarkerArmLength, 11.0f);

	// Right-upper feed. The inset only consumes the upper cinematic bar area when
	// the integration layer says that bar is actually visible.
	const int32 FeedLayer = CombatPaintNextPaintLayer(ResultLayer);
	const float FeedOuterPadding = FMath::Max(0.0f, FeedPadding);
	const float AvailableWidth = FMath::Max(1.0f, ViewportSize.X - FeedOuterPadding * 2.0f);
	const float FeedPixelWidth = FMath::Clamp(
		FeedWidth, FMath::Min(160.0f, AvailableWidth), AvailableWidth);
	const float TopInset = bCinematicBarsVisible
		? ViewportSize.Y * FMath::Clamp(CinematicInsetFraction, 0.0f, 1.0f)
		: 0.0f;
	const float FeedTop = FeedOuterPadding + TopInset;
	const float FeedLeft = ViewportSize.X - FeedOuterPadding - FeedPixelWidth;
	const float RowHeight = FMath::Max(16.0f, FeedRowHeight);
	const float RowSpacing = FMath::Max(0.0f, FeedRowSpacing);
	const float IconDiameter = FMath::Clamp(RowHeight - 8.0f, 10.0f, 22.0f);
	const FSlateFontInfo FeedFontInfo = GetSafeFeedFont();
	const FLinearColor SafeTextColor = CombatPaintSanitizeColor(FeedTextColor, FLinearColor::White);
	const FLinearColor HoleColor(0.0f, 0.0f, 0.0f, 0.9f);

	const TArray<FDustCombatFeedRow>& Rows = Feedback.Feed.GetRows();
	for (int32 RowIndex = Rows.Num() - 1; RowIndex >= 0; --RowIndex)
	{
		const FDustCombatFeedRow& Row = Rows[RowIndex];
		if (!FMath::IsFinite(Row.AnimatedIndex))
		{
			continue;
		}

		const float RowY = FeedTop + Row.AnimatedIndex * (RowHeight + RowSpacing);
		if (RowY + RowHeight < 0.0f || RowY > ViewportSize.Y)
		{
			continue;
		}

		const FDustCombatFeedPresentation Presentation = ResolveDustCombatFeedPresentation(
			Row.Event, BodyKillAccent, HeadshotKillAccent, PlayerKilledAccent);
		const FLinearColor Accent = CombatPaintSanitizeColor(Presentation.Accent, FLinearColor::White);

		CombatPaintDrawBox(
			OutDrawElements, FeedLayer, AllottedGeometry,
			FVector2D(FeedLeft, RowY),
			FVector2D(FeedPixelWidth, RowHeight),
			WhiteBrush, CombatPaintSanitizeColor(FeedRowBackground, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f)));

		CombatPaintDrawBox(
			OutDrawElements, FeedLayer, AllottedGeometry,
			FVector2D(FeedLeft, RowY),
			FVector2D(3.0f, RowHeight),
			WhiteBrush, Accent);

		const FVector2D IconCenter(
			FeedLeft + 8.0f + IconDiameter * 0.5f,
			RowY + RowHeight * 0.5f);
		CombatPaintDrawSkull(OutDrawElements, FeedLayer, AllottedGeometry,
			WhiteHandle, WhiteBrush, IconCenter, IconDiameter, Accent, HoleColor);
		if (Presentation.Icon == EDustCombatFeedIcon::HeadshotSkull)
			CombatPaintDrawTarget(OutDrawElements, FeedLayer, AllottedGeometry, WhiteHandle, WhiteBrush,
				IconCenter + FVector2D(IconDiameter * 0.33f, -IconDiameter * 0.30f),
				IconDiameter * 0.55f, Accent, HoleColor);

		const float TextX = FeedLeft + 8.0f + IconDiameter + 8.0f;
		const float TextWidth = FMath::Max(1.0f, FeedLeft + FeedPixelWidth - 8.0f - TextX);
		const float TextY = RowY + FMath::Max(0.0f, (RowHeight - FeedFontInfo.Size) * 0.5f);
		CombatPaintDrawText(
			OutDrawElements, FeedLayer, AllottedGeometry,
			FVector2D(TextX, TextY),
			FVector2D(TextWidth, RowHeight),
			Presentation.Text, FeedFontInfo, SafeTextColor);
	}

	// Center marker: four tapered diagonals, no center dot.
	const int32 MarkerLayer = CombatPaintNextPaintLayer(FeedLayer);
	if (Feedback.Center.IsMarkerActive() && SafeArmLength > 0.0f)
	{
		const FDustCombatHitSelection Selection = FDustCombatHitSelection::Resolve(
			Feedback.Center.IsMarkerHeadshot(), BodyHitColor, HeadHitColor);
		FLinearColor MarkerColor = CombatPaintSanitizeColor(Selection.Color, FLinearColor::White);
		MarkerColor.A *= Feedback.Center.GetMarkerAlpha();

		FDustCombatTaperArm Arms[FDustCombatTaperGeometry::ArmCount];
		FDustCombatTaperGeometry::MakeArms(
			Gap, SafeArmLength, MarkerInnerThickness, MarkerOuterThickness, Arms);

		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		Verts.Reserve(FDustCombatTaperGeometry::ArmCount * 4);
		Indices.Reserve(FDustCombatTaperGeometry::ArmCount * 6);
		const FColor VertexColor = CombatPaintPackColor(MarkerColor);

		for (const FDustCombatTaperArm& Arm : Arms)
		{
			FVector2D Quad[4];
			Arm.GetQuad(Quad);
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Quad[Corner] += MarkerCenter;
			}
			CombatPaintAddQuad(AllottedGeometry, Verts, Indices, Quad, VertexColor);
		}
		CombatPaintDrawVertices(OutDrawElements, MarkerLayer, WhiteHandle, Verts, Indices);
	}

	// Independent skull, centered above the marker so it never covers the aim point.
	const int32 SkullLayer = CombatPaintNextPaintLayer(MarkerLayer);
	if (Feedback.Center.IsSkullActive())
	{
		const FDustCombatHitSelection Selection = FDustCombatHitSelection::Resolve(
			Feedback.Center.IsSkullHeadshot(), BodyHitColor, HeadHitColor);
		FLinearColor SkullColor = CombatPaintSanitizeColor(Selection.Color, FLinearColor::White);
		SkullColor.A *= Feedback.Center.GetSkullAlpha();

		const float Diameter = FMath::Clamp(SkullSize, 6.0f, 32.0f);
		const float Radius = Diameter * 0.5f;
		const FVector2D SkullCenter(
			MarkerCenter.X,
			MarkerCenter.Y - Gap.Y - SafeArmLength - Radius - FMath::Max(0.0f, SkullMarkerGap));

		CombatPaintDrawSkull(
			OutDrawElements, SkullLayer, AllottedGeometry,
			WhiteHandle, WhiteBrush, SkullCenter, Diameter, SkullColor,
			FLinearColor(0.0f, 0.0f, 0.0f, FMath::Clamp(SkullColor.A * 0.85f, 0.0f, 1.0f)));
	}

	return SkullLayer;
}
