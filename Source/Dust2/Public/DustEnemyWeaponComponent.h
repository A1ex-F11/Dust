#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustEnemyWeaponComponent.generated.h"

class AWeaponRuntime;
class UWeaponDefinitionData;
class UDustEnemyCombatProfile;
class USceneComponent;

// This is the only damage bridge: Blueprint checks Hit.Actor's BPI_Damageable and sends ReceiveDamage once.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams(FDustEnemyShotResolved,
	FVector, Origin, FVector, End, const FHitResult&, Hit, float, Damage,
	float, ArmorPenetration, float, HeadDamageMultiplier, AWeaponRuntime*, Weapon);

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent, DisplayName="敌人武器与压枪"))
class DUST2_API UDustEnemyWeaponComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDustEnemyWeaponComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|配置", meta=(DisplayName="所持枪械配置"))
	TObjectPtr<UWeaponDefinitionData> WeaponDefinition;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|配置", meta=(DisplayName="战斗难度配置"))
	TObjectPtr<UDustEnemyCombatProfile> CombatProfile;
	UPROPERTY(BlueprintAssignable, Category="AI|射击") FDustEnemyShotResolved OnShotResolved;
	UFUNCTION(BlueprintCallable, Category="AI|武器") bool InitializeWeapon();
	UFUNCTION(BlueprintCallable, Category="AI|武器") void SetMuzzleSource(USceneComponent* Component, FName SocketName);
	UFUNCTION(BlueprintCallable, Category="AI|战斗") bool StartEngagement(AActor* Target);
	UFUNCTION(BlueprintCallable, Category="AI|战斗") void StopEngagement();
	UFUNCTION(BlueprintCallable, Category="AI|战斗") void SetCombatEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category="AI|战斗") bool IsEngaging() const { return bEngaging; }
	UFUNCTION(BlueprintPure, Category="AI|武器") AWeaponRuntime* GetWeaponRuntime() const { return WeaponRuntime; }
	UFUNCTION(BlueprintPure, Category="AI|战斗") AActor* GetEngagementTarget() const { return EngagementTarget.Get(); }
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|调试") FVector2D Compensation = FVector2D::ZeroVector;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|调试") FVector2D LastShotRecoil = FVector2D::ZeroVector;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|调试") FVector LastShotDirection = FVector::ZeroVector;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|调试") int32 SuccessfulShots = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|调试") bool bDrawDebugShots = false;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UPROPERTY(Transient) TObjectPtr<AWeaponRuntime> WeaponRuntime;
	UPROPERTY(Transient) TWeakObjectPtr<AActor> EngagementTarget;
	UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> MuzzleSource;
	FName MuzzleSocket = NAME_None;
	bool bCombatEnabled = true;
	bool bEngaging = false;
	FRotator TrackingRotation = FRotator::ZeroRotator;
	float ReadyAt = 0.0f;
	float NextAttemptAt = 0.0f;
	float SettledTime = 0.0f;
	int32 ShotsInVolley = 0;
	float NextDriftAt = 0.0f;
	FVector2D DriftTarget = FVector2D::ZeroVector;
	FVector2D Drift = FVector2D::ZeroVector;
	struct FRecoilObservation { float Time; FVector2D Offset; };
	TArray<FRecoilObservation> Observations;
	bool CanContinue() const;
	bool GetShotGeometry(FVector& OutMuzzle, FVector& OutAimPoint) const;
	void AttemptShot(float Now, const FVector& Muzzle, const FVector& AimPoint);
};
