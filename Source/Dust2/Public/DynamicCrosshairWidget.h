#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairStyleData.h"
#include "DynamicCrosshairWidget.generated.h"

/** A small, game-agnostic four-line crosshair renderer. */
UCLASS(BlueprintType, Blueprintable)
class DUST2_API UDynamicCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shared visual preset. Instance overrides take priority when enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style")
	TObjectPtr<UCrosshairStyleData> StyleAsset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style")
	bool bUseStyleOverride = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style",
		meta = (EditCondition = "bUseStyleOverride", EditConditionHides))
	FCrosshairStyleSettings StyleOverride;

	/** Get the active override, asset settings, or legacy appearance fields, in that order. */
	UFUNCTION(BlueprintPure, Category = "Crosshair|Style")
	FCrosshairStyleSettings GetResolvedStyleSettings() const;

	/** Effective minimum center-to-inner-edge distance for horizontal and vertical lines. */
	UFUNCTION(BlueprintPure, Category = "Crosshair|Style")
	FVector2D GetCrosshairMinimumGap() const;

	/** Convert a projected spread target before starting a display transition. */
	UFUNCTION(BlueprintPure, Category = "Crosshair|Style")
	FVector2D ResolveStyleTargetGap(FVector2D ProjectedGap) const;

	/** Set a local widget override without changing the shared style asset. */
	UFUNCTION(BlueprintCallable, Category = "Crosshair|Style")
	void ApplyStyleOverride(FCrosshairStyleSettings InSettings);

	UFUNCTION(BlueprintCallable, Category = "Crosshair|Style")
	void ClearStyleOverride();

	UFUNCTION(BlueprintCallable, Category = "Crosshair|Style")
	void SetStyleAsset(UCrosshairStyleData* InStyleAsset);

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
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "StyleAsset == nullptr", EditConditionHides))
	FVector2D MinimumGap = FVector2D(3.0f, 3.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Appearance",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "StyleAsset == nullptr", EditConditionHides))
	float LineLength = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Appearance",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "StyleAsset == nullptr", EditConditionHides))
	float LineThickness = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Appearance",
		meta = (EditCondition = "StyleAsset == nullptr", EditConditionHides))
	FLinearColor CrosshairColor = FLinearColor::White;

protected:
	virtual void NativePreConstruct() override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	void InvalidateCrosshairPaint();

	FVector2D CrosshairGap = FVector2D::ZeroVector;
};
