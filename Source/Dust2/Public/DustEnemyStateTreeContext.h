#pragma once
#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "DustEnemyStateTreeContext.generated.h"

class AAIController;
class AActor;
class APawn;
class UDustEnemyCombatAdapterComponent;
class UDustEnemyWeaponComponent;
class UDustEnemyPatrolComponent;
class UDustEnemyReloadComponent;

USTRUCT(meta=(DisplayName="敌人AI上下文数据"))
struct FDustEnemyContextData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category=Context, meta=(DisplayName="AI控制器")) TObjectPtr<AAIController> AIController;
	UPROPERTY(EditAnywhere, Category=Parameter, meta=(DisplayName="控制器目标变量名")) FName TargetPropertyName = TEXT("TargetActor");
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="受控角色")) TObjectPtr<APawn> Pawn;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="目标对象")) TObjectPtr<AActor> TargetActor;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="战斗适配器")) TObjectPtr<UDustEnemyCombatAdapterComponent> Adapter;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="武器组件")) TObjectPtr<UDustEnemyWeaponComponent> WeaponComponent;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="巡逻组件")) TObjectPtr<UDustEnemyPatrolComponent> PatrolComponent;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="换弹组件")) TObjectPtr<UDustEnemyReloadComponent> ReloadComponent;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="可巡逻")) bool bCanPatrol = false;
	/** Legal known target: confirmed now, or retained by the short loss grace/reload deferral. */
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="存在目标")) bool bHasTarget = false;
	/** TargetActor is the current confirmed actor; false while the adapter is remembering. */
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="存在已确认目标")) bool bHasConfirmedTarget = false;
	/** A real sight loss grace is active; StateTree should move to Reacquire, not chase. */
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="需要重获目标")) bool bNeedsReacquire = false;
	/** Pure spacing admission query owned by the adapter and profile cooldown. */
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="可拉开距离")) bool bCanCreateSpace = false;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="处于交战范围")) bool bInRange = false;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="存在视线")) bool bHasLineOfSight = false;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="弹匣为空")) bool bEmpty = true;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="可换弹")) bool bCanReload = false;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="换弹已就绪")) bool bReloadReady = false;
	/** Actual owned reload, including a reload driven inside CreateSpace. */
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="正在换弹")) bool bIsReloading = false;
	UPROPERTY(EditAnywhere, Category=Output, meta=(DisplayName="交战距离")) float EngagementRange = 0.0f;
};

/** Reads the native adapter when present, otherwise the legacy controller property.
 * Does not start chase/search or own a second combat state machine. */
USTRUCT(meta=(DisplayName="敌人战斗上下文", Category="AI|状态树"))
struct DUST2_API FDustEnemyContextEvaluator : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FDustEnemyContextData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};
