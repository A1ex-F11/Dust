#include "Dust2PlayerCharacter.h"
#include "Engine/World.h"
#include "WeaponPickupActor.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "WeaponInventoryComponent.h"
#include "CameraRecoilRecoveryComponent.h"
#include "Components/ChildActorComponent.h"
#include "Templates/UnrealTemplate.h"
#include "WeaponPresentationComponent.h"

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
		CancelAim();
		ClearGaitIntent();
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
	CancelAim();
	ClearGaitIntent();
	CurrentHealth = FMath::Clamp(SavedHealth, 0.0f, MaxHealth);
	bDeathRequested = false;
}

void ADust2PlayerCharacter::SetSprintHeld(bool bHeld)
{
	if (bSprintHeld == bHeld)
	{
		return;
	}
	bSprintHeld = bHeld;
	if (bHeld)
	{
		LastPressedGaitModifier = EDustGaitModifier::Sprint;
	}
	ResolveGaitIntent();
}

void ADust2PlayerCharacter::SetSlowWalkHeld(bool bHeld)
{
	if (bSlowWalkHeld == bHeld)
	{
		return;
	}
	bSlowWalkHeld = bHeld;
	if (bHeld)
	{
		LastPressedGaitModifier = EDustGaitModifier::SlowWalk;
	}
	ResolveGaitIntent();
}

void ADust2PlayerCharacter::ClearGaitIntent()
{
	const bool bHadHold = bSprintHeld || bSlowWalkHeld;
	const bool bWasNotWalk = CurrentGait != EDustMovementGait::Walk;
	bSprintHeld = false;
	bSlowWalkHeld = false;
	LastPressedGaitModifier = EDustGaitModifier::None;
	if (!bHadHold && !bWasNotWalk)
	{
		return;
	}
	CurrentGait = EDustMovementGait::Walk;
	ReceiveGaitChanged(CurrentGait);
}

void ADust2PlayerCharacter::ResolveGaitIntent()
{
	EDustMovementGait Desired = EDustMovementGait::Walk;
	if (bSprintHeld && bSlowWalkHeld)
	{
		Desired = (LastPressedGaitModifier == EDustGaitModifier::SlowWalk)
			? EDustMovementGait::SlowWalk
			: EDustMovementGait::Run;
	}
	else if (bSlowWalkHeld)
	{
		Desired = EDustMovementGait::SlowWalk;
	}
	else if (bSprintHeld)
	{
		Desired = EDustMovementGait::Run;
	}
	else
	{
		LastPressedGaitModifier = EDustGaitModifier::None;
	}

	if (CurrentGait == Desired)
	{
		return;
	}
	CurrentGait = Desired;
	ReceiveGaitChanged(CurrentGait);
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
    UnbindInventoryBridge();
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

bool ADust2PlayerCharacter::InitializePlayerInventoryBridge(
    UWeaponInventoryComponent* Inventory,
    UChildActorComponent* Slot1,
    UChildActorComponent* Slot2,
    AWeaponRuntime* InitialCurrent,
    UCameraRecoilRecoveryComponent* RecoilRecovery)
{
    if (bInventoryBridgeInitializing || !IsValid(Inventory) ||
        Inventory->GetOwner() != this || !IsValid(Slot1) ||
        !IsValid(Slot2) || Slot1 == Slot2 ||
        Slot1->GetOwner() != this || Slot2->GetOwner() != this ||
        !IsValid(RecoilRecovery) || RecoilRecovery->GetOwner() != this ||
        (IsValid(BridgedInventory) && BridgedInventory != Inventory))
    {
        return false;
    }

    TGuardValue<bool> InitializingGuard(bInventoryBridgeInitializing, true);
    const bool bAlreadyBridged = BridgedInventory == Inventory;
    UCameraRecoilRecoveryComponent* PreviousRecoil = BridgedRecoilRecovery;
    BridgedInventory = Inventory;
    BridgedRecoilRecovery = RecoilRecovery;
    Inventory->OnInventoryChanged.AddUniqueDynamic(
        this, &ADust2PlayerCharacter::HandleInventoryBridgeChanged);
    Inventory->OnBeforeWeaponChange.AddUniqueDynamic(
        this, &ADust2PlayerCharacter::HandleInventoryBridgeBeforeChange);
    Inventory->OnEquippedWeaponChanged.AddUniqueDynamic(
        this, &ADust2PlayerCharacter::HandleInventoryBridgeEquipped);

    const bool bInitialized = Inventory->InitializeNativeInventory(
        Slot1, Slot2, InitialCurrent);
    if (!bInitialized)
    {
        if (!bAlreadyBridged)
        {
            UnbindInventoryBridge();
        }
        else
        {
            // A rejected duplicate init must not detach the working bridge.
            BridgedRecoilRecovery = PreviousRecoil;
        }
    }
    return bInitialized;
}

void ADust2PlayerCharacter::HandleInventoryBridgeChanged(
    AWeaponRuntime* Slot1Weapon, AWeaponRuntime* Slot2Weapon,
    AWeaponRuntime* CurrentWeapon)
{
    ObserveAimWeapon(CurrentWeapon);
    if (IsValid(BridgedInventory))
    {
        BridgedInventory->SyncLegacyWeaponReferences();
    }
}

void ADust2PlayerCharacter::HandleInventoryBridgeBeforeChange(
    AWeaponRuntime* OldWeapon)
{
    CancelAim();
    ObserveAimWeapon(nullptr);
    ReceiveBeforeInventoryWeaponChange(OldWeapon);
}

void ADust2PlayerCharacter::HandleInventoryBridgeEquipped(
    AWeaponRuntime* NewWeapon)
{
    const bool bRefreshPresentation = IsValid(BridgedInventory) &&
        BridgedInventory->ShouldRefreshEquippedPresentation();
    if (IsValid(BridgedRecoilRecovery))
    {
        BridgedRecoilRecovery->StartRecovery();
    }
    ReceiveInventoryWeaponChanged(NewWeapon, bRefreshPresentation);
}

void ADust2PlayerCharacter::UnbindInventoryBridge()
{
    CancelAim();
    ObserveAimWeapon(nullptr);
    AimPresentation.Reset();
    if (IsValid(BridgedInventory))
    {
        BridgedInventory->OnInventoryChanged.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandleInventoryBridgeChanged);
        BridgedInventory->OnBeforeWeaponChange.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandleInventoryBridgeBeforeChange);
        BridgedInventory->OnEquippedWeaponChanged.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandleInventoryBridgeEquipped);
    }
    BridgedInventory = nullptr;
    BridgedRecoilRecovery = nullptr;
}

bool ADust2PlayerCharacter::InitializeAimState(
    UWeaponPresentationComponent* Presentation)
{
    if (!IsValid(Presentation) || Presentation->GetOwner() != this ||
        !Presentation->IsRegistered() || !IsValid(BridgedInventory))
    {
        return false;
    }
    if (AimPresentation.Get() != Presentation)
    {
        CancelAim();
    }
    AimPresentation = Presentation;
    ObserveAimWeapon(BridgedInventory->GetCurrentWeaponNative());
    return true;
}

bool ADust2PlayerCharacter::SetAimInputPressed(
    bool bPressed, bool bPresentationSwitching)
{
    if (!bPressed)
    {
        bAimInputHeld = false;
        CancelAim();
        return false;
    }
    if (bAimInputHeld)
    {
        return bNativeAiming;
    }
    bAimInputHeld = true;
    UWeaponPresentationComponent* Presentation = AimPresentation.Get();
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    // This existing query covers binding, full Holster/Draw and pending
    // transitions. Reload/death/busy checks belong to the aim entry itself.
    if (IsDead() || !IsValid(Weapon) || !IsValid(Presentation) ||
        BridgedInventory->IsInventoryBusy() ||
        (Weapon->IsReloading() && !bKeepAimDuringReload) ||
        !Presentation->CanStartReloadPresentation(Weapon, bPresentationSwitching))
    {
        return false;
    }
    ObserveAimWeapon(Weapon);
    SetNativeAimState(true);
    return bNativeAiming;
}

void ADust2PlayerCharacter::CancelAim()
{
    SetNativeAimState(false);
}

void ADust2PlayerCharacter::SetNativeAimState(bool bAiming)
{
    if (bNativeAiming == bAiming)
    {
        return;
    }
    bNativeAiming = bAiming;
    if (IsValid(BridgedRecoilRecovery))
    {
        BridgedRecoilRecovery->SetAimingRecoilMode(bAiming);
    }
    if (AWeaponRuntime* Weapon = ObservedAimWeapon.Get())
    {
        Weapon->SetOwnerAimState(bAiming);
    }
    ReceiveAimStateChanged(bAiming);
}

void ADust2PlayerCharacter::ObserveAimWeapon(AWeaponRuntime* Weapon)
{
    Weapon = IsValid(Weapon) ? Weapon : nullptr;
    if (ObservedAimWeapon.Get() == Weapon)
    {
        return;
    }
    CancelAim();
    if (AWeaponRuntime* Old = ObservedAimWeapon.Get())
    {
        Old->SetOwnerAimState(false);
        Old->OnReloadStarted.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandleAimReloadStarted);
        Old->OnDestroyed.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandleAimWeaponDestroyed);
    }
    ObservedAimWeapon = Weapon;
    if (Weapon)
    {
        Weapon->SetOwnerAimState(bNativeAiming);
        Weapon->OnReloadStarted.AddUniqueDynamic(
            this, &ADust2PlayerCharacter::HandleAimReloadStarted);
        Weapon->OnDestroyed.AddUniqueDynamic(
            this, &ADust2PlayerCharacter::HandleAimWeaponDestroyed);
    }
}

void ADust2PlayerCharacter::HandleAimReloadStarted()
{
    if (!bKeepAimDuringReload)
    {
        CancelAim();
    }
}

void ADust2PlayerCharacter::HandleAimWeaponDestroyed(AActor* DestroyedActor)
{
    if (ObservedAimWeapon == TWeakObjectPtr<AWeaponRuntime>(Cast<AWeaponRuntime>(DestroyedActor)))
    {
        CancelAim();
        ObserveAimWeapon(nullptr);
    }
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
bool ADust2PlayerCharacter::TryBuildDropSnapshotNative(
    AWeaponRuntime* CurrentWeapon,
    AWeaponRuntime* Slot1Weapon,
    AWeaponRuntime* Slot2Weapon,
    FWeaponPickupState& OutState,
    bool& bOutOldInSlot1)
{
    OutState = FWeaponPickupState{};
    bOutOldInSlot1 = false;

    if (!IsValid(CurrentWeapon) ||
        !IsValid(Slot1Weapon) ||
        !IsValid(Slot2Weapon) ||
        Slot1Weapon == Slot2Weapon ||
        (CurrentWeapon != Slot1Weapon &&
            CurrentWeapon != Slot2Weapon))
    {
        return false;
    }

    bOutOldInSlot1 = CurrentWeapon == Slot1Weapon;
    OutState = CurrentWeapon->CapturePickupRuntimeState(
        HasConsumedPickupInitialDraw(CurrentWeapon));

    return OutState.bHasSavedState;
}
