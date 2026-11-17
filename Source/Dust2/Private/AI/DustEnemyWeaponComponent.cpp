#include "AI/DustEnemyWeaponComponent.h"
#include "UI/DustCombatFeedbackComponent.h"
#include "Combat/DustShotDamageReceiptLibrary.h"
#include "DustEnemyVisibility.h"
#include "../Combat/DustActorState.h"
#include "Combat/DustRagdollDeathComponent.h"
#include "AI/DustEnemyCombatProfile.h"
#include "AI/DustEnemyAimMath.h"
#include "AI/DustEnemyCombatAdapterComponent.h"
#include "AI/DustEnemyReloadComponent.h"
#include "Mission/DustGameSessionSubsystem.h"
#include "Mission/DustEncounterComponent.h"
#include "Weapons/WeaponRuntime.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Presentation/WeaponPresentationData.h"
#include "../Weapons/WeaponBallisticsData.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "../Presentation/DustFireAudioPolicy.h"

namespace
{

	UDustEnemyCombatAdapterComponent* FindEnemyAdapter(const AActor* Owner)
	{
		if (!IsValid(Owner)) return nullptr;
		if (const APawn* Pawn = Cast<APawn>(Owner))
			if (AAIController* Controller = Cast<AAIController>(Pawn->GetController()))
				if (UDustEnemyCombatAdapterComponent* Adapter = Controller->FindComponentByClass<UDustEnemyCombatAdapterComponent>())
					return Adapter;
		return Owner->FindComponentByClass<UDustEnemyCombatAdapterComponent>();
	}

	float SafeAngle(float Value, float DefaultValue)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 180.0f) : DefaultValue;
	}

	// Small grace above the task's configured walk speed. Beyond this the moving-fire
	// permission is denied outright instead of falling back to the stationary settle gate.
	constexpr float MovingFireSpeedTolerance = 10.0f;
}

UDustEnemyWeaponComponent::UDustEnemyWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = true;
}

void UDustEnemyWeaponComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if (const FDustDeferredEnemySpawnEntry* Entry = UDustEncounterComponent::FindInitializingEnemyEntry(GetOwner());
		Entry && IsValid(Entry->WeaponDefinition.Get()))
	{
		WeaponDefinition = Entry->WeaponDefinition;
	}
}

bool UDustEnemyWeaponComponent::InitializeWeapon()
{
	// Must run before the initialized-runtime early-out: an active session always wins over the
	// component's instance default, including when the runtime itself is already valid.
	ApplySelectedSessionDifficulty();
	if (IsValid(WeaponRuntime)) return WeaponRuntime->WeaponDefinition == WeaponDefinition;
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !GetWorld() || !IsValid(WeaponDefinition) || !IsValid(WeaponDefinition->BallisticsData)) return false;
	// Bare native runtime deliberately avoids first-person Blueprint construction/events.
	WeaponRuntime = GetWorld()->SpawnActorDeferred<AWeaponRuntime>(AWeaponRuntime::StaticClass(),
		Pawn->GetActorTransform(), Pawn, Pawn, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!WeaponRuntime) return false;
	WeaponRuntime->WeaponDefinition = WeaponDefinition;
	WeaponRuntime->FinishSpawning(Pawn->GetActorTransform());
	return IsValid(WeaponRuntime);
}

bool UDustEnemyWeaponComponent::ApplySelectedSessionDifficulty()
{
	const UWorld* World = GetWorld();
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	UDustGameSessionSubsystem* Session = Instance
		? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
	if (!Session || !Session->IsSessionActive()) return false;

	UDustEnemyCombatProfile* Profile = Session->GetSelectedCombatProfile();
	if (!IsValid(Profile)) return false;
	return ApplySessionCombatProfile(Profile);
}

bool UDustEnemyWeaponComponent::ApplySessionCombatProfile(UDustEnemyCombatProfile* Profile)
{
	if (!IsValid(Profile)) return false;
	// Repeated initialization with the selected profile must not interrupt live fire.
	if (CombatProfile == Profile) return true;
	if (bEngaging) StopEngagement();
	CombatProfile = Profile;
	// Pre-placed AI may already have captured the instance-default avoidance settings.
	if (UDustEnemyCombatAdapterComponent* Adapter = FindEnemyAdapter(GetOwner()))
		Adapter->RefreshCombatProfileSettings();
	return true;
}

bool UDustEnemyWeaponComponent::ResolveEnemyPresentationProfile(FWeaponPresentationProfile& OutProfile) const
{
	return UWeaponPresentationData::ResolveEnemyProfileForDefinition(WeaponDefinition.Get(), OutProfile)
		== EWeaponPresentationResolveResult::Resolved;
}

bool UDustEnemyWeaponComponent::ApplyReloadPresentationFromProfile()
{
	UDustEnemyReloadComponent* Reload = IsValid(GetOwner())
		? GetOwner()->FindComponentByClass<UDustEnemyReloadComponent>() : nullptr;
	if (!IsValid(Reload) || Reload->IsBusy() || bEngaging) return false;
	if (IsValid(WeaponRuntime) && (WeaponRuntime->IsReloading() || WeaponRuntime->WeaponDefinition != WeaponDefinition)) return false;
	FWeaponPresentationProfile Profile;
	if (!ResolveEnemyPresentationProfile(Profile)) return false;
	// Initialization only: while idle, copy the two fixed empty-reload actions and the optional
	// sound. A resolved profile with no sound clears the component's legacy fallback; a disabled
	// profile returns before this point and leaves the legacy component configuration untouched.
	// This does not select by ammo count or start a reload transaction.
	Reload->CharacterReloadMontage = Profile.CharacterReloadEmptyMontage;
	Reload->WeaponReloadMontage = Profile.ReloadEmptyMontage;
	Reload->ReloadEmptySound = Profile.ReloadEmptySound;
	return true;
}

void UDustEnemyWeaponComponent::SetMuzzleSource(USceneComponent* Component, FName SocketName)
{
	// Only the owning enemy's component is accepted; prevents accidentally tracing from player 0.
	MuzzleSource = IsValid(Component) && Component->GetOwner() == GetOwner() ? Component : nullptr;
	MuzzleSocket = SocketName;
}

uint64 UDustEnemyWeaponComponent::BeginMovingFire(AActor* Target, float WalkSpeed)
{
	// The token is permission, not engagement: only the already-started engagement for the same
	// target may skip the stationary settle gate. A new token always supersedes the old lease.
	if (!IsValid(Target) || !bEngaging || EngagementTarget.Get() != Target) return 0;
	if (!FMath::IsFinite(WalkSpeed) || WalkSpeed <= 0.0f) return 0;

	++NextMovingFireToken;
	if (NextMovingFireToken == 0) ++NextMovingFireToken;
	MovingFireToken = NextMovingFireToken;
	MovingFireTarget = Target;
	MovingFireWalkSpeed = WalkSpeed;
	return MovingFireToken;
}

void UDustEnemyWeaponComponent::EndMovingFire(uint64 Token)
{
	// Only the identical lease may be cleared. A stale task Exit holding an older token must
	// never stop the newer same-target engagement/permission.
	if (Token == 0 || MovingFireToken != Token) return;
	StopEngagement();
}

bool UDustEnemyWeaponComponent::IsMovingFireLeaseActive(uint64 Token, const AActor* Target) const
{
	return Token != 0 && MovingFireToken == Token && IsValid(Target) && MovingFireTarget.Get() == Target;
}

void UDustEnemyWeaponComponent::ClearMovingFireLease()
{
	// Deliberately does not reset NextMovingFireToken: lease identity stays monotonic.
	MovingFireToken = 0;
	MovingFireTarget.Reset();
	MovingFireWalkSpeed = 0.0f;
}

bool UDustEnemyWeaponComponent::HasMovingFireLeaseForCurrentEngagement() const
{
	return bEngaging && MovingFireToken != 0 && IsValid(MovingFireTarget.Get())
		&& MovingFireTarget.Get() == EngagementTarget.Get();
}

bool UDustEnemyWeaponComponent::CanUseMovingFire(const UDustEnemyCombatAdapterComponent* Adapter,
	DustEnemyVisibility::FSightQuery* Query) const
{
	if (!HasMovingFireLeaseForCurrentEngagement()) return false;
	// With an adapter this is the same confirmed-target authority used by CanContinue. The
	// legacy native-pawn path has no adapter and keeps its original orientation behavior.
	if (Adapter && (!Adapter->IsInitialized() || !Adapter->IsConfirmedTarget(EngagementTarget.Get(), Query)))
		return false;

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return false;
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement || Movement->IsFalling()) return false;

	const float HorizontalSpeed = GetOwner()->GetVelocity().Size2D();
	const float MaxAllowedSpeed = FMath::Max(0.0f, MovingFireWalkSpeed) + MovingFireSpeedTolerance;
	return FMath::IsFinite(HorizontalSpeed) && HorizontalSpeed <= MaxAllowedSpeed;
}

bool UDustEnemyWeaponComponent::StartEngagement(AActor* Target)
{
	UDustEnemyCombatAdapterComponent* Adapter = FindEnemyAdapter(GetOwner());
	// While a reload-driven sight loss is deferred, do not resume fire before the adapter searches.
	if (Adapter && Adapter->HasDeferredReloadSightLoss()) { StopEngagement(); return false; }
	// A move-to-reload point or an in-progress reload owns the Pawn; shooting must not resume.
	if (const UDustEnemyReloadComponent* Reload = GetOwner() ? GetOwner()->FindComponentByClass<UDustEnemyReloadComponent>() : nullptr)
		if (Reload->IsBusy()) { StopEngagement(); return false; }
	if (IsValid(WeaponRuntime) && WeaponRuntime->IsReloading()) { StopEngagement(); return false; }
	if (bEngaging && EngagementTarget.Get() == Target)
	{
		// Repeated same-target Start must not reset the reaction, but it also cannot keep a
		// remembered/spacing-blocked engagement alive. Only this spacing's own target is allowed.
		if (Adapter && (Adapter->HasDeferredReloadSightLoss() || !Adapter->IsConfirmedTarget(Target)
			|| (Adapter->IsCreateSpaceActive() && !Adapter->IsSpacingTarget(Target))))
		{
			StopEngagement();
			return false;
		}
		return true;
	}
	StopEngagement();
	if (!bCombatEnabled || !DustActorState::IsAlive(GetOwner()) || !DustActorState::IsAlive(Target) || Target == GetOwner()
		|| !IsValid(CombatProfile) || !MuzzleSource.IsValid() || !InitializeWeapon() || WeaponRuntime->GetCurrentAmmo() <= 0) return false;
	// Adapter-backed AI may only start shooting at the currently confirmed target, never a
	// remembered one. A live Create Space request may start the same confirmed target it owns.
	if (Adapter && (!Adapter->IsConfirmedTarget(Target)
		|| (Adapter->IsCreateSpaceActive() && !Adapter->IsSpacingTarget(Target)))) { StopEngagement(); return false; }
	EngagementTarget = Target;
	FVector Eye; GetOwner()->GetActorEyesViewPoint(Eye, TrackingRotation);
	const float Now = GetWorld()->GetTimeSeconds();
	float ReactionSeconds = FMath::IsFinite(CombatProfile->ReactionDelay)
		? FMath::Max(0.0f, CombatProfile->ReactionDelay) : 0.5f;
	if (Adapter && Adapter->ConsumeQuickReacquire(Target))
	{
		const float QuickReaction = FMath::IsFinite(CombatProfile->QuickReacquireReactionSeconds)
			? FMath::Max(0.0f, CombatProfile->QuickReacquireReactionSeconds) : 0.1f;
		ReactionSeconds = FMath::Min(ReactionSeconds, QuickReaction);
	}
	ReadyAt = Now + ReactionSeconds;
	NextAttemptAt = ReadyAt;
	SettledTime = 0.0f;
	ShotsInVolley = 0;
	CurrentVolleyShotTarget = 0;
	VolleyFireMode = EFireMode::SemiAutomatic;
	bEngaging = true;
	UpdateTickActivity();
	return true;
}

void UDustEnemyWeaponComponent::StopEngagement()
{
	// Any authoritative engagement stop invalidates moving-fire permission before callbacks.
	ClearMovingFireLease();
	if (bEngaging)
		if (const APawn* Pawn = Cast<APawn>(GetOwner()))
			if (AAIController* Controller = Cast<AAIController>(Pawn->GetController())) Controller->ClearFocus(EAIFocusPriority::Gameplay);
	bEngaging = false;
	EngagementTarget.Reset();
	SettledTime = 0.0f;
	ShotsInVolley = 0;
	CurrentVolleyShotTarget = 0;
	VolleyFireMode = EFireMode::SemiAutomatic;
	// Keep physical recovery/compensation across short pauses; never refill ammo or reset the sequence.
	UpdateTickActivity();
}

void UDustEnemyWeaponComponent::SetCombatEnabled(bool bEnabled)
{
	bCombatEnabled = bEnabled;
	UpdateTickActivity();
	if (!bEnabled)
	{
		StopEngagement();
		// Disabling this enemy's combat is a hard stop: cancel its own reload/move transaction.
		if (UDustEnemyReloadComponent* Reload = GetOwner() ? GetOwner()->FindComponentByClass<UDustEnemyReloadComponent>() : nullptr)
			Reload->StopAllOperations();
	}
}

void UDustEnemyWeaponComponent::UpdateTickActivity()
{
 const bool bHasRecoil = IsValid(WeaponRuntime)
  && !WeaponRuntime->GetCurrentIndependentShotRecoil().IsNearlyZero();
 const bool bNeedsTick = bCombatEnabled && (bEngaging || bHasRecoil || !Compensation.IsNearlyZero());
 if (!bNeedsTick) Observations.Reset();
 SetComponentTickEnabled(bNeedsTick);
}

bool UDustEnemyWeaponComponent::CanContinue(const UDustEnemyCombatAdapterComponent* Adapter, DustEnemyVisibility::FSightQuery* Query) const
{
	// The current project stores terminal result on its Blueprint/native game mode.
	if (DustActorState::IsMissionOver(GetWorld())) return false;
	// The reload component owns the Pawn while moving/reloading; the runtime owns the ammo transaction.
	if (const UDustEnemyReloadComponent* Reload = GetOwner() ? GetOwner()->FindComponentByClass<UDustEnemyReloadComponent>() : nullptr)
		if (Reload->IsBusy()) return false;
	if (Adapter)
	{
		// With an adapter the weapon may only consume the confirmed target; remembered states
		// never keep a stale engagement alive. A Create Space request may fire only at the
		// exact actor it temporarily owns.
		if (Adapter->HasDeferredReloadSightLoss() || !Adapter->IsConfirmedTarget(EngagementTarget.Get(), Query)
			|| (Adapter->IsCreateSpaceActive() && !Adapter->IsSpacingTarget(EngagementTarget.Get()))) return false;
	}
	if (IsValid(WeaponRuntime) && WeaponRuntime->IsReloading()) return false;
	return bCombatEnabled && DustActorState::IsAlive(GetOwner()) && DustActorState::IsAlive(EngagementTarget.Get())
		&& IsValid(WeaponRuntime) && IsValid(CombatProfile) && WeaponRuntime->WeaponDefinition == WeaponDefinition
		&& IsValid(WeaponDefinition) && IsValid(WeaponDefinition->BallisticsData) && MuzzleSource.IsValid();
}

bool UDustEnemyWeaponComponent::GetShotGeometry(FVector& OutMuzzle, FVector& OutAimPoint, bool bContinuationChecked, DustEnemyVisibility::FSightQuery* Query) const
{
	if (!bContinuationChecked && !CanContinue(FindEnemyAdapter(GetOwner()), Query)) return false;
	const USceneComponent* Source = MuzzleSource.Get();
	if (!MuzzleSocket.IsNone() && !Source->DoesSocketExist(MuzzleSocket)) return false;
	// The physical muzzle remains the presentation anchor; only the logical origin below may
	// fall back to the eye for the restricted point-blank case.
	const FVector PhysicalMuzzle = Source->GetSocketLocation(MuzzleSocket);
	OutAimPoint = DustEnemyVisibility::GetAimPoint(EngagementTarget.Get(), CombatProfile);
	if (PhysicalMuzzle.ContainsNaN() || OutAimPoint.ContainsNaN()
		|| !DustEnemyVisibility::IsInRange(GetOwner(), EngagementTarget.Get(), CombatProfile, WeaponDefinition)) return false;
	FVector Eye; FRotator EyeRotation; GetOwner()->GetActorEyesViewPoint(Eye, EyeRotation);
	if (Eye.ContainsNaN()) return false;
	// A confirmed target pressed against the muzzle can sit on the Eye->Muzzle segment. Only that
	// exact target is allowed to be hit there; a wall or any other non-friendly still blocks the
	// segment instead of the old blanket rejection.
	if (!DustEnemyVisibility::HasSight(GetOwner(), EngagementTarget.Get(), Eye, PhysicalMuzzle)) return false;
	// Knowledge LOS to the raised aim point is unchanged and still shared with the adapter.
	if (!DustEnemyVisibility::HasSight(GetOwner(), EngagementTarget.Get(), Eye, OutAimPoint, nullptr, Query)) return false;

	const FVector EyeToAim = OutAimPoint - Eye;
	// Restricted point-blank correction: when the aim point lies before the physical muzzle along
	// Eye->Aim (the muzzle has already passed the target), Muzzle->Aim would point backwards and
	// break tracking/body/focus agreement. Only that case moves the logical shot origin to the eye.
	// This is not a general close-range ignore: Eye->Aim still needs the shared sight check above,
	// Eye->Muzzle still has to be reachable, the visible muzzle and the real trace are unchanged,
	// and normal mid/long range keeps firing from the actual muzzle.
	const bool bAimPointBeforeMuzzle = !EyeToAim.IsNearlyZero()
		&& FVector::DotProduct(OutAimPoint - PhysicalMuzzle, EyeToAim) <= 0.0;
	OutMuzzle = bAimPointBeforeMuzzle ? Eye : PhysicalMuzzle;
	return !(OutAimPoint - OutMuzzle).IsNearlyZero();
}

bool UDustEnemyWeaponComponent::IsAimAligned(const FVector& Muzzle, const FVector& AimPoint, const UDustEnemyCombatAdapterComponent* Adapter) const
{
	// Native APawn fixtures without an adapter keep the original orientation behavior.
	if (!Adapter) return true;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn)) return false;
	const FRotator Desired = (AimPoint - Muzzle).Rotation();
	const float BodyDesiredYaw = (AimPoint - Pawn->GetActorLocation()).Rotation().Yaw;
	const float Threshold = SafeAngle(CombatProfile ? CombatProfile->FireAlignmentAngleDegrees : 15.0f, 15.0f);
	const float TrackingYawError = FMath::Abs(FMath::FindDeltaAngleDegrees(TrackingRotation.Yaw, Desired.Yaw));
	const float TrackingPitchError = FMath::Abs(FMath::FindDeltaAngleDegrees(TrackingRotation.Pitch, Desired.Pitch));
	const float BodyYawError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		Pawn->GetActorForwardVector().Rotation().Yaw, BodyDesiredYaw));
	return TrackingYawError <= Threshold && TrackingPitchError <= Threshold && BodyYawError <= Threshold;
}

void UDustEnemyWeaponComponent::SetCombatDiagnostic(const TCHAR* Message)
{
	if (LastCombatDiagnostic == Message) return;
	LastCombatDiagnostic = Message;
	if (bDrawDebugShots) UE_LOG(LogTemp, Log, TEXT("[W09-COMBAT] %s %s"), *GetNameSafe(GetOwner()), Message);
}

void UDustEnemyWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bCombatEnabled) return;
	if (!GetWorld() || !IsValid(CombatProfile) || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector2D Recoil = IsValid(WeaponRuntime) ? WeaponRuntime->GetCurrentIndependentShotRecoil() : FVector2D::ZeroVector;
	FVector2D Observed = FVector2D::ZeroVector;
	if (!Recoil.IsNearlyZero())
	{
		Observations.Add({Now, Recoil});
		const float ObservationTime = Now - FMath::Clamp(CombatProfile->CompensationDelay, 0.0f, 0.5f);
		int32 ExpiredCount = 0;
		while (ExpiredCount + 1 < Observations.Num() && Observations[ExpiredCount + 1].Time <= ObservationTime)
		{
			++ExpiredCount;
		}
		// Keep one delayed sample and compact expired history once, including the size cap.
		ExpiredCount = FMath::Max(ExpiredCount, Observations.Num() - 256);
		if (ExpiredCount > 0) Observations.RemoveAt(0, ExpiredCount, EAllowShrinking::No);
		Observed = Observations.Num() && Observations[0].Time <= ObservationTime ? Observations[0].Offset : FVector2D::ZeroVector;
	}
	// On recovery, never keep compensating for recoil that no longer exists.
	else
	{
		Compensation = FVector2D::ZeroVector;
		Observations.Reset();
	}
	if (Now >= NextDriftAt)
	{
		DriftTarget = FVector2D(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f));
		NextDriftAt = Now + 0.35f;
	}
	Drift = FMath::Vector2DInterpTo(Drift, DriftTarget, DeltaTime, FMath::Max(0.0f, CombatProfile->DriftResponseSpeed));
	const float DriftScale = FMath::Min(1.0, Observed.Size()) * FMath::Max(0.0f, CombatProfile->CompensationDriftDegrees);
	Compensation = FEnemyAimMath::StepCompensation(Compensation, Observed, DeltaTime,
		CombatProfile->CompensationFraction, CombatProfile->CompensationSpeedDegPerSecond);
	if (!bEngaging) { UpdateTickActivity(); return; }
	const UDustEnemyCombatAdapterComponent* Adapter = FindEnemyAdapter(GetOwner());
	DustEnemyVisibility::FSightQuery SightQuery;
	if (!CanContinue(Adapter, &SightQuery) || WeaponRuntime->GetCurrentAmmo() <= 0) { SetCombatDiagnostic(TEXT("Stopped: confirmation/life/reload/empty")); StopEngagement(); return; }
	FVector Muzzle, AimPoint;
	if (!GetShotGeometry(Muzzle, AimPoint, true, &SightQuery))
	{
		// Keep this approach's reaction/lease while moving into range; geometry still denies fire.
		// Confirmation loss, reload and life invalidation were already rejected above.
		SetCombatDiagnostic(TEXT("Waiting: range/wall/socket geometry"));
		if (!HasMovingFireLeaseForCurrentEngagement()) StopEngagement();
		return;
	}
	TrackingRotation = FEnemyAimMath::StepTracking(TrackingRotation, (AimPoint - Muzzle).Rotation(), DeltaTime,
		CombatProfile->TrackingSpeedDegPerSecond);
	// A Create Space request owns this engagement and may fire while moving. Its focus keeps
	// pointing at the currently confirmed player so the body does not turn toward the retreat
	// direction and normal Sight keeps working beyond the old close-range exception. The native
	// moving-fire lease uses the same real aim point, but its own permission is stricter.
	const bool bSpacingShot = Adapter && Adapter->IsCreateSpaceActive()
		&& Adapter->IsSpacingTarget(EngagementTarget.Get());
	const bool bMovingFireLease = HasMovingFireLeaseForCurrentEngagement();
	const bool bMovingFireShot = bMovingFireLease && CanUseMovingFire(Adapter, &SightQuery);
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
		if (AAIController* Controller = Cast<AAIController>(Pawn->GetController()))
			Controller->SetFocalPoint(
				(bSpacingShot || bMovingFireLease) ? AimPoint : Muzzle + TrackingRotation.Vector() * 1000.0f,
				EAIFocusPriority::Gameplay);
	if (GetOwner()->GetVelocity().Size2D() <= FMath::Max(0.0f, CombatProfile->RequiredSettledSpeed)) SettledTime += DeltaTime;
	else SettledTime = 0.0f;
	// Normal Settle/Engage still require standing still; only the existing spacing request or a
	// valid moving-fire lease skips that gate. Reaction, fire interval, wall/aim checks and volley
	// pacing are unchanged.
	const float RequiredSettle = (bSpacingShot || bMovingFireShot)
		? 0.0f : FMath::Max(0.0f, CombatProfile->SettledAimSeconds);
	if (Now < ReadyAt) { SetCombatDiagnostic(TEXT("Waiting: reaction")); return; }
	// An active lease that is physically too fast or falling denies the shot outright. It must not
	// fall through to RequiredSettle=0, which is why this is separate from the spacing exception.
	if (!bSpacingShot && bMovingFireLease && !bMovingFireShot)
	{
		SetCombatDiagnostic(TEXT("Waiting: moving fire ground/speed gate"));
		return;
	}
	if (SettledTime < RequiredSettle) { SetCombatDiagnostic(TEXT("Waiting: stationary aim")); return; }
	if (Now < NextAttemptAt) { SetCombatDiagnostic(TEXT("Waiting: fire interval/volley pause")); return; }
	// Keep tracking/focus and settled time; an unaligned body or tracker simply cannot commit fire.
	// Never StopEngagement here: that would repeatedly restart the reaction delay.
	if (!IsAimAligned(Muzzle, AimPoint, Adapter)) { SetCombatDiagnostic(TEXT("Waiting: body/tracker alignment")); return; }
	// Friendly bodies do not veto firing. The actual shot trace below still resolves its first hit.
	// Drift affects control, never the weapon's random spread or the target's health directly.
	const FVector2D Saved = Compensation;
	Compensation += Drift * DriftScale;
	const int32 BeforeShots = SuccessfulShots;
	AttemptShot(Now, Muzzle, AimPoint);
	if (SuccessfulShots > BeforeShots) SetCombatDiagnostic(TEXT("Firing"));
	Compensation = Saved;
}

int32 UDustEnemyWeaponComponent::ResolveVolleyShotTarget(EFireMode Mode) const
{
	// Native weapon modes keep their own length; only full-auto uses the random 3-6 range.
	if (Mode == EFireMode::SemiAutomatic) return 1;
	if (Mode == EFireMode::Burst) return 3;
	const int32 MinShots = FMath::Max(1, CombatProfile ? CombatProfile->BurstShotCount : 1);
	const int32 MaxShots = FMath::Max(1, CombatProfile ? CombatProfile->MaxBurstShotCount : 1);
	// Reversed or non-positive author values are normalized instead of changing the mode.
	const int32 Lower = FMath::Min(MinShots, MaxShots);
	const int32 Upper = FMath::Max(MinShots, MaxShots);
	return FMath::RandRange(Lower, Upper);
}

void UDustEnemyWeaponComponent::AttemptShot(float Now, const FVector& Muzzle, const FVector& AimPoint)
{
	FRotator Aim = TrackingRotation;
	Aim.Pitch = FMath::Clamp(Aim.Pitch - Compensation.X, -89.0, 89.0);
	Aim.Yaw -= Compensation.Y;
	if (Aim.ContainsNaN() || Aim.Vector().IsNearlyZero()) { StopEngagement(); return; }
	const EWeaponFireAttemptResult Result = WeaponRuntime->TryCommitFireWithResult();
	if (Result != EWeaponFireAttemptResult::Committed)
	{
		if (Result != EWeaponFireAttemptResult::Cooldown) StopEngagement();
		NextAttemptAt = Now + 0.01f;
		return;
	}
	LastShotRecoil = WeaponRuntime->GetCommittedIndependentShotRecoil();
	bool bGenerated = false;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const bool bFalling = Character && Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsFalling();
	WeaponRuntime->GenerateShotDirection(Aim.Vector(), GetOwner()->GetVelocity().Size2D(), bFalling, false, false, LastShotDirection, bGenerated);
	if (!bGenerated) { StopEngagement(); return; }
	const UWeaponBallisticsData* Ballistics = WeaponDefinition->BallisticsData;
	const FVector End = Muzzle + LastShotDirection * FMath::Max(0.0f, Ballistics->TraceDistance);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShot), true, GetOwner());
	Params.AddIgnoredActor(WeaponRuntime);
	GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, End, ECC_Visibility, Params);
	++SuccessfulShots;
	const EFireMode Mode = WeaponRuntime->GetFireMode();
	// A live mode change must not carry the old volley's remaining shots into the new mode.
	if (Mode != VolleyFireMode)
	{
		VolleyFireMode = Mode;
		ShotsInVolley = 0;
		CurrentVolleyShotTarget = 0;
	}
	// Capture once at the first successful shot; later ticks or profile edits cannot resample it.
	if (CurrentVolleyShotTarget <= 0) CurrentVolleyShotTarget = ResolveVolleyShotTarget(Mode);
	++ShotsInVolley;
	float Interval = FMath::Max(0.001f, WeaponRuntime->GetEffectiveFireInterval());
	if (ShotsInVolley >= CurrentVolleyShotTarget)
	{
		ShotsInVolley = 0;
		CurrentVolleyShotTarget = 0;
		Interval = FMath::Max(Interval, FMath::Max(0.0f, CombatProfile->BurstPauseSeconds));
		if (Mode == EFireMode::Burst) Interval = FMath::Max(Interval, WeaponDefinition->BurstRecoveryInterval);
	}
	NextAttemptAt = Now + Interval;
	if (bDrawDebugShots) DrawDebugLine(GetWorld(), Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : End, Hit.bBlockingHit ? FColor::Red : FColor::Green, false, 1.0f);
	// Friendly bodies are not transparent and still own the real hit. Only the damage payload is
	// zeroed so the existing Blueprint bridge cannot hurt the same faction. This is not a
	// friendly-veto: ammo, shot count, presentation and the real Hit/End all still commit once.
	const bool bFriendlyHit = Hit.bBlockingHit
		&& DustEnemyVisibility::IsFriendlyBlocker(GetOwner(), Hit.GetActor());
	// Cache presentation-only impact before the synchronous damage bridge can kill the player.
	// Health, hit acceptance and death decisions remain with the existing receiver.
	if (!bFriendlyHit && Hit.bBlockingHit && IsValid(Hit.GetActor())
		&& FMath::IsFinite(Ballistics->DamagePerShot) && Ballistics->DamagePerShot > 0.0f)
		if (UDustRagdollDeathComponent* Death = Hit.GetActor()->FindComponentByClass<UDustRagdollDeathComponent>())
			Death->CacheDeathImpact(Hit, LastShotDirection);
	AActor* DamageTarget = Hit.bBlockingHit && !bFriendlyHit ? Hit.GetActor() : nullptr;
	TWeakObjectPtr<UDustCombatFeedbackComponent> VictimFeedback = IsValid(DamageTarget)
		? DamageTarget->FindComponentByClass<UDustCombatFeedbackComponent>() : nullptr;
	DustShotDamageReceipt::FScopedCapture Receipt(DamageTarget);
	DustFireAudioPolicy::ApplyToEnemy(GetOwner());
	OnShotResolved.Broadcast(Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : End, Hit,
		bFriendlyHit ? 0.0f : FMath::Max(0.0f, Ballistics->DamagePerShot),
		bFriendlyHit ? 0.0f : FMath::Max(0.0f, Ballistics->ArmorPenetration),
		bFriendlyHit ? 0.0f : FMath::Max(0.0f, Ballistics->HeadDamageMultiplier), WeaponRuntime);
	// The synchronous Blueprint bridge may destroy actors; use only the receipt and weak component.
	if (Receipt.GetResult().bKilled)
	{
		if (UDustCombatFeedbackComponent* Feedback = VictimFeedback.Get())
			Feedback->NotifyKilledByEnemy();
	}
}

void UDustEnemyWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Cancel the owned reload transaction while the runtime still exists, then destroy it.
	if (UDustEnemyReloadComponent* Reload = GetOwner() ? GetOwner()->FindComponentByClass<UDustEnemyReloadComponent>() : nullptr)
		Reload->StopAllOperations();
	StopEngagement();
	OnShotResolved.Clear();
	Observations.Reset();
	if (IsValid(WeaponRuntime)) WeaponRuntime->Destroy();
	WeaponRuntime = nullptr;
	Super::EndPlay(EndPlayReason);
}
