#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponRuntime.h"
#include "WeaponPickupActor.generated.h"

class APawn;
class UPrimitiveComponent;

UCLASS(meta=(DisplayName="武器拾取物"))
class DUST2_API AWeaponPickupActor : public AActor
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
};
