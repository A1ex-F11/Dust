#include "WeaponShotTestReceiver.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "WeaponRuntime.h"

namespace
{
	void ConfigureVisibilityBox(UBoxComponent* Box)
	{
		if (!Box) return;
		Box->SetBoxExtent(FVector(20.0, 40.0, 60.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldDynamic);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Box->SetGenerateOverlapEvents(false);
	}
}

AWeaponShotTestReceiver::AWeaponShotTestReceiver()
{
	PrimaryActorTick.bCanEverTick = false;
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ShotTestCollision"));
	SetRootComponent(CollisionBox);
	ConfigureVisibilityBox(CollisionBox);
}

void AWeaponShotTestReceiver::ReceiveDamage_Implementation(
	float Damage,
	float ArmorPenetration,
	float HeadDamageMultiplier,
	FName HitBoneName)
{
	++DamageCallCount;
	LastDamage = Damage;
	LastArmorPenetration = ArmorPenetration;
	LastHeadDamageMultiplier = HeadDamageMultiplier;
	LastHitBoneName = HitBoneName;
	if (bAttemptNestedCommitOnDamage && IsValid(ReentryWeapon))
	{
		ReentrantFireResult = static_cast<uint8>(ReentryWeapon->TryCommitFireWithResult());
	}
	if (bMarkDeadOnDamage) bIsDead = true;
}

AWeaponShotTestPlayerReceiver::AWeaponShotTestPlayerReceiver()
{
	DamageCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ShotTestVisibilityCollision"));
	DamageCollisionBox->SetupAttachment(GetCapsuleComponent());
	ConfigureVisibilityBox(DamageCollisionBox);
}

void AWeaponShotTestPlayerReceiver::ReceiveDamage_Implementation(
	float,
	float,
	float,
	FName)
{
	++DamageCallCount;
}

AWeaponShotTestLookalike::AWeaponShotTestLookalike()
{
	PrimaryActorTick.bCanEverTick = false;
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ShotTestCollision"));
	SetRootComponent(CollisionBox);
	ConfigureVisibilityBox(CollisionBox);
}

void AWeaponShotTestLookalike::ReceiveDamage(
	float,
	float,
	float,
	FName)
{
	++DamageCallCount;
}

AWeaponShotTestMalformedReceiver::AWeaponShotTestMalformedReceiver()
{
	PrimaryActorTick.bCanEverTick = false;
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ShotTestCollision"));
	SetRootComponent(CollisionBox);
	ConfigureVisibilityBox(CollisionBox);
}

void AWeaponShotTestMalformedReceiver::ReceiveDamage_Implementation(
	float,
	float,
	float,
	FName,
	bool)
{
	++DamageCallCount;
}

void UWeaponShotFireReentryListener::AttemptNestedCommit()
{
	++BroadcastCount;
	if (IsValid(Weapon))
	{
		ReentrantFireResult = static_cast<uint8>(Weapon->TryCommitFireWithResult());
	}
}
