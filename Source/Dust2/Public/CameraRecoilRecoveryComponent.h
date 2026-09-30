// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "CameraRecoilRecoveryComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class DUST2_API UCameraRecoilRecoveryComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UCameraRecoilRecoveryComponent();
	UFUNCTION(BlueprintCallable,Category="Recoil")
	void ApplyRecoilKick(float PitchKick, float YawKick);
	UFUNCTION(BlueprintCallable,Category="Recoil")
	void StartRecovery();
	UFUNCTION(BlueprintCallable,Category="Recoil")
	void ClearRecoil();

protected:
UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Recoil")
float MaxPitchOffset;
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
float MaxYawOffset;
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
float RecoveryDelay;
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
float RecoverySpeed;
UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Recoil")
float RecoveryPitchRatio;
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
float RecoveryInterval;
virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	float AccumulatedPitch;
	float AccumulatedYaw;
	float LastKickTime;
	FTimerHandle RecoveryTimerHandle;
	bool bRecoverImmediately;
	bool bRecoveryStarted;
	void StartRecoveryTimer();
	void StopRecoveryTimer();
	void RecoveryStep();
};
