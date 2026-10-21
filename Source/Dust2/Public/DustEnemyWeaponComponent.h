#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FireMode.h"
#include "WeaponPresentationTypes.h"
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
	/** Reads only this enemy's selected gameplay definition. Never selects by the bare runtime class. */
	UFUNCTION(BlueprintPure, Category="AI|武器", meta=(DisplayName="获取敌人武器表现配置"))
	bool ResolveEnemyPresentationProfile(FWeaponPresentationProfile& OutProfile) const;
	/** Initialization bridge only: does not bind animations, create visuals or start a reload. */
	UFUNCTION(BlueprintCallable, Category="AI|武器", meta=(DisplayName="应用敌人换弹表现配置"))
	bool ApplyReloadPresentationFromProfile();
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
	/** Read-only runtime view of the current volley target; 0 means the next successful shot will sample one. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category="AI|调试", meta=(DisplayName="当前轮目标发数（只读）"))
	int32 CurrentVolleyShotTarget = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|调试", meta=(DisplayName="显示射击调试线")) bool bDrawDebugShots = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|调试") FString LastCombatDiagnostic;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	friend class FEnemyWeaponVolleyBoundsTest;
	friend struct FDustEnemyWeaponAwarenessTestAccess;
	friend struct FDustEnemyCloseCombatTestAccess;
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
	EFireMode VolleyFireMode = EFireMode::SemiAutomatic;
	float NextDriftAt = 0.0f;
	FVector2D DriftTarget = FVector2D::ZeroVector;
	FVector2D Drift = FVector2D::ZeroVector;
	struct FRecoilObservation { float Time; FVector2D Offset; };
	TArray<FRecoilObservation> Observations;
	void SetCombatDiagnostic(const FString& Message);
	bool CanContinue() const;
	bool GetShotGeometry(FVector& OutMuzzle, FVector& OutAimPoint) const;
	/** Adapter-backed path only: actual pawn forward and tracking must both face the aim point. */
	bool IsAimAligned(const FVector& Muzzle, const FVector& AimPoint) const;
	/** Samples once for the current mode; full-auto normalizes the profile range, other modes stay native. */
	int32 ResolveVolleyShotTarget(EFireMode Mode) const;
	void AttemptShot(float Now, const FVector& Muzzle, const FVector& AimPoint);
};
