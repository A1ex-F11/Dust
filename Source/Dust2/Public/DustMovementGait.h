#pragma once

#include "CoreMinimal.h"
#include "DustMovementGait.generated.h"

UENUM(BlueprintType)
enum class EDustMovementGait : uint8
{
	Walk UMETA(DisplayName = "Walk"),
	Run UMETA(DisplayName = "Run"),
	SlowWalk UMETA(DisplayName = "SlowWalk")
};