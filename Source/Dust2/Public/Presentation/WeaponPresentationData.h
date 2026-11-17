#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Presentation/WeaponPresentationTypes.h"
#include "WeaponPresentationData.generated.h"

class USoundBase;
class UWeaponDefinitionData;

/**
 * W09: why a side-specific profile was or was not selected.
 * Not a Blueprint type: callers must keep "not configured" (legacy fallback) and
 * "explicitly enabled but invalid" (hard failure) distinct.
 */
enum class EWeaponPresentationResolveResult : uint8
{
	/** No PresentationData asset, or this side's explicit switch is off: caller keeps the old path. */
	NotConfigured,
	/** Explicitly enabled and valid: use OutProfile. */
	Resolved,
	/** Explicitly enabled but unusable: callers must fail, never match another profile. */
	Invalid
};

/** Enemy initialization uses only these resources; it never borrows player inventory/draw variants. */
USTRUCT(BlueprintType, meta=(DisplayName="敌人武器表现配置"))
struct DUST2_API FEnemyWeaponPresentationProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="视觉武器类"))
	TSubclassOf<AActor> VisualWeaponClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人",
		meta=(DisplayName="持枪表现资产", ToolTip="供自有动画读取，选择该枪的Viewmodel资产。"))
	TObjectPtr<UWeaponViewmodelData> ViewmodelData = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="枪身开火蒙太奇"))
	TObjectPtr<UAnimMontage> WeaponFireMontage = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="人物开火蒙太奇"))
	TObjectPtr<UAnimMontage> CharacterFireMontage = nullptr;

	/** Always visible: every supported enemy gun uses this one fixed empty-reload pair plus an optional sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="人物空仓换弹蒙太奇"))
	TObjectPtr<UAnimMontage> CharacterReloadEmptyMontage = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="枪身空仓换弹蒙太奇"))
	TObjectPtr<UAnimMontage> ReloadEmptyMontage = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="敌人空仓换弹声音"))
	TObjectPtr<USoundBase> ReloadEmptySound = nullptr;

	/** Convert at the existing Blueprint getter boundary; its output pins remain unchanged. */
	FWeaponPresentationProfile ToPresentationProfile() const;
	static FEnemyWeaponPresentationProfile FromPresentationProfile(const FWeaponPresentationProfile& Profile);
	bool IsDefault() const;
};

/**
 * W09: one optional presentation asset per weapon definition.
 * It only stores presentation data; it never owns gameplay values and has no reverse
 * reference to UWeaponDefinitionData. Both sides default to disabled so an empty asset
 * cannot silently take over the existing compatibility chain.
 */
UCLASS(BlueprintType, meta = (DisplayName = "统一武器表现配置"))
class DUST2_API UWeaponPresentationData : public UDataAsset
{
	GENERATED_BODY()
public:
	virtual void PostLoad() override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|表现|玩家",
		meta = (DisplayName = "启用玩家表现配置",
			ToolTip = "关闭时玩家继续按表现组件里的旧 FormalWeaponClass 数组查找；打开后必须配置有效 PlayerProfile，无效会明确失败而不是回落到别的枪。"))
	bool bEnablePlayerProfile = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|表现|玩家",
		meta = (DisplayName = "玩家武器表现", EditCondition = "bEnablePlayerProfile", EditConditionHides))
	FWeaponPresentationProfile PlayerProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|表现|敌人",
		meta = (DisplayName = "启用敌人表现配置",
			ToolTip = "打开后按所选Definition读取下方精简配置，不按Runtime类猜测。"))
	bool bEnableEnemyProfile = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|表现|敌人",
		meta = (DisplayName = "敌人武器表现", EditCondition = "bEnableEnemyProfile", EditConditionHides))
	FEnemyWeaponPresentationProfile EnemyPresentation;

	/** Old property name AND type retained only to load assets created before the compact editor. */
	UPROPERTY()
	FWeaponPresentationProfile EnemyProfile;

	/** PostLoad migration; also callable by native tests. Never overwrites an already configured compact entry. */
	void MigrateLegacyEnemyPresentation();

	/** Copies preserved inline values once; never saves or overwrites an existing asset. */
	UFUNCTION(CallInEditor, Category="武器|持枪表现",
		meta=(DisplayName="拆分持枪表现资产", ToolTip="在当前资产同目录创建 名称_Viewmodel，复制已有持枪参数并关联。完成后保存全部；已有引用或同名资产时不覆盖。"))
	void SplitViewmodelAsset();

	/** Reads a player or enemy resolved profile; retains the original function name for existing Blueprint nodes. */
	UFUNCTION(BlueprintPure, Category="武器|持枪表现",
		meta=(DisplayName="读取自有持枪配置", ToolTip="输入玩家或敌人已解析的表现配置，成功时返回自有数据快照；失败时清空输出，不读取或创建作者设置。"))
	static bool TryGetPlayerViewmodelSettings(
		const FWeaponPresentationProfile& Profile,
		FWeaponViewmodelSettings& OutSettings);

	/** Read-only snapshot resolver used by the player component and automation. */
	static EWeaponPresentationResolveResult ResolvePlayerProfileForDefinition(
		const UWeaponDefinitionData* Definition,
		FWeaponPresentationProfile& OutProfile);

	/** Read-only snapshot resolver used by the enemy component and automation. */
	static EWeaponPresentationResolveResult ResolveEnemyProfileForDefinition(
		const UWeaponDefinitionData* Definition,
		FWeaponPresentationProfile& OutProfile);

	/** Player side requires a valid visual class and valid viewmodel settings when enabled. */
	static bool IsPlayerProfileUsable(const FWeaponPresentationProfile& Profile);

	/** Enemy side requires a valid visual class; animation resources are profile-owned. */
	static bool IsEnemyProfileUsable(const FWeaponPresentationProfile& Profile);

	/** Used by the existing player lookup. Explicit invalid data blocks legacy fallback. */
	static bool ResolvePlayerProfileWithLegacy(
		const UWeaponDefinitionData* Definition, const UClass* FormalWeaponClass,
		const TArray<FWeaponPresentationProfile>& LegacyProfiles, FWeaponPresentationProfile& OutProfile);

	/** Aliases the selected asset/legacy entry; consume before callbacks and never retain it. */
	static const FWeaponPresentationProfile* FindPlayerProfileWithLegacy(
		const UWeaponDefinitionData* Definition, const UClass* FormalWeaponClass,
		const TArray<FWeaponPresentationProfile>& LegacyProfiles);
};
