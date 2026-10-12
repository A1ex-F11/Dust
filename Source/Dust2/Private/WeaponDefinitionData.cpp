#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"

UWeaponDefinitionData::UWeaponDefinitionData()
{
	RecoilData = CreateDefaultSubobject<UWeaponRecoilData>(TEXT("Recoil"));
	RecoilData->SetFlags(RF_Public);
	BallisticsData = CreateDefaultSubobject<UWeaponBallisticsData>(TEXT("Ballistics"));
	BallisticsData->SetFlags(RF_Public);
}
