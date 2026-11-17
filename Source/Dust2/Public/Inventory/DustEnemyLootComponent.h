#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustEnemyLootComponent.generated.h"

class AWeaponRuntime;
class AWeaponPickupActor;
class UWeaponDefinitionData;

USTRUCT(BlueprintType)
struct DUST2_API FDustEnemyLootWeaponEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(DisplayName="Enemy Weapon Definition"))
	TObjectPtr<UWeaponDefinitionData> EnemyDefinition;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(DisplayName="Player Weapon Class"))
	TSubclassOf<AWeaponRuntime> WeaponClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(DisplayName="Weapon Pickup Class"))
	TSubclassOf<AWeaponPickupActor> PickupClass;
};

/** One death attempt, with independent weapon/health rolls and no ticking. */
UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent, DisplayName="Dust Enemy Loot"))
class DUST2_API UDustEnemyLootComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustEnemyLootComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(ClampMin="0.0", ClampMax="100.0", DisplayName="Weapon Drop Chance (%)"))
	float WeaponDropChance = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(ClampMin="0.0", ClampMax="100.0", DisplayName="Health Drop Chance (%)"))
	float HealthDropChance = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(ClampMin="1", DisplayName="Minimum Total Ammo"))
	int32 MinimumTotalAmmo = 5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(ClampMin="1", DisplayName="Maximum Total Ammo"))
	int32 MaximumTotalAmmo = 15;
	// Serialized compatibility only. The selected definition is the sole live association.
	UPROPERTY(BlueprintReadOnly, Category="掉落|兼容", meta=(DisplayName="Weapon Drop Mapping"))
	TArray<FDustEnemyLootWeaponEntry> WeaponDropMapping;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(DisplayName="Health Pickup Class"))
	TSubclassOf<AActor> HealthPickupClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="掉落", meta=(DisplayName="Local Drop Offset"))
	FVector LocalDropOffset = FVector(60.0f, 0.0f, 30.0f);

	UFUNCTION(BlueprintCallable, Category="掉落", meta=(DisplayName="Native Drop Enemy Loot"))
	void NativeDropEnemyLoot();

protected:
	virtual void InitializeComponent() override;

private:
	friend struct FDustEnemyLootTestAccess;
	void DropOnce(UWeaponDefinitionData* Definition);
	bool ResolveWeaponDrop(const UWeaponDefinitionData* Definition,
		TSubclassOf<AWeaponPickupActor>& OutPickupClass, TSubclassOf<AWeaponRuntime>& OutWeaponClass) const;
	static bool RollChance(float Chance);
	bool bDropAttempted = false;
};
