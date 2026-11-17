#pragma once

#include "CoreMinimal.h"

/**
 * Pure display-space state for the two-slot weapon loadout HUD.
 *
 * This header deliberately has no UMG, weapon, actor, or world dependency: the
 * widget owns the textures/names and feeds one stable brush id per inventory
 * slot. All time and parameter sanitizing lives here so the display math can be
 * tested without constructing a widget or a weapon actor.
 */
struct FWeaponLoadoutDisplaySettings
{
	FVector2D CurrentOffset = FVector2D::ZeroVector;
	FVector2D ReserveOffset = FVector2D(0.0f, -88.0f);
	float CurrentScale = 1.0f;
	float ReserveScale = 0.72f;
	float CurrentOpacity = 1.0f;
	float ReserveOpacity = 0.65f;
	float TransitionSeconds = 0.18f;

	/** Non-finite layout values fall back to the shipped defaults; negative duration becomes 0. */
	FWeaponLoadoutDisplaySettings GetSanitized() const
	{
		FWeaponLoadoutDisplaySettings Safe = *this;
		Safe.CurrentOffset = SanitizeVector(CurrentOffset, FVector2D::ZeroVector);
		Safe.ReserveOffset = SanitizeVector(ReserveOffset, FVector2D(0.0f, -88.0f));
		Safe.CurrentScale = FMath::Max(0.0f, SanitizeFinite(CurrentScale, 1.0f));
		Safe.ReserveScale = FMath::Max(0.0f, SanitizeFinite(ReserveScale, 0.72f));
		Safe.CurrentOpacity = SanitizeOpacity(CurrentOpacity, 1.0f);
		Safe.ReserveOpacity = SanitizeOpacity(ReserveOpacity, 0.65f);
		Safe.TransitionSeconds = FMath::IsFinite(TransitionSeconds)
			? FMath::Max(0.0f, TransitionSeconds)
			: 0.0f;
		return Safe;
	}

private:
	static float SanitizeFinite(float Value, float Fallback)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	static float SanitizeOpacity(float Value, float Fallback)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : Fallback;
	}

	static FVector2D SanitizeVector(const FVector2D& Value, const FVector2D& Fallback)
	{
		return FVector2D(
			SanitizeFinite(Value.X, Fallback.X),
			SanitizeFinite(Value.Y, Fallback.Y));
	}
};

struct FWeaponLoadoutPose
{
	FVector2D Offset = FVector2D::ZeroVector;
	float Scale = 1.0f;
	float Opacity = 1.0f;
};

/**
 * One fixed inventory-slot track. The visible brush id is the brush actually on
 * screen, which is what an interrupted replacement must continue from; the
 * target brush id is only committed at the midpoint of the two-phase swap.
 */
struct FWeaponLoadoutTrackState
{
	void Reset()
	{
		Pose = FWeaponLoadoutPose();
		Pose.Opacity = 0.0f;
		StartPose = Pose;
		TargetPose = Pose;
		VisibleBrushId = 0;
		TargetBrushId = 0;
		bHasVisibleBrush = false;
		bTargetPresent = false;
		bTargetCurrent = false;
		bInitialized = false;
		bAnimating = false;
		bBrushSwapPending = false;
		bBrushSwapApplied = false;
		ElapsedSeconds = 0.0f;
		DurationSeconds = 0.0f;
		SwapSeconds = 0.0f;
	}

	/**
	 * Feed one inventory notification. First use, bImmediate, or a non-positive
	 * duration lands directly on the target. A duplicate target keeps any running
	 * transition untouched. Any other call restarts from the current displayed
	 * pose and discards a not-yet-visible pending brush.
	 */
	void Update(
		uint64 InBrushId,
		bool bInPresent,
		bool bInIsCurrent,
		const FWeaponLoadoutDisplaySettings& InSettings,
		bool bImmediate)
	{
		const FWeaponLoadoutDisplaySettings Settings = InSettings.GetSanitized();
		const uint64 NewBrushId = bInPresent ? InBrushId : 0;

		const bool bDuplicateTarget = bInitialized
			&& bTargetPresent == bInPresent
			&& bTargetCurrent == bInIsCurrent
			&& TargetBrushId == NewBrushId;
		if (bDuplicateTarget && !bImmediate)
		{
			return;
		}

		FWeaponLoadoutPose NewTargetPose;
		if (bInPresent)
		{
			NewTargetPose = MakeRolePose(Settings, bInIsCurrent);
		}
		else
		{
			// A dropped weapon keeps its current position/scale while fading out.
			NewTargetPose.Offset = Pose.Offset;
			NewTargetPose.Scale = Pose.Scale;
			NewTargetPose.Opacity = 0.0f;
		}

		if (!bInitialized || bImmediate || Settings.TransitionSeconds <= 0.0f)
		{
			SnapTo(NewBrushId, bInPresent, bInIsCurrent, NewTargetPose);
			return;
		}

		TargetBrushId = NewBrushId;
		bTargetPresent = bInPresent;
		bTargetCurrent = bInIsCurrent;
		TargetPose = NewTargetPose;
		StartPose = Pose;
		ElapsedSeconds = 0.0f;
		DurationSeconds = Settings.TransitionSeconds;
		SwapSeconds = DurationSeconds * 0.5f;
		bAnimating = true;
		bBrushSwapPending = false;
		bBrushSwapApplied = false;

		if (bInPresent)
		{
			if (!bHasVisibleBrush)
			{
				// Nothing on screen to fade out: show the new brush and fade it in.
				VisibleBrushId = NewBrushId;
				bHasVisibleBrush = true;
			}
			else if (VisibleBrushId != NewBrushId)
			{
				bBrushSwapPending = true;
			}
		}
		// A null target keeps the cached visible brush until the fade-out finishes.
	}

	/** Advance active motion only; a settled track does no per-frame work. */
	void Advance(float DeltaSeconds)
	{
		if (!bAnimating)
		{
			return;
		}
		if (DurationSeconds <= 0.0f)
		{
			FinishTransition();
			return;
		}

		const float SafeDelta = FMath::IsFinite(DeltaSeconds)
			? FMath::Max(0.0f, DeltaSeconds)
			: 0.0f;
		ElapsedSeconds = FMath::Min(DurationSeconds, ElapsedSeconds + SafeDelta);

		const float MoveAlpha = SmoothStep(ElapsedSeconds / DurationSeconds);
		Pose.Offset = FMath::Lerp(StartPose.Offset, TargetPose.Offset, MoveAlpha);
		Pose.Scale = FMath::Lerp(StartPose.Scale, TargetPose.Scale, MoveAlpha);

		if (bBrushSwapPending)
		{
			if (!bBrushSwapApplied && ElapsedSeconds >= SwapSeconds)
			{
				// Commit the pending brush exactly at the invisible midpoint.
				Pose.Opacity = 0.0f;
				VisibleBrushId = TargetBrushId;
				bBrushSwapApplied = true;
			}

			if (bBrushSwapApplied)
			{
				const float FadeInDuration = FMath::Max(DurationSeconds - SwapSeconds, KINDA_SMALL_NUMBER);
				const float FadeInAlpha = SmoothStep((ElapsedSeconds - SwapSeconds) / FadeInDuration);
				Pose.Opacity = FMath::Lerp(0.0f, TargetPose.Opacity, FadeInAlpha);
			}
			else
			{
				const float FadeOutDuration = FMath::Max(SwapSeconds, KINDA_SMALL_NUMBER);
				const float FadeOutAlpha = SmoothStep(ElapsedSeconds / FadeOutDuration);
				Pose.Opacity = FMath::Lerp(StartPose.Opacity, 0.0f, FadeOutAlpha);
			}
		}
		else
		{
			Pose.Opacity = FMath::Lerp(StartPose.Opacity, TargetPose.Opacity, MoveAlpha);
		}

		if (ElapsedSeconds >= DurationSeconds)
		{
			FinishTransition();
		}
	}

	bool IsInitialized() const { return bInitialized; }
	bool IsAnimating() const { return bAnimating; }
	bool HasVisibleBrush() const { return bHasVisibleBrush; }
	uint64 GetVisibleBrushId() const { return bHasVisibleBrush ? VisibleBrushId : 0; }
	uint64 GetTargetBrushId() const { return TargetBrushId; }
	bool IsTargetPresent() const { return bTargetPresent; }
	bool IsTargetCurrent() const { return bTargetCurrent; }
	const FWeaponLoadoutPose& GetPose() const { return Pose; }

	float GetRemainingSeconds() const
	{
		return bAnimating ? FMath::Max(0.0f, DurationSeconds - ElapsedSeconds) : 0.0f;
	}

private:
	static FWeaponLoadoutPose MakeRolePose(
		const FWeaponLoadoutDisplaySettings& Settings,
		bool bCurrent)
	{
		FWeaponLoadoutPose RolePose;
		RolePose.Offset = bCurrent ? Settings.CurrentOffset : Settings.ReserveOffset;
		RolePose.Scale = bCurrent ? Settings.CurrentScale : Settings.ReserveScale;
		RolePose.Opacity = bCurrent ? Settings.CurrentOpacity : Settings.ReserveOpacity;
		return RolePose;
	}

	static float SmoothStep(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return Clamped * Clamped * (3.0f - 2.0f * Clamped);
	}

	void SnapTo(
		uint64 NewBrushId,
		bool bInPresent,
		bool bInIsCurrent,
		const FWeaponLoadoutPose& NewTargetPose)
	{
		bInitialized = true;
		bAnimating = false;
		bBrushSwapPending = false;
		bBrushSwapApplied = false;
		bTargetPresent = bInPresent;
		bTargetCurrent = bInIsCurrent;
		TargetBrushId = NewBrushId;
		Pose = NewTargetPose;
		StartPose = Pose;
		TargetPose = Pose;
		ElapsedSeconds = 0.0f;
		DurationSeconds = 0.0f;
		SwapSeconds = 0.0f;
		bHasVisibleBrush = bInPresent;
		VisibleBrushId = bInPresent ? NewBrushId : 0;
	}

	void FinishTransition()
	{
		Pose = TargetPose;
		bAnimating = false;
		bBrushSwapPending = false;
		bBrushSwapApplied = false;
		ElapsedSeconds = DurationSeconds;
		if (bTargetPresent)
		{
			VisibleBrushId = TargetBrushId;
			bHasVisibleBrush = true;
		}
		else
		{
			VisibleBrushId = 0;
			bHasVisibleBrush = false;
		}
	}

	FWeaponLoadoutPose Pose;
	FWeaponLoadoutPose StartPose;
	FWeaponLoadoutPose TargetPose;
	uint64 VisibleBrushId = 0;
	uint64 TargetBrushId = 0;
	bool bHasVisibleBrush = false;
	bool bTargetPresent = false;
	bool bTargetCurrent = false;
	bool bInitialized = false;
	bool bAnimating = false;
	bool bBrushSwapPending = false;
	bool bBrushSwapApplied = false;
	float ElapsedSeconds = 0.0f;
	float DurationSeconds = 0.0f;
	float SwapSeconds = 0.0f;
};

/** Fixed Slot1/Slot2 tracks; tracks never migrate or reuse each other's image. */
struct FWeaponLoadoutDisplayState
{
	static constexpr int32 TrackCount = 2;

	FWeaponLoadoutTrackState Tracks[TrackCount];

	void Reset()
	{
		for (FWeaponLoadoutTrackState& Track : Tracks)
		{
			Track.Reset();
		}
	}

	bool UpdateTrack(
		int32 SlotIndex,
		uint64 BrushId,
		bool bPresent,
		bool bIsCurrent,
		const FWeaponLoadoutDisplaySettings& Settings,
		bool bImmediate)
	{
		if (SlotIndex < 0 || SlotIndex >= TrackCount)
		{
			return false;
		}
		Tracks[SlotIndex].Update(BrushId, bPresent, bIsCurrent, Settings, bImmediate);
		return true;
	}

	void Advance(float DeltaSeconds)
	{
		for (FWeaponLoadoutTrackState& Track : Tracks)
		{
			Track.Advance(DeltaSeconds);
		}
	}

	bool IsAnimating() const
	{
		for (const FWeaponLoadoutTrackState& Track : Tracks)
		{
			if (Track.IsAnimating())
			{
				return true;
			}
		}
		return false;
	}
};
