#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DustSciFiObjectiveTextData.generated.h"

UCLASS(BlueprintType, meta=(DisplayName="Dust SciFi Objective Text"))
class DUST2_API UDustSciFiObjectiveTextData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="开场补给"))
	FText Intro;

	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="第 1 区"))
	FText Area1;

	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="第 2 区"))
	FText Area2;

	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="第 3 区"))
	FText Area3;

	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="第 4 区"))
	FText Area4;

	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="第 5 区"))
	FText Area5;

	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="第 5 区清完：返回 A"))
	FText ReturnToA;

	UPROPERTY(EditAnywhere, Category="任务文案", meta=(MultiLine=true, DisplayName="拆除 C4"))
	FText DefuseC4;
};
