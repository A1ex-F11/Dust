#include "Tracer/WeaponKitShotTracerComponent.h"
#include "Math/ShotTracerGeometry.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"

UWeaponKitShotTracerComponent::UWeaponKitShotTracerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWeaponKitShotTracerComponent::PlayShotTracer(FVector MuzzleLocation, FVector ShotEnd)
{
	UWorld* World = GetWorld();
	if (!bTracerEnabled || !IsValid(TracerSystem) || !World || World->GetNetMode() == NM_DedicatedServer ||
		!FMath::IsFinite(StreakWidth) || StreakWidth <= 0.0f ||
		!FMath::IsFinite(TracerColor.R) || !FMath::IsFinite(TracerColor.G) ||
		!FMath::IsFinite(TracerColor.B) || !FMath::IsFinite(TracerColor.A) ||
		DistanceParameter.IsNone() || LifetimeParameter.IsNone() || LengthParameter.IsNone() ||
		WidthParameter.IsNone() || ColorParameter.IsNone())
	{
		return false;
	}

	float Distance = 0.0f;
	float Duration = 0.0f;
	float Length = 0.0f;
	if (!FWeaponKitShotTracerGeometry::BuildFlight(
		MuzzleLocation, ShotEnd, TravelSpeed, MaxTravelDuration, StreakLength, Distance, Duration, Length))
	{
		return false;
	}

	ActiveTracers.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Item) { return !Item.IsValid(); });
	if (ActiveTracers.Num() >= FMath::Max(1, MaxActiveTracers))
	{
		return false;
	}

	UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this,
		TracerSystem,
		MuzzleLocation,
		(ShotEnd - MuzzleLocation).Rotation(),
		FVector::OneVector,
		true,
		false,
		ENCPoolMethod::None,
		false);
	if (!Effect)
	{
		return false;
	}

	Effect->SetVariableFloat(DistanceParameter, Distance);
	Effect->SetVariableFloat(LifetimeParameter, Duration);
	Effect->SetVariableFloat(LengthParameter, Length);
	Effect->SetVariableFloat(WidthParameter, StreakWidth);
	Effect->SetVariableLinearColor(ColorParameter, TracerColor);
	ActiveTracers.Add(Effect);
	Effect->Activate(true);
	return true;
}

void UWeaponKitShotTracerComponent::ClearShotTracers()
{
	for (const TWeakObjectPtr<UNiagaraComponent>& Item : ActiveTracers)
	{
		if (UNiagaraComponent* Effect = Item.Get())
		{
			Effect->DestroyComponent();
		}
	}
	ActiveTracers.Reset();
}

void UWeaponKitShotTracerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearShotTracers();
	Super::EndPlay(EndPlayReason);
}
