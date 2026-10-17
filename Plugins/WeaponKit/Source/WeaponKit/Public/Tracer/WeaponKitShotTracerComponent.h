#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Tracer/WeaponKitShotTracerComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

UCLASS(ClassGroup = (WeaponKit), meta = (BlueprintSpawnableComponent))
class WEAPONKIT_API UWeaponKitShotTracerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponKitShotTracerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "启用曳光"))
	bool bTracerEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "曳光特效"))
	TObjectPtr<UNiagaraSystem> TracerSystem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "视觉飞行速度", ClampMin = "1.0", Units = "cm/s"))
	float TravelSpeed = 30000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "最长视觉飞行时间", ClampMin = "0.016667", ClampMax = "1.0", Units = "s"))
	float MaxTravelDuration = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "亮线长度", ClampMin = "0.01", Units = "cm"))
	float StreakLength = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "亮线宽度", ClampMin = "0.01", Units = "cm"))
	float StreakWidth = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "曳光颜色"))
	FLinearColor TracerColor = FLinearColor(1.0f, 0.65f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer", meta = (DisplayName = "同时存在上限", ClampMin = "1"))
	int32 MaxActiveTracers = 64;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer|Parameters", meta = (DisplayName = "距离参数"))
	FName DistanceParameter = TEXT("User.TracerDistance");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer|Parameters", meta = (DisplayName = "寿命参数"))
	FName LifetimeParameter = TEXT("User.TracerLifetime");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer|Parameters", meta = (DisplayName = "长度参数"))
	FName LengthParameter = TEXT("User.TracerLength");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer|Parameters", meta = (DisplayName = "宽度参数"))
	FName WidthParameter = TEXT("User.TracerWidth");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracer|Parameters", meta = (DisplayName = "颜色参数"))
	FName ColorParameter = TEXT("User.TracerColor");

	/** Call once after a successful shot. ShotEnd is the hit point or the end of the trace. */
	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Tracer", meta = (ReturnDisplayName = "Spawned"))
	bool PlayShotTracer(FVector MuzzleLocation, FVector ShotEnd);

	UFUNCTION(BlueprintCallable, Category = "WeaponKit|Tracer")
	void ClearShotTracers();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TArray<TWeakObjectPtr<UNiagaraComponent>> ActiveTracers;
};
