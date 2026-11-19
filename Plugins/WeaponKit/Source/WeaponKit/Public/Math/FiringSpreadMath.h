#pragma once

#include "CoreMinimal.h"

// Stateless spread accumulation. Each weapon owns its running total; definition assets stay read-only.
struct FWeaponKitFiringSpreadMath
{
	static float Safe(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	static float Recover(float Current, float Elapsed, float Delay, float Speed, float Maximum)
	{
		return FMath::Max(0.0f, FMath::Min(Safe(Current), Safe(Maximum)) -
			FMath::Max(0.0f, Safe(Elapsed) - Safe(Delay)) * Safe(Speed));
	}

	static float AddShot(float Current, float PerShot, float Maximum)
	{
		if (!FMath::IsFinite(Current) || !FMath::IsFinite(PerShot) || !FMath::IsFinite(Maximum))
		{
			return 0.0f;
		}
		return FMath::Min(Safe(Maximum), Safe(Current) + Safe(PerShot));
	}
};
