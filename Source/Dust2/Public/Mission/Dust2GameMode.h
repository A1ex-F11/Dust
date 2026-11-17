#pragma once
#include"CoreMinimal.h"
#include"GameFramework/GameModeBase.h"
#include"Mission/MissionState.h"
#include"Mission/MissionResult.h"
#include"Mission/DustMissionRetryAction.h"
#include "Mission/DustMissionRoute.h"
#include"Dust2GameMode.generated.h"

class ADust2PlayerCharacter;
class UChildActorComponent;
class UWeaponInventoryComponent;
struct FDustNativeRouteCheckpoint;
class UDustEncounterPersistenceSubsystem;
class UDustSciFiObjectiveTextData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMissionChanged,
	EMissionState,
	NewState
);
UCLASS(meta=(DisplayName="Dust2游戏模式"))
class DUST2_API ADust2GameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ADust2GameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	UFUNCTION(BlueprintPure, Category="任务路线")
	bool HasPendingNativeMissionCheckpoint() const;

	UFUNCTION(BlueprintCallable, Category="任务路线")
	bool TryRetryNativeMissionCheckpoint();

	bool RestoreNativeRouteLoadout(ADust2PlayerCharacter* Player, UWeaponInventoryComponent* Inventory,
		UChildActorComponent* Slot1, UChildActorComponent* Slot2);
	void MarkNativeRoutePlayerReady(ADust2PlayerCharacter* Player);
	bool IsNativeRoutePlayerReady(const ADust2PlayerCharacter* Player) const;

	bool RegisterMissionRoute(ADustMissionRoute* Route);
	void UnregisterMissionRoute(const ADustMissionRoute* Route);

	UFUNCTION(BlueprintPure, Category="任务路线")
	bool HasActiveMissionRoute() const { return MissionRoute.IsValid(); }

	UFUNCTION(BlueprintImplementableEvent, Category="任务路线")
	bool TryOpenRouteDoor(AActor* Door, FName DoorId);

	UFUNCTION(BlueprintImplementableEvent, Category="任务路线")
	void RequestRouteC4(AActor* C4);
	UPROPERTY(BlueprintAssignable,Category="任务")
	FOnMissionChanged OnMissionChanged;

	UPROPERTY(BlueprintAssignable, Category="任务")
	FOnMissionChanged OnSciFiObjectiveChanged;

	UPROPERTY(EditDefaultsOnly, Category="任务", meta=(DisplayName="SciFi 任务文案"))
	TObjectPtr<UDustSciFiObjectiveTextData> SciFiObjectiveTextData;

	UFUNCTION(BlueprintPure, Category="任务", meta=(DisplayName="Native Get SciFi Objective Text"))
	bool NativeGetSciFiObjectiveText(FText& Text) const;

	void NotifySciFiObjectiveStep(int32 Step, const FText& Text);
	bool CanActivateSciFiObjectiveStep(int32 Step) const;
	UFUNCTION(BlueprintPure,Category="任务")
	EMissionState GetMissionState() const;
	UFUNCTION(BlueprintCallable,Category="任务")
	bool TrySetMissionState(EMissionState NewState);
	UFUNCTION(BlueprintCallable,Category="任务")
	void RestoreMissionState(EMissionState SavedState);
	UFUNCTION(BlueprintCallable,Category="任务")
	bool TryResolveMissionResult(EMissionResult Result);
	UFUNCTION(BlueprintPure, Category="任务")
	bool HasResolvedMissionResult() const { return bResultResolved; }
	/** Raw result; check HasResolvedMissionResult before treating the default Defused as a win. */
	UFUNCTION(BlueprintPure, Category="任务")
	EMissionResult GetMissionResult() const { return MissionResult; }

	/**
	 * Read-only retry branch. Unresolved (including the constructor's Defused placeholder) is
	 * Unavailable; a resolved Defused victory chooses a new difficulty; resolved failure keeps
	 * the checkpoint when one exists, otherwise it restarts. The Blueprint clears or reloads
	 * after branching. This call never changes mission state.
	 */
	UFUNCTION(BlueprintPure, Category="任务", meta=(DisplayName="获取任务重试方式"))
	EDustMissionRetryAction GetMissionRetryAction(bool bHasCheckpoint) const;

	UFUNCTION(BlueprintCallable, Category="任务", meta=(DisplayName="Native Notify B Reached"))
	void NativeNotifyBReached();
	UFUNCTION(BlueprintCallable, Category="任务", meta=(DisplayName="Native Notify B Cleared"))
	void NativeNotifyBCleared();
	UFUNCTION(BlueprintCallable, Category="任务", meta=(DisplayName="Native Check B Complete"))
	bool NativeCheckBComplete();
	UFUNCTION(BlueprintPure, Category="任务")
	bool NativeHasReachedB() const { return bNativeReachedB; }
	UFUNCTION(BlueprintPure, Category="任务")
	bool NativeHasClearedB() const { return bNativeBCleared; }

	// The caller activates its Blueprint C4 only when this request succeeds.
	UFUNCTION(BlueprintCallable, Category="任务", meta=(DisplayName="Native Try Plant C4"))
	bool NativeTryPlantC4(AActor* C4Actor, bool bHasCheckpoint);
	/**
	 * Resolves a planted-C4 explosion first, then applies lethal damage through the existing
	 * player ApplyDamage path. A null player may still resolve without damage; a player from
	 * another world rejects the request before any resolution. bDiedNow is an output for the
	 * Blueprint failure UI, which remains Blueprint-driven.
	 */
	UFUNCTION(BlueprintCallable, Category="任务", meta=(DisplayName="Native Resolve Bomb Explosion"))
	bool NativeResolveBombExplosion(ADust2PlayerCharacter* Player, bool& bDiedNow);
	UFUNCTION(BlueprintCallable, Category="任务", meta=(DisplayName="Native Reset Mission Progress"))
	void NativeResetMissionProgress();

	UFUNCTION(BlueprintCallable, Category="检查点", meta=(DisplayName="Native Register Enemy Death"))
	void NativeRegisterEnemyDeath(FName EnemyId);
	UFUNCTION(BlueprintCallable, Category="检查点", meta=(DisplayName="Native Restore Dead Enemy IDs"))
	void NativeRestoreDeadEnemyIds(const TArray<FName>& SavedIds);
	UFUNCTION(BlueprintPure, Category="检查点", meta=(DisplayName="Native Get Dead Enemy IDs"))
	TArray<FName> NativeGetDeadEnemyIds() const { return NativeDeadEnemyIds; }
protected:
	UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="任务")
	EMissionState MissionState;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "任务")
	bool bResultResolved;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "任务")
	EMissionResult MissionResult;

private:
	TWeakObjectPtr<ADustMissionRoute> MissionRoute;
	TWeakObjectPtr<ADust2PlayerCharacter> NativeRouteRestoredPlayer;
	bool bNativeRouteTravelStarted = false;
	FName GetNativeRouteMapId() const;
	const FDustNativeRouteCheckpoint* GetPendingNativeRouteCheckpoint() const;
	bool HasNativeMissionCheckpoint() const;
	bool IsSciFiDemo() const;
	UDustEncounterPersistenceSubsystem* GetEncounterPersistence() const;
	UPROPERTY(Transient)
	bool bNativeReachedB = false;
	UPROPERTY(Transient)
	bool bNativeBCleared = false;
	UPROPERTY(Transient)
	TArray<FName> NativeDeadEnemyIds;
};
