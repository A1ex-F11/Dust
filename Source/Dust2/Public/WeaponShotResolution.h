#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "WeaponShotResolution.generated.h"

class AWeaponRuntime;
class UWeaponBallisticsData;

UENUM(BlueprintType, meta=(DisplayName="命中解析状态"))
enum class EWeaponShotResolutionStatus : uint8
{
	NoPendingShot UMETA(DisplayName = "无待处理射击"),
	MissingBallistics UMETA(DisplayName = "缺少弹道配置"),
	InvalidTrace UMETA(DisplayName = "无效射线"),
	NoBlockingHit UMETA(DisplayName = "未命中阻挡物"),
	NotDamageable UMETA(DisplayName = "目标不可造成伤害"),
	TargetDead UMETA(DisplayName = "目标已死亡"),
	NoDamage UMETA(DisplayName = "未造成伤害"),
	InvalidDamageContract UMETA(DisplayName = "伤害接口无效"),
	DamageDispatched UMETA(DisplayName = "已发送伤害")
};

USTRUCT(BlueprintType, meta=(DisplayName="命中解析结果"))
struct DUST2_API FWeaponShotResolution
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	EWeaponShotResolutionStatus Status = EWeaponShotResolutionStatus::NoPendingShot;

	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	bool bTracePerformed = false;

	/** True when one positive base-damage request was sent to a live target's damage interface. */
	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	bool bHitDamageable = false;

	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	FVector TraceStart = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	FVector TraceEnd = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	FVector ShotEnd = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	FVector ShotDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "武器|射击")
	FHitResult Hit;
};

namespace DustWeaponHit
{
	/** Resolves a trace and the existing damage interface for one committed shot. */
	DUST2_API FWeaponShotResolution Resolve(
		AWeaponRuntime* Weapon,
		const FVector& TraceStart,
		const FVector& ShotDirection,
		const UWeaponBallisticsData* Ballistics);
}
