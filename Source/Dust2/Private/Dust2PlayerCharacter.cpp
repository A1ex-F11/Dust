#include "Dust2PlayerCharacter.h"
#include "Engine/World.h"
#include "WeaponPickupActor.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"

ADust2PlayerCharacter::ADust2PlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	MaxHealth = 100.0f;
	CurrentHealth = 30.0f;
	bDeathRequested = false;
}
float ADust2PlayerCharacter::GetMaxHealth() const
{
	return MaxHealth;
}
float ADust2PlayerCharacter::GetCurrentHealth() const
{
	return CurrentHealth;
}
bool ADust2PlayerCharacter::IsDead() const
{
	return CurrentHealth <= 0.0f;
}
bool ADust2PlayerCharacter::ApplyDamage(float Amount, bool& bDiedNow)
{
	bDiedNow = false;
	const float PreviousHealth = CurrentHealth;
	const float SafeAmount = FMath::Max(0.0f, Amount);

	CurrentHealth = FMath::Clamp(CurrentHealth - SafeAmount, 0.0f, MaxHealth);

	const bool bHealthChanged = !FMath::IsNearlyEqual(CurrentHealth, PreviousHealth);

	bDiedNow = bHealthChanged
		&& PreviousHealth > 0.0f
		&& CurrentHealth <= 0.0f
		&& !bDeathRequested;
	if (bDiedNow)
	{
		bDeathRequested = true;
	}
	return bHealthChanged;
}
bool ADust2PlayerCharacter::ApplyHeal(float Amount)
{
	const float PreviousHealth = CurrentHealth;
	const float SafeAmount = FMath::Max(0.0f, Amount);

	if (IsDead())
	{
		return false;
	}

	CurrentHealth = FMath::Clamp(CurrentHealth + SafeAmount, 0.0f, MaxHealth);
	return !FMath::IsNearlyEqual(CurrentHealth, PreviousHealth);
}
void ADust2PlayerCharacter::RestoreHealth(float SavedHealth)
{
	CurrentHealth = FMath::Clamp(SavedHealth, 0.0f, MaxHealth);
	bDeathRequested = false;
}

bool ADust2PlayerCharacter::CanStartReloadRequest_Implementation(
	AWeaponRuntime* FormalWeapon) const
{
	return true;
}
void ADust2PlayerCharacter::RegisterNearbyWeaponPickup(AActor* Pickup)
{
	if (IsValid(Pickup))
	{
		NearbyWeaponPickups.AddUnique(TWeakObjectPtr<AActor>(Pickup));
	}
    if (UWorld* World = GetWorld())
    {
        FTimerManager& Timers = World->GetTimerManager();
        if (!Timers.IsTimerActive(PickupFocusTimerHandle))
        {
            Timers.SetTimer(PickupFocusTimerHandle, this,
                &ADust2PlayerCharacter::RefreshAimedWeaponPickup,
                0.1f, true);
        }
    }
    RefreshAimedWeaponPickup();
}

void ADust2PlayerCharacter::UnregisterNearbyWeaponPickup(AActor* Pickup)
{
	NearbyWeaponPickups.RemoveAll(
		[Pickup](const TWeakObjectPtr<AActor>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Pickup;
		});
    RefreshAimedWeaponPickup();
}

AActor* ADust2PlayerCharacter::GetNearbyWeaponPickup() const
{
    return FindNearbyWeaponPickupNative(false, nullptr);
}

AActor* ADust2PlayerCharacter::GetNearbyWeaponPickupForAutoFill(
    AActor* ExcludedPickup) const
{
    return FindNearbyWeaponPickupNative(true, ExcludedPickup);
}

AActor* ADust2PlayerCharacter::FindNearbyWeaponPickupNative(
    bool bAutomaticOnly, AActor* ExcludedPickup) const
{
    UWorld* World = GetWorld();
    if (!World || IsDead() || ManualPickupMaxDistance <= 0.0f)
    {
        return nullptr;
    }

    const float MaxDistanceSq = FMath::Square(ManualPickupMaxDistance);
    auto IsRegisteredAndInRange =
        [this, MaxDistanceSq, bAutomaticOnly, ExcludedPickup](AActor* Candidate)
        {
            AWeaponPickupActor* Pickup = Cast<AWeaponPickupActor>(Candidate);
            if (!IsValid(Pickup) || Candidate == ExcludedPickup ||
                !Pickup->CanBePickedUpBy(this, bAutomaticOnly) ||
                FVector::DistSquared(GetActorLocation(),
                    Candidate->GetActorLocation()) > MaxDistanceSq)
            {
                return false;
            }
            for (const TWeakObjectPtr<AActor>& Entry : NearbyWeaponPickups)
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
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
    }
    else
    {
        GetActorEyesViewPoint(ViewLocation, ViewRotation);
    }

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);
    if (IsValid(ExcludedPickup))
    {
        QueryParams.AddIgnoredActor(ExcludedPickup);
    }
    TArray<AActor*> AttachedActors;
    GetAttachedActors(AttachedActors, true, true);
    for (AActor* AttachedActor : AttachedActors)
    {
        QueryParams.AddIgnoredActor(AttachedActor);
    }

    const float TraceLength = ManualPickupMaxDistance +
        FVector::Distance(ViewLocation, GetActorLocation());
    const FVector AimEnd = ViewLocation + ViewRotation.Vector() * TraceLength;
    FHitResult AimHit;
    if (World->LineTraceSingleByChannel(
        AimHit, ViewLocation, AimEnd, ECC_Visibility, QueryParams) &&
        IsRegisteredAndInRange(AimHit.GetActor()))
    {
        return AimHit.GetActor();
    }

    AActor* Nearest = nullptr;
    float NearestDistanceSq = TNumericLimits<float>::Max();
    for (const TWeakObjectPtr<AActor>& Entry : NearbyWeaponPickups)
    {
        AActor* Candidate = Entry.Get();
        if (!IsRegisteredAndInRange(Candidate))
        {
            continue;
        }
        const float DistanceSq = FVector::DistSquared(
            GetActorLocation(), Candidate->GetActorLocation());
        if (DistanceSq >= NearestDistanceSq)
        {
            continue;
        }
        FVector TargetPoint;
        FVector BoundsExtent;
        Candidate->GetActorBounds(false, TargetPoint, BoundsExtent);
        FHitResult SightHit;
        if (World->LineTraceSingleByChannel(SightHit, ViewLocation,
            TargetPoint, ECC_Visibility, QueryParams) &&
            SightHit.GetActor() != Candidate)
        {
            continue;
        }
        Nearest = Candidate;
        NearestDistanceSq = DistanceSq;
    }
    return Nearest;
}

AActor* ADust2PlayerCharacter::GetAimedWeaponPickup() const
{
    AActor* Candidate = GetNearbyWeaponPickup();
    UWorld* World = GetWorld();
    if (!IsValid(Candidate) || !World)
    {
        return nullptr;
    }

    FVector ViewLocation;
    FRotator ViewRotation;
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
    }
    else
    {
        GetActorEyesViewPoint(ViewLocation, ViewRotation);
    }

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);

    TArray<AActor*> AttachedActors;
    GetAttachedActors(AttachedActors, true, true);
    for (AActor* AttachedActor : AttachedActors)
    {
        QueryParams.AddIgnoredActor(AttachedActor);
    }

    const float TraceLength =
        ManualPickupMaxDistance +
        FVector::Distance(ViewLocation, GetActorLocation());
    const FVector AimEnd =
        ViewLocation + ViewRotation.Vector() * TraceLength;

    FHitResult AimHit;
    return World->LineTraceSingleByChannel(
        AimHit, ViewLocation, AimEnd, ECC_Visibility, QueryParams) &&
        AimHit.GetActor() == Candidate
        ? Candidate
        : nullptr;
}
bool ADust2PlayerCharacter::HasValidNearbyWeaponPickup() const
{
    for (const TWeakObjectPtr<AActor>& Entry : NearbyWeaponPickups)
    {
        if (IsValid(Entry.Get()))
        {
            return true;
        }
    }
    return false;
}

void ADust2PlayerCharacter::RefreshAimedWeaponPickup()
{
    const bool bHasCandidate = HasValidNearbyWeaponPickup();
    AActor* NewAimed =
        (bHasCandidate && !IsDead()) ? GetAimedWeaponPickup() : nullptr;
    const bool bHasAim = IsValid(NewAimed);

    if (bHadAimedPickup != bHasAim ||
        (bHasAim && LastAimedPickup.Get() != NewAimed))
    {
        bHadAimedPickup = bHasAim;
        LastAimedPickup = NewAimed;
        OnAimedWeaponPickupChanged.Broadcast(NewAimed);
    }

    if (!bHasCandidate)
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(PickupFocusTimerHandle);
        }
    }
}

void ADust2PlayerCharacter::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PickupFocusTimerHandle);
    }

    if (bHadAimedPickup)
    {
        OnAimedWeaponPickupChanged.Broadcast(nullptr);
    }

    NearbyWeaponPickups.Reset();
    LastAimedPickup.Reset();
    bHadAimedPickup = false;
    Super::EndPlay(EndPlayReason);
}
bool ADust2PlayerCharacter::HasConsumedPickupInitialDraw_Implementation(
    AWeaponRuntime* FormalWeapon) const
{
    return false;
}

void ADust2PlayerCharacter::ApplyPickupInitialDrawConsumed_Implementation(
    AWeaponRuntime* FormalWeapon)
{
}
