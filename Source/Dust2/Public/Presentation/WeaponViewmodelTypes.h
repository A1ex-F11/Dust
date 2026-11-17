#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "WeaponViewmodelTypes.generated.h"

USTRUCT(BlueprintType, meta=(DisplayName="持枪摆动轴参数"))
struct DUST2_API FWeaponSwayVectorSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="刚度", ClampMin="0.0"))
	FVector Stiffness = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="阻尼", ClampMin="0.0"))
	FVector Damping = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="插值速度", ClampMin="0.0"))
	FVector InterpSpeed = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="平滑速度", ClampMin="0.0"))
	FVector SmoothingSpeed = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="限制幅度", ClampMin="0.0"))
	FVector ClampValue = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="轴向倍率"))
	FVector Scale = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="瞄准倍率", ClampMin="0.0"))
	float AdsScale = 1.0f;

	bool IsUsable() const;
};

USTRUCT(BlueprintType, meta=(DisplayName="持枪摆动参数"))
struct DUST2_API FWeaponSwaySettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="位置"))
	FWeaponSwayVectorSettings Position;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="摆动", meta=(DisplayName="旋转"))
	FWeaponSwayVectorSettings Rotation;

	bool IsUsable() const { return Position.IsUsable() && Rotation.IsUsable(); }
};

USTRUCT(BlueprintType, meta=(DisplayName="持枪偏移配置"))
struct DUST2_API FWeaponViewmodelPose
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="偏移", meta=(DisplayName="支点偏移"))
	FVector PivotOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="偏移", meta=(DisplayName="持枪姿势偏移"))
	FTransform PoseOffset = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="偏移", meta=(DisplayName="奔跑姿势偏移"))
	FTransform SprintOffset = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="偏移", meta=(DisplayName="右手偏移"))
	FVector RightHandOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="偏移", meta=(DisplayName="左手偏移"))
	FVector LeftHandOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="偏移", meta=(DisplayName="奔跑左肘偏移"))
	FVector LeftElbowSprintOffset = FVector::ZeroVector;

	bool IsUsable() const;
};

USTRUCT(BlueprintType, meta=(DisplayName="持枪姿势与摆动配置"))
struct DUST2_API FWeaponViewmodelSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持枪", meta=(DisplayName="待机持枪动画"))
	TObjectPtr<UAnimSequence> IdlePose = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持枪", meta=(DisplayName="持枪偏移"))
	FWeaponViewmodelPose Viewmodel;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持枪", meta=(DisplayName="移动摆动"))
	FWeaponSwaySettings MovementSway;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持枪", meta=(DisplayName="瞄准摆动"))
	FWeaponSwaySettings AimingSway;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持枪", meta=(DisplayName="行走强度", ClampMin="0.0"))
	float WalkIntensity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持枪", meta=(DisplayName="奔跑强度", ClampMin="0.0"))
	float SprintIntensity = 1.0f;

	bool IsUsable() const;
};
