
#include "WeaponRuntime.h"
#include "ShotRecoilMath.h"
#include "Engine/World.h"

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
	if (bCurrentReloadUsesPerRound && bIsReloading)
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
	ResetRecoilPattern();
	if (bReloadOneRoundAtATime && bIsReloading)
	{
		CancelReload();
	}

	CurrentAmmo = FMath::Clamp(NewCurrentAmmo, 0, MagazineCapacity);
	ReserveAmmo = FMath::Max(NewReserveAmmo, 0);
}
bool AWeaponRuntime::TryCommitFire()
{
	ClearPendingRecoilKick();
	bHasCommittedShotDirection = false;
	if (bIsReloading)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime < NextFireTime)
	{
		return false;
	}
	if (!TryConsumeAmmo())
	{
		return false;
	}
	CacheCommittedRecoil(CurrentTime);
	NextFireTime = CurrentTime + GetEffectiveFireInterval();
	OnFireCommitted.Broadcast();
	return true;
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

void AWeaponRuntime::ResetRecoilPattern()
{
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
	const float SafeCurrentHorizontalSpeed = FMath::Max(0.0f, CurrentHorizontalSpeed);
	if (RecoilData && RecoilData->bUseFixedPattern && RecoilData->bUseIndependentShotRecoil)
	{
		if (!bHasCommittedShotDirection) return;
		BaseDirection = FShotRecoilMath::Apply(BaseDirection, CommittedShotRecoil);
		if (BaseDirection.IsNearlyZero()) return;
	}
	const float SafeMovementSpreadSpeedThreshold = FMath::Max(0.0f, MovementSpreadSpeedThreshold);
	float SelectedSpreadAngle = MovingSpreadAngle;
	if (bIsFalling)
	{
		SelectedSpreadAngle = JumpSpreadAngle;
	}
	else if (bIsCrouched)
	{
		SelectedSpreadAngle = StationarySpreadAngle;
	}
	else if (SafeCurrentHorizontalSpeed <= SafeMovementSpreadSpeedThreshold)
	{
		SelectedSpreadAngle = StationarySpreadAngle;
	}
	else if (bIsSlowWalking)
	{
		SelectedSpreadAngle = SlowWalkSpreadAngle;
	}
	const float SafeSpreadAngle = FMath::Max(0.0f, SelectedSpreadAngle);
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

	PendingReloadAmount = (bLockReloadAmountAtStart || bReloadOneRoundAtATime)
		? FMath::Min(ReserveAmmo, MagazineCapacity - CurrentAmmo)
		: 0;
	bCurrentReloadUsesPerRound = bReloadOneRoundAtATime &&
		!(bUseBatchReloadForFullEmptyReload &&
			CurrentAmmo == 0 && PendingReloadAmount == MagazineCapacity);

	ReloadRequestId = (ReloadRequestId == MAX_int32)
		? 1
		: ReloadRequestId + 1;

	bIsReloading = true;
	ResetRecoilPattern();
	OnReloadStarted.Broadcast();
	return true;
}
bool AWeaponRuntime::FinishReload()
{
	if (!bIsReloading)
	{
		return false;
	}

	if (!bCurrentReloadUsesPerRound)
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
	bIsReloading = false;
	return true;
}
int32 AWeaponRuntime::GetReloadRequestId() const
{
	return ReloadRequestId;
}

bool AWeaponRuntime::TryCommitReloadRound(int32 ExpectedReloadRequestId)
{
	if (!bIsReloading || !bCurrentReloadUsesPerRound)
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
	return Defaults ? Defaults->WeaponDisplayName : FText::GetEmpty();
}
void AWeaponRuntime::RestorePickupRuntimeState(const FWeaponPickupState& State)
{
	if (!State.bHasSavedState)
	{
		return;
	}

	RestoreAmmo(State.CurrentAmmo, State.ReserveAmmo);
	RestoreFireMode(State.FireMode);
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
	return State;
}
