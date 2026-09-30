// Fill out your copyright notice in the Description page of Project Settings.


#include "DamageLibrary.h"

float UDamageLibrary::CalculateFinalDamage(
    float Damage,
    FName HitBoneName,
    float ArmorPenetration,
    float HeadDamageMultiplier,
    bool bHasHelmet,
    bool bHasBodyArmor)
{
    float FinalDamage = FMath::Max(Damage, 0.0f);

    if (HitBoneName == FName(TEXT("head")))
    {
        FinalDamage *= HeadDamageMultiplier;

        if (bHasHelmet)
        {
            FinalDamage *= ArmorPenetration;
        }
    }
    else if (bHasBodyArmor)
    {
        FinalDamage *= ArmorPenetration;
    }

    return FMath::Max(FinalDamage, 0.0f);
}