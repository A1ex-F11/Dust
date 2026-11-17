#pragma once

#include "CoreMinimal.h"
#include "DustEnemyDifficulty.generated.h"

/** Chosen enemy combat difficulty for one run. */
UENUM(BlueprintType)
enum class EDustEnemyDifficulty : uint8
{
	Easy UMETA(DisplayName="简单"),
	Normal UMETA(DisplayName="普通"),
	Hard UMETA(DisplayName="困难")
};
