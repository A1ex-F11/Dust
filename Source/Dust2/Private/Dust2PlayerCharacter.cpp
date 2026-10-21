#include "Dust2PlayerCharacter.h"
#include "DustEnemyVisibility.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/World.h"
#include "WeaponPickupActor.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "WeaponInventoryComponent.h"
#include "CameraRecoilRecoveryComponent.h"
#include "Components/ChildActorComponent.h"
#include "Templates/UnrealTemplate.h"
#include "WeaponPresentationComponent.h"
#include "DustMovementAudioComponent.h"
#include "DustRagdollDeathComponent.h"
#include "DustDeathCameraComponent.h"

ADust2PlayerCharacter::ADust2PlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	MaxHealth = 100.0f;
	CurrentHealth = 30.0f;
	bDeathRequested = false;

	MovementAudio = CreateDefaultSubobject<UDustMovementAudioComponent>(TEXT("MovementAudio"));
	DeathPresentation = CreateDefaultSubobject<UDustRagdollDeathComponent>(TEXT("DeathPresentation"));
	DeathPresentation->bAutoDestroyOwner = false;
	DeathCamera = CreateDefaultSubobject<UDustDeathCameraComponent>(TEXT("DeathCamera"));
}

void ADust2PlayerCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (MovementAudio)
	{
		MovementAudio->NotifyJumped();
	}
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
	if (bInvulnerable)
	{
		return false;
	}

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
		bFireInputHeld = false;
		ClearPendingSprintFire();
		ClearPendingSprintAim();
		if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(PendingSprintActionFlushTimerHandle);
		ClearGaitIntent();
		if (MovementAudio)
		{
			MovementAudio->SetMovementAudioEnabled(false);
		}
		// The Blueprint explicitly configures its full-body mesh after component setup. Existing
		// health-only users remain unchanged until that setup is complete.
		if (DeathPresentation && DeathPresentation->GetDeathMesh())
		{
			DeathPresentation->StartRagdollDeath();
			if (DeathCamera && bDeathRequested && IsDead())
				DeathCamera->BeginDeathCamera(DeathPresentation->GetDeathMesh());
		}
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
	// Checkpoint placement is owned by the existing restore flow; never teleport back to the corpse.
	if (SavedHealth > 0.0f)
	{
		if (DeathCamera) DeathCamera->ResetDeathCamera();
		if (DeathPresentation) DeathPresentation->ResetDeathPresentation();
	}
	CancelAim();
	bFireInputHeld = false;
	ClearPendingSprintFire();
	ClearGaitIntent();
	CurrentHealth = FMath::Clamp(SavedHealth, 0.0f, MaxHealth);
	bDeathRequested = false;
	if (MovementAudio)
	{
		// While disabled the stride accumulator is held at half a stride, so no stale step fires on revive.
		MovementAudio->SetMovementAudioEnabled(!IsDead());
	}
}

void ADust2PlayerCharacter::ReportSprintExitPresentationReady(bool bReady)
{
	if (!bReady || IsDead() || CurrentGait == EDustMovementGait::Run)
	{
		bSprintExitPresentationReady = false;
		return;
	}
	if (bSprintExitPresentationReady)
	{
		return;
	}
	bSprintExitPresentationReady = true;
	SchedulePendingSprintActionFlush();
}

bool ADust2PlayerCharacter::CanProcessSprintAction() const
{
	return !IsDead() &&
		CurrentGait != EDustMovementGait::Run &&
		bSprintExitPresentationReady;
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
	UpdateSprintDirectionWatch();
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
	UpdateSprintDirectionWatch();
	if (!bHadHold && !bWasNotWalk)
	{
		return;
	}
	bSprintExitPresentationReady = false;
	ApplyGait(EDustMovementGait::Walk);
}

void ADust2PlayerCharacter::ResolveGaitIntent()
{
	EDustMovementGait Desired = EDustMovementGait::Walk;
	// A pending or active aim, like a pending/held shot, keeps the player out of Run.
	const bool bSprintAllowed = bSprintHeld && !IsSprintBlockedForFire()
		&& !bPendingSprintAim && !bNativeAiming && IsSprintDirectionAllowed();
	if (bSprintAllowed && bSlowWalkHeld)
	{
		Desired = (LastPressedGaitModifier == EDustGaitModifier::SlowWalk)
			? EDustMovementGait::SlowWalk
			: EDustMovementGait::Run;
	}
	else if (bSlowWalkHeld)
	{
		Desired = EDustMovementGait::SlowWalk;
	}
	else if (bSprintAllowed)
	{
		Desired = EDustMovementGait::Run;
	}
	else if (!bSprintHeld && !bSlowWalkHeld)
	{
		// A temporary action lock is not an input release. Preserve the
		// last-pressed modifier until both gait inputs are actually released.
		LastPressedGaitModifier = EDustGaitModifier::None;
	}

	// Invalidate old animation reports whenever Run is active or is being exited.
	// A later post-evaluation report must prove the sprint pose weight reached zero.
	if (Desired == EDustMovementGait::Run ||
		CurrentGait == EDustMovementGait::Run)
	{
		bSprintExitPresentationReady = false;
	}
	if (CurrentGait == Desired)
	{
		return;
	}
	ApplyGait(Desired);
}

void ADust2PlayerCharacter::ApplyGait(EDustMovementGait NewGait)
{
	CurrentGait = NewGait;
	if (MovementAudio)
	{
		MovementAudio->SetGait(NewGait);
	}
	ReceiveGaitChanged(NewGait);
}

bool ADust2PlayerCharacter::IsSprintDirectionAllowed() const
{
	// Run only while moving forward (or diagonally forward). Backpedal, pure strafe
	// and no input all resolve to Walk while Shift stays held.
	FVector Input = GetLastMovementInputVector();
	Input.Z = 0.0f;
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.0f;
	if (!Input.Normalize() || !Forward.Normalize())
	{
		return false;
	}
	return FVector::DotProduct(Input, Forward) >= SprintMinForwardInput;
}

void ADust2PlayerCharacter::UpdateSprintDirectionWatch()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	if (bSprintHeld && !IsDead())
	{
		if (!Timers.IsTimerActive(SprintDirectionTimerHandle))
		{
			// Direction changes without any gait input event, so re-resolve while Shift is held.
			Timers.SetTimer(SprintDirectionTimerHandle, this,
				&ADust2PlayerCharacter::ResolveGaitIntent, 0.05f, true);
		}
	}
	else
	{
		Timers.ClearTimer(SprintDirectionTimerHandle);
	}
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
    bFireInputHeld = false;
    ClearPendingSprintFire();
    ClearPendingSprintAim();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PendingSprintActionFlushTimerHandle);
        World->GetTimerManager().ClearTimer(SprintDirectionTimerHandle);
    }
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
    ResolveGaitIntent();
}

void ADust2PlayerCharacter::HandleInventoryBridgeBeforeChange(
    AWeaponRuntime* OldWeapon)
{
    ClearPendingSprintFire();
    CancelAim();
    ObserveAimWeapon(nullptr);
    ReceiveBeforeInventoryWeaponChange(OldWeapon);
    ResolveGaitIntent();
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
    ResolveGaitIntent();
    return true;
}

bool ADust2PlayerCharacter::SetAimInputPressed(
    bool bPressed, bool bPresentationSwitching)
{
    if (!bPressed)
    {
        bAimInputHeld = false;
        CancelAim();
        // Do this after CancelAim; it intentionally never resolves gait itself.
        ResolveGaitIntent();
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
    if (CurrentGait == EDustMovementGait::Run ||
        !CanProcessSprintAction())
    {
        bPendingSprintAim = true;
        PendingSprintAimWeapon = Weapon;
        // Store the request before resolving gait so the same call cannot re-enter Run.
        ResolveGaitIntent();
        return false;
    }
    SetNativeAimState(true);
    ResolveGaitIntent();
    return bNativeAiming;
}

void ADust2PlayerCharacter::CancelAim()
{
    ClearPendingSprintAim();
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
    ClearPendingSprintFire();
    if (!bKeepAimDuringReload)
    {
        CancelAim();
        ResolveGaitIntent();
    }
}

void ADust2PlayerCharacter::HandleAimWeaponDestroyed(AActor* DestroyedActor)
{
    if (ObservedAimWeapon == TWeakObjectPtr<AWeaponRuntime>(Cast<AWeaponRuntime>(DestroyedActor)))
    {
        CancelAim();
        ObserveAimWeapon(nullptr);
        ResolveGaitIntent();
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

// ---- W07 G10: fire while sprinting ------------------------------------------

bool ADust2PlayerCharacter::NotifyFirePressed()
{
    if (bFireInputHeld)
    {
        // Duplicate Started without Completed: never submit a second first shot.
        return false;
    }
    bFireInputHeld = true;
    if (IsDead())
    {
        return false;
    }
    // Logical Walk alone is insufficient: wait for the animation's explicit
    // zero sprint-weight report after a recent Run.
    if (CanProcessSprintAction())
    {
        return true;
    }

    UWorld* World = GetWorld();
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    if (!World || !IsValid(Weapon))
    {
        ResolveGaitIntent();
        return false;
    }

    bPendingSprintFire = true;
    PendingSprintFireWeapon = Weapon;
    PendingSprintFireReadyTime = World->GetTimeSeconds() + SprintExitFireDelay;
    // Fire is now held, so this leaves Run immediately.
    ResolveGaitIntent();
    TryFlushPendingSprintFire();
    if (bPendingSprintFire && !World->GetTimerManager().IsTimerActive(PendingSprintFireTimerHandle))
    {
        World->GetTimerManager().SetTimer(PendingSprintFireTimerHandle, this,
            &ADust2PlayerCharacter::FlushPendingSprintActions, 0.02f, true);
    }
    return false;
}

void ADust2PlayerCharacter::NotifyFireReleased()
{
    bFireInputHeld = false;
    ClearPendingSprintFire();
    // May resume Run if Shift is still held.
    ResolveGaitIntent();
}

void ADust2PlayerCharacter::ClearPendingSprintFire()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PendingSprintFireTimerHandle);
    }
    if (!bPendingSprintFire)
    {
        return;
    }
    bPendingSprintFire = false;
    PendingSprintFireWeapon.Reset();
    PendingSprintFireReadyTime = 0.0f;
}

void ADust2PlayerCharacter::TryFlushPendingSprintFire()
{
    if (!bPendingSprintFire)
    {
        ClearPendingSprintFire();
        return;
    }
    UWorld* World = GetWorld();
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    if (!World || IsDead() || !bFireInputHeld || !IsValid(Weapon) ||
        Weapon != PendingSprintFireWeapon.Get())
    {
        ClearPendingSprintFire();
        return;
    }
    // Not ready yet: keep the single cached request and wait.
    if (!CanProcessSprintAction() ||
        BridgedInventory->IsInventoryBusy() || Weapon->IsReloading() ||
        World->GetTimeSeconds() < PendingSprintFireReadyTime)
    {
        return;
    }
    // Consume before handing off so the first shot is submitted exactly once.
    // Ammo / cooldown / reload are re-checked by RequestFire -> TryCommitFireWithResult.
    ClearPendingSprintFire();
    ReceivePendingSprintFireReady(Weapon);
}

void ADust2PlayerCharacter::ClearPendingSprintAim()
{
    bPendingSprintAim = false;
    PendingSprintAimWeapon.Reset();
}

void ADust2PlayerCharacter::TryFlushPendingSprintAim()
{
    if (!bPendingSprintAim)
    {
        return;
    }

    UWeaponPresentationComponent* Presentation = AimPresentation.Get();
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    if (!bAimInputHeld || IsDead() || !IsValid(BridgedInventory) ||
        BridgedInventory->IsInventoryBusy() || !IsValid(Presentation) ||
        !IsValid(Weapon) || Weapon != PendingSprintAimWeapon.Get() ||
        (Weapon->IsReloading() && !bKeepAimDuringReload) ||
        !Presentation->CanStartReloadPresentation(Weapon, false))
    {
        ClearPendingSprintAim();
        ResolveGaitIntent();
        return;
    }
    if (!CanProcessSprintAction())
    {
        return;
    }

    // Consume before Blueprint notifications, which may re-enter player logic.
    ClearPendingSprintAim();
    ObserveAimWeapon(Weapon);
    SetNativeAimState(true);
    ResolveGaitIntent();
}

void ADust2PlayerCharacter::FlushPendingSprintActions()
{
    // Each path validates its captured request before checking pose readiness.
    // Do not skip invalid-weapon cleanup while the animation gate is closed.
    // Enter ADS before dispatching a simultaneously pending shot.
    TryFlushPendingSprintAim();
    TryFlushPendingSprintFire();
}

void ADust2PlayerCharacter::SchedulePendingSprintActionFlush()
{
    if ((!bPendingSprintAim && !bPendingSprintFire) ||
        !GetWorld() ||
        GetWorld()->GetTimerManager().IsTimerActive(
            PendingSprintActionFlushTimerHandle))
    {
        return;
    }
    PendingSprintActionFlushTimerHandle =
        GetWorld()->GetTimerManager().SetTimerForNextTick(
            this, &ADust2PlayerCharacter::FlushPendingSprintActions);
}

UAISense_Sight::EVisibilityResult ADust2PlayerCharacter::CanBeSeenFrom(
	const FCanBeSeenFromContext& Context, FVector& OutSeenLocation,
	int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested,
	float& OutSightStrength, int32* UserData, const FOnPendingVisibilityQueryProcessedDelegate* Delegate)
{
	OutNumberOfLoSChecksPerformed = 0;
	OutNumberOfAsyncLosCheckRequested = 0;
	const APawn* Observer = DustEnemyVisibility::ResolvePawn(Context.IgnoreActor);
	const UDustEnemyWeaponComponent* Weapon = IsValid(Observer) ? Observer->FindComponentByClass<UDustEnemyWeaponComponent>() : nullptr;
	OutSeenLocation = Weapon ? DustEnemyVisibility::GetAimPoint(this, Weapon->CombatProfile) : GetActorLocation();
	const bool bVisible = DustEnemyVisibility::HasSight(Context.IgnoreActor, this,
		Context.ObserverLocation, OutSeenLocation, &OutNumberOfLoSChecksPerformed);
	OutSightStrength = bVisible ? 1.0f : 0.0f;
	return bVisible ? UAISense_Sight::EVisibilityResult::Visible : UAISense_Sight::EVisibilityResult::NotVisible;
}
