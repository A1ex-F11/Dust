#include "DynamicCrosshairWidget.h"

#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"

namespace
{
	float SanitizeGapComponent(float Value, float MinimumValue)
	{
		return FMath::IsFinite(Value)
			? FMath::Max(MinimumValue, Value)
			: MinimumValue;
	}

	float SaturatingFloat(double Value)
	{
		const double MaxFloat = static_cast<double>(TNumericLimits<float>::Max());
		return static_cast<float>(FMath::Clamp(Value, -MaxFloat, MaxFloat));
	}

	FVector2D SafePosition(double X, double Y)
	{
		return FVector2D(SaturatingFloat(X), SaturatingFloat(Y));
	}

	FLinearColor SanitizeColor(const FLinearColor& Color, const FLinearColor& Fallback)
	{
		const auto SafeChannel = [](float Value, float DefaultValue)
		{
			return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : DefaultValue;
		};

		return FLinearColor(
			SafeChannel(Color.R, Fallback.R),
			SafeChannel(Color.G, Fallback.G),
			SafeChannel(Color.B, Fallback.B),
			SafeChannel(Color.A, Fallback.A));
	}
}

FCrosshairStyleSettings FCrosshairStyleSettings::GetSanitized() const
{
	FCrosshairStyleSettings Safe = *this;
	const auto NonNegativeOrDefault = [](float Value, float DefaultValue)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : DefaultValue;
	};
	const auto ColorChannel = [](float Value, float DefaultValue)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : DefaultValue;
	};

	Safe.MinimumGap = FVector2D(
		NonNegativeOrDefault(MinimumGap.X, 3.0f),
		NonNegativeOrDefault(MinimumGap.Y, 3.0f));
	Safe.LineLength = NonNegativeOrDefault(LineLength, 7.0f);
	Safe.LineThickness = NonNegativeOrDefault(LineThickness, 1.75f);
	Safe.CrosshairColor = FLinearColor(
		ColorChannel(CrosshairColor.R, 1.0f),
		ColorChannel(CrosshairColor.G, 1.0f),
		ColorChannel(CrosshairColor.B, 1.0f),
		ColorChannel(CrosshairColor.A, 1.0f));
	Safe.SpreadDisplayScale = FMath::IsFinite(SpreadDisplayScale)
		? FMath::Clamp(SpreadDisplayScale, 0.0f, 10.0f)
		: 1.0f;
	return Safe;
}

void UDynamicCrosshairWidget::SetCrosshairGap(FVector2D InGap)
{
	const FVector2D SafeMinimumGap = GetCrosshairMinimumGap();
	const FVector2D NewGap(
		SanitizeGapComponent(InGap.X, SafeMinimumGap.X),
		SanitizeGapComponent(InGap.Y, SafeMinimumGap.Y));

	if (CrosshairGap != NewGap)
	{
		CrosshairGap = NewGap;
		InvalidateCrosshairPaint();
	}
}

FVector2D UDynamicCrosshairWidget::GetCrosshairGap() const
{
	const FCrosshairStyleSettings Style = GetResolvedStyleSettings();
	const FVector2D SafeMinimumGap = Style.MinimumGap;
	if (!Style.bDynamicSpread)
	{
		return SafeMinimumGap;
	}

	return FVector2D(
		SanitizeGapComponent(CrosshairGap.X, SafeMinimumGap.X),
		SanitizeGapComponent(CrosshairGap.Y, SafeMinimumGap.Y));
}

FCrosshairStyleSettings UDynamicCrosshairWidget::GetResolvedStyleSettings() const
{
	if (bUseStyleOverride)
	{
		return StyleOverride.GetSanitized();
	}
	if (StyleAsset)
	{
		return StyleAsset->Settings.GetSanitized();
	}

	FCrosshairStyleSettings LegacySettings;
	LegacySettings.MinimumGap = MinimumGap;
	LegacySettings.LineLength = LineLength;
	LegacySettings.LineThickness = LineThickness;
	LegacySettings.CrosshairColor = CrosshairColor;
	return LegacySettings.GetSanitized();
}

FVector2D UDynamicCrosshairWidget::GetCrosshairMinimumGap() const
{
	return GetResolvedStyleSettings().MinimumGap;
}

FVector2D UDynamicCrosshairWidget::ResolveStyleTargetGap(FVector2D ProjectedGap) const
{
	const FCrosshairStyleSettings Style = GetResolvedStyleSettings();
	const FVector2D Minimum = Style.MinimumGap;
	if (!Style.bDynamicSpread)
	{
		return Minimum;
	}

	const auto ResolveComponent = [Scale = static_cast<double>(Style.SpreadDisplayScale)](
		float Projected, float MinimumValue)
	{
		if (!FMath::IsFinite(Projected))
		{
			return MinimumValue;
		}
		const double Resolved = static_cast<double>(MinimumValue)
			+ FMath::Max(0.0, static_cast<double>(Projected) - MinimumValue) * Scale;
		const double MaxFloat = static_cast<double>(TNumericLimits<float>::Max());
		return static_cast<float>(FMath::Clamp(Resolved, static_cast<double>(MinimumValue), MaxFloat));
	};
	return FVector2D(
		ResolveComponent(ProjectedGap.X, Minimum.X),
		ResolveComponent(ProjectedGap.Y, Minimum.Y));
}

void UDynamicCrosshairWidget::ApplyStyleOverride(FCrosshairStyleSettings InSettings)
{
	StyleOverride = InSettings.GetSanitized();
	bUseStyleOverride = true;
	InvalidateCrosshairPaint();
}

void UDynamicCrosshairWidget::ClearStyleOverride()
{
	if (bUseStyleOverride)
	{
		bUseStyleOverride = false;
		InvalidateCrosshairPaint();
	}
}

void UDynamicCrosshairWidget::SetStyleAsset(UCrosshairStyleData* InStyleAsset)
{
	if (StyleAsset != InStyleAsset)
	{
		StyleAsset = InStyleAsset;
		InvalidateCrosshairPaint();
	}
}

void UDynamicCrosshairWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	InvalidateCrosshairPaint();
}

void UDynamicCrosshairWidget::InvalidateCrosshairPaint()
{
	if (TSharedPtr<SWidget> CachedWidget = GetCachedWidget())
	{
		CachedWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 UDynamicCrosshairWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const int32 ParentLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	const FCrosshairStyleSettings Style = GetResolvedStyleSettings();
	const float SafeLength = Style.LineLength;
	const float SafeThickness = Style.LineThickness;
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (SafeLength <= 0.0f || SafeThickness <= 0.0f
		|| !FMath::IsFinite(LocalSize.X) || !FMath::IsFinite(LocalSize.Y)
		|| LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return ParentLayer;
	}

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	if (!WhiteBrush)
	{
		return ParentLayer;
	}

	const FVector2D Center(LocalSize.X * 0.5f, LocalSize.Y * 0.5f);
	const FVector2D Gap = GetCrosshairGap();
	const FVector2D HorizontalSize(SafeLength, SafeThickness);
	const FVector2D VerticalSize(SafeThickness, SafeLength);
	const double HalfThickness = static_cast<double>(SafeThickness) * 0.5;

	const FLinearColor CrosshairTint = Style.CrosshairColor;
	const FLinearColor ParentTint = SanitizeColor(
		InWidgetStyle.GetColorAndOpacityTint(), FLinearColor::White);
	const FLinearColor FinalTint = CrosshairTint * ParentTint;

	const int32 CrosshairLayer = ParentLayer < MAX_int32 ? ParentLayer + 1 : ParentLayer;
	const auto DrawSegment = [&](const FVector2D& Position, const FVector2D& Size)
	{
		const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry(
			Size, FSlateLayoutTransform(Position));
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			static_cast<uint32>(CrosshairLayer),
			PaintGeometry,
			WhiteBrush,
			ESlateDrawEffect::None,
			FinalTint);
	};

	const double CenterX = static_cast<double>(Center.X);
	const double CenterY = static_cast<double>(Center.Y);
	const double GapX = static_cast<double>(Gap.X);
	const double GapY = static_cast<double>(Gap.Y);
	const double Length = static_cast<double>(SafeLength);

	DrawSegment(SafePosition(CenterX - GapX - Length, CenterY - HalfThickness), HorizontalSize);
	DrawSegment(SafePosition(CenterX + GapX, CenterY - HalfThickness), HorizontalSize);
	DrawSegment(SafePosition(CenterX - HalfThickness, CenterY - GapY - Length), VerticalSize);
	DrawSegment(SafePosition(CenterX - HalfThickness, CenterY + GapY), VerticalSize);

	return CrosshairLayer;
}
