#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"

#include "DustEnemyStateTreeTasks.generated.h"

class AActor;
class APawn;
class AWeaponRuntime;
class UDustEnemyCombatAdapterComponent;
class UDustEnemyWeaponComponent;
class UDustEnemyPatrolComponent;
class UDustEnemyReloadComponent;

USTRUCT(meta=(DisplayName="敌人交战任务数据"))
struct FDustEnemyCombatTaskInstanceData
{
	GENERATED_BODY()

	/** Weapon component owned by this enemy pawn. Bind this to the controlled pawn's component. */
	UPROPERTY(EditAnywhere, Category = Input, meta=(DisplayName="武器组件"))
	TObjectPtr<UDustEnemyWeaponComponent> WeaponComponent = nullptr;

	/** Current perceived target. Keep this binding updated from the AI's target source. */
	UPROPERTY(EditAnywhere, Category = Input, meta=(DisplayName="目标对象"))
	TObjectPtr<AActor> TargetActor = nullptr;

	/** Runtime ownership guard so ExitState only stops an engagement started by this task. */
	UPROPERTY(Transient)
	bool bStartedEngagement = false;

	/** Component instance that successfully started the engagement. */
	UPROPERTY(Transient)
	TObjectPtr<UDustEnemyWeaponComponent> StartedWeaponComponent = nullptr;

	/** Target captured when this task successfully started the engagement. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> StartedTargetActor = nullptr;
};

/** Starts a weapon engagement while the state is active and fails when the component stops engaging. */
USTRUCT(meta = (DisplayName = "敌人交战", Category = "AI|状态树"))
struct FDustEnemyCombatTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FDustEnemyCombatTaskInstanceData;

	FDustEnemyCombatTask();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT(meta=(DisplayName="敌人稳定任务数据"))
struct FDustEnemySettleTaskInstanceData
{
	GENERATED_BODY()

	/** Pawn whose movement should settle before the state continues. Bind to the AI Actor context. */
	UPROPERTY(EditAnywhere, Category = Context, meta=(DisplayName="受控角色"))
	TObjectPtr<APawn> Pawn = nullptr;

	/** Continuous time, in seconds, that the pawn must remain under MaxSpeed. */
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName="稳定等待时间"))
	float SettleSeconds = 0.25f;

	/** Maximum 3D pawn speed, in cm/s, considered settled. */
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName="稳定速度上限"))
	float MaxSpeed = 8.0f;

	/** Runtime accumulator; reset whenever speed rises above MaxSpeed. */
	UPROPERTY(Transient)
	float SettledTime = 0.0f;
};

/** Stops AI movement on entry and succeeds after the pawn remains below MaxSpeed for SettleSeconds. */
USTRUCT(meta = (DisplayName = "敌人稳定", Category = "AI|状态树"))
struct FDustEnemySettleTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FDustEnemySettleTaskInstanceData;

	FDustEnemySettleTask();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT(meta=(DisplayName="敌人巡逻任务数据"))
struct FDustEnemyPatrolTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category=Input, meta=(DisplayName="巡逻组件")) TObjectPtr<UDustEnemyPatrolComponent> PatrolComponent;
	UPROPERTY(Transient) TObjectPtr<UDustEnemyPatrolComponent> StartedComponent;
	UPROPERTY(Transient) uint64 MoveToken = 0;
};

/** One route step. Add a separate Delay state for random idle between steps. */
USTRUCT(meta=(DisplayName="敌人巡逻移动", Category="AI|状态树"))
struct DUST2_API FDustEnemyPatrolTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = FDustEnemyPatrolTaskInstanceData;
	FDustEnemyPatrolTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT(meta=(DisplayName="敌人换弹任务数据"))
struct FDustEnemyReloadTaskInstanceData
{
	GENERATED_BODY()
	/** Reload component owned by this enemy pawn. Bind it from the evaluator output. */
	UPROPERTY(EditAnywhere, Category=Input, meta=(DisplayName="换弹组件")) TObjectPtr<UDustEnemyReloadComponent> ReloadComponent;
	UPROPERTY(Transient) TObjectPtr<UDustEnemyReloadComponent> StartedComponent;
	UPROPERTY(Transient) TObjectPtr<UDustEnemyWeaponComponent> StartedWeaponComponent;
	/** Weak runtime + request id + task token; all three must still match at commit/cancel. */
	UPROPERTY(Transient) TWeakObjectPtr<AWeaponRuntime> StartedRuntime;
	UPROPERTY(Transient) int32 ReloadRequestId = 0;
	UPROPERTY(Transient) uint64 ReloadToken = 0;
	/** Actual controller adapter resolved on entry; owns the optional reload-retreat token. */
	UPROPERTY(Transient) TObjectPtr<UDustEnemyCombatAdapterComponent> StartedAdapter;
	/** This entry's single reload-retreat move token, or 0 while none is owned. */
	UPROPERTY(Transient) uint64 RetreatToken = 0;
	/** Set after the first legal Begin call; a far player or a no-destination failure must not retry every tick. */
	UPROPERTY(Transient) bool bRetreatAttempted = false;
	UPROPERTY(Transient) uint64 OperationGeneration = 0;

	// Shared by the native task and isolated tests; StateTree remains the caller/owner.
	bool StartOwnedReload();
	EStateTreeRunStatus TickOwnedReload(float DeltaTime);
	/** Releases both requests owned by this entry, invalidating handles before callbacks. */
	void CancelOwnedReloadAndRetreat();
};

/** Runs exactly one reload transaction and commits ammo only when the identical transaction completed. */
USTRUCT(meta=(DisplayName="敌人换弹", Category="AI|状态树"))
struct DUST2_API FDustEnemyReloadTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = FDustEnemyReloadTaskInstanceData;
	FDustEnemyReloadTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT(meta=(DisplayName="敌人重获目标任务数据"))
struct FDustEnemyReacquireTaskInstanceData
{
	GENERATED_BODY()
	/** Controller component that owns target knowledge, loss grace and the frozen last-known location. */
	UPROPERTY(EditAnywhere, Category=Input, meta=(DisplayName="战斗适配器")) TObjectPtr<UDustEnemyCombatAdapterComponent> Adapter;
	/** Limited horizontal focus turn while staying in place. Actual body following still uses the Pawn's CharacterMovement rotation rate. */
	UPROPERTY(EditAnywhere, Category=Parameter, meta=(ClampMin="0.0", UIMin="0.0", DisplayName="转向速度")) float TurnSpeedDegPerSecond = 180.0f;
	UPROPERTY(Transient) TObjectPtr<UDustEnemyCombatAdapterComponent> StartedAdapter;
	UPROPERTY(Transient) TObjectPtr<AActor> StartedTarget;
	UPROPERTY(Transient) uint64 AdapterGeneration = 0;
	UPROPERTY(Transient) FVector FocusLocation = FVector::ZeroVector;
	UPROPERTY(Transient) FVector LastFocusPoint = FVector::ZeroVector;
	UPROPERTY(Transient) float FocusYaw = 0.0f;
	UPROPERTY(Transient) bool bHasFocusPoint = false;
};

/** Stays in place, turns toward the frozen last-known location at a limited focus rate, succeeds only
 * when the adapter confirms the same target again. A cleared grace/identity fails the task. */
USTRUCT(meta=(DisplayName="敌人重获目标", Category="AI|状态树"))
struct DUST2_API FDustEnemyReacquireTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = FDustEnemyReacquireTaskInstanceData;
	FDustEnemyReacquireTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT(meta=(DisplayName="敌人拉开距离任务数据"))
struct FDustEnemyCreateSpaceTaskInstanceData
{
	GENERATED_BODY()
	/** Controller component that owns the one-shot spacing token/request id and profile cooldown. */
	UPROPERTY(EditAnywhere, Category=Input, meta=(DisplayName="战斗适配器")) TObjectPtr<UDustEnemyCombatAdapterComponent> Adapter;
	UPROPERTY(Transient) TObjectPtr<UDustEnemyCombatAdapterComponent> StartedAdapter;
	UPROPERTY(Transient) uint64 MoveToken = 0;
	UPROPERTY(Transient) TObjectPtr<UDustEnemyWeaponComponent> StartedWeapon;
	UPROPERTY(Transient) TObjectPtr<UDustEnemyReloadComponent> StartedReload;
	UPROPERTY(Transient) TObjectPtr<AActor> StartedTarget;
	UPROPERTY(Transient) TWeakObjectPtr<AWeaponRuntime> StartedRuntime;
	UPROPERTY(Transient) uint64 ReloadToken = 0;
	UPROPERTY(Transient) int32 ReloadRequestId = 0;
	UPROPERTY(Transient) bool bMoveSucceeded = false;
	UPROPERTY(Transient) uint64 OperationGeneration = 0;
	// Shared by the native task and isolated tests; StateTree remains the caller/owner.
	EStateTreeRunStatus TickOwnedOperations(float DeltaTime);
	void CancelOwnedOperations();
};

/** One short move away from the currently confirmed target. Starts no move when no safe reachable point exists. */
USTRUCT(meta=(DisplayName="敌人拉开距离", Category="AI|状态树"))
struct DUST2_API FDustEnemyCreateSpaceTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = FDustEnemyCreateSpaceTaskInstanceData;
	FDustEnemyCreateSpaceTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
