#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "RecoilKickEnvelope.h"
#include "CameraRecoilRecoveryComponent.generated.h"

class UWeaponRecoilData;
class UCameraComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class DUST2_API UCameraRecoilRecoveryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCameraRecoilRecoveryComponent();
    // Compatibility entry: immediate kick, original cumulative limits.
    UFUNCTION(BlueprintCallable, Category="Recoil")
    void ApplyRecoilKick(float PitchKick, float YawKick);
    UFUNCTION(BlueprintCallable, Category="Recoil")
    void ApplyRecoilKickWithData(float PitchKick, float YawKick, UWeaponRecoilData* RecoilSettings);
    UFUNCTION(BlueprintCallable, Category="Recoil")
    void ApplyRecoilWithCamera(float PitchKick, float YawKick,
        UWeaponRecoilData* RecoilSettings, UCameraComponent* RecoilCamera);
    UFUNCTION(BlueprintCallable, Category="Recoil")
    void StartRecovery();
    // Manual look discards recovery bookkeeping, but retains accepted shot tails.
    UFUNCTION(BlueprintCallable, Category="Recoil")
    void NotifyManualLook();
    // Terminal cleanup: cancels both recovery and outstanding shot tails.
    UFUNCTION(BlueprintCallable, Category="Recoil")
    void ClearRecoil();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil")
    float MaxPitchOffset;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil")
    float MaxYawOffset;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil")
    float RecoveryDelay;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil")
    float RecoverySpeed;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil")
    float RecoveryPitchRatio;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil")
    float RecoveryInterval;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    friend class ADust2PlayerCharacter;
    // Native player aim authority chooses the writer; Blueprints cannot set a second aim state.
    void SetAimingRecoilMode(bool bAiming);
    bool bAimingRecoilMode = false;
    bool bIndependentVisualMode = false;
    TWeakObjectPtr<UCameraComponent> VisualCamera;
    FVector2D VisualOffset = FVector2D::ZeroVector;
    FTransform AppliedVisualTransform = FTransform::Identity;
    TArray<FRecoilKickEnvelope> VisualKicks;
    float VisualReturnSpeed = 12.0f;
    void WriteVisualOffset();
    void ClearIndependentVisual();
    float AccumulatedPitch = 0.0f;
    float AccumulatedYaw = 0.0f;
    float LastKickTime = 0.0f;
    float ActiveRecoveryDelay = 0.2f;
    float ActiveRecoverySpeed = 1.0f;
    float ActiveRecoveryRatio = 0.35f;
    FTimerHandle RecoveryTimerHandle;
    bool bRecoverImmediately = false;
    bool bRecoveryStarted = false;
    TArray<FRecoilKickEnvelope> PendingKicks;
    void QueueKick(float PitchKick, float YawKick, float Duration,
        float ImmediateFraction, bool bUseLegacyLimits);
    void ApplyDelta(const FVector2D& Delta, bool bRecoverable);
    void StartRecoveryTimer();
    void StopRecoveryTimer();
    void RecoveryStep();
};
