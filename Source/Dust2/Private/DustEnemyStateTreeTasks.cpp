#include "DustEnemyStateTreeTasks.h"

#include "AIController.h"
#include "DustEnemyWeaponComponent.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"

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
