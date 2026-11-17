#pragma once

#include "CoreMinimal.h"
#include "DustTutorialMessage.generated.h"

USTRUCT(BlueprintType)
struct FDustTutorialMessage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tutorial")
	FName Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tutorial")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tutorial", meta=(MultiLine=true))
	FText Body;
};
