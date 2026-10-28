#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Math/RecoilKickEnvelope.h"
#include "TimerManager.h"
#include "Recoil/WeaponKitCameraRecoilComponent.generated.h"

class UCameraComponent;
class UWeaponKitRecoilData;

/**
 * Applies a weapon kick either to the pawn's control rotation or as a camera-only offset.
 * Aim state is a public call. The owning character decides when the player is aiming.
 */
UCLASS(ClassGroup = (WeaponKit), meta = (BlueprintSpawnableComponent))
class WEAPONKIT_API UWeaponKitCameraRecoilComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponKitCameraRecoilComponent();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Recoil")
	void SetAiming(bool bAiming);

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Recoil")
	bool IsAiming() const { return bAiming; }

	/**
	 * One accepted kick.
	 * Aimed fire writes control rotation, scaled by AimingRecoilScale, so the next shot samples the same view.
	 * Hip fire with independent shot recoil only offsets the camera.
	 * Other hip fire writes controller input, and random patterns stay inside MaxPitchOffset / MaxYawOffset.
	 */
	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Recoil")
	void ApplyRecoil(float PitchKick, float YawKick, UWeaponKitRecoilData* RecoilSettings, UCameraComponent* RecoilCamera);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Recoil")
	void StartRecovery();

	/** Manual look drops recovery bookkeeping and keeps any kick tail that was already accepted. */
	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Recoil")
	void NotifyManualLook();

	/** Cancels recovery and any outstanding kick tail. */
	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Recoil")
	void ClearRecoil();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	float MaxPitchOffset = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	float MaxYawOffset = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	float RecoveryDelay = 0.20f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	float RecoverySpeed = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	float RecoveryPitchRatio = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	float RecoveryInterval = 0.02f;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void QueueKick(float PitchKick, float YawKick, float Duration, float ImmediateFraction, bool bUseCumulativeLimits);
	void ApplyDelta(const FVector2D& Delta, bool bRecoverable);
	void StartRecoveryTimer();
	void StopRecoveryTimer();
	void RecoveryStep();
	void WriteVisualOffset();
	void ClearIndependentVisual();

	bool bAiming = false;
	bool bIndependentVisualMode = false;
	TWeakObjectPtr<UCameraComponent> VisualCamera;
	FVector2D VisualOffset = FVector2D::ZeroVector;
	FTransform AppliedVisualTransform = FTransform::Identity;
	TArray<FWeaponKitRecoilKickEnvelope> VisualKicks;
	float VisualReturnSpeed = 12.0f;

	float AccumulatedPitch = 0.0f;
	float AccumulatedYaw = 0.0f;
	float LastKickTime = 0.0f;
	float ActiveRecoveryDelay = 0.2f;
	float ActiveRecoverySpeed = 1.0f;
	float ActiveRecoveryRatio = 0.35f;
	FTimerHandle RecoveryTimerHandle;
	bool bRecoverImmediately = false;
	bool bRecoveryStarted = false;
	TArray<FWeaponKitRecoilKickEnvelope> PendingKicks;
};
