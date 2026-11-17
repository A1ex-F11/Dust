#include "Presentation/WeaponSwayRuntime.h"
#include <cfloat>

namespace
{
	bool IsStepValid(float DeltaSeconds)
	{
		return FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f;
	}

	bool IsVectorFinite(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}
	bool IsSpringVectorSafe(const FVector& Value)
	{
		return IsVectorFinite(Value) && Value.GetAbsMax() <= FLT_MAX;
	}

	void InterpTarget(FVector& Current, const FVector& Target, const FVector& Speed, float Dt)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
			Current[Axis] = FMath::FInterpTo(Current[Axis], Target[Axis], Dt, Speed[Axis]);
	}

	bool AccumulateAndReturn(FVector& Current, const FVector& Delta,
		const FWeaponSwayVectorSettings& Settings, bool bAiming, float Dt)
	{
		const float Ads = bAiming ? Settings.AdsScale : 1.0f;
		FVector Next = Current;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Sum = Current[Axis] + Delta[Axis] * Settings.Scale[Axis] * Ads;
			if (!FMath::IsFinite(Sum)) return false;
			const double Value = FMath::Clamp(Sum,
				-Settings.ClampValue[Axis], Settings.ClampValue[Axis]);
			Next[Axis] = FMath::FInterpTo(Value, 0.0, Dt, Settings.SmoothingSpeed[Axis]);
		}
		if (!IsSpringVectorSafe(Next)) return false;
		Current = Next;
		return true;
	}

	bool StepSpring(FVector& Current, const FVector& Target,
		FFloatSpringState (&Springs)[3], const FWeaponSwayVectorSettings& Settings, float Dt)
	{
		if (!IsSpringVectorSafe(Current) || !IsSpringVectorSafe(Target)
			|| !IsSpringVectorSafe(Settings.Stiffness) || !IsSpringVectorSafe(Settings.Damping)) return false;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			// Match the Blueprint wrapper: Speed scales time, not spring stiffness.
			const float SpringDt = static_cast<float>(FMath::Min(Settings.InterpSpeed[Axis] * Dt, 1.0));
			Current[Axis] = UKismetMathLibrary::FloatSpringInterp(
				static_cast<float>(Current[Axis]), static_cast<float>(Target[Axis]), Springs[Axis],
				static_cast<float>(Settings.Stiffness[Axis]), static_cast<float>(Settings.Damping[Axis]),
				SpringDt, 1.0f, 1.0f);
			if (!FMath::IsFinite(Current[Axis]) || !FMath::IsFinite(Springs[Axis].Velocity)
				|| !FMath::IsFinite(Springs[Axis].PrevTarget)) return false;
		}
		return true;
	}

	bool ComputeLayer(FWeaponSwayLayerState& State, const FWeaponSwaySettings& Settings, float Dt)
	{
		if (!IsStepValid(Dt) || !Settings.IsUsable()) return false;
		FWeaponSwayLayerState Next = State;
		if (!StepSpring(Next.Location, Next.TargetLocation, Next.LocationSprings, Settings.Position, Dt)
			|| !StepSpring(Next.Rotation, Next.TargetRotation, Next.RotationSprings, Settings.Rotation, Dt)) return false;
		State = Next;
		return true;
	}
}

bool FWeaponSwayRuntime::ComputeMoveTarget(const FWeaponSwaySettings& Settings,
	const FVector& PositionInput, const FVector& RotationInput, bool bAiming, float DeltaSeconds)
{
	if (!IsStepValid(DeltaSeconds) || !Settings.IsUsable()
		|| !IsVectorFinite(PositionInput) || !IsVectorFinite(RotationInput)) return false;
	const FVector Position = PositionInput * Settings.Position.Scale * (bAiming ? Settings.Position.AdsScale : 1.0f);
	const FVector Rotation = RotationInput * Settings.Rotation.Scale * (bAiming ? Settings.Rotation.AdsScale : 1.0f);
	if (!IsSpringVectorSafe(Position) || !IsSpringVectorSafe(Rotation)) return false;
	InterpTarget(Move.TargetLocation, Position, Settings.Position.SmoothingSpeed, DeltaSeconds);
	InterpTarget(Move.TargetRotation, Rotation, Settings.Rotation.SmoothingSpeed, DeltaSeconds);
	return true;
}

bool FWeaponSwayRuntime::ComputeAimTarget(const FWeaponSwaySettings& Settings,
	const FVector& PositionDelta, const FVector& RotationDelta, bool bAiming, float DeltaSeconds)
{
	if (!IsStepValid(DeltaSeconds) || !Settings.IsUsable()
		|| !IsVectorFinite(PositionDelta) || !IsVectorFinite(RotationDelta)) return false;
	FVector Position = Aim.TargetLocation;
	Position.X = 0.0;
	FVector Rotation = Aim.TargetRotation;
	if (!AccumulateAndReturn(Position, FVector(0.0, PositionDelta.Y, PositionDelta.Z), Settings.Position, bAiming, DeltaSeconds)
		|| !AccumulateAndReturn(Rotation, RotationDelta, Settings.Rotation, bAiming, DeltaSeconds)) return false;
	Aim.TargetLocation = Position;
	Aim.TargetRotation = Rotation;
	return true;
}

bool FWeaponSwayRuntime::ComputeMove(const FWeaponSwaySettings& Settings, float DeltaSeconds)
{
	return ComputeLayer(Move, Settings, DeltaSeconds);
}

bool FWeaponSwayRuntime::ComputeAim(const FWeaponSwaySettings& Settings, float DeltaSeconds)
{
	return ComputeLayer(Aim, Settings, DeltaSeconds);
}
