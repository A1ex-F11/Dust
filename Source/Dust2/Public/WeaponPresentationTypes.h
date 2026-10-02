#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMontage.h"
#include "Sound/SoundBase.h"
#include "WeaponRuntime.h"
#include "WeaponPresentationTypes.generated.h"

USTRUCT(BlueprintType)
struct DUST2_API FWeaponReloadVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "角色换弹蒙太奇"))
	TObjectPtr<UAnimMontage> CharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "枪身换弹蒙太奇"))
	TObjectPtr<UAnimMontage> WeaponMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "换弹整段声音"))
	TObjectPtr<USoundBase> ReloadSound = nullptr;
};

USTRUCT(BlueprintType)
struct DUST2_API FWeaponPresentationProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping",
		meta = (DisplayName = "正式武器类"))
	TSubclassOf<AWeaponRuntime> FormalWeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping",
		meta = (DisplayName = "视觉武器类"))
	TSubclassOf<AActor> VisualWeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation",
		meta = (DisplayName = "启用 Tactical 表现"))
	bool bUseTacticalPresentation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation",
		meta = (DisplayName = "每次装备使用完整拔枪"))
	bool bAlwaysUseFullDraw = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation",
		meta = (DisplayName = "使用完整收枪"))
	bool bUseFullHolster = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "按装弹数量选择的换弹变体"))
	TArray<FWeaponReloadVariant> ReloadVariants;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "换弹通知声音"))
	TMap<FName, TObjectPtr<USoundBase>> ReloadNotifySounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "逐颗装填通知名"))
	FName ReloadRoundCommitNotifyName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire",
		meta = (DisplayName = "枪身最后一发开火蒙太奇"))
	TObjectPtr<UAnimMontage> EmptyFireWeaponMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "枪身普通换弹蒙太奇"))
	TObjectPtr<UAnimMontage> ReloadTacticalMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "枪身空仓换弹蒙太奇"))
	TObjectPtr<UAnimMontage> ReloadEmptyMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draw",
		meta = (DisplayName = "角色拔枪蒙太奇"))
	TObjectPtr<UAnimMontage> DrawCharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draw",
		meta = (DisplayName = "枪身首次拔枪蒙太奇"))
	TObjectPtr<UAnimMontage> InitialDrawWeaponMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holster",
		meta = (DisplayName = "角色快速收枪蒙太奇"))
	TObjectPtr<UAnimMontage> FastHolsterCharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holster",
		meta = (DisplayName = "角色完整收枪蒙太奇"))
	TObjectPtr<UAnimMontage> FullHolsterCharacterMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holster",
		meta = (DisplayName = "枪身完整收枪蒙太奇"))
	TObjectPtr<UAnimMontage> FullHolsterWeaponMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "普通换弹声音"))
	TObjectPtr<USoundBase> ReloadTacticalSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload",
		meta = (DisplayName = "空仓换弹声音"))
	TObjectPtr<USoundBase> ReloadEmptySound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draw",
		meta = (DisplayName = "快速拔枪声音"))
	TObjectPtr<USoundBase> FastDrawSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holster",
		meta = (DisplayName = "快速收枪声音"))
	TObjectPtr<USoundBase> FastHolsterSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draw",
		meta = (DisplayName = "角色快速拔枪起播时间"))
	float FastDrawStartTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holster",
		meta = (DisplayName = "角色快速收枪起播时间"))
	float FastHolsterStartTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holster",
		meta = (DisplayName = "收枪过渡等待时间"))
	float HolsterTransitionDelay = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draw",
		meta = (DisplayName = "角色拔枪阶段切换时间"))
	float FastDrawTransitionTime = 0.0f;
};
