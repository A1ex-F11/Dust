#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMontage.h"
#include "Sound/SoundBase.h"
#include "WeaponRuntime.h"
#include "WeaponPresentationTypes.generated.h"

USTRUCT(BlueprintType, meta=(DisplayName="换弹动画变体"))
struct DUST2_API FWeaponReloadVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "角色换弹蒙太奇"))
	TObjectPtr<UAnimMontage> CharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "枪身换弹蒙太奇"))
	TObjectPtr<UAnimMontage> WeaponMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "换弹整段声音"))
	TObjectPtr<USoundBase> ReloadSound = nullptr;
};

USTRUCT(BlueprintType, meta=(DisplayName="武器表现配置"))
struct DUST2_API FWeaponPresentationProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "输入|映射",
		meta = (DisplayName = "正式武器类"))
	TSubclassOf<AWeaponRuntime> FormalWeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "输入|映射",
		meta = (DisplayName = "视觉武器类"))
	TSubclassOf<AActor> VisualWeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|表现",
		meta = (DisplayName = "启用 Tactical 表现"))
	bool bUseTacticalPresentation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|表现",
		meta = (DisplayName = "每次装备使用完整拔枪"))
	bool bAlwaysUseFullDraw = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|表现",
		meta = (DisplayName = "使用完整收枪"))
	bool bUseFullHolster = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "按补弹数量选择换弹动画（R08式）", ToolTip="默认关闭。开启后按实际补入数量选择下方变体，索引0对应1发；这只选择表现，不修改枪械装填规则。"))
	bool bUseCountBasedReload = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "补弹数量换弹动画（1发、2发……）", EditCondition="bUseCountBasedReload", EditConditionHides))
	TArray<FWeaponReloadVariant> ReloadVariants;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "换弹通知声音"))
	TMap<FName, TObjectPtr<USoundBase>> ReloadNotifySounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "逐颗装填通知名"))
	FName ReloadRoundCommitNotifyName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|射击",
		meta = (DisplayName = "枪身最后一发开火蒙太奇"))
	TObjectPtr<UAnimMontage> EmptyFireWeaponMontage = nullptr;
	// W06: failed empty trigger audio, independent of the last successful shot montage.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|射击", meta = (DisplayName = "空仓扣扳机提示声"))
	TObjectPtr<USoundBase> DryFireSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|射击", meta = (DisplayName = "空仓提示最小间隔", ClampMin = "0.0", Units = "s"))
	float DryFireMinInterval = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "枪身普通换弹蒙太奇", EditCondition="!bUseCountBasedReload", EditConditionHides))
	TObjectPtr<UAnimMontage> ReloadTacticalMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "枪身空仓换弹蒙太奇", EditCondition="!bUseCountBasedReload", EditConditionHides))
	TObjectPtr<UAnimMontage> ReloadEmptyMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拔枪",
		meta = (DisplayName = "角色拔枪蒙太奇"))
	TObjectPtr<UAnimMontage> DrawCharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拔枪",
		meta = (DisplayName = "枪身首次拔枪蒙太奇"))
	TObjectPtr<UAnimMontage> InitialDrawWeaponMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|收枪",
		meta = (DisplayName = "角色快速收枪蒙太奇"))
	TObjectPtr<UAnimMontage> FastHolsterCharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|收枪",
		meta = (DisplayName = "角色完整收枪蒙太奇"))
	TObjectPtr<UAnimMontage> FullHolsterCharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|收枪",
		meta = (DisplayName = "枪身完整收枪蒙太奇"))
	TObjectPtr<UAnimMontage> FullHolsterWeaponMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "普通换弹声音", EditCondition="!bUseCountBasedReload", EditConditionHides))
	TObjectPtr<USoundBase> ReloadTacticalSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "空仓换弹声音", EditCondition="!bUseCountBasedReload", EditConditionHides))
	TObjectPtr<USoundBase> ReloadEmptySound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拔枪",
		meta = (DisplayName = "快速拔枪声音"))
	TObjectPtr<USoundBase> FastDrawSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|收枪",
		meta = (DisplayName = "快速收枪声音"))
	TObjectPtr<USoundBase> FastHolsterSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拔枪",
		meta = (DisplayName = "角色快速拔枪起播时间"))
	float FastDrawStartTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|收枪",
		meta = (DisplayName = "角色快速收枪起播时间"))
	float FastHolsterStartTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|收枪",
		meta = (DisplayName = "收枪过渡等待时间"))
	float HolsterTransitionDelay = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|拔枪",
		meta = (DisplayName = "角色拔枪阶段切换时间"))
	float FastDrawTransitionTime = 0.0f;

	// W09: optional profile-owned actions. Empty values keep the existing Blueprint parameters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|射击",
		meta = (DisplayName = "人物开火蒙太奇"))
	TObjectPtr<UAnimMontage> CharacterFireMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|射击",
		meta = (DisplayName = "枪身普通开火蒙太奇"))
	TObjectPtr<UAnimMontage> WeaponFireMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "人物普通换弹蒙太奇", EditCondition="!bUseCountBasedReload", EditConditionHides))
	TObjectPtr<UAnimMontage> CharacterReloadTacticalMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|换弹",
		meta = (DisplayName = "人物空仓换弹蒙太奇", EditCondition="!bUseCountBasedReload", EditConditionHides))
	TObjectPtr<UAnimMontage> CharacterReloadEmptyMontage = nullptr;

	// W09: DataAsset value only. The one-time Blueprint bridge casts it to the author animation
	// settings type and assigns ActiveSettings; C++ never reads, casts or reflects it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|表现",
		meta = (DisplayName = "作者动画设置（蓝图桥Cast验证）",
			ToolTip = "仅由蓝图桥读取，Cast 成作者动画实际类型后赋给 ActiveSettings；C++ 不读取、不反射，写入本字段不会自动生效。"))
	TObjectPtr<UObject> AuthorAnimationSettings = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|表现",
		meta = (DisplayName = "视觉武器挂接插槽",
			ToolTip = "仅由蓝图桥在视觉武器初始化时读取；C++ 不自动挂接。"))
	FName VisualWeaponAttachSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "武器|表现",
		meta = (DisplayName = "视觉武器相对变换",
			ToolTip = "仅由蓝图桥在视觉武器初始化时读取；C++ 不自动应用。"))
	FTransform VisualWeaponRelativeTransform = FTransform::Identity;

	/** Register the package version, then use ordinary tagged property serialization. */
	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FWeaponPresentationProfile> : TStructOpsTypeTraitsBase2<FWeaponPresentationProfile>
{
	enum { WithSerializer = true, WithPostSerialize = true };
};
