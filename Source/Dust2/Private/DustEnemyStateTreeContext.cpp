#include "DustEnemyStateTreeContext.h"
#include "DustEnemyVisibility.h"
#include "DustEnemyWeaponComponent.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyPatrolComponent.h"
#include "DustEnemyReloadComponent.h"
#include "DustEnemyCombatProfile.h"
#include "WeaponRuntime.h"
#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "UObject/UnrealType.h"

void FDustEnemyContextEvaluator::TreeStart(FStateTreeExecutionContext& Context) const
{
	Tick(Context, 0.0f);
}

void FDustEnemyContextEvaluator::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	auto& Data = Context.GetInstanceData(*this);
	Data.bHasTarget = false;
	Data.bHasConfirmedTarget = false;
	Data.bNeedsReacquire = false;
	Data.bCanCreateSpace = false;
	Data.Pawn = IsValid(Data.AIController) ? Data.AIController->GetPawn() : nullptr;
	Data.WeaponComponent = IsValid(Data.Pawn) ? Data.Pawn->FindComponentByClass<UDustEnemyWeaponComponent>() : nullptr;
	Data.PatrolComponent = IsValid(Data.Pawn) ? Data.Pawn->FindComponentByClass<UDustEnemyPatrolComponent>() : nullptr;
	Data.ReloadComponent = IsValid(Data.Pawn) ? Data.Pawn->FindComponentByClass<UDustEnemyReloadComponent>() : nullptr;
	Data.bIsReloading = IsValid(Data.ReloadComponent) && Data.ReloadComponent->IsBusy();
	UDustEnemyCombatAdapterComponent* IdleAdapter = IsValid(Data.AIController) ? Data.AIController->FindComponentByClass<UDustEnemyCombatAdapterComponent>() : nullptr;
	Data.bCanPatrol = Data.PatrolComponent && Data.PatrolComponent->HasPatrolRoute() && IdleAdapter && IdleAdapter->CanRunIdleLogic();
	Data.Adapter = IdleAdapter;
	Data.TargetActor = nullptr;
	if (IsValid(Data.AIController))
	{
		// Presence selects the new authority even before initialization/after Stop.
		// Never resurrect a stale Blueprint TargetActor after native cleanup.
		if (IdleAdapter)
		{
			Data.TargetActor = IdleAdapter->GetConfirmedTarget();
			Data.bHasTarget = IdleAdapter->HasKnownTarget();
			Data.bHasConfirmedTarget = IdleAdapter->HasConfirmedTarget();
			Data.bNeedsReacquire = IdleAdapter->NeedsReacquire();
			Data.bCanCreateSpace = IdleAdapter->CanCreateSpace();
		}
		else if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Data.AIController->GetClass(), Data.TargetPropertyName))
		{
			Data.TargetActor = Cast<AActor>(Property->GetObjectPropertyValue_InContainer(Data.AIController));
			Data.bHasTarget = IsValid(Data.TargetActor) && !Data.TargetActor->IsActorBeingDestroyed();
			Data.bHasConfirmedTarget = Data.bHasTarget;
			Data.bNeedsReacquire = false;
			Data.bCanCreateSpace = false;
		}
	}
	if (!IdleAdapter)
		Data.bHasTarget = IsValid(Data.TargetActor) && !Data.TargetActor->IsActorBeingDestroyed();
	// Aim/reload gates use the confirmed target when the adapter exists; legacy setups keep their target.
	const bool bCombatTarget = IdleAdapter ? Data.bHasConfirmedTarget : Data.bHasTarget;
	Data.bEmpty = !Data.WeaponComponent || !Data.WeaponComponent->GetWeaponRuntime() || Data.WeaponComponent->GetWeaponRuntime()->GetCurrentAmmo() <= 0;
	// "Can reload this time" includes a live target; component CanReload() covers weapon/definition/adapter life.
	Data.bCanReload = bCombatTarget && IsValid(Data.ReloadComponent) && Data.ReloadComponent->CanReload();
	// ReloadReady only adds the failure/cancel cooldown; IsBusy is deliberately NOT an admission gate.
	Data.bReloadReady = Data.bCanReload && Data.ReloadComponent->IsReloadReady();
	Data.EngagementRange = Data.WeaponComponent && Data.WeaponComponent->CombatProfile ? FMath::Max(0.0f, Data.WeaponComponent->CombatProfile->EngagementRange) : 0.0f;
	if (Data.WeaponComponent && Data.WeaponComponent->WeaponDefinition && Data.WeaponComponent->WeaponDefinition->BallisticsData)
		Data.EngagementRange = FMath::Min(Data.EngagementRange, FMath::Max(0.0f, Data.WeaponComponent->WeaponDefinition->BallisticsData->TraceDistance));
	Data.bInRange = bCombatTarget && IsValid(Data.WeaponComponent)
		&& DustEnemyVisibility::IsInRange(Data.Pawn, Data.TargetActor,
			Data.WeaponComponent->CombatProfile, Data.WeaponComponent->WeaponDefinition);
	if (bCombatTarget && IsValid(Data.Pawn))
	{
		FVector Eye; FRotator Rotation;
		Data.Pawn->GetActorEyesViewPoint(Eye, Rotation);
		Data.bHasLineOfSight = DustEnemyVisibility::HasSight(Data.Pawn, Data.TargetActor, Eye,
			DustEnemyVisibility::GetAimPoint(Data.TargetActor, Data.WeaponComponent ? Data.WeaponComponent->CombatProfile.Get() : nullptr));
	}
	else Data.bHasLineOfSight = false;
	if (Data.WeaponComponent && Data.WeaponComponent->IsEngaging() && Data.WeaponComponent->GetEngagementTarget() != Data.TargetActor)
		Data.WeaponComponent->StopEngagement();
}
