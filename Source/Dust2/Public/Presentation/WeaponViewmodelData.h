#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Presentation/WeaponViewmodelTypes.h"
#include "WeaponViewmodelData.generated.h"

/** Shared authored values only; each presentation component keeps its own sway state. */
UCLASS(BlueprintType, meta=(DisplayName="持枪姿势与摆动配置"))
class DUST2_API UWeaponViewmodelData : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="武器|持枪表现",
		meta=(DisplayName="持枪姿势与摆动"))
	FWeaponViewmodelSettings Settings;
};

