#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Components/ActorComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "DustEnemyCombatAdapterComponent.generated.h"

namespace DustEnemyVisibility { struct FSightQuery; }

class AAIController;
class APawn;
class UAIPerceptionComponent;
class UCharacterMovementComponent;
class UPathFollowingComponent;
class UStateTreeAIComponent;
class UDustEnemyWeaponComponent;
struct FPathFollowingResult;

UENUM(BlueprintType, meta=(DisplayName="敌人AI停止原因"))
enum class EDustEnemyAIStopReason : uint8
{
	EnemyDeath UMETA(DisplayName = "敌人死亡"),
	LostControl UMETA(DisplayName = "失去控制"),
	MissionEnd UMETA(DisplayName = "任务结束"),
	EndPlay UMETA(DisplayName = "结束播放"),
	ExternalRequest UMETA(DisplayName = "外部请求")
};

/** Internal ownership result for one Enemy Create Space task entry. */
enum class EDustCreateSpaceStatus : uint8 { Running, Succeeded, Failed };

struct FDustEnemyDecisionState
{
 AActor* ConfirmedTarget = nullptr;
 bool bHasKnownTarget = false;
 bool bHasConfirmedTarget = false;
 bool bNeedsReacquire = false;
 bool bCanCreateSpace = false;
};

/** Controller-side target authority. StateTree owns combat; this component owns only
 * perception handoff, the existing last-known-location search, short target knowledge,
 * and the optional one-shot spacing move. Add to AIC_Enemy, then initialize after
 * muzzle/weapon/damage setup. */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent, DisplayName="Dust Enemy Combat Adapter Component"))
class DUST2_API UDustEnemyCombatAdapterComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDustEnemyCombatAdapterComponent();
	/** Refreshes captured movement settings after the session changes an existing enemy's profile. */
	void RefreshCombatProfileSettings();
	// Default preserves the existing player-only filter; set BP_Dust2Player to narrow it further.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|目标", meta=(DisplayName="目标类")) TSubclassOf<AActor> TargetClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|搜索", meta=(ClampMin="0", DisplayName="搜索到达容差", ToolTip="移动到最后已知目标位置时使用的到达容差，单位厘米。")) float SearchAcceptanceRadius = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|搜索", meta=(ClampMin="0", DisplayName="搜索等待时间")) float SearchWaitSeconds = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|搜索", meta=(ClampMin="0.01", DisplayName="搜索重试间隔")) float SearchRetrySeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|搜索", meta=(ClampMin="0.01", Units="cm/s", DisplayName="搜索奔跑速度", ToolTip="失去视野或听到枪声后奔向冻结线索位置的临时最大行走速度；普通脚步调查不受影响。")) float SearchRunSpeed = 450.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|搜索", meta=(ClampMin="0.01", Units="cm/s", DisplayName="搜索接近步行速度", ToolTip="进入减速距离后只切换一次的临时最大行走速度；普通脚步调查不受影响。")) float SearchWalkSpeed = 200.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|搜索", meta=(ClampMin="0", Units="cm", DisplayName="搜索减速距离", ToolTip="与最后已知或枪声线索位置的水平距离不大于该值时切到步行速度；实际阈值不小于到达容差。")) float SearchSlowdownDistance = 300.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|搜索", meta=(ClampMin="0.1", Units="s", DisplayName="搜索总时限", ToolTip="正式进入最后位置搜索后的总时限，包含移动、重试和等待；重试不会延长。")) float SearchMaxSeconds = 12.0f;


    // Hearing only supplies a frozen location clue, never a confirmed weapon target.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|听觉", meta=(DisplayName="启用声音调查"))
    bool bEnableHearingInvestigation = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|听觉", meta=(DisplayName="调查总时限", Units="s", ClampMin="0.1", ToolTip="包含移动、重试和等待；新脚步不会延长。"))
    float HearingMaxSeconds = 12.0f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category="AI|听觉", meta=(DisplayName="线索更新间隔", Units="s", ClampMin="0.1"))
    float HearingUpdateInterval = 0.75f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category="AI|听觉", meta=(DisplayName="线索更新距离", Units="m", ClampMin="0.1", ToolTip="新事件位置变化至少这么远才改道；此字段直接以米保存。"))
    float HearingUpdateDistanceMeters = 2.0f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category="AI|听觉", meta=(DisplayName="最多改道次数", ClampMin="0", ClampMax="10"))
    int32 HearingMaxRedirects = 2;
    UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category="AI|听觉", meta=(DisplayName="调查结束冷却", Units="s", ClampMin="0"))
    float HearingCooldownSeconds = 2.0f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category="AI|听觉", meta=(DisplayName="线索有效年龄", Units="s", ClampMin="0.01"))
    float HearingMaxStimulusAge = 1.5f;

	/** Returns readiness, not proof that the StateTree asset can run. No probe StartLogic. */
	UFUNCTION(BlueprintCallable, Category="AI|初始化") bool InitializeCombatAI(UDustEnemyWeaponComponent* Weapon);
	/** Permanent for this Pawn life. Idempotent; never resets ammo or checkpoint bookkeeping. */
	UFUNCTION(BlueprintCallable, Category="AI|生命周期") void StopCombatAI(EDustEnemyAIStopReason Reason);
	/** Legacy-known target (confirmed or remembered during the loss grace). Use
	 * GetConfirmedTarget for anything that may chase/fire/aim. */
	UFUNCTION(BlueprintPure, Category="AI|目标") AActor* GetTarget() const;
	/** The only actor a weapon may aim/fire at. Null while the adapter is remembering. */
	AActor* GetConfirmedTarget() const;
	/** Legal known target: confirmed now, or retained by the short loss grace/reload deferral. */
	AActor* GetKnownTarget() const;
	bool HasKnownTarget() const;
	bool HasConfirmedTarget(DustEnemyVisibility::FSightQuery* Query = nullptr) const;
	FDustEnemyDecisionState ReadDecisionState() const;
	/** True only while a real sight loss grace is keeping the known target alive. */
	bool NeedsReacquire() const;
	bool IsConfirmedTarget(const AActor* Actor, DustEnemyVisibility::FSightQuery* Query = nullptr) const;
	/** One-shot fast reacquire for the same target identity inside the profile window. */
	bool ConsumeQuickReacquire(AActor* Target);
	uint64 GetKnowledgeGeneration() const { return KnowledgeGeneration; }
	AAIController* GetAIController() const;
    bool TryGetShareableSightLocation(const AActor* Player, FVector& OutLocation) const;
    void ReceiveSquadKillAlert(AActor* Player, const FVector& Location, uint64 DeathSerial);

	UFUNCTION(BlueprintPure, Category="AI|状态") bool IsInitialized() const { return bInitialized && !bStopped; }
	UFUNCTION(BlueprintPure, Category="AI|搜索") bool IsSearching() const { return bSearching && IsInitialized(); }
	/** Life current, not interrupting, a legal known target is present and this Pawn is not searching. */
	UFUNCTION(BlueprintPure, Category="AI|状态") bool CanRunCombatLogic() const;
	/** Reload-only life gate: also permits the owned CreateSpace task to finish a reload. */
	bool CanRunReloadLogic() const;
	/** True while a reload-driven sight loss is deferred; used by the weapon to keep fire suppressed. */
	UFUNCTION(BlueprintPure, Category="AI|状态") bool HasDeferredReloadSightLoss() const { return bDeferredReloadSightLoss; }
	/** Patrol is allowed only outside combat/search/spacing and during this live Pawn life. */
	UFUNCTION(BlueprintPure, Category="AI|状态") bool CanRunIdleLogic() const;
	UFUNCTION(BlueprintPure, Category="AI|搜索") FVector GetLastKnownTargetLocation() const { return LastKnownLocation; }

	// ---- Enemy Create Space (independent StateTree task ownership) ----
	/** Pure query for the StateTree condition. Never starts a move or changes state. */
	bool CanCreateSpace() const;
	bool IsCreateSpaceActive() const { return ActiveSpacingToken != 0; }
	/** Starts exactly one short MoveTo, temporarily owns MaxWalkSpeed, and starts the same
	 * confirmed target's weapon engagement so the move can fire while retreating. Returns a
	 * token owned by one task entry, or 0. */
	uint64 BeginCreateSpace();
	bool CanCreateSpaceForReload() const;
	uint64 BeginCreateSpaceForReload();
	EDustCreateSpaceStatus UpdateCreateSpace(uint64 Token, float DeltaTime);
	/** Releases a succeeded move (aborts only its stale request id) and applies the shared cooldown.
	 * The winning state keeps the existing engagement so a short retreat does not restart reaction. */
	void CompleteCreateSpace(uint64 Token);
	/** Releases and aborts only this token's request; applies the shared cooldown and stops the
	 * engagement that this spacing owned. */
	void CancelCreateSpace(uint64 Token);
	/** True only while this spacing token owns the given actor as its firing target. */
	bool IsSpacingTarget(const AActor* Actor) const;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|调试") FString LastDiagnostic;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|调试") EDustEnemyAIStopReason LastStopReason = EDustEnemyAIStopReason::ExternalRequest;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend struct FDustEnemyHearingTestAccess;
	friend struct FDustEnemyCombatAdapterTestAccess;
	friend struct FDustEnemyReloadTestAccess;
	friend struct FDustEnemyCombatAwarenessTestAccess;
	friend struct FDustEnemySightTestAccess;
	friend struct FDustEnemyReloadRetreatTestAccess;
	friend struct FDustEnemySearchMovementTestAccess;
	friend struct FDustEnemyCombatStrafeTestAccess;
	friend struct FDustDifficultySessionTestAccess;
	TWeakObjectPtr<AAIController> Controller;
	TWeakObjectPtr<APawn> BoundPawn;
	TWeakObjectPtr<APawn> StoppedPawn;
	TWeakObjectPtr<UDustEnemyWeaponComponent> WeaponComponent;
	TWeakObjectPtr<UStateTreeAIComponent> Tree;
	TWeakObjectPtr<UAIPerceptionComponent> Perception;
	TWeakObjectPtr<UPathFollowingComponent> PathFollowing;
	/** Legal known target. It may be unconfirmed while the loss grace/reload deferral is active. */
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<AActor> SearchTarget;
	TWeakObjectPtr<AActor> QuickReacquireTarget;
	FVector LastKnownLocation = FVector::ZeroVector;
	FTimerHandle SearchTimer;
	FAIRequestID SearchMoveId;
	/** Sight-loss search deadline only; gunshot hearing keeps its own deadline and never uses this. */
	float SearchDeadline = 0.0f;
	// Temporary search movement ownership, restored only while our applied value is live.
	TWeakObjectPtr<UCharacterMovementComponent> SearchMovement;
	float SearchSavedMaxWalkSpeed = 0.0f;
	float SearchAppliedMaxWalkSpeed = 0.0f;
	bool bSearchSpeedApplied = false;
	bool bSearchSpeedSlowed = false;
	bool bSearchSpeedOverridden = false;
	FDelegateHandle MoveFinishedHandle;
	uint64 Generation = 0;
	/** Invalidates task ownership when identity or confirmation state changes. */
	uint64 KnowledgeGeneration = 0;
	bool bInitialized = false;
	bool bStopped = false;
	bool bInterrupting = false;
	bool bSearching = false;
    bool bHearingSearch = false;
    int32 HearingSearchPriority = 0;
    uint64 LastSquadAlertSerial = 0;
    float HearingDeadline = 0.0f;
    float NextHearingUpdateAt = 0.0f;
    float NextHearingAllowedAt = 0.0f;
    float HearingListenStartTime = 0.0f;
    int32 HearingRedirectCount = 0;
	bool bRetryUsed = false;
	bool bIssuingMove = false;
	// Sight loss that happened only because this Pawn is retreating/reloading; search is deferred.
	bool bDeferredReloadSightLoss = false;
	FVector DeferredReloadLossLocation = FVector::ZeroVector;
	FAIRequestID ImmediateMoveId;
	bool bImmediateMoveSucceeded = false;

	// Short target knowledge. Only a real confirmation changes LastKnownLocation/LastConfirmedTime.
	bool bSightCurrentlySensed = false;
	bool bConfirmedTarget = false;
	bool bInLossGrace = false;
	float GraceStartTime = 0.0f;
	float LastConfirmedTime = 0.0f;
	float QuickReacquireExpiry = 0.0f;
	bool bQuickReacquireAvailable = false;

	// One Enemy Create Space entry. Token/generation/request id all have to match.
	uint64 SpacingGeneration = 0;
	uint64 ActiveSpacingToken = 0;
	FAIRequestID SpacingMoveId;
	FAIRequestID ImmediateSpacingMoveId;
	FVector SpacingDestination = FVector::ZeroVector;
	TWeakObjectPtr<AActor> SpacingTarget;
	float SpacingElapsedSeconds = 0.0f;
	float NextSpacingAllowedAt = 0.0f;
	EDustCreateSpaceStatus SpacingStatus = EDustCreateSpaceStatus::Failed;
	bool bCreateSpaceActive = false;
	bool bSpacingIsCombatSpread = false;
	bool bSpacingIsCombatStrafe = false;
	bool bIssuingSpacingMove = false;
	bool bImmediateSpacingSucceeded = false;
	// Temporary movement ownership. Only restored when the live value is still ours, so a
	// concurrent Blueprint/other system change is never overwritten.
	TWeakObjectPtr<UCharacterMovementComponent> SpacingMovement;
	float SpacingSavedMaxWalkSpeed = 0.0f;
	float SpacingAppliedMaxWalkSpeed = 0.0f;
	bool bSpacingSpeedApplied = false;
	TWeakObjectPtr<AAIController> SpacingStrafeController;
	bool bSpacingSavedAllowStrafe = false;
	bool bSpacingStrafeApplied = false;
	TWeakObjectPtr<UCharacterMovementComponent> AvoidanceMovement;
	float SavedAvoidanceRadius = 0.0f;
	float AppliedAvoidanceRadius = 0.0f;
	bool bSavedAvoidanceEnabled = false;
	bool bAvoidanceApplied = false;
	void ApplyLocalAvoidance();
	void RestoreLocalAvoidance();
	bool IsSpacingSegmentClear(const FVector& Start, const FVector& End, float Radius, float HalfHeight) const;
	friend struct FDustEnemyAvoidanceTestAccess;

	bool Fail(const FString& Message);
	uint64 BeginCreateSpaceInternal(bool bDuringReload);
	void EnsureKnownTargetTreeRunning();
	float NextTreeRecoveryAt = 0.0f;
	bool bHasStartedTree = false;
	bool IsLifeCurrent() const;
	bool IsEligibleTarget(AActor* Actor) const;
	bool IsMissionOver() const;
	bool IsReloadBusy() const;
	bool IsSearchCurrent(uint64 Serial) const;
	bool HasPhysicalLineOfSight(const AActor* Actor, DustEnemyVisibility::FSightQuery* Query = nullptr) const;
	bool CanConfirmKnownTarget(DustEnemyVisibility::FSightQuery* Query = nullptr) const;
	void Acquire(AActor* Actor, const FVector& Location);
	void Lose(AActor* Actor, const FVector& Location, bool bEliminated);
	void AcquireAlreadyVisibleTarget();
	void HandleHearing(AActor* Actor, const FAIStimulus& Stimulus);
    void ApplyInvestigation(AActor* Actor, const FVector& Location, float Now, int32 Priority);
    bool CanRedirectHearing(AActor* Actor, const FVector& EventLocation, float Now, int32 IncomingPriority = 1) const;
	void ResumeIdleLogic();
	// Invalidates timers/request identity BEFORE synchronous engine abort callbacks.
	bool InterruptActivity();
	void ClearSearch();
	void ClearKnowledgeState();
	void ClearQuickReacquire();
	void ConfirmKnownTarget(AActor* Actor, const FVector& Location);
	void BeginLossGrace(AActor* Actor, const FVector& Location);
	void Unbind();
	void StartSearchMove(uint64 Serial);
	void ResolveSearchMove(uint64 Serial, bool bSucceeded);
	void FinishSearch(uint64 Serial);
	/** Sight-loss search or a gunshot-priority hearing investigation; plain footsteps stay normal. */
	bool SearchUsesMovementSpeed() const;
	/** Captures the controlled CharacterMovement and applies the scoped search speed. */
	bool ApplySearchMovementSpeed(bool bNearGoal);
	/** One-way run-to-walk switch while a sight-loss or gunshot search move is still active. */
	void UpdateSearchMovementSpeed();
	/** Restores only our applied value; a newer external speed is left untouched. */
	void RestoreSearchMovementSpeed();
	float GetSearchSlowdownDistance() const;
	void HandleMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
	bool FindCreateSpaceDestination(FVector& OutDestination) const;
	struct FCombatNeighbor
	{
		FVector Location = FVector::ZeroVector;
		FVector ReservedLocation = FVector::ZeroVector;
		float Radius = 0.0f;
		uint32 StableId = 0;
		bool bHasReservation = false;
	};
	TArray<FCombatNeighbor> GatherCombatNeighbors(const FVector& Center, float Radius) const;
	bool ShouldCombatSpread() const;
	bool ShouldCombatSpread(bool bTargetConfirmed) const;
	/** Read-only admission: confirmed, outside the close trigger, inside the existing weapon range. */
	bool ShouldCombatStrafe(bool bTargetConfirmed) const;
	bool CanCreateSpace(bool bTargetConfirmed) const;
	bool FindCombatSpreadDestination(FVector& OutDestination) const;
	/** One short lateral destination: tries PreferredSide, then the exact opposite side once. */
	bool FindCombatStrafeDestination(const FVector& PreferredSide, float StepDistance,
		FVector& OutDestination) const;
	friend struct FDustEnemyCombatSpreadTestAccess;
	/** One-shot step toward the desired distance, never above the configured single-move cap. */
	static float ComputeCreateSpaceStepDistance(float CurrentDistance, float DesiredDistance, float MaxStepDistance);
	/** Finite, positive, ordered per-move lateral distance range. */
	static void NormalizeCombatStrafeDistanceRange(float MinDistance, float MaxDistance,
		float& OutMinDistance, float& OutMaxDistance);
	/** Pawn->target XY perpendicular; bRightSide picks +right, otherwise the exact opposite. */
	static FVector ComputeCombatStrafeDirection(const FVector& PawnLocation,
		const FVector& TargetLocation, bool bRightSide);
	/** Saves the real CharacterMovement value, applies this spacing's speed and restores only our value. */
	bool ApplySpacingMovementSpeed(bool bForCombatSpread = false, bool bForCombatStrafe = false);
	void RestoreSpacingMovementSpeed();
	void ReleaseSpacing(bool bAbortMove, bool bApplyCooldown, bool bKeepEngagement = false);
	void ResolveSpacingMove(bool bSucceeded);
	/** Strafe samples its 1-2s (configured) pause once here; other modes keep their cooldown. */
	void ApplySpacingCooldown(bool bForCombatSpread = false, bool bForCombatStrafe = false);
	UFUNCTION() void HandlePerception(AActor* Actor, FAIStimulus Stimulus);
	UFUNCTION() void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);
	UFUNCTION() void HandlePawnDestroyed(AActor* Actor);
	UFUNCTION() void HandleTargetDestroyed(AActor* Actor);
};
