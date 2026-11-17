#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Mission/DustEnemyDifficulty.h"
#include "Mission/DustMissionRoute.h"
#include "Mission/MissionState.h"
#include "Weapons/FireMode.h"
#include "DustGameSessionSubsystem.generated.h"

class AWeaponRuntime;
class UDustEnemyCombatProfile;
class UWorld;

USTRUCT()
struct FDustRouteWeaponSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<AWeaponRuntime> WeaponClass;

	int32 CurrentAmmo = 0;
	int32 ReserveAmmo = 0;
	EFireMode FireMode = EFireMode::SemiAutomatic;
	bool bInitialDrawConsumed = false;
};

USTRUCT()
struct FDustNativeRouteCheckpoint
{
	GENERATED_BODY()

	UPROPERTY()
	FDustRouteWeaponSnapshot Slot1;

	UPROPERTY()
	FDustRouteWeaponSnapshot Slot2;

	FDustRouteCheckpoint Route;
	FName CheckpointId;
	FTransform SpawnTransform = FTransform::Identity;
	float Health = 0.0f;
	bool bSlot1Active = false;
	EMissionState MissionState = EMissionState::NotStarted;
	TArray<FName> DeadEnemyIds;
};

/**
 * Run-local difficulty selection. It stores only data-asset references, never level actors.
 * A Blueprint selects a difficulty through BeginNewSession after the target world already
 * exists; the subsystem then applies that profile to the current world's existing enemies.
 * Travel, mission state, pickups, ammo and checkpoint snapshots stay with their existing owners.
 */
UCLASS(BlueprintType, meta=(DisplayName="Dust 游戏会话"))
class DUST2_API UDustGameSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Explicitly sets all three difficulty assets. While a session is active, only the exact
	 * same triple is accepted as an idempotent call; any different triple is rejected unchanged.
	 * A null slot is allowed and makes BeginNewSession fail for that difficulty.
	 */
	UFUNCTION(BlueprintCallable, Category="难度", meta=(DisplayName="配置三档难度"))
	bool ConfigureDifficultyProfiles(
		UDustEnemyCombatProfile* Easy,
		UDustEnemyCombatProfile* Normal,
		UDustEnemyCombatProfile* Hard);

	/**
	 * Starts one difficulty session. Only valid while no session is active and the selected
	 * profile is configured. It does not open a level, clear Blueprint data or start the mission.
	 */
	UFUNCTION(BlueprintCallable, Category="难度", meta=(DisplayName="开始新会话"))
	bool BeginNewSession(EDustEnemyDifficulty Difficulty);

	/**
	 * Leaves the active session so the victory flow can return to the selection page. The last
	 * selected difficulty and all three configured assets are kept. Normal failure retries must
	 * not call this.
	 */
	UFUNCTION(BlueprintCallable, Category="难度", meta=(DisplayName="结束会话返回难度选择"))
	void EndSessionForDifficultySelection();

	UFUNCTION(BlueprintPure, Category="难度")
	EDustEnemyDifficulty GetSelectedDifficulty() const { return SelectedDifficulty; }

	UFUNCTION(BlueprintPure, Category="难度")
	UDustEnemyCombatProfile* GetSelectedCombatProfile() const { return SelectedCombatProfile.Get(); }

	UFUNCTION(BlueprintPure, Category="难度")
	bool IsSessionActive() const { return bSessionActive; }

	UFUNCTION(BlueprintPure, Category="难度")
	UDustEnemyCombatProfile* GetDifficultyProfile(EDustEnemyDifficulty Difficulty) const;

	bool HasSeenTutorial(FName Key) const { return SeenTutorials.Contains(Key); }
	void MarkTutorialSeen(FName Key) { if (!Key.IsNone()) SeenTutorials.Add(Key); }

	const FDustNativeRouteCheckpoint* GetRouteCheckpoint(FName MapId) const;
	const FDustNativeRouteCheckpoint* GetPendingRouteCheckpoint(FName MapId) const;
	void SaveRouteCheckpoint(FDustNativeRouteCheckpoint&& Checkpoint);
	void SetRouteRetryPending(bool bPending) { bRouteRetryPending = bPending; }
	void ResetRouteCheckpoint();

private:
	UPROPERTY(Transient)
	FDustNativeRouteCheckpoint RouteCheckpoint;

	bool bRouteRetryPending = false;
	TSet<FName> SeenTutorials;

	UPROPERTY(EditAnywhere, Category="难度", meta=(DisplayName="简单难度配置"))
	TObjectPtr<UDustEnemyCombatProfile> EasyProfile;

	UPROPERTY(EditAnywhere, Category="难度", meta=(DisplayName="普通难度配置"))
	TObjectPtr<UDustEnemyCombatProfile> NormalProfile;

	UPROPERTY(EditAnywhere, Category="难度", meta=(DisplayName="困难难度配置"))
	TObjectPtr<UDustEnemyCombatProfile> HardProfile;

	UPROPERTY(Transient)
	EDustEnemyDifficulty SelectedDifficulty = EDustEnemyDifficulty::Normal;

	UPROPERTY(Transient)
	TObjectPtr<UDustEnemyCombatProfile> SelectedCombatProfile;

	UPROPERTY(Transient)
	bool bSessionActive = false;

	/** Applies the active selection to every enemy weapon component already in World. */
	int32 ApplySelectedProfileToWorldEnemies(UWorld* World);
};
