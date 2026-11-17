#pragma once

#include "Blueprint/UserWidget.h"
#include "UI/DustTutorialMessage.h"
#include "DustTutorialWidget.generated.h"

class UDustTutorialComponent;

UCLASS(Abstract, Blueprintable, meta=(DisplayName="Dust Tutorial Widget"))
class DUST2_API UDustTutorialWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="Tutorial")
	FDustTutorialMessage TutorialMessage;

	UFUNCTION(BlueprintCallable, Category="Tutorial")
	void ContinueTutorial();

private:
	friend class UDustTutorialComponent;
	TWeakObjectPtr<UDustTutorialComponent> Presenter;
};
