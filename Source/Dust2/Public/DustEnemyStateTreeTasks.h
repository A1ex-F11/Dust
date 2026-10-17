#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"

#include "DustEnemyStateTreeTasks.generated.h"

class AActor;
class APawn;
class UDustEnemyWeaponComponent;

USTRUCT()
struct FDustEnemyCombatTaskInstanceData
{
	GENERATED_BODY()

	/** Weapon component owned by this enemy pawn. Bind this to the controlled pawn's component. */
	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<UDustEnemyWeaponComponent> WeaponComponent = nullptr;

	/** Current perceived target. Keep this binding updated from the AI's target source. */
	UPROPERTY(EditAnywhere, Category = Input)
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
USTRUCT(meta = (DisplayName = "Enemy Combat Engagement", Category = "AI|Dust2"))
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

USTRUCT()
struct FDustEnemySettleTaskInstanceData
{
	GENERATED_BODY()

	/** Pawn whose movement should settle before the state continues. Bind to the AI Actor context. */
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<APawn> Pawn = nullptr;

	/** Continuous time, in seconds, that the pawn must remain under MaxSpeed. */
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SettleSeconds = 0.25f;

	/** Maximum 3D pawn speed, in cm/s, considered settled. */
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxSpeed = 8.0f;

	/** Runtime accumulator; reset whenever speed rises above MaxSpeed. */
	UPROPERTY(Transient)
	float SettledTime = 0.0f;
};

/** Stops AI movement on entry and succeeds after the pawn remains below MaxSpeed for SettleSeconds. */
USTRUCT(meta = (DisplayName = "Enemy Settle", Category = "AI|Dust2"))
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
