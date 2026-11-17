#pragma once

#include "CoreMinimal.h"
#include "Inventory/DustPickupActorBase.h"
#include "Weapons/WeaponRuntime.h"
#include "WeaponPickupActor.generated.h"

class APawn;
class UPrimitiveComponent;

UCLASS(meta=(DisplayName="武器拾取物"))
class DUST2_API AWeaponPickupActor : public ADustPickupActorBase
{
    GENERATED_BODY()

public:
    AWeaponPickupActor();

    UFUNCTION(BlueprintPure, Category = "武器|拾取")
    TSubclassOf<AWeaponRuntime> GetPickupWeaponClass() const;

    UFUNCTION(BlueprintPure, Category = "武器|拾取")
    FWeaponPickupState GetPickupSavedState() const;

    bool CanBePickedUpBy(const APawn* Player, bool bAutomatic) const;
    bool TryClaimPickup(APawn* Player, bool bAutomatic);
    void ReleasePickupClaim();
    // Fixed reward metadata belongs to this enemy drop, never to player drop snapshots.
    bool InitializeEnemyLoot(TSubclassOf<AWeaponRuntime> WeaponClass, int32 TotalAmmo);
    int32 GetEnemyLootAmmoRewardNative() const { return bEnemyLoot ? EnemyLootAmmoReward : 0; }
    UFUNCTION(BlueprintPure, Category = "武器|拾取", meta=(DisplayName="Native Is Enemy Loot"))
    bool NativeIsEnemyLoot() const { return bEnemyLoot; }
    void InitializeDroppedWeapon(
        TSubclassOf<AWeaponRuntime> WeaponClass,
        const FWeaponPickupState& State,
        APawn* DroppedBy,
        float AutoPickupBlockSeconds);

    UFUNCTION(BlueprintImplementableEvent, Category = "武器|拾取")
    void LaunchDroppedWeapon(FVector ForwardDirection);

protected:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|拾取", meta=(DisplayName="拾取武器类"))
    TSubclassOf<AWeaponRuntime> PickupWeaponClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "武器|拾取", meta=(DisplayName="拾取状态"))
    FWeaponPickupState PickupSavedState;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "武器|拾取", meta=(DisplayName="自动拾取组件名"))
    FName AutoPickupComponentName = TEXT("AutoPickupCollision");

private:
    const UPrimitiveComponent* FindAutoPickupVolume() const;

    UPROPERTY(Transient)
    TWeakObjectPtr<APawn> AutoPickupBlockedPawn;

    double AutoPickupBlockedUntil = 0.0;
    bool bPickupClaimed = false;
    bool bEnemyLoot = false;
    int32 EnemyLootAmmoReward = 0;
};
