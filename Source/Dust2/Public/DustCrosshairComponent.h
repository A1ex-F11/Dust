#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CrosshairDisplayMath.h"
#include "DustCrosshairComponent.generated.h"

class ADust2PlayerCharacter;
class AWeaponRuntime;
class UCameraComponent;
class UDynamicCrosshairWidget;
class UWeaponInventoryComponent;
class UWeaponPresentationComponent;

/** Dust2-only adapter. The reusable renderer and transition math know no weapon types. */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent, DisplayName="准星组件"))
class DUST2_API UDustCrosshairComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDustCrosshairComponent();

    // Explicit setup once the actual HUD and native inventory bridge exist.
    UFUNCTION(BlueprintCallable, Category = "准星", meta = (DisplayName = "初始化动态准星"))
    bool InitializeCrosshair(UDynamicCrosshairWidget* Widget,
        UWeaponInventoryComponent* Inventory, UWeaponPresentationComponent* Presentation,
        UCameraComponent* ShootingCamera);

    UFUNCTION(BlueprintCallable, Category = "准星", meta = (DisplayName = "停止动态准星"))
    void StopCrosshair();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "准星|显示",
        meta = (ClampMin = "0.0", DisplayName = "普通间距跟随速度"))
    float FollowRate = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "准星|显示",
        meta = (ClampMin = "1.0", UIMax = "4.0", DisplayName = "射击扩散显示倍率",
            ToolTip = "只放大射击累计项的准星反馈，不改变移动基础、弹道或固定轨迹。1按真实角度；大于1时是视觉反馈，不是严格弹着范围。"))
    float FiringSpreadDisplayScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "准星|显示",
        meta = (ClampMin = "0.0", DisplayName = "换枪过渡最长秒数"))
    float SwitchTransitionSeconds = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "准星|显示",
        meta = (ClampMin = "0.0", DisplayName = "拔枪完成前提前秒数"))
    float FinishBeforeDrawSeconds = 0.04f;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleEquippedWeaponChanged(AWeaponRuntime* NewWeapon);
    UFUNCTION()
    void HandleDrawFinished(bool bCompletedNormally);
    bool BuildTargetGap(FVector2D& OutGap) const;

    TWeakObjectPtr<ADust2PlayerCharacter> Player;
    TWeakObjectPtr<UDynamicCrosshairWidget> CrosshairWidget;
    TWeakObjectPtr<UWeaponInventoryComponent> InventorySource;
    TWeakObjectPtr<UWeaponPresentationComponent> PresentationSource;
    TWeakObjectPtr<UCameraComponent> CameraSource;
    TWeakObjectPtr<AWeaponRuntime> DisplayedWeapon;
    FCrosshairDisplayTransitionState DisplayState;
    bool bHasFirstProjection = false;
    bool bFinishAfterProjection = false;
};
