#pragma once
#include "CoreMinimal.h"
class AActor;
class APawn;
class UDustEnemyCombatProfile;
class UWeaponDefinitionData;

// Knowledge rays may ignore allies; physical shot traces never use this filter.
namespace DustEnemyVisibility
{
	APawn* ResolvePawn(const AActor* Actor);
	bool IsFriendlyBlocker(const AActor* Observer, const AActor* Blocker);
	FVector GetAimPoint(const AActor* Target, const UDustEnemyCombatProfile* Profile);
	bool IsInRange(const AActor* Observer, const AActor* Target,
		const UDustEnemyCombatProfile* Profile, const UWeaponDefinitionData* Definition);
	bool HasSight(const AActor* Observer, const AActor* Target, const FVector& Eye,
		const FVector& End, int32* Checks = nullptr);
}
