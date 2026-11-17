#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DustEncounterPersistenceSubsystem.generated.h"

/**
 * Run-local record of deferred regions that finished their one-time activation.
 * Stores only map package identity + region FName. It never stores level actors.
 *
 * The existing pickup reset entry also clears this state, because both are
 * run-local GameInstance data cleared before a complete new-game travel.
 */
UCLASS(BlueprintType, meta=(DisplayName="Dust 遭遇区域持久化"))
class DUST2_API UDustEncounterPersistenceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool IsRegionActivated(FName LevelId, FName RegionId) const;
	bool RecordRegionActivated(FName LevelId, FName RegionId);

	UFUNCTION(BlueprintCallable, Category="遭遇", meta=(DisplayName="Native Reset Activated Regions"))
	void NativeResetActivatedRegions();

	UFUNCTION(BlueprintPure, Category="遭遇")
	TArray<FName> NativeGetActivatedRegionIds(FName LevelId) const;

	bool AdvanceSciFiObjective(int32 Step, const FText& Text);
	int32 GetSciFiObjectiveStep() const { return SciFiObjectiveStep; }
	const FText& GetSciFiObjectiveText() const { return SciFiObjectiveText; }

private:
	int32 SciFiObjectiveStep = 0;
	FText SciFiObjectiveText;
	TMap<FName, TSet<FName>> ActivatedRegionsByLevel;
};
