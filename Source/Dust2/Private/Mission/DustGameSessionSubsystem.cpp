#include "Mission/DustGameSessionSubsystem.h"

#include "AI/DustEnemyCombatProfile.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

bool UDustGameSessionSubsystem::ConfigureDifficultyProfiles(
	UDustEnemyCombatProfile* Easy,
	UDustEnemyCombatProfile* Normal,
	UDustEnemyCombatProfile* Hard)
{
	if (bSessionActive)
	{
		// A live session locks its difficulty. Repeating the identical configuration is harmless.
		return EasyProfile == Easy && NormalProfile == Normal && HardProfile == Hard;
	}

	EasyProfile = Easy;
	NormalProfile = Normal;
	HardProfile = Hard;
	return true;
}

bool UDustGameSessionSubsystem::BeginNewSession(EDustEnemyDifficulty Difficulty)
{
	// Never switch difficulty in the middle of a run; the Blueprint has to end the old session.
	if (bSessionActive) return false;

	UDustEnemyCombatProfile* Profile = GetDifficultyProfile(Difficulty);
	if (!IsValid(Profile)) return false;

	UGameInstance* Instance = GetGameInstance();
	if (!Instance) return false;

	// Commit the complete selection only after every validation passed, so a rejected request
	// leaves the previous inactive state untouched.
	SelectedDifficulty = Difficulty;
	SelectedCombatProfile = Profile;
	SeenTutorials.Reset();
	bSessionActive = true;

	ApplySelectedProfileToWorldEnemies(Instance->GetWorld());
	return true;
}

const FDustNativeRouteCheckpoint* UDustGameSessionSubsystem::GetRouteCheckpoint(FName MapId) const
{
	return !MapId.IsNone() && RouteCheckpoint.Route.MapId == MapId ? &RouteCheckpoint : nullptr;
}

const FDustNativeRouteCheckpoint* UDustGameSessionSubsystem::GetPendingRouteCheckpoint(FName MapId) const
{
	return bRouteRetryPending ? GetRouteCheckpoint(MapId) : nullptr;
}

void UDustGameSessionSubsystem::SaveRouteCheckpoint(FDustNativeRouteCheckpoint&& Checkpoint)
{
	RouteCheckpoint = MoveTemp(Checkpoint);
	bRouteRetryPending = false;
}

void UDustGameSessionSubsystem::ResetRouteCheckpoint()
{
	RouteCheckpoint = FDustNativeRouteCheckpoint{};
	bRouteRetryPending = false;
}

void UDustGameSessionSubsystem::EndSessionForDifficultySelection()
{
	ResetRouteCheckpoint();
	// Deliberately keeps SelectedDifficulty and the three configured assets for the next menu visit.
	SelectedCombatProfile = nullptr;
	SeenTutorials.Reset();
	bSessionActive = false;
}

UDustEnemyCombatProfile* UDustGameSessionSubsystem::GetDifficultyProfile(
	EDustEnemyDifficulty Difficulty) const
{
	switch (Difficulty)
	{
	case EDustEnemyDifficulty::Easy:
		return EasyProfile.Get();
	case EDustEnemyDifficulty::Normal:
		return NormalProfile.Get();
	case EDustEnemyDifficulty::Hard:
		return HardProfile.Get();
	default:
		return nullptr;
	}
}

int32 UDustGameSessionSubsystem::ApplySelectedProfileToWorldEnemies(UWorld* World)
{
	if (!World || !bSessionActive || !IsValid(SelectedCombatProfile)) return 0;

	// The sweep only reads the current world. Pre-placed enemies that already exist when the
	// Blueprint starts the session are included, exactly like enemies spawned earlier in a menu.
	int32 AppliedCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) continue;

		TInlineComponentArray<UDustEnemyWeaponComponent*> Weapons;
		Actor->GetComponents(Weapons);
		for (UDustEnemyWeaponComponent* Weapon : Weapons)
		{
			if (IsValid(Weapon) && Weapon->ApplySelectedSessionDifficulty())
			{
				++AppliedCount;
			}
		}
	}
	return AppliedCount;
}
