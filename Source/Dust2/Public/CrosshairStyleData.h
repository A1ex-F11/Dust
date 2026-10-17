#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CrosshairStyleData.generated.h"

/** Reusable visual settings for a four-line crosshair. */
USTRUCT(BlueprintType)
struct DUST2_API FCrosshairStyleSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style",
		meta = (DisplayName="中心间距", ClampMin = "0.0", UIMin = "0.0"))
	FVector2D MinimumGap = FVector2D(3.0f, 3.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style",
		meta = (DisplayName="线条长度", ClampMin = "0.0", UIMin = "0.0"))
	float LineLength = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style",
		meta = (DisplayName="线条粗细", ClampMin = "0.0", UIMin = "0.0"))
	float LineThickness = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style", meta=(DisplayName="准星颜色（含透明度）"))
	FLinearColor CrosshairColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style", meta=(DisplayName="启用动态扩散",ToolTip="只控制准星是否跟随散布变化，不改变枪械弹道精度。"))
	bool bDynamicSpread = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Style",
		meta = (DisplayName="扩散显示强度", ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0", ToolTip="只缩放超过中心间距的显示扩张量；1保持当前表现。"))
	float SpreadDisplayScale = 1.0f;

	/** Return finite, editor-independent values safe for drawing and gap calculations. */
	FCrosshairStyleSettings GetSanitized() const;
};

/** Shared crosshair appearance preset. Runtime widget overrides never write into this asset. */
UCLASS(BlueprintType, meta=(DisplayName="准星样式预设"))
class DUST2_API UCrosshairStyleData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "准星样式", meta=(DisplayName="样式参数",ShowOnlyInnerProperties))
	FCrosshairStyleSettings Settings;
};
