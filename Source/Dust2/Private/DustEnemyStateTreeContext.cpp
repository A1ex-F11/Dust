#include "DustEnemyStateTreeContext.h"
#include "DustEnemyWeaponComponent.h"
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
	Data.Pawn = IsValid(Data.AIController) ? Data.AIController->GetPawn() : nullptr;
	Data.WeaponComponent = IsValid(Data.Pawn) ? Data.Pawn->FindComponentByClass<UDustEnemyWeaponComponent>() : nullptr;
	Data.TargetActor = nullptr;
	if (IsValid(Data.AIController))
		if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Data.AIController->GetClass(), Data.TargetPropertyName))
			Data.TargetActor = Cast<AActor>(Property->GetObjectPropertyValue_InContainer(Data.AIController));
	Data.bHasTarget = IsValid(Data.TargetActor) && !Data.TargetActor->IsActorBeingDestroyed();
	Data.bEmpty = !Data.WeaponComponent || !Data.WeaponComponent->GetWeaponRuntime() || Data.WeaponComponent->GetWeaponRuntime()->GetCurrentAmmo() <= 0;
	Data.EngagementRange = Data.WeaponComponent && Data.WeaponComponent->CombatProfile ? FMath::Max(0.0f, Data.WeaponComponent->CombatProfile->EngagementRange) : 0.0f;
	if (Data.WeaponComponent && Data.WeaponComponent->WeaponDefinition && Data.WeaponComponent->WeaponDefinition->BallisticsData)
		Data.EngagementRange = FMath::Min(Data.EngagementRange, FMath::Max(0.0f, Data.WeaponComponent->WeaponDefinition->BallisticsData->TraceDistance));
	Data.bInRange = Data.bHasTarget && IsValid(Data.Pawn) && FVector::DistSquared(Data.Pawn->GetActorLocation(), Data.TargetActor->GetActorLocation()) <= FMath::Square(Data.EngagementRange);
	Data.bHasLineOfSight = Data.bHasTarget && IsValid(Data.AIController) && Data.AIController->LineOfSightTo(Data.TargetActor);
	if (Data.WeaponComponent && Data.WeaponComponent->IsEngaging() && Data.WeaponComponent->GetEngagementTarget() != Data.TargetActor)
		Data.WeaponComponent->StopEngagement();
}
