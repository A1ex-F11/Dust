#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DustEnemyLootProfile.generated.h"

UCLASS(BlueprintType, meta=(DisplayName="Dust Enemy Loot Profile"))
class DUST2_API UDustEnemyLootProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Loot", meta=(ClampMin="0.0", ClampMax="100.0"))
	float WeaponDropChance = 50.0f;

	UPROPERTY(EditAnywhere, Category="Loot", meta=(ClampMin="0.0", ClampMax="100.0"))
	float HealthDropChance = 25.0f;

	UPROPERTY(EditAnywhere, Category="Loot", meta=(ClampMin="1"))
	int32 MinimumTotalAmmo = 5;

	UPROPERTY(EditAnywhere, Category="Loot", meta=(ClampMin="1"))
	int32 MaximumTotalAmmo = 15;

	UPROPERTY(EditAnywhere, Category="Loot")
	TSubclassOf<AActor> HealthPickupClass;
};
