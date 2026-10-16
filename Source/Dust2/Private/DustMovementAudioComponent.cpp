#include "DustMovementAudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

UDustMovementAudioComponent::UDustMovementAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<USoundBase> WalkFinder(
		TEXT("/Game/KINEMATION/TacticalShooterPack/SFX/General/Walk/SC_Walk.SC_Walk"));
	static ConstructorHelpers::FObjectFinder<USoundBase> RunFinder(
		TEXT("/Game/KINEMATION/TacticalShooterPack/SFX/General/Run/SC_Run.SC_Run"));
	static ConstructorHelpers::FObjectFinder<USoundBase> JumpFinder(
		TEXT("/Game/KINEMATION/TacticalShooterPack/SFX/General/S_Jump.S_Jump"));
	static ConstructorHelpers::FObjectFinder<USoundBase> LandFinder(
		TEXT("/Game/KINEMATION/TacticalShooterPack/SFX/General/S_Land.S_Land"));

	FDustFootstepGaitSettings Walk;
	Walk.Sound = WalkFinder.Object;
	Walk.StrideLength = 170.0f;
	Walk.Loudness = 0.5f;
	FDustFootstepGaitSettings Run;
	Run.Sound = RunFinder.Object;
	Run.StrideLength = 230.0f;
	Run.Loudness = 1.0f;
	FDustFootstepGaitSettings SlowWalk;
	SlowWalk.bAudible = false;
	SlowWalk.Loudness = 0.0f;
	FootstepSettings.Add(EDustMovementGait::Walk, Walk);
	FootstepSettings.Add(EDustMovementGait::Run, Run);
	FootstepSettings.Add(EDustMovementGait::SlowWalk, SlowWalk);

	JumpSound = JumpFinder.Object;
	LandSound = LandFinder.Object;
}

void UDustMovementAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (ACharacter* Character = OwnerCharacter.Get())
	{
		Character->LandedDelegate.AddUniqueDynamic(this, &UDustMovementAudioComponent::HandleLanded);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(FootstepTimerHandle, this,
				&UDustMovementAudioComponent::UpdateFootsteps, FootstepInterval, true);
		}
	}
}

void UDustMovementAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FootstepTimerHandle);
	}
	if (ACharacter* Character = OwnerCharacter.Get())
	{
		Character->LandedDelegate.RemoveDynamic(this, &UDustMovementAudioComponent::HandleLanded);
	}
	Super::EndPlay(EndPlayReason);
}

void UDustMovementAudioComponent::SetGait(EDustMovementGait NewGait)
{
	CurrentGait = NewGait;
}

void UDustMovementAudioComponent::SetMovementAudioEnabled(bool bEnabled)
{
	bMovementAudioEnabled = bEnabled;
}

void UDustMovementAudioComponent::NotifyJumped()
{
	if (bMovementAudioEnabled)
	{
		EmitNoise(EDustMovementNoiseType::Jump, JumpSound, JumpLoudness);
	}
}

void UDustMovementAudioComponent::HandleLanded(const FHitResult& Hit)
{
	const ACharacter* Character = OwnerCharacter.Get();
	if (!bMovementAudioEnabled || !Character || -Character->GetVelocity().Z < LandMinFallSpeed)
	{
		return;
	}
	EmitNoise(EDustMovementNoiseType::Land, LandSound, LandLoudness);
}

void UDustMovementAudioComponent::UpdateFootsteps()
{
	const ACharacter* Character = OwnerCharacter.Get();
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	const FDustFootstepGaitSettings* Settings = FootstepSettings.Find(CurrentGait);
	const float Speed = Character ? Character->GetVelocity().Size2D() : 0.0f;
	if (!bMovementAudioEnabled || !Move || !Move->IsMovingOnGround() || !Settings ||
		!Settings->bAudible || Speed < FootstepMinSpeed)
	{
		// Restart at half a stride so the first step after moving off comes quickly.
		FootstepDistanceAccum = Settings ? Settings->StrideLength * 0.5f : 0.0f;
		return;
	}
	FootstepDistanceAccum += Speed * FootstepInterval;
	if (FootstepDistanceAccum < Settings->StrideLength)
	{
		return;
	}
	FootstepDistanceAccum = FMath::Fmod(FootstepDistanceAccum, Settings->StrideLength);
	EmitNoise(EDustMovementNoiseType::Footstep, Settings->Sound, Settings->Loudness);
}

void UDustMovementAudioComponent::EmitNoise(
	EDustMovementNoiseType Type, USoundBase* Sound, float Loudness)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	FDustMovementNoise Noise;
	Noise.Location = Owner->GetActorLocation();
	Noise.Type = Type;
	Noise.Gait = CurrentGait;
	Noise.Loudness = Loudness;
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Noise.Location);
	}
	OnMovementNoise.Broadcast(Noise);
}