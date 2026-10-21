#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "UObject/Object.h"
#include "DustEnemyCloseCombatTests.generated.h"

class AWeaponRuntime;

/**
 * Test-only receiver for the existing BlueprintAssignable FDustEnemyShotResolved delegate.
 * Dynamic multicast delegates bind UFUNCTIONs, so the close-combat tests need one real native
 * receiver to record the payload. It owns no gameplay logic and is only created by the tests.
 */
UCLASS()
class UDustEnemyCloseCombatShotListener : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION()
	void HandleShot(FVector Origin, FVector End, const FHitResult& Hit, float Damage,
		float ArmorPenetration, float HeadDamageMultiplier, AWeaponRuntime* Weapon);

	int32 ShotCount = 0;
	bool bLastBlockingHit = false;
	TWeakObjectPtr<AActor> LastHitActor;
	FVector LastOrigin = FVector::ZeroVector;
	FVector LastEnd = FVector::ZeroVector;
	float LastDamage = -1.0f;
	float LastArmorPenetration = -1.0f;
	float LastHeadDamageMultiplier = -1.0f;
};
