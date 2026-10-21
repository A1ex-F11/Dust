#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "RecoilKickEnvelope.h"
#include "CameraRecoilRecoveryComponent.generated.h"

class UWeaponRecoilData;
class UCameraComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="镜头后坐恢复组件"))
class DUST2_API UCameraRecoilRecoveryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCameraRecoilRecoveryComponent();
    // Compatibility entry: immediate kick, original cumulative limits.
    UFUNCTION(BlueprintCallable, Category="武器|后坐力")
    void ApplyRecoilKick(float PitchKick, float YawKick);
    UFUNCTION(BlueprintCallable, Category="武器|后坐力")
    void ApplyRecoilKickWithData(float PitchKick, float YawKick, UWeaponRecoilData* RecoilSettings);
    UFUNCTION(BlueprintCallable, Category="武器|后坐力")
    void ApplyRecoilWithCamera(float PitchKick, float YawKick,
        UWeaponRecoilData* RecoilSettings, UCameraComponent* RecoilCamera);
    UFUNCTION(BlueprintCallable, Category="武器|后坐力")
    void StartRecovery();
    // Manual look discards recovery bookkeeping, but retains accepted shot tails.
    UFUNCTION(BlueprintCallable, Category="武器|后坐力")
    void NotifyManualLook();
    // Terminal cleanup: cancels both recovery and outstanding shot tails.
    UFUNCTION(BlueprintCallable, Category="武器|后坐力")
    void ClearRecoil();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="武器|后坐力", meta=(DisplayName="最大俯仰偏移"))
    float MaxPitchOffset;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="武器|后坐力", meta=(DisplayName="最大偏航偏移"))
    float MaxYawOffset;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="武器|后坐力", meta=(DisplayName="恢复延迟"))
    float RecoveryDelay;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="武器|后坐力", meta=(DisplayName="恢复速度"))
    float RecoverySpeed;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="武器|后坐力", meta=(DisplayName="俯仰恢复比例"))
    float RecoveryPitchRatio;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="武器|后坐力", meta=(DisplayName="恢复间隔"))
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
