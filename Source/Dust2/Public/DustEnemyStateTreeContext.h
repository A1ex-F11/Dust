#pragma once
#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "DustEnemyStateTreeContext.generated.h"

class AAIController;
class AActor;
class APawn;
class UDustEnemyWeaponComponent;

USTRUCT()
struct FDustEnemyContextData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category=Context) TObjectPtr<AAIController> AIController;
	UPROPERTY(EditAnywhere, Category=Parameter, meta=(DisplayName="控制器目标变量名")) FName TargetPropertyName = TEXT("TargetActor");
	UPROPERTY(EditAnywhere, Category=Output) TObjectPtr<APawn> Pawn;
	UPROPERTY(EditAnywhere, Category=Output) TObjectPtr<AActor> TargetActor;
	UPROPERTY(EditAnywhere, Category=Output) TObjectPtr<UDustEnemyWeaponComponent> WeaponComponent;
	UPROPERTY(EditAnywhere, Category=Output) bool bHasTarget = false;
	UPROPERTY(EditAnywhere, Category=Output) bool bInRange = false;
	UPROPERTY(EditAnywhere, Category=Output) bool bHasLineOfSight = false;
	UPROPERTY(EditAnywhere, Category=Output) bool bEmpty = true;
	UPROPERTY(EditAnywhere, Category=Output) float EngagementRange = 0.0f;
};

/** Reads existing controller state. Does not start chase/search or own a second combat state machine. */
USTRUCT(meta=(DisplayName="Enemy Combat Context", Category="AI|Dust2"))
struct DUST2_API FDustEnemyContextEvaluator : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FDustEnemyContextData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};
