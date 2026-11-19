#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/WeaponKitTypes.h"
#include "Damage/WeaponKitDamageLibrary.generated.h"

UCLASS()
class WEAPONKIT_API UWeaponKitDamageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Bones listed on a region use that region's multiplier and armor flags.
	 * Any other bone uses multiplier 1 and body-armor mitigation, including a bone named "head"
	 * when the caller did not put that name in a region.
	 */
	UFUNCTION(BlueprintPure, Category = "WeaponKit|Damage")
	static float CalculateFinalDamage(
		float Damage,
		FName HitBoneName,
		float ArmorPenetration,
		const TArray<FWeaponKitDamageRegion>& Regions,
		bool bHasHelmet,
		bool bHasBodyArmor);

	/** One head region plus the implicit body fallback. Pass the skeleton's head bone, not a hardcoded name. */
	UFUNCTION(BlueprintPure, Category = "WeaponKit|Damage")
	static TArray<FWeaponKitDamageRegion> MakeHeadBodyRegions(FName HeadBoneName, float HeadDamageMultiplier);
};
