#include "Presentation/ShotTracerComponent.h"
#include "Presentation/ShotTracerGeometry.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "CoreGlobals.h"

UShotTracerComponent::UShotTracerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DefaultTracer(
		TEXT("/Game/Code/VFX/Tracer/NS_ShotTracer.NS_ShotTracer"));
	TracerSystem = DefaultTracer.Object;
}

bool UShotTracerComponent::PlayShotTracer(FVector MuzzleLocation, FVector ShotEnd)
{
	UWorld* World = GetWorld();
	// Migrated Blueprint component templates may still serialize a null asset override.
	if (!TracerSystem) TracerSystem = GetDefault<UShotTracerComponent>()->TracerSystem;
	// A retained legacy Blueprint callback may also request this same resolved shot.
	if (LastSpawnFrame == GFrameCounter && LastShotEnd.Equals(ShotEnd, 0.1f) &&
		LastMuzzle.Equals(MuzzleLocation, 0.1f)) return false;
	if (!bTracerEnabled || !IsValid(TracerSystem) || !World || World->GetNetMode() == NM_DedicatedServer ||
		!FMath::IsFinite(StreakWidth) || StreakWidth <= 0.0f ||
		!FMath::IsFinite(TracerColor.R) || !FMath::IsFinite(TracerColor.G) ||
		!FMath::IsFinite(TracerColor.B) || !FMath::IsFinite(TracerColor.A)) return false;
	float Distance, Duration, Length;
	if (!FShotTracerGeometry::BuildFlight(MuzzleLocation, ShotEnd, TravelSpeed,
		MaxTravelDuration, StreakLength, Distance, Duration, Length)) return false;
	ActiveTracers.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Item) { return !Item.IsValid(); });
	if (ActiveTracers.Num() >= 64) return false;
	UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, TracerSystem, MuzzleLocation, (ShotEnd - MuzzleLocation).Rotation(),
		FVector::OneVector, true, false, ENCPoolMethod::None, false);
	if (!Effect) return false;
	Effect->SetVariableFloat(TEXT("User.TracerDistance"), Distance);
	Effect->SetVariableFloat(TEXT("User.TracerLifetime"), Duration);
	Effect->SetVariableFloat(TEXT("User.TracerLength"), Length);
	Effect->SetVariableFloat(TEXT("User.TracerWidth"), StreakWidth);
	Effect->SetVariableLinearColor(TEXT("User.TracerColor"), TracerColor);
	ActiveTracers.Add(Effect);
	LastSpawnFrame = GFrameCounter;
	LastShotEnd = ShotEnd;
	LastMuzzle = MuzzleLocation;
	Effect->Activate(true);
	return true;
}

void UShotTracerComponent::ClearShotTracers()
{
	for (const TWeakObjectPtr<UNiagaraComponent>& Item : ActiveTracers)
	{
		if (UNiagaraComponent* Effect = Item.Get()) Effect->DestroyComponent();
	}
	ActiveTracers.Reset();
	LastSpawnFrame = MAX_uint64;
}

void UShotTracerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearShotTracers();
	Super::EndPlay(EndPlayReason);
}
