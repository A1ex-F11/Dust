#pragma once
#include"CoreMinimal.h"
#include"GameFramework/GameModeBase.h"
#include"MissionState.h"
#include"MissionResult.h"
#include"Dust2GameMode.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMissionChanged,
	EMissionState,
	NewState
);
UCLASS(meta=(DisplayName="Dust2游戏模式"))
class DUST2_API ADust2GameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ADust2GameMode();
	UPROPERTY(BlueprintAssignable,Category="任务")
	FOnMissionChanged OnMissionChanged;
	UFUNCTION(BlueprintPure,Category="任务")
	EMissionState GetMissionState() const;
	UFUNCTION(BlueprintCallable,Category="任务")
	bool TrySetMissionState(EMissionState NewState);
	UFUNCTION(BlueprintCallable,Category="任务")
	void RestoreMissionState(EMissionState SavedState);
	UFUNCTION(BlueprintCallable,Category="任务")
	bool TryResolveMissionResult(EMissionResult Result);
protected:
	UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="任务")
	EMissionState MissionState;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "任务")
	bool bResultResolved;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "任务")
	EMissionResult MissionResult;
};
