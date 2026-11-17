#include "Inventory/DustPickupPersistenceSubsystem.h"

#include "Engine/GameInstance.h"
#include "Mission/DustEncounterPersistenceSubsystem.h"
#include "Mission/DustGameSessionSubsystem.h"

bool UDustPickupPersistenceSubsystem::IsConsumed(FName LevelId, FName PickupId) const
{
	if (LevelId.IsNone() || PickupId.IsNone()) return false;
	const TSet<FName>* Ids = ConsumedByLevel.Find(LevelId);
	return Ids && Ids->Contains(PickupId);
}

bool UDustPickupPersistenceSubsystem::RecordConsumed(FName LevelId, FName PickupId)
{
	if (LevelId.IsNone() || PickupId.IsNone()) return false;
	TSet<FName>& Ids = ConsumedByLevel.FindOrAdd(LevelId);
	if (Ids.Contains(PickupId)) return false;
	Ids.Add(PickupId);
	return true;
}

TArray<FName> UDustPickupPersistenceSubsystem::GetConsumedPickupIds(FName LevelId) const
{
    const TSet<FName>* Ids = ConsumedByLevel.Find(LevelId);
    return Ids ? Ids->Array() : TArray<FName>{};
}

void UDustPickupPersistenceSubsystem::RestoreConsumedPickupIds(FName LevelId, const TArray<FName>& SavedIds)
{
    if (LevelId.IsNone()) return;
    ConsumedByLevel.Remove(LevelId);
    for (FName Id : SavedIds) RecordConsumed(LevelId, Id);
}

void UDustPickupPersistenceSubsystem::NativeResetConsumedPickups()
{
	ConsumedByLevel.Reset();

	// The existing complete-new-run reset entry also clears deferred encounter
	// activation, because both are run-local GameInstance identity state.
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UDustGameSessionSubsystem* Session = Instance->GetSubsystem<UDustGameSessionSubsystem>())
		{
			Session->ResetRouteCheckpoint();
		}
		if (UDustEncounterPersistenceSubsystem* EncounterPersistence =
			Instance->GetSubsystem<UDustEncounterPersistenceSubsystem>())
		{
			EncounterPersistence->NativeResetActivatedRegions();
		}
	}
}
