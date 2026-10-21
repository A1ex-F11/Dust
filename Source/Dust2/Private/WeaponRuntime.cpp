
#include "WeaponRuntime.h"
#include "ShotRecoilMath.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Perception/AISense_Hearing.h"
#include "DustActorState.h"
#include "WeaponDefinitionData.h"
#include "WeaponAccuracyData.h"
#include "WeaponBallisticsData.h"
#include "FiringSpreadMath.h"
#include "UObject/UnrealType.h"
#include "Templates/UnrealTemplate.h"

namespace
{
	// Shared by the committed shot and the read-only next-shot display.
	float ResolveTotalSpreadAngle(const FWeaponSpreadProfile& Profile, float CurrentHorizontalSpeed,
		bool bIsFalling, bool /*bIsCrouched*/, bool bIsSlowWalking, float FiringSpread)
	{
		const float SafeCurrentHorizontalSpeed = FMath::Max(0.0f, CurrentHorizontalSpeed);
		const float SafeMovementSpreadSpeedThreshold = FMath::Max(0.0f, Profile.MovementSpreadSpeedThreshold);
		if (Profile.bUseAdditiveStateSpread)
		{
			const auto Safe = [](float V) { return FMath::IsFinite(V) ? FMath::Max(0.0f, V) : 0.0f; };
			const float MovementExtra = SafeCurrentHorizontalSpeed > SafeMovementSpreadSpeedThreshold
				? Safe(bIsSlowWalking && !bIsFalling ? Profile.SlowWalkSpreadAngle : Profile.MovingSpreadAngle) : 0.0f;
			return FMath::Clamp(Safe(Profile.StationarySpreadAngle) + MovementExtra
				+ (bIsFalling ? Safe(Profile.JumpSpreadAngle) : 0.0f) + Safe(FiringSpread), 0.0f, 89.0f);
		}
		float SelectedSpreadAngle = Profile.MovingSpreadAngle;
		if (bIsFalling)
		{
			SelectedSpreadAngle = Profile.JumpSpreadAngle;
		}
		else if (SafeCurrentHorizontalSpeed <= SafeMovementSpreadSpeedThreshold)
		{
			SelectedSpreadAngle = Profile.StationarySpreadAngle;
		}
		else if (bIsSlowWalking)
		{
			SelectedSpreadAngle = Profile.SlowWalkSpreadAngle;
		}
		return FMath::Clamp(
			(FMath::IsFinite(SelectedSpreadAngle) ? FMath::Max(0.0f, SelectedSpreadAngle) : 0.0f) + FiringSpread, 0.0f, 89.0f);
	}

	/** Copy every recoil/gameplay field; the shared source asset is never modified. */
	void CopyRecoilSettings(UWeaponRecoilData& Target, const UWeaponRecoilData& Source)
	{
		Target.AimingRecoilScale = Source.AimingRecoilScale;
		Target.bUseFixedPattern = Source.bUseFixedPattern;
		Target.bUseIndependentShotRecoil = Source.bUseIndependentShotRecoil;
		Target.CameraFeedbackScale = Source.CameraFeedbackScale;
		Target.CameraFeedbackPreset = Source.CameraFeedbackPreset;
		Target.bOverrideCameraFeedback = Source.bOverrideCameraFeedback;
		Target.CameraFeedbackOverrides = Source.CameraFeedbackOverrides;
		Target.CameraReturnSpeed = Source.CameraReturnSpeed;
		Target.ShotRecoilRecoverySpeed = Source.ShotRecoilRecoverySpeed;
		Target.FixedPattern = Source.FixedPattern;
		Target.PatternResetDelay = Source.PatternResetDelay;
		Target.FixedPatternPitchScale = Source.FixedPatternPitchScale;
		Target.FixedPatternYawScale = Source.FixedPatternYawScale;
		Target.LoopStartIndex = Source.LoopStartIndex;
		Target.SmoothKickDuration = Source.SmoothKickDuration;
		Target.ImmediateKickFraction = Source.ImmediateKickFraction;
		Target.RecoveryDelay = Source.RecoveryDelay;
		Target.RecoverySpeed = Source.RecoverySpeed;
		Target.RecoveryPitchRatio = Source.RecoveryPitchRatio;
		Target.VerticalKickMin = Source.VerticalKickMin;
		Target.VerticalKickMax = Source.VerticalKickMax;
		Target.HorizontalKickMax = Source.HorizontalKickMax;
	}

	/** Overlay only the accuracy asset's camera fields, keeping recoil sequence/recovery intact. */
	void CopyAccuracyCamera(UWeaponRecoilData& Target, const UWeaponAccuracyData& Source)
	{
		Target.CameraFeedbackScale = Source.CameraFeedbackScale;
		Target.CameraFeedbackPreset = Source.CameraFeedbackPreset;
		Target.bOverrideCameraFeedback = Source.bOverrideCameraFeedback;
		Target.CameraFeedbackOverrides = Source.CameraFeedbackOverrides;
		Target.CameraReturnSpeed = Source.CameraReturnSpeed;
		Target.SmoothKickDuration = Source.SmoothKickDuration;
		Target.ImmediateKickFraction = Source.ImmediateKickFraction;
	}
}
AWeaponRuntime::AWeaponRuntime()

{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	CurrentAmmo = 30;
	MagazineCapacity = 30;
	ReserveAmmo = 90;
	FireInterval = 0.15f;
	NextFireTime = 0.0f;
	BurstShotInterval = 0.0f;
	StationarySpreadAngle = 0.0f;
	JumpSpreadAngle = 12.0f;
	SlowWalkSpreadAngle = 1.0f;
	MovingSpreadAngle = 3.0f;
	MovementSpreadSpeedThreshold = 10.0f;
	bIsReloading = false;
}
EFireMode AWeaponRuntime::GetFireMode() const
{
	return CurrentFireMode;
}
void AWeaponRuntime::RestoreFireMode(EFireMode SavedFireMode)
{
	bHasPendingNativeShot = false;
	bRestoredFireMode = true;
	CurrentFireMode = SavedFireMode;

}
int32 AWeaponRuntime::GetCurrentAmmo() const
{
	return CurrentAmmo;
}
int32 AWeaponRuntime::GetReserveAmmo()const
{
	return ReserveAmmo;
}
int32 AWeaponRuntime::GetMagazineCapacity()const
{
	return MagazineCapacity;
}
bool AWeaponRuntime::TryConsumeAmmo()
{
	if (CurrentAmmo <= 0)
	{
		return false;
	}
	--CurrentAmmo;
	return true;
}
bool AWeaponRuntime::AddReserveAmmo(int32 Amount)
{
	if (Amount <= 0)
	{
		return false;
	}
	ReserveAmmo += Amount;
	return true;
}
bool AWeaponRuntime::ReloadAmmo()
{
	// A batch/infinite request owns the whole transaction; an early direct refill would desync
	// the captured animation amount, so it is rejected just like the old per-round path.
	if ((bCurrentReloadUsesPerRound || bCurrentReloadUsesEmptyBatch || bCurrentReloadUsesInfiniteSource)
		&& bIsReloading)
	{
		return false;
	}

	const int32 MissingAmmo = MagazineCapacity - CurrentAmmo;
	if (MissingAmmo <= 0 || ReserveAmmo <= 0)
	{
		return false;
	}

	const int32 ReloadAmount = FMath::Min(ReserveAmmo, MissingAmmo);
	CurrentAmmo += ReloadAmount;
	ReserveAmmo -= ReloadAmount;
	return true;
}
void AWeaponRuntime::RestoreAmmo(int32 NewCurrentAmmo, int32 NewReserveAmmo)
{
	bHasPendingNativeShot = false;
	bRestoredAmmo = true;
	ApplyDefinitionSettings();
	ResetRecoilPattern();
	if ((bReloadOneRoundAtATime || bCurrentReloadUsesEmptyBatch || bCurrentReloadUsesInfiniteSource)
		&& bIsReloading)
	{
		CancelReload();
	}

	CurrentAmmo = FMath::Clamp(NewCurrentAmmo, 0, MagazineCapacity);
	ReserveAmmo = FMath::Max(NewReserveAmmo, 0);
}
bool AWeaponRuntime::TryCommitFire()
{
	return TryCommitFireWithResult() == EWeaponFireAttemptResult::Committed;
}

void AWeaponRuntime::ReportCommittedGunshotNoise()
{
	if (!IsValid(WeaponDefinition))
	{
		return;
	}
	const float RangeCm = WeaponDefinition->GetGunshotHearingRangeCm();
	if (RangeCm <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || DustActorState::IsMissionOver(World))
	{
		return;
	}

	APawn* Source = Cast<APawn>(GetOwner());
	if (!Source)
	{
		Source = Cast<APawn>(GetInstigator());
	}
	if (!Source)
	{
		Source = Cast<APawn>(GetParentActor());
	}
	if (!Source || !DustActorState::IsAlive(Source))
	{
		return;
	}

	UAISense_Hearing::ReportNoiseEvent(
		World, Source->GetActorLocation(), 1.0f, Source, RangeCm, FName(TEXT("Gunshot")));
}

EWeaponFireAttemptResult AWeaponRuntime::TryCommitFireWithResult()
{
	// Presentation and damage delegates are synchronous. Reject nested fire before touching
	// the outer shot's ammo, recoil/spread cache or one-shot resolution credential.
	if (bCommittingFire || bResolvingNativeShot) return EWeaponFireAttemptResult::Busy;
	TGuardValue<bool> CommitGuard(bCommittingFire, true);
	// A rejected attempt cannot leave a previous shot available to the new BP wrapper.
	bHasPendingNativeShot = false;
	bHasCommittedSpreadProfile = false;
	bCommittedIgnoreShotRecoil = false;
	CommittedFiringSpread = 0.0f;
	ClearPendingRecoilKick();
	bHasCommittedShotDirection = false;
	if (bIsReloading)
	{
		return EWeaponFireAttemptResult::Reloading;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return EWeaponFireAttemptResult::NoWorld;
	}
	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime < NextFireTime)
	{
		return EWeaponFireAttemptResult::Cooldown;
	}
	if (!TryConsumeAmmo())
	{
		return EWeaponFireAttemptResult::Empty;
	}
	CommittedSpreadProfile = GetSpreadProfile(bOwnerAiming);
	bHasCommittedSpreadProfile = true;
	// ADS follows the supplied sight-aligned view ray; do not add hidden ballistic recoil.
	bCommittedIgnoreShotRecoil = bOwnerAiming;
	CacheCommittedFiringSpread(CurrentTime);
	CacheCommittedRecoil(CurrentTime);
	NextFireTime = CurrentTime + GetEffectiveFireInterval();
	bHasPendingNativeShot = true;
	ReportCommittedGunshotNoise();
	OnFireCommitted.Broadcast();
	return EWeaponFireAttemptResult::Committed;
}
FWeaponShotResolution AWeaponRuntime::ResolveCommittedShotNative(const FVector& TraceStart, const FVector& ShotDirection)
{
	if (!bHasPendingNativeShot || bCommittingFire || bResolvingNativeShot || IsActorBeingDestroyed() || !GetWorld() || bIsReloading)
	{
		return FWeaponShotResolution();
	}
	// Consume before interface dispatch: ReceiveDamage can synchronously reenter gameplay.
	bHasPendingNativeShot = false;
	TGuardValue<bool> ResolvingGuard(bResolvingNativeShot, true);
	const UWeaponBallisticsData* Ballistics = nullptr;
	if (IsValid(WeaponDefinition))
	{
		Ballistics = WeaponDefinition->BallisticsData;
	}
	else if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("BallisticsData")))
	{
		// Compatibility with the existing BP_WeaponBase field; never add a same-name native UPROPERTY.
		Ballistics = Cast<UWeaponBallisticsData>(Property->GetObjectPropertyValue_InContainer(this));
	}
	return DustWeaponHit::Resolve(this, TraceStart, ShotDirection, Ballistics);
}
float AWeaponRuntime::GetEffectiveFireInterval() const
{
	if (CurrentFireMode == EFireMode::Burst && BurstShotInterval > 0.0f)
	{
		return BurstShotInterval;
	}
	return FireInterval;
}
void AWeaponRuntime::GenerateRecoilKick(
	float& PitchKick,
	float& YawKick,
	bool& bGenerated)
{
	PitchKick = 0.0f;
	YawKick = 0.0f;
	bGenerated = false;
	if (!bHasPendingRecoilKick)
	{
		return;
	}

	const float CommittedPitchKick = CachedRecoilPitchKick;
	const float CommittedYawKick = CachedRecoilYawKick;
	ClearPendingRecoilKick();
	if (!FMath::IsFinite(CommittedPitchKick) ||
		!FMath::IsFinite(CommittedYawKick))
	{
		return;
	}

	PitchKick = CommittedPitchKick;
	YawKick = CommittedYawKick;
	bGenerated = true;
}

FVector2D AWeaponRuntime::GetCurrentIndependentShotRecoil() const
{
	if (!RecoilData || !RecoilData->bUseFixedPattern || !RecoilData->bUseIndependentShotRecoil
		|| !bHasSuccessfulShotTime || !GetWorld()) return FVector2D::ZeroVector;
	const float QuietTime = FMath::Max(0.0f, GetWorld()->GetTimeSeconds() - LastSuccessfulShotTime);
	const float Reset = FMath::IsFinite(RecoilData->PatternResetDelay) ? FMath::Max(0.0f, RecoilData->PatternResetDelay) : 0.0f;
	if (QuietTime >= Reset) return FVector2D::ZeroVector;
	const float Delay = FMath::IsFinite(RecoilData->RecoveryDelay) ? FMath::Max(0.0f, RecoilData->RecoveryDelay) : 0.0f;
	const float Speed = FMath::IsFinite(RecoilData->ShotRecoilRecoverySpeed) ? FMath::Max(0.0f, RecoilData->ShotRecoilRecoverySpeed) : 0.0f;
	return FShotRecoilMath::Recover(AccumulatedShotRecoil, QuietTime, Delay, Speed);
}

FVector2D AWeaponRuntime::GetCommittedIndependentShotRecoil() const
{
	return bHasCommittedShotDirection && !bCommittedIgnoreShotRecoil ? CommittedShotRecoil : FVector2D::ZeroVector;
}

void AWeaponRuntime::ResetRecoilPattern()
{
	AccumulatedAimFiringSpread = 0.0f;
	bHasAimSpreadShot = false;
	bHasCommittedSpreadProfile = false;
	bCommittedIgnoreShotRecoil = false;
	AccumulatedFiringSpread = 0.0f;
	CommittedFiringSpread = 0.0f;
	bHasSpreadShot = false;
	ResetRecoilForWeaponChange();
}

void AWeaponRuntime::ResetRecoilForWeaponChange()
{
	bHasPendingNativeShot = false;
	bHasCommittedSpreadProfile = false;
	bCommittedIgnoreShotRecoil = false;
	CommittedFiringSpread = 0.0f;
	AccumulatedShotRecoil = FVector2D::ZeroVector;
	CommittedShotRecoil = FVector2D::ZeroVector;
	bHasCommittedShotDirection = false;
	RecoilPatternIndex = 0;
	LastSuccessfulShotTime = 0.0f;
	bHasSuccessfulShotTime = false;
	ClearPendingRecoilKick();
}

void AWeaponRuntime::CacheCommittedRecoil(float SuccessfulShotTime)
{
	const bool bUseFixedPattern = RecoilData && RecoilData->bUseFixedPattern;
	const bool bIndependent = bUseFixedPattern && RecoilData->bUseIndependentShotRecoil;
	if (bUseFixedPattern && bHasSuccessfulShotTime)
	{
		const float ResetDelay = FMath::IsFinite(RecoilData->PatternResetDelay)
			? FMath::Max(0.0f, RecoilData->PatternResetDelay)
			: 0.0f;
		const float TimeSinceLastShot = SuccessfulShotTime - LastSuccessfulShotTime;
		if (FMath::IsFinite(TimeSinceLastShot) &&
			TimeSinceLastShot >= ResetDelay)
		{
			RecoilPatternIndex = 0;
			AccumulatedShotRecoil = FVector2D::ZeroVector;
		}
		else if (bIndependent)
		{
			const float Delay = FMath::IsFinite(RecoilData->RecoveryDelay) ? FMath::Max(0.0f, RecoilData->RecoveryDelay) : 0.0f;
			const float Speed = FMath::IsFinite(RecoilData->ShotRecoilRecoverySpeed) ? FMath::Max(0.0f, RecoilData->ShotRecoilRecoverySpeed) : 0.0f;
			AccumulatedShotRecoil = FShotRecoilMath::Recover(AccumulatedShotRecoil, TimeSinceLastShot, Delay, Speed);
		}
	}
	if (!bIndependent) AccumulatedShotRecoil = FVector2D::ZeroVector;
	// This shot uses the prior total. Its impulse affects the following accepted shot.
	CommittedShotRecoil = bIndependent ? AccumulatedShotRecoil : FVector2D::ZeroVector;
	bHasCommittedShotDirection = true;

	LastSuccessfulShotTime = SuccessfulShotTime;
	bHasSuccessfulShotTime = true;

	if (!RecoilData)
	{
		return;
	}

	if (RecoilData->bUseFixedPattern)
	{
		const int32 PatternSize = RecoilData->FixedPattern.Num();
		if (PatternSize <= 0)
		{
			RecoilPatternIndex = 0;
			return;
		}

		RecoilPatternIndex = FMath::Clamp(RecoilPatternIndex, 0, PatternSize - 1);
		const FVector2D& PatternKick = RecoilData->FixedPattern[RecoilPatternIndex];
		if (FMath::IsFinite(PatternKick.X) && FMath::IsFinite(PatternKick.Y))
		{
			const float PitchScale = FMath::IsFinite(RecoilData->FixedPatternPitchScale)
				? FMath::Max(0.0f, RecoilData->FixedPatternPitchScale) : 0.0f;
			const float YawScale = FMath::IsFinite(RecoilData->FixedPatternYawScale)
				? FMath::Max(0.0f, RecoilData->FixedPatternYawScale) : 0.0f;
			const float ScaledPitch = FMath::Max(0.0f, PatternKick.X) * PitchScale;
			const float ScaledYaw = PatternKick.Y * YawScale;
			if (FMath::IsFinite(ScaledPitch) && FMath::IsFinite(ScaledYaw))
			{
				CachedRecoilPitchKick = ScaledPitch;
				CachedRecoilYawKick = ScaledYaw;
				bHasPendingRecoilKick = true;
				if (bIndependent)
				{
					AccumulatedShotRecoil.X = FMath::Clamp(AccumulatedShotRecoil.X + ScaledPitch, 0.0, 60.0);
					AccumulatedShotRecoil.Y = FMath::Clamp(AccumulatedShotRecoil.Y + ScaledYaw, -60.0, 60.0);
				}
			}
		}
		AdvanceRecoilPatternIndex(PatternSize);
		return;
	}

	if (!FMath::IsFinite(RecoilData->VerticalKickMin) ||
		!FMath::IsFinite(RecoilData->VerticalKickMax) ||
		!FMath::IsFinite(RecoilData->HorizontalKickMax))
	{
		return;
	}

	const float RawVerticalMin = FMath::Min(
		RecoilData->VerticalKickMin,
		RecoilData->VerticalKickMax);
	const float RawVerticalMax = FMath::Max(
		RecoilData->VerticalKickMin,
		RecoilData->VerticalKickMax);
	const float VerticalMin = FMath::Max(0.0f, RawVerticalMin);
	const float VerticalMax = FMath::Max(VerticalMin, RawVerticalMax);
	const float HorizontalMax = FMath::Abs(RecoilData->HorizontalKickMax);
	const float PitchKick = FMath::FRandRange(VerticalMin, VerticalMax);
	const float YawKick = FMath::FRandRange(-HorizontalMax, HorizontalMax);
	if (!FMath::IsFinite(PitchKick) || !FMath::IsFinite(YawKick))
	{
		return;
	}

	CachedRecoilPitchKick = PitchKick;
	CachedRecoilYawKick = YawKick;
	bHasPendingRecoilKick = true;
}

void AWeaponRuntime::AdvanceRecoilPatternIndex(int32 PatternSize)
{
	if (PatternSize <= 0)
	{
		RecoilPatternIndex = 0;
		return;
	}

	const int32 NextIndex = RecoilPatternIndex + 1;
	if (NextIndex < PatternSize)
	{
		RecoilPatternIndex = NextIndex;
		return;
	}

	if (!RecoilData || RecoilData->LoopStartIndex < 0)
	{
		RecoilPatternIndex = PatternSize - 1;
		return;
	}

	RecoilPatternIndex = FMath::Clamp(
		RecoilData->LoopStartIndex,
		0,
		PatternSize - 1);
}

void AWeaponRuntime::ClearPendingRecoilKick()
{
	CachedRecoilPitchKick = 0.0f;
	CachedRecoilYawKick = 0.0f;
	bHasPendingRecoilKick = false;
}
void AWeaponRuntime::GenerateShotDirection(
	const FVector& TraceDirection,
	float CurrentHorizontalSpeed,
	bool bIsFalling,
	bool bIsCrouched,
	bool bIsSlowWalking,
	FVector& ShotDirection,
	bool& bGenerated
)
{
	ShotDirection = FVector::ZeroVector;
	bGenerated = false;
	FVector BaseDirection = TraceDirection.GetSafeNormal();
	if (BaseDirection.IsNearlyZero())
	{
		return;
	}
	if (RecoilData && RecoilData->bUseFixedPattern && RecoilData->bUseIndependentShotRecoil)
	{
		if (!bHasCommittedShotDirection) return;
		if (!bCommittedIgnoreShotRecoil)
			BaseDirection = FShotRecoilMath::Apply(BaseDirection, CommittedShotRecoil);
		if (BaseDirection.IsNearlyZero()) return;
	}
	const FWeaponSpreadProfile Profile = bHasCommittedSpreadProfile
		? CommittedSpreadProfile : GetSpreadProfile(bOwnerAiming);
	const float SafeSpreadAngle = ResolveTotalSpreadAngle(Profile, CurrentHorizontalSpeed,
		bIsFalling, bIsCrouched, bIsSlowWalking, CommittedFiringSpread);
	const float ConeHalfAngleRadians = FMath::DegreesToRadians(SafeSpreadAngle);
	ShotDirection = FMath::VRandCone(
		BaseDirection,
		ConeHalfAngleRadians
	);
	bGenerated = true;
}
void AWeaponRuntime::CycleFireMode()
{
	if (AllowedFireModes.Num() < 2)
	{
		return;
	}
	bHasPendingNativeShot = false;
	const int32 CurrentIndex = AllowedFireModes.Find(CurrentFireMode);
	if (CurrentIndex == INDEX_NONE)
	{
		CurrentFireMode = AllowedFireModes[0];
		return;
	}
	const int32 NextIndex = (CurrentIndex + 1) % AllowedFireModes.Num();
	CurrentFireMode = AllowedFireModes[NextIndex];
}
bool AWeaponRuntime::IsReloading()const
{
	return bIsReloading;
}
bool AWeaponRuntime::TryStartReload()
{
	if (bIsReloading || CurrentAmmo >= MagazineCapacity || ReserveAmmo <= 0)
	{
		return false;
	}

	const int32 LockedAmount = (bLockReloadAmountAtStart || bReloadOneRoundAtATime)
		? FMath::Min(ReserveAmmo, MagazineCapacity - CurrentAmmo)
		: 0;
	const bool bPerRound = bReloadOneRoundAtATime &&
		!(bUseBatchReloadForFullEmptyReload &&
			CurrentAmmo == 0 && LockedAmount == MagazineCapacity);

	StartReloadInternal(LockedAmount, bPerRound, false);
	return true;
}

bool AWeaponRuntime::TryStartEmptyBatchReload(int32 ExpectedAmount)
{
	if (bIsReloading) return false;
	// Only the explicit empty-batch definition opt-in is accepted; no per-round guessing.
	if (!IsValid(WeaponDefinition) || !WeaponDefinition->UsesEmptyBatchReload()) return false;
	if (CurrentAmmo != 0 || ReserveAmmo <= 0) return false;

	const int32 RequiredAmount = FMath::Min(MagazineCapacity - CurrentAmmo, ReserveAmmo);
	if (ExpectedAmount <= 0 || ExpectedAmount != RequiredAmount) return false;

	// This request is forced batch (no per-round commit) for this transaction only.
	StartReloadInternal(ExpectedAmount, false, true);
	return true;
}

bool AWeaponRuntime::TryStartInfiniteEmptyReload()
{
	if (bIsReloading) return false;
	if (!IsValid(WeaponDefinition)) return false;
	// Ordinary Magazine and EmptyBatch are supported; a pure per-round/sniper definition is not
	// silently converted by the enemy unlimited-source policy.
	if (WeaponDefinition->UsesPerRoundReload() && !WeaponDefinition->UsesEmptyBatchReload()) return false;
	// The source is unlimited, but the magazine itself must be genuinely empty and finite.
	if (CurrentAmmo != 0) return false;
	const int32 RefillAmount = FMath::Max(0, MagazineCapacity - CurrentAmmo);
	if (RefillAmount <= 0) return false;

	// Capture the unlimited source and exact full-magazine refill before OnReloadStarted.
	StartReloadInternal(RefillAmount, false, false, true);
	return true;
}

void AWeaponRuntime::StartReloadInternal(int32 InPendingAmount, bool bInPerRound,
	bool bInEmptyBatch, bool bInInfiniteSource)
{
	PendingReloadAmount = InPendingAmount;
	bCurrentReloadUsesPerRound = bInPerRound;
	bCurrentReloadUsesEmptyBatch = bInEmptyBatch;
	bCurrentReloadUsesInfiniteSource = bInInfiniteSource;

	ReloadRequestId = (ReloadRequestId == MAX_int32)
		? 1
		: ReloadRequestId + 1;

	bIsReloading = true;
	ResetRecoilPattern();
	OnReloadStarted.Broadcast();
}

bool AWeaponRuntime::FinishReload()
{
	if (!bIsReloading)
	{
		return false;
	}

	if (bCurrentReloadUsesInfiniteSource)
	{
		// One full-magazine commit from the captured unlimited source. ReserveAmmo is
		// deliberately left at its original value.
		const int32 MissingAmmo = FMath::Max(0, MagazineCapacity - CurrentAmmo);
		const int32 AmountToLoad = FMath::Min(FMath::Max(0, PendingReloadAmount), MissingAmmo);
		CurrentAmmo += AmountToLoad;
	}
	else if (bCurrentReloadUsesEmptyBatch)
	{
		// The captured PendingReloadAmount is the hard upper bound; the normal ammo
		// availability guards still prevent an overfill or a negative reserve.
		const int32 MissingAmmo = FMath::Max(0, MagazineCapacity - CurrentAmmo);
		const int32 AvailableAmmo = FMath::Max(0, ReserveAmmo);
		const int32 AmountToLoad = FMath::Min(
			FMath::Max(0, PendingReloadAmount),
			FMath::Min(MissingAmmo, AvailableAmmo));

		CurrentAmmo += AmountToLoad;
		ReserveAmmo -= AmountToLoad;
	}
	else if (!bCurrentReloadUsesPerRound)
	{
		if (bLockReloadAmountAtStart || bReloadOneRoundAtATime)
		{
			const int32 MissingAmmo = FMath::Max(0, MagazineCapacity - CurrentAmmo);
			const int32 AvailableAmmo = FMath::Max(0, ReserveAmmo);
			const int32 AmountToLoad = FMath::Min(
				FMath::Max(0, PendingReloadAmount),
				FMath::Min(MissingAmmo, AvailableAmmo));

			CurrentAmmo += AmountToLoad;
			ReserveAmmo -= AmountToLoad;
		}
		else
		{
			ReloadAmmo();
		}
	}

	PendingReloadAmount = 0;
	bCurrentReloadUsesPerRound = false;
	bCurrentReloadUsesEmptyBatch = false;
	bCurrentReloadUsesInfiniteSource = false;
	bIsReloading = false;
	return true;
}
bool AWeaponRuntime::CancelReload()
{
	if (!bIsReloading)
	{
		return false;
	}

	PendingReloadAmount = 0;
	bCurrentReloadUsesPerRound = false;
	bCurrentReloadUsesEmptyBatch = false;
	bCurrentReloadUsesInfiniteSource = false;
	bIsReloading = false;
	return true;
}
int32 AWeaponRuntime::GetReloadRequestId() const
{
	return ReloadRequestId;
}

bool AWeaponRuntime::TryCommitReloadRound(int32 ExpectedReloadRequestId)
{
	if (!bIsReloading || !bCurrentReloadUsesPerRound
		|| bCurrentReloadUsesEmptyBatch || bCurrentReloadUsesInfiniteSource)
	{
		return false;
	}

	if (ExpectedReloadRequestId <= 0 ||
		ExpectedReloadRequestId != ReloadRequestId)
	{
		return false;
	}

	if (PendingReloadAmount <= 0 ||
		CurrentAmmo >= MagazineCapacity ||
		ReserveAmmo <= 0)
	{
		return false;
	}


	++CurrentAmmo;
	--ReserveAmmo;
	--PendingReloadAmount;

	return true;
}
FText AWeaponRuntime::GetDisplayNameForWeaponClass(
	TSubclassOf<AWeaponRuntime> WeaponClass)
{
	const AWeaponRuntime* Defaults = WeaponClass.GetDefaultObject();
	return Defaults ? (Defaults->WeaponDefinition ? Defaults->WeaponDefinition->WeaponDisplayName : Defaults->WeaponDisplayName) : FText::GetEmpty();
}
void AWeaponRuntime::RestorePickupRuntimeState(const FWeaponPickupState& State)
{
	if (!State.bHasSavedState)
	{
		return;
	}

	RestoreAmmo(State.CurrentAmmo, State.ReserveAmmo);
	RestoreFireMode(State.FireMode);
	if (State.bHasFiringSpreadSnapshot && GetWorld())
	{
		const auto Safe = [](float Value) { return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f; };
		const float Now = GetWorld()->GetTimeSeconds();
		const float DroppedElapsed = FMath::Max(0.0f, Now - Safe(State.CapturedWorldTime));
		AccumulatedFiringSpread = Safe(State.HipAccumulatedSpread);
		AccumulatedAimFiringSpread = Safe(State.AimAccumulatedSpread);
		LastSpreadShotTime = Now - Safe(State.HipSecondsSinceShot) - DroppedElapsed;
		LastAimSpreadShotTime = Now - Safe(State.AimSecondsSinceShot) - DroppedElapsed;
		bHasSpreadShot = State.bHasHipSpreadShot;
		bHasAimSpreadShot = State.bHasAimSpreadShot;
	}
}
FWeaponPickupState AWeaponRuntime::CapturePickupRuntimeState(
	bool bInitialDrawConsumed) const
{
	FWeaponPickupState State;
	State.bHasSavedState = true;
	State.CurrentAmmo = GetCurrentAmmo();
	State.ReserveAmmo = GetReserveAmmo();
	State.FireMode = GetFireMode();
	State.bInitialDrawConsumed = bInitialDrawConsumed;
	if (GetWorld())
	{
		State.bHasFiringSpreadSnapshot = true;
		State.CapturedWorldTime = GetWorld()->GetTimeSeconds();
		State.HipAccumulatedSpread = AccumulatedFiringSpread;
		State.AimAccumulatedSpread = AccumulatedAimFiringSpread;
		State.HipSecondsSinceShot = FMath::Max(0.0f, State.CapturedWorldTime - LastSpreadShotTime);
		State.AimSecondsSinceShot = FMath::Max(0.0f, State.CapturedWorldTime - LastAimSpreadShotTime);
		State.bHasHipSpreadShot = bHasSpreadShot;
		State.bHasAimSpreadShot = bHasAimSpreadShot;
	}
	return State;
}

namespace
{
	float NonNegative(float Value) { return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f; }
}

void AWeaponRuntime::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinitionSettings();
}

void AWeaponRuntime::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyDefinitionSettings();
	if (WeaponDefinition && !bInitialDefinitionApplied)
	{
  if (!bRestoredAmmo)
  {
   CurrentAmmo = FMath::Clamp(WeaponDefinition->InitialMagazineAmmo, 0, MagazineCapacity);
   ReserveAmmo = FMath::Max(0, WeaponDefinition->InitialReserveAmmo);
  }
  if (!bRestoredFireMode) CurrentFireMode = WeaponDefinition->GetInitialFireMode();
  bInitialDefinitionApplied = true;
	}
}

void AWeaponRuntime::ApplyDefinitionSettings()
{
	if (!WeaponDefinition) return;
	const UWeaponDefinitionData* D = WeaponDefinition;
	WeaponDisplayName = D->WeaponDisplayName;
	AllowedFireModes = D->AllowedFireModes;
	MagazineCapacity = FMath::Max(1, D->MagazineCapacity);
	FireInterval = FMath::Max(0.001f, NonNegative(D->FireInterval));
	BurstShotInterval = NonNegative(D->BurstShotInterval);
	const UWeaponRecoilData* SourceRecoil = D->RecoilData;
	if (D->AccuracyData && SourceRecoil && GetWorld())
	{
		// Private copy: recoil gameplay from the shared asset, camera from the accuracy
		// asset. The pointer is stable across repeated Apply calls and is never saved.
		if (!RuntimeRecoilData)
		{
			RuntimeRecoilData = NewObject<UWeaponRecoilData>(this, NAME_None, RF_Transient);
		}
		if (RuntimeRecoilData)
		{
			CopyRecoilSettings(*RuntimeRecoilData, *SourceRecoil);
			CopyAccuracyCamera(*RuntimeRecoilData, *D->AccuracyData.Get());
			RecoilData = RuntimeRecoilData;
		}
		else
		{
			RecoilData = D->RecoilData;
		}
	}
	else
	{
		RecoilData = D->RecoilData;
	}
	StationarySpreadAngle = NonNegative(D->StationarySpreadAngle);
	MovingSpreadAngle = NonNegative(D->MovingSpreadAngle);
	JumpSpreadAngle = NonNegative(D->JumpSpreadAngle);
	SlowWalkSpreadAngle = NonNegative(D->SlowWalkSpreadAngle);
	MovementSpreadSpeedThreshold = NonNegative(D->MovementSpreadSpeedThreshold);
	bLockReloadAmountAtStart = D->bLockReloadAmountAtStart;
	bReloadOneRoundAtATime = D->UsesPerRoundReload();
	bUseBatchReloadForFullEmptyReload = D->UsesEmptyBatchReload();
	// Existing Blueprint readers remain compatible; only the definition owns static data.
	if (FObjectPropertyBase* P = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("BallisticsData")))
	{
  if (P->PropertyClass == UWeaponBallisticsData::StaticClass()) P->SetObjectPropertyValue_InContainer(this, D->BallisticsData);
	}
	if (FNumericProperty* P = FindFProperty<FNumericProperty>(GetClass(), TEXT("BurstRecoveryInterval")); P && P->IsFloatingPoint())
  P->SetFloatingPointPropertyValue(P->ContainerPtrToValuePtr<void>(this), NonNegative(D->BurstRecoveryInterval));
}

void AWeaponRuntime::CaptureDefinition(UWeaponDefinitionData* D) const
{
	if (!D) return;
	D->bUseIntuitiveHipAccuracy = WeaponDefinition && (WeaponDefinition->HasUnifiedConfiguration() || WeaponDefinition->bUseIntuitiveHipAccuracy);
	D->HipAccuracy = WeaponDefinition ? WeaponDefinition->HipAccuracy : FWeaponIntuitiveAccuracy{};
	D->bUseIntuitiveAimAccuracy = WeaponDefinition && (WeaponDefinition->HasUnifiedConfiguration() || WeaponDefinition->bUseIntuitiveAimAccuracy);
	D->AimAccuracy = WeaponDefinition ? WeaponDefinition->AimAccuracy : FWeaponIntuitiveAccuracy{};
	D->bUseAimingSpread = WeaponDefinition && WeaponDefinition->bUseAimingSpread;
	D->AimingSpread = WeaponDefinition ? WeaponDefinition->AimingSpread : FWeaponSpreadProfile{};
	D->bIgnoreShotRecoilWhenAiming = WeaponDefinition && WeaponDefinition->bIgnoreShotRecoilWhenAiming;
	D->WeaponDisplayName = WeaponDisplayName;
	D->DefaultFireMode = CurrentFireMode;
	D->ConfigurationVersion = WeaponDefinition ? WeaponDefinition->ConfigurationVersion : 0;
	D->ReloadStyle = WeaponDefinition ? WeaponDefinition->ReloadStyle : EWeaponReloadStyle::Magazine;
	D->AllowedFireModes = AllowedFireModes;
	if(D->HasUnifiedConfiguration())
	{
		const int32 Start=D->AllowedFireModes.IndexOfByKey(CurrentFireMode);
		if(Start>0) { const TArray<EFireMode> Old=D->AllowedFireModes; for(int32 I=0;I<Old.Num();++I) D->AllowedFireModes[I]=Old[(Start+I)%Old.Num()]; }
	}
	D->MagazineCapacity = MagazineCapacity;
	D->InitialMagazineAmmo = CurrentAmmo;
	D->InitialReserveAmmo = ReserveAmmo;
	D->FireInterval = FireInterval;
	D->BurstShotInterval = BurstShotInterval;
	D->StationarySpreadAngle = StationarySpreadAngle;
	D->MovingSpreadAngle = MovingSpreadAngle;
	D->JumpSpreadAngle = JumpSpreadAngle;
	D->SlowWalkSpreadAngle = SlowWalkSpreadAngle;
	D->MovementSpreadSpeedThreshold = MovementSpreadSpeedThreshold;
	D->bLockReloadAmountAtStart = bLockReloadAmountAtStart;
	D->bReloadOneRoundAtATime = bReloadOneRoundAtATime;
	D->bUseBatchReloadForFullEmptyReload = bUseBatchReloadForFullEmptyReload;
	// Effective accuracy/camera is copied into a private object so the shared accuracy
	// asset is never written back to. Guns without AccuracyData keep the old path.
	UWeaponAccuracyData* SourceAccuracy = WeaponDefinition ? WeaponDefinition->AccuracyData.Get() : nullptr;
	D->AccuracyData = nullptr;
	if (SourceAccuracy)
	{
		if (UWeaponAccuracyData* CapturedAccuracy = DuplicateObject<UWeaponAccuracyData>(SourceAccuracy, D))
		{
			CapturedAccuracy->ClearFlags(RF_Standalone | RF_Transient);
			CapturedAccuracy->SetFlags(RF_Public);
			D->AccuracyData = CapturedAccuracy;
			D->HipAccuracy = SourceAccuracy->HipAccuracy;
			D->AimAccuracy = SourceAccuracy->AimAccuracy;
			D->bUseAimingSpread = SourceAccuracy->bUseAimingSpread;
		}
	}
	UWeaponRecoilData* RecoilSource = (SourceAccuracy && WeaponDefinition && WeaponDefinition->RecoilData)
		? WeaponDefinition->RecoilData.Get() : RecoilData.Get();
	if (RecoilSource)
	{
		if (UWeaponRecoilData* CapturedRecoil = DuplicateObject<UWeaponRecoilData>(RecoilSource, D))
		{
			CapturedRecoil->ClearFlags(RF_Standalone | RF_Transient);
			CapturedRecoil->SetFlags(RF_Public);
			D->RecoilData = CapturedRecoil;
		}
	}
	if (const FObjectPropertyBase* P = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("BallisticsData")))
  if (const UWeaponBallisticsData* B = Cast<UWeaponBallisticsData>(P->GetObjectPropertyValue_InContainer(this)))
   { D->BallisticsData = DuplicateObject<UWeaponBallisticsData>(B, D); D->BallisticsData->ClearFlags(RF_Standalone); D->BallisticsData->SetFlags(RF_Public); }
	if (const FNumericProperty* P = FindFProperty<FNumericProperty>(GetClass(), TEXT("BurstRecoveryInterval")); P && P->IsFloatingPoint())
  D->BurstRecoveryInterval = P->GetFloatingPointPropertyValue(P->ContainerPtrToValuePtr<void>(this));
}

float AWeaponRuntime::GetCurrentFiringSpread() const
{
	return GetFiringSpreadForProfile(bOwnerAiming);
}

float AWeaponRuntime::GetCurrentTotalSpreadAngle(float CurrentHorizontalSpeed,
	bool bIsFalling, bool bIsCrouched, bool bIsSlowWalking) const
{
	return ResolveTotalSpreadAngle(GetSpreadProfile(bOwnerAiming), CurrentHorizontalSpeed,
		bIsFalling, bIsCrouched, bIsSlowWalking, GetCurrentFiringSpread());
}
FWeaponSpreadProfile AWeaponRuntime::GetSpreadProfile(bool bAiming) const
{
	// Split accuracy asset wins whenever present; old Definitions keep the complete
	// legacy branch below unchanged.
	if (WeaponDefinition && WeaponDefinition->AccuracyData)
	{
		const UWeaponAccuracyData& Accuracy = *WeaponDefinition->AccuracyData.Get();
		return bAiming && Accuracy.bUseAimingSpread
			? Accuracy.AimAccuracy.BuildProfile(GetEffectiveFireInterval(), MovementSpreadSpeedThreshold)
			: Accuracy.HipAccuracy.BuildProfile(GetEffectiveFireInterval(), MovementSpreadSpeedThreshold);
	}
	if (bAiming && WeaponDefinition && WeaponDefinition->bUseAimingSpread)
		return (WeaponDefinition->HasUnifiedConfiguration() || WeaponDefinition->bUseIntuitiveAimAccuracy)
			? WeaponDefinition->AimAccuracy.BuildProfile(GetEffectiveFireInterval(), MovementSpreadSpeedThreshold)
			: WeaponDefinition->AimingSpread;
	if (WeaponDefinition && (WeaponDefinition->HasUnifiedConfiguration() || WeaponDefinition->bUseIntuitiveHipAccuracy))
		return WeaponDefinition->HipAccuracy.BuildProfile(GetEffectiveFireInterval(), MovementSpreadSpeedThreshold);
	FWeaponSpreadProfile P;
	P.StationarySpreadAngle = StationarySpreadAngle;
	P.MovingSpreadAngle = MovingSpreadAngle;
	P.JumpSpreadAngle = JumpSpreadAngle;
	P.SlowWalkSpreadAngle = SlowWalkSpreadAngle;
	P.MovementSpreadSpeedThreshold = MovementSpreadSpeedThreshold;
	if (WeaponDefinition)
	{
  P.bUseFiringSpread = WeaponDefinition->bUseFiringSpread;
  P.SpreadPerShot = WeaponDefinition->SpreadPerShot;
  P.MaxFiringSpread = WeaponDefinition->MaxFiringSpread;
  P.SpreadRecoveryDelay = WeaponDefinition->SpreadRecoveryDelay;
  P.SpreadRecoverySpeed = WeaponDefinition->SpreadRecoverySpeed;
	}
	return P;
}

float AWeaponRuntime::GetFiringSpreadForProfile(bool bAiming) const
{
	const bool bAimProfile = bAiming && WeaponDefinition && WeaponDefinition->UsesAimingSpread();
	const FWeaponSpreadProfile P = GetSpreadProfile(bAimProfile);
	const bool bHasShot = bAimProfile ? bHasAimSpreadShot : bHasSpreadShot;
	if (!P.bUseFiringSpread || !bHasShot || !GetWorld()) return 0.0f;
	return FFiringSpreadMath::Recover(bAimProfile ? AccumulatedAimFiringSpread : AccumulatedFiringSpread,
  GetWorld()->GetTimeSeconds() - (bAimProfile ? LastAimSpreadShotTime : LastSpreadShotTime),
  P.SpreadRecoveryDelay, P.SpreadRecoverySpeed, P.MaxFiringSpread);
}

void AWeaponRuntime::CacheCommittedFiringSpread(float SuccessfulShotTime)
{
	CommittedFiringSpread = GetCurrentFiringSpread();
	// Advance both profiles on every accepted shot: changing aim cannot reset bloom.
	const float AimBefore = GetFiringSpreadForProfile(true);
	const float HipBefore = GetFiringSpreadForProfile(false);
	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
  const FWeaponSpreadProfile P = GetSpreadProfile(Mode == 1);
  float& Accumulated = Mode == 1 ? AccumulatedAimFiringSpread : AccumulatedFiringSpread;
  float& LastTime = Mode == 1 ? LastAimSpreadShotTime : LastSpreadShotTime;
  bool& bHasShot = Mode == 1 ? bHasAimSpreadShot : bHasSpreadShot;
  Accumulated = P.bUseFiringSpread
   ? FFiringSpreadMath::AddShot(Mode == 1 ? AimBefore : HipBefore, P.SpreadPerShot, P.MaxFiringSpread) : 0.0f;
  LastTime = SuccessfulShotTime;
  bHasShot = P.bUseFiringSpread;
	}
}
