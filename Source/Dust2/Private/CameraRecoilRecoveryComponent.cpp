#include "CameraRecoilRecoveryComponent.h"
#include "WeaponRecoilData.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Camera/CameraComponent.h"

namespace
{
float SafeNonNegative(float Value, float Fallback = 0.0f)
{
    return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : Fallback;
}
}

UCameraRecoilRecoveryComponent::UCameraRecoilRecoveryComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    MaxPitchOffset = 3.0f;
    MaxYawOffset = 0.5f;
    RecoveryDelay = 0.20f;
    RecoverySpeed = 1.0f;
    RecoveryInterval = 0.02f;
    RecoveryPitchRatio = 0.35f;
}

void UCameraRecoilRecoveryComponent::ApplyRecoilKick(float PitchKick, float YawKick)
{
    ClearIndependentVisual();
    ActiveRecoveryDelay = SafeNonNegative(RecoveryDelay);
    ActiveRecoverySpeed = SafeNonNegative(RecoverySpeed);
    ActiveRecoveryRatio = FMath::Clamp(RecoveryPitchRatio, 0.0f, 1.0f);
    QueueKick(PitchKick, YawKick, 0.0f, 1.0f, true);
}

void UCameraRecoilRecoveryComponent::ApplyRecoilKickWithData(
    float PitchKick, float YawKick, UWeaponRecoilData* RecoilSettings)
{
    if (!IsValid(RecoilSettings))
    {
        return;
    }
    // The old node must not write ControllerRotation for independent ballistics.
    if (RecoilSettings->bUseFixedPattern && RecoilSettings->bUseIndependentShotRecoil) return;
    ClearIndependentVisual();
    // First rollout is opt-in per weapon: other guns retain the old application path.
    if (!RecoilSettings->bUseFixedPattern)
    {
        ApplyRecoilKick(PitchKick, YawKick);
        return;
    }
    ActiveRecoveryDelay = SafeNonNegative(RecoilSettings->RecoveryDelay);
    ActiveRecoverySpeed = SafeNonNegative(RecoilSettings->RecoverySpeed);
    ActiveRecoveryRatio = FMath::IsFinite(RecoilSettings->RecoveryPitchRatio)
        ? FMath::Clamp(RecoilSettings->RecoveryPitchRatio, 0.0f, 1.0f) : 0.0f;
    QueueKick(PitchKick, YawKick,
        SafeNonNegative(RecoilSettings->GetSmoothKickDuration()),
        FMath::IsFinite(RecoilSettings->GetImmediateKickFraction())
            ? FMath::Clamp(RecoilSettings->GetImmediateKickFraction(), 0.0f, 1.0f) : 1.0f,
        !RecoilSettings->bUseFixedPattern);
}

void UCameraRecoilRecoveryComponent::QueueKick(float PitchKick, float YawKick,
    float Duration, float ImmediateFraction, bool bUseLegacyLimits)
{
    APawn* PawnOwner = Cast<APawn>(GetOwner());
    UWorld* World = GetWorld();
    if (!PawnOwner || !PawnOwner->GetController() || !World ||
        !FMath::IsFinite(PitchKick) || !FMath::IsFinite(YawKick))
    {
        return;
    }
    StopRecoveryTimer();
    bRecoveryStarted = false;
    bRecoverImmediately = false;
    FVector2D Accepted(FMath::Max(0.0f, PitchKick), YawKick);
    if (bUseLegacyLimits)
    {
        FVector2D Reserved(AccumulatedPitch, AccumulatedYaw);
        for (const FRecoilKickEnvelope& Kick : PendingKicks)
        {
            Reserved += Kick.GetRemaining();
        }
        const float PitchLimit = SafeNonNegative(MaxPitchOffset);
        const float YawLimit = SafeNonNegative(MaxYawOffset);
        Accepted.X = FMath::Clamp(Reserved.X + Accepted.X, 0.0f, PitchLimit) - Reserved.X;
        Accepted.Y = FMath::Clamp(Reserved.Y + Accepted.Y, -YawLimit, YawLimit) - Reserved.Y;
    }
    LastKickTime = World->GetTimeSeconds();
    if (Duration <= KINDA_SMALL_NUMBER)
    {
        ApplyDelta(Accepted, true);
    }
    else
    {
        ApplyDelta(Accepted * ImmediateFraction, true);
        FRecoilKickEnvelope Tail;
        Tail.Total = Accepted * (1.0f - ImmediateFraction);
        Tail.Duration = Duration;
        if (!Tail.Total.IsNearlyZero())
        {
            PendingKicks.Add(Tail);
            SetComponentTickEnabled(true);
        }
    }
    StartRecoveryTimer();
}

void UCameraRecoilRecoveryComponent::ApplyDelta(const FVector2D& Delta, bool bRecoverable)
{
    APawn* PawnOwner = Cast<APawn>(GetOwner());
    if (!PawnOwner || !PawnOwner->GetController())
    {
        return;
    }
    if (bAimingRecoilMode)
    {
        // ADS recoil changes the same view sampled by the next shot, exactly once.
        FRotator View = PawnOwner->GetController()->GetControlRotation();
        View.Pitch = FMath::ClampAngle(View.Pitch + Delta.X, -89.0f, 89.0f);
        View.Yaw += Delta.Y;
        PawnOwner->GetController()->SetControlRotation(View);
    }
    else
    {
        PawnOwner->AddControllerPitchInput(-Delta.X);
        PawnOwner->AddControllerYawInput(Delta.Y);
    }
    if (bRecoverable)
    {
        AccumulatedPitch += Delta.X;
        AccumulatedYaw += Delta.Y;
    }
}

void UCameraRecoilRecoveryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bIndependentVisualMode)
    {
        if (!VisualCamera.IsValid() || !FMath::IsFinite(DeltaTime))
        {
            ClearRecoil();
            return;
        }
        const float Dt = FMath::Max(0.0f, DeltaTime);
        for (int32 Index = VisualKicks.Num() - 1; Index >= 0; --Index)
        {
            VisualOffset += VisualKicks[Index].Advance(Dt);
            if (VisualKicks[Index].IsComplete()) VisualKicks.RemoveAt(Index);
        }
        VisualOffset *= FMath::Exp(-VisualReturnSpeed * Dt);
        if (VisualKicks.IsEmpty() && VisualOffset.IsNearlyZero(0.0001))
        {
            VisualOffset = FVector2D::ZeroVector;
            SetComponentTickEnabled(false);
        }
        WriteVisualOffset();
        return;
    }
    APawn* PawnOwner = Cast<APawn>(GetOwner());
    if (!PawnOwner || !PawnOwner->GetController() || !FMath::IsFinite(DeltaTime))
    {
        ClearRecoil();
        return;
    }
    for (int32 Index = PendingKicks.Num() - 1; Index >= 0; --Index)
    {
        FRecoilKickEnvelope& Kick = PendingKicks[Index];
        ApplyDelta(Kick.Advance(DeltaTime), Kick.bRecoverable);
        if (Kick.IsComplete())
        {
            PendingKicks.RemoveAt(Index);
        }
    }
    if (PendingKicks.IsEmpty())
    {
        SetComponentTickEnabled(false);
    }
}

void UCameraRecoilRecoveryComponent::StartRecoveryTimer()
{
    if (PendingKicks.IsEmpty() && FMath::IsNearlyZero(AccumulatedPitch) &&
        FMath::IsNearlyZero(AccumulatedYaw))
    {
        StopRecoveryTimer();
        return;
    }
    if (UWorld* World = GetWorld())
    {
        const float Interval = FMath::Max(0.001f, SafeNonNegative(RecoveryInterval, 0.02f));
        World->GetTimerManager().SetTimer(RecoveryTimerHandle, this,
            &UCameraRecoilRecoveryComponent::RecoveryStep,
            bRecoverImmediately ? Interval : FMath::Max(0.001f, ActiveRecoveryDelay), false);
    }
}

void UCameraRecoilRecoveryComponent::StopRecoveryTimer()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RecoveryTimerHandle);
    }
}

void UCameraRecoilRecoveryComponent::RecoveryStep()
{
    UWorld* World = GetWorld();
    APawn* PawnOwner = Cast<APawn>(GetOwner());
    if (!World || !PawnOwner || !PawnOwner->GetController())
    {
        ClearRecoil();
        return;
    }
    const float Interval = FMath::Max(0.001f, SafeNonNegative(RecoveryInterval, 0.02f));
    // Do not recover a shot while its accepted impulse is still being applied.
    const float QuietRemaining = ActiveRecoveryDelay - (World->GetTimeSeconds() - LastKickTime);
    if (!PendingKicks.IsEmpty() || (!bRecoveryStarted && !bRecoverImmediately && QuietRemaining > KINDA_SMALL_NUMBER))
    {
        World->GetTimerManager().SetTimer(RecoveryTimerHandle, this,
            &UCameraRecoilRecoveryComponent::RecoveryStep,
            FMath::Max(Interval, bRecoverImmediately ? Interval : QuietRemaining), false);
        return;
    }
    if (!bRecoveryStarted)
    {
        bRecoveryStarted = true;
        bRecoverImmediately = false;
        AccumulatedPitch *= ActiveRecoveryRatio;
        AccumulatedYaw = 0.0f;
        if (FMath::IsNearlyZero(AccumulatedPitch))
        {
            StopRecoveryTimer();
            return;
        }
        World->GetTimerManager().SetTimer(RecoveryTimerHandle, this,
            &UCameraRecoilRecoveryComponent::RecoveryStep, Interval, true);
    }
    const float PitchStep = FMath::Min(AccumulatedPitch, ActiveRecoverySpeed * Interval);
    if (PitchStep <= KINDA_SMALL_NUMBER)
    {
        NotifyManualLook();
        return;
    }
    if (bAimingRecoilMode)
    {
        FRotator View = PawnOwner->GetController()->GetControlRotation();
        View.Pitch = FMath::ClampAngle(View.Pitch - PitchStep, -89.0f, 89.0f);
        PawnOwner->GetController()->SetControlRotation(View);
    }
    else
    {
        PawnOwner->AddControllerPitchInput(PitchStep);
    }
    AccumulatedPitch -= PitchStep;
    if (FMath::IsNearlyZero(AccumulatedPitch))
    {
        StopRecoveryTimer();
        AccumulatedPitch = 0.0f;
    }
}

void UCameraRecoilRecoveryComponent::StartRecovery()
{
    if (bIndependentVisualMode) return; // View-only feedback already settles every frame.
    StopRecoveryTimer();
    bRecoveryStarted = false;
    bRecoverImmediately = true;
    StartRecoveryTimer();
}

void UCameraRecoilRecoveryComponent::NotifyManualLook()
{
    if (bIndependentVisualMode) return; // Mouse aim never owns this render-only offset.
    StopRecoveryTimer();
    AccumulatedPitch = 0.0f;
    AccumulatedYaw = 0.0f;
    bRecoveryStarted = false;
    bRecoverImmediately = false;
    for (FRecoilKickEnvelope& Kick : PendingKicks)
    {
        Kick.bRecoverable = false;
    }
}

void UCameraRecoilRecoveryComponent::ClearRecoil()
{
    ClearIndependentVisual();
    NotifyManualLook();
    PendingKicks.Empty();
    LastKickTime = 0.0f;
    SetComponentTickEnabled(false);
}

void UCameraRecoilRecoveryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearRecoil();
    Super::EndPlay(EndPlayReason);
}

void UCameraRecoilRecoveryComponent::ApplyRecoilWithCamera(float PitchKick, float YawKick,
    UWeaponRecoilData* RecoilSettings, UCameraComponent* RecoilCamera)
{
    if (!IsValid(RecoilSettings)) return;
    if (bAimingRecoilMode)
    {
        if (!FMath::IsFinite(PitchKick) || !FMath::IsFinite(YawKick)) return;
        ClearIndependentVisual();
        ActiveRecoveryDelay = SafeNonNegative(RecoilSettings->RecoveryDelay);
        ActiveRecoverySpeed = SafeNonNegative(RecoilSettings->ShotRecoilRecoverySpeed);
        ActiveRecoveryRatio = FMath::IsFinite(RecoilSettings->RecoveryPitchRatio)
            ? FMath::Clamp(RecoilSettings->RecoveryPitchRatio, 0.0f, 1.0f) : 0.0f;
        const float Scale = SafeNonNegative(RecoilSettings->AimingRecoilScale);
        QueueKick(PitchKick * Scale, YawKick * Scale,
            SafeNonNegative(RecoilSettings->GetSmoothKickDuration()),
            FMath::IsFinite(RecoilSettings->GetImmediateKickFraction())
                ? FMath::Clamp(RecoilSettings->GetImmediateKickFraction(), 0.0f, 1.0f) : 1.0f,
            false);
        return;
    }
    if (!RecoilSettings->bUseFixedPattern || !RecoilSettings->bUseIndependentShotRecoil)
    {
        ApplyRecoilKickWithData(PitchKick, YawKick, RecoilSettings);
        return;
    }
    if (!IsValid(RecoilCamera) || !FMath::IsFinite(PitchKick) || !FMath::IsFinite(YawKick)) return;
    if (!bIndependentVisualMode || VisualCamera.Get() != RecoilCamera)
    {
        ClearRecoil();
        bIndependentVisualMode = true;
        VisualCamera = RecoilCamera;
    }
    const float Scale = FMath::IsFinite(RecoilSettings->CameraFeedbackScale)
        ? FMath::Max(0.0f, RecoilSettings->CameraFeedbackScale) : 0.0f;
    VisualReturnSpeed = FMath::Max(0.1f, SafeNonNegative(RecoilSettings->GetCameraReturnSpeed(), 12.0f));
    const FVector2D Kick(FMath::Max(0.0f, PitchKick) * Scale, YawKick * Scale);
    const float Duration = SafeNonNegative(RecoilSettings->GetSmoothKickDuration());
    const float Immediate = FMath::IsFinite(RecoilSettings->GetImmediateKickFraction())
        ? FMath::Clamp(RecoilSettings->GetImmediateKickFraction(), 0.0f, 1.0f) : 1.0f;
    VisualOffset += Duration <= KINDA_SMALL_NUMBER ? Kick : Kick * Immediate;
    if (Duration > KINDA_SMALL_NUMBER)
    {
        FRecoilKickEnvelope Tail;
        Tail.Total = Kick * (1.0f - Immediate);
        Tail.Duration = Duration;
        if (!Tail.Total.IsNearlyZero()) VisualKicks.Add(Tail);
    }
    WriteVisualOffset();
    if (!VisualKicks.IsEmpty() || !VisualOffset.IsNearlyZero()) SetComponentTickEnabled(true);
}

void UCameraRecoilRecoveryComponent::WriteVisualOffset()
{
    if (UCameraComponent* Camera = VisualCamera.Get())
    {
        const FTransform NewTransform(FRotator(VisualOffset.X, VisualOffset.Y, 0.0).Quaternion());
        Camera->AddAdditiveOffset(AppliedVisualTransform.Inverse() * NewTransform, 0.0f);
        AppliedVisualTransform = NewTransform;
    }
}

void UCameraRecoilRecoveryComponent::ClearIndependentVisual()
{
    if (UCameraComponent* Camera = VisualCamera.Get())
        Camera->AddAdditiveOffset(AppliedVisualTransform.Inverse(), 0.0f);
    VisualCamera.Reset();
    VisualKicks.Reset();
    VisualOffset = FVector2D::ZeroVector;
    AppliedVisualTransform = FTransform::Identity;
    bIndependentVisualMode = false;
}

void UCameraRecoilRecoveryComponent::SetAimingRecoilMode(bool bAiming)
{
    if (bAimingRecoilMode == bAiming) return;
    // Keep the player's current orientation but stop all tails from the previous mode.
    ClearRecoil();
    bAimingRecoilMode = bAiming;
}

