#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/WeaponKitInterfaces.h"
#include "Runtime/WeaponKitRuntime.h"
#include "WeaponKitTestCarrier.generated.h"

/** Test pawn that implements the carrier interface. Not part of the runtime plugin. */
UCLASS()
class WEAPONKITTESTS_API AWeaponKitTestCarrier : public APawn, public IWeaponKitCarrier
{
	GENERATED_BODY()

public:
	bool bAlive = true;
	bool bDrawConsumed = false;

	virtual bool IsWeaponKitCarrierAlive_Implementation() const override
	{
		return bAlive;
	}

	virtual bool HasConsumedWeaponInitialDraw_Implementation(AWeaponKitRuntime* Weapon) const override
	{
		return bDrawConsumed && Weapon == LastDrawWeapon.Get();
	}

	virtual void ApplyWeaponInitialDrawConsumed_Implementation(AWeaponKitRuntime* Weapon) override
	{
		bDrawConsumed = true;
		LastDrawWeapon = Weapon;
	}

	TWeakObjectPtr<AWeaponKitRuntime> LastDrawWeapon;
};
