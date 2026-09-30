#pragma once
#include"CoreMinimal.h"
#include"FireMode.generated.h"
UENUM(BlueprintType)
enum class EFireMode : uint8
{
	SemiAutomatic UMETA(DisplayName = "半自动"),
	FullAutomatic UMETA(DisplayName = "全自动"),
	Burst UMETA(DisplayName = "三连发"),
};
