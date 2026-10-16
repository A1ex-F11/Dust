#include "DynamicCrosshairWidget.h"

#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"

namespace
{
	float SanitizeNonNegative(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

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

void UDynamicCrosshairWidget::SetCrosshairGap(FVector2D InGap)
{
	const FVector2D SafeMinimumGap = GetSafeMinimumGap();
	const FVector2D NewGap(
		SanitizeGapComponent(InGap.X, SafeMinimumGap.X),
		SanitizeGapComponent(InGap.Y, SafeMinimumGap.Y));

	if (CrosshairGap != NewGap)
	{
		CrosshairGap = NewGap;
		if (TSharedPtr<SWidget> CachedWidget = GetCachedWidget())
		{
			CachedWidget->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
}

FVector2D UDynamicCrosshairWidget::GetCrosshairGap() const
{
	const FVector2D SafeMinimumGap = GetSafeMinimumGap();
	return FVector2D(
		SanitizeGapComponent(CrosshairGap.X, SafeMinimumGap.X),
		SanitizeGapComponent(CrosshairGap.Y, SafeMinimumGap.Y));
}

FVector2D UDynamicCrosshairWidget::GetSafeMinimumGap() const
{
	return FVector2D(
		SanitizeNonNegative(MinimumGap.X),
		SanitizeNonNegative(MinimumGap.Y));
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

	const float SafeLength = SanitizeNonNegative(LineLength);
	const float SafeThickness = SanitizeNonNegative(LineThickness);
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

	const FLinearColor CrosshairTint = SanitizeColor(CrosshairColor, FLinearColor::White);
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
