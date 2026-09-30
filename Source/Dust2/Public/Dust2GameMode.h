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
UCLASS()
class DUST2_API ADust2GameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ADust2GameMode();
	UPROPERTY(BlueprintAssignable,Category="Mission")
	FOnMissionChanged OnMissionChanged;
	UFUNCTION(BlueprintPure,Category="Mission")
	EMissionState GetMissionState() const;
	UFUNCTION(BlueprintCallable,Category="Mission")
	bool TrySetMissionState(EMissionState NewState);
	UFUNCTION(BlueprintCallable,Category="Mission")
	void RestoreMissionState(EMissionState SavedState);
	UFUNCTION(BlueprintCallable,Category="Mission")
	bool TryResolveMissionResult(EMissionResult Result);
protected:
	UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Mission")
	EMissionState MissionState;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Mission")
	bool bResultResolved;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Mission")
	EMissionResult MissionResult;
};
