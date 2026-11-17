#pragma once
#include"CoreMinimal.h"
#include"MissionResult.generated.h"
 UENUM(BlueprintType)
	 enum class EMissionResult:uint8
 {
	 Defused,
	 Exploded,
	 PlayerDied
 };