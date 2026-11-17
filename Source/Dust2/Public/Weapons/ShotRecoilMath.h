#pragma once
#include "CoreMinimal.h"

struct FShotRecoilMath
{
	static FVector2D Recover(FVector2D Offset, float QuietTime, float Delay, float Speed)
	{
		const float Time = FMath::Max(0.0f, QuietTime - Delay);
		return FVector2D(FMath::FInterpConstantTo(Offset.X, 0.0, Time, Speed),
			FMath::FInterpConstantTo(Offset.Y, 0.0, Time, Speed));
	}
	static FVector Apply(const FVector& Base, const FVector2D& Offset)
	{
		if (Base.ContainsNaN() || Offset.ContainsNaN() || Base.IsNearlyZero()) return FVector::ZeroVector;
		FRotator Aim = Base.Rotation();
		Aim.Pitch = FMath::Clamp(Aim.Pitch + Offset.X, -89.0, 89.0);
		Aim.Yaw += Offset.Y;
		return Aim.Vector().GetSafeNormal();
	}
};
