#include "Runtime/WeaponKitInteractionScanner.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UWeaponKitInteractionScanner::UWeaponKitInteractionScanner()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWeaponKitInteractionScanner::IsCarrierAlive() const
{
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return false;
	}
	if (OwnerActor->GetClass()->ImplementsInterface(UWeaponKitCarrier::StaticClass()))
	{
		return IWeaponKitCarrier::Execute_IsWeaponKitCarrierAlive(OwnerActor);
	}
	return true;
}

bool UWeaponKitInteractionScanner::RegisterPickup(AActor* Pickup)
{
	if (!IsValid(Pickup) || !Pickup->GetClass()->ImplementsInterface(UWeaponKitPickup::StaticClass()))
	{
		return false;
	}
	NearbyPickups.AddUnique(Pickup);
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		if (!Timers.IsTimerActive(RefreshTimerHandle))
		{
			Timers.SetTimer(
				RefreshTimerHandle,
				this,
				&UWeaponKitInteractionScanner::RefreshAimedPickup,
				FMath::Max(0.01f, RefreshInterval),
				true);
		}
	}
	RefreshAimedPickup();
	return true;
}

void UWeaponKitInteractionScanner::UnregisterPickup(AActor* Pickup)
{
	NearbyPickups.RemoveAll([Pickup](const TWeakObjectPtr<AActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Pickup;
	});
	RefreshAimedPickup();
}

AActor* UWeaponKitInteractionScanner::GetManualPickup() const
{
	return FindPickup(false, nullptr);
}

AActor* UWeaponKitInteractionScanner::GetAutomaticPickup(AActor* ExcludedPickup) const
{
	return FindPickup(true, ExcludedPickup);
}

AActor* UWeaponKitInteractionScanner::GetAimedPickup() const
{
	AActor* Candidate = GetManualPickup();
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(Candidate) || !World || !IsValid(OwnerActor))
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	if (const APawn* Pawn = Cast<APawn>(OwnerActor))
	{
		if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
		{
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
		else
		{
			Pawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);
		}
	}
	else
	{
		OwnerActor->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponKitAimedPickup), false, OwnerActor);
	if (const APawn* Pawn = Cast<APawn>(OwnerActor))
	{
		TArray<AActor*> AttachedActors;
		Pawn->GetAttachedActors(AttachedActors, true, true);
		for (AActor* AttachedActor : AttachedActors)
		{
			QueryParams.AddIgnoredActor(AttachedActor);
		}
	}

	const float TraceLength = MaxDistance + FVector::Distance(ViewLocation, OwnerActor->GetActorLocation());
	FHitResult AimHit;
	return World->LineTraceSingleByChannel(
			AimHit,
			ViewLocation,
			ViewLocation + ViewRotation.Vector() * TraceLength,
			TraceChannel,
			QueryParams) &&
		AimHit.GetActor() == Candidate
		? Candidate
		: nullptr;
}

AActor* UWeaponKitInteractionScanner::FindPickup(bool bAutomaticOnly, AActor* ExcludedPickup) const
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	const APawn* Pawn = Cast<APawn>(OwnerActor);
	if (!World || !IsValid(OwnerActor) || !IsValid(Pawn) || !IsCarrierAlive() || MaxDistance <= 0.0f)
	{
		return nullptr;
	}

	const float MaxDistanceSq = FMath::Square(MaxDistance);
	auto IsRegisteredAndInRange = [this, Pawn, MaxDistanceSq, bAutomaticOnly, ExcludedPickup](AActor* Candidate)
	{
		if (!IsValid(Candidate) || Candidate == ExcludedPickup ||
			!Candidate->GetClass()->ImplementsInterface(UWeaponKitPickup::StaticClass()) ||
			!IWeaponKitPickup::Execute_CanBePickedUpBy(Candidate, Pawn, bAutomaticOnly) ||
			FVector::DistSquared(GetOwner()->GetActorLocation(), Candidate->GetActorLocation()) > MaxDistanceSq)
		{
			return false;
		}
		for (const TWeakObjectPtr<AActor>& Entry : NearbyPickups)
		{
			if (Entry.Get() == Candidate)
			{
				return true;
			}
		}
		return false;
	};

	FVector ViewLocation;
	FRotator ViewRotation;
	if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		Pawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponKitPickup), false, Pawn);
	if (IsValid(ExcludedPickup))
	{
		QueryParams.AddIgnoredActor(ExcludedPickup);
	}
	TArray<AActor*> AttachedActors;
	Pawn->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		QueryParams.AddIgnoredActor(AttachedActor);
	}

	const float TraceLength = MaxDistance + FVector::Distance(ViewLocation, Pawn->GetActorLocation());
	FHitResult AimHit;
	if (World->LineTraceSingleByChannel(
			AimHit,
			ViewLocation,
			ViewLocation + ViewRotation.Vector() * TraceLength,
			TraceChannel,
			QueryParams) &&
		IsRegisteredAndInRange(AimHit.GetActor()))
	{
		return AimHit.GetActor();
	}

	AActor* Nearest = nullptr;
	float NearestDistanceSq = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AActor>& Entry : NearbyPickups)
	{
		AActor* Candidate = Entry.Get();
		if (!IsRegisteredAndInRange(Candidate))
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(Pawn->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSq >= NearestDistanceSq)
		{
			continue;
		}
		FVector TargetPoint = FVector::ZeroVector;
		FVector BoundsExtent = FVector::ZeroVector;
		Candidate->GetActorBounds(false, TargetPoint, BoundsExtent);
		FHitResult SightHit;
		if (World->LineTraceSingleByChannel(SightHit, ViewLocation, TargetPoint, TraceChannel, QueryParams) &&
			SightHit.GetActor() != Candidate)
		{
			continue;
		}
		Nearest = Candidate;
		NearestDistanceSq = DistanceSq;
	}
	return Nearest;
}

bool UWeaponKitInteractionScanner::HasRegisteredPickup() const
{
	for (const TWeakObjectPtr<AActor>& Entry : NearbyPickups)
	{
		if (IsValid(Entry.Get()))
		{
			return true;
		}
	}
	return false;
}

void UWeaponKitInteractionScanner::RefreshAimedPickup()
{
	const bool bHasCandidate = HasRegisteredPickup();
	AActor* NewAimed = (bHasCandidate && IsCarrierAlive()) ? GetAimedPickup() : nullptr;
	const bool bHasAim = IsValid(NewAimed);
	if (bHadAimedPickup != bHasAim || (bHasAim && LastAimedPickup.Get() != NewAimed))
	{
		bHadAimedPickup = bHasAim;
		LastAimedPickup = NewAimed;
		OnAimedPickupChanged.Broadcast(NewAimed);
	}
	if (!bHasCandidate)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RefreshTimerHandle);
		}
	}
}

void UWeaponKitInteractionScanner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
	if (bHadAimedPickup)
	{
		OnAimedPickupChanged.Broadcast(nullptr);
	}
	NearbyPickups.Reset();
	LastAimedPickup.Reset();
	bHadAimedPickup = false;
	Super::EndPlay(EndPlayReason);
}
