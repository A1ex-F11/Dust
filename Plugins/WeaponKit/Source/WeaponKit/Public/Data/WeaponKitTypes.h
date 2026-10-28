#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponKitTypes.generated.h"

UENUM(BlueprintType)
enum class EWeaponKitFireMode : uint8
{
	SemiAutomatic UMETA(DisplayName = "半自动"),
	FullAutomatic UMETA(DisplayName = "全自动"),
	Burst UMETA(DisplayName = "三连发"),
};

/** Hip-fire and aimed fire share this shape. Continuous-fire bloom lives on the same profile. */
USTRUCT(BlueprintType)
struct WEAPONKIT_API FWeaponKitSpreadProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "静止散布角度", ClampMin = "0.0"))
	float StationarySpreadAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "移动散布角度", ClampMin = "0.0"))
	float MovingSpreadAngle = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "跳跃散布角度", ClampMin = "0.0"))
	float JumpSpreadAngle = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "慢走散布角度", ClampMin = "0.0"))
	float SlowWalkSpreadAngle = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "移动散布速度阈值", ClampMin = "0.0"))
	float MovementSpreadSpeedThreshold = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "启用连续射击散布"))
	bool bUseFiringSpread = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "每发增加散布", ClampMin = "0.0"))
	float SpreadPerShot = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "连续射击散布上限", ClampMin = "0.0"))
	float MaxFiringSpread = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "连续射击散布恢复延迟", ClampMin = "0.0"))
	float SpreadRecoveryDelay = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "连续射击散布恢复速度", ClampMin = "0.0"))
	float SpreadRecoverySpeed = 2.0f;
};

/** Runtime ammo and fire mode carried by a dropped or checkpointed weapon. */
USTRUCT(BlueprintType)
struct WEAPONKIT_API FWeaponKitPickupState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Pickup")
	bool bHasSavedState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Pickup")
	int32 CurrentAmmo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Pickup")
	int32 ReserveAmmo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Pickup")
	EWeaponKitFireMode FireMode = EWeaponKitFireMode::SemiAutomatic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Pickup")
	bool bInitialDrawConsumed = false;
};

/**
 * One damage rule. Bones not listed by any region use multiplier 1 and body-armor mitigation.
 * The first region that names the bone wins.
 */
USTRUCT(BlueprintType)
struct WEAPONKIT_API FWeaponKitDamageRegion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Damage")
	FName RegionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Damage")
	TArray<FName> BoneNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Damage", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Damage")
	bool bMitigatedByHelmet = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponKit|Damage")
	bool bMitigatedByBodyArmor = false;
};
