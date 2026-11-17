#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DustPickupPersistenceSubsystem.generated.h"

/** Run-local consumed IDs survive level reloads, independently of checkpoint rollback. */
UCLASS(BlueprintType, meta=(DisplayName="Dust Pickup Persistence"))
class DUST2_API UDustPickupPersistenceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool IsConsumed(FName LevelId, FName PickupId) const;
	bool RecordConsumed(FName LevelId, FName PickupId);
	TArray<FName> GetConsumedPickupIds(FName LevelId) const;
	void RestoreConsumedPickupIds(FName LevelId, const TArray<FName>& SavedIds);

	// Call before new-game travel, never after level actors have begun play or during retry.
	UFUNCTION(BlueprintCallable, Category="拾取", meta=(DisplayName="Native Reset Consumed Pickups"))
	void NativeResetConsumedPickups();

private:
	TMap<FName, TSet<FName>> ConsumedByLevel;
};
