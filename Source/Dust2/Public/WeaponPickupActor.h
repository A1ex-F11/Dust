#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponRuntime.h"
#include "WeaponPickupActor.generated.h"

class APawn;
class UPrimitiveComponent;

UCLASS()
class DUST2_API AWeaponPickupActor : public AActor
{
    GENERATED_BODY()

public:
    AWeaponPickupActor();

    UFUNCTION(BlueprintPure, Category = "Weapon|Pickup")
    TSubclassOf<AWeaponRuntime> GetPickupWeaponClass() const;

    UFUNCTION(BlueprintPure, Category = "Weapon|Pickup")
    FWeaponPickupState GetPickupSavedState() const;

    bool CanBePickedUpBy(const APawn* Player, bool bAutomatic) const;
    bool TryClaimPickup(APawn* Player, bool bAutomatic);
    void ReleasePickupClaim();
    void InitializeDroppedWeapon(
        TSubclassOf<AWeaponRuntime> WeaponClass,
        const FWeaponPickupState& State,
        APawn* DroppedBy,
        float AutoPickupBlockSeconds);

    UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Pickup")
    void LaunchDroppedWeapon(FVector ForwardDirection);

protected:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Pickup")
    TSubclassOf<AWeaponRuntime> PickupWeaponClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Pickup")
    FWeaponPickupState PickupSavedState;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Pickup")
    FName AutoPickupComponentName = TEXT("AutoPickupCollision");

private:
    const UPrimitiveComponent* FindAutoPickupVolume() const;

    UPROPERTY(Transient)
    TWeakObjectPtr<APawn> AutoPickupBlockedPawn;

    double AutoPickupBlockedUntil = 0.0;
    bool bPickupClaimed = false;
};
