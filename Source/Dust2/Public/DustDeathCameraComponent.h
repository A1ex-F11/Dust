#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustDeathCameraComponent.generated.h"

class ACameraActor;
class APlayerController;
class USkeletalMeshComponent;
class USpringArmComponent;

/** Local death view; does not own health, UI, pause or respawn decisions. */
UCLASS(ClassGroup=(Death), meta=(BlueprintSpawnableComponent, DisplayName="玩家死亡镜头"))
class DUST2_API UDustDeathCameraComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDustDeathCameraComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="镜头过渡秒数", ClampMin="0.0"))
	float BlendSeconds = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="镜头后拉距离", ClampMin="80.0"))
	float ArmLength = 260.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="观察点高度偏移"))
	float FocusHeight = 60.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="相对死亡朝向的侧偏角"))
	float YawOffset = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="镜头俯仰角"))
	float CameraPitch = -15.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="观察骨骼"))
	FName FocusBone = TEXT("pelvis");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="跟随平滑速度", ClampMin="0.0"))
	float FollowSpeed = 6.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="相机碰撞探测半径", ClampMin="1.0"))
	float ProbeRadius = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡镜头", meta=(DisplayName="死亡镜头视野角", ClampMin="30.0", ClampMax="120.0"))
	float FieldOfView = 80.0f;
	UFUNCTION(BlueprintCallable, Category="死亡镜头")
	bool BeginDeathCamera(USkeletalMeshComponent* InBodyMesh);
	UFUNCTION(BlueprintCallable, Category="死亡镜头")
	void ResetDeathCamera();
	UFUNCTION(BlueprintPure, Category="死亡镜头")
	bool IsDeathCameraActive() const { return bCameraActive; }
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="死亡镜头")
	FString LastCameraDiagnostic;
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend struct FDustDeathCameraTestAccess;
	TWeakObjectPtr<ACameraActor> DeathCamera;
	TWeakObjectPtr<APlayerController> DeathController;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TWeakObjectPtr<USkeletalMeshComponent> FollowMesh;
	bool bCameraActive = false;
	bool bOwnsMoveIgnore = false;
	bool bOwnsLookIgnore = false;
	uint64 CameraGeneration = 0;
	FVector GetFocusLocation() const;
	void ReleaseCamera(bool bRestoreView);
};
