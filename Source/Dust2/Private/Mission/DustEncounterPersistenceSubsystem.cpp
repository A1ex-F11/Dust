#include "Mission/DustEncounterPersistenceSubsystem.h"

bool UDustEncounterPersistenceSubsystem::IsRegionActivated(FName LevelId, FName RegionId) const
{
	if (LevelId.IsNone() || RegionId.IsNone()) return false;
	const TSet<FName>* Regions = ActivatedRegionsByLevel.Find(LevelId);
	return Regions && Regions->Contains(RegionId);
}

bool UDustEncounterPersistenceSubsystem::RecordRegionActivated(FName LevelId, FName RegionId)
{
	if (LevelId.IsNone() || RegionId.IsNone()) return false;
	TSet<FName>& Regions = ActivatedRegionsByLevel.FindOrAdd(LevelId);
	if (Regions.Contains(RegionId)) return false;
	Regions.Add(RegionId);
	return true;
}

bool UDustEncounterPersistenceSubsystem::AdvanceSciFiObjective(int32 Step, const FText& Text)
{
	if (Step <= SciFiObjectiveStep || Step > 6) return false;
	SciFiObjectiveStep = Step;
	SciFiObjectiveText = Text;
	return true;
}

void UDustEncounterPersistenceSubsystem::NativeResetActivatedRegions()
{
	ActivatedRegionsByLevel.Reset();
	SciFiObjectiveStep = 0;
	SciFiObjectiveText = FText::GetEmpty();
}

TArray<FName> UDustEncounterPersistenceSubsystem::NativeGetActivatedRegionIds(FName LevelId) const
{
	TArray<FName> Result;
	if (const TSet<FName>* Regions = ActivatedRegionsByLevel.Find(LevelId))
	{
		Result.Reserve(Regions->Num());
		for (const FName& RegionId : *Regions)
		{
			Result.Add(RegionId);
		}
	}
	return Result;
}
