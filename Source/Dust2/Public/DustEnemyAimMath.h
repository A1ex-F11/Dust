#pragma once

#include "CoreMinimal.h"

/** Pure helpers for AI target tracking and recoil compensation. Angles are degrees. */
struct DUST2_API FEnemyAimMath
{
	/**
	 * Move the AI's positive compensation offset toward a fraction of the weapon recoil.
	 * X stores pitch recoil and Y stores signed yaw recoil. The resulting compensation is
	 * subtracted from the aim rotator by the caller.
	 */
	static FVector2D StepCompensation(
		const FVector2D& CurrentOffsetDegrees,
		const FVector2D& DesiredWeaponRecoilDegrees,
		float DeltaSeconds,
		float Fraction,
		float SpeedDegreesPerSecond)
	{
		const FVector2D Current = ClampCompensation(Sanitize(CurrentOffsetDegrees));
		const FVector2D WeaponRecoil = ClampCompensation(Sanitize(DesiredWeaponRecoilDegrees));
		const float SafeFraction = FMath::IsFinite(Fraction)
			? FMath::Clamp(Fraction, 0.0f, 1.0f)
			: 0.0f;
		const FVector2D Desired = ClampCompensation(WeaponRecoil * SafeFraction);

		const float MaxStep = GetMaxStep(DeltaSeconds, SpeedDegreesPerSecond);
		return MoveToward(Current, Desired, MaxStep);
	}

	/** Turn toward a target rotator at a fixed angular speed, using the shortest yaw arc. */
	static FRotator StepTracking(
		const FRotator& CurrentRotator,
		const FRotator& DesiredRotator,
		float DeltaSeconds,
		float SpeedDegreesPerSecond)
	{
		const FRotator Current = Sanitize(CurrentRotator);
		const FRotator Desired = Sanitize(DesiredRotator);
		const float MaxStep = GetMaxStep(DeltaSeconds, SpeedDegreesPerSecond);

		FRotator Result;
		Result.Pitch = FMath::Clamp(
			Current.Pitch + FMath::Clamp(FMath::FindDeltaAngleDegrees(Current.Pitch, Desired.Pitch), -MaxStep, MaxStep), -89.0f, 89.0f);
		Result.Yaw = FMath::UnwindDegrees(
			FMath::FixedTurn(Current.Yaw, Desired.Yaw, MaxStep));
		Result.Roll = FMath::UnwindDegrees(
			FMath::FixedTurn(Current.Roll, Desired.Roll, MaxStep));
		return Result;
	}

	/** Recover a positive compensation offset toward zero without crossing it. */
	static FVector2D Recover(
		const FVector2D& CurrentOffsetDegrees,
		float DeltaSeconds,
		float SpeedDegreesPerSecond)
	{
		const FVector2D Current = ClampCompensation(Sanitize(CurrentOffsetDegrees));
		return MoveToward(Current, FVector2D::ZeroVector,
			GetMaxStep(DeltaSeconds, SpeedDegreesPerSecond));
	}

private:
	static constexpr float MaxPitchCompensationDegrees = 60.0f;
	static constexpr float MaxYawCompensationDegrees = 60.0f;

	static FVector2D Sanitize(const FVector2D& Value)
	{
		return FVector2D(
			FMath::IsFinite(Value.X) ? Value.X : 0.0f,
			FMath::IsFinite(Value.Y) ? Value.Y : 0.0f);
	}

	static FVector2D ClampCompensation(const FVector2D& Value)
	{
		return FVector2D(
			FMath::Clamp(Value.X, 0.0f, MaxPitchCompensationDegrees),
			FMath::Clamp(Value.Y, -MaxYawCompensationDegrees, MaxYawCompensationDegrees));
	}

	static FRotator Sanitize(const FRotator& Value)
	{
		const float Pitch = FMath::IsFinite(Value.Pitch) ? Value.Pitch : 0.0f;
		const float Yaw = FMath::IsFinite(Value.Yaw) ? Value.Yaw : 0.0f;
		const float Roll = FMath::IsFinite(Value.Roll) ? Value.Roll : 0.0f;
		return FRotator(
			FMath::Clamp(FMath::UnwindDegrees(Pitch), -89.0f, 89.0f),
			FMath::UnwindDegrees(Yaw),
			FMath::UnwindDegrees(Roll));
	}

	static float GetMaxStep(float DeltaSeconds, float SpeedDegreesPerSecond)
	{
		if (!FMath::IsFinite(DeltaSeconds) || !FMath::IsFinite(SpeedDegreesPerSecond))
		{
			return 0.0f;
		}

		const double SafeDelta = FMath::Max(0.0, static_cast<double>(DeltaSeconds));
		const double SafeSpeed = FMath::Max(0.0, static_cast<double>(SpeedDegreesPerSecond));
		return static_cast<float>(FMath::Min(SafeDelta * SafeSpeed, 180.0));
	}

	static FVector2D MoveToward(
		const FVector2D& Current,
		const FVector2D& Target,
		float MaxStepDegrees)
	{
		const FVector2D Delta = Target - Current;
		const float Distance = Delta.Size();
		if (!FMath::IsFinite(Distance) || Distance <= KINDA_SMALL_NUMBER ||
			MaxStepDegrees <= 0.0f)
		{
			return Current;
		}

		if (Distance <= MaxStepDegrees)
		{
			return Target;
		}

		return Current + Delta * (MaxStepDegrees / Distance);
	}
};
