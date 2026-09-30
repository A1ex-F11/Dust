#include"Dust2GameMode.h"

ADust2GameMode::ADust2GameMode()
{
	MissionState = EMissionState::NotStarted;
	bResultResolved = false;
	MissionResult = EMissionResult::Defused;
}
EMissionState ADust2GameMode::GetMissionState() const
{
	return MissionState;
}
bool ADust2GameMode::TryResolveMissionResult(EMissionResult Result)
{
	if (bResultResolved)
	{
		return false;
	}
	MissionResult = Result;
	bResultResolved = true;
	return true;
}
bool ADust2GameMode::TrySetMissionState(EMissionState NewState)
{
	if (NewState == MissionState)
	{
		return false;
	}
	bool bAllowed = false;
	switch (MissionState)
	{
		case EMissionState::NotStarted:
			bAllowed = (NewState == EMissionState::Resupply);
			break;

		case EMissionState::Resupply:
			bAllowed = (NewState == EMissionState::GoToB);
			break;

		case EMissionState::GoToB:
				bAllowed = (NewState == EMissionState::ReturnToA);
				break;

		case EMissionState::ReturnToA:
			bAllowed = (NewState == EMissionState::C4Planted);
			break;

		default:
			bAllowed = false;
			break;
	}
	if (!bAllowed)
	{
		return false;
	}
	MissionState = NewState;
	OnMissionChanged.Broadcast(MissionState);
	return true;
}
void ADust2GameMode::RestoreMissionState(EMissionState SavedState)

{
	MissionState = SavedState;
	OnMissionChanged.Broadcast(MissionState);
}