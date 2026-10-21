#pragma once

#include "CoreMinimal.h"

struct FWeaponIntuitiveAccuracy;
class UWeaponRecoilData;

namespace DustWeaponPresetCleanup
{
    /** Copy the currently effective values locally; return true only if a preset was detached. */
    DUST2_API bool LocalizePosture(FWeaponIntuitiveAccuracy& Accuracy, float MovementThreshold);
    DUST2_API bool LocalizeCamera(UWeaponRecoilData& Recoil);
}
