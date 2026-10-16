#pragma once

#include "CoreMinimal.h"

/**
 * Pure display-space smoothing for a crosshair gap. The owner supplies targets
 * and delta time; this type has no widget, player, weapon, or world dependency.
 */
struct FCrosshairDisplayTransitionState
{
	void Initialize(const FVector2D& InitialGap)
	{
		CurrentGap = SanitizeGap(InitialGap, FVector2D::ZeroVector);
		TransitionStartGap = CurrentGap;
		TransitionElapsedSeconds = 0.0f;
		TransitionDurationSeconds = 0.0f;
		bTransitionActive = false;
	}

	/** Start a timed transition from the gap currently shown on screen. */
	void BeginTransition(float DurationSeconds)
	{
		TransitionStartGap = CurrentGap;
		TransitionElapsedSeconds = 0.0f;
		TransitionDurationSeconds = FMath::IsFinite(DurationSeconds)
			? FMath::Max(0.0f, DurationSeconds)
			: 0.0f;
		bTransitionActive = true;
	}

	/** Shorten an active transition to at most MaxSeconds from the current display. */
	bool ConstrainRemainingTime(float MaxSeconds)
	{
		if (!bTransitionActive)
		{
			return false;
		}

		const float SafeMaxSeconds = FMath::IsFinite(MaxSeconds)
			? FMath::Max(0.0f, MaxSeconds)
			: 0.0f;
		const float RemainingSeconds = GetRemainingTime();
		if (SafeMaxSeconds >= RemainingSeconds)
		{
			return false;
		}

		TransitionStartGap = CurrentGap;
		TransitionElapsedSeconds = 0.0f;
		TransitionDurationSeconds = SafeMaxSeconds;
		return true;
	}

	/** Immediately land on the latest target and end any active transition. */
	void FinishTransition(const FVector2D& TargetGap)
	{
		CurrentGap = SanitizeGap(TargetGap, CurrentGap);
		TransitionStartGap = CurrentGap;
		TransitionElapsedSeconds = 0.0f;
		TransitionDurationSeconds = 0.0f;
		bTransitionActive = false;
	}

	/**
	 * Advance toward the latest target. During a timed transition the endpoint
	 * follows target changes and is reached exactly at the deadline. Otherwise,
	 * RatePerSecond is an exponential-smoothing rate (constant-target motion is
	 * invariant to frame partitioning).
	 */
	FVector2D Advance(const FVector2D& TargetGap, float DeltaSeconds, float RatePerSecond)
	{
		const FVector2D SafeTarget = SanitizeGap(TargetGap, CurrentGap);

		// A zero or invalid transition duration means snap on this next Advance,
		// even when DeltaSeconds itself is invalid.
		if (bTransitionActive && TransitionDurationSeconds <= 0.0f)
		{
			CurrentGap = SafeTarget;
			bTransitionActive = false;
			return CurrentGap;
		}

		const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds)
			? FMath::Max(0.0f, DeltaSeconds)
			: 0.0f;

		if (bTransitionActive)
		{
			TransitionElapsedSeconds = FMath::Min(
				TransitionDurationSeconds,
				TransitionElapsedSeconds + SafeDeltaSeconds);

			const float LinearAlpha = FMath::Clamp(
				TransitionElapsedSeconds / TransitionDurationSeconds,
				0.0f, 1.0f);
			const float SmoothAlpha = LinearAlpha * LinearAlpha * (3.0f - 2.0f * LinearAlpha);

			if (TransitionElapsedSeconds >= TransitionDurationSeconds)
			{
				CurrentGap = SafeTarget;
				bTransitionActive = false;
			}
			else
			{
				CurrentGap = FMath::Lerp(TransitionStartGap, SafeTarget, SmoothAlpha);
			}
			return CurrentGap;
		}

		const float SafeRatePerSecond = FMath::IsFinite(RatePerSecond)
			? FMath::Max(0.0f, RatePerSecond)
			: 0.0f;
		if (SafeDeltaSeconds <= 0.0f || SafeRatePerSecond <= 0.0f)
		{
			return CurrentGap;
		}

		const float Alpha = 1.0f - FMath::Exp(-SafeRatePerSecond * SafeDeltaSeconds);
		CurrentGap = FMath::Lerp(CurrentGap, SafeTarget, FMath::Clamp(Alpha, 0.0f, 1.0f));
		return CurrentGap;
	}

	const FVector2D& GetCurrentGap() const { return CurrentGap; }
	bool IsTransitioning() const { return bTransitionActive; }
	float GetRemainingTime() const
	{
		return bTransitionActive
			? FMath::Max(0.0f, TransitionDurationSeconds - TransitionElapsedSeconds)
			: 0.0f;
	}

private:
	static FVector2D SanitizeGap(const FVector2D& Value, const FVector2D& Fallback)
	{
		return FVector2D(
			SanitizeGapComponent(Value.X, Fallback.X),
			SanitizeGapComponent(Value.Y, Fallback.Y));
	}

	static float SanitizeGapComponent(float Value, float Fallback)
	{
		if (FMath::IsFinite(Value))
		{
			return FMath::Max(0.0f, Value);
		}
		return FMath::IsFinite(Fallback) ? FMath::Max(0.0f, Fallback) : 0.0f;
	}

	FVector2D CurrentGap = FVector2D::ZeroVector;
	FVector2D TransitionStartGap = FVector2D::ZeroVector;
	float TransitionElapsedSeconds = 0.0f;
	float TransitionDurationSeconds = 0.0f;
	bool bTransitionActive = false;
};
