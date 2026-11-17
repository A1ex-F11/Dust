#pragma once

#include "CoreMinimal.h"
#include "Kismet/KismetMathLibrary.h"
#include "Presentation/WeaponViewmodelTypes.h"

/** Runtime state only. Rotation vectors use X=Roll, Y=Pitch, Z=Yaw. */
struct DUST2_API FWeaponSwayLayerState
{
	FVector TargetLocation = FVector::ZeroVector;
	FVector TargetRotation = FVector::ZeroVector;
	FVector Location = FVector::ZeroVector;
	FVector Rotation = FVector::ZeroVector;
	FFloatSpringState LocationSprings[3];
	FFloatSpringState RotationSprings[3];

	FTransform GetTransform() const
	{
		return FTransform(FRotator(Rotation.Y, Rotation.Z, Rotation.X), Location);
	}
};

/** Inputs are already sampled and axis-mapped by the caller, before authored Scale/ADS. */
struct DUST2_API FWeaponSwayRuntime
{
	FWeaponSwayLayerState Move;
	FWeaponSwayLayerState Aim;
	void Reset() { *this = FWeaponSwayRuntime{}; }
	bool ComputeMoveTarget(const FWeaponSwaySettings& Settings,
		const FVector& PositionInput, const FVector& RotationInput, bool bAiming, float DeltaSeconds);
	bool ComputeAimTarget(const FWeaponSwaySettings& Settings,
		const FVector& PositionDelta, const FVector& RotationDelta, bool bAiming, float DeltaSeconds);
	bool ComputeMove(const FWeaponSwaySettings& Settings, float DeltaSeconds);
	bool ComputeAim(const FWeaponSwaySettings& Settings, float DeltaSeconds);
};
