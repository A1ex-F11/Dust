#pragma once
#include "CoreMinimal.h"
class AActor;
class APawn;
class UDustEnemyCombatProfile;
class UWeaponDefinitionData;

// Knowledge rays may ignore allies; physical shot traces never use this filter.
namespace DustEnemyVisibility
{
	// Stack-local reuse for one synchronous decision. Discard before callbacks or the next tick.
	struct FSightQuery
	{
		const AActor* Observer = nullptr;
		const AActor* Target = nullptr;
		FVector Eye = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		bool bHasResult = false;
		bool bVisible = false;
	};
	APawn* ResolvePawn(const AActor* Actor);
	bool IsFriendlyBlocker(const AActor* Observer, const AActor* Blocker);
	FVector GetAimPoint(const AActor* Target, const UDustEnemyCombatProfile* Profile);
	bool IsInRange(const AActor* Observer, const AActor* Target,
		const UDustEnemyCombatProfile* Profile, const UWeaponDefinitionData* Definition);
	bool HasSight(const AActor* Observer, const AActor* Target, const FVector& Eye,
		const FVector& End, int32* Checks = nullptr, FSightQuery* Query = nullptr);
}
