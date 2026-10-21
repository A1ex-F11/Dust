#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponPresentationTypes.h"
#include "WeaponPresentationData.generated.h"

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="作者动画设置"))
	TObjectPtr<UObject> AuthorAnimationSettings = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="枪身开火蒙太奇"))
	TObjectPtr<UAnimMontage> WeaponFireMontage = nullptr;

	/** Always visible: every supported enemy gun plays this one fixed empty-reload pair. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="人物空仓换弹蒙太奇"))
	TObjectPtr<UAnimMontage> CharacterReloadEmptyMontage = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="枪身空仓换弹蒙太奇"))
	TObjectPtr<UAnimMontage> ReloadEmptyMontage = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="视觉武器挂接插槽"))
	FName VisualWeaponAttachSocket = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="武器|表现|敌人", meta=(DisplayName="视觉武器相对变换"))
	FTransform VisualWeaponRelativeTransform = FTransform::Identity;

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

	/** Read-only, allocation-free resolver used by the player component and automation. */
	static EWeaponPresentationResolveResult ResolvePlayerProfileForDefinition(
		const UWeaponDefinitionData* Definition,
		FWeaponPresentationProfile& OutProfile);

	/** Read-only, allocation-free resolver used by the enemy component and automation. */
	static EWeaponPresentationResolveResult ResolveEnemyProfileForDefinition(
		const UWeaponDefinitionData* Definition,
		FWeaponPresentationProfile& OutProfile);

	/** Player side requires both a visual weapon class and the existing Tactical switch. */
	static bool IsPlayerProfileUsable(const FWeaponPresentationProfile& Profile);

	/** Enemy side only requires the visual weapon class; its animation bridge is Blueprint-owned. */
	static bool IsEnemyProfileUsable(const FWeaponPresentationProfile& Profile);

	/** Used by the existing player lookup. Explicit invalid data blocks legacy fallback. */
	static bool ResolvePlayerProfileWithLegacy(
		const UWeaponDefinitionData* Definition, const UClass* FormalWeaponClass,
		const TArray<FWeaponPresentationProfile>& LegacyProfiles, FWeaponPresentationProfile& OutProfile);
};
