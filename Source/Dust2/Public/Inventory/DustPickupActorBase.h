#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/DustTutorialMessage.h"
#include "DustPickupActorBase.generated.h"

class APawn;

UCLASS(Blueprintable, meta=(DisplayName="Dust Pickup Actor Base"))
class DUST2_API ADustPickupActorBase : public AActor
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="拾取", meta=(DisplayName="Native Can Attempt Pickup"))
	bool NativeCanAttemptPickup(const APawn* Player) const;

	// Call only after the reward has been granted.
	UFUNCTION(BlueprintCallable, Category="拾取", meta=(DisplayName="Native Commit Successful Pickup"))
	void NativeCommitSuccessfulPickup(APawn* Player = nullptr);

	UPROPERTY(EditDefaultsOnly, Category="拾取|教学")
	FDustTutorialMessage PickupTutorial;
};
