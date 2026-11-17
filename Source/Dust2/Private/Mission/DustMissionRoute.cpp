#include "Mission/DustMissionRoute.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Mission/DustGameSessionSubsystem.h"
#include "Inventory/DustPickupPersistenceSubsystem.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustEncounterComponent.h"
#include "Player/Dust2PlayerCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogDustMissionRoute, Log, All);

namespace
{
	UDustEncounterComponent* GetEncounter(AActor* Actor)
	{
		return IsValid(Actor) && !Actor->IsActorBeingDestroyed()
			? Actor->FindComponentByClass<UDustEncounterComponent>() : nullptr;
	}

	template <typename ComponentType>
	ComponentType* ResolveStepComponent(const FComponentReference& Reference, AActor* Region, ComponentType* Default)
	{
		return Reference == FComponentReference{}
			? Default : Cast<ComponentType>(Reference.GetComponent(Region));
	}
}

void FDustMissionRouteStep::MigrateLegacyActions()
{
	if (ActivateRegion)
	{
		NextRegion = ActivateRegion;
		bActivateNextRegion = true;
		ActivateRegion = nullptr;
	}
	if (Door || !DoorId.IsNone())
	{
		FDustRouteDoor& Entry = Doors.AddDefaulted_GetRef();
		Entry.Door = Door;
		Entry.DoorId = DoorId;
		bOpenDoors = true;
		Door = nullptr;
		DoorId = NAME_None;
	}
	if (C4)
	{
		C4Actor = C4;
		bStartC4 = true;
		C4 = nullptr;
	}
}

ADustMissionRoute::ADustMissionRoute()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADustMissionRoute::PostLoad()
{
	Super::PostLoad();
	for (FDustMissionRouteStep& Step : Steps)
	{
		Step.MigrateLegacyActions();
	}
}

FName ADustMissionRoute::GetMapId() const
{
	const UWorld* World = GetWorld();
	return World ? FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : NAME_None;
}

int32 ADustMissionRoute::ResolveResumeIndex(const FDustRouteCheckpoint& Checkpoint) const
{
	if (Checkpoint.MapId.IsNone()) return Steps.IsEmpty() ? INDEX_NONE : 0;
	if (Checkpoint.MapId != GetMapId()) return INDEX_NONE;
	if (Checkpoint.bRouteComplete) return Checkpoint.ResumeStepId.IsNone() ? Steps.Num() : INDEX_NONE;
	if (Checkpoint.ResumeStepId.IsNone()) return INDEX_NONE;
	return Steps.IndexOfByPredicate([&Checkpoint](const FDustMissionRouteStep& Step)
	{
		return Step.StepId == Checkpoint.ResumeStepId;
	});
}

bool ADustMissionRoute::ValidateSteps(int32 FirstActiveStep, bool bResumeOnlyC4) const
{
	TSet<FName> Ids;
	TMap<FName, const AActor*> DoorActorsById;
	const UWorld* World = GetWorld();
	for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
	{
		const FDustMissionRouteStep& Step = Steps[StepIndex];
		const auto Reject = [this, &Step](const TCHAR* Reason)
		{
			UE_LOG(LogDustMissionRoute, Warning, TEXT("%s has an invalid route step '%s': %s."),
				*GetName(), *Step.StepId.ToString(), Reason);
			return false;
		};

		if (Step.StepId.IsNone() || Ids.Contains(Step.StepId)) return Reject(TEXT("missing or duplicate Step Id"));
		Ids.Add(Step.StepId);
		// Completed actions may have destroyed their door actors before retry starts.
		if (StepIndex < FirstActiveStep) continue;

		UDustEncounterComponent* Region = GetEncounter(Step.Region);
		if (!Region || Region->GetWorld() != World) return Reject(TEXT("select a region in this map"));
		if (Step.bStartC4 && (!IsValid(Step.C4Actor) || Step.C4Actor->GetWorld() != World))
			return Reject(TEXT("select a C4 actor in this map"));
		if (bResumeOnlyC4 && StepIndex == FirstActiveStep) continue;

		if (Step.RequiresArrival())
		{
			const UBoxComponent* Box = ResolveStepComponent(Step.EntryBox, Step.Region.Get(), Region->GetRouteEntryBox());
			if (!IsValid(Box) || Box->GetWorld() != World) return Reject(TEXT("select an entry Box"));
		}
		if (Step.bSaveCheckpoint)
		{
			const UArrowComponent* Arrow = ResolveStepComponent(
				Step.RespawnArrow, Step.Region.Get(), Region->GetRouteRespawnArrow());
			if (!IsValid(Arrow) || Arrow->GetWorld() != World) return Reject(TEXT("select a respawn Arrow"));
		}
		if (Step.bActivateNextRegion)
		{
			const UDustEncounterComponent* Next = GetEncounter(Step.NextRegion);
			if (!Next || Next->GetWorld() != World || Next == Region || !Next->NativeIsDeferredRegionEnabled())
				return Reject(TEXT("select a different region with deferred spawning enabled"));
		}
		if (Step.bOpenDoors)
		{
			if (Step.Doors.IsEmpty()) return Reject(TEXT("add at least one door"));
			TSet<FName> StepDoorIds;
			for (const FDustRouteDoor& Door : Step.Doors)
			{
				if (!IsValid(Door.Door) || Door.Door->GetWorld() != World || Door.DoorId.IsNone())
					return Reject(TEXT("each door needs a door actor in this map and a Door Id"));
				const AActor* const* OtherDoor = DoorActorsById.Find(Door.DoorId);
				if (StepDoorIds.Contains(Door.DoorId) || (OtherDoor && *OtherDoor != Door.Door.Get()))
					return Reject(TEXT("duplicate Door Id"));
				StepDoorIds.Add(Door.DoorId);
				DoorActorsById.Add(Door.DoorId, Door.Door.Get());
			}
		}
	}
	return !Steps.IsEmpty();
}

bool ADustMissionRoute::StartMissionRoute(ADust2PlayerCharacter* InPlayer, const FDustRouteCheckpoint& Checkpoint)
{
	if (CurrentIndex != INDEX_NONE) return Player.Get() == InPlayer && IsValid(InPlayer) && !bEnded;
	UWorld* World = GetWorld();
	if (!World || !IsValid(InPlayer) || InPlayer->GetWorld() != World || InPlayer->IsDead() ||
		bEnded || bAdvancing) return false;
	ADust2GameMode* GameMode = World->GetAuthGameMode<ADust2GameMode>();
	UGameInstance* Instance = GetGameInstance();
	auto* Session = Instance ? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
	const FDustNativeRouteCheckpoint* Pending = Session ? Session->GetPendingRouteCheckpoint(GetMapId()) : nullptr;
	const FDustRouteCheckpoint& RouteState = Pending ? Pending->Route : Checkpoint;
	const int32 ResumeIndex = ResolveResumeIndex(RouteState);
	if (ResumeIndex == INDEX_NONE)
	{
		UE_LOG(LogDustMissionRoute, Warning, TEXT("%s cannot resume step '%s' from map '%s'."),
			*GetName(), *RouteState.ResumeStepId.ToString(), *RouteState.MapId.ToString());
		return false;
	}
	// A checkpoint saved immediately before arming resumes the same step at its C4 action.
	const bool bResumeOnlyC4 = Pending && Steps.IsValidIndex(ResumeIndex) &&
		Pending->CheckpointId == RouteState.ResumeStepId && Steps[ResumeIndex].bStartC4;
	if (!ValidateSteps(ResumeIndex, bResumeOnlyC4) || !GameMode || !GameMode->RegisterMissionRoute(this)) return false;

	Mode = GameMode;
	Player = InPlayer;
	if (Pending)
	{
		TGuardValue<bool> Guard(bAdvancing, true);
		AController* Controller = InPlayer->GetController();
		if (!World->HasBegunPlay() || !IsValid(Controller) || !GameMode->IsNativeRoutePlayerReady(InPlayer))
		{
			UE_LOG(LogDustMissionRoute, Warning, TEXT("%s is waiting for native checkpoint player initialization."),
				*GetName());
			return false;
		}

		const FDustNativeRouteCheckpoint Saved = *Pending;
		GameMode->NativeRestoreDeadEnemyIds(Saved.DeadEnemyIds);
		GameMode->RestoreMissionState(Saved.MissionState);
		TMap<UDustEncounterComponent*, bool> Regions;
		for (const FDustMissionRouteStep& Step : Steps)
		{
			if (UDustEncounterComponent* Region = GetEncounter(Step.Region))
			{
				Regions.FindOrAdd(Region) |= Step.bRequireClear;
			}
			if (Step.bActivateNextRegion)
			{
				if (UDustEncounterComponent* Next = GetEncounter(Step.NextRegion))
				{
					Regions.FindOrAdd(Next);
				}
			}
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UDustEncounterComponent* Region = GetEncounter(*It))
			{
				Regions.FindOrAdd(Region);
			}
		}
		for (const auto& Entry : Regions)
		{
			if (!IsValid(Entry.Key) || !Entry.Key->RestoreRouteCheckpointEnemies(Saved.DeadEnemyIds, Entry.Value))
			{
				UE_LOG(LogDustMissionRoute, Warning, TEXT("%s could not restore route region '%s'."),
					*GetName(), *GetNameSafe(Entry.Key));
				return false;
			}
		}
		if (bEnded || !Player.IsValid() || Player->IsDead() || !IsValid(Controller) ||
			Controller != Player->GetController()) return false;
		Controller->SetControlRotation(Saved.SpawnTransform.Rotator());
		Session->SetRouteRetryPending(false);
	}
	CurrentIndex = ResumeIndex;
	bAwaitingC4 = bResumeOnlyC4;
	BindCurrentStep();
	OnRouteTextChanged.Broadcast(GetCurrentRouteText());
	TryAdvance();
	return true;
}

bool ADustMissionRoute::PrepareMissionRouteRetry(const FDustRouteCheckpoint& Checkpoint)
{
	if (Checkpoint.MapId.IsNone()) return true;
	if (bEnded || ResolveResumeIndex(Checkpoint) == INDEX_NONE)
	{
		UE_LOG(LogDustMissionRoute, Warning, TEXT("%s rejected retry snapshot from '%s', step '%s'."),
			*GetName(), *Checkpoint.MapId.ToString(), *Checkpoint.ResumeStepId.ToString());
		return false;
	}
	UGameInstance* Instance = GetGameInstance();
	UDustPickupPersistenceSubsystem* Pickups = Instance
		? Instance->GetSubsystem<UDustPickupPersistenceSubsystem>() : nullptr;
	if (!Pickups) return false;
	Pickups->RestoreConsumedPickupIds(Checkpoint.MapId, Checkpoint.ConsumedPickupIds);
	return true;
}

FText ADustMissionRoute::GetCurrentRouteText() const
{
	const int32 Index = CurrentIndex == INDEX_NONE ? 0 : CurrentIndex;
	return Steps.IsValidIndex(Index) ? Steps[Index].ObjectiveText : CompletionText;
}

FName ADustMissionRoute::GetCurrentRouteStepId() const
{
	return Steps.IsValidIndex(CurrentIndex) ? Steps[CurrentIndex].StepId : NAME_None;
}

bool ADustMissionRoute::CanAdvance() const
{
	return !bEnded && Mode.IsValid() && !Mode->HasResolvedMissionResult() &&
		Player.IsValid() && !Player->IsActorBeingDestroyed() && !Player->IsDead() &&
		Steps.IsValidIndex(CurrentIndex);
}

bool ADustMissionRoute::IsApplyingC4(const AActor* C4) const
{
	return bAdvancing && CanAdvance() && IsValid(C4) && ApplyingC4.Get() == C4;
}

void ADustMissionRoute::BindCurrentStep()
{
	if (!Steps.IsValidIndex(CurrentIndex)) return;
	const FDustMissionRouteStep& Step = Steps[CurrentIndex];
	UDustEncounterComponent* Region = GetEncounter(Step.Region);
	if (!Region) return;
	BoundRegion = Region;
	if (Step.bRequireClear) Region->OnNativeEncounterCleared.AddUniqueDynamic(this, &ThisClass::HandleCleared);
	if (Step.RequiresArrival())
	{
		if (UBoxComponent* Box = ResolveStepComponent(Step.EntryBox, Step.Region.Get(), Region->GetRouteEntryBox()))
		{
			BoundBox = Box;
			Box->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::HandleEntered);
		}
	}
}

void ADustMissionRoute::UnbindCurrentStep()
{
	if (UPrimitiveComponent* Box = BoundBox.Get())
		Box->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::HandleEntered);
	if (UDustEncounterComponent* Region = BoundRegion.Get())
		Region->OnNativeEncounterCleared.RemoveDynamic(this, &ThisClass::HandleCleared);
	BoundBox.Reset();
	BoundRegion.Reset();
	bArrived = false;
	bAwaitingC4 = false;
}

void ADustMissionRoute::HandleEntered(UPrimitiveComponent* Overlapped, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit)
{
	if (bAdvancing || !CanAdvance() || Overlapped != BoundBox.Get() ||
		OtherActor != Player.Get() || OtherComponent != Player->GetCapsuleComponent()) return;
	bArrived = true;
	TryAdvance();
}

void ADustMissionRoute::HandleCleared()
{
	TryAdvance();
}

void ADustMissionRoute::RetryCurrentRouteStep()
{
	TryAdvance();
}

bool ADustMissionRoute::ApplyStep(const FDustMissionRouteStep& Step)
{
	if (!bAwaitingC4 && !ApplyStepActions(Step)) return false;
	if (!Step.bStartC4) return true;

	bAwaitingC4 = true;
	if (!IsValid(Step.C4Actor) || Step.C4Actor->IsActorBeingDestroyed() ||
		Mode->GetMissionState() == EMissionState::C4Planted) return false;

	TGuardValue<TWeakObjectPtr<AActor>> C4Guard(ApplyingC4, TWeakObjectPtr<AActor>(Step.C4Actor.Get()));
	Mode->RequestRouteC4(Step.C4Actor);
	if (!CanAdvance()) return false;
	if (Mode->GetMissionState() == EMissionState::C4Planted) return true;

	UE_LOG(LogDustMissionRoute, Warning, TEXT("%s requested C4 for '%s' but it did not arm."),
		*GetName(), *Step.StepId.ToString());
	return false;
}

bool ADustMissionRoute::ApplyStepActions(const FDustMissionRouteStep& Step)
{
	UDustGameSessionSubsystem* Session = nullptr;
	FDustNativeRouteCheckpoint Checkpoint;
	if (Step.bSaveCheckpoint)
	{
		const UArrowComponent* Arrow = ResolveStepComponent(Step.RespawnArrow, Step.Region.Get(),
			BoundRegion.IsValid() ? BoundRegion->GetRouteRespawnArrow() : nullptr);
		UGameInstance* Instance = GetGameInstance();
		Session = Instance ? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
		const auto* Pickups = Instance ? Instance->GetSubsystem<UDustPickupPersistenceSubsystem>() : nullptr;
		if (!IsValid(Arrow) || !Session || !Pickups || !Player->CaptureNativeRouteLoadout(Checkpoint))
		{
			UE_LOG(LogDustMissionRoute, Warning, TEXT("%s cannot save '%s' without a respawn arrow, session and ready player inventory."),
				*GetName(), *Step.StepId.ToString());
			return false;
		}

		Checkpoint.Route.MapId = GetMapId();
		// A retry must replay arming because the live C4 actor and fuse are not saved.
		const int32 NextIndex = Step.bStartC4 ? CurrentIndex : CurrentIndex + 1;
		Checkpoint.Route.bRouteComplete = !Steps.IsValidIndex(NextIndex);
		Checkpoint.Route.ResumeStepId = Checkpoint.Route.bRouteComplete ? NAME_None : Steps[NextIndex].StepId;
		Checkpoint.Route.ConsumedPickupIds = Pickups->GetConsumedPickupIds(Checkpoint.Route.MapId);
		Checkpoint.CheckpointId = Step.StepId;
		Checkpoint.SpawnTransform = FTransform(Arrow->GetComponentQuat(), Arrow->GetComponentLocation());
		Checkpoint.MissionState = Mode->GetMissionState();
		Checkpoint.DeadEnemyIds = Mode->NativeGetDeadEnemyIds();
		if (Checkpoint.Route.MapId.IsNone() || !Checkpoint.SpawnTransform.IsValid()) return false;
	}

	if (Step.bActivateNextRegion)
	{
		UDustEncounterComponent* Next = GetEncounter(Step.NextRegion);
		if (!Next || !Next->EnsureRouteRegionActive() || !CanAdvance()) return false;
	}

	if (Step.bOpenDoors)
	{
		for (const FDustRouteDoor& Door : Step.Doors)
		{
			if (!IsValid(Door.Door) || Door.Door->IsActorBeingDestroyed() ||
				!Mode->TryOpenRouteDoor(Door.Door, Door.DoorId))
			{
				UE_LOG(LogDustMissionRoute, Warning, TEXT("%s could not open door '%s' in step '%s'."),
					*GetName(), *Door.DoorId.ToString(), *Step.StepId.ToString());
				return false;
			}
			if (!CanAdvance()) return false;
		}
	}

	if (Session)
	{
		Session->SaveRouteCheckpoint(MoveTemp(Checkpoint));
		UE_LOG(LogDustMissionRoute, Log, TEXT("%s saved native checkpoint '%s'."),
			*GetName(), *Step.StepId.ToString());
	}

	return true;
}

void ADustMissionRoute::TryAdvance()
{
	if (bAdvancing) return;
	TGuardValue<bool> Guard(bAdvancing, true);
	while (CanAdvance() && BoundRegion.IsValid())
	{
		const FDustMissionRouteStep& Step = Steps[CurrentIndex];
		if (!bAwaitingC4 && ((Step.RequiresArrival() && !bArrived) ||
			(Step.bRequireClear && !BoundRegion->NativeIsEncounterCleared()))) return;
		if (!ApplyStep(Step))
		{
			UE_LOG(LogDustMissionRoute, Warning, TEXT("%s could not finish step '%s'; step retained."),
				*GetName(), *Step.StepId.ToString());
			return;
		}
		UnbindCurrentStep();
		++CurrentIndex;
		BindCurrentStep();
		OnRouteTextChanged.Broadcast(GetCurrentRouteText());
	}
}

void ADustMissionRoute::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnded = true;
	UnbindCurrentStep();
	if (ADust2GameMode* GameMode = Mode.Get()) GameMode->UnregisterMissionRoute(this);
	Super::EndPlay(EndPlayReason);
}
