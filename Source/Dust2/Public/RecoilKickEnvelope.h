#pragma once

#include "CoreMinimal.h"

// One accepted shot. Its integral is independent of frame subdivision.
struct FRecoilKickEnvelope
{
    FVector2D Total = FVector2D::ZeroVector;
    float Duration = 0.0f;
    float Elapsed = 0.0f;
    float AppliedFraction = 0.0f;
    bool bRecoverable = true;

    FVector2D Advance(float DeltaSeconds)
    {
        Elapsed = FMath::Min(Duration, Elapsed + FMath::Max(0.0f, DeltaSeconds));
        const float T = Duration > KINDA_SMALL_NUMBER ? Elapsed / Duration : 1.0f;
        const float Remaining = 1.0f - T;
        const float Fraction = 1.0f - Remaining * Remaining * Remaining;
        const FVector2D Delta = Total * (Fraction - AppliedFraction);
        AppliedFraction = Fraction;
        return Delta;
    }

    bool IsComplete() const { return Elapsed >= Duration; }
    FVector2D GetRemaining() const { return Total * (1.0f - AppliedFraction); }
};
