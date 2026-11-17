#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustPickupIdleComponent.generated.h"

class UPrimitiveComponent;
class USceneComponent;

UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent, DisplayName="Dust Pickup Idle"))
class DUST2_API UDustPickupIdleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustPickupIdleComponent();

	UPROPERTY(EditAnywhere, Category="拾取展示")
	float SpinDegreesPerSecond = 45.0f;

	UPROPERTY(EditAnywhere, Category="拾取展示", meta=(ClampMin="0.0"))
	float BobHeight = 3.0f;

	UPROPERTY(EditAnywhere, Category="拾取展示", meta=(ClampMin="0.01"))
	float BobPeriod = 2.0f;

	UFUNCTION(BlueprintCallable, Category="拾取展示", meta=(DisplayName="Start Pickup Idle"))
	void StartPickupIdle(USceneComponent* VisualTarget, UPrimitiveComponent* PhysicsBody);

	UFUNCTION(BlueprintCallable, Category="拾取展示", meta=(DisplayName="Stop Pickup Idle"))
	void StopPickupIdle();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool CanMoveVisual() const;

	TWeakObjectPtr<USceneComponent> Target;
	TWeakObjectPtr<UPrimitiveComponent> Body;
	FVector BaseLocation = FVector::ZeroVector;
	FQuat BaseRotation = FQuat::Identity;
	double ElapsedSeconds = 0.0;
};
