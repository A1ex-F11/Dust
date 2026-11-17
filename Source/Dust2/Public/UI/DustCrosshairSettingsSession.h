#pragma once

#include "CoreMinimal.h"
#include "UI/CrosshairStyleData.h"
#include "GameFramework/SaveGame.h"
#include "UObject/Object.h"
#include "DustCrosshairSettingsSession.generated.h"

class UDynamicCrosshairWidget;

/** Local preferences only; never part of the gameplay checkpoint. */
UCLASS()
class DUST2_API UDustCrosshairPreferences : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 Version = 1;
	UPROPERTY(SaveGame)
	bool bUseOverride = false;
	UPROPERTY(SaveGame)
	FCrosshairStyleSettings Style;
};

/** Menu-owned draft. The menu calls CancelEditing when closed without applying. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="准星设置编辑会话"))
class DUST2_API UDustCrosshairSettingsSession : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="准星|设置", meta=(DisplayName="开始编辑准星设置"))
	bool BeginEditing(UDynamicCrosshairWidget* LiveCrosshair, UDynamicCrosshairWidget* PreviewCrosshair);

	UFUNCTION(BlueprintCallable, Category="准星|设置", meta=(DisplayName="预览准星设置"))
	bool SetDraftStyle(FCrosshairStyleSettings Style);

	UFUNCTION(BlueprintPure, Category="准星|设置", meta=(DisplayName="获取待应用准星设置"))
	FCrosshairStyleSettings GetDraftStyle() const { return DraftStyle; }

	UFUNCTION(BlueprintPure, Category="准星|设置", meta=(DisplayName="正在编辑准星设置"))
	bool IsEditing() const { return bEditing; }

	UFUNCTION(BlueprintCallable, Category="准星|设置", meta=(DisplayName="预览恢复默认准星"))
	void RestoreDefaults();

	/** A display-only extra gap; never fires a shot or writes weapon spread. */
	UFUNCTION(BlueprintCallable, Category="准星|设置", meta=(DisplayName="设置预览扩张量"))
	void SetPreviewExpansion(float ExtraGap);

	/** False leaves the draft open, preserving the existing preferences on validation failure. */
	UFUNCTION(BlueprintCallable, Category="准星|设置", meta=(DisplayName="应用并保存准星设置"))
	bool ApplyAndSave();

	UFUNCTION(BlueprintCallable, Category="准星|设置", meta=(DisplayName="取消准星设置编辑"))
	void CancelEditing();

	/** Call once for each newly created gameplay crosshair, including after a retry. */
	UFUNCTION(BlueprintCallable, Category="准星|设置", meta=(DisplayName="加载本地准星设置"))
	static bool LoadSavedStyle(UDynamicCrosshairWidget* LiveCrosshair);

private:
	void PushDraft();
	void ReleaseWidgets();
	static FCrosshairStyleSettings ReadDefaults(const UDynamicCrosshairWidget* Widget);
	TWeakObjectPtr<UDynamicCrosshairWidget> Live;
	TWeakObjectPtr<UDynamicCrosshairWidget> Preview;
	FCrosshairStyleSettings OriginalOverride;
	FCrosshairStyleSettings DefaultStyle;
	FCrosshairStyleSettings DraftStyle;
	bool bOriginalOverride = false;
	bool bDraftOverride = false;
	bool bEditing = false;
	float PreviewExpansion = 0.0f;
};
