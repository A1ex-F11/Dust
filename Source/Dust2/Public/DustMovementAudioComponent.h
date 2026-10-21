#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustMovementGait.h"
#include "DustMovementAudioComponent.generated.h"

class ACharacter;
class USoundBase;

// What made the noise. W10 hearing reads this; add new kinds (e.g. crouch, slide) here.
UENUM(BlueprintType, meta=(DisplayName="移动噪声类型"))
enum class EDustMovementNoiseType : uint8
{
	Footstep UMETA(DisplayName = "脚步"),
	Jump UMETA(DisplayName = "跳跃"),
	Land UMETA(DisplayName = "落地")
};

USTRUCT(BlueprintType, meta=(DisplayName="移动噪声数据"))
struct FDustMovementNoise
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "移动|声音")
	FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "移动|声音")
	EDustMovementNoiseType Type = EDustMovementNoiseType::Footstep;
	UPROPERTY(BlueprintReadOnly, Category = "移动|声音")
	EDustMovementGait Gait = EDustMovementGait::Walk;
	// 0..1. Hearing range is decided by the listener, not here.
	UPROPERTY(BlueprintReadOnly, Category = "移动|声音")
	float Loudness = 0.0f;
};

USTRUCT(BlueprintType, meta=(DisplayName="步态脚步设置"))
struct FDustFootstepGaitSettings
{
	GENERATED_BODY()

	// Unchecked = this gait makes no footstep sound and no noise event.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta=(DisplayName="启用脚步噪声"))
	bool bAudible = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta=(DisplayName="脚步音效"))
	TObjectPtr<USoundBase> Sound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta = (ClampMin = "10.0", DisplayName="步幅长度"))
	float StrideLength = 170.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName="响度"))
	float Loudness = 0.5f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDustMovementNoise, const FDustMovementNoise&, Noise);

// Plays footstep / jump / land sounds for any ACharacter and reports each as a noise event.
// The owner only pushes its gait (SetGait) and jump start (NotifyJumped); landing is detected here.
UCLASS(ClassGroup = (Dust2), meta = (BlueprintSpawnableComponent, DisplayName="移动声音组件"))
class DUST2_API UDustMovementAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustMovementAudioComponent();

	UFUNCTION(BlueprintCallable, Category = "移动|声音")
	void SetGait(EDustMovementGait NewGait);
	UFUNCTION(BlueprintCallable, Category = "移动|声音")
	void NotifyJumped();
	// Turn everything off, e.g. on death or in cutscenes.
	UFUNCTION(BlueprintCallable, Category = "移动|声音")
	void SetMovementAudioEnabled(bool bEnabled);

	// Every audible movement noise. W10 AI hearing subscribes here.
	UPROPERTY(BlueprintAssignable, Category = "移动|声音")
	FOnDustMovementNoise OnMovementNoise;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta=(DisplayName="脚步设置"))
	TMap<EDustMovementGait, FDustFootstepGaitSettings> FootstepSettings;
	// Horizontal speed below this counts as standing still.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta = (ClampMin = "0.0", DisplayName="脚步最小速度"))
	float FootstepMinSpeed = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta=(DisplayName="跳跃音效"))
	TObjectPtr<USoundBase> JumpSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName="跳跃响度"))
	float JumpLoudness = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta=(DisplayName="落地音效"))
	TObjectPtr<USoundBase> LandSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName="落地响度"))
	float LandLoudness = 1.0f;
	// Drops slower than this (small steps, stairs) land silently.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "移动|声音", meta = (ClampMin = "0.0", DisplayName="落地最小下落速度"))
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