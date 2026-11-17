#include "Mission/DustEncounterComponent.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"

#include "AI/DustEnemyPatrolComponent.h"
#include "AI/DustEnemyCombatAdapterComponent.h"
#include "AIController.h"
#include "../Combat/DustActorState.h"
#include "Combat/DustShotDamageReceiptLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Identity/DustStableIdComponent.h"
#include "Inventory/DustPickupPersistenceSubsystem.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustEncounterPersistenceSubsystem.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogDustDeferredEncounter, Log, All);

namespace
{
	bool IsUsableSpawnPoint(const AActor* Point, const UWorld* World)
	{
		return IsValid(Point) && !Point->IsActorBeingDestroyed() && Point->GetWorld() == World;
	}

	FTransform MakeSpawnTransform(const AActor* Point)
	{
		return FTransform(Point->GetActorRotation(), Point->GetActorLocation(), FVector::OneVector);
	}

	UClass* GetUsableClass(const TSubclassOf<AActor>& Class)
	{
		UClass* Raw = Class.Get();
		return Raw && !Raw->HasAnyClassFlags(CLASS_Abstract) ? Raw : nullptr;
	}

	UClass* GetUsablePawnClass(const TSubclassOf<APawn>& Class)
	{
		UClass* Raw = Class.Get();
		return Raw && Raw->IsChildOf(APawn::StaticClass()) && !Raw->HasAnyClassFlags(CLASS_Abstract)
			? Raw : nullptr;
	}
}

UDustEncounterComponent::UDustEncounterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UDustEncounterComponent::NativeInitializeEncounter(const TArray<AActor*>& Enemies)
{
	return InitializeEncounterInternal(Enemies, false);
}

bool UDustEncounterComponent::InitializeEncounterInternal(
	const TArray<AActor*>& Enemies, bool bFromDeferredActivation)
{
	if (bInitialized || bEndedPlay || !GetWorld()) return false;

	// A deferred region must never be initialized empty by the old BeginPlay path.
	// Activation and restore call this private entry with the flag set.
	if (bEnableDeferredRegion && !bFromDeferredActivation)
	{
		UE_LOG(LogDustDeferredEncounter, Warning,
			TEXT("%s uses a deferred region; legacy initialization was rejected. Activate or restore the region explicitly."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	for (AActor* Enemy : Enemies)
	{
		if (IsValid(Enemy) && !Enemy->IsActorBeingDestroyed() && Enemy != GetOwner() &&
			Enemy->GetWorld() == GetWorld())
		{
			RemainingEnemies.AddUnique(Enemy);
		}
	}
	bInitialized = true;
	bEncounterCleared = RemainingEnemies.IsEmpty();
	// An initially empty or restored encounter must not advance an objective.
	return true;
}

bool UDustEncounterComponent::NativeNotifyEnemyDied(AActor* Enemy)
{
	if (!bInitialized || bEndedPlay || !Enemy || RemainingEnemies.Remove(Enemy) == 0) return false;
	DustShotDamageReceipt::RecordEncounterDeath(Enemy, this);
	if (RemainingEnemies.IsEmpty() && !bEncounterCleared)
	{
		bEncounterCleared = true;
		if (SciFiObjectiveStep == 5)
		{
			if (UWorld* World = GetWorld())
			{
				if (ADust2GameMode* Mode = World->GetAuthGameMode<ADust2GameMode>())
				{
					Mode->NotifySciFiObjectiveStep(6, FText::GetEmpty());
				}
			}
		}
		OnNativeEncounterCleared.Broadcast();
	}
	return true;
}

void UDustEncounterComponent::NotifyNearestEnemiesOfPlayerKill(
    AActor* Player, const FVector& PlayerLocation, const FVector& VictimLocation, uint64 DeathSerial)
{
    if (!bInitialized || bEndedPlay || DustActorState::IsDead(Player) ||
        Player->GetWorld() != GetWorld() || PlayerLocation.ContainsNaN() ||
        VictimLocation.ContainsNaN() || DeathSerial == 0)
    {
        return;
    }

    TWeakObjectPtr<APawn> Nearest[2];
    double DistanceSquared[2] = { TNumericLimits<double>::Max(), TNumericLimits<double>::Max() };
    for (AActor* Enemy : RemainingEnemies)
    {
        APawn* Pawn = Cast<APawn>(Enemy);
        if (DustActorState::IsDead(Pawn) || Pawn == Player) continue;
        const double Distance = FVector::DistSquared(Pawn->GetActorLocation(), VictimLocation);
        if (Distance < DistanceSquared[0])
        {
            Nearest[1] = Nearest[0];
            DistanceSquared[1] = DistanceSquared[0];
            Nearest[0] = Pawn;
            DistanceSquared[0] = Distance;
        }
        else if (Distance < DistanceSquared[1])
        {
            Nearest[1] = Pawn;
            DistanceSquared[1] = Distance;
        }
    }

    const TWeakObjectPtr<AActor> PlayerWeak(Player);
    for (const TWeakObjectPtr<APawn>& Candidate : Nearest)
    {
        if (bEndedPlay || !PlayerWeak.IsValid()) break;
        APawn* Pawn = Candidate.Get();
        if (DustActorState::IsDead(Pawn) || !RemainingEnemies.Contains(Pawn)) continue;
        AAIController* Controller = Cast<AAIController>(Pawn->GetController());
        if (!IsValid(Controller)) continue;
        if (UDustEnemyCombatAdapterComponent* Adapter =
            Controller->FindComponentByClass<UDustEnemyCombatAdapterComponent>())
        {
            Adapter->ReceiveSquadKillAlert(PlayerWeak.Get(), PlayerLocation, DeathSerial);
        }
    }
}

bool UDustEncounterComponent::NativeRemoveEnemySilently(AActor* Enemy)
{
	if (!bInitialized || bEndedPlay || !Enemy || RemainingEnemies.Remove(Enemy) == 0) return false;
	bEncounterCleared = RemainingEnemies.IsEmpty();
	return true;
}

bool UDustEncounterComponent::NativeIsDeferredRegionActivated() const
{
	if (bEndedPlay || !bEnableDeferredRegion || DeferredRegion.RegionId.IsNone()) return false;
	if (bRegionActivated) return true;

	const UDustEncounterPersistenceSubsystem* Persistence = GetPersistence();
	return Persistence &&
		Persistence->IsRegionActivated(GetPersistenceLevelId(), DeferredRegion.RegionId);
}

FName UDustEncounterComponent::NativeGetActorStableId(AActor* Actor) const
{
	return UDustStableIdComponent::ResolveStableIdForActor(Actor);
}

UBoxComponent* UDustEncounterComponent::GetRouteEntryBox() const
{
    UBoxComponent* Box = Cast<UBoxComponent>(RouteEntryBox.GetComponent(GetOwner()));
    return Box && Box->GetOwner() == GetOwner() ? Box : nullptr;
}

UArrowComponent* UDustEncounterComponent::GetRouteRespawnArrow() const
{
    UArrowComponent* Arrow = Cast<UArrowComponent>(RouteRespawnArrow.GetComponent(GetOwner()));
    return Arrow && Arrow->GetOwner() == GetOwner() ? Arrow : nullptr;
}

bool UDustEncounterComponent::EnsureRouteRegionActive()
{
    if (!bEnableDeferredRegion || bEndedPlay) return false;
    if (bInitialized && bRegionActivated) return true;
    return NativeIsDeferredRegionActivated()
        ? NativeRestoreDeferredRegion() : NativeActivateDeferredRegion();
}

bool UDustEncounterComponent::RestoreRouteCheckpointEnemies(const TArray<FName>& DeadEnemyIds, bool bRequiresClear)
{
	if (bEndedPlay) return false;
	if (bEnableDeferredRegion)
	{
		return !NativeIsDeferredRegionActivated() || EnsureRouteRegionActive();
	}
	if (!bInitialized) return !bRequiresClear;

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> Removed;
	for (AActor* Enemy : RemainingEnemies)
	{
		if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed()) continue;
		const FName StableId = UDustStableIdComponent::ResolveStableIdForActor(Enemy);
		const FNameProperty* LegacyId = FindFProperty<FNameProperty>(Enemy->GetClass(), TEXT("EnemyId"));
		if (DeadEnemyIds.Contains(StableId) ||
			(LegacyId && DeadEnemyIds.Contains(LegacyId->GetPropertyValue_InContainer(Enemy))))
		{
			Removed.Add(Enemy);
		}
	}
	for (const TWeakObjectPtr<AActor>& Entry : Removed)
	{
		AActor* Enemy = Entry.Get();
		if (Enemy && NativeRemoveEnemySilently(Enemy))
		{
			Enemy->Destroy();
		}
	}
	return !bEndedPlay;
}

bool UDustEncounterComponent::NativeActivateDeferredRegion()
{
	if (!bEnableDeferredRegion || bEndedPlay || bInitialized || bRegionOperationInProgress)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return false;

	ADust2GameMode* Mode = World->GetAuthGameMode<ADust2GameMode>();
	if (Mode && !Mode->CanActivateSciFiObjectiveStep(SciFiObjectiveStep)) return false;

	FString Reason;
	if (!ValidateDeferredRegion(Reason) || !ValidateWorldUniqueness(Reason))
	{
		UE_LOG(LogDustDeferredEncounter, Warning, TEXT("%s cannot activate deferred region: %s"),
			*GetNameSafe(GetOwner()), *Reason);
		return false;
	}

	UDustEncounterPersistenceSubsystem* Persistence = GetPersistence();
	const FName LevelId = GetPersistenceLevelId();
	if (Persistence && Persistence->IsRegionActivated(LevelId, DeferredRegion.RegionId))
	{
		return false;
	}

	bRegionOperationInProgress = true;
	ON_SCOPE_EXIT
	{
		bRegionOperationInProgress = false;
	};

	TArray<AActor*> SpawnedEnemies;
	TArray<AActor*> SpawnedSupplies;
	const TSet<FName> NoDeadEnemyIds;
	if (!SpawnDeferredRegionActors(NoDeadEnemyIds, SpawnedEnemies, SpawnedSupplies))
	{
		DestroySpawnedActors(SpawnedEnemies);
		DestroySpawnedActors(SpawnedSupplies);
		return false;
	}

	if (!InitializeEncounterInternal(SpawnedEnemies, true))
	{
		DestroySpawnedActors(SpawnedEnemies);
		DestroySpawnedActors(SpawnedSupplies);
		return false;
	}

	bRegionActivated = true;
	if (Persistence && !Persistence->RecordRegionActivated(LevelId, DeferredRegion.RegionId))
	{
		UE_LOG(LogDustDeferredEncounter, Warning,
			TEXT("%s spawned region %s but the activation record was already present."),
			*GetNameSafe(GetOwner()), *DeferredRegion.RegionId.ToString());
	}
	if (Mode) Mode->NotifySciFiObjectiveStep(SciFiObjectiveStep, SciFiObjectiveText);
	return true;
}

bool UDustEncounterComponent::NativeRestoreDeferredRegion()
{
	if (!bEnableDeferredRegion || bEndedPlay || bInitialized || bRegionOperationInProgress)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return false;

	UDustEncounterPersistenceSubsystem* Persistence = GetPersistence();
	if (!Persistence || DeferredRegion.RegionId.IsNone() ||
		!Persistence->IsRegionActivated(GetPersistenceLevelId(), DeferredRegion.RegionId))
	{
		// Never spawn a region that never finished its first activation.
		return false;
	}

	FString Reason;
	if (!ValidateDeferredRegion(Reason) || !ValidateWorldUniqueness(Reason))
	{
		UE_LOG(LogDustDeferredEncounter, Warning, TEXT("%s cannot restore deferred region: %s"),
			*GetNameSafe(GetOwner()), *Reason);
		return false;
	}

	ADust2GameMode* Mode = FindGameMode();
	if (!Mode)
	{
		UE_LOG(LogDustDeferredEncounter, Warning,
			TEXT("%s cannot restore region %s without the Dust2 GameMode dead-id list."),
			*GetNameSafe(GetOwner()), *DeferredRegion.RegionId.ToString());
		return false;
	}

	TSet<FName> DeadEnemyIds;
	for (const FName& DeadId : Mode->NativeGetDeadEnemyIds())
	{
		if (!DeadId.IsNone()) DeadEnemyIds.Add(DeadId);
	}

	bRegionOperationInProgress = true;
	ON_SCOPE_EXIT
	{
		bRegionOperationInProgress = false;
	};

	TArray<AActor*> SpawnedEnemies;
	TArray<AActor*> SpawnedSupplies;
	if (!SpawnDeferredRegionActors(DeadEnemyIds, SpawnedEnemies, SpawnedSupplies))
	{
		DestroySpawnedActors(SpawnedEnemies);
		DestroySpawnedActors(SpawnedSupplies);
		return false;
	}

	if (!InitializeEncounterInternal(SpawnedEnemies, true))
	{
		DestroySpawnedActors(SpawnedEnemies);
		DestroySpawnedActors(SpawnedSupplies);
		return false;
	}

	// The region was already recorded; a restored empty set still initializes
	// without broadcasting OnNativeEncounterCleared.
	bRegionActivated = true;
	return true;
}

bool UDustEncounterComponent::ValidateDeferredRegion(FString& OutReason) const
{
	OutReason.Reset();
	if (!bEnableDeferredRegion)
	{
		OutReason = TEXT("deferred region is not enabled");
		return false;
	}
	if (DeferredRegion.RegionId.IsNone())
	{
		OutReason = TEXT("Region ID is empty");
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		OutReason = TEXT("not in a game world");
		return false;
	}

	TSet<FName> UsedEntryIds;
	auto ValidateEntryId = [&UsedEntryIds, &OutReason](const FName& EntryId, const TCHAR* Kind) -> bool
	{
		if (EntryId.IsNone())
		{
			OutReason = FString::Printf(TEXT("%s entry has no stable id"), Kind);
			return false;
		}
		if (UsedEntryIds.Contains(EntryId))
		{
			OutReason = FString::Printf(TEXT("duplicate entry id %s"), *EntryId.ToString());
			return false;
		}
		UsedEntryIds.Add(EntryId);
		return true;
	};

	for (const FDustDeferredEnemySpawnEntry& Entry : DeferredRegion.EnemySpawns)
	{
		if (!ValidateEntryId(Entry.EntryId, TEXT("enemy"))) return false;
		if (!GetUsablePawnClass(Entry.PawnClass))
		{
			OutReason = FString::Printf(TEXT("enemy entry %s has no usable Pawn class"), *Entry.EntryId.ToString());
			return false;
		}
		if (!IsUsableSpawnPoint(Entry.SpawnPoint.Get(), World))
		{
			OutReason = FString::Printf(TEXT("enemy entry %s has an invalid spawn point"), *Entry.EntryId.ToString());
			return false;
		}
		for (const TObjectPtr<AActor>& PatrolPoint : Entry.PatrolPoints)
		{
			if (!IsUsableSpawnPoint(PatrolPoint.Get(), World))
			{
				OutReason = FString::Printf(TEXT("enemy entry %s has an invalid patrol point"), *Entry.EntryId.ToString());
				return false;
			}
		}
	}

	for (const FDustDeferredSupplySpawnEntry& Entry : DeferredRegion.SupplySpawns)
	{
		if (!ValidateEntryId(Entry.EntryId, TEXT("supply"))) return false;
		if (!GetUsableClass(Entry.ActorClass))
		{
			OutReason = FString::Printf(TEXT("supply entry %s has no usable Actor class"), *Entry.EntryId.ToString());
			return false;
		}
		if (!IsUsableSpawnPoint(Entry.SpawnPoint.Get(), World))
		{
			OutReason = FString::Printf(TEXT("supply entry %s has an invalid spawn point"), *Entry.EntryId.ToString());
			return false;
		}
	}

	return true;
}

bool UDustEncounterComponent::ValidateWorldUniqueness(FString& OutReason) const
{
	OutReason.Reset();
	const UWorld* World = GetWorld();
	if (!World) return false;

	TSet<FName> MyEntryIds;
	for (const FDustDeferredEnemySpawnEntry& Entry : DeferredRegion.EnemySpawns)
	{
		MyEntryIds.Add(Entry.EntryId);
	}
	for (const FDustDeferredSupplySpawnEntry& Entry : DeferredRegion.SupplySpawns)
	{
		MyEntryIds.Add(Entry.EntryId);
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == GetOwner()) continue;

		TInlineComponentArray<UDustEncounterComponent*> OtherEncounters;
		Actor->GetComponents(OtherEncounters);
		for (const UDustEncounterComponent* Other : OtherEncounters)
		{
			if (!Other || Other == this || !Other->bEnableDeferredRegion) continue;

			if (!Other->DeferredRegion.RegionId.IsNone() &&
				Other->DeferredRegion.RegionId == DeferredRegion.RegionId)
			{
				OutReason = FString::Printf(TEXT("region id %s is also used by %s"),
					*DeferredRegion.RegionId.ToString(), *GetNameSafe(Actor));
				return false;
			}

			// Keep identity checks explicit: the same entry id in another live
			// region would make GameMode dead-id filtering ambiguous.
			for (const FDustDeferredEnemySpawnEntry& OtherEntry : Other->DeferredRegion.EnemySpawns)
			{
				if (MyEntryIds.Contains(OtherEntry.EntryId))
				{
					OutReason = FString::Printf(TEXT("entry id %s is also used by %s"),
						*OtherEntry.EntryId.ToString(), *GetNameSafe(Actor));
					return false;
				}
			}
			for (const FDustDeferredSupplySpawnEntry& OtherEntry : Other->DeferredRegion.SupplySpawns)
			{
				if (MyEntryIds.Contains(OtherEntry.EntryId))
				{
					OutReason = FString::Printf(TEXT("entry id %s is also used by %s"),
						*OtherEntry.EntryId.ToString(), *GetNameSafe(Actor));
					return false;
				}
			}
		}
	}
	return true;
}

bool UDustEncounterComponent::SpawnDeferredRegionActors(
	const TSet<FName>& DeadEnemyIds, TArray<AActor*>& OutEnemies, TArray<AActor*>& OutSupplies)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner) return false;

	for (const FDustDeferredEnemySpawnEntry& Entry : DeferredRegion.EnemySpawns)
	{
		if (DeadEnemyIds.Contains(Entry.EntryId)) continue;

		AActor* Point = Entry.SpawnPoint.Get();
		if (!IsUsableSpawnPoint(Point, World)) return false;

		const FTransform Transform = MakeSpawnTransform(Point);
		APawn* Pawn = World->SpawnActorDeferred<APawn>(Entry.PawnClass.Get(), Transform, Owner, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogDustDeferredEncounter, Warning, TEXT("Failed to spawn enemy entry %s"),
				*Entry.EntryId.ToString());
			return false;
		}

		UDustStableIdComponent::SetDeferredStableId(Pawn, Entry.EntryId);

		if (Pawn->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) &&
			!OnConfigureDeferredPawn.IsBound())
		{
			UE_LOG(LogDustDeferredEncounter, Warning,
				TEXT("%s must bind OnConfigureDeferredPawn before activating a Blueprint enemy."),
				*GetNameSafe(Owner));
			Pawn->Destroy();
			return false;
		}
		OnConfigureDeferredPawn.Broadcast(Pawn, Entry.EntryId);
		if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return false;

		Pawn->FinishSpawning(Transform);
		if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed())
		{
			UE_LOG(LogDustDeferredEncounter, Warning, TEXT("Enemy entry %s was destroyed during FinishSpawning"),
				*Entry.EntryId.ToString());
			return false;
		}

		UDustStableIdComponent::EnsureStableComponent(Pawn, Entry.EntryId);
		WarnIfAIDidNotStart(Pawn);
		OutEnemies.Add(Pawn);
	}

	const UGameInstance* Instance = World->GetGameInstance();
	const UDustPickupPersistenceSubsystem* Pickups = Instance
		? Instance->GetSubsystem<UDustPickupPersistenceSubsystem>() : nullptr;
	const FName LevelId = GetPersistenceLevelId();
	for (const FDustDeferredSupplySpawnEntry& Entry : DeferredRegion.SupplySpawns)
	{
		if (Pickups && Pickups->IsConsumed(LevelId, Entry.EntryId)) continue;

		AActor* Point = Entry.SpawnPoint.Get();
		if (!IsUsableSpawnPoint(Point, World)) return false;

		const FTransform Transform = MakeSpawnTransform(Point);
		AActor* Supply = World->SpawnActorDeferred<AActor>(Entry.ActorClass.Get(), Transform, Owner, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Supply))
		{
			UE_LOG(LogDustDeferredEncounter, Warning, TEXT("Failed to spawn supply entry %s"),
				*Entry.EntryId.ToString());
			return false;
		}

		// The pickup-persistence BeginPlay check needs this id before FinishSpawning.
		UDustStableIdComponent::SetDeferredStableId(Supply, Entry.EntryId);
		Supply->FinishSpawning(Transform);

		// A consumed placed pickup destroys itself in BeginPlay. That is the
		// intended silent skip, not a half-failure.
		if (!IsValid(Supply) || Supply->IsActorBeingDestroyed()) continue;

		UDustStableIdComponent::EnsureStableComponent(Supply, Entry.EntryId);
		OutSupplies.Add(Supply);
	}

	return true;
}

const FDustDeferredEnemySpawnEntry* UDustEncounterComponent::FindInitializingEnemyEntry(AActor* Pawn)
{
	if (!IsValid(Pawn)) return nullptr;
	const AActor* Region = Pawn->GetOwner();
	const UDustEncounterComponent* Encounter = IsValid(Region)
		? Region->FindComponentByClass<UDustEncounterComponent>() : nullptr;
	if (!Encounter || !Encounter->bRegionOperationInProgress) return nullptr;

	const FName Id = UDustStableIdComponent::ResolveStableIdForActor(Pawn);
	if (Id.IsNone()) return nullptr;
	return Encounter->DeferredRegion.EnemySpawns.FindByPredicate(
		[Id](const FDustDeferredEnemySpawnEntry& Entry) { return Entry.EntryId == Id; });
}

bool UDustEncounterComponent::NativeGetDeferredArmorOverride(
	AActor* Pawn, bool& HasBodyArmor, bool& HasHelmet) const
{
	HasBodyArmor = false;
	HasHelmet = false;
	if (!IsValid(Pawn) || Pawn->GetOwner() != GetOwner()) return false;

	const FDustDeferredEnemySpawnEntry* Entry = FindInitializingEnemyEntry(Pawn);
	if (!Entry || !Entry->bOverrideArmor) return false;

	HasBodyArmor = Entry->bHasBodyArmor;
	HasHelmet = Entry->bHasHelmet;
	return true;
}

void UDustEncounterComponent::ConfigureDeferredPatrol(
	AActor* Pawn, UDustEnemyPatrolComponent& Patrol) const
{
	if (const FDustDeferredEnemySpawnEntry* Entry = FindInitializingEnemyEntry(Pawn))
	{
		Patrol.PatrolPoints = Entry->PatrolPoints;
	}
}

void UDustEncounterComponent::WarnIfAIDidNotStart(APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->GetController() || !Pawn->AIControllerClass ||
		Pawn->AutoPossessAI == EAutoPossessAI::Disabled)
	{
		return;
	}

	if (Pawn->AutoPossessAI == EAutoPossessAI::PlacedInWorld)
	{
		UE_LOG(LogDustDeferredEncounter, Warning,
			TEXT("Spawned pawn %s did not start AI because Auto Possess AI is Placed In World. "
				 "Set BP_EnemyCharacter Auto Possess AI to Placed In World Or Spawned."),
			*GetNameSafe(Pawn));
	}
	else
	{
		UE_LOG(LogDustDeferredEncounter, Warning,
			TEXT("Spawned pawn %s has Auto Possess AI %d but no controller; check AIControllerClass."),
			*GetNameSafe(Pawn), static_cast<int32>(Pawn->AutoPossessAI));
	}
}

void UDustEncounterComponent::DestroySpawnedActors(TArray<AActor*>& Actors)
{
	for (AActor* Actor : Actors)
	{
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}
	Actors.Reset();
}

void UDustEncounterComponent::NormalizeSpawnEntryIds(bool bRegenerateRegion, bool bRegenerateEntries)
{
	if (!bEnableDeferredRegion) return;

	if (bRegenerateRegion || DeferredRegion.RegionId.IsNone())
	{
		DeferredRegion.RegionId = UDustStableIdComponent::GenerateStableId(TEXT("Region"));
	}

	TSet<FName> UsedIds;
	auto FixEntryId = [&UsedIds, bRegenerateEntries](FName& EntryId, const TCHAR* Prefix)
	{
		if (bRegenerateEntries || EntryId.IsNone() || UsedIds.Contains(EntryId))
		{
			do
			{
				EntryId = UDustStableIdComponent::GenerateStableId(Prefix);
			}
			while (UsedIds.Contains(EntryId));
		}
		UsedIds.Add(EntryId);
	};

	for (FDustDeferredEnemySpawnEntry& Entry : DeferredRegion.EnemySpawns)
	{
		FixEntryId(Entry.EntryId, TEXT("Enemy"));
	}
	for (FDustDeferredSupplySpawnEntry& Entry : DeferredRegion.SupplySpawns)
	{
		FixEntryId(Entry.EntryId, TEXT("Supply"));
	}
}

bool UDustEncounterComponent::NativeEnsureSpawnEntryIds()
{
	if (!bEnableDeferredRegion || HasAnyFlags(RF_ClassDefaultObject)) return false;
	if (const AActor* Owner = GetOwner(); Owner && Owner->HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}
	if (IsEditorConfigContext())
	{
		Modify();
	}
	NormalizeSpawnEntryIds(false, false);
	return true;
}

bool UDustEncounterComponent::IsEditorConfigContext() const
{
	if (HasAnyFlags(RF_ClassDefaultObject)) return false;
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAnyFlags(RF_ClassDefaultObject)) return false;
	const UWorld* World = GetWorld();
	return World && World->IsEditorWorld();
}

FName UDustEncounterComponent::GetPersistenceLevelId() const
{
	const UWorld* World = GetWorld();
	return World
		? FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()))
		: NAME_None;
}

UDustEncounterPersistenceSubsystem* UDustEncounterComponent::GetPersistence() const
{
	if (PersistenceOverride) return PersistenceOverride;
	const UWorld* World = GetWorld();
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UDustEncounterPersistenceSubsystem>() : nullptr;
}

ADust2GameMode* UDustEncounterComponent::FindGameMode() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	if (ADust2GameMode* Mode = World->GetAuthGameMode<ADust2GameMode>())
	{
		return Mode;
	}
	for (TActorIterator<ADust2GameMode> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void UDustEncounterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndedPlay = true;
	bInitialized = false;
	bEncounterCleared = false;
	bRegionActivated = false;
	bRegionOperationInProgress = false;
	RemainingEnemies.Reset();
	OnNativeEncounterCleared.Clear();
	OnConfigureDeferredPawn.Clear();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UDustEncounterComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (bEnableDeferredRegion && IsEditorConfigContext())
	{
		const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDustEncounterComponent, bEnableDeferredRegion) ||
			MemberName == GET_MEMBER_NAME_CHECKED(UDustEncounterComponent, DeferredRegion))
		{
			NormalizeSpawnEntryIds(false, false);
		}
	}
	// The component can be reconstructed by the superclass. Update serialized values first.
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDustEncounterComponent::PostEditImport()
{
	Super::PostEditImport();

	if (bEnableDeferredRegion && IsEditorConfigContext())
	{
		// Copy/paste of the encounter must not duplicate its region identity.
		NormalizeSpawnEntryIds(true, true);
	}
}

void UDustEncounterComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// PIE world duplication and reload keep the serialized config.
	if (bDuplicateForPIE || !bEnableDeferredRegion || !IsEditorConfigContext()) return;
	NormalizeSpawnEntryIds(true, true);
}
