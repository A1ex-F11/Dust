#pragma once

#include "CoreMinimal.h"
#include "DustMissionRetryAction.generated.h"

/** Blueprint-facing retry branch after a resolved mission result. */
UENUM(BlueprintType)
enum class EDustMissionRetryAction : uint8
{
	Unavailable UMETA(DisplayName="不可用"),
	ChooseDifficulty UMETA(DisplayName="选择难度"),
	RestoreCheckpoint UMETA(DisplayName="恢复检查点"),
	RestartFromBeginning UMETA(DisplayName="从头开始")
};
