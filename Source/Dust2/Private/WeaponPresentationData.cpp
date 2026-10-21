#include "WeaponPresentationData.h"

#include "WeaponDefinitionData.h"

FWeaponPresentationProfile FEnemyWeaponPresentationProfile::ToPresentationProfile() const
{
	FWeaponPresentationProfile Out;
	Out.VisualWeaponClass = VisualWeaponClass;
	Out.AuthorAnimationSettings = AuthorAnimationSettings;
	Out.WeaponFireMontage = WeaponFireMontage;
	Out.CharacterReloadEmptyMontage = CharacterReloadEmptyMontage;
	Out.ReloadEmptyMontage = ReloadEmptyMontage;
	Out.VisualWeaponAttachSocket = VisualWeaponAttachSocket;
	Out.VisualWeaponRelativeTransform = VisualWeaponRelativeTransform;
	// Out is the existing player-shaped Blueprint output; its count-reload fields stay at their
	// defaults. The enemy side neither configures nor borrows that player feature.
	return Out;
}

FEnemyWeaponPresentationProfile FEnemyWeaponPresentationProfile::FromPresentationProfile(const FWeaponPresentationProfile& Profile)
{
	FEnemyWeaponPresentationProfile Out;
	Out.VisualWeaponClass = Profile.VisualWeaponClass;
	Out.AuthorAnimationSettings = Profile.AuthorAnimationSettings;
	Out.WeaponFireMontage = Profile.WeaponFireMontage;
	Out.CharacterReloadEmptyMontage = Profile.CharacterReloadEmptyMontage;
	Out.ReloadEmptyMontage = Profile.ReloadEmptyMontage;
	Out.VisualWeaponAttachSocket = Profile.VisualWeaponAttachSocket;
	Out.VisualWeaponRelativeTransform = Profile.VisualWeaponRelativeTransform;
	return Out;
}

bool FEnemyWeaponPresentationProfile::IsDefault() const
{
	return !VisualWeaponClass && !AuthorAnimationSettings && !WeaponFireMontage
		&& !CharacterReloadEmptyMontage && !ReloadEmptyMontage
		&& VisualWeaponAttachSocket.IsNone() && VisualWeaponRelativeTransform.Equals(FTransform::Identity, 0.0f);
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
		// EnemyPresentation keeps only the fixed 7-field compact set; FromPresentationProfile
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

	// Explicitly enabled: an unusable entry is a hard failure, never a quiet legacy match.
	if (!IsPlayerProfileUsable(PresentationData->PlayerProfile))
	{
		return EWeaponPresentationResolveResult::Invalid;
	}

	OutProfile = PresentationData->PlayerProfile;
	return EWeaponPresentationResolveResult::Resolved;
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

	// The enemy visual/author-animation setup is Blueprint-owned; the visual class is the
	// minimum proof that this side is configured.
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
	return IsEnemyProfileUsable(Profile)
		&& Profile.bUseTacticalPresentation;
}

bool UWeaponPresentationData::IsEnemyProfileUsable(const FWeaponPresentationProfile& Profile)
{
	return IsValid(Profile.VisualWeaponClass.Get())
		&& !Profile.VisualWeaponClass->HasAnyClassFlags(CLASS_Abstract);
}

bool UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
	const UWeaponDefinitionData* Definition, const UClass* FormalWeaponClass,
	const TArray<FWeaponPresentationProfile>& LegacyProfiles, FWeaponPresentationProfile& OutProfile)
{
	const EWeaponPresentationResolveResult Result = ResolvePlayerProfileForDefinition(Definition, OutProfile);
	if (Result != EWeaponPresentationResolveResult::NotConfigured)
		return Result == EWeaponPresentationResolveResult::Resolved;
	if (!IsValid(FormalWeaponClass)) return false;
	for (const FWeaponPresentationProfile& Profile : LegacyProfiles)
	{
		if (Profile.FormalWeaponClass && FormalWeaponClass->IsChildOf(Profile.FormalWeaponClass.Get()))
		{
			OutProfile = Profile;
			return true;
		}
	}
	return false;
}
