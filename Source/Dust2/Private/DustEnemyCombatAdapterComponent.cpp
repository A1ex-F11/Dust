#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyVisibility.h"
#include "DustActorState.h"

#include "AIController.h"
#include "Components/StateTreeAIComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyWeaponComponent.h"
#include "DustEnemyReloadComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"
#include "Templates/UnrealTemplate.h"
#include "WeaponRuntime.h"
#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"

DEFINE_LOG_CATEGORY_STATIC(LogDustEnemyLifecycle, Log, All);

namespace
{

	float SafeNonNegative(float Value, float DefaultValue)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : DefaultValue;
	}

	float SafePositive(float Value, float DefaultValue)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.01f, Value) : DefaultValue;
	}

	const UDustEnemyCombatProfile* GetProfile(const UDustEnemyWeaponComponent* Weapon)
	{
		return IsValid(Weapon) ? Weapon->CombatProfile.Get() : nullptr;
	}
}

UDustEnemyCombatAdapterComponent::UDustEnemyCombatAdapterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
	TargetClass = ADust2PlayerCharacter::StaticClass();
}

bool UDustEnemyCombatAdapterComponent::Fail(const FString& Message)
{
	LastDiagnostic = Message;
	UE_LOG(LogDustEnemyLifecycle, Warning, TEXT("%s: %s"), *GetNameSafe(GetOwner()), *Message);
	return false;
}

bool UDustEnemyCombatAdapterComponent::IsMissionOver() const
{
	return DustActorState::IsMissionOver(GetWorld());
}

bool UDustEnemyCombatAdapterComponent::IsLifeCurrent() const
{
	return IsInitialized() && Controller.IsValid() && BoundPawn.IsValid()
		&& Controller->GetPawn() == BoundPawn.Get() && !DustActorState::IsDead(BoundPawn.Get())
		&& Tree.IsValid() && WeaponComponent.IsValid() && !IsMissionOver();
}

AAIController* UDustEnemyCombatAdapterComponent::GetAIController() const
{
	return Controller.Get();
}

bool UDustEnemyCombatAdapterComponent::IsEligibleTarget(AActor* Actor) const
{
	return TargetClass && IsValid(Actor) && Actor->IsA(TargetClass.Get())
		&& Actor != BoundPawn.Get() && Actor != GetOwner() && !DustActorState::IsDead(Actor);
}

AActor* UDustEnemyCombatAdapterComponent::GetTarget() const
{
	return HasKnownTarget() ? Target.Get() : nullptr;
}

AActor* UDustEnemyCombatAdapterComponent::GetKnownTarget() const
{
	return HasKnownTarget() ? Target.Get() : nullptr;
}

AActor* UDustEnemyCombatAdapterComponent::GetConfirmedTarget() const
{
	return HasConfirmedTarget() ? Target.Get() : nullptr;
}

bool UDustEnemyCombatAdapterComponent::HasKnownTarget() const
{
	return IsLifeCurrent() && !bInterrupting && IsEligibleTarget(Target.Get());
}

bool UDustEnemyCombatAdapterComponent::HasConfirmedTarget() const
{
	return bConfirmedTarget && !bInLossGrace && !bDeferredReloadSightLoss
		&& HasKnownTarget() && CanConfirmKnownTarget();
}

bool UDustEnemyCombatAdapterComponent::NeedsReacquire() const
{
	// Cover the interval between a live visibility query failing and the next 0.1s adapter
	// tick starting grace. Otherwise Root can have no selectable state in that interval.
	return HasKnownTarget() && !bDeferredReloadSightLoss && !IsReloadBusy() && !HasConfirmedTarget();
}

bool UDustEnemyCombatAdapterComponent::IsConfirmedTarget(const AActor* Actor) const
{
	return IsValid(Actor) && GetConfirmedTarget() == Actor;
}

bool UDustEnemyCombatAdapterComponent::IsReloadBusy() const
{
	const APawn* Pawn = BoundPawn.Get();
	if (!IsValid(Pawn)) return false;
	const UDustEnemyReloadComponent* Reload = Pawn->FindComponentByClass<UDustEnemyReloadComponent>();
	return IsValid(Reload) && Reload->IsBusy();
}

bool UDustEnemyCombatAdapterComponent::CanRunIdleLogic() const
{
	return IsLifeCurrent() && !bInterrupting && !Target.IsValid() && !bSearching && !bCreateSpaceActive;
}

bool UDustEnemyCombatAdapterComponent::CanRunCombatLogic() const
{
	// Read-only package for evaluators/tasks: no new authority, no state changes.
	// The known target may still be unconfirmed during loss grace/deferred reload; consumers that
	// aim/fire must use HasConfirmedTarget/IsConfirmedTarget instead.
	return IsLifeCurrent() && !bInterrupting && IsValid(GetTarget()) && !bSearching && !bCreateSpaceActive;
}

bool UDustEnemyCombatAdapterComponent::CanRunReloadLogic() const
{
	return IsLifeCurrent() && !bInterrupting && HasKnownTarget() && !bSearching;
}

bool UDustEnemyCombatAdapterComponent::CanCreateSpace() const
{
	if (!IsLifeCurrent() || bInterrupting || bSearching || bCreateSpaceActive) return false;
	if (!PathFollowing.IsValid() || !HasConfirmedTarget()) return false;
	if (IsReloadBusy()) return false;
	const AActor* Known = Target.Get();
	const APawn* Pawn = BoundPawn.Get();
	if (!IsValid(Known) || !IsValid(Pawn) || !GetWorld()) return false;
	if (!WeaponComponent.IsValid() || !IsValid(WeaponComponent->GetWeaponRuntime())
		|| WeaponComponent->GetWeaponRuntime()->GetCurrentAmmo() <= 0
		|| WeaponComponent->GetWeaponRuntime()->IsReloading()) return false;
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float Trigger = SafeNonNegative(Profile ? Profile->CreateSpaceTriggerDistance : 300.0f, 300.0f);
	if (GetWorld()->GetTimeSeconds() < NextSpacingAllowedAt) return false;
	return FVector::Dist2D(Pawn->GetActorLocation(), Known->GetActorLocation()) <= Trigger || ShouldCombatSpread();
}

bool UDustEnemyCombatAdapterComponent::IsSpacingTarget(const AActor* Actor) const
{
	return bCreateSpaceActive && IsValid(Actor) && SpacingTarget.Get() == Actor;
}

void UDustEnemyCombatAdapterComponent::ClearQuickReacquire()
{
	QuickReacquireTarget.Reset();
	QuickReacquireExpiry = 0.0f;
	bQuickReacquireAvailable = false;
}

void UDustEnemyCombatAdapterComponent::ClearKnowledgeState()
{
	bSightCurrentlySensed = false;
	bConfirmedTarget = false;
	bInLossGrace = false;
	GraceStartTime = 0.0f;
	LastConfirmedTime = 0.0f;
	ClearQuickReacquire();
}

bool UDustEnemyCombatAdapterComponent::ConsumeQuickReacquire(AActor* TargetActor)
{
	if (!bQuickReacquireAvailable || !IsValid(TargetActor) || !GetWorld()) return false;
	if (QuickReacquireTarget.Get() != TargetActor || GetWorld()->GetTimeSeconds() > QuickReacquireExpiry) return false;
	if (!IsConfirmedTarget(TargetActor)) return false;
	bQuickReacquireAvailable = false;
	QuickReacquireTarget.Reset();
	QuickReacquireExpiry = 0.0f;
	return true;
}

void UDustEnemyCombatAdapterComponent::ConfirmKnownTarget(AActor* Actor, const FVector& Location)
{
	if (!IsLifeCurrent() || bInterrupting || Target.Get() != Actor || !IsEligibleTarget(Actor)) return;
	const bool bWasLost = !bConfirmedTarget || bInLossGrace || bDeferredReloadSightLoss;
	if (bWasLost) ++KnowledgeGeneration;
	bConfirmedTarget = true;
	bInLossGrace = false;
	bDeferredReloadSightLoss = false;
	GraceStartTime = 0.0f;
	LastConfirmedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (!Location.ContainsNaN()) LastKnownLocation = Location;
}

void UDustEnemyCombatAdapterComponent::BeginLossGrace(AActor* Actor, const FVector& Location)
{
	// Both perception events and physical occlusion use this single loss policy. A failed
	// stimulus is never a position/time observation; Location is intentionally not consumed.
	if (!IsLifeCurrent() || bInterrupting || Target.Get() != Actor) return;
	if (!IsEligibleTarget(Actor)) { Lose(Actor, LastKnownLocation, true); return; }
	if (bInLossGrace || bDeferredReloadSightLoss) return;
	bConfirmedTarget = false;
	bDeferredReloadSightLoss = IsReloadBusy();
	bInLossGrace = !bDeferredReloadSightLoss;
	GraceStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	DeferredReloadLossLocation = LastKnownLocation;
	QuickReacquireTarget = Actor;
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	QuickReacquireExpiry = LastConfirmedTime + SafeNonNegative(Profile
		? Profile->QuickReacquireWindowSeconds : 3.0f, 3.0f);
	bQuickReacquireAvailable = false;
	const uint64 Serial = ++KnowledgeGeneration;
	auto StillOwnsLoss = [this, Actor, Serial]()
	{
		return IsLifeCurrent() && !bInterrupting && Target.Get() == Actor
			&& KnowledgeGeneration == Serial && !bConfirmedTarget;
	};
	LastDiagnostic = bDeferredReloadSightLoss
		? TEXT("Sight lost during reload; frozen search deferred until reload ends")
		: TEXT("Sight lost; target frozen for the loss grace");
	if (ActiveSpacingToken != 0) CancelCreateSpace(ActiveSpacingToken);
	if (!StillOwnsLoss()) return;
	if (WeaponComponent.IsValid()) WeaponComponent->StopEngagement();
	if (!StillOwnsLoss()) return;
	Controller->StopMovement();
	if (!StillOwnsLoss()) return;
	Controller->ClearFocus(EAIFocusPriority::Gameplay);
	const float Grace = SafeNonNegative(Profile ? Profile->LostSightGraceSeconds : 0.8f, 0.8f);
	if (!bDeferredReloadSightLoss && Grace <= 0.0f) Lose(Actor, LastKnownLocation, false);
}

void UDustEnemyCombatAdapterComponent::ResumeIdleLogic()
{
	if (!CanRunIdleLogic() || Tree->IsRunning()) return;
	const uint64 Serial = Generation;
	Tree->StartLogic();
	if (!IsLifeCurrent() || Generation != Serial)
	{
		// A reentrant perception acquisition may already own a newer combat tree.
		if (Tree.IsValid() && (bStopped || (!GetTarget() && !bSearching)))
			Tree->StopLogic(TEXT("Interrupted during idle startup"));
		return;
	}
	if (!Tree->IsRunning()) Fail(TEXT("Idle StateTree did not start; check asset/schema/context bindings"));
	else { bHasStartedTree = true; LastDiagnostic = TEXT("Idle/patrol owned by StateTree"); }
}

bool UDustEnemyCombatAdapterComponent::InitializeCombatAI(UDustEnemyWeaponComponent* Weapon)
{
	if (bInterrupting) return Fail(TEXT("Cannot initialize during cleanup"));
	AAIController* OwnerController = Cast<AAIController>(GetOwner());
	APawn* Pawn = OwnerController ? OwnerController->GetPawn() : nullptr;
	if (!IsValid(Pawn) || DustActorState::IsDead(Pawn) || IsMissionOver())
		return Fail(TEXT("Missing/live Pawn or mission already ended"));
	if (bStopped && StoppedPawn.Get() == Pawn)
		return Fail(TEXT("This Pawn life has already stopped"));
	if (bInitialized)
		return IsLifeCurrent() && WeaponComponent.Get() == Weapon;
	// An idempotent initialization call must preserve a live reload's deferred search.
	bDeferredReloadSightLoss = false;
	if (!TargetClass || !IsValid(Weapon) || Weapon->GetOwner() != Pawn || !IsValid(Weapon->GetWeaponRuntime()))
		return Fail(TEXT("Set TargetClass; complete muzzle, weapon and damage bridge setup first"));
	UStateTreeAIComponent* StateTree = OwnerController->FindComponentByClass<UStateTreeAIComponent>();
	UAIPerceptionComponent* Senses = OwnerController->GetAIPerceptionComponent();
	UPathFollowingComponent* Paths = OwnerController->GetPathFollowingComponent();
	if (!IsValid(StateTree) || !StateTree->IsRegistered() || !IsValid(Senses) || !IsValid(Paths))
		return Fail(TEXT("Missing registered StateTreeAI, AI perception or path following component"));
	if (StateTree->IsRunning() || (OwnerController->GetBrainComponent() && OwnerController->GetBrainComponent() != StateTree))
		return Fail(TEXT("Disable automatic/old Blueprint StateTree startup before initializing"));

	Controller = OwnerController;
	BoundPawn = Pawn;
	WeaponComponent = Weapon;
	Tree = StateTree;
	Perception = Senses;
	PathFollowing = Paths;
	bStopped = false;
	bInitialized = true;
	bHasStartedTree = false;
	NextTreeRecoveryAt = 0.0f;
	bRetryUsed = false;
	NextHearingAllowedAt = 0.0f;
	HearingListenStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	bConfirmedTarget = false;
	bInLossGrace = false;
	GraceStartTime = 0.0f;
	LastConfirmedTime = 0.0f;
	ClearQuickReacquire();
	ActiveSpacingToken = 0;
	SpacingMoveId = FAIRequestID::InvalidRequest;
	ImmediateSpacingMoveId = FAIRequestID::InvalidRequest;
	SpacingStatus = EDustCreateSpaceStatus::Failed;
	bCreateSpaceActive = false;
	bIssuingSpacingMove = false;
	NextSpacingAllowedAt = 0.0f;
	bSpacingIsCombatSpread = false;
	RestoreSpacingMovementSpeed();
	SpacingTarget.Reset();
	SpacingDestination = FVector::ZeroVector;
	++Generation;
	++KnowledgeGeneration;
	ApplyLocalAvoidance();
	Senses->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &UDustEnemyCombatAdapterComponent::HandlePerception);
	OwnerController->OnPossessedPawnChanged.AddUniqueDynamic(this, &UDustEnemyCombatAdapterComponent::HandlePawnChanged);
	Pawn->OnDestroyed.AddUniqueDynamic(this, &UDustEnemyCombatAdapterComponent::HandlePawnDestroyed);
	MoveFinishedHandle = Paths->OnRequestFinished.AddUObject(this, &UDustEnemyCombatAdapterComponent::HandleMoveFinished);
	SetComponentTickEnabled(true);
	LastDiagnostic = TEXT("Ready; starting StateTree after initial sight handoff");
	UE_LOG(LogDustEnemyLifecycle, Log, TEXT("[W09-SIGHT] Init controller=%s pawn=%s perception=%s filter=%s life=%d"),
		*GetPathNameSafe(OwnerController), *GetNameSafe(Pawn), *GetPathNameSafe(Senses), *GetNameSafe(TargetClass.Get()), IsLifeCurrent());
	AcquireAlreadyVisibleTarget();
	ResumeIdleLogic();
	return IsInitialized();
}

void UDustEnemyCombatAdapterComponent::ClearSearch()
{
	++Generation;
	bSearching = false;
	bHearingSearch = false;
    HearingSearchPriority = 0;
	HearingDeadline = 0.0f;
	NextHearingUpdateAt = 0.0f;
	HearingRedirectCount = 0;
	SearchMoveId = FAIRequestID::InvalidRequest;
	ImmediateMoveId = FAIRequestID::InvalidRequest;
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(SearchTimer);
	if (AActor* Actor = SearchTarget.Get())
		Actor->OnDestroyed.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
	SearchTarget.Reset();
}

bool UDustEnemyCombatAdapterComponent::InterruptActivity()
{
	// AbortMove can invoke external callbacks too; no acquisition may interleave this handoff.
	TGuardValue<bool> Guard(bInterrupting, true);
	bDeferredReloadSightLoss = false;
	if (ActiveSpacingToken != 0) CancelCreateSpace(ActiveSpacingToken);
	ClearSearch();
	const uint64 Serial = Generation;
	if (Tree.IsValid()) Tree->StopLogic(TEXT("Dust enemy handoff"));
	// Fallback for a task that never received ExitState: cancel only this Pawn's own reload/move.
	if (APawn* Pawn = BoundPawn.Get())
		if (UDustEnemyReloadComponent* Reload = Pawn->FindComponentByClass<UDustEnemyReloadComponent>())
			Reload->StopAllOperations();
	if (!IsLifeCurrent() || Generation != Serial) return false;
	WeaponComponent->StopEngagement();
	if (!IsLifeCurrent() || Generation != Serial) return false;
	Controller->StopMovement();
	Controller->ClearFocus(EAIFocusPriority::Gameplay);
	return IsLifeCurrent() && Generation == Serial;
}

void UDustEnemyCombatAdapterComponent::Acquire(AActor* Actor, const FVector& Location)
{
	if (!IsLifeCurrent() || bInterrupting || !IsEligibleTarget(Actor)) return;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	// Identity + time window were captured before this confirmation; used only by the weapon start.
	const bool bWasQuickCandidate = QuickReacquireTarget.Get() == Actor && Now <= QuickReacquireExpiry;
	const float QuickExpiry = QuickReacquireExpiry;
	// Acquire is called only by normal Sight confirmation (including initial perceived actors).
	if (Target.Get() == Actor) bSightCurrentlySensed = true;
	// Re-sensing the same target ends any deferred reload sight loss before the early return.
	if (bDeferredReloadSightLoss && Target.Get() == Actor)
	{
		const bool bWasLost = !bConfirmedTarget;
		ConfirmKnownTarget(Actor, Location);
		if (bWasLost) bQuickReacquireAvailable = bWasQuickCandidate;
		return;
	}
	if (Target.Get() == Actor)
	{
		const bool bWasLost = !bConfirmedTarget || bInLossGrace;
		ConfirmKnownTarget(Actor, Location);
		if (bWasLost) bQuickReacquireAvailable = bWasQuickCandidate;
		return;
	}
	if (GetConfirmedTarget()) return;
	if (!InterruptActivity())
	{
		UE_LOG(LogDustEnemyLifecycle, Log, TEXT("[W09-SIGHT] Acquire interrupted controller=%s life=%d stopped=%d"),
			*GetNameSafe(GetOwner()), IsLifeCurrent(), bStopped);
		return;
	}
	if (AActor* PreviousTarget = Target.Get())
		PreviousTarget->OnDestroyed.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
	ClearKnowledgeState();
	bSightCurrentlySensed = true;
	Target = Actor;
	bConfirmedTarget = true;
	LastKnownLocation = Location;
	LastConfirmedTime = Now;
	++KnowledgeGeneration;
	// A search reacquire inside the same-target window keeps the fast reaction; a brand new first
	// acquisition and a different target never inherit it.
	bQuickReacquireAvailable = bWasQuickCandidate;
	if (bQuickReacquireAvailable)
	{
		QuickReacquireTarget = Actor;
		QuickReacquireExpiry = QuickExpiry;
	}
	else
	{
		QuickReacquireTarget.Reset();
		QuickReacquireExpiry = 0.0f;
	}
	Actor->OnDestroyed.AddUniqueDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
	const uint64 Serial = Generation;
	Tree->StartLogic();
	// StartLogic can synchronously stop this life or replace the request.
	if (!IsLifeCurrent() || Generation != Serial)
	{
		if (Tree.IsValid() && (bStopped || !GetTarget())) Tree->StopLogic(TEXT("Interrupted during startup"));
		return;
	}
	if (!Tree->IsRunning())
	{
		Actor->OnDestroyed.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
		Target.Reset();
		ClearKnowledgeState();
		++KnowledgeGeneration;
		InterruptActivity();
		Fail(TEXT("StateTree did not start; check asset/schema/context bindings"));
		return;
	}
	bHasStartedTree = true;
	LastDiagnostic = TEXT("Player acquired; combat owned by StateTree");
	UE_LOG(LogDustEnemyLifecycle, Log, TEXT("[W09-SIGHT] Acquired controller=%s target=%s treeRunning=%d"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Actor), Tree->IsRunning());
}

void UDustEnemyCombatAdapterComponent::Lose(AActor* Actor, const FVector& Location, bool bEliminated)
{
	if (!IsLifeCurrent() || bInterrupting || Target.Get() != Actor) return;
	// Any real loss/cleanup takes over from a pending reload-driven deferral.
	bDeferredReloadSightLoss = false;
	const float LastConfirmed = LastConfirmedTime;
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float Window = SafeNonNegative(Profile ? Profile->QuickReacquireWindowSeconds : 3.0f, 3.0f);
	if (IsValid(Actor)) Actor->OnDestroyed.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
	ClearKnowledgeState();
	Target.Reset();
	++KnowledgeGeneration;
	if (!bEliminated && IsValid(Actor))
	{
		// The search/known identity stays eligible for a short fast reacquire after the last confirm.
		QuickReacquireTarget = Actor;
		QuickReacquireExpiry = LastConfirmed + Window;
	}
	if (!Location.ContainsNaN()) LastKnownLocation = Location;
	if (!InterruptActivity()) return;
	if (bEliminated) { LastDiagnostic = TEXT("Player eliminated; no search"); ResumeIdleLogic(); return; }
	SearchTarget = Actor;
	Actor->OnDestroyed.AddUniqueDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
	bSearching = true;
	bRetryUsed = false;
	LastDiagnostic = TEXT("Sight lost; moving to last known location");
	StartSearchMove(Generation);
}

bool UDustEnemyCombatAdapterComponent::CanRedirectHearing(AActor* Actor, const FVector& EventLocation,
    float Now, int32 IncomingPriority) const
{
    if (!bHearingSearch || !IsSearchCurrent(Generation) || SearchTarget.Get() != Actor
        || Now >= HearingDeadline || IncomingPriority < HearingSearchPriority) return false;
    // Footstep(1) can upgrade once to Gunshot(2), even during the footstep throttle.
    // Updating the stored priority prevents another upgrade or a later footstep downgrade.
    if (HearingSearchPriority > 0 && IncomingPriority > HearingSearchPriority) return true;
    if (Now < NextHearingUpdateAt
        || HearingRedirectCount >= FMath::Clamp(HearingMaxRedirects, 0, 10)) return false;
    const double MinDistanceCm = 100.0 * SafePositive(HearingUpdateDistanceMeters, 2.0f);
    return FVector::DistSquared(LastKnownLocation, EventLocation) >= FMath::Square(MinDistanceCm);
}

void UDustEnemyCombatAdapterComponent::HandleHearing(AActor* Actor, const FAIStimulus& Stimulus)
{
    static const FName FootstepTag(TEXT("Footstep"));
    static const FName GunshotTag(TEXT("Gunshot"));
    const int32 IncomingPriority = Stimulus.Tag == GunshotTag ? 2 : 1;
    if (!bEnableHearingInvestigation || !IsLifeCurrent() || bInterrupting
        || !IsEligibleTarget(Actor) || Target.IsValid()
        || Stimulus.Type != UAISense::GetSenseID<UAISense_Hearing>()
        || (Stimulus.Tag != FootstepTag && Stimulus.Tag != GunshotTag) || !Stimulus.WasSuccessfullySensed() || Stimulus.IsExpired()
        || !FMath::IsFinite(Stimulus.Strength) || Stimulus.Strength <= 0.0f
        || !FAISystem::IsValidLocation(Stimulus.StimulusLocation)) return;
    const UWorld* World = GetWorld();
    if (!World) return;
    const float Now = World->GetTimeSeconds();
    const float Age = Stimulus.GetAge();
    if (!FMath::IsFinite(Age) || Age < 0.0f
        || Age > SafePositive(HearingMaxStimulusAge, 1.5f)
        || Now - Age + KINDA_SMALL_NUMBER < HearingListenStartTime) return;

    if (bSearching)
    {
        // A sight-loss search has priority. Same-source footsteps may redirect only finitely.
        if (!CanRedirectHearing(Actor, Stimulus.StimulusLocation, Now, IncomingPriority)) return;
        const bool bPriorityUpgrade = IncomingPriority > HearingSearchPriority;

        // Invalidate the old callback identity BEFORE AbortMove can synchronously notify.
        const FAIRequestID PreviousMove = SearchMoveId;
        const uint64 Serial = ++Generation;
        SearchMoveId = FAIRequestID::InvalidRequest;
        ImmediateMoveId = FAIRequestID::InvalidRequest;
        GetWorld()->GetTimerManager().ClearTimer(SearchTimer);
        {
            TGuardValue<bool> Guard(bInterrupting, true);
            if (PathFollowing.IsValid() && PreviousMove.IsValid()
                && PathFollowing->GetCurrentRequestId().GetID() == PreviousMove.GetID())
                PathFollowing->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, PreviousMove);
        }
        if (!IsSearchCurrent(Serial))
        {
            if (IsLifeCurrent() && !bSearching && !Target.IsValid()) ResumeIdleLogic();
            return;
        }
        LastKnownLocation = Stimulus.StimulusLocation;
        if (!bPriorityUpgrade) ++HearingRedirectCount;
        HearingSearchPriority = IncomingPriority;
        NextHearingUpdateAt = Now + FMath::Max(0.1f, SafePositive(HearingUpdateInterval, 0.75f));
        // Keep the original deadline and the single shared failure-retry budget.
        LastDiagnostic = TEXT("Hearing: updated event location; deadline unchanged");
        StartSearchMove(Serial);
        return;
    }

    if (Now < NextHearingAllowedAt || !InterruptActivity()) return;
    if (!IsEligibleTarget(Actor))
    {
        ResumeIdleLogic();
        return;
    }
    ClearQuickReacquire();
    SearchTarget = Actor;
    Actor->OnDestroyed.AddUniqueDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
    LastKnownLocation = Stimulus.StimulusLocation;
    bSearching = true;
    bHearingSearch = true;
    HearingSearchPriority = IncomingPriority;
    bRetryUsed = false;
    HearingRedirectCount = 0;
    HearingDeadline = Now + FMath::Max(0.1f, SafePositive(HearingMaxSeconds, 12.0f));
    NextHearingUpdateAt = Now + FMath::Max(0.1f, SafePositive(HearingUpdateInterval, 0.75f));
    LastDiagnostic = TEXT("Hearing: investigating noise event location");
    UE_LOG(LogDustEnemyLifecycle, Verbose, TEXT("[W10-HEARING] controller=%s source=%s location=%s deadline=%.2f"),
        *GetNameSafe(GetOwner()), *GetNameSafe(Actor), *LastKnownLocation.ToCompactString(), HearingDeadline);
    StartSearchMove(Generation);
}

void UDustEnemyCombatAdapterComponent::HandlePerception(AActor* Actor, FAIStimulus Stimulus)
{
    if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
    {
        HandleHearing(Actor, Stimulus);
        return;
    }
	if (IsValid(Actor) && TargetClass && Actor->IsA(TargetClass.Get()))
	{
		const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(Actor);
		UE_LOG(LogDustEnemyLifecycle, Log, TEXT("[W09-SIGHT] Event controller=%s actor=%s sensed=%d sight=%d expired=%d life=%d interrupt=%d eligible=%d playerHP=%.1f"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor), Stimulus.WasSuccessfullySensed(),
			Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>(), Stimulus.IsExpired(),
			IsLifeCurrent(), bInterrupting, IsEligibleTarget(Actor), Player ? Player->GetCurrentHealth() : -1.0f);
	}
	if (!IsLifeCurrent() || bInterrupting || Stimulus.Type != UAISense::GetSenseID<UAISense_Sight>()) return;
	if (Stimulus.WasSuccessfullySensed() && !Stimulus.IsExpired())
	{
		if (HasPhysicalLineOfSight(Actor)) Acquire(Actor, Actor->GetActorLocation());
	}
	else if (Target.Get() == Actor)
	{
		bSightCurrentlySensed = false;
		if (!IsEligibleTarget(Actor)) { Lose(Actor, LastKnownLocation, true); return; }
		// A nearby known target leaving the forward sight cone is not a combat loss.
		if (CanConfirmKnownTarget())
		{
			const bool bWasLost = !bConfirmedTarget;
			const bool bQuick = QuickReacquireTarget.Get() == Actor
				&& GetWorld()->GetTimeSeconds() <= QuickReacquireExpiry;
			ConfirmKnownTarget(Actor, Actor->GetActorLocation());
			if (bWasLost) bQuickReacquireAvailable = bQuick;
			return;
		}
		BeginLossGrace(Actor, LastKnownLocation);
	}
}


void UDustEnemyCombatAdapterComponent::AcquireAlreadyVisibleTarget()
{
	if (!Perception.IsValid() || !IsLifeCurrent()) return;
	TArray<AActor*> Actors;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Actors);
	for (AActor* Actor : Actors)
	{
		if (!IsEligibleTarget(Actor) || !HasPhysicalLineOfSight(Actor)) continue;
		Acquire(Actor, Actor->GetActorLocation());
		if (GetConfirmedTarget()) break;
	}
}

bool UDustEnemyCombatAdapterComponent::IsSearchCurrent(uint64 Serial) const
{
	return IsLifeCurrent() && !bInterrupting && bSearching && Generation == Serial
		&& !Target.IsValid() && IsEligibleTarget(SearchTarget.Get());
}

void UDustEnemyCombatAdapterComponent::StartSearchMove(uint64 Serial)
{
	if (!IsSearchCurrent(Serial)) return;
    if (bHearingSearch && GetWorld()->GetTimeSeconds() >= HearingDeadline)
    {
        FinishSearch(Serial);
        return;
    }
	FAIMoveRequest Request;
	Request.SetGoalLocation(LastKnownLocation);
	Request.SetAcceptanceRadius(FMath::Max(0.0f, SearchAcceptanceRadius));
	Request.SetUsePathfinding(true);
	Request.SetProjectGoalLocation(true);
	Request.SetAllowPartialPath(false);
	ImmediateMoveId = FAIRequestID::InvalidRequest;
	FPathFollowingRequestResult Move;
	{
		TGuardValue<bool> Guard(bIssuingMove, true);
		Move = Controller->MoveTo(Request);
	}
	if (!IsSearchCurrent(Serial)) return;
	if (Move.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		SearchMoveId = Move.MoveId;
		// Covers synchronous completion during MoveTo; unrelated aborted moves cannot match.
		if (ImmediateMoveId.IsValid() && ImmediateMoveId.GetID() == SearchMoveId.GetID())
			ResolveSearchMove(Serial, bImmediateMoveSucceeded);
	}
	else ResolveSearchMove(Serial, Move.Code == EPathFollowingRequestResult::AlreadyAtGoal);
}

bool UDustEnemyCombatAdapterComponent::HasPhysicalLineOfSight(const AActor* Actor) const
{
	const APawn* Pawn = BoundPawn.Get();
	if (!IsValid(Pawn) || !IsValid(Actor)) return false;
	FVector Eye; FRotator Rotation;
	Pawn->GetActorEyesViewPoint(Eye, Rotation);
	return DustEnemyVisibility::HasSight(Pawn, Actor, Eye,
		DustEnemyVisibility::GetAimPoint(Actor, GetProfile(WeaponComponent.Get())));
}

bool UDustEnemyCombatAdapterComponent::CanConfirmKnownTarget() const
{
	const APawn* Pawn = BoundPawn.Get();
	const AActor* Known = Target.Get();
	if (!IsValid(Pawn) || !IsEligibleTarget(Target.Get())) return false;
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float CloseRange = SafeNonNegative(Profile ? Profile->CloseCombatSenseRange : 500.0f, 500.0f);
	const bool bCloseKnown = CloseRange > 0.0f
		&& FVector::Dist2D(Pawn->GetActorLocation(), Known->GetActorLocation()) <= CloseRange;
	return (bSightCurrentlySensed || bCloseKnown) && HasPhysicalLineOfSight(Known);
}

void UDustEnemyCombatAdapterComponent::HandleMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result)
{
	if (bIssuingMove)
	{
		ImmediateMoveId = RequestId;
		bImmediateMoveSucceeded = Result.IsSuccess();
		return;
	}
	if (bIssuingSpacingMove)
	{
		ImmediateSpacingMoveId = RequestId;
		bImmediateSpacingSucceeded = Result.IsSuccess();
		return;
	}
	if (bCreateSpaceActive && SpacingMoveId.IsValid() && RequestId.GetID() == SpacingMoveId.GetID())
	{
		ResolveSpacingMove(Result.IsSuccess());
		return;
	}
	if (!IsSearchCurrent(Generation) || !SearchMoveId.IsValid() || RequestId.GetID() != SearchMoveId.GetID()) return;
	ResolveSearchMove(Generation, Result.IsSuccess());
}

void UDustEnemyCombatAdapterComponent::ResolveSearchMove(uint64 Serial, bool bSucceeded)
{
	if (!IsSearchCurrent(Serial)) return;
	SearchMoveId = FAIRequestID::InvalidRequest;
	if (bSucceeded)
	{
		LastDiagnostic = TEXT("At last known location; waiting");
		if (SearchWaitSeconds <= 0.0f) { FinishSearch(Serial); return; }
		GetWorld()->GetTimerManager().SetTimer(SearchTimer, FTimerDelegate::CreateWeakLambda(this,
			[this, Serial]() { FinishSearch(Serial); }), SearchWaitSeconds, false);
	}
	else if (!bRetryUsed)
	{
		bRetryUsed = true;
		GetWorld()->GetTimerManager().SetTimer(SearchTimer, FTimerDelegate::CreateWeakLambda(this,
			[this, Serial]() { StartSearchMove(Serial); }), FMath::Max(0.01f, SearchRetrySeconds), false);
	}
	else FinishSearch(Serial);
}

void UDustEnemyCombatAdapterComponent::FinishSearch(uint64 Serial)
{
	if (!IsSearchCurrent(Serial)) return;
    if (bHearingSearch)
        NextHearingAllowedAt = GetWorld()->GetTimeSeconds() + SafeNonNegative(HearingCooldownSeconds, 2.0f);
	ClearQuickReacquire();
	if (InterruptActivity()) ResumeIdleLogic();
}

void UDustEnemyCombatAdapterComponent::ApplySpacingCooldown(bool bForCombatSpread)
{
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float Cooldown = bForCombatSpread
		? FMath::Max(0.5f, SafeNonNegative(Profile ? Profile->CombatSpreadCooldownSeconds : 2.0f, 2.0f))
		: SafeNonNegative(Profile ? Profile->CreateSpaceCooldownSeconds : 3.0f, 3.0f);
	NextSpacingAllowedAt = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f) + Cooldown;
}

bool UDustEnemyCombatAdapterComponent::ApplySpacingMovementSpeed(bool bForCombatSpread)
{
	// Never leave a previous application behind if a caller re-enters before release.
	if (bSpacingSpeedApplied) RestoreSpacingMovementSpeed();
	const ACharacter* Character = Cast<ACharacter>(BoundPawn.Get());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!IsValid(Movement))
	{
		LastDiagnostic = TEXT("Create Space needs the controlled CharacterMovement for its move speed");
		return false;
	}
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float Speed = bForCombatSpread
		? SafePositive(Profile ? Profile->CombatSpreadMoveSpeed : 180.0f, 180.0f)
		: SafePositive(Profile ? Profile->CreateSpaceMoveSpeed : 200.0f, 200.0f);
	// Save the actual object/original/applied values. If another system writes MaxWalkSpeed
	// during the move, RestoreSpacingMovementSpeed deliberately leaves that newer value alone.
	SpacingMovement = Movement;
	SpacingSavedMaxWalkSpeed = Movement->MaxWalkSpeed;
	SpacingAppliedMaxWalkSpeed = Speed;
	bSpacingSpeedApplied = true;
	Movement->MaxWalkSpeed = Speed;
	return true;
}

void UDustEnemyCombatAdapterComponent::RestoreSpacingMovementSpeed()
{
	if (bSpacingStrafeApplied)
	{
		if (AAIController* OwnerController = SpacingStrafeController.Get())
			if (OwnerController->bAllowStrafe) OwnerController->bAllowStrafe = bSpacingSavedAllowStrafe;
		bSpacingStrafeApplied = false;
		SpacingStrafeController.Reset();
	}
	if (!bSpacingSpeedApplied) return;
	UCharacterMovementComponent* Movement = SpacingMovement.Get();
	// Restore only when the live value is still the one this spacing applied.
	if (IsValid(Movement) && FMath::IsNearlyEqual(Movement->MaxWalkSpeed, SpacingAppliedMaxWalkSpeed, 0.01f))
		Movement->MaxWalkSpeed = SpacingSavedMaxWalkSpeed;
	bSpacingSpeedApplied = false;
	SpacingMovement.Reset();
	SpacingSavedMaxWalkSpeed = 0.0f;
	SpacingAppliedMaxWalkSpeed = 0.0f;
}

void UDustEnemyCombatAdapterComponent::ReleaseSpacing(bool bAbortMove, bool bApplyCooldown, bool bKeepEngagement)
{
	const bool bReleasedCombatSpread = bSpacingIsCombatSpread;
	const FAIRequestID Request = SpacingMoveId;
	const TWeakObjectPtr<AActor> ReleasedTarget = SpacingTarget;
	if (PathFollowing.IsValid() && PathFollowing->GetCurrentRequestId().IsValid()
		&& (!Request.IsValid() || PathFollowing->GetCurrentRequestId().GetID() != Request.GetID()))
	{
		// A replacement movement request now owns controller strafe settings.
		bSpacingStrafeApplied = false;
		SpacingStrafeController.Reset();
	}
	// Invalidate identity BEFORE AbortMove can run a synchronous completion callback.
	ActiveSpacingToken = 0;
	++SpacingGeneration;
	SpacingMoveId = FAIRequestID::InvalidRequest;
	ImmediateSpacingMoveId = FAIRequestID::InvalidRequest;
	SpacingDestination = FVector::ZeroVector;
	SpacingTarget.Reset();
	SpacingElapsedSeconds = 0.0f;
	SpacingStatus = EDustCreateSpaceStatus::Failed;
	bCreateSpaceActive = false;
	bIssuingSpacingMove = false;
	bImmediateSpacingSucceeded = false;
	bSpacingIsCombatSpread = false;
	RestoreSpacingMovementSpeed();
	// Stop only the engagement this spacing owned. A stale token never reaches here, so a
	// newer target/request keeps its own shooting and focus.
	if (!bKeepEngagement && WeaponComponent.IsValid() && WeaponComponent->IsEngaging()
		&& WeaponComponent->GetEngagementTarget() == ReleasedTarget.Get())
	{
		WeaponComponent->StopEngagement();
	}
	if (bApplyCooldown) ApplySpacingCooldown(bReleasedCombatSpread);
	if (bAbortMove && PathFollowing.IsValid() && Request.IsValid())
		PathFollowing->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, Request);
}

void UDustEnemyCombatAdapterComponent::ResolveSpacingMove(bool bSucceeded)
{
	if (!bCreateSpaceActive) return;
	SpacingStatus = bSucceeded ? EDustCreateSpaceStatus::Succeeded : EDustCreateSpaceStatus::Failed;
	if (bSucceeded)
	{
		LastDiagnostic = TEXT("Create Space reached its destination");
		return;
	}
	LastDiagnostic = TEXT("Create Space move failed; cooldown then resume in place");
	ReleaseSpacing(false, true, false);
}

void UDustEnemyCombatAdapterComponent::CompleteCreateSpace(uint64 Token)
{
	if (Token == 0 || Token != ActiveSpacingToken) return;
	LastDiagnostic = TEXT("Create Space completed; cooldown starts");
	// Winning the move does not stop the engagement: the next combat state keeps this same
	// target and its current reaction/volley.
	ReleaseSpacing(true, true, true);
}

void UDustEnemyCombatAdapterComponent::CancelCreateSpace(uint64 Token)
{
	if (Token == 0 || Token != ActiveSpacingToken) return;
	LastDiagnostic = TEXT("Create Space cancelled by its owner; cooldown starts");
	ReleaseSpacing(true, true, false);
}

float UDustEnemyCombatAdapterComponent::ComputeCreateSpaceStepDistance(
	float CurrentDistance, float DesiredDistance, float MaxStepDistance)
{
	const float Current = FMath::IsFinite(CurrentDistance) ? FMath::Max(0.0f, CurrentDistance) : 0.0f;
	const float Desired = FMath::IsFinite(DesiredDistance) ? FMath::Max(0.0f, DesiredDistance) : 600.0f;
	const float MaxStep = FMath::IsFinite(MaxStepDistance) ? FMath::Max(1.0f, MaxStepDistance) : 400.0f;
	if (Current >= Desired) return 0.0f;
	// Close the gap toward the desired engagement distance, never above one move's cap.
	return FMath::Clamp(Desired - Current, 1.0f, MaxStep);
}

void UDustEnemyCombatAdapterComponent::ApplyLocalAvoidance()
{
	RestoreLocalAvoidance();
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	ACharacter* Character = Cast<ACharacter>(BoundPawn.Get());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Profile || !Profile->bEnableLocalAvoidance || !IsValid(Movement)) return;
	// CrowdFollowing owns its own avoidance. Do not run both systems on the same pawn.
	if (PathFollowing.IsValid() && PathFollowing->IsA(UCrowdFollowingComponent::StaticClass())) return;
	AvoidanceMovement = Movement;
	bSavedAvoidanceEnabled = Movement->bUseRVOAvoidance;
	SavedAvoidanceRadius = Movement->AvoidanceConsiderationRadius;
	AppliedAvoidanceRadius = SafePositive(Profile->LocalAvoidanceRadius, 500.0f);
	bAvoidanceApplied = true;
	Movement->AvoidanceConsiderationRadius = AppliedAvoidanceRadius;
	Movement->SetAvoidanceEnabled(true);
}

void UDustEnemyCombatAdapterComponent::RestoreLocalAvoidance()
{
	UCharacterMovementComponent* Movement = AvoidanceMovement.Get();
	const bool bOwned = bAvoidanceApplied;
	bAvoidanceApplied = false;
	AvoidanceMovement.Reset();
	if (!bOwned || !IsValid(Movement)) return;
	if (FMath::IsNearlyEqual(Movement->AvoidanceConsiderationRadius, AppliedAvoidanceRadius))
		Movement->AvoidanceConsiderationRadius = SavedAvoidanceRadius;
	if (Movement->bUseRVOAvoidance) Movement->SetAvoidanceEnabled(bSavedAvoidanceEnabled);
}

bool UDustEnemyCombatAdapterComponent::IsSpacingSegmentClear(const FVector& Start, const FVector& End,
	float Radius, float HalfHeight) const
{
	const APawn* Pawn = BoundPawn.Get();
	UWorld* World = GetWorld();
	if (!World || !IsValid(Pawn)) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DustEnemyCreateSpaceRoute), false, Pawn);
	const UCharacterMovementComponent* Movement = AvoidanceMovement.Get();
	const bool bCanAvoidFriends = bAvoidanceApplied && IsValid(Movement) && Movement->bUseRVOAvoidance;
	for (int32 Attempt = 0; Attempt < 64; ++Attempt)
	{
		FHitResult Hit;
		if (!World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), Params)) return true;
		// Only moving corridor tests defer friendly bodies to RVO. Occupied destinations are
		// still rejected separately, and walls/players keep blocking this sweep.
		if (!bCanAvoidFriends || !DustEnemyVisibility::IsFriendlyBlocker(Pawn, Hit.GetActor())) return false;
		Params.AddIgnoredActor(Hit.GetActor());
		if (APawn* Friendly = DustEnemyVisibility::ResolvePawn(Hit.GetActor())) Params.AddIgnoredActor(Friendly);
	}
	return false;
}

bool UDustEnemyCombatAdapterComponent::FindCreateSpaceDestination(FVector& OutDestination) const
{
	const APawn* Pawn = BoundPawn.Get();
	const AActor* Known = Target.Get();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (!IsValid(Pawn) || !IsValid(Known) || !NavSystem) return false;
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float Desired = SafePositive(Profile ? Profile->DesiredEngagementDistance : 600.0f, 600.0f);
	const float MaxStep = SafePositive(Profile ? Profile->CreateSpaceStepDistance : 400.0f, 400.0f);
	const FVector PawnLocation = Pawn->GetActorLocation();
	// The destination uses this one player-position snapshot only; later ticks never chase the
	// live player distance and never queue another retreat.
	const FVector TargetLocation = Known->GetActorLocation();
	const float CurrentDistance = FVector::Dist2D(PawnLocation, TargetLocation);
	const float Step = ComputeCreateSpaceStepDistance(CurrentDistance, Desired, MaxStep);
	if (Step <= 0.0f) return false;
	FVector Away = (PawnLocation - TargetLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero()) Away = -Pawn->GetActorForwardVector().GetSafeNormal2D();
	if (Away.IsNearlyZero()) Away = FVector::ForwardVector;
	// One point directly away plus a small left/right fallback set. Side/back candidates still
	// have to increase separation, not merely be reachable.
	const float SideAngles[] = { 0.0f, 40.0f, -40.0f, 75.0f, -75.0f };
	const float MinSeparation = FMath::Min(Step,
		FMath::Max(10.0f, FMath::Min(Step * 0.5f, 150.0f)));
	const float Radius = FMath::Max(1.0f, Pawn->GetSimpleCollisionRadius());
	const float HalfHeight = FMath::Max(1.0f, Pawn->GetSimpleCollisionHalfHeight());
	for (const float SideAngle : SideAngles)
	{
		const FVector Candidate = PawnLocation - FVector(0.0f, 0.0f, HalfHeight)
			+ Away.RotateAngleAxis(SideAngle, FVector::UpVector) * Step;
		FNavLocation Projected;
		if (!NavSystem->ProjectPointToNavigation(Candidate, Projected, FVector(80.0f, 80.0f, 160.0f))) continue;
		if (FVector::Dist2D(Projected.Location, Candidate) > 80.0f) continue;
		if (FVector::Dist2D(Projected.Location, PawnLocation) > MaxStep) continue;
		if (FVector::Dist2D(Projected.Location, TargetLocation) < CurrentDistance + MinSeparation) continue;
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			World, PawnLocation, Projected.Location, const_cast<APawn*>(Pawn));
		if (!IsValid(Path) || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2) continue;
		float PathLength = 0.0f;
		for (int32 Index = 1; Index < Path->PathPoints.Num(); ++Index)
			PathLength += FVector::Dist(Path->PathPoints[Index - 1], Path->PathPoints[Index]);
		if (!FMath::IsFinite(PathLength) || PathLength > Step * 1.6f + 80.0f) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DustEnemyCreateSpace), false, Pawn);
		const FVector TestCenter = Projected.Location + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
		if (World->OverlapBlockingTestByChannel(TestCenter, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), Params)) continue;
		bool bClearRoute = true;
		FVector SegmentStart = PawnLocation + FVector(0.0f, 0.0f, 2.0f);
		for (int32 Index = 1; Index < Path->PathPoints.Num(); ++Index)
		{
			const FVector SegmentEnd = Path->PathPoints[Index] + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
			if (!IsSpacingSegmentClear(SegmentStart, SegmentEnd, Radius, HalfHeight)) { bClearRoute = false; break; }
			SegmentStart = SegmentEnd;
		}
		if (!bClearRoute) continue;
		OutDestination = Projected.Location;
		return true;
	}
	return false;
}

TArray<UDustEnemyCombatAdapterComponent::FCombatNeighbor>
UDustEnemyCombatAdapterComponent::GatherCombatNeighbors(const FVector& Center, float Radius) const
{
	TArray<FCombatNeighbor> Neighbors;
	const APawn* Pawn = BoundPawn.Get();
	UWorld* World = GetWorld();
	if (!World || !IsValid(Pawn) || Center.ContainsNaN()) return Neighbors;
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DustCombatNeighbors), false, Pawn);
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(FMath::Max(1.0f, Radius)), Params);
	TSet<const APawn*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Other = DustEnemyVisibility::ResolvePawn(Overlap.GetActor());
		if (!IsValid(Other) || Other == Pawn || DustActorState::IsDead(Other) || Seen.Contains(Other)
			|| !DustEnemyVisibility::IsFriendlyBlocker(Pawn, Other)) continue;
		Seen.Add(Other);
		if (FMath::Abs(Other->GetActorLocation().Z - Center.Z)
			> FMath::Max(1.0f, Pawn->GetSimpleCollisionHalfHeight()) + Other->GetSimpleCollisionHalfHeight()) continue;
		FCombatNeighbor Neighbor;
		Neighbor.Location = Other->GetActorLocation();
		Neighbor.Radius = FMath::Max(1.0f, Other->GetSimpleCollisionRadius());
		Neighbor.StableId = Other->GetUniqueID();
		const AController* OtherController = Other->GetController();
		const UDustEnemyCombatAdapterComponent* OtherAdapter = OtherController
			? OtherController->FindComponentByClass<UDustEnemyCombatAdapterComponent>() : nullptr;
		if (OtherAdapter && OtherAdapter->bCreateSpaceActive && OtherAdapter->ActiveSpacingToken != 0)
		{
			Neighbor.bHasReservation = true;
			Neighbor.ReservedLocation = OtherAdapter->SpacingDestination;
		}
		Neighbors.Add(Neighbor);
	}
	return Neighbors;
}

bool UDustEnemyCombatAdapterComponent::ShouldCombatSpread() const
{
	const APawn* Pawn = BoundPawn.Get();
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	if (!IsValid(Pawn) || !Profile || !Profile->bEnableCombatSpread || !HasConfirmedTarget()
		|| !WeaponComponent.IsValid()
		|| !DustEnemyVisibility::IsInRange(Pawn, Target.Get(), Profile, WeaponComponent->WeaponDefinition)) return false;
	// This is intentionally independent of Weapon.IsEngaging: the old task exits and stops
	// engagement before BeginCreateSpace revalidates admission.
	const float Desired = FMath::Max(120.0f, SafePositive(Profile->FriendlySpacingDistance, 180.0f));
	const FVector Location = Pawn->GetActorLocation();
	for (const FCombatNeighbor& Neighbor : GatherCombatNeighbors(Location, Desired + Pawn->GetSimpleCollisionRadius()))
	{
		const float Trigger = FMath::Max(Desired * 0.8f,
			Pawn->GetSimpleCollisionRadius() + Neighbor.Radius + 20.0f);
		if (Neighbor.StableId < Pawn->GetUniqueID()
			&& FVector::DistSquared2D(Neighbor.Location, Location) < FMath::Square(Trigger)) return true;
	}
	return false;
}

bool UDustEnemyCombatAdapterComponent::FindCombatSpreadDestination(FVector& OutDestination) const
{
	const APawn* Pawn = BoundPawn.Get();
	const AActor* Known = Target.Get();
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (!IsValid(Pawn) || !IsValid(Known) || !Profile || !Nav) return false;
	const float Desired = FMath::Max(120.0f, SafePositive(Profile->FriendlySpacingDistance, 180.0f));
	const float MaxStep = SafePositive(Profile->CombatSpreadStepDistance, 250.0f);
	const FVector Start = Pawn->GetActorLocation();
	const float Radius = FMath::Max(1.0f, Pawn->GetSimpleCollisionRadius());
	const float HalfHeight = FMath::Max(1.0f, Pawn->GetSimpleCollisionHalfHeight());
	// A single local snapshot includes neighbors around every candidate, not only the blocker.
	const TArray<FCombatNeighbor> Neighbors = GatherCombatNeighbors(Start, MaxStep + Desired * 2.0f);
	if (Neighbors.IsEmpty()) return false;
	FVector Repulsion = FVector::ZeroVector;
	float CurrentNearest = Desired * 2.0f;
	for (const FCombatNeighbor& Neighbor : Neighbors)
	{
		const FVector Delta = Start - Neighbor.Location;
		const float Distance = Delta.Size2D();
		CurrentNearest = FMath::Min(CurrentNearest, Distance);
		if (Distance < Desired) Repulsion += Delta.GetSafeNormal2D() / FMath::Max(20.0f, Distance);
	}
	const FVector ToPlayer = (Known->GetActorLocation() - Start).GetSafeNormal2D();
	FVector Side = FVector::CrossProduct(FVector::UpVector, ToPlayer).GetSafeNormal2D();
	if (Side.IsNearlyZero()) Side = Pawn->GetActorRightVector().GetSafeNormal2D();
	if (Repulsion.IsNearlyZero()) Repulsion = Side;
	const FVector Away = Repulsion.GetSafeNormal2D();
	const FVector Directions[] = { Away, Side, -Side,
		Away.RotateAngleAxis(45.0f, FVector::UpVector), Away.RotateAngleAxis(-45.0f, FVector::UpVector), -ToPlayer };
	const float Steps[] = { FMath::Min(MaxStep, Desired), MaxStep };
	FVector Eye; FRotator EyeRotation; Pawn->GetActorEyesViewPoint(Eye, EyeRotation);
	const FVector EyeOffset = Eye - Start;
	const FVector AimPoint = DustEnemyVisibility::GetAimPoint(Known, Profile);
	// These inputs describe the current pawn/configuration, not an individual candidate.
	if (!DustEnemyVisibility::IsInRange(Pawn, Known, Profile, WeaponComponent->WeaponDefinition)) return false;
	const UWeaponDefinitionData* Definition = WeaponComponent->WeaponDefinition;
	const float Range = FMath::Min(SafeNonNegative(Profile->EngagementRange, 0.0f),
		Definition && Definition->BallisticsData ? SafeNonNegative(Definition->BallisticsData->TraceDistance, 0.0f) : 0.0f);
	float BestScore = -1.0f;
	for (const float Step : Steps)
	for (const FVector& Direction : Directions)
	{
		if (Direction.IsNearlyZero()) continue;
		const FVector Raw = Start - FVector(0, 0, HalfHeight) + Direction * Step;
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Raw, Projected, FVector(80, 80, 160))
			|| FVector::Dist2D(Projected.Location, Raw) > 80.0f
			|| FVector::Dist2D(Projected.Location, Start) > MaxStep) continue;
		const FVector Center = Projected.Location + FVector(0, 0, HalfHeight + 2.0f);
		if (FVector::DistSquared(Center, AimPoint) > FMath::Square(Range)
			|| !DustEnemyVisibility::HasSight(Pawn, Known, Center + EyeOffset, AimPoint)) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DustCombatSpreadEnd), false, Pawn);
		if (World->OverlapBlockingTestByChannel(Center, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), Params)) continue;
		float Nearest = Desired * 2.0f;
		bool bEnoughRoom = true;
		for (const FCombatNeighbor& Neighbor : Neighbors)
		{
			float Distance = FVector::Dist2D(Center, Neighbor.Location);
			if (Neighbor.bHasReservation) Distance = FMath::Min(Distance, FVector::Dist2D(Center, Neighbor.ReservedLocation));
			Nearest = FMath::Min(Nearest, Distance);
			if (Distance < Radius + Neighbor.Radius + 20.0f) { bEnoughRoom = false; break; }
		}
		if (!bEnoughRoom || Nearest < CurrentNearest + 20.0f) continue;
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Start, Projected.Location, const_cast<APawn*>(Pawn));
		if (!IsValid(Path) || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2) continue;
		float Length = 0.0f;
		FVector SegmentStart = Start + FVector(0, 0, 2.0f);
		bool bClear = true;
		for (int32 Index = 1; Index < Path->PathPoints.Num(); ++Index)
		{
			Length += FVector::Dist(Path->PathPoints[Index - 1], Path->PathPoints[Index]);
			const FVector SegmentEnd = Path->PathPoints[Index] + FVector(0, 0, HalfHeight + 2.0f);
			if (!IsSpacingSegmentClear(SegmentStart, SegmentEnd, Radius, HalfHeight)) { bClear = false; break; }
			SegmentStart = SegmentEnd;
		}
		if (!bClear || !FMath::IsFinite(Length) || Length > MaxStep * 1.6f + 80.0f) continue;
		// Prefer useful separation, then the shorter move; no random per-tick destination chase.
		const float Score = FMath::Min(Nearest, Desired) - FVector::Dist2D(Start, Center) * 0.05f;
		if (Score > BestScore) { BestScore = Score; OutDestination = Projected.Location; }
	}
	return BestScore >= 0.0f;
}

bool UDustEnemyCombatAdapterComponent::CanCreateSpaceForReload() const
{
	if (!IsLifeCurrent() || bInterrupting || bSearching || bCreateSpaceActive
		|| !PathFollowing.IsValid() || !HasConfirmedTarget() || !IsReloadBusy() || !GetWorld()) return false;
	const AWeaponRuntime* Runtime = WeaponComponent->GetWeaponRuntime();
	if (!IsValid(Runtime) || !Runtime->IsReloading() || GetWorld()->GetTimeSeconds() < NextSpacingAllowedAt) return false;
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float Trigger = SafeNonNegative(Profile ? Profile->CreateSpaceTriggerDistance : 300.0f, 300.0f);
	return Trigger > 0.0f && FVector::Dist2D(BoundPawn->GetActorLocation(), Target->GetActorLocation()) <= Trigger;
}

uint64 UDustEnemyCombatAdapterComponent::BeginCreateSpace() { return BeginCreateSpaceInternal(false); }
uint64 UDustEnemyCombatAdapterComponent::BeginCreateSpaceForReload() { return BeginCreateSpaceInternal(true); }

uint64 UDustEnemyCombatAdapterComponent::BeginCreateSpaceInternal(bool bDuringReload)
{
	if (!(bDuringReload ? CanCreateSpaceForReload() : CanCreateSpace())) return 0;

	FVector Destination;
	const bool bRequestedCombatSpread = !bDuringReload && ShouldCombatSpread();
	const bool bCombatSpread = bRequestedCombatSpread && FindCombatSpreadDestination(Destination);
	const UDustEnemyCombatProfile* StartProfile = GetProfile(WeaponComponent.Get());
	const float Trigger = SafeNonNegative(StartProfile ? StartProfile->CreateSpaceTriggerDistance : 300.0f, 300.0f);
	const bool bPlayerClose = Target.IsValid() && BoundPawn.IsValid()
		&& FVector::Dist2D(Target->GetActorLocation(), BoundPawn->GetActorLocation()) <= Trigger;
	if (!bCombatSpread && (!(bDuringReload || bPlayerClose) || !FindCreateSpaceDestination(Destination)))
	{
		LastDiagnostic = TEXT("Create Space found no safe reachable point; cooldown then resume in place");
		ApplySpacingCooldown(bRequestedCombatSpread);
		return 0;
	}
	const TWeakObjectPtr<AActor> Known = Target;
	const uint64 KnowledgeSerial = KnowledgeGeneration;
	auto StillOwnsStart = [this, Known, KnowledgeSerial, bDuringReload]()
	{
		return Target == Known && KnowledgeGeneration == KnowledgeSerial
			&& (bDuringReload ? CanCreateSpaceForReload() : CanCreateSpace());
	};
	if (!StillOwnsStart()) { ApplySpacingCooldown(bCombatSpread); return 0; }
	// Leave combat/approach movement only after a candidate exists. The StateTree has already
	// transitioned out of combat; this is not an Adapter Tick decision.
	if (WeaponComponent.IsValid()) WeaponComponent->StopEngagement();
	if (!StillOwnsStart()) { ApplySpacingCooldown(bCombatSpread); return 0; }
	Controller->StopMovement();
	if (!StillOwnsStart()) { ApplySpacingCooldown(bCombatSpread); return 0; }
	Controller->ClearFocus(EAIFocusPriority::Gameplay);
	if (!IsLifeCurrent() || !PathFollowing.IsValid() || PathFollowing->GetStatus() != EPathFollowingStatus::Idle)
	{
		LastDiagnostic = TEXT("Create Space could not take movement ownership");
		ApplySpacingCooldown(bCombatSpread);
		return 0;
	}
	// Temporary, restorable movement ownership for this one move only.
	if (!ApplySpacingMovementSpeed(bCombatSpread))
	{
		ApplySpacingCooldown(bCombatSpread);
		return 0;
	}
	ActiveSpacingToken = ++SpacingGeneration;
	const uint64 Token = ActiveSpacingToken;
	bSpacingIsCombatSpread = bCombatSpread;
	SpacingTarget = Known;
	SpacingDestination = Destination;
	SpacingElapsedSeconds = 0.0f;
	SpacingStatus = EDustCreateSpaceStatus::Running;
	bCreateSpaceActive = true;
	bIssuingSpacingMove = false;
	SpacingMoveId = FAIRequestID::InvalidRequest;
	ImmediateSpacingMoveId = FAIRequestID::InvalidRequest;
	bImmediateSpacingSucceeded = false;
	FAIMoveRequest Request;
	Request.SetGoalLocation(SpacingDestination);
	const UDustEnemyCombatProfile* MoveProfile = GetProfile(WeaponComponent.Get());
	const float Acceptance = SafeNonNegative(MoveProfile ? MoveProfile->CreateSpaceAcceptanceRadius : 40.0f, 40.0f);
	Request.SetAcceptanceRadius(Acceptance);
	Request.SetUsePathfinding(true);
	Request.SetProjectGoalLocation(true);
	Request.SetAllowPartialPath(false);
	// Keep the body facing the player while retreating instead of turning toward the move direction.
	SpacingStrafeController = Controller;
	bSpacingSavedAllowStrafe = Controller->bAllowStrafe;
	bSpacingStrafeApplied = true;
	Request.SetCanStrafe(true);
	const bool PriorAllowStrafe = bSpacingSavedAllowStrafe;
	const TWeakObjectPtr<AAIController> MoveController = Controller;
	FPathFollowingRequestResult Move;
	{
		TGuardValue<bool> Guard(bIssuingSpacingMove, true);
		Move = Controller->MoveTo(Request);
	}
	// A synchronous cancel/life end may already have invalidated this token.
	if (Token != ActiveSpacingToken)
	{
		// MoveTo may publish its request/strafe flag after a synchronous Stop invalidated us.
		// Abort only that returned id, never a replacement request started by a callback.
		AAIController* OldController = MoveController.Get();
		UPathFollowingComponent* Paths = IsValid(OldController) ? OldController->GetPathFollowingComponent() : nullptr;
		if (IsValid(Paths) && Move.MoveId.IsValid()
			&& Paths->GetCurrentRequestId().GetID() == Move.MoveId.GetID())
			Paths->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, Move.MoveId);
		if (IsValid(OldController) && ActiveSpacingToken == 0
			&& (!IsValid(Paths) || !Paths->GetCurrentRequestId().IsValid()))
			OldController->bAllowStrafe = PriorAllowStrafe;
		return 0;
	}
	if (Move.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		SpacingMoveId = Move.MoveId;
		if (ImmediateSpacingMoveId.IsValid() && ImmediateSpacingMoveId.GetID() == SpacingMoveId.GetID())
			ResolveSpacingMove(bImmediateSpacingSucceeded);
	}
	else ResolveSpacingMove(Move.Code == EPathFollowingRequestResult::AlreadyAtGoal);
	if (!bCreateSpaceActive || SpacingStatus == EDustCreateSpaceStatus::Failed) return 0;
	// The same spacing request now owns the weapon's Start/Tick: one move plus shooting at the
	// currently confirmed player. No extra Engagement task is layered on top.
	if (!bDuringReload && WeaponComponent.IsValid())
	{
		AActor* SpacingActor = SpacingTarget.Get();
		if (!IsValid(SpacingActor))
		{
			ReleaseSpacing(true, true, false);
			return 0;
		}
		const float AimOffset = MoveProfile && FMath::IsFinite(MoveProfile->AimHeightOffset)
			? MoveProfile->AimHeightOffset : 50.0f;
		if (!WeaponComponent->StartEngagement(SpacingActor))
		{
			LastDiagnostic = TEXT("Create Space could not start its moving engagement; move cancelled");
			ReleaseSpacing(true, true, false);
			return 0;
		}
		// Set after StartEngagement: a fresh start clears focus, and this spacing must keep the
		// body/controller facing the confirmed player while backing away.
		Controller->SetFocalPoint(SpacingActor->GetActorLocation() + FVector(0.0f, 0.0f, AimOffset),
			EAIFocusPriority::Gameplay);
	}
	if (bDuringReload && IsConfirmedTarget(SpacingTarget.Get()))
		Controller->SetFocalPoint(DustEnemyVisibility::GetAimPoint(SpacingTarget.Get(), MoveProfile), EAIFocusPriority::Gameplay);
	LastDiagnostic = bDuringReload ? TEXT("Reload continues while retreating from nearby player")
		: (bCombatSpread ? TEXT("Combat Spread sidesteps from nearby friends while firing")
			: TEXT("Create Space owns one short move and fires while retreating"));
	return Token;
}

EDustCreateSpaceStatus UDustEnemyCombatAdapterComponent::UpdateCreateSpace(uint64 Token, float DeltaTime)
{
	if (Token == 0 || Token != ActiveSpacingToken || !bCreateSpaceActive)
		return EDustCreateSpaceStatus::Failed;
	const float SafeDelta = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0f, DeltaTime) : 0.0f;
	if (!IsLifeCurrent() || bInterrupting || !HasConfirmedTarget() || Target.Get() != SpacingTarget.Get())
	{
		LastDiagnostic = TEXT("Create Space lost target confirmation; cancelled");
		ReleaseSpacing(true, true, false);
		return EDustCreateSpaceStatus::Failed;
	}
	if (SpacingStatus != EDustCreateSpaceStatus::Running)
	{
		const EDustCreateSpaceStatus Result = SpacingStatus;
		if (Result == EDustCreateSpaceStatus::Failed) ReleaseSpacing(false, true, false);
		return Result;
	}
	SpacingElapsedSeconds += SafeDelta;
	const UDustEnemyCombatProfile* Profile = GetProfile(WeaponComponent.Get());
	const float Timeout = FMath::Max(0.1f,
		SafePositive(Profile ? Profile->CreateSpaceTimeoutSeconds : 3.0f, 3.0f));
	if (SpacingElapsedSeconds >= Timeout)
	{
		LastDiagnostic = TEXT("Create Space timed out; cooldown then resume in place");
		ReleaseSpacing(true, true, false);
		return EDustCreateSpaceStatus::Failed;
	}
	// The StateTree task starts and owns the reload before updating this move. Only an
	// actual reload permits an empty/non-engaging weapon to retain its movement request.
	const AWeaponRuntime* Runtime = WeaponComponent.IsValid() ? WeaponComponent->GetWeaponRuntime() : nullptr;
	if (!IsValid(Runtime) || (!IsReloadBusy()
		&& (Runtime->GetCurrentAmmo() <= 0 || !WeaponComponent->IsEngaging())))
	{
		LastDiagnostic = TEXT("Create Space has neither shooting nor an active reload; released");
		ReleaseSpacing(true, true, false);
		return EDustCreateSpaceStatus::Failed;
	}
	if (IsReloadBusy())
	{
		const float AimOffset = Profile && FMath::IsFinite(Profile->AimHeightOffset) ? Profile->AimHeightOffset : 50.0f;
		Controller->SetFocalPoint(Target->GetActorLocation() + FVector(0.0f, 0.0f, AimOffset), EAIFocusPriority::Gameplay);
	}

	const APawn* Pawn = BoundPawn.Get();
	const float Acceptance = SafeNonNegative(Profile ? Profile->CreateSpaceAcceptanceRadius : 40.0f, 40.0f);
	if (IsValid(Pawn) && FVector::Dist2D(Pawn->GetActorLocation(), SpacingDestination) <= Acceptance)
	{
		ResolveSpacingMove(true);
		return EDustCreateSpaceStatus::Succeeded;
	}
	if (!PathFollowing.IsValid() || !SpacingMoveId.IsValid()
		|| PathFollowing->GetCurrentRequestId().GetID() != SpacingMoveId.GetID())
	{
		LastDiagnostic = TEXT("Create Space lost its path-following request; cooldown then resume in place");
		ReleaseSpacing(false, true, false);
		return EDustCreateSpaceStatus::Failed;
	}
	return EDustCreateSpaceStatus::Running;
}

void UDustEnemyCombatAdapterComponent::Unbind()
{
	RestoreLocalAvoidance();
	if (Perception.IsValid()) Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandlePerception);
	if (Controller.IsValid()) Controller->OnPossessedPawnChanged.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandlePawnChanged);
	if (BoundPawn.IsValid()) BoundPawn->OnDestroyed.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandlePawnDestroyed);
	if (Target.IsValid()) Target->OnDestroyed.RemoveDynamic(this, &UDustEnemyCombatAdapterComponent::HandleTargetDestroyed);
	if (PathFollowing.IsValid()) PathFollowing->OnRequestFinished.Remove(MoveFinishedHandle);
	MoveFinishedHandle.Reset();
}

void UDustEnemyCombatAdapterComponent::StopCombatAI(EDustEnemyAIStopReason Reason)
{
	if (bStopped) return;
	// A stale deferred reload sight loss must not survive this Pawn life.
	bDeferredReloadSightLoss = false;
	// Latch before StopLogic/StopMovement: both can invoke synchronous callbacks.
	bStopped = true;
	bInitialized = false;
	NextHearingAllowedAt = 0.0f;
	HearingListenStartTime = 0.0f;
	LastStopReason = Reason;
	AAIController* OwnerController = Controller.IsValid() ? Controller.Get() : Cast<AAIController>(GetOwner());
	APawn* PawnBeforeStop = BoundPawn.Get();
	if (!PawnBeforeStop && OwnerController) PawnBeforeStop = OwnerController->GetPawn();
	StoppedPawn = PawnBeforeStop;
	if (ActiveSpacingToken != 0) CancelCreateSpace(ActiveSpacingToken);
	ClearKnowledgeState();
	++KnowledgeGeneration;
	ClearSearch();
	Unbind();
	Target.Reset();
	SetComponentTickEnabled(false);
	TGuardValue<bool> Guard(bInterrupting, true);
	UStateTreeAIComponent* StateTree = Tree.IsValid() ? Tree.Get() : (OwnerController ? OwnerController->FindComponentByClass<UStateTreeAIComponent>() : nullptr);
	UDustEnemyWeaponComponent* Weapon = WeaponComponent.Get();
	if (!Weapon && StoppedPawn.IsValid()) Weapon = StoppedPawn->FindComponentByClass<UDustEnemyWeaponComponent>();
	if (StateTree) StateTree->StopLogic(TEXT("Dust enemy life stopped"));
	// After the tree is down, make sure an unhandled reload task cannot keep the runtime busy.
	if (APawn* ReloadPawn = StoppedPawn.IsValid() ? StoppedPawn.Get() : BoundPawn.Get())
		if (UDustEnemyReloadComponent* Reload = ReloadPawn->FindComponentByClass<UDustEnemyReloadComponent>())
			Reload->StopAllOperations();
	if (Weapon) Weapon->SetCombatEnabled(false);
	if (OwnerController) { OwnerController->StopMovement(); OwnerController->ClearFocus(EAIFocusPriority::Gameplay); }
	LastDiagnostic = TEXT("Stopped; target, grace, spacing, search timers, movement, fire and bindings cleared");
}

void UDustEnemyCombatAdapterComponent::HandlePawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (IsInitialized() && NewPawn != BoundPawn.Get()) StopCombatAI(EDustEnemyAIStopReason::LostControl);
}

void UDustEnemyCombatAdapterComponent::HandlePawnDestroyed(AActor* Actor)
{
	StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
}

void UDustEnemyCombatAdapterComponent::HandleTargetDestroyed(AActor* Actor)
{
	if (Target.Get() == Actor) Lose(Actor, LastKnownLocation, true);
	else if (SearchTarget.Get() == Actor && IsLifeCurrent())
	{
		ClearQuickReacquire();
		if (InterruptActivity()) ResumeIdleLogic();
	}
}

void UDustEnemyCombatAdapterComponent::EnsureKnownTargetTreeRunning()
{
	if (!IsLifeCurrent() || bInterrupting || bSearching || !HasKnownTarget()
		|| !bHasStartedTree || Tree->IsRunning() || Tree->IsPaused()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextTreeRecoveryAt) return;
	NextTreeRecoveryAt = Now + 1.0f;
	const uint64 Serial = Generation;
	Tree->StartLogic();
	if (IsLifeCurrent() && Generation == Serial)
	{
		LastDiagnostic = Tree->IsRunning() ? TEXT("Known-target StateTree recovered without a new sight event")
			: TEXT("Known target but StateTree cannot start; check state conditions and asset");
		UE_LOG(LogDustEnemyLifecycle, Log, TEXT("[W09-RECOVERY] controller=%s target=%s running=%d"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Target.Get()), Tree->IsRunning());
	}
}

void UDustEnemyCombatAdapterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsInitialized()) return;
	if (IsMissionOver()) { StopCombatAI(EDustEnemyAIStopReason::MissionEnd); return; }
	if (DustActorState::IsDead(BoundPawn.Get())) { StopCombatAI(EDustEnemyAIStopReason::EnemyDeath); return; }
	if (!IsLifeCurrent()) { StopCombatAI(EDustEnemyAIStopReason::LostControl); return; }
	// Destruction can invalidate the weak known-target pointer before/without a usable loss event.
	if (!Target.IsValid() && (bConfirmedTarget || bInLossGrace || bDeferredReloadSightLoss))
	{
		if (bInLossGrace)
		{
			Lose(Target.Get(), LastKnownLocation, true);
			return;
		}
		bDeferredReloadSightLoss = false;
		ClearKnowledgeState();
		++KnowledgeGeneration;
		if (InterruptActivity()) ResumeIdleLogic();
		return;
	}
	if (Target.IsValid() && !IsEligibleTarget(Target.Get()))
	{
		Lose(Target.Get(), LastKnownLocation, true);
		return;
	}
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    if (bHearingSearch && IsSearchCurrent(Generation) && Now >= HearingDeadline)
    {
        LastDiagnostic = TEXT("Hearing: investigation deadline reached");
        FinishSearch(Generation);
        return;
    }
	AActor* Known = Target.Get();
	if (IsValid(Known))
	{
		if (CanConfirmKnownTarget())
		{
			const bool bWasLost = !bConfirmedTarget;
			const bool bQuick = QuickReacquireTarget.Get() == Known && Now <= QuickReacquireExpiry;
			ConfirmKnownTarget(Known, Known->GetActorLocation());
			if (bWasLost) bQuickReacquireAvailable = bQuick;
		}
		else if (bConfirmedTarget) BeginLossGrace(Known, LastKnownLocation);
	}
	if (!IsLifeCurrent()) return;

	if (bDeferredReloadSightLoss)
	{
		AActor* KnownTarget = Target.Get();
		if (!IsValid(KnownTarget) || !IsEligibleTarget(KnownTarget))
		{
			// Target death/destruction is never deferred.
			bDeferredReloadSightLoss = false;
			if (IsValid(KnownTarget)) Lose(KnownTarget, LastKnownLocation, true);
			return;
		}
		if (!IsReloadBusy())
		{
			// Reload/move finished (or was cancelled): now run the original last-known-location search.
			const FVector LossLocation = DeferredReloadLossLocation;
			bDeferredReloadSightLoss = false;
			Lose(KnownTarget, LossLocation, false);
			return;
		}
	}

	if (bInLossGrace)
	{
		AActor* KnownTarget = Target.Get();
		if (!IsValid(KnownTarget) || !IsEligibleTarget(KnownTarget))
		{
			Lose(KnownTarget, LastKnownLocation, true);
			return;
		}
		const UDustEnemyCombatProfile* GraceProfile = GetProfile(WeaponComponent.Get());
		const float Grace = SafeNonNegative(GraceProfile ? GraceProfile->LostSightGraceSeconds : 0.8f, 0.8f);
		if (Now - GraceStartTime >= Grace)
		{
			Lose(KnownTarget, LastKnownLocation, false);
			return;
		}
	}

	EnsureKnownTargetTreeRunning();
	if (bSearching && !IsEligibleTarget(SearchTarget.Get()))
	{
		ClearQuickReacquire();
		if (InterruptActivity()) ResumeIdleLogic();
	}
}

void UDustEnemyCombatAdapterComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// Last-resort restore even if a non-standard teardown skipped StopCombatAI.
	RestoreSpacingMovementSpeed();
	if (bInitialized || bStopped) StopCombatAI(EDustEnemyAIStopReason::EndPlay);
	Super::EndPlay(Reason);
}
