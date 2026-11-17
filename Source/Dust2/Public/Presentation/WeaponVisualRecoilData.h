#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveVector.h"
#include "WeaponVisualRecoilData.generated.h"

/** Resource amplitudes. Rotation order is Roll, Pitch, Yaw (not FRotator's constructor order). */
USTRUCT(BlueprintType)
struct DUST2_API FVisualRecoilPoseSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="旋转") FVector4 Roll = FVector4(0, 0, 0, 0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="旋转") FVector2D Pitch = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="旋转") FVector4 Yaw = FVector4(0, 0, 0, 0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="位移") FVector2D KickBack = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="位移") FVector2D KickRight = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="位移") FVector2D KickUp = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="支点") FVector PivotOffset = FVector::ZeroVector;
	bool IsFinite() const;
};

/** Only gun-body presentation. No ammo, camera kick, input compensation or author BP type. */
UCLASS(BlueprintType, meta=(DisplayName="枪身后坐表现配置"))
class DUST2_API UWeaponVisualRecoilData : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="曲线") TObjectPtr<UCurveVector> SingleRotation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="曲线") TObjectPtr<UCurveVector> SingleLocation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="曲线") TObjectPtr<UCurveVector> AutoRotation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="曲线") TObjectPtr<UCurveVector> AutoLocation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="幅度") FVisualRecoilPoseSettings Hip;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="幅度") FVisualRecoilPoseSettings Aim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="播放", meta=(ClampMin="0.01")) float PlayRate = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="播放", meta=(ClampMin="0.0")) float PlaybackOffset = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="播放") bool bAlternateRoll = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="播放") FVector AutoRotationScale = FVector::OneVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="播放") FVector AutoLocationScale = FVector::OneVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="播放", meta=(ClampMin="0.01")) float SmoothingSpeed = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="空间") FRotator SpaceRotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="后移") float PushbackAmount = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="后移", meta=(ClampMin="0.01")) float PushbackDamping = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="后移", meta=(ClampMin="0.01")) float PushbackInterpSpeed = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="扰动") FVector2D NoiseAmount = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="扰动") FVector2D NoiseDamping = FVector2D(10, 10);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="扰动") FVector2D NoiseInterpSpeed = FVector2D(20, 20);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="扰动", meta=(ClampMin="0.0")) float NoiseAimScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="摆动") FVector2D PitchSwayRange = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="摆动") FVector2D YawSwayRange = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="摆动", meta=(ClampMin="0.01")) float SwayReturnSpeed = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="累积") float ProgressPitchPerShot = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="累积") float ProgressUpPerShot = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="累积", meta=(ClampMin="0.01")) float ProgressReturnSpeed = 10.0f;

	bool IsUsable() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
#if WITH_EDITORONLY_DATA
	// Editor migration input only; cleared on success and never used by the runtime.
	UPROPERTY(EditAnywhere, Category="迁移") TObjectPtr<UObject> LegacyImportSource;
	UPROPERTY(VisibleAnywhere, Category="迁移") FString ImportStatus;
#endif
	UFUNCTION(CallInEditor, Category="迁移", meta=(DisplayName="从旧后坐资源导入参数"))
	void ImportLegacyVisualRecoil();
};
