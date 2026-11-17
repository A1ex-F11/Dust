#include "Presentation/DustPickupIdleComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

UDustPickupIdleComponent::UDustPickupIdleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UDustPickupIdleComponent::StartPickupIdle(
	USceneComponent* VisualTarget, UPrimitiveComponent* PhysicsBody)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || !IsValid(VisualTarget) ||
		VisualTarget->GetOwner() != Owner || VisualTarget == Owner->GetRootComponent() ||
		VisualTarget->Mobility != EComponentMobility::Movable)
	{
		return;
	}
	if (PhysicsBody && (!IsValid(PhysicsBody) || PhysicsBody->GetOwner() != Owner ||
		(PhysicsBody != VisualTarget && PhysicsBody->IsAttachedTo(VisualTarget))))
	{
		return;
	}
	if (Target.Get() == VisualTarget && Body.Get() == PhysicsBody) return;

	StopPickupIdle();
	Target = VisualTarget;
	Body = PhysicsBody;
	BaseLocation = VisualTarget->GetRelativeLocation();
	BaseRotation = VisualTarget->GetRelativeRotation().Quaternion();
	SetComponentTickEnabled(true);
}

void UDustPickupIdleComponent::StopPickupIdle()
{
	if (CanMoveVisual())
	{
		Target->SetRelativeLocationAndRotation(BaseLocation, BaseRotation);
	}
	Target.Reset();
	Body.Reset();
	ElapsedSeconds = 0.0;
	SetComponentTickEnabled(false);
}

bool UDustPickupIdleComponent::CanMoveVisual() const
{
	const USceneComponent* Visual = Target.Get();
	return IsValid(Visual) && !Visual->IsSimulatingPhysics();
}

void UDustPickupIdleComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Target.IsValid() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed())
	{
		StopPickupIdle();
		return;
	}
	if (!CanMoveVisual()) return;
	if (UPrimitiveComponent* PhysicsBody = Body.Get();
		PhysicsBody && PhysicsBody->IsSimulatingPhysics() && PhysicsBody->IsAnyRigidBodyAwake())
	{
		Target->SetRelativeLocationAndRotation(BaseLocation, BaseRotation);
		ElapsedSeconds = 0.0;
		return;
	}

	ElapsedSeconds += DeltaTime;
	const double Yaw = FMath::Fmod(ElapsedSeconds * SpinDegreesPerSecond, 360.0);
	const double Period = FMath::Max(0.01, static_cast<double>(BobPeriod));
	const double Phase = FMath::Fmod(ElapsedSeconds, Period) / Period;
	const double OffsetZ = FMath::Sin(Phase * 2.0 * UE_DOUBLE_PI) * FMath::Max(0.0f, BobHeight);
	const FQuat Rotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw)) * BaseRotation;
	Target->SetRelativeLocationAndRotation(BaseLocation + FVector(0.0, 0.0, OffsetZ), Rotation);
}
