#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "DustEncounterComponent.generated.h"

class AActor;
class APawn;
class UDustEnemyLootProfile;
class UWeaponDefinitionData;
class ADust2GameMode;
class UArrowComponent;
class UBoxComponent;
class UDustEncounterPersistenceSubsystem;
class UDustEnemyPatrolComponent;
struct FPropertyChangedEvent;

USTRUCT(BlueprintType)
struct DUST2_API FDustDeferredEnemySpawnEntry
{
	GENERATED_BODY()

	/** Serialized identity, also written to the spawned pawn's EnemyId by BP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|敌人", meta=(DisplayName="稳定条目 ID"))
	FName EntryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|敌人", meta=(DisplayName="Pawn 类"))
	TSubclassOf<APawn> PawnClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|敌人", meta=(DisplayName="生成点"))
	TObjectPtr<AActor> SpawnPoint;

	/** Ordered loop. C++ writes these to the pawn's Dust Enemy Patrol component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|敌人", meta=(DisplayName="巡逻点（按顺序）"))
	TArray<TObjectPtr<AActor>> PatrolPoints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|敌人", meta=(DisplayName="所持枪械配置"))
	TObjectPtr<UWeaponDefinitionData> WeaponDefinition;

	// Saved entries with an old asset selection keep it until manually overridden.
	UPROPERTY()
	TObjectPtr<UDustEnemyLootProfile> LootProfile;

	UPROPERTY(EditAnywhere, Category="延迟区域|掉落", meta=(DisplayName="手动配置掉落"))
	bool bOverrideLoot = false;

	UPROPERTY(EditAnywhere, Category="延迟区域|掉落", meta=(EditCondition="bOverrideLoot", ClampMin="0.0", ClampMax="100.0", DisplayName="武器掉率 (%)"))
	float WeaponDropChance = 50.0f;

	UPROPERTY(EditAnywhere, Category="延迟区域|掉落", meta=(EditCondition="bOverrideLoot", ClampMin="0.0", ClampMax="100.0", DisplayName="医疗包掉率 (%)"))
	float HealthDropChance = 25.0f;

	UPROPERTY(EditAnywhere, Category="延迟区域|掉落", meta=(EditCondition="bOverrideLoot", ClampMin="1", DisplayName="最少奖励弹量"))
	int32 MinimumTotalAmmo = 5;

	UPROPERTY(EditAnywhere, Category="延迟区域|掉落", meta=(EditCondition="bOverrideLoot", ClampMin="1", DisplayName="最多奖励弹量"))
	int32 MaximumTotalAmmo = 15;

	UPROPERTY(EditAnywhere, Category="延迟区域|掉落", meta=(EditCondition="bOverrideLoot", DisplayName="医疗包拾取类"))
	TSubclassOf<AActor> HealthPickupClass;

	UPROPERTY(EditAnywhere, Category="延迟区域|掉落", meta=(EditCondition="bOverrideLoot", DisplayName="本地掉落偏移"))
	FVector LocalDropOffset = FVector(60.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="延迟区域|敌人", meta=(DisplayName="覆盖护甲配置"))
	bool bOverrideArmor = false;

	UPROPERTY(EditAnywhere, Category="延迟区域|敌人", meta=(EditCondition="bOverrideArmor", DisplayName="有身体护甲"))
	bool bHasBodyArmor = false;

	UPROPERTY(EditAnywhere, Category="延迟区域|敌人", meta=(EditCondition="bOverrideArmor", DisplayName="有头盔"))
	bool bHasHelmet = false;
};

USTRUCT(BlueprintType)
struct DUST2_API FDustDeferredSupplySpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|补给", meta=(DisplayName="稳定条目 ID"))
	FName EntryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|补给", meta=(DisplayName="Actor 类"))
	TSubclassOf<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域|补给", meta=(DisplayName="生成点"))
	TObjectPtr<AActor> SpawnPoint;
};

USTRUCT(BlueprintType)
struct DUST2_API FDustDeferredRegionConfig
{
	GENERATED_BODY()

	/** Unique in this map; this is the key stored in the GameInstance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域", meta=(DisplayName="区域 ID"))
	FName RegionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域", meta=(DisplayName="敌人生成条目"))
	TArray<FDustDeferredEnemySpawnEntry> EnemySpawns;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="延迟区域", meta=(DisplayName="预设补给条目"))
	TArray<FDustDeferredSupplySpawnEntry> SupplySpawns;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDustEncounterCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustConfigureDeferredPawn, APawn*, SpawnedPawn, FName, StableId);

/** Tracks membership; level Blueprints decide what a cleared encounter means. */
UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent, DisplayName="Dust Encounter"))
class DUST2_API UDustEncounterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustEncounterComponent();
	static const FDustDeferredEnemySpawnEntry* FindInitializingEnemyEntry(AActor* Pawn);

	UPROPERTY(EditAnywhere, Category="遭遇|任务路线", meta=(UseComponentPicker, AllowedClasses="/Script/Engine.BoxComponent"))
	FComponentReference RouteEntryBox;

	UPROPERTY(EditAnywhere, Category="遭遇|任务路线", meta=(UseComponentPicker, AllowedClasses="/Script/Engine.ArrowComponent"))
	FComponentReference RouteRespawnArrow;

	UBoxComponent* GetRouteEntryBox() const;
	UArrowComponent* GetRouteRespawnArrow() const;
	bool EnsureRouteRegionActive();
	bool RestoreRouteCheckpointEnemies(const TArray<FName>& DeadEnemyIds, bool bRequiresClear);

	UFUNCTION(BlueprintPure, Category="遭遇|延迟区域", meta=(DisplayName="Native Get Deferred Armor Override"))
	bool NativeGetDeferredArmorOverride(AActor* Pawn, bool& HasBodyArmor, bool& HasHelmet) const;

	UPROPERTY(BlueprintAssignable, Category="遭遇")
	FDustEncounterCleared OnNativeEncounterCleared;

	/**
	 * Opt-in. False keeps the original pre-placed one-shot initialization exactly
	 * as before. True delays all membership work until Native Activate Deferred
	 * Region (or Native Restore Deferred Region).
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="遭遇|延迟区域", meta=(DisplayName="启用延迟区域生成"))
	bool bEnableDeferredRegion = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="遭遇|延迟区域",
		meta=(EditCondition="bEnableDeferredRegion", DisplayName="区域配置"))
	FDustDeferredRegionConfig DeferredRegion;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="遭遇|任务",
		meta=(ClampMin="0", ClampMax="5", DisplayName="SciFi 战区编号"))
	int32 SciFiObjectiveStep = 0;

	UPROPERTY(EditInstanceOnly, Category="遭遇|任务", meta=(MultiLine=true, DisplayName="SciFi 战区提示"))
	FText SciFiObjectiveText;

	// Initialize once from the owning Blueprint's instance-editable configuration.
	UFUNCTION(BlueprintCallable, Category="遭遇", meta=(DisplayName="Native Initialize Encounter"))
	bool NativeInitializeEncounter(const TArray<AActor*>& Enemies);
	UFUNCTION(BlueprintCallable, Category="遭遇", meta=(DisplayName="Native Notify Enemy Died"))
	bool NativeNotifyEnemyDied(AActor* Enemy);

    void NotifyNearestEnemiesOfPlayerKill(
        AActor* Player, const FVector& PlayerLocation, const FVector& VictimLocation, uint64 DeathSerial);
	UFUNCTION(BlueprintCallable, Category="遭遇", meta=(DisplayName="Native Remove Enemy Silently"))
	bool NativeRemoveEnemySilently(AActor* Enemy);
	UFUNCTION(BlueprintPure, Category="遭遇", meta=(DisplayName="Native Is Encounter Cleared"))
	bool NativeIsEncounterCleared() const { return bInitialized && bEncounterCleared; }
	UFUNCTION(BlueprintPure, Category="遭遇", meta=(DisplayName="Native Get Remaining Enemy Count"))
	int32 NativeGetRemainingEnemyCount() const { return RemainingEnemies.Num(); }

	UFUNCTION(BlueprintPure, Category="遭遇|延迟区域", meta=(DisplayName="Native Is Deferred Region Enabled"))
	bool NativeIsDeferredRegionEnabled() const { return bEnableDeferredRegion; }

	/** Spawns this region's entries once and initializes the tracker. */
	UFUNCTION(BlueprintCallable, Category="遭遇|延迟区域", meta=(DisplayName="Native Activate Deferred Region"))
	bool NativeActivateDeferredRegion();

	/**
	 * Called explicitly after the checkpoint's dead EnemyIds were restored into
	 * GameMode. It only acts for regions recorded as successfully activated and
	 * silently skips dead entry ids.
	 */
	UFUNCTION(BlueprintCallable, Category="遭遇|延迟区域", meta=(DisplayName="Native Restore Deferred Region"))
	bool NativeRestoreDeferredRegion();

	UFUNCTION(BlueprintPure, Category="遭遇|延迟区域", meta=(DisplayName="Native Is Deferred Region Activated"))
	bool NativeIsDeferredRegionActivated() const;

	UFUNCTION(BlueprintPure, Category="遭遇|延迟区域", meta=(DisplayName="Native Get Deferred Region Id"))
	FName NativeGetDeferredRegionId() const { return DeferredRegion.RegionId; }

	/** Explicit editor repair; activation never generates ids at runtime. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="遭遇|延迟区域", meta=(DisplayName="Native Ensure Spawn Entry Ids"))
	bool NativeEnsureSpawnEntryIds();

	/** Tracker/Blueprint read path for the shared identity component. */
	UFUNCTION(BlueprintPure, Category="遭遇", meta=(DisplayName="Native Get Actor Stable Id"))
	FName NativeGetActorStableId(AActor* Actor) const;

	/** BP_Encounter assigns the pawn's existing EnemyId and EncounterRef before BeginPlay. */
	UPROPERTY(BlueprintAssignable, Category="遭遇|延迟区域")
	FDustConfigureDeferredPawn OnConfigureDeferredPawn;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;

private:
	bool InitializeEncounterInternal(const TArray<AActor*>& Enemies, bool bFromDeferredActivation);
	bool ValidateDeferredRegion(FString& OutReason) const;
	bool ValidateWorldUniqueness(FString& OutReason) const;
	bool SpawnDeferredRegionActors(const TSet<FName>& DeadEnemyIds,
		TArray<AActor*>& OutEnemies, TArray<AActor*>& OutSupplies);
	friend class UDustEnemyPatrolComponent;
	void ConfigureDeferredPatrol(AActor* Pawn, UDustEnemyPatrolComponent& Patrol) const;
	void WarnIfAIDidNotStart(APawn* Pawn) const;
	void DestroySpawnedActors(TArray<AActor*>& Actors);
	void NormalizeSpawnEntryIds(bool bRegenerateRegion, bool bRegenerateEntries);
	bool IsEditorConfigContext() const;

	FName GetPersistenceLevelId() const;
	UDustEncounterPersistenceSubsystem* GetPersistence() const;
	ADust2GameMode* FindGameMode() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> RemainingEnemies;
	bool bInitialized = false;
	bool bEncounterCleared = false;
	bool bEndedPlay = false;
	bool bRegionActivated = false;
	bool bRegionOperationInProgress = false;

	friend struct FDustEncounterTestAccess;
	UDustEncounterPersistenceSubsystem* PersistenceOverride = nullptr;
};
