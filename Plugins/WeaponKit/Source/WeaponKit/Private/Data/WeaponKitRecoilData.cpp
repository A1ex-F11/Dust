#include "Data/WeaponKitRecoilData.h"

UWeaponKitRecoilData::UWeaponKitRecoilData()
	: bUseFixedPattern(false)
	, PatternResetDelay(0.25f)
	, FixedPatternPitchScale(1.0f)
	, FixedPatternYawScale(1.0f)
	, LoopStartIndex(-1)
	, SmoothKickDuration(0.05f)
	, ImmediateKickFraction(0.25f)
	, RecoveryDelay(0.2f)
	, RecoverySpeed(1.0f)
	, RecoveryPitchRatio(0.35f)
	, VerticalKickMin(0.35f)
	, VerticalKickMax(0.50f)
	, HorizontalKickMax(0.08f)
{
}
