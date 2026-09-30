#pragma once
#include"CoreMinimal.h"
#include"MissionState.generated.h"

UENUM(BlueprintType)
enum class EMissionState : uint8
{
	NotStarted,
    Resupply,
    GoToB,
    ReturnToA,
    C4Planted
};