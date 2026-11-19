#include "Damage/WeaponKitDamageLibrary.h"

namespace
{
const FWeaponKitDamageRegion* FindDamageRegion(const TArray<FWeaponKitDamageRegion>& Regions, FName HitBoneName)
{
	for (const FWeaponKitDamageRegion& Region : Regions)
	{
		if (Region.BoneNames.Contains(HitBoneName))
		{
			return &Region;
		}
	}
	return nullptr;
}
}

float UWeaponKitDamageLibrary::CalculateFinalDamage(
	float Damage,
	FName HitBoneName,
	float ArmorPenetration,
	const TArray<FWeaponKitDamageRegion>& Regions,
	bool bHasHelmet,
	bool bHasBodyArmor)
{
	if (!FMath::IsFinite(Damage))
	{
		return 0.0f;
	}

	float Multiplier = 1.0f;
	bool bMitigatedByHelmet = false;
	bool bMitigatedByBodyArmor = true;
	if (const FWeaponKitDamageRegion* Region = FindDamageRegion(Regions, HitBoneName))
	{
		Multiplier = FMath::IsFinite(Region->DamageMultiplier) ? FMath::Max(0.0f, Region->DamageMultiplier) : 0.0f;
		bMitigatedByHelmet = Region->bMitigatedByHelmet;
		bMitigatedByBodyArmor = Region->bMitigatedByBodyArmor;
	}

	float FinalDamage = FMath::Max(Damage, 0.0f) * Multiplier;
	const bool bHelmetApplies = bMitigatedByHelmet && bHasHelmet;
	const bool bBodyArmorApplies = bMitigatedByBodyArmor && bHasBodyArmor && !bHelmetApplies;
	if (bHelmetApplies || bBodyArmorApplies)
	{
		const float Penetration = FMath::IsFinite(ArmorPenetration) ? ArmorPenetration : 0.0f;
		FinalDamage *= Penetration;
	}
	return FMath::Max(FinalDamage, 0.0f);
}

TArray<FWeaponKitDamageRegion> UWeaponKitDamageLibrary::MakeHeadBodyRegions(
	FName HeadBoneName,
	float HeadDamageMultiplier)
{
	FWeaponKitDamageRegion Head;
	Head.RegionId = TEXT("Head");
	if (!HeadBoneName.IsNone())
	{
		Head.BoneNames.Add(HeadBoneName);
	}
	Head.DamageMultiplier = FMath::IsFinite(HeadDamageMultiplier) ? FMath::Max(0.0f, HeadDamageMultiplier) : 0.0f;
	Head.bMitigatedByHelmet = true;
	Head.bMitigatedByBodyArmor = false;

	TArray<FWeaponKitDamageRegion> Regions;
	Regions.Add(Head);
	return Regions;
}
