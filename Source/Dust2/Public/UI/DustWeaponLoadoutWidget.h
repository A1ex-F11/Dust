#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ObjectKey.h"
#include "UI/DustWeaponLoadoutTransition.h"
#include "DustWeaponLoadoutWidget.generated.h"

class AWeaponRuntime;
class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;

/**
 * Presentation-only two-slot weapon loadout HUD.
 *
 * Slot1/Slot2 are fixed inventory slots. Each slot owns one Image; a normal
 * switch moves the same image between the current (lower) and reserve (upper)
 * poses instead of swapping brushes between fixed positions. The widget never
 * reads inventory, ammo, or fire mode and never binds gameplay delegates.
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="双武器装备HUD"))
class DUST2_API UDustWeaponLoadoutWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Exact-class icon lookup. A missing entry hides the image; no fake weapon is drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|图标",
		meta = (DisplayName = "武器图标"))
	TMap<TSubclassOf<AWeaponRuntime>, TObjectPtr<UTexture2D>> WeaponIcons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|布局",
		meta = (DisplayName = "当前武器偏移"))
	FVector2D CurrentOffset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|布局",
		meta = (DisplayName = "备用武器偏移"))
	FVector2D ReserveOffset = FVector2D(0.0f, -88.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|布局",
		meta = (DisplayName = "当前武器缩放"))
	float CurrentScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|布局",
		meta = (DisplayName = "备用武器缩放"))
	float ReserveScale = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|布局",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "当前武器不透明度"))
	float CurrentOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|布局",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "备用武器不透明度"))
	float ReserveOpacity = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|HUD|布局",
		meta = (ClampMin = "0.0", DisplayName = "过渡时长（秒）",
			ToolTip = "正常切枪、替补淡出淡入及详情淡入的显示时长；0 或负值直接到达目标。"))
	float TransitionSeconds = 0.18f;

	/**
	 * Called by the Blueprint inventory callback. Slot1Weapon/Slot2Weapon are the
	 * fixed inventory slots; CurrentWeapon is compared by identity only.
	 */
	UFUNCTION(BlueprintCallable, Category = "武器|HUD",
		meta = (DisplayName = "刷新武器装备显示"))
	void UpdateWeaponLoadout(
		AWeaponRuntime* Slot1Weapon,
		AWeaponRuntime* Slot2Weapon,
		AWeaponRuntime* CurrentWeapon,
		bool bImmediate = false);

	/** Clear animation and cached display; never touches inventory or weapon state. */
	UFUNCTION(BlueprintCallable, Category = "武器|HUD",
		meta = (DisplayName = "重置武器装备显示"))
	void ResetWeaponLoadout();

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	/** All optional so a partially rebuilt WBP still runs without crashing. */
	UPROPERTY(BlueprintReadOnly, Category = "武器|HUD|绑定", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WeaponSlot1Root;

	UPROPERTY(BlueprintReadOnly, Category = "武器|HUD|绑定", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WeaponSlot2Root;

	UPROPERTY(BlueprintReadOnly, Category = "武器|HUD|绑定", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_WeaponSlot1;

	UPROPERTY(BlueprintReadOnly, Category = "武器|HUD|绑定", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_WeaponSlot2;

	UPROPERTY(BlueprintReadOnly, Category = "武器|HUD|绑定", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_WeaponName;

	UPROPERTY(BlueprintReadOnly, Category = "武器|HUD|绑定", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WeaponDetails;

private:
	struct FWeaponLoadoutVisual
	{
		TWeakObjectPtr<UTexture2D> Icon;
	};

	FWeaponLoadoutDisplaySettings BuildDisplaySettings() const;
	float GetEffectiveTransitionSeconds() const;

	uint64 ResolveBrushToken(AWeaponRuntime* Weapon);
	UWidget* GetSlotRoot(int32 SlotIndex) const;
	UImage* GetSlotImage(int32 SlotIndex) const;
	UWidget* GetTrackTransformTarget(int32 SlotIndex) const;

	void ApplyTrackPresentation(int32 SlotIndex);
	void ApplyTrackBrush(int32 SlotIndex, uint64 BrushId);
	void ApplyCurrentWeaponPresentation(AWeaponRuntime* CurrentWeapon, bool bImmediate);
	void AdvanceDetailsFade(float DeltaSeconds);
	void PruneDisplayCache();

	FWeaponLoadoutDisplayState DisplayState;

	/** Stable display identity per live weapon actor; never dereferenced by the animation. */
	TMap<FObjectKey, uint64> WeaponBrushTokens;
	TMap<uint64, FWeaponLoadoutVisual> BrushVisuals;
	uint64 NextBrushToken = 1;
	uint64 CurrentWeaponBrushToken = 0;

	/** Cached objects currently pushed to each Image, used only to avoid redundant brush resets. */
	uint64 AppliedBrushTokens[2] = { 0, 0 };
	TWeakObjectPtr<UTexture2D> AppliedTextures[2];
	bool bBrushApplied[2] = { false, false };

	bool bInitialized = false;
	bool bDetailsFading = false;
	float DetailsFadeElapsedSeconds = 0.0f;
	float DetailsFadeDurationSeconds = 0.0f;
	float DetailsFadeStartOpacity = 0.0f;
};
