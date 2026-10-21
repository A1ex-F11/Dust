#pragma once

#include "CoreMinimal.h"
#include "Dust2PlayerCharacter.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "WeaponShotTestReceiver.generated.h"

class UBoxComponent;
class AWeaponRuntime;

UINTERFACE(BlueprintType)
class UWeaponShotTestDamageable : public UInterface
{
	GENERATED_BODY()
};

class IWeaponShotTestDamageable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Weapon Shot Test")
	void ReceiveDamage(
		float Damage,
		float ArmorPenetration,
		float HeadDamageMultiplier,
		FName HitBoneName);
};

/** Native event receiver with the same four-input shape as the existing damage contract. */
UCLASS(Transient, NotBlueprintable)
class AWeaponShotTestReceiver : public AActor, public IWeaponShotTestDamageable
{
	GENERATED_BODY()

public:
	AWeaponShotTestReceiver();

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(Transient)
	bool bIsDead = false;

	UPROPERTY(Transient)
	TObjectPtr<AWeaponRuntime> ReentryWeapon;

	bool bMarkDeadOnDamage = false;
	bool bAttemptNestedCommitOnDamage = false;
	int32 DamageCallCount = 0;
	uint8 ReentrantFireResult = MAX_uint8;
	float LastDamage = 0.0f;
	float LastArmorPenetration = 0.0f;
	float LastHeadDamageMultiplier = 0.0f;
	FName LastHitBoneName = NAME_None;

	virtual void ReceiveDamage_Implementation(
		float Damage,
		float ArmorPenetration,
		float HeadDamageMultiplier,
		FName HitBoneName) override;
};

/** Player-derived receiver lets the real player IsDead() gate be exercised in a world trace. */
UCLASS(Transient, NotBlueprintable)
class AWeaponShotTestPlayerReceiver : public ADust2PlayerCharacter, public IWeaponShotTestDamageable
{
	GENERATED_BODY()

public:
	AWeaponShotTestPlayerReceiver();

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> DamageCollisionBox;

	int32 DamageCallCount = 0;

	virtual void ReceiveDamage_Implementation(
		float Damage,
		float ArmorPenetration,
		float HeadDamageMultiplier,
		FName HitBoneName) override;
};

/** Same-named method without the interface must never be called by the reflection bridge. */
UCLASS(Transient, NotBlueprintable)
class AWeaponShotTestLookalike : public AActor
{
	GENERATED_BODY()

public:
	AWeaponShotTestLookalike();

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> CollisionBox;

	int32 DamageCallCount = 0;

	UFUNCTION()
	void ReceiveDamage(
		float Damage,
		float ArmorPenetration,
		float HeadDamageMultiplier,
		FName HitBoneName);
};

UINTERFACE(BlueprintType)
class UWeaponShotTestMalformedDamageable : public UInterface
{
	GENERATED_BODY()
};

class IWeaponShotTestMalformedDamageable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Weapon Shot Test")
	void ReceiveDamage(
		float Damage,
		float ArmorPenetration,
		float HeadDamageMultiplier,
		FName HitBoneName,
		bool bRequiredExtraArgument);
};

UCLASS(Transient, NotBlueprintable)
class AWeaponShotTestMalformedReceiver : public AActor, public IWeaponShotTestMalformedDamageable
{
	GENERATED_BODY()

public:
	AWeaponShotTestMalformedReceiver();

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> CollisionBox;

	int32 DamageCallCount = 0;

	virtual void ReceiveDamage_Implementation(
		float Damage,
		float ArmorPenetration,
		float HeadDamageMultiplier,
		FName HitBoneName,
		bool bRequiredExtraArgument) override;
};

UCLASS(Transient, NotBlueprintable)
class UWeaponShotFireReentryListener : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<AWeaponRuntime> Weapon;

	int32 BroadcastCount = 0;
	uint8 ReentrantFireResult = MAX_uint8;

	UFUNCTION()
	void AttemptNestedCommit();
};
