#include "Inventory/DustPickupActorBase.h"

#include "Inventory/DustPickupPersistenceComponent.h"
#include "Player/Dust2PlayerCharacter.h"
#include "UI/DustTutorialComponent.h"

bool ADustPickupActorBase::NativeCanAttemptPickup(const APawn* Player) const
{
	const ADust2PlayerCharacter* Character = Cast<ADust2PlayerCharacter>(Player);
	if (!IsValid(Character) || Character->IsDead() || IsActorBeingDestroyed()) return false;

	const UDustPickupPersistenceComponent* Persistence = FindComponentByClass<UDustPickupPersistenceComponent>();
	return !Persistence || Persistence->NativeIsPickupAvailable();
}

void ADustPickupActorBase::NativeCommitSuccessfulPickup(APawn* Player)
{
	if (IsActorBeingDestroyed()) return;
	if (IsValid(Player) && !PickupTutorial.Key.IsNone())
	{
		if (UDustTutorialComponent* Tutorial = Player->FindComponentByClass<UDustTutorialComponent>())
		{
			Tutorial->RequestTutorial(PickupTutorial);
		}
	}
	if (UDustPickupPersistenceComponent* Persistence = FindComponentByClass<UDustPickupPersistenceComponent>())
	{
		Persistence->NativeConsumePickup();
		return;
	}
	Destroy();
}
