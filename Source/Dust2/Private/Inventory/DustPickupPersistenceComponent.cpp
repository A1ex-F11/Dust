#include "Inventory/DustPickupPersistenceComponent.h"

#include "Identity/DustStableIdComponent.h"
#include "Inventory/DustPickupPersistenceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogDustPickupPersistence, Log, All);

UDustPickupPersistenceComponent::UDustPickupPersistenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FName UDustPickupPersistenceComponent::GetLevelId() const
{
	const UWorld* World = GetWorld();
	return World ? FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : NAME_None;
}

UDustPickupPersistenceSubsystem* UDustPickupPersistenceComponent::GetPersistence() const
{
	if (PersistenceOverride) return PersistenceOverride;
	const UWorld* World = GetWorld();
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UDustPickupPersistenceSubsystem>() : nullptr;
}

FName UDustPickupPersistenceComponent::NativeGetEffectivePickupId() const
{
	// Existing hand-authored ids keep priority over automatic identities.
	if (!PickupId.IsNone()) return PickupId;
	return UDustStableIdComponent::ResolveStableIdForActor(GetOwner());
}

bool UDustPickupPersistenceComponent::NativeIsPickupAvailable() const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || bConsumedThisInstance) return false;

	const FName EffectiveId = NativeGetEffectivePickupId();
	if (EffectiveId.IsNone()) return true;

	const UDustPickupPersistenceSubsystem* Persistence = GetPersistence();
	return !Persistence || !Persistence->IsConsumed(GetLevelId(), EffectiveId);
}

bool UDustPickupPersistenceComponent::RecordSuccessfulPickup()
{
	if (bConsumedThisInstance) return false;
	bConsumedThisInstance = true;

	const FName EffectiveId = NativeGetEffectivePickupId();
	if (EffectiveId.IsNone()) return false;

	UDustPickupPersistenceSubsystem* Persistence = GetPersistence();
	if (!Persistence)
	{
		UE_LOG(LogDustPickupPersistence, Warning,
			TEXT("Pickup %s could not persist: no game instance subsystem."), *EffectiveId.ToString());
		return false;
	}
	return Persistence->RecordConsumed(GetLevelId(), EffectiveId);
}

void UDustPickupPersistenceComponent::NativeConsumePickup()
{
	RecordSuccessfulPickup();
	if (AActor* Owner = GetOwner(); IsValid(Owner) && !Owner->IsActorBeingDestroyed())
	{
		Owner->SetActorEnableCollision(false);
		Owner->SetActorHiddenInGame(true);
		Owner->Destroy();
	}
}

void UDustPickupPersistenceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!NativeIsPickupAvailable())
	{
		if (AActor* Owner = GetOwner(); IsValid(Owner) && !Owner->IsActorBeingDestroyed())
		{
			Owner->SetActorEnableCollision(false);
			Owner->SetActorHiddenInGame(true);
			Owner->Destroy();
		}
	}
}
