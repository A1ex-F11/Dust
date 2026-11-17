// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DamageLibrary.generated.h"

/**
 * 
 */
UCLASS(meta=(DisplayName="伤害计算库"))
class UDamageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	public:
		UFUNCTION(BlueprintPure, Category = "伤害")
		static float CalculateFinalDamage(
			float Damage,
			FName HitBoneName,
			float ArmorPenetration,
			float HeadDamageMultiplier,
			bool bHasHelmet,
			bool bHasBodyArmor
		);
};
