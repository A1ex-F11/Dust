
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include"FireMode.h"
#include"WeaponRecoilData.h"
#include "WeaponRuntime.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFireCommitted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReloadStarted);

UCLASS()
class DUST2_API AWeaponRuntime : public AActor
{
	GENERATED_BODY()
public:
	AWeaponRuntime();
	UFUNCTION(BlueprintPure, Category = "Weapon")
	EFireMode GetFireMode() const;
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void RestoreFireMode(EFireMode SavedFireMode);
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void CycleFireMode();
	UFUNCTION(BlueprintPure, Category = "Reload")
	bool IsReloading() const;
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool TryStartReload();
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool FinishReload();
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool CancelReload();
	UFUNCTION(BlueprintPure, Category = "Reload")
	int32 GetReloadRequestId() const;
	UFUNCTION(BlueprintCallable, Category = "Reload")
	bool TryCommitReloadRound(int32 ExpectedReloadRequestId);
	UPROPERTY(BlueprintAssignable, Category = "Reload")
	FOnReloadStarted OnReloadStarted;
	UFUNCTION(BlueprintPure, category = "Ammo")
	int32 GetCurrentAmmo()const;
	UFUNCTION(BlueprintPure, category = "Ammo")
	int32 GetReserveAmmo()const;
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	bool TryConsumeAmmo();
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	bool AddReserveAmmo(int32 Amount);
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	bool ReloadAmmo();
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	void RestoreAmmo(int32 NewCurrentAmmo, int32 NewReserveAmmo);
	UFUNCTION(BlueprintCallable, Category = "Fire")
	bool TryCommitFire();
	UFUNCTION(BlueprintPure, Category = "Fire")
	float GetEffectiveFireInterval() const;
	UPROPERTY(BlueprintAssignable, Category = "Fire")
	FOnFireCommitted OnFireCommitted;
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void GenerateRecoilKick(float& PitchKick, float& YawKick, bool& bGenerated);
	UFUNCTION(BlueprintCallable, Category = "Spread")
	void GenerateShotDirection(
		const FVector& TraceDirection,
		float CurrentHorizontalSpeed,
		bool bIsFalling,
		bool bIsCrouched,
		bool bIsSlowWalking,
		FVector& ShotDirection,
		bool& bGenerated);


protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	EFireMode CurrentFireMode = EFireMode::SemiAutomatic;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TArray<EFireMode> AllowedFireModes;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	int32 CurrentAmmo;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	int32 MagazineCapacity;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	int32 ReserveAmmo;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Reload")
	bool bIsReloading;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fire")
	float FireInterval;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Fire")
	float NextFireTime;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fire",
		meta = (ClampMin = "0.0"))
	float BurstShotInterval;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TObjectPtr<UWeaponRecoilData>RecoilData;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread")
	float StationarySpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread")
	float MovementSpreadSpeedThreshold;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread")
	float MovingSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread")
	float JumpSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread")
	float SlowWalkSpreadAngle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reload")
	bool bLockReloadAmountAtStart = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reload",
		meta = (DisplayName = "逐颗装入弹药"))
	bool bReloadOneRoundAtATime = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reload",
		meta = (DisplayName = "满数量空仓换弹一次装入"))
	bool bUseBatchReloadForFullEmptyReload = false;
	UPROPERTY(Transient)
	bool bCurrentReloadUsesPerRound = false;
	UPROPERTY(Transient)
	int32 PendingReloadAmount = 0;
	UPROPERTY(Transient)
	int32 ReloadRequestId = 0;
};
