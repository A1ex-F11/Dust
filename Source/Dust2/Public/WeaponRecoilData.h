// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponRecoilData.generated.h"

UCLASS(BlueprintType)
class DUST2_API UWeaponRecoilData : public UDataAsset
{
	GENERATED_BODY()
public:
	UWeaponRecoilData();
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recoil")
	float VerticalKickMin;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float VerticalKickMax;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float HorizontalKickMax;
	
};
