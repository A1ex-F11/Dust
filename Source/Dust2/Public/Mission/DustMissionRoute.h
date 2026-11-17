#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "DustMissionRoute.generated.h"

class ADust2GameMode;
class ADust2PlayerCharacter;
class UDustEncounterComponent;
class UPrimitiveComponent;

USTRUCT(BlueprintType)
struct FDustRouteCheckpoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="任务路线")
	FName MapId;

	UPROPERTY(BlueprintReadWrite, Category="任务路线")
	FName ResumeStepId;

	UPROPERTY(BlueprintReadWrite, Category="任务路线")
	bool bRouteComplete = false;

	UPROPERTY(BlueprintReadWrite, Category="任务路线")
	TArray<FName> ConsumedPickupIds;
};

USTRUCT(BlueprintType)
struct FDustRouteDoor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="开门")
	TObjectPtr<AActor> Door;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="开门")
	FName DoorId;
};

USTRUCT(BlueprintType)
struct FDustMissionRouteStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="任务路线")
	FName StepId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="任务路线", meta=(MultiLine=true))
	FText ObjectiveText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="条件")
	TObjectPtr<AActor> Region;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="条件", meta=(ToolTip="Require a box entry in addition to clearing the region. Steps without a clear condition already require entry."))
	bool bRequireArrival = false;

	UPROPERTY(EditAnywhere, Category="条件", meta=(EditCondition="bRequireArrival || !bRequireClear", EditConditionHides, HideEditConditionToggle,
		UseComponentPicker, AllowAnyActor, AllowedClasses="/Script/Engine.BoxComponent",
		ToolTip="Leave empty to use the selected region's Route Entry Box."))
	FComponentReference EntryBox;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="条件")
	bool bRequireClear = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="动作")
	bool bSaveCheckpoint = false;

	UPROPERTY(EditAnywhere, Category="动作", meta=(EditCondition="bSaveCheckpoint", EditConditionHides, HideEditConditionToggle,
		UseComponentPicker, AllowAnyActor, AllowedClasses="/Script/Engine.ArrowComponent",
		ToolTip="Leave empty to use the selected region's Route Respawn Arrow."))
	FComponentReference RespawnArrow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="动作")
	bool bOpenDoors = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="动作", meta=(EditCondition="bOpenDoors", EditConditionHides, HideEditConditionToggle, TitleProperty="DoorId"))
	TArray<FDustRouteDoor> Doors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="动作")
	bool bActivateNextRegion = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="动作", meta=(EditCondition="bActivateNextRegion", EditConditionHides, HideEditConditionToggle))
	TObjectPtr<AActor> NextRegion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="动作")
	bool bStartC4 = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="动作", meta=(EditCondition="bStartC4", EditConditionHides, HideEditConditionToggle))
	TObjectPtr<AActor> C4Actor;

	// Keep the old serialized names so existing level actors can migrate on load.
	UPROPERTY()
	TObjectPtr<AActor> ActivateRegion;

	UPROPERTY()
	TObjectPtr<AActor> Door;

	UPROPERTY()
	FName DoorId;

	UPROPERTY()
	TObjectPtr<AActor> C4;

	bool RequiresArrival() const { return bRequireArrival || !bRequireClear; }
	void MigrateLegacyActions();
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDustRouteTextChanged, const FText&, Text);

/** Place one in each map that opts into data-driven objectives. */
UCLASS(meta=(DisplayName="Dust Mission Route"))
class DUST2_API ADustMissionRoute : public AActor
{
	GENERATED_BODY()

public:
	ADustMissionRoute();
	virtual void PostLoad() override;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="任务路线")
	TArray<FDustMissionRouteStep> Steps;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="任务路线", meta=(MultiLine=true))
	FText CompletionText;

	UPROPERTY(BlueprintAssignable, Category="任务路线")
	FDustRouteTextChanged OnRouteTextChanged;

	UFUNCTION(BlueprintCallable, Category="任务路线", meta=(AutoCreateRefTerm="Checkpoint", AdvancedDisplay="Checkpoint"))
	bool StartMissionRoute(ADust2PlayerCharacter* InPlayer, const FDustRouteCheckpoint& Checkpoint);

	/** Call on the old world before retry travel. Empty extensions preserve legacy pickup behavior. */
	UFUNCTION(BlueprintCallable, Category="任务路线")
	bool PrepareMissionRouteRetry(const FDustRouteCheckpoint& Checkpoint);

	UFUNCTION(BlueprintPure, Category="任务路线")
	FText GetCurrentRouteText() const;

	UFUNCTION(BlueprintPure, Category="任务路线")
	FName GetCurrentRouteStepId() const;

	UFUNCTION(BlueprintCallable, Category="任务路线")
	void RetryCurrentRouteStep();

	bool IsApplyingC4(const AActor* C4) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend struct FDustMissionRouteTestAccess;

	FName GetMapId() const;
	int32 ResolveResumeIndex(const FDustRouteCheckpoint& Checkpoint) const;
	bool ValidateSteps(int32 FirstActiveStep, bool bResumeOnlyC4) const;
	bool CanAdvance() const;
	bool ApplyStep(const FDustMissionRouteStep& Step);
	bool ApplyStepActions(const FDustMissionRouteStep& Step);
	void BindCurrentStep();
	void UnbindCurrentStep();
	void TryAdvance();

	UFUNCTION()
	void HandleEntered(UPrimitiveComponent* Overlapped, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);

	UFUNCTION()
	void HandleCleared();

	TWeakObjectPtr<ADust2GameMode> Mode;
	TWeakObjectPtr<ADust2PlayerCharacter> Player;
	TWeakObjectPtr<UDustEncounterComponent> BoundRegion;
	TWeakObjectPtr<UPrimitiveComponent> BoundBox;
	int32 CurrentIndex = INDEX_NONE;
	bool bArrived = false;
	bool bAwaitingC4 = false;
	bool bAdvancing = false;
	bool bEnded = false;
	TWeakObjectPtr<AActor> ApplyingC4;
};
