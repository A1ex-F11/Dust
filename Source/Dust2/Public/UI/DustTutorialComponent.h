#pragma once

#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "UI/DustTutorialMessage.h"
#include "DustTutorialComponent.generated.h"

class APlayerController;
class AWeaponRuntime;
class UDustTutorialWidget;

UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent, DisplayName="Dust Tutorial"))
class DUST2_API UDustTutorialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustTutorialComponent();

	UPROPERTY(EditAnywhere, Category="Tutorial")
	TSubclassOf<UDustTutorialWidget> TutorialWidgetClass;

	UPROPERTY(EditAnywhere, Category="Tutorial")
	FDustTutorialMessage ControlsTutorial;

	UPROPERTY(EditAnywhere, Category="Tutorial")
	TMap<TSubclassOf<AWeaponRuntime>, FDustTutorialMessage> WeaponTutorials;

	UFUNCTION(BlueprintCallable, Category="Tutorial")
	void RequestControlsTutorial();

	void RequestTutorial(const FDustTutorialMessage& Message);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	friend class UDustTutorialWidget;

	UFUNCTION()
	void HandleWeaponPickup(TSubclassOf<AWeaponRuntime> WeaponClass);

	APlayerController* FindLocalController() const;
	void ScheduleOpen();
	void TryOpenNext();
	void ContinueFrom(UDustTutorialWidget* Widget);
	void ReleasePause();

	UPROPERTY(Transient)
	TArray<FDustTutorialMessage> Pending;

	UPROPERTY(Transient)
	TObjectPtr<UDustTutorialWidget> ActiveWidget;

	TWeakObjectPtr<APlayerController> PauseOwner;
	FTimerHandle OpenTimer;
	bool bPreviousCursor = false;
};
