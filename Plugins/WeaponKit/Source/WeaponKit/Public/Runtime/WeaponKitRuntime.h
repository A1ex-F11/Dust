#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/WeaponKitTypes.h"
#include "Data/WeaponKitDefinitionData.h"
#include "Runtime/WeaponKitRuntime.generated.h"

class UWeaponKitBallisticsData;
class UWeaponKitRecoilData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponKitFireCommitted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponKitReloadStarted);

/**
 * Runtime weapon actor. Static tuning comes from UWeaponKitDefinitionData.
 * Aim is SetAiming(); the player character is not a friend and is not referenced.
 */
UCLASS(Blueprintable)
class WEAPONKIT_API AWeaponKitRuntime : public AActor
{
	GENERATED_BODY()

public:
	AWeaponKitRuntime();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WeaponKit", meta = (DisplayName = "枪械玩法配置"))
	TObjectPtr<UWeaponKitDefinitionData> WeaponDefinition;

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Aim")
	void SetAiming(bool bAiming);

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Aim")
	bool IsAiming() const { return bOwnerAiming; }

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Config", meta = (DisplayName = "应用枪械配置（保留当前弹药）"))
	void ApplyDefinitionSettings();

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Config")
	float GetCurrentFiringSpread() const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Config")
	int32 GetMagazineCapacity() const { return MagazineCapacity; }

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Config")
	float GetBurstRecoveryInterval() const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Config")
	UWeaponKitBallisticsData* GetBallistics() const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Config")
	FText GetWeaponDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit")
	EWeaponKitFireMode GetFireMode() const { return CurrentFireMode; }

	UFUNCTION(BlueprintCallable, Category = "WeaponKit")
	void RestoreFireMode(EWeaponKitFireMode SavedFireMode);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit")
	void CycleFireMode();

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Reload")
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Reload")
	bool TryStartReload();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Reload")
	bool FinishReload();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Reload")
	bool CancelReload();

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Reload")
	int32 GetReloadRequestId() const { return ReloadRequestId; }

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Reload")
	bool TryCommitReloadRound(int32 ExpectedReloadRequestId);

	UPROPERTY(BlueprintAssignable, Category = "WeaponKit|Reload")
	FOnWeaponKitReloadStarted OnReloadStarted;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Ammo")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Ammo")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Ammo")
	bool TryConsumeAmmo();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Ammo")
	bool AddReserveAmmo(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Ammo")
	bool ReloadAmmo();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Ammo")
	void RestoreAmmo(int32 NewCurrentAmmo, int32 NewReserveAmmo);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Fire")
	bool TryCommitFire();

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Fire")
	float GetEffectiveFireInterval() const;

	UPROPERTY(BlueprintAssignable, Category = "WeaponKit|Fire")
	FOnWeaponKitFireCommitted OnFireCommitted;

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Recoil")
	void ResetRecoilPattern();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Recoil")
	void GenerateRecoilKick(float& PitchKick, float& YawKick, bool& bGenerated);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Spread")
	void GenerateShotDirection(
		const FVector& TraceDirection,
		float CurrentHorizontalSpeed,
		bool bIsFalling,
		bool bIsCrouched,
		bool bIsSlowWalking,
		FVector& ShotDirection,
		bool& bGenerated);

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Display")
	static FText GetDisplayNameForWeaponClass(TSubclassOf<AWeaponKitRuntime> WeaponClass);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Pickup")
	void RestorePickupRuntimeState(const FWeaponKitPickupState& State);

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Pickup")
	FWeaponKitPickupState CapturePickupRuntimeState(bool bInitialDrawConsumed) const;

private:
	FWeaponKitSpreadProfile GetSpreadProfile(bool bAiming) const;
	float GetFiringSpreadForProfile(bool bAiming) const;
	void CacheCommittedRecoil(float SuccessfulShotTime);
	void CacheCommittedFiringSpread(float SuccessfulShotTime);
	void AdvanceRecoilPatternIndex(int32 PatternSize);
	void ClearPendingRecoilKick();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Ammo", meta = (AllowPrivateAccess = "true"))
	int32 CurrentAmmo = 30;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Ammo", meta = (AllowPrivateAccess = "true"))
	int32 MagazineCapacity = 30;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Ammo", meta = (AllowPrivateAccess = "true"))
	int32 ReserveAmmo = 90;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit", meta = (AllowPrivateAccess = "true"))
	EWeaponKitFireMode CurrentFireMode = EWeaponKitFireMode::SemiAutomatic;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit", meta = (AllowPrivateAccess = "true"))
	TArray<EWeaponKitFireMode> AllowedFireModes;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Fire", meta = (AllowPrivateAccess = "true"))
	float FireInterval = 0.15f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Fire", meta = (AllowPrivateAccess = "true"))
	float BurstShotInterval = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Display", meta = (AllowPrivateAccess = "true"))
	FText WeaponDisplayName;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Reload", meta = (AllowPrivateAccess = "true"))
	bool bIsReloading = false;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "WeaponKit|Fire", meta = (AllowPrivateAccess = "true"))
	float NextFireTime = 0.0f;

	TObjectPtr<UWeaponKitRecoilData> RecoilData;
	bool bLockReloadAmountAtStart = false;
	bool bReloadOneRoundAtATime = false;
	bool bUseBatchReloadForFullEmptyReload = false;
	bool bCurrentReloadUsesPerRound = false;
	int32 PendingReloadAmount = 0;
	int32 ReloadRequestId = 0;

	bool bOwnerAiming = false;
	FWeaponKitSpreadProfile CommittedSpreadProfile;
	bool bHasCommittedSpreadProfile = false;
	bool bCommittedIgnoreShotRecoil = false;
	float AccumulatedAimFiringSpread = 0.0f;
	float LastAimSpreadShotTime = 0.0f;
	bool bHasAimSpreadShot = false;
	float AccumulatedFiringSpread = 0.0f;
	float CommittedFiringSpread = 0.0f;
	float LastSpreadShotTime = 0.0f;
	bool bHasSpreadShot = false;
	bool bRestoredAmmo = false;
	bool bRestoredFireMode = false;
	bool bInitialDefinitionApplied = false;
	int32 RecoilPatternIndex = 0;
	float LastSuccessfulShotTime = 0.0f;
	bool bHasSuccessfulShotTime = false;
	float CachedRecoilPitchKick = 0.0f;
	float CachedRecoilYawKick = 0.0f;
	bool bHasPendingRecoilKick = false;
	FVector2D AccumulatedShotRecoil = FVector2D::ZeroVector;
	FVector2D CommittedShotRecoil = FVector2D::ZeroVector;
	bool bHasCommittedShotDirection = false;
};

USTRUCT(BlueprintType)
struct WEAPONKIT_API FWeaponKitSlotSave
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Inventory")
	TSubclassOf<AWeaponKitRuntime> WeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Inventory")
	FWeaponKitPickupState RuntimeState;
};

/** One checkpoint record for every configured slot. Replaces a fixed parameter list. */
USTRUCT(BlueprintType)
struct WEAPONKIT_API FWeaponKitLoadoutSave
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Inventory")
	TArray<FWeaponKitSlotSave> Slots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Inventory")
	int32 ActiveSlotIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct WEAPONKIT_API FWeaponKitInventorySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WeaponKit|Inventory")
	TArray<TObjectPtr<AWeaponKitRuntime>> SlotWeapons;

	UPROPERTY(BlueprintReadOnly, Category = "WeaponKit|Inventory")
	TObjectPtr<AWeaponKitRuntime> CurrentWeapon;

	UPROPERTY(BlueprintReadOnly, Category = "WeaponKit|Inventory")
	int32 ActiveSlotIndex = INDEX_NONE;
};
