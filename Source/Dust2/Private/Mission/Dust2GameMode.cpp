#include"Mission/Dust2GameMode.h"
#include"Player/Dust2PlayerCharacter.h"
#include"Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Combat/DustEnemyHealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Mission/DustEncounterPersistenceSubsystem.h"
#include "Mission/DustSciFiObjectiveTextData.h"
#include "Inventory/DustPickupPersistenceSubsystem.h"
#include "Inventory/WeaponInventoryComponent.h"
#include "Mission/DustGameSessionSubsystem.h"

namespace
{
	bool IsKnownMissionState(EMissionState State)
	{
		switch (State)
		{
		case EMissionState::NotStarted:
		case EMissionState::Resupply:
		case EMissionState::GoToB:
		case EMissionState::ReturnToA:
		case EMissionState::C4Planted:
			return true;
		default:
			return false;
		}
	}
}

ADust2GameMode::ADust2GameMode()
{
	MissionState = EMissionState::NotStarted;
	bResultResolved = false;
	MissionResult = EMissionResult::Defused;
}
void ADust2GameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (const FDustNativeRouteCheckpoint* Checkpoint = GetPendingNativeRouteCheckpoint())
	{
		MissionState = Checkpoint->MissionState;
		NativeRestoreDeadEnemyIds(Checkpoint->DeadEnemyIds);
	}
}

FName ADust2GameMode::GetNativeRouteMapId() const
{
	const UWorld* World = GetWorld();
	return World ? FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : NAME_None;
}

const FDustNativeRouteCheckpoint* ADust2GameMode::GetPendingNativeRouteCheckpoint() const
{
	const UGameInstance* Instance = GetGameInstance();
	const auto* Session = Instance ? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
	return Session ? Session->GetPendingRouteCheckpoint(GetNativeRouteMapId()) : nullptr;
}

bool ADust2GameMode::HasNativeMissionCheckpoint() const
{
	const UGameInstance* Instance = GetGameInstance();
	const auto* Session = Instance ? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
	return Session && Session->GetRouteCheckpoint(GetNativeRouteMapId());
}

bool ADust2GameMode::HasPendingNativeMissionCheckpoint() const
{
	return GetPendingNativeRouteCheckpoint() != nullptr;
}

bool ADust2GameMode::TryRetryNativeMissionCheckpoint()
{
	ADustMissionRoute* Route = MissionRoute.Get();
	if (bNativeRouteTravelStarted || !Route ||
		GetMissionRetryAction(false) != EDustMissionRetryAction::RestoreCheckpoint) return false;

	UGameInstance* Instance = GetGameInstance();
	auto* Session = Instance ? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
	const FName MapId = GetNativeRouteMapId();
	const FDustNativeRouteCheckpoint* Checkpoint = Session ? Session->GetRouteCheckpoint(MapId) : nullptr;
	if (!Checkpoint || !Route->PrepareMissionRouteRetry(Checkpoint->Route)) return false;

	bNativeRouteTravelStarted = true;
	Session->SetRouteRetryPending(true);
	UGameplayStatics::SetGamePaused(this, false);
	UGameplayStatics::OpenLevel(this, MapId);
	return true;
}

bool ADust2GameMode::RestoreNativeRouteLoadout(ADust2PlayerCharacter* Player,
	UWeaponInventoryComponent* Inventory, UChildActorComponent* Slot1, UChildActorComponent* Slot2)
{
	const FDustNativeRouteCheckpoint* Checkpoint = GetPendingNativeRouteCheckpoint();
	if (!Checkpoint || !IsValid(Player) || Player->GetWorld() != GetWorld() || !IsValid(Inventory))
	{
		return false;
	}
	if (NativeRouteRestoredPlayer.IsValid()) return NativeRouteRestoredPlayer.Get() == Player;
	if (Inventory->IsNativeInventoryInitialized()) return false;

	// A slot rebuild can run Blueprint callbacks. Keep the input stable until it finishes.
	const FDustNativeRouteCheckpoint Saved = *Checkpoint;
	if (!Player->SetActorLocationAndRotation(Saved.SpawnTransform.GetLocation(),
		Saved.SpawnTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics) ||
		!Inventory->RestoreCheckpointWeapons(Slot1, Slot2,
			Saved.Slot1.WeaponClass, Saved.Slot1.CurrentAmmo, Saved.Slot1.ReserveAmmo,
			Saved.Slot1.FireMode, Saved.Slot1.bInitialDrawConsumed,
			Saved.Slot2.WeaponClass, Saved.Slot2.CurrentAmmo, Saved.Slot2.ReserveAmmo,
			Saved.Slot2.FireMode, Saved.Slot2.bInitialDrawConsumed, Saved.bSlot1Active))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not restore native checkpoint '%s' for '%s'."),
			*Saved.CheckpointId.ToString(), *GetNameSafe(Player));
		return false;
	}
	Player->RestoreHealth(Saved.Health);
	return !Player->IsDead();
}

void ADust2GameMode::MarkNativeRoutePlayerReady(ADust2PlayerCharacter* Player)
{
	NativeRouteRestoredPlayer = Player;
}

bool ADust2GameMode::IsNativeRoutePlayerReady(const ADust2PlayerCharacter* Player) const
{
	return IsValid(Player) && NativeRouteRestoredPlayer.Get() == Player;
}

bool ADust2GameMode::RegisterMissionRoute(ADustMissionRoute* Route)
{
    if (!IsValid(Route) || Route->GetWorld() != GetWorld() ||
        (MissionRoute.IsValid() && MissionRoute.Get() != Route)) return false;
    MissionRoute = Route;
    return true;
}

void ADust2GameMode::UnregisterMissionRoute(const ADustMissionRoute* Route)
{
    if (MissionRoute.Get() == Route) MissionRoute.Reset();
}

EMissionState ADust2GameMode::GetMissionState() const
{
	return MissionState;
}
bool ADust2GameMode::TryResolveMissionResult(EMissionResult Result)
{
	if (bResultResolved ||
		(Result != EMissionResult::Defused && Result != EMissionResult::Exploded &&
		 Result != EMissionResult::PlayerDied))
	{
		return false;
	}
	MissionResult = Result;
	bResultResolved = true;
	return true;
}
EDustMissionRetryAction ADust2GameMode::GetMissionRetryAction(bool bHasCheckpoint) const
{
	// The constructor's default Defused value is not a victory until bResultResolved is set.
	if (!bResultResolved) return EDustMissionRetryAction::Unavailable;

	switch (MissionResult)
	{
	case EMissionResult::Defused:
		return EDustMissionRetryAction::ChooseDifficulty;

	case EMissionResult::Exploded:
	case EMissionResult::PlayerDied:
		return (HasActiveMissionRoute() ? HasNativeMissionCheckpoint() : bHasCheckpoint)
			? EDustMissionRetryAction::RestoreCheckpoint
			: EDustMissionRetryAction::RestartFromBeginning;

	default:
		return EDustMissionRetryAction::Unavailable;
	}
}

bool ADust2GameMode::TrySetMissionState(EMissionState NewState)
{
	if (bResultResolved || NewState == MissionState ||
		(HasActiveMissionRoute() && NewState == EMissionState::C4Planted))
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
	// A region can report its facts before the preceding objective advances.
	if (MissionState == EMissionState::GoToB)
	{
		NativeCheckBComplete();
	}
	return true;
}
void ADust2GameMode::RestoreMissionState(EMissionState SavedState)

{
	if (!IsKnownMissionState(SavedState)) return;
	bResultResolved = false;
	MissionResult = EMissionResult::Defused;
	// Existing snapshots save the stage, not partially completed B-site facts.
	bNativeReachedB = SavedState == EMissionState::ReturnToA || SavedState == EMissionState::C4Planted;
	bNativeBCleared = bNativeReachedB;
	MissionState = SavedState;
	OnMissionChanged.Broadcast(MissionState);
}

void ADust2GameMode::NativeNotifyBReached()
{
	if (bResultResolved) return;
	bNativeReachedB = true;
	NativeCheckBComplete();
}

void ADust2GameMode::NativeNotifyBCleared()
{
	if (bResultResolved) return;
	bNativeBCleared = true;
	NativeCheckBComplete();
}

bool ADust2GameMode::NativeCheckBComplete()
{
	if (HasActiveMissionRoute() || bResultResolved || MissionState != EMissionState::GoToB ||
		!bNativeReachedB || !bNativeBCleared) return false;
	return TrySetMissionState(EMissionState::ReturnToA);
}

bool ADust2GameMode::IsSciFiDemo() const
{
	return UGameplayStatics::GetCurrentLevelName(this, true) == TEXT("L_SciFiDemo");
}

UDustEncounterPersistenceSubsystem* ADust2GameMode::GetEncounterPersistence() const
{
	const UGameInstance* Instance = GetGameInstance();
	return Instance ? Instance->GetSubsystem<UDustEncounterPersistenceSubsystem>() : nullptr;
}

bool ADust2GameMode::CanActivateSciFiObjectiveStep(int32 Step) const
{
	if (HasActiveMissionRoute()) return !bResultResolved;
	if (!IsSciFiDemo() || Step == 0) return true;
	if (bResultResolved || Step < 1 || Step > 5) return false;
	return Step <= 3 ? MissionState == EMissionState::GoToB : MissionState == EMissionState::ReturnToA;
}

void ADust2GameMode::NotifySciFiObjectiveStep(int32 Step, const FText& Text)
{
	if (HasActiveMissionRoute() || !IsSciFiDemo() || bResultResolved || Step < 1 || Step > 6) return;
	const EMissionState RequiredState = Step <= 3 ? EMissionState::GoToB : EMissionState::ReturnToA;
	if (MissionState != RequiredState) return;
	if (UDustEncounterPersistenceSubsystem* Persistence = GetEncounterPersistence();
		Persistence && Persistence->AdvanceSciFiObjective(Step, Text))
	{
		OnSciFiObjectiveChanged.Broadcast(MissionState);
	}
}

bool ADust2GameMode::NativeGetSciFiObjectiveText(FText& Text) const
{
	if (const ADustMissionRoute* Route = MissionRoute.Get())
	{
		Text = Route->GetCurrentRouteText();
		return true;
	}
	Text = FText::GetEmpty();
	if (!IsSciFiDemo()) return false;

	if (MissionState == EMissionState::Resupply || MissionState == EMissionState::C4Planted)
	{
		if (!SciFiObjectiveTextData) return false;
		Text = MissionState == EMissionState::Resupply
			? SciFiObjectiveTextData->Intro : SciFiObjectiveTextData->DefuseC4;
		return !Text.IsEmpty();
	}

	if (MissionState != EMissionState::GoToB && MissionState != EMissionState::ReturnToA) return false;

	const UDustEncounterPersistenceSubsystem* Persistence = GetEncounterPersistence();
	const int32 SavedStep = Persistence ? Persistence->GetSciFiObjectiveStep() : 0;
	const int32 Step = MissionState == EMissionState::GoToB
		? FMath::Clamp(SavedStep, 1, 3) : FMath::Clamp(SavedStep, 4, 6);
	if (SciFiObjectiveTextData)
	{
		const FText* Entries[] = {
			&SciFiObjectiveTextData->Area1, &SciFiObjectiveTextData->Area2,
			&SciFiObjectiveTextData->Area3, &SciFiObjectiveTextData->Area4,
			&SciFiObjectiveTextData->Area5, &SciFiObjectiveTextData->ReturnToA
		};
		if (!Entries[Step - 1]->IsEmpty())
		{
			Text = *Entries[Step - 1];
			return true;
		}
	}

	if (Persistence && SavedStep == Step && !Persistence->GetSciFiObjectiveText().IsEmpty())
	{
		Text = Persistence->GetSciFiObjectiveText();
		return true;
	}

	switch (Step)
	{
	case 1: Text = NSLOCTEXT("DustSciFi", "Area1", "清缴第 1 区，打开通往 B 点的路线。"); break;
	case 2: Text = NSLOCTEXT("DustSciFi", "Area2", "进入第 2 区，清除沿途守军。"); break;
	case 3: Text = NSLOCTEXT("DustSciFi", "Area3", "进入 B 点，清缴第 3 区。"); break;
	case 4: Text = NSLOCTEXT("DustSciFi", "Area4", "B 点没有炸弹。返回 A 区，清缴第 4 区。"); break;
	case 5: Text = NSLOCTEXT("DustSciFi", "Area5", "清缴第 5 区，打通返回 A 区的路线。"); break;
	case 6: Text = NSLOCTEXT("DustSciFi", "ReturnA", "返回 A 区，找到 C4。"); break;
	default: return false;
	}
	return true;
}

bool ADust2GameMode::NativeTryPlantC4(AActor* C4Actor, bool bHasCheckpoint)
{
	if (bResultResolved || (HasActiveMissionRoute() ? !HasNativeMissionCheckpoint() : !bHasCheckpoint) ||
		!IsValid(C4Actor) || C4Actor->IsActorBeingDestroyed() || C4Actor->GetWorld() != GetWorld())
	{
		return false;
	}
	if (const ADustMissionRoute* Route = MissionRoute.Get())
	{
		if (MissionState == EMissionState::C4Planted || !Route->IsApplyingC4(C4Actor)) return false;
		MissionState = EMissionState::C4Planted;
		OnMissionChanged.Broadcast(MissionState);
		return true;
	}
	if (MissionState != EMissionState::ReturnToA) return false;
	if (IsSciFiDemo())
	{
		const UDustEncounterPersistenceSubsystem* Persistence = GetEncounterPersistence();
		if (!Persistence || Persistence->GetSciFiObjectiveStep() < 6) return false;
	}
	return TrySetMissionState(EMissionState::C4Planted);
}

bool ADust2GameMode::NativeResolveBombExplosion(ADust2PlayerCharacter* Player, bool& bDiedNow)
{
	bDiedNow = false;
	if (MissionState != EMissionState::C4Planted) return false;

	UWorld* World = GetWorld();
	if (!World) return false;

	// A live actor from another world is rejected before resolution, so a valid C4 in this world
	// can never kill an unrelated player elsewhere. A null player is allowed to resolve without
	// damage; a non-null player without a world is treated as invalid and receives no damage.
	if (IsValid(Player) && Player->GetWorld() && Player->GetWorld() != World) return false;

	// The existing result lock makes repeated calls, a post-victory call and a later death
	// callback harmless: none of them may replace the first terminal result.
	if (!TryResolveMissionResult(EMissionResult::Exploded)) return false;

	// Death events may remove actors or change encounters, so collect targets first.
	TArray<TWeakObjectPtr<UDustEnemyHealthComponent>> EnemyHealthComponents;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (UDustEnemyHealthComponent* Health = It->FindComponentByClass<UDustEnemyHealthComponent>())
		{
			EnemyHealthComponents.Add(Health);
		}
	}
	for (const TWeakObjectPtr<UDustEnemyHealthComponent>& Entry : EnemyHealthComponents)
	{
		if (UDustEnemyHealthComponent* Health = Entry.Get())
		{
			Health->KillFromEnvironment();
		}
	}

	if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld() != World)
	{
		return true;
	}

	const float Health = Player->GetCurrentHealth();
	if (!FMath::IsFinite(Health) || Health <= 0.0f) return true;

	// Explosion failure must kill through the existing ApplyDamage path (CancelActions, ragdoll,
	// death camera), even when the player enabled the debug-only invulnerability flag.
	const bool bWasInvulnerable = Player->bInvulnerable;
	Player->bInvulnerable = false;
	Player->ApplyDamage(Health, bDiedNow);
	Player->bInvulnerable = bWasInvulnerable;
	return true;
}

void ADust2GameMode::NativeResetMissionProgress()
{
	bNativeReachedB = false;
	bNativeBCleared = false;
	NativeDeadEnemyIds.Reset();
	bResultResolved = false;
	MissionResult = EMissionResult::Defused;
	MissionState = EMissionState::NotStarted;
	OnMissionChanged.Broadcast(MissionState);
}

void ADust2GameMode::NativeRegisterEnemyDeath(FName EnemyId)
{
	if (!EnemyId.IsNone()) NativeDeadEnemyIds.AddUnique(EnemyId);
}

void ADust2GameMode::NativeRestoreDeadEnemyIds(const TArray<FName>& SavedIds)
{
	NativeDeadEnemyIds.Reset();
	for (FName Id : SavedIds) NativeRegisterEnemyDeath(Id);
}
