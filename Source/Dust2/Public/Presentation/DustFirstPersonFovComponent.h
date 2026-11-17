#pragma once

#include "Components/ActorComponent.h"
#include "DustFirstPersonFovComponent.generated.h"

class UCameraComponent;

UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent))
class DUST2_API UDustFirstPersonFovComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDustFirstPersonFovComponent();

    UFUNCTION(BlueprintCallable, Category="Dust2|Camera")
    void InitializeFirstPersonFov(UCameraComponent* InCamera);

    UFUNCTION(BlueprintCallable, Category="Dust2|Camera")
    void UpdateTargetFov(float NewFov, float InterpSpeed);

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    TWeakObjectPtr<UCameraComponent> Camera;
    float TargetFov = 0.0f;
    float FovInterpSpeed = 0.0f;
};
