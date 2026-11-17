#pragma once

#include "CoreMinimal.h"

/**
 * Presentation-only hit region. The gameplay side confirms the region; this
 * header never inspects a hit result, a bone, an actor or a health component.
 */
enum class EDustCombatHitRegion : uint8
{
	Body,
	Head
};

/** The three confirmed right-upper feed events. Ordinary hits never create a row. */
enum class EDustCombatFeedEvent : uint8
{
	BodyKill,
	HeadshotKill,
	PlayerKilled
};

/** Procedural icon family, so the feed needs no texture or emoji fallback. */
enum class EDustCombatFeedIcon : uint8
{
	Skull,
	HeadshotSkull
};

/**
 * Pure timing/selection values for the confirmed-combat overlay.
 *
 * Every duration is finite and bounded. A zero duration means "do not show that
 * part"; it is not a division by zero, because the render code only reads alpha
 * while the matching remaining time is positive.
 */
struct FDustCombatFeedbackTuning
{
	/** Upper bound for any duration, so bad data cannot pin feedback on forever. */
	static constexpr float MaxTimerSeconds = 10.0f;

	/** Confirmed positive damage keeps the marker on screen for this long. */
	float MarkerFlashSeconds = 0.12f;

	/** A killing hit adds the independent skull for this long. */
	float SkullSeconds = 0.30f;

	/** Existing feed rows slide to their new slot in this long. */
	float FeedShiftSeconds = 0.15f;

	FDustCombatFeedbackTuning Sanitized() const
	{
		const FDustCombatFeedbackTuning Defaults;
		FDustCombatFeedbackTuning Result;
		Result.MarkerFlashSeconds = SanitizeSeconds(MarkerFlashSeconds, Defaults.MarkerFlashSeconds);
		Result.SkullSeconds = SanitizeSeconds(SkullSeconds, Defaults.SkullSeconds);
		Result.FeedShiftSeconds = SanitizeSeconds(FeedShiftSeconds, Defaults.FeedShiftSeconds);
		return Result;
	}

	static float SanitizeSeconds(float Value, float Fallback)
	{
		if (!FMath::IsFinite(Value))
		{
			return FMath::Clamp(Fallback, 0.0f, MaxTimerSeconds);
		}
		return FMath::Clamp(Value, 0.0f, MaxTimerSeconds);
	}
};

/** Color selection for the center marker. Kill state deliberately does not participate. */
struct FDustCombatHitSelection
{
	EDustCombatHitRegion Region = EDustCombatHitRegion::Body;
	FLinearColor Color = FLinearColor::White;

	static FDustCombatHitSelection Resolve(
		bool bHeadshot,
		const FLinearColor& BodyColor,
		const FLinearColor& HeadColor)
	{
		FDustCombatHitSelection Result;
		Result.Region = bHeadshot ? EDustCombatHitRegion::Head : EDustCombatHitRegion::Body;
		Result.Color = bHeadshot ? HeadColor : BodyColor;
		return Result;
	}
};

/**
 * Independent center feedback timers.
 *
 * Marker and skull are separate clocks on purpose: an ordinary follow-up hit may
 * refresh the white/red marker, but it must not shorten or recolor the skull that
 * belongs to the previous killing blow.
 */
struct FDustCombatCenterFeedbackState
{
	void Reset()
	{
		MarkerRemainingSeconds = 0.0f;
		MarkerDurationSeconds = 0.0f;
		SkullRemainingSeconds = 0.0f;
		SkullDurationSeconds = 0.0f;
		bMarkerHeadshot = false;
		bSkullHeadshot = false;
	}

	/** Returns true only for a finite, strictly positive confirmed damage value. */
	bool NotifyHit(
		float ActualDamage,
		bool bHeadshot,
		bool bKilled,
		const FDustCombatFeedbackTuning& Tuning)
	{
		if (!FMath::IsFinite(ActualDamage) || ActualDamage <= 0.0f)
		{
			return false;
		}

		const FDustCombatFeedbackTuning Safe = Tuning.Sanitized();
		bMarkerHeadshot = bHeadshot;
		if (Safe.MarkerFlashSeconds > 0.0f)
		{
			MarkerDurationSeconds = Safe.MarkerFlashSeconds;
			MarkerRemainingSeconds = Safe.MarkerFlashSeconds;
		}
		else
		{
			MarkerDurationSeconds = 0.0f;
			MarkerRemainingSeconds = 0.0f;
		}

		if (bKilled)
		{
			bSkullHeadshot = bHeadshot;
			if (Safe.SkullSeconds > 0.0f)
			{
				SkullDurationSeconds = Safe.SkullSeconds;
				SkullRemainingSeconds = Safe.SkullSeconds;
			}
			else
			{
				SkullDurationSeconds = 0.0f;
				SkullRemainingSeconds = 0.0f;
			}
		}

		return true;
	}

	/** Invalid, negative and zero deltas are ignored; timers never go negative. */
	void Advance(float DeltaSeconds)
	{
		if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
		{
			return;
		}

		if (MarkerRemainingSeconds > 0.0f)
		{
			MarkerRemainingSeconds = FMath::Max(0.0f, MarkerRemainingSeconds - DeltaSeconds);
			if (MarkerRemainingSeconds <= 0.0f)
			{
				MarkerDurationSeconds = 0.0f;
			}
		}

		if (SkullRemainingSeconds > 0.0f)
		{
			SkullRemainingSeconds = FMath::Max(0.0f, SkullRemainingSeconds - DeltaSeconds);
			if (SkullRemainingSeconds <= 0.0f)
			{
				SkullDurationSeconds = 0.0f;
			}
		}
	}

	bool IsMarkerActive() const
	{
		return MarkerRemainingSeconds > 0.0f && MarkerDurationSeconds > 0.0f;
	}

	bool IsSkullActive() const
	{
		return SkullRemainingSeconds > 0.0f && SkullDurationSeconds > 0.0f;
	}

	float GetMarkerAlpha() const
	{
		return IsMarkerActive()
			? MarkerRemainingSeconds / MarkerDurationSeconds
			: 0.0f;
	}

	float GetSkullAlpha() const
	{
		return IsSkullActive()
			? SkullRemainingSeconds / SkullDurationSeconds
			: 0.0f;
	}

	float GetMarkerRemainingSeconds() const { return MarkerRemainingSeconds; }
	float GetSkullRemainingSeconds() const { return SkullRemainingSeconds; }
	bool IsMarkerHeadshot() const { return bMarkerHeadshot; }
	bool IsSkullHeadshot() const { return bSkullHeadshot; }

private:
	float MarkerRemainingSeconds = 0.0f;
	float MarkerDurationSeconds = 0.0f;
	float SkullRemainingSeconds = 0.0f;
	float SkullDurationSeconds = 0.0f;
	bool bMarkerHeadshot = false;
	bool bSkullHeadshot = false;
};

/** One feed entry. AnimatedIndex is a row slot, not a pixel offset, so layout stays reusable. */
struct FDustCombatFeedRow
{
	EDustCombatFeedEvent Event = EDustCombatFeedEvent::BodyKill;
	float AnimatedIndex = 0.0f;
	float ShiftStartIndex = 0.0f;
	float ShiftElapsedSeconds = 0.0f;
	float AgeSeconds = 0.0f;
};

/**
 * Newest is index 0; each row expires independently after three seconds.
 * Pushing a sixth entry discards the oldest without extending any lifetime.
 */
struct FDustCombatFeedState
{
	/** Hard maximum approved by the product contract. */
	static constexpr int32 MaxRows = 5;
	static constexpr float RowLifetimeSeconds = 3.0f;

	void Reset()
	{
		Rows.Reset();
	}

	void Push(EDustCombatFeedEvent Event, const FDustCombatFeedbackTuning& Tuning)
	{
		const float ShiftSeconds = Tuning.Sanitized().FeedShiftSeconds;

		// Existing rows keep their current animated position and become one slot lower.
		for (FDustCombatFeedRow& Row : Rows)
		{
			Row.ShiftStartIndex = Row.AnimatedIndex;
			Row.ShiftElapsedSeconds = 0.0f;
		}

		FDustCombatFeedRow NewRow;
		NewRow.Event = Event;
		NewRow.AnimatedIndex = 0.0f;
		NewRow.ShiftStartIndex = 0.0f;
		NewRow.ShiftElapsedSeconds = 0.0f;
		Rows.Insert(NewRow, 0);

		while (Rows.Num() > MaxRows)
		{
			Rows.RemoveAt(Rows.Num() - 1, 1, EAllowShrinking::No);
		}

		if (ShiftSeconds <= 0.0f)
		{
			SnapToTargets();
		}
	}

	void Advance(float DeltaSeconds, const FDustCombatFeedbackTuning& Tuning)
	{
		if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
		{
			return;
		}

		bool bRemovedRows = false;
		for (int32 Index = Rows.Num() - 1; Index >= 0; --Index)
		{
			Rows[Index].AgeSeconds = FMath::Min(RowLifetimeSeconds, Rows[Index].AgeSeconds + DeltaSeconds);
			if (Rows[Index].AgeSeconds >= RowLifetimeSeconds)
			{
				Rows.RemoveAt(Index, 1, EAllowShrinking::No);
				bRemovedRows = true;
			}
		}
		if (bRemovedRows)
		{
			for (FDustCombatFeedRow& Row : Rows)
			{
				Row.ShiftStartIndex = Row.AnimatedIndex;
				Row.ShiftElapsedSeconds = 0.0f;
			}
		}

		const float ShiftSeconds = Tuning.Sanitized().FeedShiftSeconds;
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			FDustCombatFeedRow& Row = Rows[Index];
			const float TargetIndex = static_cast<float>(Index);
			if (ShiftSeconds <= 0.0f)
			{
				Row.AnimatedIndex = TargetIndex;
				Row.ShiftStartIndex = TargetIndex;
				Row.ShiftElapsedSeconds = 0.0f;
				continue;
			}

			Row.ShiftElapsedSeconds = FMath::Min(
				ShiftSeconds,
				Row.ShiftElapsedSeconds + DeltaSeconds);

			if (Row.ShiftElapsedSeconds >= ShiftSeconds)
			{
				Row.AnimatedIndex = TargetIndex;
			}
			else
			{
				const float Alpha = SmoothStep(Row.ShiftElapsedSeconds / ShiftSeconds);
				Row.AnimatedIndex = FMath::Lerp(Row.ShiftStartIndex, TargetIndex, Alpha);
			}
		}
	}

	bool IsAnimating() const
	{
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			if (!FMath::IsNearlyEqual(Rows[Index].AnimatedIndex, static_cast<float>(Index), KINDA_SMALL_NUMBER))
			{
				return true;
			}
		}
		return false;
	}

	int32 Num() const { return Rows.Num(); }
	const TArray<FDustCombatFeedRow>& GetRows() const { return Rows; }

private:
	static float SmoothStep(float Alpha)
	{
		const float SafeAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return SafeAlpha * SafeAlpha * (3.0f - 2.0f * SafeAlpha);
	}

	void SnapToTargets()
	{
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			Rows[Index].AnimatedIndex = static_cast<float>(Index);
			Rows[Index].ShiftStartIndex = static_cast<float>(Index);
			Rows[Index].ShiftElapsedSeconds = 0.0f;
		}
	}

	TArray<FDustCombatFeedRow> Rows;
};

/** One small coordinator so the widget and tests drive exactly the same state machine. */
struct FDustCombatFeedbackState
{
	FDustCombatFeedbackTuning Tuning = FDustCombatFeedbackTuning().Sanitized();
	FDustCombatCenterFeedbackState Center;
	FDustCombatFeedState Feed;

	void SetTuning(const FDustCombatFeedbackTuning& InTuning)
	{
		Tuning = InTuning.Sanitized();
	}

	void Reset()
	{
		Center.Reset();
		Feed.Reset();
	}

	/** A killing notification adds exactly one kill row; a non-lethal one adds none. */
	bool NotifyConfirmedHit(float ActualDamage, bool bHeadshot, bool bKilled)
	{
		if (!Center.NotifyHit(ActualDamage, bHeadshot, bKilled, Tuning))
		{
			return false;
		}

		if (bKilled)
		{
			Feed.Push(
				bHeadshot ? EDustCombatFeedEvent::HeadshotKill : EDustCombatFeedEvent::BodyKill,
				Tuning);
		}
		return true;
	}

	/** Player death clears both center clocks and retains/extends the feed history. */
	void NotifyPlayerKilled()
	{
		Center.Reset();
		Feed.Push(EDustCombatFeedEvent::PlayerKilled, Tuning);
	}

	void Advance(float DeltaSeconds)
	{
		Center.Advance(DeltaSeconds);
		Feed.Advance(DeltaSeconds, Tuning);
	}
};

inline FString GetDustCombatFeedText(EDustCombatFeedEvent Event)
{
	switch (Event)
	{
	case EDustCombatFeedEvent::BodyKill:
		return FString(TEXT("你击杀了一个敌人"));
	case EDustCombatFeedEvent::HeadshotKill:
		return FString(TEXT("你爆头击杀了一名敌人"));
	case EDustCombatFeedEvent::PlayerKilled:
		return FString(TEXT("你被敌人击杀了"));
	default:
		return FString();
	}
}

/** Resolved row metadata. Colors are passed in so UMG properties stay authoritative. */
struct FDustCombatFeedPresentation
{
	EDustCombatFeedIcon Icon = EDustCombatFeedIcon::Skull;
	FLinearColor Accent = FLinearColor::White;
	FString Text;
};

inline FDustCombatFeedPresentation ResolveDustCombatFeedPresentation(
	EDustCombatFeedEvent Event,
	const FLinearColor& BodyKillAccent,
	const FLinearColor& HeadshotKillAccent,
	const FLinearColor& PlayerKilledAccent)
{
	FDustCombatFeedPresentation Result;
	Result.Text = GetDustCombatFeedText(Event);

	switch (Event)
	{
	case EDustCombatFeedEvent::BodyKill:
		Result.Icon = EDustCombatFeedIcon::Skull;
		Result.Accent = BodyKillAccent;
		break;
	case EDustCombatFeedEvent::HeadshotKill:
		Result.Icon = EDustCombatFeedIcon::HeadshotSkull;
		Result.Accent = HeadshotKillAccent;
		break;
	case EDustCombatFeedEvent::PlayerKilled:
	default:
		Result.Icon = EDustCombatFeedIcon::Skull;
		Result.Accent = PlayerKilledAccent;
		break;
	}

	return Result;
}

/** One tapered diagonal arm. InnerHalfThickness is guaranteed not to exceed the outer half. */
struct FDustCombatTaperArm
{
	FVector2D Inner = FVector2D::ZeroVector;
	FVector2D Outer = FVector2D::ZeroVector;
	FVector2D InnerNormal = FVector2D(0.0f, -1.0f);
	float InnerHalfThickness = 0.0f;
	float OuterHalfThickness = 0.0f;

	FVector2D GetDirection() const { return (Outer - Inner).GetSafeNormal(); }
	float GetLength() const { return FVector2D::Distance(Inner, Outer); }

	/** CCW quad: inner+normal, outer+normal, outer-normal, inner-normal. */
	void GetQuad(FVector2D OutQuad[4]) const
	{
		const FVector2D Normal = InnerNormal.GetSafeNormal();
		OutQuad[0] = Inner + Normal * InnerHalfThickness;
		OutQuad[1] = Outer + Normal * OuterHalfThickness;
		OutQuad[2] = Outer - Normal * OuterHalfThickness;
		OutQuad[3] = Inner - Normal * InnerHalfThickness;
	}
};

/**
 * Four-arm taper builder at 45/135/225/315 degrees.
 *
 * The inner point follows the crosshair's elliptical X/Y gap, the outer point is
 * Inner + Direction * Length, so changing the gap translates an arm without
 * changing its length or thickness. All outputs are finite and bounded.
 */
struct FDustCombatTaperGeometry
{
	static constexpr int32 ArmCount = 4;
	static constexpr float MaxCoordinate = 100000.0f;

	static int32 WrapArmIndex(int32 Index)
	{
		return ((Index % ArmCount) + ArmCount) % ArmCount;
	}

	static float GetArmAngleDegrees(int32 Index)
	{
		return 45.0f + 90.0f * static_cast<float>(WrapArmIndex(Index));
	}

	static FVector2D GetArmDirection(int32 Index)
	{
		const float Radians = FMath::DegreesToRadians(GetArmAngleDegrees(Index));
		return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
	}

	static float SanitizeNonNegative(float Value, float Fallback = 0.0f)
	{
		if (!FMath::IsFinite(Value))
		{
			return FMath::Clamp(Fallback, 0.0f, MaxCoordinate);
		}
		return FMath::Clamp(Value, 0.0f, MaxCoordinate);
	}

	static FDustCombatTaperArm MakeArm(
		int32 Index,
		const FVector2D& Gap,
		float Length,
		float InnerThickness,
		float OuterThickness)
	{
		FDustCombatTaperArm Arm;
		const FVector2D SafeGap(
			SanitizeNonNegative(Gap.X),
			SanitizeNonNegative(Gap.Y));
		const float SafeLength = SanitizeNonNegative(Length);
		const float SafeInner = SanitizeNonNegative(InnerThickness);
		const float SafeOuter = SanitizeNonNegative(OuterThickness);
		const float Thin = FMath::Min(SafeInner, SafeOuter);
		const float Thick = FMath::Max(SafeInner, SafeOuter);
		const FVector2D Direction = GetArmDirection(Index);

		// Elliptical inner point: X gap controls horizontal reach, Y gap vertical.
		Arm.Inner = FVector2D(SafeGap.X * Direction.X, SafeGap.Y * Direction.Y);
		Arm.Outer = Arm.Inner + Direction * SafeLength;
		Arm.InnerNormal = FVector2D(-Direction.Y, Direction.X);
		Arm.InnerHalfThickness = Thin * 0.5f;
		Arm.OuterHalfThickness = Thick * 0.5f;
		return Arm;
	}

	static void MakeArms(
		const FVector2D& Gap,
		float Length,
		float InnerThickness,
		float OuterThickness,
		FDustCombatTaperArm* OutArms)
	{
		if (!OutArms)
		{
			return;
		}

		for (int32 Index = 0; Index < ArmCount; ++Index)
		{
			OutArms[Index] = MakeArm(Index, Gap, Length, InnerThickness, OuterThickness);
		}
	}
};
