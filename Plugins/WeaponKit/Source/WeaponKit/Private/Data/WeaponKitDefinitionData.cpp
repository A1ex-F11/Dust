#include "Data/WeaponKitDefinitionData.h"
#include "Data/WeaponKitBallisticsData.h"

UWeaponKitDefinitionData::UWeaponKitDefinitionData()
{
	HipFire.StationarySpreadAngle = 0.0f;
	HipFire.MovingSpreadAngle = 3.0f;
	HipFire.JumpSpreadAngle = 12.0f;
	HipFire.SlowWalkSpreadAngle = 1.0f;
	HipFire.MovementSpreadSpeedThreshold = 10.0f;
	HipFire.bUseFiringSpread = false;
	HipFire.SpreadPerShot = 0.15f;
	HipFire.MaxFiringSpread = 0.8f;
	HipFire.SpreadRecoveryDelay = 0.2f;
	HipFire.SpreadRecoverySpeed = 2.0f;

	Aiming.StationarySpreadAngle = 0.0f;
	Aiming.MovingSpreadAngle = 1.5f;
	Aiming.JumpSpreadAngle = 6.0f;
	Aiming.SlowWalkSpreadAngle = 0.5f;
	Aiming.MovementSpreadSpeedThreshold = 10.0f;
	Aiming.bUseFiringSpread = false;
	Aiming.SpreadPerShot = 0.02f;
	Aiming.MaxFiringSpread = 0.3f;
	Aiming.SpreadRecoveryDelay = 0.15f;
	Aiming.SpreadRecoverySpeed = 1.5f;

	RecoilData = CreateDefaultSubobject<UWeaponKitRecoilData>(TEXT("Recoil"));
	RecoilData->SetFlags(RF_Public);
	BallisticsData = CreateDefaultSubobject<UWeaponKitBallisticsData>(TEXT("Ballistics"));
	BallisticsData->SetFlags(RF_Public);
}

const FWeaponKitSpreadProfile& UWeaponKitDefinitionData::GetSpreadProfile(bool bAiming) const
{
	return (bAiming && bUseSeparateAimingSpread) ? Aiming : HipFire;
}
