// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Math/NumericLimits.h"

/**
 * One conversion boundary for weapon cadence.
 *
 * Author-facing asset data stores rounds per minute (RPM). Runtime cooldown, AI/player
 * scheduling and accuracy profiles stay in seconds. Keeping the conversions here makes the
 * two representations agree and centralises the existing 0.001s anti-infinite-fire floor.
 */
namespace DustWeaponFireRate
{
	/** Existing runtime minimum: no accepted shot may schedule a refire faster than this. */
	inline constexpr float MinIntervalSeconds = 0.001f;

	/** A legacy seconds value is usable when it can represent a real interval. */
	inline bool IsFiniteNonNegative(float Seconds)
	{
		return FMath::IsFinite(Seconds) && Seconds >= 0.0f;
	}

	/** Only a finite, strictly positive RPM is an authored fire rate. Zero means "not configured". */
	inline bool IsValidRoundsPerMinute(float RoundsPerMinute)
	{
		return FMath::IsFinite(RoundsPerMinute) && RoundsPerMinute > 0.0f;
	}

	/** Invalid, negative and zero seconds fall back to the existing 0.001s floor. */
	inline float SanitizeIntervalSeconds(float Seconds)
	{
		return FMath::IsFinite(Seconds) ? FMath::Max(Seconds, MinIntervalSeconds) : MinIntervalSeconds;
	}

	/**
	 * Converts a valid RPM to seconds and applies the runtime floor. Invalid RPM returns 0 so
	 * the caller must choose the legacy fallback explicitly; it never fabricates a cadence.
	 */
	inline float RoundsPerMinuteToInterval(float RoundsPerMinute)
	{
		if (!IsValidRoundsPerMinute(RoundsPerMinute))
		{
			return 0.0f;
		}

		const double Seconds = 60.0 / static_cast<double>(RoundsPerMinute);
		return static_cast<float>(FMath::Clamp(
			Seconds,
			static_cast<double>(MinIntervalSeconds),
			static_cast<double>(TNumericLimits<float>::Max())));
	}

	/**
	 * Converts an already-resolved interval to its RPM representation. Values below the floor
	 * (including 0, negative, NaN and Infinity) use the floor, matching GetEffectiveFireInterval.
	 */
	inline float IntervalToRoundsPerMinute(float Seconds)
	{
		const float SafeSeconds = SanitizeIntervalSeconds(Seconds);
		return static_cast<float>(60.0 / static_cast<double>(SafeSeconds));
	}

	/** Single precedence for normal cadence: a valid RPM wins, otherwise the retained seconds value. */
	inline float ResolveNormalInterval(float RoundsPerMinute, float LegacySeconds)
	{
		return IsValidRoundsPerMinute(RoundsPerMinute)
			? RoundsPerMinuteToInterval(RoundsPerMinute)
			: SanitizeIntervalSeconds(LegacySeconds);
	}
}
