#include "Presentation/WeaponPresentationData.h"

#include "Weapons/WeaponDefinitionData.h"
#include "Presentation/WeaponViewmodelData.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#endif

namespace
{
	// Keep explicit invalid data distinct from missing data so legacy fallback stays safe.
	EWeaponPresentationResolveResult SelectPlayerProfileForPresentationDefinition(
		const UWeaponDefinitionData* Definition,
		const FWeaponPresentationProfile*& OutProfile)
	{
		OutProfile = nullptr;

		if (!IsValid(Definition))
		{
			return EWeaponPresentationResolveResult::NotConfigured;
		}

		const UWeaponPresentationData* PresentationData =
			Definition->PresentationData.Get();
		if (!IsValid(PresentationData) || !PresentationData->bEnablePlayerProfile)
		{
			return EWeaponPresentationResolveResult::NotConfigured;
		}

		// The explicit asset has one player enable switch.
		const FWeaponPresentationProfile* Profile =
			&PresentationData->PlayerProfile;
		if (!UWeaponPresentationData::IsPlayerProfileUsable(*Profile))
		{
			return EWeaponPresentationResolveResult::Invalid;
		}

		OutProfile = Profile;
		return EWeaponPresentationResolveResult::Resolved;
	}
}

bool UWeaponPresentationData::TryGetPlayerViewmodelSettings(
	const FWeaponPresentationProfile& Profile,
	FWeaponViewmodelSettings& OutSettings)
{
	OutSettings = FWeaponViewmodelSettings{};
	if (!Profile.bUseViewmodelSettings)
	{
		return false;
	}

	// A configured external asset is authoritative, including when its contents are invalid.
	const FWeaponViewmodelSettings* Settings = &Profile.ViewmodelSettings;
	if (Profile.ViewmodelData)
	{
		if (!IsValid(Profile.ViewmodelData.Get())) return false;
		Settings = &Profile.ViewmodelData->Settings;
	}
	if (!Settings->IsUsable()) return false;
	OutSettings = *Settings;
	return true;
}

void UWeaponPresentationData::SplitViewmodelAsset()
{
#if WITH_EDITOR
	if (PlayerProfile.ViewmodelData)
	{
		UE_LOG(LogTemp, Display, TEXT("%s 已关联持枪表现资产，未重复拆分或覆盖。"), *GetName());
		return;
	}
	const FString SourcePackage = GetOutermost()->GetName();
	if (!SourcePackage.StartsWith(TEXT("/Game/")) || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		UE_LOG(LogTemp, Error, TEXT("请在项目内容目录中的武器表现数据资产上执行拆分。"));
		return;
	}
	const FString AssetName = GetName() + TEXT("_Viewmodel");
	const FString PackageName = FPackageName::GetLongPackagePath(SourcePackage) / AssetName;
	if (FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
	{
		UE_LOG(LogTemp, Error, TEXT("拆分取消：%s 已存在，未覆盖。可手动关联已有资产或重命名后重试。"), *PackageName);
		return;
	}
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return;
	UWeaponViewmodelData* Data = NewObject<UWeaponViewmodelData>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!Data) return;
	// Copy even unfinished parameters so the split never discards work in progress.
	Data->Settings = PlayerProfile.ViewmodelSettings;
	Modify();
	PlayerProfile.ViewmodelData = Data;
	FAssetRegistryModule::AssetCreated(Data);
	Data->MarkPackageDirty();
	MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("已创建并关联 %s，原内联值保留。请保存全部。"), *Data->GetPathName());
#endif
}

FWeaponPresentationProfile FEnemyWeaponPresentationProfile::ToPresentationProfile() const
{
	FWeaponPresentationProfile Out;
	Out.VisualWeaponClass = VisualWeaponClass;
	Out.ViewmodelData = ViewmodelData;
	Out.bUseViewmodelSettings = ViewmodelData != nullptr;
	Out.WeaponFireMontage = WeaponFireMontage;
	Out.CharacterFireMontage = CharacterFireMontage;
	Out.CharacterReloadEmptyMontage = CharacterReloadEmptyMontage;
	Out.ReloadEmptyMontage = ReloadEmptyMontage;
	Out.ReloadEmptySound = ReloadEmptySound;
	// Out is the existing player-shaped Blueprint output; its count-reload fields stay at their
	// defaults. The enemy side neither configures nor borrows that player feature.
	return Out;
}

FEnemyWeaponPresentationProfile FEnemyWeaponPresentationProfile::FromPresentationProfile(const FWeaponPresentationProfile& Profile)
{
	FEnemyWeaponPresentationProfile Out;
	Out.VisualWeaponClass = Profile.VisualWeaponClass;
	Out.WeaponFireMontage = Profile.WeaponFireMontage;
	Out.CharacterFireMontage = Profile.CharacterFireMontage;
	Out.CharacterReloadEmptyMontage = Profile.CharacterReloadEmptyMontage;
	Out.ReloadEmptyMontage = Profile.ReloadEmptyMontage;
	Out.ReloadEmptySound = Profile.ReloadEmptySound;
	return Out;
}

bool FEnemyWeaponPresentationProfile::IsDefault() const
{
	return !VisualWeaponClass && !ViewmodelData && !WeaponFireMontage && !CharacterFireMontage
		&& !CharacterReloadEmptyMontage && !ReloadEmptyMontage && !ReloadEmptySound;
}

void UWeaponPresentationData::PostLoad()
{
	Super::PostLoad();
	MigrateLegacyEnemyPresentation();
}

void UWeaponPresentationData::MigrateLegacyEnemyPresentation()
{
	if (EnemyPresentation.IsDefault())
	{
		EnemyPresentation = FEnemyWeaponPresentationProfile::FromPresentationProfile(EnemyProfile);
		// EnemyPresentation keeps only the fixed compact set; FromPresentationProfile
		// deliberately ignores the old player-shaped count switch and variant array.
	}
	// Consume old data so intentionally clearing the new configuration cannot resurrect it later.
	EnemyProfile = FWeaponPresentationProfile{};
}

EWeaponPresentationResolveResult UWeaponPresentationData::ResolvePlayerProfileForDefinition(
	const UWeaponDefinitionData* Definition,
	FWeaponPresentationProfile& OutProfile)
{
	OutProfile = FWeaponPresentationProfile{};

	const FWeaponPresentationProfile* Profile = nullptr;
	const EWeaponPresentationResolveResult Result =
		SelectPlayerProfileForPresentationDefinition(Definition, Profile);
	if (Result == EWeaponPresentationResolveResult::Resolved)
	{
		OutProfile = *Profile;
	}
	return Result;
}

EWeaponPresentationResolveResult UWeaponPresentationData::ResolveEnemyProfileForDefinition(
	const UWeaponDefinitionData* Definition,
	FWeaponPresentationProfile& OutProfile)
{
	OutProfile = FWeaponPresentationProfile{};

	if (!IsValid(Definition))
	{
		return EWeaponPresentationResolveResult::NotConfigured;
	}

	const UWeaponPresentationData* PresentationData =
		Definition->PresentationData.Get();
	if (!IsValid(PresentationData) || !PresentationData->bEnableEnemyProfile)
	{
		return EWeaponPresentationResolveResult::NotConfigured;
	}

	// The visual class is the minimum proof that the enemy side is configured.
	const FWeaponPresentationProfile Enemy = PresentationData->EnemyPresentation.ToPresentationProfile();
	if (!IsEnemyProfileUsable(Enemy))
	{
		return EWeaponPresentationResolveResult::Invalid;
	}

	OutProfile = Enemy;
	return EWeaponPresentationResolveResult::Resolved;
}

bool UWeaponPresentationData::IsPlayerProfileUsable(const FWeaponPresentationProfile& Profile)
{
	FWeaponViewmodelSettings Settings;
	return IsEnemyProfileUsable(Profile)
		&& (!Profile.bUseViewmodelSettings || TryGetPlayerViewmodelSettings(Profile, Settings));
}

bool UWeaponPresentationData::IsEnemyProfileUsable(const FWeaponPresentationProfile& Profile)
{
	return IsValid(Profile.VisualWeaponClass.Get())
		&& !Profile.VisualWeaponClass->HasAnyClassFlags(CLASS_Abstract);
}

const FWeaponPresentationProfile* UWeaponPresentationData::FindPlayerProfileWithLegacy(
	const UWeaponDefinitionData* Definition, const UClass* FormalWeaponClass,
	const TArray<FWeaponPresentationProfile>& LegacyProfiles)
{
	const FWeaponPresentationProfile* Profile = nullptr;
	const EWeaponPresentationResolveResult Result =
		SelectPlayerProfileForPresentationDefinition(Definition, Profile);
	if (Result != EWeaponPresentationResolveResult::NotConfigured)
	{
		// An explicit side is authoritative: valid returns the asset pointer, invalid blocks legacy.
		return Result == EWeaponPresentationResolveResult::Resolved ? Profile : nullptr;
	}

	if (!IsValid(FormalWeaponClass)) return nullptr;
	for (const FWeaponPresentationProfile& LegacyProfile : LegacyProfiles)
	{
		if (LegacyProfile.FormalWeaponClass && FormalWeaponClass->IsChildOf(LegacyProfile.FormalWeaponClass.Get()))
		{
			return &LegacyProfile;
		}
	}
	return nullptr;
}

bool UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
	const UWeaponDefinitionData* Definition, const UClass* FormalWeaponClass,
	const TArray<FWeaponPresentationProfile>& LegacyProfiles, FWeaponPresentationProfile& OutProfile)
{
	OutProfile = FWeaponPresentationProfile{};
	const FWeaponPresentationProfile* Profile = FindPlayerProfileWithLegacy(
		Definition, FormalWeaponClass, LegacyProfiles);
	if (!Profile) return false;
	OutProfile = *Profile;
	return true;
}
