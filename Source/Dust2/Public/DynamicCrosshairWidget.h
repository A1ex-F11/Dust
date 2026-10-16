#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DynamicCrosshairWidget.generated.h"

/** A small, game-agnostic four-line crosshair renderer. */
UCLASS(BlueprintType, Blueprintable)
class DUST2_API UDynamicCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Set the horizontal and vertical distance from the widget center to each
	 * line's inner edge, in UMG/Slate layout units.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosshair|Display")
	void SetCrosshairGap(FVector2D InGap);

	/** Current inner-edge distance after applying MinimumGap. */
	UFUNCTION(BlueprintPure, Category = "Crosshair|Display")
	FVector2D GetCrosshairGap() const;

	/** Minimum center-to-inner-edge distance for horizontal and vertical lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Appearance",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	FVector2D MinimumGap = FVector2D(3.0f, 3.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Appearance",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LineLength = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Appearance",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LineThickness = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Appearance")
	FLinearColor CrosshairColor = FLinearColor::White;

protected:
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	FVector2D GetSafeMinimumGap() const;

	FVector2D CrosshairGap = FVector2D::ZeroVector;
};
