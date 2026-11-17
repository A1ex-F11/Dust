#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustCombatFeedbackComponent.generated.h"

class UDynamicCrosshairWidget;
class UDustCombatFeedbackWidget;
class ADust2PlayerCharacter;

/** Local player's presentation owner, independent from the battle HUD's visibility. */
UCLASS(ClassGroup=(UI), meta=(BlueprintSpawnableComponent, DisplayName="Dust Combat Feedback"))
class DUST2_API UDustCombatFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDustCombatFeedbackComponent();

    UFUNCTION(BlueprintCallable, Category="战斗|命中反馈", meta=(DisplayName="Initialize Combat Feedback"))
    bool InitializeCombatFeedback(UDynamicCrosshairWidget* Crosshair);

    UFUNCTION(BlueprintCallable, Category="战斗|命中反馈", meta=(DisplayName="Clear Combat Feedback"))
    void ClearCombatFeedback();

    UFUNCTION(BlueprintCallable, Category="战斗|命中反馈", meta=(DisplayName="Set Feedback Cinematic Bars Visible"))
    void SetFeedbackCinematicBarsVisible(bool bVisible);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗|命中反馈", meta=(DisplayName="反馈控件类"))
    TSubclassOf<UDustCombatFeedbackWidget> FeedbackWidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗|命中反馈", meta=(DisplayName="反馈显示层级"))
    int32 ViewportZOrder = 60;

    void NotifyConfirmedShot(float ActualDamage, bool bHeadshot, bool bKilled);
    void NotifyKilledByEnemy();

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UDustCombatFeedbackWidget> FeedbackWidget;
    TWeakObjectPtr<ADust2PlayerCharacter> LocalPlayer;
    bool bWasDead = false;
    bool bDeathRowAdded = false;
    bool bCinematicBarsVisible = false;
};
