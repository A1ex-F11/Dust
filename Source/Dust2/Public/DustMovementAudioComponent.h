#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustMovementGait.h"
#include "DustMovementAudioComponent.generated.h"

class ACharacter;
class USoundBase;

// What made the noise. W10 hearing reads this; add new kinds (e.g. crouch, slide) here.
UENUM(BlueprintType)
enum class EDustMovementNoiseType : uint8
{
	Footstep,
	Jump,
	Land
};

USTRUCT(BlueprintType)
struct FDustMovementNoise
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Movement|Audio")
	FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Audio")
	EDustMovementNoiseType Type = EDustMovementNoiseType::Footstep;
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Audio")
	EDustMovementGait Gait = EDustMovementGait::Walk;
	// 0..1. Hearing range is decided by the listener, not here.
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Audio")
	float Loudness = 0.0f;
};

USTRUCT(BlueprintType)
struct FDustFootstepGaitSettings
{
	GENERATED_BODY()

	// Unchecked = this gait makes no footstep sound and no noise event.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio")
	bool bAudible = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio")
	TObjectPtr<USoundBase> Sound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio", meta = (ClampMin = "10.0"))
	float StrideLength = 170.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Loudness = 0.5f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDustMovementNoise, const FDustMovementNoise&, Noise);

// Plays footstep / jump / land sounds for any ACharacter and reports each as a noise event.
// The owner only pushes its gait (SetGait) and jump start (NotifyJumped); landing is detected here.
UCLASS(ClassGroup = (Dust2), meta = (BlueprintSpawnableComponent))
class DUST2_API UDustMovementAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustMovementAudioComponent();

	UFUNCTION(BlueprintCallable, Category = "Movement|Audio")
	void SetGait(EDustMovementGait NewGait);
	UFUNCTION(BlueprintCallable, Category = "Movement|Audio")
	void NotifyJumped();
	// Turn everything off, e.g. on death or in cutscenes.
	UFUNCTION(BlueprintCallable, Category = "Movement|Audio")
	void SetMovementAudioEnabled(bool bEnabled);

	// Every audible movement noise. W10 AI hearing subscribes here.
	UPROPERTY(BlueprintAssignable, Category = "Movement|Audio")
	FOnDustMovementNoise OnMovementNoise;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio")
	TMap<EDustMovementGait, FDustFootstepGaitSettings> FootstepSettings;
	// Horizontal speed below this counts as standing still.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio", meta = (ClampMin = "0.0"))
	float FootstepMinSpeed = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio")
	TObjectPtr<USoundBase> JumpSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JumpLoudness = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio")
	TObjectPtr<USoundBase> LandSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LandLoudness = 1.0f;
	// Drops slower than this (small steps, stairs) land silently.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Audio", meta = (ClampMin = "0.0"))
	float LandMinFallSpeed = 300.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);
	void UpdateFootsteps();
	void EmitNoise(EDustMovementNoiseType Type, USoundBase* Sound, float Loudness);

	TWeakObjectPtr<ACharacter> OwnerCharacter;
	EDustMovementGait CurrentGait = EDustMovementGait::Walk;
	bool bMovementAudioEnabled = true;
	float FootstepDistanceAccum = 0.0f;
	FTimerHandle FootstepTimerHandle;
	static constexpr float FootstepInterval = 0.05f;
};