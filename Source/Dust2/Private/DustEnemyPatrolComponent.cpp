#include "DustEnemyPatrolComponent.h"

#include "AIController.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"

UDustEnemyPatrolComponent::UDustEnemyPatrolComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UDustEnemyPatrolComponent::FindNextValidPoint() const
{
	if (!bPatrolEnabled || PatrolPoints.IsEmpty()) return INDEX_NONE;
	const int32 Start = FMath::Clamp(NextPointIndex, 0, PatrolPoints.Num() - 1);
	for (int32 Offset = 0; Offset < PatrolPoints.Num(); ++Offset)
	{
		const int32 Index = (Start + Offset) % PatrolPoints.Num();
		AActor* Point = PatrolPoints[Index];
		if (IsValid(Point) && !Point->IsActorBeingDestroyed() && Point->GetWorld() == GetWorld()) return Index;
	}
	return INDEX_NONE;
}

bool UDustEnemyPatrolComponent::HasPatrolRoute() const
{
	return FindNextValidPoint() != INDEX_NONE;
}

bool UDustEnemyPatrolComponent::CanMove() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	const UDustEnemyCombatAdapterComponent* Adapter = Controller ? Controller->FindComponentByClass<UDustEnemyCombatAdapterComponent>() : nullptr;
	return Adapter && Adapter->CanRunIdleLogic();
}

uint64 UDustEnemyPatrolComponent::BeginPatrolMove()
{
	// No second task may silently replace the first task's ownership.
	if (ActiveToken != 0 || !CanMove()) return 0;
	const int32 PointIndex = FindNextValidPoint();
	APawn* Pawn = Cast<APawn>(GetOwner());
	AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	if (PointIndex == INDEX_NONE || !Following || Following->GetStatus() != EPathFollowingStatus::Idle) return 0;

	ActiveToken = ++Generation;
	const uint64 Token = ActiveToken;
	ActivePointIndex = PointIndex;
	ActivePoint = PatrolPoints[PointIndex];
	MoveController = Controller;
	Paths = Following;
	ElapsedSeconds = 0.0f;
	Status = EDustPatrolMoveStatus::Running;
	MoveId = FAIRequestID::InvalidRequest;
	ImmediateMoveId = FAIRequestID::InvalidRequest;
	MoveFinishedHandle = Following->OnRequestFinished.AddUObject(this, &UDustEnemyPatrolComponent::HandleMoveFinished);
	FAIMoveRequest Request;
	Request.SetGoalLocation(ActivePoint->GetActorLocation());
	Request.SetAcceptanceRadius(FMath::Max(0.0f, AcceptanceRadius));
	Request.SetUsePathfinding(true);
	Request.SetProjectGoalLocation(true);
	Request.SetAllowPartialPath(false);
	FPathFollowingRequestResult Result;
	{
		TGuardValue<bool> Guard(bIssuingMove, true);
		Result = Controller->MoveTo(Request);
	}
	// A synchronous stop/EndPlay may have invalidated ownership during MoveTo.
	if (ActiveToken != Token) return 0;
	if (Result.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		MoveId = Result.MoveId;
		if (ImmediateMoveId.IsValid() && ImmediateMoveId.GetID() == MoveId.GetID())
			Status = bImmediateSuccess ? EDustPatrolMoveStatus::Succeeded : EDustPatrolMoveStatus::Failed;
	}
	else Status = Result.Code == EPathFollowingRequestResult::AlreadyAtGoal ? EDustPatrolMoveStatus::Succeeded : EDustPatrolMoveStatus::Failed;
	if (!CanMove()) { CancelPatrolMove(Token); return 0; }
	return Token;
}

void UDustEnemyPatrolComponent::HandleMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result)
{
	if (ActiveToken == 0) return;
	if (bIssuingMove)
	{
		ImmediateMoveId = RequestId;
		bImmediateSuccess = Result.IsSuccess();
		return;
	}
	if (!MoveId.IsValid() || RequestId.GetID() != MoveId.GetID()) return;
	Status = Result.IsSuccess() ? EDustPatrolMoveStatus::Succeeded : EDustPatrolMoveStatus::Failed;
}

EDustPatrolMoveStatus UDustEnemyPatrolComponent::UpdatePatrolMove(uint64 Token, float DeltaTime)
{
	if (Token == 0 || ActiveToken != Token) return EDustPatrolMoveStatus::Failed;
	if (!CanMove()) { CancelPatrolMove(Token); return EDustPatrolMoveStatus::Failed; }
	if (Status != EDustPatrolMoveStatus::Running) return Status;
	ElapsedSeconds += FMath::Max(0.0f, DeltaTime);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const bool bTimedOut = ElapsedSeconds >= FMath::Max(0.1f, MoveTimeoutSeconds);
	const bool bLostMove = !Paths.IsValid() || !MoveId.IsValid() || Paths->GetCurrentRequestId().GetID() != MoveId.GetID();
	if (!bPatrolEnabled || !ActivePoint.IsValid() || !Pawn || Pawn->GetController() != MoveController.Get() || bTimedOut || bLostMove)
	{
		// Keep the result/token until the task consumes it, but abort only this request.
		Status = EDustPatrolMoveStatus::Failed;
		const FAIRequestID Request = MoveId;
		MoveId = FAIRequestID::InvalidRequest;
		if (Paths.IsValid() && Request.IsValid()) Paths->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, Request);
	}
	return Status;
}

void UDustEnemyPatrolComponent::ReleaseMove()
{
	const FAIRequestID Request = MoveId;
	UPathFollowingComponent* Following = Paths.Get();
	// Invalidate ownership and remove the delegate BEFORE AbortMove's synchronous callback.
	ActiveToken = 0;
	++Generation;
	MoveId = FAIRequestID::InvalidRequest;
	ImmediateMoveId = FAIRequestID::InvalidRequest;
	if (Following) Following->OnRequestFinished.Remove(MoveFinishedHandle);
	MoveFinishedHandle.Reset();
	Paths.Reset();
	MoveController.Reset();
	ActivePoint.Reset();
	ActivePointIndex = INDEX_NONE;
	Status = EDustPatrolMoveStatus::Failed;
	if (Following && Request.IsValid()) Following->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, Request);
}

void UDustEnemyPatrolComponent::CompletePatrolMove(uint64 Token)
{
	if (Token == 0 || ActiveToken != Token || Status == EDustPatrolMoveStatus::Running) return;
	// Failed/unreachable points also advance, with StateTree's wait between attempts.
	NextPointIndex = PatrolPoints.IsEmpty() ? 0 : (ActivePointIndex + 1) % PatrolPoints.Num();
	ReleaseMove();
}

void UDustEnemyPatrolComponent::CancelPatrolMove(uint64 Token)
{
	if (Token != 0 && ActiveToken == Token) ReleaseMove();
}

void UDustEnemyPatrolComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseMove();
	Super::EndPlay(Reason);
}
