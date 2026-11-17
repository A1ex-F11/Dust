#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustEnemyHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustNativeEnemyHealthChanged, float, NewHealth, bool, bKilledNow);

/** Owns damage and health for an enemy that has completed Blueprint setup. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="Dust Enemy Health Component"))
class DUST2_API UDustEnemyHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Existing enemy presentation and death handlers run synchronously from this event. */
	UPROPERTY(BlueprintAssignable, Category="战斗|敌人", meta=(DisplayName="On Native Enemy Health Changed"))
	FDustNativeEnemyHealthChanged OnNativeEnemyHealthChanged;

	UFUNCTION(BlueprintCallable, Category="战斗|敌人", meta=(DisplayName="Initialize Native Enemy Health"))
	void InitializeNativeEnemyHealth(float InitialHealth, bool bInHasHelmet, bool bInHasBodyArmor);

	UFUNCTION(BlueprintCallable, Category="战斗|敌人", meta=(DisplayName="Apply Native Enemy Damage"))
	float ApplyNativeEnemyDamage(float Damage, FName HitBoneName, float ArmorPenetration,
		float HeadDamageMultiplier, bool& bKilledNow);

	UFUNCTION(BlueprintPure, Category="战斗|敌人", meta=(DisplayName="Is Native Enemy Health Initialized"))
	bool IsInitialized() const { return bInitialized; }

	bool IsDead() const { return bInitialized && CurrentHealth <= 0.0f; }
	void KillFromEnvironment();

private:
	float CurrentHealth = 0.0f;
	bool bHasHelmet = false;
	bool bHasBodyArmor = false;
	bool bInitialized = false;
};
