#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Interfaces/WeaponKitInterfaces.h"
#include "TimerManager.h"
#include "Runtime/WeaponKitInteractionScanner.generated.h"

class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponKitAimedPickupChanged, AActor*, AimedPickup);

/**
 * Tracks nearby pickups and chooses a manual or automatic candidate.
 * Inventory does not know how the player looks at a weapon; call this, then pass the actor to TryPickup.
 */
UCLASS(ClassGroup = (WeaponKit), meta = (BlueprintSpawnableComponent))
class WEAPONKIT_API UWeaponKitInteractionScanner : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponKitInteractionScanner();

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Scanner")
	bool RegisterPickup(AActor* Pickup);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Scanner")
	void UnregisterPickup(AActor* Pickup);

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Scanner")
	AActor* GetManualPickup() const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Scanner")
	AActor* GetAutomaticPickup(AActor* ExcludedPickup) const;

	UFUNCTION(BlueprintPure, Category = "WeaponKit|Scanner")
	AActor* GetAimedPickup() const;

	UPROPERTY(BlueprintAssignable, Category = "WeaponKit|Scanner")
	FOnWeaponKitAimedPickupChanged OnAimedPickupChanged;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WeaponKit|Scanner", meta = (ClampMin = "1.0"))
	float MaxDistance = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WeaponKit|Scanner")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WeaponKit|Scanner", meta = (ClampMin = "0.01"))
	float RefreshInterval = 0.1f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AActor* FindPickup(bool bAutomaticOnly, AActor* ExcludedPickup) const;
	bool IsCarrierAlive() const;
	void RefreshAimedPickup();
	bool HasRegisteredPickup() const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> NearbyPickups;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastAimedPickup;

	FTimerHandle RefreshTimerHandle;
	bool bHadAimedPickup = false;
};
