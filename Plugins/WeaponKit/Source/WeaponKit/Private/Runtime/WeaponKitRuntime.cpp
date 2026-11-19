#include "Runtime/WeaponKitRuntime.h"
#include "Data/WeaponKitBallisticsData.h"
#include "Data/WeaponKitRecoilData.h"
#include "Engine/World.h"
#include "Math/FiringSpreadMath.h"
#include "Math/ShotRecoilMath.h"

namespace
{
float WeaponKitNonNegative(float Value)
{
	return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
}

FWeaponKitSpreadProfile WeaponKitFallbackSpread()
{
	FWeaponKitSpreadProfile Profile;
	Profile.StationarySpreadAngle = 0.0f;
	Profile.MovingSpreadAngle = 3.0f;
	Profile.JumpSpreadAngle = 12.0f;
	Profile.SlowWalkSpreadAngle = 1.0f;
	Profile.MovementSpreadSpeedThreshold = 10.0f;
	return Profile;
}
}

AWeaponKitRuntime::AWeaponKitRuntime()
{
	PrimaryActorTick.bCanEverTick = false;
	AllowedFireModes = { EWeaponKitFireMode::SemiAutomatic };
}

void AWeaponKitRuntime::SetAiming(bool bAiming)
{
	bOwnerAiming = bAiming;
}

void AWeaponKitRuntime::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinitionSettings();
}

void AWeaponKitRuntime::PostInitializeComponents()
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
		if (!bRestoredFireMode)
		{
			CurrentFireMode = WeaponDefinition->DefaultFireMode;
		}
		bInitialDefinitionApplied = true;
	}
}

void AWeaponKitRuntime::ApplyDefinitionSettings()
{
	const UWeaponKitDefinitionData* Definition = WeaponDefinition;
	if (!Definition)
	{
		return;
	}

	WeaponDisplayName = Definition->WeaponDisplayName;
	AllowedFireModes = Definition->AllowedFireModes;
	MagazineCapacity = FMath::Max(1, Definition->MagazineCapacity);
	FireInterval = FMath::Max(0.001f, WeaponKitNonNegative(Definition->FireInterval));
	BurstShotInterval = WeaponKitNonNegative(Definition->BurstShotInterval);
	RecoilData = Definition->RecoilData;
	bLockReloadAmountAtStart = Definition->bLockReloadAmountAtStart;
	bReloadOneRoundAtATime = Definition->bReloadOneRoundAtATime;
	bUseBatchReloadForFullEmptyReload = Definition->bUseBatchReloadForFullEmptyReload;
}

float AWeaponKitRuntime::GetBurstRecoveryInterval() const
{
	return WeaponDefinition ? WeaponKitNonNegative(WeaponDefinition->BurstRecoveryInterval) : 0.0f;
}

UWeaponKitBallisticsData* AWeaponKitRuntime::GetBallistics() const
{
	return WeaponDefinition ? WeaponDefinition->BallisticsData : nullptr;
}

FText AWeaponKitRuntime::GetWeaponDisplayName() const
{
	return WeaponDefinition ? WeaponDefinition->WeaponDisplayName : WeaponDisplayName;
}

void AWeaponKitRuntime::RestoreFireMode(EWeaponKitFireMode SavedFireMode)
{
	bRestoredFireMode = true;
	CurrentFireMode = SavedFireMode;
}

bool AWeaponKitRuntime::TryConsumeAmmo()
{
	if (CurrentAmmo <= 0)
	{
		return false;
	}
	--CurrentAmmo;
	return true;
}

bool AWeaponKitRuntime::AddReserveAmmo(int32 Amount)
{
	if (Amount <= 0)
	{
		return false;
	}
	ReserveAmmo += Amount;
	return true;
}

bool AWeaponKitRuntime::ReloadAmmo()
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

void AWeaponKitRuntime::RestoreAmmo(int32 NewCurrentAmmo, int32 NewReserveAmmo)
{
	bRestoredAmmo = true;
	ApplyDefinitionSettings();
	ResetRecoilPattern();
	if (bReloadOneRoundAtATime && bIsReloading)
	{
		CancelReload();
	}
	CurrentAmmo = FMath::Clamp(NewCurrentAmmo, 0, MagazineCapacity);
	ReserveAmmo = FMath::Max(NewReserveAmmo, 0);
}

bool AWeaponKitRuntime::TryCommitFire()
{
	bHasCommittedSpreadProfile = false;
	bCommittedIgnoreShotRecoil = false;
	CommittedFiringSpread = 0.0f;
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

	CommittedSpreadProfile = GetSpreadProfile(bOwnerAiming);
	bHasCommittedSpreadProfile = true;
	bCommittedIgnoreShotRecoil = bOwnerAiming;
	CacheCommittedFiringSpread(CurrentTime);
	CacheCommittedRecoil(CurrentTime);
	NextFireTime = CurrentTime + GetEffectiveFireInterval();
	OnFireCommitted.Broadcast();
	return true;
}

float AWeaponKitRuntime::GetEffectiveFireInterval() const
{
	if (CurrentFireMode == EWeaponKitFireMode::Burst && BurstShotInterval > 0.0f)
	{
		return BurstShotInterval;
	}
	return FireInterval;
}

void AWeaponKitRuntime::GenerateRecoilKick(float& PitchKick, float& YawKick, bool& bGenerated)
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
	if (!FMath::IsFinite(CommittedPitchKick) || !FMath::IsFinite(CommittedYawKick))
	{
		return;
	}

	PitchKick = CommittedPitchKick;
	YawKick = CommittedYawKick;
	bGenerated = true;
}

void AWeaponKitRuntime::ResetRecoilPattern()
{
	AccumulatedAimFiringSpread = 0.0f;
	bHasAimSpreadShot = false;
	bHasCommittedSpreadProfile = false;
	bCommittedIgnoreShotRecoil = false;
	AccumulatedFiringSpread = 0.0f;
	CommittedFiringSpread = 0.0f;
	bHasSpreadShot = false;
	AccumulatedShotRecoil = FVector2D::ZeroVector;
	CommittedShotRecoil = FVector2D::ZeroVector;
	bHasCommittedShotDirection = false;
	RecoilPatternIndex = 0;
	LastSuccessfulShotTime = 0.0f;
	bHasSuccessfulShotTime = false;
	ClearPendingRecoilKick();
}

void AWeaponKitRuntime::CacheCommittedRecoil(float SuccessfulShotTime)
{
	const bool bUseFixedPattern = RecoilData && RecoilData->bUseFixedPattern;
	const bool bIndependent = bUseFixedPattern && RecoilData->bUseIndependentShotRecoil;
	if (bUseFixedPattern && bHasSuccessfulShotTime)
	{
		const float ResetDelay = FMath::IsFinite(RecoilData->PatternResetDelay)
			? FMath::Max(0.0f, RecoilData->PatternResetDelay)
			: 0.0f;
		const float TimeSinceLastShot = SuccessfulShotTime - LastSuccessfulShotTime;
		if (FMath::IsFinite(TimeSinceLastShot) && TimeSinceLastShot >= ResetDelay)
		{
			RecoilPatternIndex = 0;
			AccumulatedShotRecoil = FVector2D::ZeroVector;
		}
		else if (bIndependent)
		{
			const float Delay = FMath::IsFinite(RecoilData->RecoveryDelay) ? FMath::Max(0.0f, RecoilData->RecoveryDelay) : 0.0f;
			const float Speed = FMath::IsFinite(RecoilData->ShotRecoilRecoverySpeed)
				? FMath::Max(0.0f, RecoilData->ShotRecoilRecoverySpeed)
				: 0.0f;
			AccumulatedShotRecoil = FWeaponKitShotRecoilMath::Recover(AccumulatedShotRecoil, TimeSinceLastShot, Delay, Speed);
		}
	}
	if (!bIndependent)
	{
		AccumulatedShotRecoil = FVector2D::ZeroVector;
	}
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
				? FMath::Max(0.0f, RecoilData->FixedPatternPitchScale)
				: 0.0f;
			const float YawScale = FMath::IsFinite(RecoilData->FixedPatternYawScale)
				? FMath::Max(0.0f, RecoilData->FixedPatternYawScale)
				: 0.0f;
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

	const float RawVerticalMin = FMath::Min(RecoilData->VerticalKickMin, RecoilData->VerticalKickMax);
	const float RawVerticalMax = FMath::Max(RecoilData->VerticalKickMin, RecoilData->VerticalKickMax);
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

void AWeaponKitRuntime::AdvanceRecoilPatternIndex(int32 PatternSize)
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
	RecoilPatternIndex = FMath::Clamp(RecoilData->LoopStartIndex, 0, PatternSize - 1);
}

void AWeaponKitRuntime::ClearPendingRecoilKick()
{
	CachedRecoilPitchKick = 0.0f;
	CachedRecoilYawKick = 0.0f;
	bHasPendingRecoilKick = false;
}

void AWeaponKitRuntime::GenerateShotDirection(
	const FVector& TraceDirection,
	float CurrentHorizontalSpeed,
	bool bIsFalling,
	bool bIsCrouched,
	bool bIsSlowWalking,
	FVector& ShotDirection,
	bool& bGenerated)
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
		if (!bHasCommittedShotDirection)
		{
			return;
		}
		if (!bCommittedIgnoreShotRecoil)
		{
			BaseDirection = FWeaponKitShotRecoilMath::Apply(BaseDirection, CommittedShotRecoil);
		}
		if (BaseDirection.IsNearlyZero())
		{
			return;
		}
	}

	const FWeaponKitSpreadProfile Profile = bHasCommittedSpreadProfile
		? CommittedSpreadProfile
		: GetSpreadProfile(bOwnerAiming);
	const float SafeMovementSpreadSpeedThreshold = FMath::Max(0.0f, Profile.MovementSpreadSpeedThreshold);
	float SelectedSpreadAngle = Profile.MovingSpreadAngle;
	if (bIsFalling)
	{
		SelectedSpreadAngle = Profile.JumpSpreadAngle;
	}
	else if (bIsCrouched)
	{
		SelectedSpreadAngle = Profile.StationarySpreadAngle;
	}
	else if (SafeCurrentHorizontalSpeed <= SafeMovementSpreadSpeedThreshold)
	{
		SelectedSpreadAngle = Profile.StationarySpreadAngle;
	}
	else if (bIsSlowWalking)
	{
		SelectedSpreadAngle = Profile.SlowWalkSpreadAngle;
	}

	const float SafeSpreadAngle = FMath::Clamp(
		(FMath::IsFinite(SelectedSpreadAngle) ? FMath::Max(0.0f, SelectedSpreadAngle) : 0.0f) + CommittedFiringSpread,
		0.0f,
		89.0f);
	ShotDirection = FMath::VRandCone(BaseDirection, FMath::DegreesToRadians(SafeSpreadAngle));
	bGenerated = true;
}

void AWeaponKitRuntime::CycleFireMode()
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
	CurrentFireMode = AllowedFireModes[(CurrentIndex + 1) % AllowedFireModes.Num()];
}

bool AWeaponKitRuntime::TryStartReload()
{
	if (bIsReloading || CurrentAmmo >= MagazineCapacity || ReserveAmmo <= 0)
	{
		return false;
	}

	PendingReloadAmount = (bLockReloadAmountAtStart || bReloadOneRoundAtATime)
		? FMath::Min(ReserveAmmo, MagazineCapacity - CurrentAmmo)
		: 0;
	bCurrentReloadUsesPerRound = bReloadOneRoundAtATime &&
		!(bUseBatchReloadForFullEmptyReload && CurrentAmmo == 0 && PendingReloadAmount == MagazineCapacity);
	ReloadRequestId = (ReloadRequestId == MAX_int32) ? 1 : ReloadRequestId + 1;
	bIsReloading = true;
	ResetRecoilPattern();
	OnReloadStarted.Broadcast();
	return true;
}

bool AWeaponKitRuntime::FinishReload()
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

bool AWeaponKitRuntime::CancelReload()
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

bool AWeaponKitRuntime::TryCommitReloadRound(int32 ExpectedReloadRequestId)
{
	if (!bIsReloading || !bCurrentReloadUsesPerRound)
	{
		return false;
	}
	if (ExpectedReloadRequestId <= 0 || ExpectedReloadRequestId != ReloadRequestId)
	{
		return false;
	}
	if (PendingReloadAmount <= 0 || CurrentAmmo >= MagazineCapacity || ReserveAmmo <= 0)
	{
		return false;
	}

	++CurrentAmmo;
	--ReserveAmmo;
	--PendingReloadAmount;
	return true;
}

FText AWeaponKitRuntime::GetDisplayNameForWeaponClass(TSubclassOf<AWeaponKitRuntime> WeaponClass)
{
	const AWeaponKitRuntime* Defaults = WeaponClass.GetDefaultObject();
	if (!Defaults)
	{
		return FText::GetEmpty();
	}
	return Defaults->WeaponDefinition ? Defaults->WeaponDefinition->WeaponDisplayName : Defaults->WeaponDisplayName;
}

void AWeaponKitRuntime::RestorePickupRuntimeState(const FWeaponKitPickupState& State)
{
	if (!State.bHasSavedState)
	{
		return;
	}
	RestoreAmmo(State.CurrentAmmo, State.ReserveAmmo);
	RestoreFireMode(State.FireMode);
}

FWeaponKitPickupState AWeaponKitRuntime::CapturePickupRuntimeState(bool bInitialDrawConsumed) const
{
	FWeaponKitPickupState State;
	State.bHasSavedState = true;
	State.CurrentAmmo = GetCurrentAmmo();
	State.ReserveAmmo = GetReserveAmmo();
	State.FireMode = GetFireMode();
	State.bInitialDrawConsumed = bInitialDrawConsumed;
	return State;
}

float AWeaponKitRuntime::GetCurrentFiringSpread() const
{
	return GetFiringSpreadForProfile(bOwnerAiming);
}

FWeaponKitSpreadProfile AWeaponKitRuntime::GetSpreadProfile(bool bAiming) const
{
	if (WeaponDefinition)
	{
		return WeaponDefinition->GetSpreadProfile(bAiming);
	}
	return WeaponKitFallbackSpread();
}

float AWeaponKitRuntime::GetFiringSpreadForProfile(bool bAiming) const
{
	const bool bAimProfile = bAiming && WeaponDefinition && WeaponDefinition->bUseSeparateAimingSpread;
	const FWeaponKitSpreadProfile Profile = GetSpreadProfile(bAimProfile);
	const bool bHasShot = bAimProfile ? bHasAimSpreadShot : bHasSpreadShot;
	if (!Profile.bUseFiringSpread || !bHasShot || !GetWorld())
	{
		return 0.0f;
	}
	return FWeaponKitFiringSpreadMath::Recover(
		bAimProfile ? AccumulatedAimFiringSpread : AccumulatedFiringSpread,
		GetWorld()->GetTimeSeconds() - (bAimProfile ? LastAimSpreadShotTime : LastSpreadShotTime),
		Profile.SpreadRecoveryDelay,
		Profile.SpreadRecoverySpeed,
		Profile.MaxFiringSpread);
}

void AWeaponKitRuntime::CacheCommittedFiringSpread(float SuccessfulShotTime)
{
	CommittedFiringSpread = GetCurrentFiringSpread();
	const float AimBefore = GetFiringSpreadForProfile(true);
	const float HipBefore = GetFiringSpreadForProfile(false);
	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
		const FWeaponKitSpreadProfile Profile = GetSpreadProfile(Mode == 1);
		float& Accumulated = Mode == 1 ? AccumulatedAimFiringSpread : AccumulatedFiringSpread;
		float& LastTime = Mode == 1 ? LastAimSpreadShotTime : LastSpreadShotTime;
		bool& bHasShot = Mode == 1 ? bHasAimSpreadShot : bHasSpreadShot;
		Accumulated = Profile.bUseFiringSpread
			? FWeaponKitFiringSpreadMath::AddShot(Mode == 1 ? AimBefore : HipBefore, Profile.SpreadPerShot, Profile.MaxFiringSpread)
			: 0.0f;
		LastTime = SuccessfulShotTime;
		bHasShot = Profile.bUseFiringSpread;
	}
}
