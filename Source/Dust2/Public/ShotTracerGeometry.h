#pragma once

#include "CoreMinimal.h"

// Cosmetic timing and segment geometry only; this never performs a damage trace.
struct FShotTracerGeometry
{
	static bool BuildFlight(const FVector& Start, const FVector& End,
		float Speed, float MaxDuration, float Length,
		float& OutDistance, float& OutDuration, float& OutLength)
	{
		OutDistance = OutDuration = OutLength = 0.0f;
		if (Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(Speed) ||
			!FMath::IsFinite(MaxDuration) || !FMath::IsFinite(Length) ||
			Speed <= 0.0f || MaxDuration <= 0.0f || Length <= 0.0f) return false;
		const double Distance = FVector::Distance(Start, End);
		if (!FMath::IsFinite(Distance) || Distance <= KINDA_SMALL_NUMBER || Distance > 1.0e7) return false;
		OutDistance = static_cast<float>(Distance);
		OutDuration = FMath::Clamp(OutDistance / Speed, 1.0f / 60.0f,
			FMath::Clamp(MaxDuration, 1.0f / 60.0f, 1.0f));
		OutLength = FMath::Min(Length, OutDistance);
		return true;
	}

	static FVector2D Segment(float Distance, float Duration, float Length, float Age)
	{
		if (!FMath::IsFinite(Distance) || !FMath::IsFinite(Duration) ||
			!FMath::IsFinite(Length) || !FMath::IsFinite(Age) ||
			Distance <= 0 || Duration <= 0 || Length <= 0) return FVector2D::ZeroVector;
		const float Head = Distance * FMath::Clamp(Age / Duration, 0.0f, 1.0f);
		const float Tail = FMath::Max(0.0f, Head - Length);
		return FVector2D((Head + Tail) * 0.5f, Head - Tail);
	}
};
