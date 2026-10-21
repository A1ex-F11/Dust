#pragma once

#include "CoreMinimal.h"
#include "Dust2PlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/UnrealType.h"

/** Read-only queries shared by AI components; existing actors keep state ownership. */
namespace DustActorState
{
	inline bool IsDead(const AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return true;
		if (const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(Actor))
			if (Player->IsDead()) return true;
		// Preserve the existing Blueprint enemy death field.
		if (const FBoolProperty* Flag = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bIsDead")))
			return Flag->GetPropertyValue_InContainer(Actor);
		return false;
	}

	inline bool IsAlive(const AActor* Actor)
	{
		return !IsDead(Actor);
	}

	inline bool IsMissionOver(const UWorld* World)
	{
		const AGameModeBase* Mode = World ? World->GetAuthGameMode() : nullptr;
		if (Mode)
			if (const FBoolProperty* Flag = FindFProperty<FBoolProperty>(Mode->GetClass(), TEXT("bResultResolved")))
				return Flag->GetPropertyValue_InContainer(Mode);
		return false;
	}
}
