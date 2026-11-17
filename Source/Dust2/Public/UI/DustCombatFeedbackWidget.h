#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "UI/DustCombatFeedbackMath.h"
#include "DustCombatFeedbackWidget.generated.h"

class UDynamicCrosshairWidget;
class USoundBase;

/**
 * Native-drawn confirmed-combat overlay.
 *
 * This widget is intentionally independent from the battle HUD: it can stay
 * visible when the HUD is collapsed on death, draws its marker/feed entirely in
 * NativePaint, and needs no texture to operate. Gameplay code owns the truth and
 * only calls the notify functions with already-confirmed facts; this class never
 * queries actors, bones, health or damage rules.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "战斗反馈"))
class DUST2_API UDustCombatFeedbackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDustCombatFeedbackWidget(const FObjectInitializer& ObjectInitializer);

	/** Read the shared crosshair gap each tick, even while that widget is hidden during ADS. */
	UFUNCTION(BlueprintCallable, Category = "战斗反馈|准星",
		meta = (DisplayName = "设置准星间距来源"))
	void SetCrosshairSource(UDynamicCrosshairWidget* Source);

	/** Apply one caller-confirmed positive-damage event. Kill state is presentation metadata only. */
	UFUNCTION(BlueprintCallable, Category = "战斗反馈|中心",
		meta = (DisplayName = "通知确认命中"))
	void NotifyConfirmedHit(float ActualDamage, bool bHeadshot, bool bKilled);

	/** Clear the center, retain the feed history, and append one death row. */
	UFUNCTION(BlueprintCallable, Category = "战斗反馈|记录",
		meta = (DisplayName = "通知玩家被击杀"))
	void NotifyPlayerKilled();

	/** Clear marker and skull only; feed rows and shared crosshair source are untouched. */
	UFUNCTION(BlueprintCallable, Category = "战斗反馈|中心",
		meta = (DisplayName = "清除中心反馈"))
	void ClearCenterFeedback();

	/** Clear all visual/feed state. Use on retry, main-menu return and EndPlay. */
	UFUNCTION(BlueprintCallable, Category = "战斗反馈|记录",
		meta = (DisplayName = "重置战斗反馈"))
	void ResetFeedback();

	/** Reserve the upper cinematic bar area for the feed. */
	UFUNCTION(BlueprintCallable, Category = "战斗反馈|记录",
		meta = (DisplayName = "设置电影黑边"))
	void SetCinematicBarsVisible(bool bVisible);

	/** Marker arm length in Slate units; increasing gap translates the arm only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "标记臂长度"))
	float MarkerArmLength = 11.0f;

	/** Thickness of the inner (center-facing) end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "标记内端粗细"))
	float MarkerInnerThickness = 1.2f;

	/** Thickness of the outer end. Values below the inner thickness are clamped by geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "标记外端粗细"))
	float MarkerOuterThickness = 3.4f;

	/** Body-hit marker color. Kill state never changes this selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (DisplayName = "身体命中颜色"))
	FLinearColor BodyHitColor = FLinearColor::White;

	/** Head-hit marker and skull color, including non-lethal headshots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (DisplayName = "头部命中颜色"))
	FLinearColor HeadHitColor = FLinearColor(1.0f, 0.12f, 0.12f, 1.0f);

	/** Marker flash duration; the contract default is about 0.12 s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (ClampMin = "0.0", UIMax = "10.0", DisplayName = "命中闪动时长（秒）"))
	float MarkerFlashSeconds = 0.12f;

	/** Independent skull duration for a killing hit; ordinary hits leave it running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (ClampMin = "0.0", UIMax = "10.0", DisplayName = "击杀骷髅时长（秒）"))
	float SkullSeconds = 0.30f;

	/** Procedural skull diameter in Slate units. Kept small so it clears the aim point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (ClampMin = "6.0", UIMax = "32.0", DisplayName = "骷髅大小"))
	float SkullSize = 14.0f;

	/** Extra space between the marker's upper arm and the skull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|中心",
		meta = (ClampMin = "0.0", DisplayName = "骷髅与标记间距"))
	float SkullMarkerGap = 4.0f;

	/** Gap used until a crosshair source is assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|准星",
		meta = (DisplayName = "默认准星间距"))
	FVector2D FallbackCrosshairGap = FVector2D(6.0f, 6.0f);

	/** Feed distance from the top/right viewport edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (ClampMin = "0.0", DisplayName = "记录边距"))
	float FeedPadding = 18.0f;

	/** Feed width in Slate units; automatically clamped to the current viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (ClampMin = "160.0", DisplayName = "记录宽度"))
	float FeedWidth = 320.0f;

	/** Feed row height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (ClampMin = "16.0", DisplayName = "记录行高"))
	float FeedRowHeight = 30.0f;

	/** Space between feed rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (ClampMin = "0.0", DisplayName = "记录行距"))
	float FeedRowSpacing = 4.0f;

	/** Motion time for existing rows after a new row is inserted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (ClampMin = "0.0", UIMax = "10.0", DisplayName = "记录下移动画（秒）"))
	float FeedShiftSeconds = 0.15f;

	/** Engine-default font, editable in the WBP. A null/invalid font falls back locally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (DisplayName = "记录字体"))
	FSlateFontInfo FeedFont;

	/** Feed body text color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (DisplayName = "记录文字颜色"))
	FLinearColor FeedTextColor = FLinearColor::White;

	/** Dark translucent row background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (DisplayName = "记录背景颜色"))
	FLinearColor FeedRowBackground = FLinearColor(0.0f, 0.0f, 0.0f, 0.55f);

	/** Accent for "you killed an enemy" rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (DisplayName = "身体击杀强调色"))
	FLinearColor BodyKillAccent = FLinearColor(0.10f, 0.85f, 1.0f, 1.0f);

	/** Accent for headshot-kill rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (DisplayName = "爆头击杀强调色"))
	FLinearColor HeadshotKillAccent = FLinearColor(1.0f, 0.12f, 0.12f, 1.0f);

	/** Accent for the player-death row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (DisplayName = "玩家死亡强调色"))
	FLinearColor PlayerKilledAccent = FLinearColor(1.0f, 0.12f, 0.12f, 1.0f);

	/**
	 * Fraction of viewport height added to the feed's top inset while the
	 * cinematic-bars flag is true, so rows stay below the upper black bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|记录",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "电影黑边占屏比例"))
	float CinematicInsetFraction = 0.12f;

	/** Local 2D sound for an accepted non-lethal hit. Leave null to stay silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|声音",
		meta = (DisplayName = "命中音效"))
	TObjectPtr<USoundBase> HitSound = nullptr;

	/** Local 2D sound for a killing hit. Killing hits never also play HitSound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗反馈|声音",
		meta = (DisplayName = "击杀音效"))
	TObjectPtr<USoundBase> KillSound = nullptr;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual void NativeDestruct() override;

private:
	FDustCombatFeedbackTuning BuildTuning() const;
	void SyncTuning();
	void RefreshCrosshairGap();
	void InvalidateFeedbackPaint();
	void PlayLocalFeedbackSound(USoundBase* Sound) const;
	FSlateFontInfo GetSafeFeedFont() const;

	/** Stored weakly; no ownership of the crosshair widget and no visibility gate. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UDynamicCrosshairWidget> CrosshairSource;

	FDustCombatFeedbackState Feedback;
	FVector2D LastCrosshairGap = FVector2D(6.0f, 6.0f);
	bool bCinematicBarsVisible = false;
};
