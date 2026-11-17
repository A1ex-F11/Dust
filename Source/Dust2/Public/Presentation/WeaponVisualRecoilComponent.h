#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Presentation/WeaponVisualRecoilData.h"
#include "WeaponVisualRecoilComponent.generated.h"

class AWeaponRuntime;

/** Per-owner visual state. Play is called only for an already committed shot. */
UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent, DisplayName="枪身后坐表现"))
class DUST2_API UWeaponVisualRecoilComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UWeaponVisualRecoilComponent();
	UFUNCTION(BlueprintCallable, Category="武器|枪身后坐")
	bool InitializeVisualRecoil(AWeaponRuntime* Weapon, UWeaponVisualRecoilData* Data);
	UFUNCTION(BlueprintCallable, Category="武器|枪身后坐") bool PlayVisualRecoil();
	UFUNCTION(BlueprintCallable, Category="武器|枪身后坐") void StopVisualRecoil();
	UFUNCTION(BlueprintCallable, Category="武器|枪身后坐") void SetVisualRecoilAiming(bool bAiming);
	UFUNCTION(BlueprintCallable, Category="武器|枪身后坐") void ResetVisualRecoil();
	UPROPERTY(Transient, BlueprintReadOnly, Category="武器|枪身后坐") FTransform RecoilAnimation = FTransform::Identity;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UPROPERTY(Transient) TObjectPtr<UWeaponVisualRecoilData> RecoilData;
	TWeakObjectPtr<AWeaponRuntime> BoundWeapon;
	UPROPERTY(Transient) TObjectPtr<UCurveVector> RotationCurve;
	UPROPERTY(Transient) TObjectPtr<UCurveVector> LocationCurve;
	bool bAiming = false;
	bool bHasShot = false;
	bool bPlaying = false;
	double LastShotTime = 0;
	float Playback = 0;
	float EndTime = 0;
	float ShotInterval = 0.15f;
	FVector TargetRotation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	FVector StartRotation = FVector::ZeroVector;
	FVector StartLocation = FVector::ZeroVector;
	FVector RawRotation = FVector::ZeroVector;
	FVector RawLocation = FVector::ZeroVector;
	FVector SmoothedRotation = FVector::ZeroVector;
	FVector SmoothedLocation = FVector::ZeroVector;
	FVector2D NoiseTarget = FVector2D::ZeroVector;
	FVector2D NoiseCurrent = FVector2D::ZeroVector;
	FVector2D Sway = FVector2D::ZeroVector;
	FVector2D Progress = FVector2D::ZeroVector;
	float PushbackTarget = 0;
	float PushbackCurrent = 0;
	bool CanUpdate() const;
	void UpdateVisual(float DeltaTime);
};
