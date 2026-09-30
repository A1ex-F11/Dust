// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FireMode.h"
#include "WeaponBallisticsData.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class UWeaponBallisticsData :public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float DamagePerShot = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float ArmorPenetration = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float HeadDamageMultiplier = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float TraceDistance = 0.0f;
};
