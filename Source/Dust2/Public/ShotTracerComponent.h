#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShotTracerComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;

UCLASS(ClassGroup=(Weapon), meta=(BlueprintSpawnableComponent))
class DUST2_API UShotTracerComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShotTracerComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(DisplayName="启用曳光"))
	bool bTracerEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(DisplayName="曳光特效"))
	TObjectPtr<UNiagaraSystem> TracerSystem;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(DisplayName="视觉飞行速度", ClampMin="1.0", Units="cm/s"))
	float TravelSpeed = 30000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(DisplayName="最长视觉飞行时间", ClampMin="0.016667", ClampMax="1.0", Units="s"))
	float MaxTravelDuration = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(DisplayName="亮线长度", ClampMin="0.01", Units="cm"))
	float StreakLength = 80.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(DisplayName="亮线宽度", ClampMin="0.01", Units="cm"))
	float StreakWidth = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(DisplayName="曳光颜色"))
	FLinearColor TracerColor = FLinearColor(1.0f, 0.65f, 0.25f, 1.0f);

	// Call once AFTER a successful shot's trace. ShotEnd must be its hit point or trace end.
	UFUNCTION(BlueprintCallable, Category="Weapon|Tracer", meta=(ReturnDisplayName="Spawned"))
	bool PlayShotTracer(FVector MuzzleLocation, FVector ShotEnd);
	UFUNCTION(BlueprintCallable, Category="Weapon|Tracer")
	void ClearShotTracers();
protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	TArray<TWeakObjectPtr<UNiagaraComponent>> ActiveTracers;
};
