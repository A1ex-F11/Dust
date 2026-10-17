#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"

UWeaponDefinitionData::UWeaponDefinitionData()
{
	RecoilData = CreateDefaultSubobject<UWeaponRecoilData>(TEXT("Recoil"));
	RecoilData->SetFlags(RF_Public);
	BallisticsData = CreateDefaultSubobject<UWeaponBallisticsData>(TEXT("Ballistics"));
	BallisticsData->SetFlags(RF_Public);
}

FWeaponSpreadProfile FWeaponIntuitiveAccuracy::BuildProfile(float ShotInterval, float MovementThreshold) const
{
	const auto Safe = [](float V) { return FMath::IsFinite(V) ? FMath::Max(0.0f, V) : 0.0f; };
	FWeaponSpreadProfile P;
	P.bUseAdditiveStateSpread = true;
	P.StationarySpreadAngle = Safe(BaseSpreadAngle);
	P.MovingSpreadAngle = Safe(MovingExtraAngle);
	P.SlowWalkSpreadAngle = Safe(SlowWalkExtraAngle);
	P.JumpSpreadAngle = Safe(AirborneExtraAngle);
	P.MovementSpreadSpeedThreshold = Safe(MovementThreshold);
	P.MaxFiringSpread = FMath::Min(89.0f, Safe(MaxFiringExtraAngle));
	P.bUseFiringSpread = P.MaxFiringSpread > 0.0f;
	P.SpreadRecoveryDelay = 0.0f;
	const float Seconds = FMath::Max(0.01f, Safe(FullRecoverySeconds));
	P.SpreadRecoverySpeed = P.MaxFiringSpread / Seconds;
	const int32 Count = FMath::Clamp(ShotsToMaximum, 1, 1000);
	// First shot adds p; each subsequent one adds p-r*interval. Account for recovery.
	const double PerShot = static_cast<double>(P.MaxFiringSpread) / Count
		+ static_cast<double>(P.SpreadRecoverySpeed) * Safe(ShotInterval) * (Count - 1) / Count;
	P.SpreadPerShot = static_cast<float>(FMath::Min(PerShot, static_cast<double>(P.MaxFiringSpread)));
	return P;
}
