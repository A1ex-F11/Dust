#include "UI/DustCombatFeedbackComponent.h"

#include "Player/Dust2PlayerCharacter.h"
#include "UI/DustCombatFeedbackWidget.h"
#include "UI/DynamicCrosshairWidget.h"
#include "GameFramework/PlayerController.h"

UDustCombatFeedbackComponent::UDustCombatFeedbackComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    FeedbackWidgetClass = UDustCombatFeedbackWidget::StaticClass();
}

bool UDustCombatFeedbackComponent::InitializeCombatFeedback(UDynamicCrosshairWidget* Crosshair)
{
    ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
    APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
    if (!IsValid(Player) || !IsValid(Controller) || !Controller->IsLocalController()
        || !IsValid(Crosshair) || Crosshair->GetOwningPlayerPawn() != Player)
        return false;
    if (IsValid(FeedbackWidget)) FeedbackWidget->RemoveFromParent();
    TSubclassOf<UDustCombatFeedbackWidget> WidgetClass = FeedbackWidgetClass;
    if (!WidgetClass) WidgetClass = UDustCombatFeedbackWidget::StaticClass();
    FeedbackWidget = CreateWidget<UDustCombatFeedbackWidget>(Controller, WidgetClass);
    if (!FeedbackWidget) return false;
    LocalPlayer = Player;
    bWasDead = Player->IsDead();
    bDeathRowAdded = false;
    FeedbackWidget->SetCrosshairSource(Crosshair);
    FeedbackWidget->SetCinematicBarsVisible(bCinematicBarsVisible);
    FeedbackWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
    FeedbackWidget->AddToViewport(ViewportZOrder);
    SetComponentTickEnabled(true);
    return true;
}

void UDustCombatFeedbackComponent::NotifyConfirmedShot(float ActualDamage, bool bHeadshot, bool bKilled)
{
    if (LocalPlayer.IsValid() && !LocalPlayer->IsDead() && IsValid(FeedbackWidget))
        FeedbackWidget->NotifyConfirmedHit(ActualDamage, bHeadshot, bKilled);
}

void UDustCombatFeedbackComponent::NotifyKilledByEnemy()
{
    if (!bDeathRowAdded && LocalPlayer.IsValid() && LocalPlayer->IsDead() && IsValid(FeedbackWidget))
    {
        bDeathRowAdded = true;
        bWasDead = true;
        FeedbackWidget->NotifyPlayerKilled();
    }
}

void UDustCombatFeedbackComponent::ClearCombatFeedback()
{
    if (IsValid(FeedbackWidget)) FeedbackWidget->ResetFeedback();
    bDeathRowAdded = false;
}

void UDustCombatFeedbackComponent::SetFeedbackCinematicBarsVisible(bool bVisible)
{
    bCinematicBarsVisible = bVisible;
    if (IsValid(FeedbackWidget)) FeedbackWidget->SetCinematicBarsVisible(bVisible);
}

void UDustCombatFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!LocalPlayer.IsValid() || !IsValid(FeedbackWidget)) return;
    const bool bDead = LocalPlayer->IsDead();
    if (bDead && !bWasDead) FeedbackWidget->ClearCenterFeedback();
    if (!bDead && bWasDead) ClearCombatFeedback();
    bWasDead = bDead;
}

void UDustCombatFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SetComponentTickEnabled(false);
    if (IsValid(FeedbackWidget))
    {
        FeedbackWidget->ResetFeedback();
        FeedbackWidget->RemoveFromParent();
    }
    FeedbackWidget = nullptr;
    LocalPlayer.Reset();
    Super::EndPlay(EndPlayReason);
}
