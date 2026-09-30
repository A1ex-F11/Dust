// Fill out your copyright notice in the Description page of Project Settings.


#include "CameraRecoilRecoveryComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

// Sets default values for this component's properties
UCameraRecoilRecoveryComponent::UCameraRecoilRecoveryComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	MaxPitchOffset = 3.0f;
	MaxYawOffset = 0.5f;
	RecoveryDelay = 0.20f;
	RecoverySpeed = 1.0f;
	RecoveryInterval = 0.02f;
	RecoveryPitchRatio = 0.35f;
	AccumulatedPitch = 0.0f;
	AccumulatedYaw = 0.0f;
	LastKickTime = 0.0f;
	bRecoverImmediately = false;
	bRecoveryStarted = false;
}
void UCameraRecoilRecoveryComponent::ApplyRecoilKick(float PitchKick, float YawKick)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!PawnOwner || !PawnOwner->GetController() || !World)
	{
		return;
	}

	StopRecoveryTimer();
	bRecoveryStarted = false;
	bRecoverImmediately = false;

	const float PitchLimit = FMath::Max(0.0f, MaxPitchOffset);
	const float YawLimit = FMath::Max(0.0f, MaxYawOffset);

	const float NewPitch = FMath::Clamp(
		AccumulatedPitch + FMath::Max(0.0f, PitchKick),
	0.0f,
		PitchLimit);
	const float NewYaw = FMath::Clamp(
		AccumulatedYaw + YawKick,
		-YawLimit,
		YawLimit);

	const float AcceptedPitch = NewPitch - AccumulatedPitch;
	const float AcceptedYaw = NewYaw - AccumulatedYaw;

	AccumulatedPitch = NewPitch;
	AccumulatedYaw = NewYaw;
	LastKickTime = World->GetTimeSeconds();
	PawnOwner->AddControllerPitchInput(-AcceptedPitch);
	PawnOwner->AddControllerYawInput(AcceptedYaw);
	StartRecoveryTimer();
}
void UCameraRecoilRecoveryComponent::StartRecoveryTimer()
{
	if (FMath::IsNearlyZero(AccumulatedPitch) &&
		FMath::IsNearlyZero(AccumulatedYaw))
	{
		StopRecoveryTimer();
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Interval = FMath::Max(0.001f, RecoveryInterval);
	const float Delay = bRecoverImmediately
		? Interval
		: FMath::Max(0.001f, RecoveryDelay);

	World->GetTimerManager().SetTimer(
		RecoveryTimerHandle,
		this,
		&UCameraRecoilRecoveryComponent::RecoveryStep,
		Delay,
		false);
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

	const float Interval = FMath::Max(0.001f, RecoveryInterval);

	// A stale one-shot callback must never recover while real shots are still
	// refreshing LastKickTime. Re-arm it for exactly the remaining quiet time.
	if (!bRecoverImmediately)
	{
		const float ElapsedSinceLastKick =
			World->GetTimeSeconds() - LastKickTime;
		const float RemainingDelay = RecoveryDelay - ElapsedSinceLastKick;
		if (RemainingDelay > KINDA_SMALL_NUMBER)
		{
			World->GetTimerManager().SetTimer(
				RecoveryTimerHandle,
				this,
				&UCameraRecoilRecoveryComponent::RecoveryStep,
				RemainingDelay,
				false);
			return;
		}
	}

	if (!bRecoveryStarted)
	{
		bRecoveryStarted = true;
		bRecoverImmediately = false;
		AccumulatedPitch *= FMath::Clamp(RecoveryPitchRatio, 0.0f, 1.0f);
		AccumulatedYaw = 0.0f;

		if (FMath::IsNearlyZero(AccumulatedPitch))
		{
			ClearRecoil();
			return;
		}

		World->GetTimerManager().SetTimer(
			RecoveryTimerHandle,
			this,
			&UCameraRecoilRecoveryComponent::RecoveryStep,
			Interval,
			true);
	}

	const float StepSize =
		FMath::Max(0.0f, RecoverySpeed) *
		Interval;
	if (StepSize <= KINDA_SMALL_NUMBER)
	{
		ClearRecoil();
		return;
	}
	const float PitchStep = FMath::Min(AccumulatedPitch, StepSize);
	PawnOwner->AddControllerPitchInput(PitchStep);
	AccumulatedPitch -= PitchStep;

	if (FMath::IsNearlyZero(AccumulatedPitch))
	{
		ClearRecoil();
	}
}
void UCameraRecoilRecoveryComponent::StartRecovery()
{
	if (FMath::IsNearlyZero(AccumulatedPitch) &&
		FMath::IsNearlyZero(AccumulatedYaw))
	{
		ClearRecoil();
		return;
	}
	StopRecoveryTimer();
	bRecoveryStarted = false;
	bRecoverImmediately = true;
	StartRecoveryTimer();
}
void UCameraRecoilRecoveryComponent::ClearRecoil()
{
	StopRecoveryTimer();

	AccumulatedPitch = 0.0f;
	AccumulatedYaw = 0.0f;
	LastKickTime = 0.0f;
	bRecoverImmediately = false;
	bRecoveryStarted = false;
}
void UCameraRecoilRecoveryComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearRecoil();
	Super::EndPlay(EndPlayReason);
}
