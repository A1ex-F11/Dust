#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponAccuracyPreset.generated.h"

UENUM(BlueprintType)
enum class EWeaponReloadStyle : uint8
{
	Magazine UMETA(DisplayName="整弹匣装填"),
	PerRound UMETA(DisplayName="逐颗装填"),
	PerRoundOrEmptyBatch UMETA(DisplayName="逐颗，全空且备弹充足时整批")
};

USTRUCT(BlueprintType)
struct DUST2_API FWeaponPostureSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="走动额外散布",ClampMin="0.0"))
	float MovingExtraAngle=2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="静步额外散布",ClampMin="0.0"))
	float SlowWalkExtraAngle=0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName="空中额外散布",ClampMin="0.0"))
	float AirborneExtraAngle=6.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, meta=(DisplayName="移动判定速度",ClampMin="0.0"))
	float MovementThreshold=10.0f;
};

UCLASS(BlueprintType, meta=(DisplayName="共用姿态精度预设"))
class DUST2_API UWeaponAccuracyPreset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="姿态精度", meta=(ShowOnlyInnerProperties))
	FWeaponPostureSettings Settings;
};
