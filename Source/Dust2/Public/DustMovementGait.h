#pragma once

#include "CoreMinimal.h"
#include "DustMovementGait.generated.h"

UENUM(BlueprintType, meta=(DisplayName="移动步态"))
enum class EDustMovementGait : uint8
{
	Walk UMETA(DisplayName = "行走"),
	Run UMETA(DisplayName = "奔跑"),
	SlowWalk UMETA(DisplayName = "慢走")
};