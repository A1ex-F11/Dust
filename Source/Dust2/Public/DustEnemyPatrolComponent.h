#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Components/ActorComponent.h"
#include "DustEnemyPatrolComponent.generated.h"

class AAIController;
class UPathFollowingComponent;
struct FPathFollowingResult;

enum class EDustPatrolMoveStatus : uint8 { Running, Succeeded, Failed };

/** Per-pawn route and ONE task-owned move. StateTree owns the patrol/wait loop. */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent, DisplayName="敌人巡逻路线"))
class DUST2_API UDustEnemyPatrolComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDustEnemyPatrolComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|巡逻", meta=(DisplayName="启用巡逻")) bool bPatrolEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|巡逻", meta=(DisplayName="巡逻点（按顺序循环）")) TArray<TObjectPtr<AActor>> PatrolPoints;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|巡逻", meta=(DisplayName="到达半径", ClampMin="0", ToolTip="移动到巡逻点时使用的到达容差，单位厘米。")) float AcceptanceRadius = 75.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|巡逻", meta=(DisplayName="单次移动超时（秒）", ClampMin="0.1")) float MoveTimeoutSeconds = 20.0f;
	UFUNCTION(BlueprintPure, Category="AI|巡逻") bool HasPatrolRoute() const;

	// A token belongs to one task entry. Stale reads/exits never affect a newer move.
	uint64 BeginPatrolMove();
	EDustPatrolMoveStatus UpdatePatrolMove(uint64 Token, float DeltaTime);
	void CompletePatrolMove(uint64 Token);
	void CancelPatrolMove(uint64 Token);
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend struct FDustEnemyPatrolTestAccess;
	int32 NextPointIndex = 0;
	int32 ActivePointIndex = INDEX_NONE;
	TWeakObjectPtr<AActor> ActivePoint;
	TWeakObjectPtr<AAIController> MoveController;
	TWeakObjectPtr<UPathFollowingComponent> Paths;
	FDelegateHandle MoveFinishedHandle;
	FAIRequestID MoveId;
	FAIRequestID ImmediateMoveId;
	uint64 Generation = 0;
	uint64 ActiveToken = 0;
	float ElapsedSeconds = 0.0f;
	EDustPatrolMoveStatus Status = EDustPatrolMoveStatus::Failed;
	bool bIssuingMove = false;
	bool bImmediateSuccess = false;
	bool CanMove() const;
	int32 FindNextValidPoint() const;
	void ReleaseMove();
	void HandleMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
};
