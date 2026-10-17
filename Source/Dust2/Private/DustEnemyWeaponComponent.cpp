#include "DustEnemyWeaponComponent.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyAimMath.h"
#include "WeaponRuntime.h"
#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"
#include "Dust2PlayerCharacter.h"
#include "GameFramework/GameModeBase.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	bool ActorAvailable(const AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return false;
		if (const auto* Player = Cast<ADust2PlayerCharacter>(Actor); Player && Player->IsDead()) return false;
		if (const FBoolProperty* Dead = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bIsDead")))
			if (Dead->GetPropertyValue_InContainer(Actor)) return false;
		return true;
	}
}

UDustEnemyWeaponComponent::UDustEnemyWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

bool UDustEnemyWeaponComponent::InitializeWeapon()
{
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

void UDustEnemyWeaponComponent::SetMuzzleSource(USceneComponent* Component, FName SocketName)
{
	// Only the owning enemy's component is accepted; prevents accidentally tracing from player 0.
	MuzzleSource = IsValid(Component) && Component->GetOwner() == GetOwner() ? Component : nullptr;
	MuzzleSocket = SocketName;
}

bool UDustEnemyWeaponComponent::StartEngagement(AActor* Target)
{
	if (bEngaging && EngagementTarget.Get() == Target) return true;
	StopEngagement();
	if (!bCombatEnabled || !ActorAvailable(GetOwner()) || !ActorAvailable(Target) || Target == GetOwner()
		|| !IsValid(CombatProfile) || !MuzzleSource.IsValid() || !InitializeWeapon() || WeaponRuntime->GetCurrentAmmo() <= 0) return false;
	EngagementTarget = Target;
	FVector Eye; GetOwner()->GetActorEyesViewPoint(Eye, TrackingRotation);
	const float Now = GetWorld()->GetTimeSeconds();
	ReadyAt = Now + FMath::Max(0.0f, CombatProfile->ReactionDelay);
	NextAttemptAt = ReadyAt;
	SettledTime = 0.0f;
	ShotsInVolley = 0;
	bEngaging = true;
	return true;
}

void UDustEnemyWeaponComponent::StopEngagement()
{
	if (bEngaging)
		if (const APawn* Pawn = Cast<APawn>(GetOwner()))
			if (AAIController* Controller = Cast<AAIController>(Pawn->GetController())) Controller->ClearFocus(EAIFocusPriority::Gameplay);
	bEngaging = false;
	EngagementTarget.Reset();
	SettledTime = 0.0f;
	ShotsInVolley = 0;
	// Keep physical recovery/compensation across short pauses; never refill ammo or reset the sequence.
}

void UDustEnemyWeaponComponent::SetCombatEnabled(bool bEnabled)
{
	bCombatEnabled = bEnabled;
	if (!bEnabled) StopEngagement();
}

bool UDustEnemyWeaponComponent::CanContinue() const
{
	// The current project stores terminal result on its Blueprint/native game mode.
	if (const AGameModeBase* Mode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
		if (const FBoolProperty* Resolved = FindFProperty<FBoolProperty>(Mode->GetClass(), TEXT("bResultResolved")))
			if (Resolved->GetPropertyValue_InContainer(Mode)) return false;
	return bCombatEnabled && ActorAvailable(GetOwner()) && ActorAvailable(EngagementTarget.Get())
		&& IsValid(WeaponRuntime) && IsValid(CombatProfile) && WeaponRuntime->WeaponDefinition == WeaponDefinition
		&& IsValid(WeaponDefinition) && IsValid(WeaponDefinition->BallisticsData) && MuzzleSource.IsValid();
}

bool UDustEnemyWeaponComponent::GetShotGeometry(FVector& OutMuzzle, FVector& OutAimPoint) const
{
	if (!CanContinue()) return false;
	const USceneComponent* Source = MuzzleSource.Get();
	if (!MuzzleSocket.IsNone() && !Source->DoesSocketExist(MuzzleSocket)) return false;
	OutMuzzle = Source->GetSocketLocation(MuzzleSocket);
	OutAimPoint = EngagementTarget->GetActorLocation() + FVector(0, 0, CombatProfile->AimHeightOffset);
	if (OutMuzzle.ContainsNaN() || OutAimPoint.ContainsNaN()) return false;
	const float Distance = FVector::Dist(GetOwner()->GetActorLocation(), OutAimPoint);
	if (Distance > FMath::Max(0.0f, CombatProfile->EngagementRange)
		|| Distance > FMath::Max(0.0f, WeaponDefinition->BallisticsData->TraceDistance)) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySight), true, GetOwner());
	Params.AddIgnoredActor(WeaponRuntime);
	FVector Eye; FRotator EyeRotation; GetOwner()->GetActorEyesViewPoint(Eye, EyeRotation);
	// A muzzle beyond a wall must not let the enemy shoot through that wall.
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, OutMuzzle, ECC_Visibility, Params)) return false;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, OutAimPoint, ECC_Visibility, Params)
		&& Hit.GetActor() != EngagementTarget.Get()) return false;
	return !(OutAimPoint - OutMuzzle).IsNearlyZero();
}

void UDustEnemyWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetWorld() || !IsValid(CombatProfile) || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector2D Recoil = IsValid(WeaponRuntime) ? WeaponRuntime->GetCurrentIndependentShotRecoil() : FVector2D::ZeroVector;
	Observations.Add({Now, Recoil});
	const float ObservationTime = Now - FMath::Clamp(CombatProfile->CompensationDelay, 0.0f, 0.5f);
	while (Observations.Num() > 1 && Observations[1].Time <= ObservationTime) Observations.RemoveAt(0, 1, EAllowShrinking::No);
	// Bound memory even when paused or delay is edited live.
	if (Observations.Num() > 256) Observations.RemoveAt(0, Observations.Num() - 256, EAllowShrinking::No);
	FVector2D Observed = Observations.Num() && Observations[0].Time <= ObservationTime ? Observations[0].Offset : FVector2D::ZeroVector;
	// On recovery, never keep compensating for recoil that no longer exists.
	if (Recoil.IsNearlyZero())
	{
		Observed = FVector2D::ZeroVector;
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
	if (!bEngaging) return;
	if (!CanContinue() || WeaponRuntime->GetCurrentAmmo() <= 0) { StopEngagement(); return; }
	FVector Muzzle, AimPoint;
	if (!GetShotGeometry(Muzzle, AimPoint)) { StopEngagement(); return; }
	TrackingRotation = FEnemyAimMath::StepTracking(TrackingRotation, (AimPoint - Muzzle).Rotation(), DeltaTime,
		CombatProfile->TrackingSpeedDegPerSecond);
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
		if (AAIController* Controller = Cast<AAIController>(Pawn->GetController()))
			Controller->SetFocalPoint(Muzzle + TrackingRotation.Vector() * 1000.0f, EAIFocusPriority::Gameplay);
	if (GetOwner()->GetVelocity().Size2D() <= FMath::Max(0.0f, CombatProfile->RequiredSettledSpeed)) SettledTime += DeltaTime;
	else SettledTime = 0.0f;
	if (Now < ReadyAt || SettledTime < FMath::Max(0.0f, CombatProfile->SettledAimSeconds) || Now < NextAttemptAt) return;
	// Drift affects control, never the weapon's random spread or the target's health directly.
	const FVector2D Saved = Compensation;
	Compensation += Drift * DriftScale;
	AttemptShot(Now, Muzzle, AimPoint);
	Compensation = Saved;
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
	++ShotsInVolley;
	float Interval = FMath::Max(0.001f, WeaponRuntime->GetEffectiveFireInterval());
	const EFireMode Mode = WeaponRuntime->GetFireMode();
	const int32 VolleySize = Mode == EFireMode::SemiAutomatic ? 1 : (Mode == EFireMode::Burst ? 3 : FMath::Max(1, CombatProfile->BurstShotCount));
	if (ShotsInVolley >= VolleySize)
	{
		ShotsInVolley = 0;
		Interval = FMath::Max(Interval, FMath::Max(0.0f, CombatProfile->BurstPauseSeconds));
		if (Mode == EFireMode::Burst) Interval = FMath::Max(Interval, WeaponDefinition->BurstRecoveryInterval);
	}
	NextAttemptAt = Now + Interval;
	if (bDrawDebugShots) DrawDebugLine(GetWorld(), Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : End, Hit.bBlockingHit ? FColor::Red : FColor::Green, false, 1.0f);
	OnShotResolved.Broadcast(Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : End, Hit,
		FMath::Max(0.0f, Ballistics->DamagePerShot), FMath::Max(0.0f, Ballistics->ArmorPenetration),
		FMath::Max(0.0f, Ballistics->HeadDamageMultiplier), WeaponRuntime);
}

void UDustEnemyWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopEngagement();
	OnShotResolved.Clear();
	Observations.Reset();
	if (IsValid(WeaponRuntime)) WeaponRuntime->Destroy();
	WeaponRuntime = nullptr;
	Super::EndPlay(EndPlayReason);
}
