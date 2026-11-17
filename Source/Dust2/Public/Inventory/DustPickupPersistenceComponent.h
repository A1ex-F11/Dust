#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustPickupPersistenceComponent.generated.h"

class UDustPickupPersistenceSubsystem;
struct FDustPickupPersistenceTestAccess;

/** Opt-in persistence for placed pickups. Runtime drops keep PickupId=None. */
UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent, DisplayName="Dust Pickup Persistence"))
class DUST2_API UDustPickupPersistenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustPickupPersistenceComponent();

	/**
	 * Old hand-authored id. It wins when set, preserving existing placed pickups.
	 * Leave it empty on new instances to use the owner's Dust Stable Id.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="拾取", meta=(DisplayName="Pickup ID"))
	FName PickupId;

	UFUNCTION(BlueprintPure, Category="拾取", meta=(DisplayName="Native Is Pickup Available"))
	bool NativeIsPickupAvailable() const;
	// Inventory calls this only after its pickup transaction commits.
	bool RecordSuccessfulPickup();
	UFUNCTION(BlueprintCallable, Category="拾取", meta=(DisplayName="Native Consume Pickup"))
	void NativeConsumePickup();

	/** PickupId first, then the shared stable-id component or its deferred marker. */
	UFUNCTION(BlueprintPure, Category="拾取", meta=(DisplayName="Native Get Effective Pickup Id"))
	FName NativeGetEffectivePickupId() const;

protected:
	virtual void BeginPlay() override;

private:
	FName GetLevelId() const;
	UDustPickupPersistenceSubsystem* GetPersistence() const;
	bool bConsumedThisInstance = false;

	friend struct FDustPickupPersistenceTestAccess;
	UDustPickupPersistenceSubsystem* PersistenceOverride = nullptr;
};
