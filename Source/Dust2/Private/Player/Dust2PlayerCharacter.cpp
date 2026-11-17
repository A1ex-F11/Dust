#include "Player/Dust2PlayerCharacter.h"
#include "../AI/DustEnemyVisibility.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Engine/World.h"
#include "Inventory/WeaponPickupActor.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/WeaponInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustGameSessionSubsystem.h"
#include "Presentation/CameraRecoilRecoveryComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "../Weapons/WeaponBallisticsData.h"
#include "Components/ChildActorComponent.h"
#include "Templates/UnrealTemplate.h"
#include "Presentation/WeaponPresentationComponent.h"
#include "Player/DustMovementAudioComponent.h"
#include "Combat/DustRagdollDeathComponent.h"
#include "Combat/DustDeathCameraComponent.h"
#include "Combat/DustShotDamageReceiptLibrary.h"

ADust2PlayerCharacter::ADust2PlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	MaxHealth = 100.0f;
	CurrentHealth = 30.0f;
	bDeathRequested = false;

	MovementAudio = CreateDefaultSubobject<UDustMovementAudioComponent>(TEXT("MovementAudio"));
	DeathPresentation = CreateDefaultSubobject<UDustRagdollDeathComponent>(TEXT("DeathPresentation"));
	DeathPresentation->bAutoDestroyOwner = false;
	DeathPresentation->bDisableOtherMeshShadowsOnDeath = true;
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
	const bool bWasAlreadyDead = bDeathRequested || PreviousHealth <= 0.0f;
	const float SafeAmount = FMath::Max(0.0f, Amount);

	CurrentHealth = FMath::Clamp(CurrentHealth - SafeAmount, 0.0f, MaxHealth);

	const bool bHealthChanged = !FMath::IsNearlyEqual(CurrentHealth, PreviousHealth);

	bDiedNow = bHealthChanged
		&& PreviousHealth > 0.0f
		&& CurrentHealth <= 0.0f
		&& !bDeathRequested;
	UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(this,
		PreviousHealth, CurrentHealth, bWasAlreadyDead);
	if (bDiedNow)
	{
		bDeathRequested = true;
		NativePlayerCancelActions();
		CancelAim();
		bFireInputHeld = false;
		SetFireAnimationActive(false);
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
	SetFireAnimationActive(false);
	ClearPendingSprintFire();
	ClearGaitIntent();
	CurrentHealth = FMath::Clamp(SavedHealth, 0.0f, MaxHealth);
	bDeathRequested = false;
	// Health is restored first so the native action slice can re-observe the current weapon.
	NativePlayerCancelActions();
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
    bool bAutomaticOnly, AActor* ExcludedPickup,
    bool bAllowNearestFallback) const
{
    UWorld* World = GetWorld();
    if (!World || IsDead() || ManualPickupMaxDistance <= 0.0f)
    {
        return nullptr;
    }

    const float MaxDistanceSq = FMath::Square(ManualPickupMaxDistance);
    auto IsEligiblePickup =
        [this, MaxDistanceSq, bAutomaticOnly, ExcludedPickup](AActor* Candidate)
        {
            AWeaponPickupActor* Pickup = Cast<AWeaponPickupActor>(Candidate);
            return IsValid(Pickup) && Candidate != ExcludedPickup &&
                Pickup->CanBePickedUpBy(this, bAutomaticOnly) &&
                FVector::DistSquared(GetActorLocation(),
                    Candidate->GetActorLocation()) <= MaxDistanceSq;
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
        AimHit, ViewLocation, AimEnd, ECC_Visibility, QueryParams))
    {
        AActor* HitActor = AimHit.GetActor();
        if (IsEligiblePickup(HitActor) && NearbyWeaponPickups.Contains(TWeakObjectPtr<AActor>(HitActor)))
        {
            return HitActor;
        }
    }

    if (!bAllowNearestFallback)
    {
        return nullptr;
    }

    AActor* Nearest = nullptr;
    float NearestDistanceSq = TNumericLimits<float>::Max();
    for (const TWeakObjectPtr<AActor>& Entry : NearbyWeaponPickups)
    {
        AActor* Candidate = Entry.Get();
        if (!IsEligiblePickup(Candidate))
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
    return FindNearbyWeaponPickupNative(false, nullptr, /*bAllowNearestFallback=*/false);
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
    ShutdownPlayerActionsNative();
    bFireInputHeld = false;
    SetFireAnimationActive(false);
    ClearPendingSprintFire();
    ClearPendingSprintAim();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(NativeMissionCheckpointResumeTimer);
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

bool ADust2PlayerCharacter::CaptureNativeRouteLoadout(FDustNativeRouteCheckpoint& Checkpoint) const
{
    const UWeaponInventoryComponent* Inventory = BridgedInventory.Get();
    if (!IsValid(Inventory) || !Inventory->IsNativeInventoryInitialized() ||
        Inventory->IsInventoryBusy() || !FMath::IsFinite(CurrentHealth) || IsDead())
    {
        return false;
    }

    AWeaponRuntime* Slot1 = Inventory->GetSlotWeaponNative(0);
    AWeaponRuntime* Slot2 = Inventory->GetSlotWeaponNative(1);
    AWeaponRuntime* Current = Inventory->GetCurrentWeaponNative();
    if ((Current && Current != Slot1 && Current != Slot2) || (!Current && (Slot1 || Slot2)))
    {
        return false;
    }

    const auto CaptureSlot = [this](AWeaponRuntime* Weapon, FDustRouteWeaponSnapshot& Slot)
    {
        if (!Weapon) return;
        Slot.WeaponClass = Weapon->GetClass();
        Slot.CurrentAmmo = Weapon->GetCurrentAmmo();
        Slot.ReserveAmmo = Weapon->GetReserveAmmo();
        Slot.FireMode = Weapon->GetFireMode();
        Slot.bInitialDrawConsumed = HasConsumedPickupInitialDraw_Implementation(Weapon);
    };
    CaptureSlot(Slot1, Checkpoint.Slot1);
    CaptureSlot(Slot2, Checkpoint.Slot2);
    Checkpoint.Health = CurrentHealth;
    Checkpoint.bSlot1Active = Current && Current == Slot1;
    return !IsDead();
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
    UWorld* World = GetWorld();
    ADust2GameMode* Mode = World ? World->GetAuthGameMode<ADust2GameMode>() : nullptr;
    const bool bNativeRetry = Mode && Mode->HasPendingNativeMissionCheckpoint();
    if (bNativeRetry && !Mode->RestoreNativeRouteLoadout(this, Inventory, Slot1, Slot2)) return false;

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
    if (bInitialized && bNativeRetry)
    {
        Mode->MarkNativeRoutePlayerReady(this);
    }
    return bInitialized;
}

bool ADust2PlayerCharacter::TryQueueNativeMissionCheckpointResume()
{
    UWorld* World = GetWorld();
    const ADust2GameMode* Mode = World ? World->GetAuthGameMode<ADust2GameMode>() : nullptr;
    if (!Mode || !Mode->HasPendingNativeMissionCheckpoint()) return false;

    FTimerManager& Timers = World->GetTimerManager();
    if (!Timers.TimerExists(NativeMissionCheckpointResumeTimer))
    {
        // Blueprint BeginPlay may run before the new pawn is possessed.
        NativeMissionCheckpointResumeTimer = Timers.SetTimerForNextTick(
            this, &ADust2PlayerCharacter::ResumeNativeMissionCheckpoint);
    }
    return true;
}

void ADust2PlayerCharacter::ResumeNativeMissionCheckpoint()
{
    NativeMissionCheckpointResumeTimer.Invalidate();
    UWorld* World = GetWorld();
    ADust2GameMode* Mode = World ? World->GetAuthGameMode<ADust2GameMode>() : nullptr;
    if (!Mode || !Mode->HasPendingNativeMissionCheckpoint() || IsDead()) return;

    APlayerController* PlayerController = Cast<APlayerController>(GetController());
    if (!IsValid(PlayerController) || !PlayerController->IsLocalController() ||
        !World->HasBegunPlay() || !Mode->IsNativeRoutePlayerReady(this))
    {
        UE_LOG(LogTemp, Warning, TEXT("Native checkpoint resume skipped for '%s': local player or inventory is not ready."),
            *GetName());
        return;
    }

    auto* Route = Cast<ADustMissionRoute>(UGameplayStatics::GetActorOfClass(World, ADustMissionRoute::StaticClass()));
    if (!IsValid(Route) || !Route->StartMissionRoute(this, FDustRouteCheckpoint{}) ||
        Mode->HasPendingNativeMissionCheckpoint())
    {
        UE_LOG(LogTemp, Warning, TEXT("Native checkpoint resume failed for '%s': route could not be restored."),
            *GetName());
        return;
    }

    if (IsValid(PlayerController) && GetController() == PlayerController && !IsDead() &&
        !Mode->HasResolvedMissionResult())
    {
        PlayerController->SetInputMode(FInputModeGameOnly{});
        PlayerController->SetShowMouseCursor(false);
    }
}

void ADust2PlayerCharacter::HandleInventoryBridgeChanged(
    AWeaponRuntime* Slot1Weapon, AWeaponRuntime* Slot2Weapon,
    AWeaponRuntime* CurrentWeapon)
{
    if (bPlayerActionsNativeEnabled)
    {
        // A switch commits the new current weapon without the before-change broadcast;
        // drop a buffer aimed at the old weapon before re-observing reloads.
        if (bPendingSemiAutoBuffer &&
            PendingSemiAutoBufferWeapon.Get() != CurrentWeapon)
        {
            ClearPendingSemiAutoBuffer();
        }
        ObservePlayerActionsWeapon(CurrentWeapon);
    }
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
    // No-op unless the native action slice was explicitly enabled.
    NativePlayerCancelActions();
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
    ShutdownPlayerActionsNative();
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
    if (!CanProcessSprintAction())
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
    const UWeaponPresentationComponent* Presentation = NativeActionsPresentation.IsValid()
        ? NativeActionsPresentation.Get() : FindComponentByClass<UWeaponPresentationComponent>();
    return IsValid(Presentation) && Presentation->HasConsumedInitialDraw(FormalWeapon);
}

void ADust2PlayerCharacter::ApplyPickupInitialDrawConsumed_Implementation(
    AWeaponRuntime* FormalWeapon)
{
    // Checkpoint reconstruction may run before native action initialization binds the component.
    UWeaponPresentationComponent* Presentation = NativeActionsPresentation.IsValid()
        ? NativeActionsPresentation.Get() : FindComponentByClass<UWeaponPresentationComponent>();
    if (IsValid(Presentation)) Presentation->MarkInitialDrawConsumed(FormalWeapon);
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
    // This legacy/menu entry cancels in-flight native attempts as well as queued input.
    ++NativeActionsSerial;
    bFireInputHeld = false;
    SetFireAnimationActive(false);
    ClearPendingSprintFire();
    // Legacy entry used by the old Blueprint menu/input chain. The native release does
    // not call this, so a normal native release keeps an accepted semi buffer alive.
    ClearPendingSemiAutoBuffer();
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
    // Ammo / cooldown / reload are re-checked by the native commit or the legacy RequestFire.
    ClearPendingSprintFire();
    if (bPlayerActionsNativeEnabled)
    {
        StartNativePressFire(Weapon);
    }
    else
    {
        ReceivePendingSprintFireReady(Weapon);
    }
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

// ---- Native player fire / reload / mode scheduler ---------------------------
// The whole slice stays dormant until InitializePlayerActionsNative validates the
// explicit opt-in, so the existing Blueprint chain keeps its behavior until J wires it.

bool ADust2PlayerCharacter::EnsurePlayerActionsPresentationBound(
    UWeaponPresentationComponent* Presentation)
{
    if (!IsValid(Presentation) || Presentation->GetOwner() != this ||
        !Presentation->IsRegistered())
    {
        return false;
    }
    if (NativeActionsPresentation.Get() != Presentation)
    {
        UnbindPlayerActionsPresentation();
        NativeActionsPresentation = Presentation;
    }
    // AddUnique makes repeated initialization idempotent.
    Presentation->OnReloadWeaponPresentationCompleted.AddUniqueDynamic(
        this, &ADust2PlayerCharacter::HandlePresentationReloadCompleted);
    Presentation->OnReloadRoundPresentationCommitted.AddUniqueDynamic(
        this, &ADust2PlayerCharacter::HandlePresentationReloadRoundCommitted);
    return true;
}

void ADust2PlayerCharacter::UnbindPlayerActionsPresentation()
{
    if (UWeaponPresentationComponent* Presentation =
        NativeActionsPresentation.Get())
    {
        Presentation->OnReloadWeaponPresentationCompleted.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandlePresentationReloadCompleted);
        Presentation->OnReloadRoundPresentationCommitted.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandlePresentationReloadRoundCommitted);
    }
}

void ADust2PlayerCharacter::ObservePlayerActionsWeapon(AWeaponRuntime* Weapon)
{
    Weapon = IsValid(Weapon) ? Weapon : nullptr;
    if (ObservedPlayerActionsWeapon.Get() == Weapon)
    {
        return;
    }
    if (AWeaponRuntime* OldWeapon = ObservedPlayerActionsWeapon.Get())
    {
        OldWeapon->OnReloadStarted.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandlePlayerActionsWeaponReloadStarted);
    }
    ObservedPlayerActionsWeapon = Weapon;
    if (Weapon)
    {
        // External reloads must stop native shooting but not clear our own fresh context.
        Weapon->OnReloadStarted.AddUniqueDynamic(
            this, &ADust2PlayerCharacter::HandlePlayerActionsWeaponReloadStarted);
    }
    SetFireAnimationActive(false);
}

void ADust2PlayerCharacter::UnbindObservedPlayerActionsWeapon()
{
    if (AWeaponRuntime* Weapon = ObservedPlayerActionsWeapon.Get())
    {
        Weapon->OnReloadStarted.RemoveDynamic(
            this, &ADust2PlayerCharacter::HandlePlayerActionsWeaponReloadStarted);
    }
    ObservedPlayerActionsWeapon.Reset();
}

bool ADust2PlayerCharacter::InitializePlayerActionsNative(
    UWeaponPresentationComponent* Presentation,
    UCameraComponent* ShootingCamera)
{
    if (bPlayerActionsCancelling || bReloadCancelGuard || bFireDispatchGuard ||
        (bPlayerActionsNativeEnabled &&
         (NativeActionsPresentation.Get() != Presentation || NativeActionsCamera.Get() != ShootingCamera)))
    {
        return false;
    }
    // Explicit opt-in only: the prerequisites must already exist and be owned/registered.
    if (!IsValid(ShootingCamera) || ShootingCamera->GetOwner() != this ||
        !ShootingCamera->IsRegistered() ||
        !IsValid(BridgedInventory) || !IsValid(BridgedRecoilRecovery))
    {
        UE_LOG(LogTemp, Warning, TEXT("InitializePlayerActionsNative rejected for %s: camera %s must be owned by this player and registered. Inventory bridge valid=%d, recoil recovery valid=%d."),
            *GetPathName(), *GetPathNameSafe(ShootingCamera),
            IsValid(BridgedInventory), IsValid(BridgedRecoilRecovery));
        return false;
    }
    if (!EnsurePlayerActionsPresentationBound(Presentation))
    {
        UE_LOG(LogTemp, Warning, TEXT("InitializePlayerActionsNative rejected for %s: presentation %s must be valid, owned by this player and registered."),
            *GetPathName(), *GetPathNameSafe(Presentation));
        return false;
    }
    const bool bFirstInitialization = !bPlayerActionsNativeEnabled;
    NativeActionsCamera = ShootingCamera;
    bPlayerActionsNativeEnabled = true;
    AWeaponRuntime* CurrentWeapon = BridgedInventory->GetCurrentWeaponNative();
    ObservePlayerActionsWeapon(CurrentWeapon);

    // The inventory may have equipped a weapon before action initialization.
    if (bFirstInitialization && IsValid(CurrentWeapon) &&
        !Presentation->IsCurrentPresentationWeapon(CurrentWeapon, CurrentWeapon))
    {
        Presentation->SetWeaponForPresentation(CurrentWeapon);
    }
    return true;
}

void ADust2PlayerCharacter::ShutdownPlayerActionsNative()
{
    if (bPlayerActionsNativeEnabled)
    {
        NativePlayerCancelActions();
    }
    ClearPendingSemiAutoBuffer();
    bPlayerActionsNativeEnabled = false;
    ++NativeActionsSerial;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BurstTimerHandle);
        World->GetTimerManager().ClearTimer(ReloadFallbackTimerHandle);
    }
    bReloadFallbackTimerActive = false;
    ++ReloadFallbackTimerSerial;
    BurstWeapon.Reset();
    BurstCommittedShots = 0;
    ReloadWeapon.Reset();
    ActiveReloadRequestId = 0;
    ++ReloadSerial;
    bReloadStarting = false;
    bReloadAnimationDriven = false;
    UnbindObservedPlayerActionsWeapon();
    UnbindPlayerActionsPresentation();
    NativeActionsPresentation.Reset();
    NativeActionsCamera.Reset();
    BurstRecoveryDeadlines.Reset();
}

void ADust2PlayerCharacter::NativePlayerCancelActions()
{
    // Idempotent and safe before the guard: a cancelled/disabled slice must never keep
    // a semi-automatic buffer alive.
    ClearPendingSemiAutoBuffer();
    if (!bPlayerActionsNativeEnabled || bPlayerActionsCancelling)
    {
        // Pre-wiring: the existing notify/death/switch cleanup stays authoritative.
        return;
    }
    TGuardValue<bool> CancellingGuard(bPlayerActionsCancelling, true);
    ++NativeActionsSerial;
    bFireInputHeld = false;
    SetFireAnimationActive(false);
    bSemiAutoAttemptedThisPress = false;
    bSuppressFirstAutoTrigger = false;
    bDryFireNotifiedThisPress = false;
    ClearPendingSprintFire();
    StopBurstGroupNative(false);
    NativePlayerCancelReload();
    PruneBurstRecoveryNative();
    UnbindObservedPlayerActionsWeapon();
    UnbindPlayerActionsPresentation();
    if (!IsDead())
    {
        // A switch or a rejected switch keeps this slice's presentation bindings healthy.
        if (UWeaponPresentationComponent* Presentation =
            NativeActionsPresentation.Get())
        {
            EnsurePlayerActionsPresentationBound(Presentation);
        }
        if (IsValid(BridgedInventory))
        {
            ObservePlayerActionsWeapon(
                BridgedInventory->GetCurrentWeaponNative());
        }
    }
}

bool ADust2PlayerCharacter::IsNativePlayerFireReady(
    AWeaponRuntime* Weapon,
    UWeaponPresentationComponent*& OutPresentation) const
{
    OutPresentation = nullptr;
    if (!bPlayerActionsNativeEnabled || bPlayerActionsCancelling || bReloadCancelGuard ||
        bCycleFireModeGuard || IsDead() || !GetWorld() || !IsValid(Weapon) ||
        !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon)
    {
        return false;
    }
    UWeaponPresentationComponent* Presentation = NativeActionsPresentation.Get();
    if (!IsValid(Presentation) || Presentation->GetOwner() != this ||
        !Presentation->IsRegistered() || !NativeActionsCamera.IsValid() ||
        !IsValid(BridgedRecoilRecovery) || BridgedInventory->IsInventoryBusy() ||
        !CanProcessSprintAction() || Weapon->IsReloading() ||
        !CanStartReloadRequest(Weapon) ||
        !Presentation->CanStartReloadPresentation(Weapon, false))
    {
        return false;
    }
    // Validate the whole ballistics contract before TryCommitFireWithResult consumes ammo.
    const UWeaponBallisticsData* Ballistics =
        Weapon->GetEffectiveBallisticsNative();
    if (!Ballistics)
    {
        ensureMsgf(false, TEXT("Native player fire rejected before ammo commit: no BallisticsData for weapon %s, definition %s. Check the definition or the legacy Blueprint field."),
            *Weapon->GetPathName(), *GetPathNameSafe(Weapon->WeaponDefinition.Get()));
        return false;
    }
    if (!FMath::IsFinite(Ballistics->TraceDistance) ||
        Ballistics->TraceDistance < 0.0f ||
        !FMath::IsFinite(Ballistics->DamagePerShot) ||
        Ballistics->DamagePerShot < 0.0f ||
        !FMath::IsFinite(Ballistics->ArmorPenetration) ||
        Ballistics->ArmorPenetration < 0.0f ||
        !FMath::IsFinite(Ballistics->HeadDamageMultiplier) ||
        Ballistics->HeadDamageMultiplier < 0.0f)
    {
        ensureMsgf(false, TEXT("Native player fire rejected before ammo commit: invalid BallisticsData %s for %s. Expected finite nonnegative values. TraceDistance=%g DamagePerShot=%g ArmorPenetration=%g HeadDamageMultiplier=%g."),
            *Ballistics->GetPathName(), *Weapon->GetPathName(),
            Ballistics->TraceDistance, Ballistics->DamagePerShot,
            Ballistics->ArmorPenetration, Ballistics->HeadDamageMultiplier);
        return false;
    }
    OutPresentation = Presentation;
    return true;
}

ADust2PlayerCharacter::ENativePlayerFireOutcome
ADust2PlayerCharacter::DispatchNativePlayerShot(AWeaponRuntime* ExpectedWeapon, bool bAllowDryFire)
{
    if (bFireDispatchGuard) return ENativePlayerFireOutcome::Busy;
    TGuardValue<bool> DispatchGuard(bFireDispatchGuard, true);
    const uint64 ActionsSerialAtStart = NativeActionsSerial;
    UWeaponPresentationComponent* Presentation = nullptr;
    if (!IsNativePlayerFireReady(ExpectedWeapon, Presentation) ||
        NativeActionsSerial != ActionsSerialAtStart || !IsValid(ExpectedWeapon) ||
        IsDead() || !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != ExpectedWeapon)
    {
        return ENativePlayerFireOutcome::NotReady;
    }
    AWeaponRuntime* Weapon = ExpectedWeapon;
    PruneBurstRecoveryNative();
    if (Weapon->GetFireMode() == EFireMode::Burst)
    {
        const float* Deadline = BurstRecoveryDeadlines.Find(TWeakObjectPtr<AWeaponRuntime>(Weapon));
        if (Deadline && *Deadline > GetWorld()->GetTimeSeconds())
        {
            return ENativePlayerFireOutcome::Cooldown;
        }
    }

    const EWeaponFireAttemptResult Result = Weapon->TryCommitFireWithResult();
    if (Result == EWeaponFireAttemptResult::Committed)
    {
#if WITH_DEV_AUTOMATION_TESTS
        ++NativeCommittedShotCount;
#endif
    }
    else if (Result == EWeaponFireAttemptResult::Empty)
    {
        if (bAllowDryFire) NotifyNativeDryFire(Weapon);
        return ENativePlayerFireOutcome::Empty;
    }
    else if (Result == EWeaponFireAttemptResult::Reloading)
    {
        return ENativePlayerFireOutcome::Reloading;
    }
    else if (Result == EWeaponFireAttemptResult::Cooldown)
    {
        return ENativePlayerFireOutcome::Cooldown;
    }
    else if (Result == EWeaponFireAttemptResult::Busy)
    {
        return ENativePlayerFireOutcome::Busy;
    }
    else
    {
        return ENativePlayerFireOutcome::NotReady;
    }

    // OnFireCommitted already ran synchronously. A Blueprint handler may have switched
    // weapons or killed the player, so re-check identity before touching the camera.
    if (NativeActionsSerial != ActionsSerialAtStart || !IsValid(Weapon) ||
        IsDead() || !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon)
    {
        return ENativePlayerFireOutcome::Committed;
    }

    float PitchKick = 0.0f;
    float YawKick = 0.0f;
    bool bKickGenerated = false;
    Weapon->GenerateRecoilKick(PitchKick, YawKick, bKickGenerated);
    if (bKickGenerated)
    {
        if (UWeaponRecoilData* RecoilSettings = Weapon->GetRecoilDataNative())
        {
            BridgedRecoilRecovery->ApplyRecoilWithCamera(
                PitchKick, YawKick, RecoilSettings,
                NativeActionsCamera.Get());
        }
    }
    if (NativeActionsSerial != ActionsSerialAtStart || !IsValid(Weapon) ||
        IsDead() || !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon)
    {
        return ENativePlayerFireOutcome::Committed;
    }

    // Sample the camera only after recoil, matching the old Blueprint node order.
    UCameraComponent* ShootingCamera = NativeActionsCamera.Get();
    if (!IsValid(ShootingCamera))
    {
        return ENativePlayerFireOutcome::Committed;
    }
    const FVector TraceStart = ShootingCamera->GetComponentLocation();
    if (TraceStart.ContainsNaN())
    {
        return ENativePlayerFireOutcome::Committed;
    }
    const FVector ViewDirection = ShootingCamera->GetForwardVector();
    if (ViewDirection.ContainsNaN() || ViewDirection.IsNearlyZero())
    {
        return ENativePlayerFireOutcome::Committed;
    }
    const bool bFalling = GetCharacterMovement() &&
        GetCharacterMovement()->IsFalling();
    FVector ShotDirection = FVector::ZeroVector;
    bool bDirectionGenerated = false;
    Weapon->GenerateShotDirection(
        ViewDirection, GetVelocity().Size2D(), bFalling, bIsCrouched,
        IsSlowWalking(), ShotDirection, bDirectionGenerated);
    if (!bDirectionGenerated)
    {
        return ENativePlayerFireOutcome::Committed;
    }

    // Exactly one resolve: ammo/recoil/spread were already committed above.
    const FWeaponShotResolution Resolution =
        Weapon->ResolveCommittedShotNative(TraceStart, ShotDirection);

    // The synchronous damage path can also switch/kill; never notify a new weapon.
    if (NativeActionsSerial != ActionsSerialAtStart || !IsValid(Weapon) ||
        IsDead() || !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon)
    {
        return ENativePlayerFireOutcome::Committed;
    }
#if WITH_DEV_AUTOMATION_TESTS
    ++NativeShotResolvedNotifiedCount;
#endif
    if (Resolution.bTracePerformed && IsValid(Presentation)) Presentation->PlayResolvedShotTracer(Resolution.ShotEnd);
    ReceivePlayerShotResolvedNative(Weapon, Resolution);
    // Buffered semi shots and burst tails may commit after release; never re-latch the pose.
    if (NativeActionsSerial == ActionsSerialAtStart && bFireInputHeld &&
        !IsDead() && IsValid(Weapon) && !Weapon->IsReloading() &&
        IsValid(BridgedInventory) && !BridgedInventory->IsInventoryBusy() &&
        BridgedInventory->GetCurrentWeaponNative() == Weapon)
    {
        SetFireAnimationActive(true);
    }
    return ENativePlayerFireOutcome::Committed;
}

void ADust2PlayerCharacter::SetFireAnimationActive(bool bActive)
{
    if (bFireAnimationActive == bActive) return;
    bFireAnimationActive = bActive;
    TGuardValue<bool> AnimationGuard(bFireDispatchGuard, true);
    ReceiveFireAnimationStateChanged(bActive);
}

void ADust2PlayerCharacter::NotifyNativeDryFire(AWeaponRuntime* Weapon)
{
    if (bDryFireNotifiedThisPress)
    {
        return;
    }
    bDryFireNotifiedThisPress = true;
#if WITH_DEV_AUTOMATION_TESTS
    ++NativeDryFireCount;
#endif
    ReceivePlayerDryFireNative(Weapon);
}

void ADust2PlayerCharacter::StartNativePressFire(AWeaponRuntime* Weapon)
{
    if (!IsValid(Weapon))
    {
        return;
    }
    switch (Weapon->GetFireMode())
    {
    case EFireMode::SemiAutomatic:
    {
        if (bSemiAutoAttemptedThisPress)
        {
            return;
        }
        // At most one semi-automatic attempt per physical press. A short cooldown
        // rejection may be buffered once; every other rejection stays final.
        bSemiAutoAttemptedThisPress = true;
        const uint64 SerialBeforeShot = NativeActionsSerial;
        const ENativePlayerFireOutcome Outcome =
            DispatchNativePlayerShot(Weapon, true);
        if (Outcome == ENativePlayerFireOutcome::Cooldown)
        {
            TryBufferSemiAutoShot(Weapon, SerialBeforeShot);
        }
        break;
    }
    case EFireMode::Burst:
    {
        if (BurstWeapon.IsValid()) return;
        const uint64 SerialBeforeShot = NativeActionsSerial;
        const ENativePlayerFireOutcome Outcome =
            DispatchNativePlayerShot(Weapon, true);
        if (Outcome == ENativePlayerFireOutcome::Committed && NativeActionsSerial == SerialBeforeShot)
        {
            StartBurstGroupNative(Weapon);
        }
        break;
    }
    case EFireMode::FullAutomatic:
    default:
    {
        DispatchNativePlayerShot(Weapon, true);
        // Skip the very first same-frame Triggered attempt: no duplicate first shot or
        // dry-fire sound. The marker is consumed exactly once.
        bSuppressFirstAutoTrigger = true;
        break;
    }
    }
}

void ADust2PlayerCharacter::TryBufferSemiAutoShot(
    AWeaponRuntime* Weapon, uint64 ActionsSerialAtAttempt)
{
    // A semi-automatic Started press may be buffered only while the runtime's own
    // next-fire time is this close. The same window bounds how late its one-shot
    // timer may land, so a frame hitch cannot turn into a surprise catch-up shot.
    constexpr float SemiAutoBufferWindowSeconds = 0.040f;
    // Re-check identity after the synchronous dispatch: a Blueprint callback may have
    // cancelled actions or switched weapons before the cooldown result returned.
    if (!bPlayerActionsNativeEnabled || bPlayerActionsCancelling ||
        bReloadCancelGuard || bCycleFireModeGuard || bFireDispatchGuard ||
        IsDead() || !GetWorld() || !IsValid(Weapon) ||
        NativeActionsSerial != ActionsSerialAtAttempt ||
        !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon ||
        Weapon->GetFireMode() != EFireMode::SemiAutomatic ||
        Weapon->GetCurrentAmmo() <= 0)
    {
        return;
    }
    // One pending request only. A new click can neither multiply the buffered shot nor
    // renew its deadline.
    if (bPendingSemiAutoBuffer)
    {
        return;
    }
    const float Now = GetWorld()->GetTimeSeconds();
    const float NextFireTime = Weapon->GetNextFireTimeNative();
    const float Remaining = NextFireTime - Now;
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(NextFireTime) ||
        !FMath::IsFinite(Remaining) || Remaining <= 0.0f ||
        Remaining > SemiAutoBufferWindowSeconds)
    {
        return;
    }
    bPendingSemiAutoBuffer = true;
    PendingSemiAutoBufferWeapon = Weapon;
    PendingSemiAutoBufferActionsSerial = ActionsSerialAtAttempt;
    PendingSemiAutoBufferDeadline = Now + SemiAutoBufferWindowSeconds;
    ++PendingSemiAutoBufferGeneration;
    const uint64 Generation = PendingSemiAutoBufferGeneration;
    const TWeakObjectPtr<AWeaponRuntime> WeaponWeak(Weapon);
    FTimerDelegate Delegate = FTimerDelegate::CreateWeakLambda(this,
        [this, Generation, WeaponWeak]()
        {
            HandleSemiAutoBufferElapsed(Generation, WeaponWeak);
        });
    // One one-shot timer at the runtime's own cooldown expiry; no Tick or polling.
    GetWorld()->GetTimerManager().SetTimer(PendingSemiAutoBufferTimerHandle,
        Delegate, FMath::Max(Remaining, 0.001f), false);
}

void ADust2PlayerCharacter::HandleSemiAutoBufferElapsed(
    uint64 Generation, TWeakObjectPtr<AWeaponRuntime> WeaponWeak)
{
    // A stale timer must not consume or clear a newer pending request.
    if (!bPendingSemiAutoBuffer || Generation != PendingSemiAutoBufferGeneration)
    {
        return;
    }
    AWeaponRuntime* Weapon = WeaponWeak.Get();
    const bool bIdentityValid =
        bPlayerActionsNativeEnabled && !bPlayerActionsCancelling &&
        !bReloadCancelGuard && !bCycleFireModeGuard && !bFireDispatchGuard &&
        !IsDead() && GetWorld() && IsValid(Weapon) &&
        PendingSemiAutoBufferWeapon.Get() == Weapon &&
        NativeActionsSerial == PendingSemiAutoBufferActionsSerial &&
        IsValid(BridgedInventory) &&
        BridgedInventory->GetCurrentWeaponNative() == Weapon &&
        Weapon->GetFireMode() == EFireMode::SemiAutomatic;
    if (!bIdentityValid)
    {
        ClearPendingSemiAutoBuffer();
        return;
    }
    const float Now = GetWorld()->GetTimeSeconds();
    const float NextFireTime = Weapon->GetNextFireTimeNative();
    const bool bTooLate = !FMath::IsFinite(Now) ||
        Now > PendingSemiAutoBufferDeadline;
    const bool bTooEarly = !FMath::IsFinite(NextFireTime) ||
        Now < NextFireTime;
    // Consume before Dispatch: synchronous Blueprint callbacks may re-enter.
    ClearPendingSemiAutoBuffer();
    if (bTooLate || bTooEarly)
    {
        return;
    }
    // The original dispatch still owns ammo/recoil/ballistics and re-validates the
    // cancel serial and ready gates. A rejected single attempt is not retried.
    DispatchNativePlayerShot(Weapon);
}

void ADust2PlayerCharacter::ClearPendingSemiAutoBuffer()
{
    // Invalidate captured timer generations even when no request is currently set.
    ++PendingSemiAutoBufferGeneration;
    bPendingSemiAutoBuffer = false;
    PendingSemiAutoBufferWeapon.Reset();
    PendingSemiAutoBufferActionsSerial = 0;
    PendingSemiAutoBufferDeadline = 0.0f;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PendingSemiAutoBufferTimerHandle);
    }
}

bool ADust2PlayerCharacter::NativePlayerFirePressed()
{
    if (!bPlayerActionsNativeEnabled)
    {
        return false;
    }
    if (bCycleFireModeGuard || bFireDispatchGuard || bPlayerActionsCancelling || bReloadCancelGuard)
    {
        return false;
    }
    if (bFireInputHeld)
    {
        // Duplicate Started: never reset the first-attempt markers or submit twice.
        return false;
    }
    bDryFireNotifiedThisPress = false;
    bSemiAutoAttemptedThisPress = false;
    bSuppressFirstAutoTrigger = false;
    const bool bReady = NotifyFirePressed();
    if (!bReady)
    {
        // Sprint pose gate queued the single pending press; the flush dispatches natively.
        return false;
    }
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    if (IsValid(Weapon))
    {
        StartNativePressFire(Weapon);
    }
    return true;
}

void ADust2PlayerCharacter::NativePlayerFireTriggered()
{
    if (!bPlayerActionsNativeEnabled || bCycleFireModeGuard || bPlayerActionsCancelling || bReloadCancelGuard ||
        !bFireInputHeld || bPendingSprintFire || IsDead())
    {
        return;
    }
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    if (!IsValid(Weapon) || Weapon->GetFireMode() != EFireMode::FullAutomatic)
    {
        return;
    }
    if (bSuppressFirstAutoTrigger)
    {
        bSuppressFirstAutoTrigger = false;
        return;
    }
    // No timer and no input buffer: keep the original Triggered frequency and let the
    // runtime cooldown/gates reject each attempt.
    DispatchNativePlayerShot(Weapon);
}

void ADust2PlayerCharacter::NativePlayerFireReleased()
{
    if (!bPlayerActionsNativeEnabled)
    {
        return;
    }
    if (bCycleFireModeGuard)
    {
        return;
    }
    bFireInputHeld = false;
    SetFireAnimationActive(false);
    bSemiAutoAttemptedThisPress = false;
    bSuppressFirstAutoTrigger = false;
    bDryFireNotifiedThisPress = false;
    ClearPendingSprintFire();
    // The semi-automatic buffer intentionally survives release: one accepted click is
    // still valid after the key is let go. The legacy NotifyFireReleased entry clears it.
    ResolveGaitIntent();
    // Burst deliberately survives release so the current three-shot group can finish.
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    if (IsValid(Weapon))
    {
        TGuardValue<bool> ReleaseGuard(bCycleFireModeGuard, true);
        ReceivePlayerFireReleasedNative(Weapon);
    }
}

void ADust2PlayerCharacter::NativePlayerCycleFireMode()
{
    if (!bPlayerActionsNativeEnabled || bCycleFireModeGuard || bPlayerActionsCancelling || bReloadCancelGuard || bFireDispatchGuard)
    {
        return;
    }
    // Changing fire mode discards a buffered semi-automatic press.
    ClearPendingSemiAutoBuffer();
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    if (!IsValid(Weapon))
    {
        return;
    }
    // Block every reentrant fire entry while pending/burst/held input is cleared.
    TGuardValue<bool> CycleGuard(bCycleFireModeGuard, true);
    bFireInputHeld = false;
    SetFireAnimationActive(false);
    bSemiAutoAttemptedThisPress = false;
    bSuppressFirstAutoTrigger = false;
    bDryFireNotifiedThisPress = false;
    ClearPendingSprintFire();
    StopBurstGroupNative(false);
    ReceivePlayerFireReleasedNative(Weapon);
    if (!IsValid(Weapon) || IsDead() || !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon) return;
    const EFireMode PreviousMode = Weapon->GetFireMode();
    Weapon->CycleFireMode();
    ResolveGaitIntent();
    if (Weapon->GetFireMode() != PreviousMode)
    {
#if WITH_DEV_AUTOMATION_TESTS
        ++NativeFireModeChangedCount;
#endif
        ReceivePlayerFireModeChangedNative(Weapon, PreviousMode);
    }
}

void ADust2PlayerCharacter::StartBurstGroupNative(AWeaponRuntime* Weapon)
{
    StopBurstGroupNative(false);
    if (!IsValid(Weapon) || IsDead() || !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon ||
        Weapon->GetFireMode() != EFireMode::Burst)
    {
        return;
    }
    BurstWeapon = Weapon;
    // The opening shot is already committed; two more successful commits end the group.
    BurstCommittedShots = 1;
    ScheduleNextBurstShotNative();
}

void ADust2PlayerCharacter::ScheduleNextBurstShotNative()
{
    if (!bPlayerActionsNativeEnabled || !BurstWeapon.IsValid() ||
        BurstCommittedShots <= 0)
    {
        return;
    }
    UWorld* World = GetWorld();
    AWeaponRuntime* Weapon = BurstWeapon.Get();
    if (!World || !IsValid(Weapon))
    {
        StopBurstGroupNative(false);
        return;
    }
    float Delay = Weapon->GetNextFireTimeNative() - World->GetTimeSeconds();
    if (!FMath::IsFinite(Delay) || Delay <= 0.0f)
    {
        Delay = FMath::Max(0.001f, Weapon->GetEffectiveFireInterval());
    }
    World->GetTimerManager().SetTimer(BurstTimerHandle, this,
        &ADust2PlayerCharacter::HandleBurstShotTimer,
        FMath::Max(0.001f, Delay), false);
}

void ADust2PlayerCharacter::HandleBurstShotTimer()
{
    if (!bPlayerActionsNativeEnabled)
    {
        StopBurstGroupNative(false);
        return;
    }
    AWeaponRuntime* Weapon = BurstWeapon.Get();
    if (!IsValid(Weapon) || BurstCommittedShots <= 0)
    {
        StopBurstGroupNative(false);
        return;
    }
    if (!IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon || IsDead() ||
        Weapon->IsReloading() ||
        Weapon->GetFireMode() != EFireMode::Burst)
    {
        StopBurstGroupNative(false);
        return;
    }
    const ENativePlayerFireOutcome Outcome = DispatchNativePlayerShot(Weapon);
    switch (Outcome)
    {
    case ENativePlayerFireOutcome::Committed:
        // A synchronous callback may have cancelled this group already.
        if (!BurstWeapon.IsValid() || BurstCommittedShots <= 0)
        {
            return;
        }
        ++BurstCommittedShots;
        if (BurstCommittedShots >= 3)
        {
            StopBurstGroupNative(true);
        }
        else
        {
            ScheduleNextBurstShotNative();
        }
        break;
    case ENativePlayerFireOutcome::Cooldown:
    case ENativePlayerFireOutcome::Busy:
        // Timing/reentrancy rejections never advance the three-shot count.
        ScheduleNextBurstShotNative();
        break;
    default:
        StopBurstGroupNative(false);
        break;
    }
}

void ADust2PlayerCharacter::StopBurstGroupNative(bool bApplyGroupRecovery)
{
    if (bApplyGroupRecovery)
    {
        AWeaponRuntime* Weapon = BurstWeapon.Get();
        UWorld* World = GetWorld();
        if (IsValid(Weapon) && World)
        {
            const float RecoveryInterval = Weapon->GetBurstRecoveryIntervalNative();
            if (FMath::IsFinite(RecoveryInterval) && RecoveryInterval > 0.0f)
            {
                const float Deadline = World->GetTimeSeconds() + RecoveryInterval;
                BurstRecoveryDeadlines.Add(
                    TWeakObjectPtr<AWeaponRuntime>(Weapon), Deadline);
            }
        }
    }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BurstTimerHandle);
    }
    BurstWeapon.Reset();
    BurstCommittedShots = 0;
}

void ADust2PlayerCharacter::PruneBurstRecoveryNative()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        BurstRecoveryDeadlines.Reset();
        return;
    }
    const float Now = World->GetTimeSeconds();
    for (auto It = BurstRecoveryDeadlines.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid() || !FMath::IsFinite(It.Value()) ||
            It.Value() <= Now)
        {
            It.RemoveCurrent();
        }
    }
}

void ADust2PlayerCharacter::HandlePlayerActionsWeaponReloadStarted()
{
    if (!bPlayerActionsNativeEnabled)
    {
        return;
    }
    // Any successful reload, including an external one, ends a queued semi shot.
    ClearPendingSemiAutoBuffer();
    AWeaponRuntime* Weapon = ObservedPlayerActionsWeapon.Get();
    // Any reload stops shooting. Release would let an already-started burst finish, but
    // a reload is a hard stop for the group.
    StopBurstGroupNative(false);
    ClearPendingSprintFire();
    SetFireAnimationActive(false);
    if (bReloadStarting && ReloadWeapon.Get() == Weapon)
    {
        // Our own NativePlayerStartReload broadcast; keep the fresh context.
        return;
    }
    if (IsValid(Weapon) && ReloadWeapon.Get() == Weapon &&
        ActiveReloadRequestId > 0 &&
        Weapon->GetReloadRequestId() == ActiveReloadRequestId)
    {
        return;
    }
    if (ReloadWeapon.Get() == Weapon)
    {
        // An external reload replaced this weapon's stale request context/timer.
        ClearReloadContextNative();
    }
}

bool ADust2PlayerCharacter::StartReloadFallbackTimer(
    AWeaponRuntime* Weapon, int32 RequestId, float Seconds)
{
    ClearReloadFallbackTimer();
    UWorld* World = GetWorld();
    if (!World || !IsValid(Weapon) || RequestId <= 0)
    {
        return false;
    }
    const float SafeSeconds = (FMath::IsFinite(Seconds) && Seconds > 0.0f)
        ? Seconds : 2.0f;
    const uint64 Serial = ReloadFallbackTimerSerial;
    bReloadFallbackTimerActive = true;
    FTimerDelegate Delegate = FTimerDelegate::CreateWeakLambda(this,
        [this, Serial,
        WeaponWeak = TWeakObjectPtr<AWeaponRuntime>(Weapon), RequestId]()
        {
            HandleReloadFallbackElapsed(Serial, WeaponWeak, RequestId);
        });
    World->GetTimerManager().SetTimer(
        ReloadFallbackTimerHandle, Delegate, SafeSeconds, false);
    return true;
}

void ADust2PlayerCharacter::ClearReloadFallbackTimer()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReloadFallbackTimerHandle);
    }
    bReloadFallbackTimerActive = false;
    ++ReloadFallbackTimerSerial;
}

void ADust2PlayerCharacter::ClearReloadContextNative()
{
    ++ReloadSerial;
    ClearReloadFallbackTimer();
    ReloadWeapon.Reset();
    ActiveReloadRequestId = 0;
    bReloadStarting = false;
    bReloadAnimationDriven = false;
}

void ADust2PlayerCharacter::HandleReloadFallbackElapsed(
    uint64 Serial, TWeakObjectPtr<AWeaponRuntime> WeaponWeak,
    int32 RequestId)
{
    if (!bPlayerActionsNativeEnabled || !bReloadFallbackTimerActive ||
        Serial != ReloadFallbackTimerSerial)
    {
        return;
    }
    bReloadFallbackTimerActive = false;
    if (bReloadAnimationDriven)
    {
        // The montage took ownership; the fallback can never complete that request.
        return;
    }
    AWeaponRuntime* Weapon = WeaponWeak.Get();
    if (!IsValid(Weapon) || ReloadWeapon.Get() != Weapon ||
        ActiveReloadRequestId != RequestId)
    {
        // Stale timer: never touch a newer request or another weapon.
        return;
    }
    if (!Weapon->IsReloading() ||
        Weapon->GetReloadRequestId() != RequestId)
    {
        ClearReloadContextNative();
        return;
    }
    if (!IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon || IsDead())
    {
        ClearReloadContextNative();
        return;
    }
    CompleteNativeReloadInternal(Weapon, RequestId, false);
}

bool ADust2PlayerCharacter::CompleteNativeReloadInternal(
    AWeaponRuntime* Weapon, int32 RequestId, bool bFromAnimation)
{
    if (!bPlayerActionsNativeEnabled || !IsValid(Weapon) ||
        ReloadWeapon.Get() != Weapon || RequestId <= 0 ||
        !Weapon->IsReloading() ||
        Weapon->GetReloadRequestId() != RequestId)
    {
        return false;
    }
    // Animation completion is valid only for an animation-driven active request. A
    // fallback request ignores unrelated montage-completed events.
    if (bFromAnimation && !bReloadAnimationDriven)
    {
        return false;
    }
    if (!bReloadStarting && ActiveReloadRequestId != 0 &&
        ActiveReloadRequestId != RequestId)
    {
        return false;
    }
    if (!IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon || IsDead())
    {
        return false;
    }
    ActiveReloadRequestId = RequestId;
    // Clear local context and timer BEFORE FinishReload so a reentrant caller cannot
    // complete the same request twice.
    ClearReloadContextNative();
    const bool bFinished = Weapon->FinishReload();
    if (bFinished)
    {
#if WITH_DEV_AUTOMATION_TESTS
        ++NativeAmmoChangedCount;
#endif
        ReceivePlayerAmmoChangedNative(Weapon);
    }
    return bFinished;
}

bool ADust2PlayerCharacter::NativePlayerStartReload(
    float FallbackCompletionSeconds)
{
    if (!bPlayerActionsNativeEnabled || bCycleFireModeGuard || bPlayerActionsCancelling || bReloadCancelGuard || bFireDispatchGuard || bReloadEntryGuard)
    {
        return false;
    }
    TGuardValue<bool> EntryGuard(bReloadEntryGuard, true);
    const uint64 ActionsSerialAtEntry = NativeActionsSerial;
    AWeaponRuntime* Weapon = IsValid(BridgedInventory)
        ? BridgedInventory->GetCurrentWeaponNative() : nullptr;
    UWeaponPresentationComponent* Presentation = NativeActionsPresentation.Get();
    // Validate everything before changing any existing available action.
    if (IsDead() || !IsValid(Weapon) || !IsValid(BridgedInventory) ||
        !IsValid(Presentation) || !Presentation->IsRegistered() ||
        BridgedInventory->IsInventoryBusy() || Weapon->IsReloading() ||
        !Presentation->CanStartReloadPresentation(Weapon, false) ||
        !CanStartReloadRequest(Weapon))
    {
        return false;
    }
    if (NativeActionsSerial != ActionsSerialAtEntry || IsDead() ||
        !IsValid(Weapon) || !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon || Weapon->IsReloading())
    {
        return false;
    }
    const float SafeSeconds =
        (FMath::IsFinite(FallbackCompletionSeconds) &&
            FallbackCompletionSeconds > 0.0f)
        ? FallbackCompletionSeconds : 2.0f;

    ClearReloadContextNative();
    PendingReloadFallbackSeconds = SafeSeconds;
    ReloadWeapon = Weapon;
    bReloadStarting = true;
    const uint64 ReloadSerialAtStart = ReloadSerial;

    // TryStartReload broadcasts OnReloadStarted synchronously; the start guard keeps the
    // Blueprint animation callback from clearing this fresh context.
    const bool bStarted = Weapon->TryStartReload();
    if (!bStarted)
    {
        // A rejected start leaves every still-available action untouched (no burst stop).
        if (ReloadSerial == ReloadSerialAtStart)
        {
            ClearReloadContextNative();
        }
        return false;
    }
    // A real reload transaction exists now; only this success path discards the buffer.
    ClearPendingSemiAutoBuffer();
    // The OnReloadStarted handler already stops any burst group; repeat here only after
    // the real transaction exists so a failed start cannot cancel a running group.
    StopBurstGroupNative(false);
    if (ReloadSerial != ReloadSerialAtStart)
    {
        // A synchronous completion/cancel already consumed this request.
        return false;
    }
    if (!IsValid(Weapon) || !Weapon->IsReloading() ||
        !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != Weapon)
    {
        ClearReloadContextNative();
        return false;
    }
    const int32 RequestId = Weapon->GetReloadRequestId();
    if (RequestId <= 0 ||
        (ActiveReloadRequestId != 0 && ActiveReloadRequestId != RequestId))
    {
        ClearReloadContextNative();
        return false;
    }
    ActiveReloadRequestId = RequestId;
    bReloadStarting = false;
    if (IsValid(BridgedRecoilRecovery)) BridgedRecoilRecovery->StartRecovery();
    if (!bReloadAnimationDriven && !bReloadFallbackTimerActive)
    {
        StartReloadFallbackTimer(Weapon, RequestId, PendingReloadFallbackSeconds);
    }
    return true;
}

bool ADust2PlayerCharacter::NativePlayerSetReloadAnimationDriven(
    AWeaponRuntime* FormalWeapon, int32 ReloadRequestId, bool bAnimationDriven)
{
    if (!bPlayerActionsNativeEnabled || bPlayerActionsCancelling || bReloadCancelGuard || ReloadRequestId <= 0 || !IsValid(FormalWeapon) ||
        ReloadWeapon.Get() != FormalWeapon ||
        FormalWeapon->GetReloadRequestId() != ReloadRequestId ||
        !FormalWeapon->IsReloading() ||
        !IsValid(BridgedInventory) ||
        BridgedInventory->GetCurrentWeaponNative() != FormalWeapon)
    {
        return false;
    }
    if (!bReloadStarting && ActiveReloadRequestId == 0)
    {
        return false;
    }
    if (ActiveReloadRequestId != 0 &&
        ActiveReloadRequestId != ReloadRequestId)
    {
        return false;
    }
    ActiveReloadRequestId = ReloadRequestId;
    if (bAnimationDriven)
    {
        bReloadAnimationDriven = true;
        // The montage now owns completion; the fallback must not race it.
        ClearReloadFallbackTimer();
    }
    else
    {
        bReloadAnimationDriven = false;
        StartReloadFallbackTimer(
            FormalWeapon, ReloadRequestId, PendingReloadFallbackSeconds);
    }
    return true;
}

bool ADust2PlayerCharacter::NativePlayerCompleteReload(
    AWeaponRuntime* FormalWeapon, int32 ReloadRequestId)
{
    if (!bPlayerActionsNativeEnabled)
    {
        return false;
    }
    if (bReloadStarting && ReloadWeapon.Get() == FormalWeapon &&
        IsValid(FormalWeapon) && FormalWeapon->IsReloading() &&
        FormalWeapon->GetReloadRequestId() == ReloadRequestId)
    {
        // Accept a synchronous completion from the OnReloadStarted callback.
        ActiveReloadRequestId = ReloadRequestId;
    }
    return CompleteNativeReloadInternal(
        FormalWeapon, ReloadRequestId, false);
}

void ADust2PlayerCharacter::NativePlayerCancelReload()
{
    if (!bPlayerActionsNativeEnabled || bReloadCancelGuard)
    {
        return;
    }
    TGuardValue<bool> CancelGuard(bReloadCancelGuard, true);
    AWeaponRuntime* Weapon = ReloadWeapon.Get();
    int32 RequestId = ActiveReloadRequestId;
    if (RequestId <= 0 && bReloadStarting && IsValid(Weapon))
    {
        // A Blueprint failure/cancel can arrive synchronously from OnReloadStarted,
        // before NativePlayerStartReload has published the new request id.
        RequestId = Weapon->GetReloadRequestId();
    }
    const bool bHadRequest = IsValid(Weapon) && RequestId > 0;
    // Context/timer are cleared even when the weak target has already gone.
    ClearReloadContextNative();
    if (!bHadRequest ||
        !Weapon->IsReloading() ||
        Weapon->GetReloadRequestId() != RequestId)
    {
        return;
    }
    const uint64 SerialAtCancel = ReloadSerial;
    if (UWeaponPresentationComponent* Presentation =
        NativeActionsPresentation.Get())
    {
        if (IsValid(Presentation) && Presentation->IsRegistered())
        {
            // Only this matching playback is torn down; other/new presentations are untouched.
            Presentation->CancelReloadWeaponPresentation(true);
            if (SerialAtCancel == ReloadSerial && IsValid(Weapon) &&
                Weapon->IsReloading() &&
                Weapon->GetReloadRequestId() == RequestId)
            {
                // The fallback path has no presentation playback to cancel the transaction.
                Weapon->CancelReload();
            }
            if (SerialAtCancel == ReloadSerial)
            {
                Presentation->StopReloadPresentationSounds();
            }
        }
    }
    if (SerialAtCancel == ReloadSerial && IsValid(Weapon) &&
        Weapon->IsReloading() &&
        Weapon->GetReloadRequestId() == RequestId)
    {
        Weapon->CancelReload();
    }
}

void ADust2PlayerCharacter::HandlePresentationReloadCompleted(
    AWeaponRuntime* FormalWeapon, int32 ReloadRequestId)
{
    if (!bPlayerActionsNativeEnabled)
    {
        return;
    }
    // A fallback-owned request must not be completed by an unrelated montage event.
    if (!bReloadAnimationDriven)
    {
        return;
    }
    CompleteNativeReloadInternal(FormalWeapon, ReloadRequestId, true);
}

void ADust2PlayerCharacter::HandlePresentationReloadRoundCommitted(
    AWeaponRuntime* FormalWeapon, int32 ReloadRequestId)
{
    if (!bPlayerActionsNativeEnabled || !IsValid(FormalWeapon) ||
        ReloadWeapon.Get() != FormalWeapon ||
        ReloadRequestId <= 0 ||
        ActiveReloadRequestId != ReloadRequestId)
    {
        return;
    }
    if (!FormalWeapon->IsReloading() ||
        FormalWeapon->GetReloadRequestId() != ReloadRequestId)
    {
        return;
    }
    // HUD refresh only; the existing per-round transaction stays untouched.
#if WITH_DEV_AUTOMATION_TESTS
    ++NativeAmmoChangedCount;
#endif
    ReceivePlayerAmmoChangedNative(FormalWeapon);
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
