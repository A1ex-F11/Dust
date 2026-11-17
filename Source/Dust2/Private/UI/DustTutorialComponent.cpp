#include "UI/DustTutorialComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/WeaponInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustGameSessionSubsystem.h"
#include "Player/Dust2PlayerCharacter.h"
#include "UI/DustTutorialWidget.h"

UDustTutorialComponent::UDustTutorialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ControlsTutorial.Key = TEXT("Tutorial_Controls");
	ControlsTutorial.Title = NSLOCTEXT("DustTutorial", "ControlsTitle", "操作指南");
	ControlsTutorial.Body = NSLOCTEXT("DustTutorial", "ControlsBody",
		"W / A / S / D　移动\n鼠标移动　转动视角\n左键　开火\n按住右键　瞄准\n空格　跳跃\nE　交互或手动拾取武器\nG　丢弃当前武器\nQ　切换武器\nR　换弹");
}

void UDustTutorialComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		if (UWeaponInventoryComponent* Inventory = Owner->FindComponentByClass<UWeaponInventoryComponent>())
		{
			Inventory->OnWeaponPickupCommitted.AddUniqueDynamic(this, &UDustTutorialComponent::HandleWeaponPickup);
		}
	}
}

void UDustTutorialComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AActor* Owner = GetOwner())
	{
		if (UWeaponInventoryComponent* Inventory = Owner->FindComponentByClass<UWeaponInventoryComponent>())
		{
			Inventory->OnWeaponPickupCommitted.RemoveDynamic(this, &UDustTutorialComponent::HandleWeaponPickup);
		}
	}
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(OpenTimer);
	if (ActiveWidget) ActiveWidget->RemoveFromParent();
	ActiveWidget = nullptr;
	Pending.Reset();
	if (Reason == EEndPlayReason::Destroyed || Reason == EEndPlayReason::RemovedFromWorld)
	{
		ReleasePause();
	}
	PauseOwner.Reset();
	Super::EndPlay(Reason);
}

APlayerController* UDustTutorialComponent::FindLocalController() const
{
	const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
	APlayerController* Controller = IsValid(Player) && !Player->IsDead()
		? Cast<APlayerController>(Player->GetController()) : nullptr;
	return IsValid(Controller) && Controller->IsLocalController() ? Controller : nullptr;
}

void UDustTutorialComponent::RequestControlsTutorial()
{
	RequestTutorial(ControlsTutorial);
}

void UDustTutorialComponent::HandleWeaponPickup(TSubclassOf<AWeaponRuntime> WeaponClass)
{
	if (const FDustTutorialMessage* Message = WeaponTutorials.Find(WeaponClass))
	{
		RequestTutorial(*Message);
	}
}

void UDustTutorialComponent::RequestTutorial(const FDustTutorialMessage& Message)
{
	if (Message.Key.IsNone() || Message.Body.IsEmpty() || !TutorialWidgetClass ||
		!FindLocalController() || UGameplayStatics::GetCurrentLevelName(this, true) != TEXT("L_SciFiDemo"))
	{
		return;
	}

	const UGameInstance* Instance = GetWorld()->GetGameInstance();
	const UDustGameSessionSubsystem* Session = Instance ? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
	if (!Session || Session->HasSeenTutorial(Message.Key) ||
		Pending.ContainsByPredicate([&Message](const FDustTutorialMessage& Item) { return Item.Key == Message.Key; }))
	{
		return;
	}
	Pending.Add(Message);
	if (!ActiveWidget) ScheduleOpen();
}

void UDustTutorialComponent::ScheduleOpen()
{
	UWorld* World = GetWorld();
	if (World && !OpenTimer.IsValid())
	{
		OpenTimer = World->GetTimerManager().SetTimerForNextTick(this, &UDustTutorialComponent::TryOpenNext);
	}
}

void UDustTutorialComponent::TryOpenNext()
{
	OpenTimer.Invalidate();
	UWorld* World = GetWorld();
	APlayerController* Controller = FindLocalController();
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	UDustGameSessionSubsystem* Session = Instance ? Instance->GetSubsystem<UDustGameSessionSubsystem>() : nullptr;
	const ADust2GameMode* Mode = World ? World->GetAuthGameMode<ADust2GameMode>() : nullptr;
	if (!Controller || !Session || !TutorialWidgetClass || (Mode && Mode->HasResolvedMissionResult()))
	{
		Pending.Reset();
		ReleasePause();
		return;
	}

	Pending.RemoveAll([Session](const FDustTutorialMessage& Item) { return Session->HasSeenTutorial(Item.Key); });
	if (Pending.IsEmpty())
	{
		ReleasePause();
		return;
	}
	if (!PauseOwner.IsValid() && UGameplayStatics::IsGamePaused(this))
	{
		ScheduleOpen();
		return;
	}

	UDustTutorialWidget* Widget = CreateWidget<UDustTutorialWidget>(Controller, TutorialWidgetClass);
	if (!Widget)
	{
		Pending.Reset();
		ReleasePause();
		return;
	}

	if (!PauseOwner.IsValid())
	{
		bPreviousCursor = Controller->bShowMouseCursor;
		if (!Controller->SetPause(true))
		{
			Pending.Reset();
			return;
		}
		PauseOwner = Controller;
	}

	Widget->TutorialMessage = MoveTemp(Pending[0]);
	Pending.RemoveAt(0);
	Widget->Presenter = this;
	ActiveWidget = Widget;
	Session->MarkTutorialSeen(Widget->TutorialMessage.Key);
	Widget->AddToViewport(50);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget->TakeWidget());
	Controller->SetInputMode(InputMode);
	Controller->bShowMouseCursor = true;
}

void UDustTutorialComponent::ContinueFrom(UDustTutorialWidget* Widget)
{
	if (!IsValid(Widget) || Widget != ActiveWidget.Get()) return;
	Widget->RemoveFromParent();
	ActiveWidget = nullptr;
	TryOpenNext();
}

void UDustTutorialComponent::ReleasePause()
{
	if (APlayerController* Controller = PauseOwner.Get())
	{
		PauseOwner.Reset();
		Controller->SetPause(false);
		Controller->SetInputMode(FInputModeGameOnly());
		Controller->bShowMouseCursor = bPreviousCursor;
	}
}

void UDustTutorialWidget::ContinueTutorial()
{
	if (UDustTutorialComponent* Component = Presenter.Get())
	{
		Component->ContinueFrom(this);
	}
}
