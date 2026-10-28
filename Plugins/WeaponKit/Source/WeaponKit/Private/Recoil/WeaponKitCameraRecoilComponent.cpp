#include "Recoil/WeaponKitCameraRecoilComponent.h"
#include "Camera/CameraComponent.h"
#include "Data/WeaponKitRecoilData.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace
{
float WeaponKitSafeNonNegative(float Value, float Fallback = 0.0f)
{
	return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : Fallback;
}
}

UWeaponKitCameraRecoilComponent::UWeaponKitCameraRecoilComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UWeaponKitCameraRecoilComponent::SetAiming(bool bInAiming)
{
	if (bAiming == bInAiming)
	{
		return;
	}
	ClearRecoil();
	bAiming = bInAiming;
}

void UWeaponKitCameraRecoilComponent::ApplyRecoil(
	float PitchKick,
	float YawKick,
	UWeaponKitRecoilData* RecoilSettings,
	UCameraComponent* RecoilCamera)
{
	if (!IsValid(RecoilSettings))
	{
		return;
	}

	if (bAiming)
	{
		if (!FMath::IsFinite(PitchKick) || !FMath::IsFinite(YawKick))
		{
			return;
		}
		ClearIndependentVisual();
		ActiveRecoveryDelay = WeaponKitSafeNonNegative(RecoilSettings->RecoveryDelay);
		ActiveRecoverySpeed = WeaponKitSafeNonNegative(RecoilSettings->ShotRecoilRecoverySpeed);
		ActiveRecoveryRatio = FMath::IsFinite(RecoilSettings->RecoveryPitchRatio)
			? FMath::Clamp(RecoilSettings->RecoveryPitchRatio, 0.0f, 1.0f)
			: 0.0f;
		const float Scale = WeaponKitSafeNonNegative(RecoilSettings->AimingRecoilScale);
		QueueKick(
			PitchKick * Scale,
			YawKick * Scale,
			WeaponKitSafeNonNegative(RecoilSettings->SmoothKickDuration),
			FMath::IsFinite(RecoilSettings->ImmediateKickFraction)
				? FMath::Clamp(RecoilSettings->ImmediateKickFraction, 0.0f, 1.0f)
				: 1.0f,
			false);
		return;
	}

	const bool bIndependentVisual = RecoilSettings->bUseFixedPattern && RecoilSettings->bUseIndependentShotRecoil;
	if (!bIndependentVisual)
	{
		ClearIndependentVisual();
		if (!RecoilSettings->bUseFixedPattern)
		{
			ActiveRecoveryDelay = WeaponKitSafeNonNegative(RecoveryDelay);
			ActiveRecoverySpeed = WeaponKitSafeNonNegative(RecoverySpeed);
			ActiveRecoveryRatio = FMath::Clamp(RecoveryPitchRatio, 0.0f, 1.0f);
			QueueKick(PitchKick, YawKick, 0.0f, 1.0f, true);
			return;
		}

		ActiveRecoveryDelay = WeaponKitSafeNonNegative(RecoilSettings->RecoveryDelay);
		ActiveRecoverySpeed = WeaponKitSafeNonNegative(RecoilSettings->RecoverySpeed);
		ActiveRecoveryRatio = FMath::IsFinite(RecoilSettings->RecoveryPitchRatio)
			? FMath::Clamp(RecoilSettings->RecoveryPitchRatio, 0.0f, 1.0f)
			: 0.0f;
		QueueKick(
			PitchKick,
			YawKick,
			WeaponKitSafeNonNegative(RecoilSettings->SmoothKickDuration),
			FMath::IsFinite(RecoilSettings->ImmediateKickFraction)
				? FMath::Clamp(RecoilSettings->ImmediateKickFraction, 0.0f, 1.0f)
				: 1.0f,
			false);
		return;
	}

	if (!IsValid(RecoilCamera) || !FMath::IsFinite(PitchKick) || !FMath::IsFinite(YawKick))
	{
		return;
	}
	if (!bIndependentVisualMode || VisualCamera.Get() != RecoilCamera)
	{
		ClearRecoil();
		bIndependentVisualMode = true;
		VisualCamera = RecoilCamera;
	}

	const float Scale = FMath::IsFinite(RecoilSettings->CameraFeedbackScale)
		? FMath::Max(0.0f, RecoilSettings->CameraFeedbackScale)
		: 0.0f;
	VisualReturnSpeed = FMath::Max(0.1f, WeaponKitSafeNonNegative(RecoilSettings->CameraReturnSpeed, 12.0f));
	const FVector2D Kick(FMath::Max(0.0f, PitchKick) * Scale, YawKick * Scale);
	const float Duration = WeaponKitSafeNonNegative(RecoilSettings->SmoothKickDuration);
	const float Immediate = FMath::IsFinite(RecoilSettings->ImmediateKickFraction)
		? FMath::Clamp(RecoilSettings->ImmediateKickFraction, 0.0f, 1.0f)
		: 1.0f;
	VisualOffset += Duration <= KINDA_SMALL_NUMBER ? Kick : Kick * Immediate;
	if (Duration > KINDA_SMALL_NUMBER)
	{
		FWeaponKitRecoilKickEnvelope Tail;
		Tail.Total = Kick * (1.0f - Immediate);
		Tail.Duration = Duration;
		if (!Tail.Total.IsNearlyZero())
		{
			VisualKicks.Add(Tail);
		}
	}
	WriteVisualOffset();
	if (!VisualKicks.IsEmpty() || !VisualOffset.IsNearlyZero())
	{
		SetComponentTickEnabled(true);
	}
}

void UWeaponKitCameraRecoilComponent::QueueKick(
	float PitchKick,
	float YawKick,
	float Duration,
	float ImmediateFraction,
	bool bUseCumulativeLimits)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!PawnOwner || !PawnOwner->GetController() || !World || !FMath::IsFinite(PitchKick) || !FMath::IsFinite(YawKick))
	{
		return;
	}

	StopRecoveryTimer();
	bRecoveryStarted = false;
	bRecoverImmediately = false;
	FVector2D Accepted(FMath::Max(0.0f, PitchKick), YawKick);
	if (bUseCumulativeLimits)
	{
		FVector2D Reserved(AccumulatedPitch, AccumulatedYaw);
		for (const FWeaponKitRecoilKickEnvelope& Kick : PendingKicks)
		{
			Reserved += Kick.GetRemaining();
		}
		const float PitchLimit = WeaponKitSafeNonNegative(MaxPitchOffset);
		const float YawLimit = WeaponKitSafeNonNegative(MaxYawOffset);
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
		FWeaponKitRecoilKickEnvelope Tail;
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

void UWeaponKitCameraRecoilComponent::ApplyDelta(const FVector2D& Delta, bool bRecoverable)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || !PawnOwner->GetController())
	{
		return;
	}

	if (bAiming)
	{
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

void UWeaponKitCameraRecoilComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
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
			if (VisualKicks[Index].IsComplete())
			{
				VisualKicks.RemoveAt(Index);
			}
		}
		VisualOffset *= FMath::Exp(-VisualReturnSpeed * Dt);
		if (VisualKicks.IsEmpty() && VisualOffset.IsNearlyZero(0.0001f))
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
		FWeaponKitRecoilKickEnvelope& Kick = PendingKicks[Index];
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

void UWeaponKitCameraRecoilComponent::StartRecoveryTimer()
{
	if (PendingKicks.IsEmpty() && FMath::IsNearlyZero(AccumulatedPitch) && FMath::IsNearlyZero(AccumulatedYaw))
	{
		StopRecoveryTimer();
		return;
	}
	if (UWorld* World = GetWorld())
	{
		const float Interval = FMath::Max(0.001f, WeaponKitSafeNonNegative(RecoveryInterval, 0.02f));
		World->GetTimerManager().SetTimer(
			RecoveryTimerHandle,
			this,
			&UWeaponKitCameraRecoilComponent::RecoveryStep,
			bRecoverImmediately ? Interval : FMath::Max(0.001f, ActiveRecoveryDelay),
			false);
	}
}

void UWeaponKitCameraRecoilComponent::StopRecoveryTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecoveryTimerHandle);
	}
}

void UWeaponKitCameraRecoilComponent::RecoveryStep()
{
	UWorld* World = GetWorld();
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!World || !PawnOwner || !PawnOwner->GetController())
	{
		ClearRecoil();
		return;
	}

	const float Interval = FMath::Max(0.001f, WeaponKitSafeNonNegative(RecoveryInterval, 0.02f));
	const float QuietRemaining = ActiveRecoveryDelay - (World->GetTimeSeconds() - LastKickTime);
	if (!PendingKicks.IsEmpty() || (!bRecoveryStarted && !bRecoverImmediately && QuietRemaining > KINDA_SMALL_NUMBER))
	{
		World->GetTimerManager().SetTimer(
			RecoveryTimerHandle,
			this,
			&UWeaponKitCameraRecoilComponent::RecoveryStep,
			FMath::Max(Interval, bRecoverImmediately ? Interval : QuietRemaining),
			false);
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
		World->GetTimerManager().SetTimer(
			RecoveryTimerHandle,
			this,
			&UWeaponKitCameraRecoilComponent::RecoveryStep,
			Interval,
			true);
	}

	const float PitchStep = FMath::Min(AccumulatedPitch, ActiveRecoverySpeed * Interval);
	if (PitchStep <= KINDA_SMALL_NUMBER)
	{
		NotifyManualLook();
		return;
	}

	if (bAiming)
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

void UWeaponKitCameraRecoilComponent::StartRecovery()
{
	if (bIndependentVisualMode)
	{
		return;
	}
	StopRecoveryTimer();
	bRecoveryStarted = false;
	bRecoverImmediately = true;
	StartRecoveryTimer();
}

void UWeaponKitCameraRecoilComponent::NotifyManualLook()
{
	if (bIndependentVisualMode)
	{
		return;
	}
	StopRecoveryTimer();
	AccumulatedPitch = 0.0f;
	AccumulatedYaw = 0.0f;
	bRecoveryStarted = false;
	bRecoverImmediately = false;
	for (FWeaponKitRecoilKickEnvelope& Kick : PendingKicks)
	{
		Kick.bRecoverable = false;
	}
}

void UWeaponKitCameraRecoilComponent::ClearRecoil()
{
	ClearIndependentVisual();
	NotifyManualLook();
	PendingKicks.Empty();
	LastKickTime = 0.0f;
	SetComponentTickEnabled(false);
}

void UWeaponKitCameraRecoilComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearRecoil();
	Super::EndPlay(EndPlayReason);
}

void UWeaponKitCameraRecoilComponent::WriteVisualOffset()
{
	if (UCameraComponent* Camera = VisualCamera.Get())
	{
		const FTransform NewTransform(FRotator(VisualOffset.X, VisualOffset.Y, 0.0).Quaternion());
		Camera->AddAdditiveOffset(AppliedVisualTransform.Inverse() * NewTransform, 0.0f);
		AppliedVisualTransform = NewTransform;
	}
}

void UWeaponKitCameraRecoilComponent::ClearIndependentVisual()
{
	if (UCameraComponent* Camera = VisualCamera.Get())
	{
		Camera->AddAdditiveOffset(AppliedVisualTransform.Inverse(), 0.0f);
	}
	VisualCamera.Reset();
	VisualKicks.Reset();
	VisualOffset = FVector2D::ZeroVector;
	AppliedVisualTransform = FTransform::Identity;
	bIndependentVisualMode = false;
}
