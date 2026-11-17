#include "Presentation/WeaponViewmodelTypes.h"

namespace
{
	bool IsFiniteViewmodelVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsNonNegativeVector(const FVector& Value)
	{
		return IsFiniteViewmodelVector(Value) && Value.X >= 0.0 && Value.Y >= 0.0 && Value.Z >= 0.0;
	}
}

bool FWeaponSwayVectorSettings::IsUsable() const
{
	return IsNonNegativeVector(Stiffness) && IsNonNegativeVector(Damping)
		&& IsNonNegativeVector(InterpSpeed) && IsNonNegativeVector(SmoothingSpeed)
		&& IsNonNegativeVector(ClampValue) && IsFiniteViewmodelVector(Scale)
		&& FMath::IsFinite(AdsScale) && AdsScale >= 0.0f;
}

bool FWeaponViewmodelPose::IsUsable() const
{
	return IsFiniteViewmodelVector(PivotOffset) && PoseOffset.IsValid() && SprintOffset.IsValid()
		&& IsFiniteViewmodelVector(RightHandOffset) && IsFiniteViewmodelVector(LeftHandOffset)
		&& IsFiniteViewmodelVector(LeftElbowSprintOffset);
}

bool FWeaponViewmodelSettings::IsUsable() const
{
	return IsValid(IdlePose.Get()) && Viewmodel.IsUsable()
		&& MovementSway.IsUsable() && AimingSway.IsUsable()
		&& FMath::IsFinite(WalkIntensity) && WalkIntensity >= 0.0f
		&& FMath::IsFinite(SprintIntensity) && SprintIntensity >= 0.0f;
}
