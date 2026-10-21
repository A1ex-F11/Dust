#include "DustEnemyStateTreeTasks.h"

#include "AIController.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyWeaponComponent.h"
#include "DustEnemyPatrolComponent.h"
#include "DustEnemyReloadComponent.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "WeaponRuntime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DustEnemyStateTreeTasks)

FDustEnemyCombatTask::FDustEnemyCombatTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnExitState = true;
}

EStateTreeRunStatus FDustEnemyCombatTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FDustEnemyCombatTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bStartedEngagement = false;
	InstanceData.StartedWeaponComponent = nullptr;
	InstanceData.StartedTargetActor = nullptr;

	if (!InstanceData.WeaponComponent || !InstanceData.TargetActor)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.WeaponComponent->StartEngagement(InstanceData.TargetActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bStartedEngagement = true;
	InstanceData.StartedWeaponComponent = InstanceData.WeaponComponent;
	InstanceData.StartedTargetActor = InstanceData.TargetActor;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDustEnemyCombatTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FDustEnemyCombatTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.bStartedEngagement
		|| !InstanceData.WeaponComponent
		|| InstanceData.WeaponComponent != InstanceData.StartedWeaponComponent
		|| !InstanceData.TargetActor
		|| InstanceData.TargetActor != InstanceData.StartedTargetActor
		|| InstanceData.WeaponComponent->GetEngagementTarget() != InstanceData.StartedTargetActor
		|| !InstanceData.WeaponComponent->IsEngaging())
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

void FDustEnemyCombatTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FDustEnemyCombatTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.bStartedEngagement
		&& InstanceData.StartedWeaponComponent
		&& InstanceData.StartedWeaponComponent->GetEngagementTarget() == InstanceData.StartedTargetActor)
	{
		InstanceData.StartedWeaponComponent->StopEngagement();
	}

	InstanceData.bStartedEngagement = false;
	InstanceData.StartedWeaponComponent = nullptr;
	InstanceData.StartedTargetActor = nullptr;
}

FDustEnemySettleTask::FDustEnemySettleTask()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FDustEnemySettleTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FDustEnemySettleTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.SettledTime = 0.0f;

	if (!InstanceData.Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	AAIController* AIController = Cast<AAIController>(InstanceData.Pawn->GetController());
	if (!AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	AIController->StopMovement();

	const float RequiredSettleSeconds = FMath::Max(0.0f, InstanceData.SettleSeconds);
	const float MaxSpeed = FMath::Max(0.0f, InstanceData.MaxSpeed);
	if (RequiredSettleSeconds <= 0.0f && InstanceData.Pawn->GetVelocity().SizeSquared() <= FMath::Square(MaxSpeed))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDustEnemySettleTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FDustEnemySettleTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Pawn || !Cast<AAIController>(InstanceData.Pawn->GetController()))
	{
		return EStateTreeRunStatus::Failed;
	}

	const float RequiredSettleSeconds = FMath::Max(0.0f, InstanceData.SettleSeconds);
	const float MaxSpeed = FMath::Max(0.0f, InstanceData.MaxSpeed);
	if (InstanceData.Pawn->GetVelocity().SizeSquared() <= FMath::Square(MaxSpeed))
	{
		InstanceData.SettledTime += FMath::Max(0.0f, DeltaTime);
		return InstanceData.SettledTime >= RequiredSettleSeconds
			? EStateTreeRunStatus::Succeeded
			: EStateTreeRunStatus::Running;
	}

	InstanceData.SettledTime = 0.0f;
	return EStateTreeRunStatus::Running;
}

void FDustEnemySettleTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// No controller or movement state is owned after the settle condition completes.
}

FDustEnemyPatrolTask::FDustEnemyPatrolTask()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FDustEnemyPatrolTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	Data.StartedComponent = Data.PatrolComponent;
	Data.MoveToken = IsValid(Data.StartedComponent) ? Data.StartedComponent->BeginPatrolMove() : 0;
	return Data.MoveToken != 0 ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FDustEnemyPatrolTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	auto& Data = Context.GetInstanceData(*this);
	if (!IsValid(Data.StartedComponent) || Data.StartedComponent != Data.PatrolComponent || Data.MoveToken == 0)
		return EStateTreeRunStatus::Failed;
	const EDustPatrolMoveStatus Status = Data.StartedComponent->UpdatePatrolMove(Data.MoveToken, DeltaTime);
	if (Status == EDustPatrolMoveStatus::Running) return EStateTreeRunStatus::Running;
	Data.StartedComponent->CompletePatrolMove(Data.MoveToken);
	Data.MoveToken = 0;
	return Status == EDustPatrolMoveStatus::Succeeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

void FDustEnemyPatrolTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	if (IsValid(Data.StartedComponent)) Data.StartedComponent->CancelPatrolMove(Data.MoveToken);
	Data.MoveToken = 0;
	Data.StartedComponent = nullptr;
}

FDustEnemyReloadTask::FDustEnemyReloadTask()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FDustEnemyReloadTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	return Data.StartOwnedReload() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FDustEnemyReloadTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Context.GetInstanceData(*this).TickOwnedReload(DeltaTime);
}

void FDustEnemyReloadTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	Context.GetInstanceData(*this).CancelOwnedReloadAndRetreat();
}

bool FDustEnemyReloadTaskInstanceData::StartOwnedReload()
{
	const uint64 ExpectedGeneration = OperationGeneration + 1;
	CancelOwnedReloadAndRetreat();
	if (OperationGeneration != ExpectedGeneration) return false;
	const uint64 EntryGeneration = OperationGeneration;
	StartedComponent = ReloadComponent;
	StartedWeaponComponent = nullptr;
	StartedRuntime.Reset();
	ReloadRequestId = 0;
	ReloadToken = 0;
	StartedAdapter = nullptr;
	RetreatToken = 0;
	bRetreatAttempted = false;

	if (!IsValid(StartedComponent)) return false;
	StartedWeaponComponent = StartedComponent->GetWeaponComponent();
	if (!IsValid(StartedWeaponComponent)) return false;

	// Capture the actual controller-side adapter once per entry. The retreat is best-effort:
	// a missing adapter never changes the original reload transaction.
	if (const APawn* Pawn = Cast<APawn>(StartedComponent->GetOwner()))
	{
		if (const AAIController* Controller = Cast<AAIController>(Pawn->GetController()))
			StartedAdapter = Controller->FindComponentByClass<UDustEnemyCombatAdapterComponent>();
	}

	// Stop any shooting before the reload transaction starts.
	StartedWeaponComponent->StopEngagement();
	AWeaponRuntime* Runtime = StartedWeaponComponent->GetWeaponRuntime();
	if (!IsValid(Runtime)) return false;
	StartedRuntime = Runtime;

	UDustEnemyReloadComponent* Reload = StartedComponent;
	const uint64 NewToken = Reload->TryStartReload();
	if (OperationGeneration != EntryGeneration)
	{
		if (IsValid(Reload) && NewToken != 0) Reload->CancelReload(NewToken);
		return false;
	}
	ReloadToken = NewToken;
	if (ReloadToken == 0) return false;

	ReloadRequestId = Runtime->GetReloadRequestId();
	if (ReloadRequestId <= 0)
	{
		StartedComponent->CancelReload(ReloadToken);
		ReloadToken = 0;
		return false;
	}
	return true;
}

EStateTreeRunStatus FDustEnemyReloadTaskInstanceData::TickOwnedReload(float DeltaTime)
{
	const uint64 EntryGeneration = OperationGeneration;
	if (!IsValid(StartedComponent) || StartedComponent != ReloadComponent || ReloadToken == 0)
	{
		CancelOwnedReloadAndRetreat();
		return EStateTreeRunStatus::Failed;
	}

	// Advance the original transaction first: UpdateReload revalidates runtime, request and
	// definition identity. A completed/failed reload never starts a new retreat.
	const EDustEnemyReloadStatus ReloadStatus = StartedComponent->UpdateReload(ReloadToken, DeltaTime);
	if (OperationGeneration != EntryGeneration) return EStateTreeRunStatus::Failed;
	if (ReloadStatus == EDustEnemyReloadStatus::Failed)
	{
		CancelOwnedReloadAndRetreat();
		return EStateTreeRunStatus::Failed;
	}

	if (ReloadStatus == EDustEnemyReloadStatus::Running)
	{
		// Try exactly once per entry, and only while the real reload is still progressing.
		// A far/cooldown/occupied query leaves bRetreatAttempted false so a later approach can try.
		if (!bRetreatAttempted && IsValid(StartedAdapter) && StartedAdapter->CanCreateSpaceForReload())
		{
			UDustEnemyCombatAdapterComponent* Adapter = StartedAdapter;
			bRetreatAttempted = true;
			const uint64 NewToken = Adapter->BeginCreateSpaceForReload();
			if (OperationGeneration == EntryGeneration && StartedAdapter == Adapter && bRetreatAttempted)
				RetreatToken = NewToken;
			else if (IsValid(Adapter) && NewToken != 0)
				Adapter->CancelCreateSpace(NewToken);
			if (OperationGeneration != EntryGeneration) return EStateTreeRunStatus::Failed;
		}

		if (RetreatToken != 0)
		{
			UDustEnemyCombatAdapterComponent* Adapter = StartedAdapter;
			const uint64 OwnedToken = RetreatToken;
			if (!IsValid(Adapter))
			{
				RetreatToken = 0;
			}
			else
			{
				const EDustCreateSpaceStatus MoveStatus = Adapter->UpdateCreateSpace(OwnedToken, DeltaTime);
				if (OperationGeneration != EntryGeneration) return EStateTreeRunStatus::Failed;
				// Invalidate our token before release callbacks so a stale cleanup cannot clear
				// a move installed by a newer entry.
				if (RetreatToken == OwnedToken)
				{
					if (MoveStatus == EDustCreateSpaceStatus::Succeeded)
					{
						RetreatToken = 0;
						Adapter->CompleteCreateSpace(OwnedToken);
					}
					else if (MoveStatus == EDustCreateSpaceStatus::Failed)
					{
						RetreatToken = 0;
						Adapter->CancelCreateSpace(OwnedToken);
					}
				}
			}
		}
		return EStateTreeRunStatus::Running;
	}

	// Commit only the captured runtime + request id + token, and only while the runtime is still reloading.
	AWeaponRuntime* Runtime = StartedRuntime.Get();
	const bool bIdentityValid = IsValid(Runtime) && IsValid(StartedWeaponComponent)
		&& StartedWeaponComponent->GetWeaponRuntime() == Runtime
		&& Runtime->GetReloadRequestId() == ReloadRequestId
		&& Runtime->IsReloading();
	if (!bIdentityValid)
	{
		CancelOwnedReloadAndRetreat();
		return EStateTreeRunStatus::Failed;
	}

	// Invalidate both handles BEFORE FinishReload can run synchronous external callbacks.
	// FinishReload already abandons this exact transaction on failure; stale tokens only no-op.
	UDustEnemyReloadComponent* Reload = StartedComponent;
	UDustEnemyCombatAdapterComponent* Adapter = StartedAdapter;
	const uint64 CompletingToken = ReloadToken;
	const uint64 CompletingRetreatToken = RetreatToken;
	ReloadToken = 0;
	RetreatToken = 0;

	const bool bFinished = IsValid(Reload) && Reload->FinishReload(CompletingToken);
	if (IsValid(Adapter) && CompletingRetreatToken != 0)
		Adapter->CancelCreateSpace(CompletingRetreatToken);

	// The StateTree owns the handoff back to normal combat; this task only ends its reloading
	// state and never restarts shooting or extends the state.
	return bFinished ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

void FDustEnemyReloadTaskInstanceData::CancelOwnedReloadAndRetreat()
{
	++OperationGeneration;
	UDustEnemyCombatAdapterComponent* OldAdapter = StartedAdapter;
	UDustEnemyReloadComponent* OldReload = StartedComponent;
	const uint64 OldRetreatToken = RetreatToken;
	const uint64 OldReloadToken = ReloadToken;

	// Invalidate every handle BEFORE either cancel can run synchronous callbacks; a reentrant
	// new entry then keeps the fields it installed.
	StartedAdapter = nullptr;
	RetreatToken = 0;
	bRetreatAttempted = false;
	StartedComponent = nullptr;
	StartedWeaponComponent = nullptr;
	StartedRuntime.Reset();
	ReloadRequestId = 0;
	ReloadToken = 0;

	if (IsValid(OldAdapter) && OldRetreatToken != 0) OldAdapter->CancelCreateSpace(OldRetreatToken);
	// Cancel only this task's identical transaction; a newer request is never touched.
	if (IsValid(OldReload) && OldReloadToken != 0) OldReload->CancelReload(OldReloadToken);
}

FDustEnemyReacquireTask::FDustEnemyReacquireTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnExitState = true;
}

EStateTreeRunStatus FDustEnemyReacquireTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	Data.StartedAdapter = Data.Adapter;
	Data.StartedTarget = nullptr;
	Data.AdapterGeneration = 0;
	Data.FocusLocation = FVector::ZeroVector;
	Data.LastFocusPoint = FVector::ZeroVector;
	Data.FocusYaw = 0.0f;
	Data.bHasFocusPoint = false;
	if (!IsValid(Data.StartedAdapter)) return EStateTreeRunStatus::Failed;
	AAIController* Controller = Data.StartedAdapter->GetAIController();
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!IsValid(Controller) || !IsValid(Pawn)) return EStateTreeRunStatus::Failed;
	// A confirmation that arrived before this state started means there is nothing to reacquire.
	if (Data.StartedAdapter->HasConfirmedTarget()) return EStateTreeRunStatus::Succeeded;
	if (!Data.StartedAdapter->NeedsReacquire()) return EStateTreeRunStatus::Failed;
	Data.StartedTarget = Data.StartedAdapter->GetKnownTarget();
	if (!IsValid(Data.StartedTarget)) return EStateTreeRunStatus::Failed;
	Data.AdapterGeneration = Data.StartedAdapter->GetKnowledgeGeneration();
	Data.FocusLocation = Data.StartedAdapter->GetLastKnownTargetLocation();
	// Own the "stay in place" part of this state; ExitState never touches movement.
	Controller->StopMovement();
	if (!Data.StartedAdapter->NeedsReacquire()
		|| Data.StartedAdapter->GetKnowledgeGeneration() != Data.AdapterGeneration)
		return EStateTreeRunStatus::Failed;
	FVector Eye;
	FRotator EyeRotation;
	Pawn->GetActorEyesViewPoint(Eye, EyeRotation);
	// Start from the actual body facing so entering the state never snaps a turn.
	Data.FocusYaw = FRotator::NormalizeAxis(Pawn->GetActorRotation().Yaw);
	const FVector ToFocus = Data.FocusLocation - Eye;
	const float FocusDistance = FMath::Max(100.0f, ToFocus.Size2D());
	const FVector InitialFocus = Eye + FRotator(0.0f, Data.FocusYaw, 0.0f).Vector() * FocusDistance;
	Controller->SetFocalPoint(InitialFocus, EAIFocusPriority::Gameplay);
	Data.LastFocusPoint = InitialFocus;
	Data.bHasFocusPoint = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDustEnemyReacquireTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	auto& Data = Context.GetInstanceData(*this);
	if (!IsValid(Data.StartedAdapter) || Data.StartedAdapter != Data.Adapter)
		return EStateTreeRunStatus::Failed;
	if (Data.StartedAdapter->HasConfirmedTarget())
	{
		return Data.StartedAdapter->GetConfirmedTarget() == Data.StartedTarget.Get()
			? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}
	if (Data.StartedAdapter->GetKnowledgeGeneration() != Data.AdapterGeneration
		|| !Data.StartedAdapter->NeedsReacquire() || !IsValid(Data.StartedTarget)
		|| Data.StartedAdapter->GetKnownTarget() != Data.StartedTarget.Get())
		return EStateTreeRunStatus::Failed;
	AAIController* Controller = Data.StartedAdapter->GetAIController();
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!IsValid(Controller) || !IsValid(Pawn)) return EStateTreeRunStatus::Failed;
	// The frozen location cannot chase the live actor while hidden.
	Data.FocusLocation = Data.StartedAdapter->GetLastKnownTargetLocation();
	FVector Eye;
	FRotator EyeRotation;
	Pawn->GetActorEyesViewPoint(Eye, EyeRotation);
	const FVector ToFocus = Data.FocusLocation - Eye;
	const float DesiredYaw = ToFocus.GetSafeNormal2D().IsNearlyZero()
		? Data.FocusYaw : ToFocus.Rotation().Yaw;
	const float TurnRate = FMath::IsFinite(Data.TurnSpeedDegPerSecond)
		? FMath::Max(0.0f, Data.TurnSpeedDegPerSecond) : 0.0f;
	const float SafeDelta = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0f, DeltaTime) : 0.0f;
	Data.FocusYaw = FMath::FixedTurn(Data.FocusYaw, DesiredYaw, TurnRate * SafeDelta);
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(Data.FocusYaw, DesiredYaw)) <= 1.0f)
	{
		Controller->SetFocalPoint(Data.FocusLocation, EAIFocusPriority::Gameplay);
		Data.LastFocusPoint = Data.FocusLocation;
	}
	else
	{
		const float FocusDistance = FMath::Max(100.0f, ToFocus.Size2D());
		FVector FocusPoint = Eye + FRotator(0.0f, Data.FocusYaw, 0.0f).Vector() * FocusDistance;
		FocusPoint.Z = Data.FocusLocation.Z;
		Controller->SetFocalPoint(FocusPoint, EAIFocusPriority::Gameplay);
		Data.LastFocusPoint = FocusPoint;
	}
	Data.bHasFocusPoint = true;
	return EStateTreeRunStatus::Running;
}

void FDustEnemyReacquireTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	// Old exits must not clear a focus/move installed by a newer state. Generation plus the exact
	// focal point we last wrote both have to still match.
	AAIController* Controller = IsValid(Data.StartedAdapter) ? Data.StartedAdapter->GetAIController() : nullptr;
	if (IsValid(Controller) && IsValid(Data.StartedAdapter)
		&& Data.StartedAdapter->GetKnowledgeGeneration() == Data.AdapterGeneration
		&& Data.StartedAdapter->NeedsReacquire()
		&& Data.bHasFocusPoint
		&& Controller->GetFocalPoint().Equals(Data.LastFocusPoint, 1.0f))
	{
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
	Data.StartedAdapter = nullptr;
	Data.StartedTarget = nullptr;
	Data.AdapterGeneration = 0;
	Data.FocusLocation = FVector::ZeroVector;
	Data.LastFocusPoint = FVector::ZeroVector;
	Data.bHasFocusPoint = false;
}

FDustEnemyCreateSpaceTask::FDustEnemyCreateSpaceTask()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FDustEnemyCreateSpaceTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	Data.CancelOwnedOperations();
	Data.StartedAdapter = Data.Adapter;
	AAIController* Controller = IsValid(Data.StartedAdapter) ? Data.StartedAdapter->GetAIController() : nullptr;
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!IsValid(Pawn)) return EStateTreeRunStatus::Failed;
	Data.StartedWeapon = Pawn->FindComponentByClass<UDustEnemyWeaponComponent>();
	Data.StartedReload = Pawn->FindComponentByClass<UDustEnemyReloadComponent>();
	Data.StartedTarget = Data.StartedAdapter->GetConfirmedTarget();
	Data.StartedRuntime = IsValid(Data.StartedWeapon) ? Data.StartedWeapon->GetWeaponRuntime() : nullptr;
	Data.MoveToken = Data.StartedAdapter->BeginCreateSpace();
	return Data.MoveToken != 0 ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FDustEnemyCreateSpaceTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Context.GetInstanceData(*this).TickOwnedOperations(DeltaTime);
}

void FDustEnemyCreateSpaceTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	Context.GetInstanceData(*this).CancelOwnedOperations();
}

void FDustEnemyCreateSpaceTaskInstanceData::CancelOwnedOperations()
{
	// Clear our handles before callbacks; an old Exit never clears fields installed reentrantly.
	UDustEnemyCombatAdapterComponent* OldAdapter = StartedAdapter;
	UDustEnemyReloadComponent* OldReload = StartedReload;
	const uint64 OldMove = MoveToken;
	const uint64 OldReloadToken = ReloadToken;
	++OperationGeneration;
	MoveToken = 0;
	ReloadToken = 0;
	ReloadRequestId = 0;
	bMoveSucceeded = false;
	StartedAdapter = nullptr;
	StartedReload = nullptr;
	StartedWeapon = nullptr;
	StartedRuntime.Reset();
	StartedTarget = nullptr;
	if (IsValid(OldAdapter) && OldMove != 0) OldAdapter->CancelCreateSpace(OldMove);
	if (IsValid(OldReload) && OldReloadToken != 0) OldReload->CancelReload(OldReloadToken);
}

EStateTreeRunStatus FDustEnemyCreateSpaceTaskInstanceData::TickOwnedOperations(float DeltaTime)
{
	const uint64 Serial = OperationGeneration;
	if (!IsValid(StartedAdapter) || StartedAdapter != Adapter || !StartedAdapter->CanRunReloadLogic()
		|| StartedAdapter->GetKnownTarget() != StartedTarget || !IsValid(StartedWeapon)
		|| !StartedRuntime.IsValid() || StartedWeapon->GetWeaponRuntime() != StartedRuntime.Get())
	{
		CancelOwnedOperations();
		return EStateTreeRunStatus::Failed;
	}
	if (ReloadToken != 0)
	{
		if (!IsValid(StartedReload) || StartedReload->GetWeaponRuntime() != StartedRuntime.Get()
			|| StartedRuntime->GetReloadRequestId() != ReloadRequestId || !StartedRuntime->IsReloading())
		{
			CancelOwnedOperations();
			return EStateTreeRunStatus::Failed;
		}
		const EDustEnemyReloadStatus ReloadStatus = StartedReload->UpdateReload(ReloadToken, DeltaTime);
		if (OperationGeneration != Serial) return EStateTreeRunStatus::Failed;
		if (ReloadStatus == EDustEnemyReloadStatus::Failed)
		{
			CancelOwnedOperations();
			return EStateTreeRunStatus::Failed;
		}
		if (ReloadStatus == EDustEnemyReloadStatus::Succeeded)
		{
			const bool bCommitted = StartedReload->FinishReload(ReloadToken);
			if (OperationGeneration != Serial) return EStateTreeRunStatus::Failed;
			if (!bCommitted)
			{
				CancelOwnedOperations();
				return EStateTreeRunStatus::Failed;
			}
			ReloadToken = 0;
			ReloadRequestId = 0;
			if (MoveToken != 0 && StartedAdapter->IsCreateSpaceActive()
				&& StartedAdapter->IsConfirmedTarget(StartedTarget))
				StartedWeapon->StartEngagement(StartedTarget);
		}
	}
	else if (MoveToken != 0 && StartedRuntime->GetCurrentAmmo() <= 0
		&& StartedAdapter->IsCreateSpaceActive() && StartedAdapter->IsConfirmedTarget(StartedTarget))
	{
		StartedWeapon->StopEngagement();
		UDustEnemyReloadComponent* Reload = StartedReload;
		const uint64 NewToken = IsValid(Reload) ? Reload->TryStartReload() : 0;
		if (OperationGeneration != Serial)
		{
			if (IsValid(Reload) && NewToken != 0) Reload->CancelReload(NewToken);
			return EStateTreeRunStatus::Failed;
		}
		ReloadToken = NewToken;
		if (ReloadToken == 0)
		{
			CancelOwnedOperations();
			return EStateTreeRunStatus::Failed;
		}
		ReloadRequestId = StartedRuntime->GetReloadRequestId();
	}
	if (MoveToken != 0)
	{
		const EDustCreateSpaceStatus MoveStatus = StartedAdapter->UpdateCreateSpace(MoveToken, DeltaTime);
		if (OperationGeneration != Serial) return EStateTreeRunStatus::Failed;
		if (MoveStatus != EDustCreateSpaceStatus::Running)
		{
			bMoveSucceeded = MoveStatus == EDustCreateSpaceStatus::Succeeded;
			if (bMoveSucceeded) StartedAdapter->CompleteCreateSpace(MoveToken);
			else StartedAdapter->CancelCreateSpace(MoveToken);
			if (OperationGeneration != Serial) return EStateTreeRunStatus::Failed;
			MoveToken = 0;
		}
	}
	if (ReloadToken != 0 || MoveToken != 0) return EStateTreeRunStatus::Running;
	return bMoveSucceeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}
