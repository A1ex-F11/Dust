
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponPresentationTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "TimerManager.h"
#include "WeaponPresentationComponent.generated.h"

class AWeaponRuntime;
class UChildActorComponent;
class USkeletalMeshComponent;
class USceneComponent;
class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWeaponPresentationSignal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FReloadWeaponPresentationEvent,
	AWeaponRuntime*, FormalWeapon,
	int32, ReloadRequestId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FDrawPresentationFinishedEvent, bool, CompletedNormally);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FPresentationSwitchCommitEvent,
	AWeaponRuntime*, FormalWeapon);
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="武器表现组件") )
class DUST2_API UWeaponPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:	
	UWeaponPresentationComponent();
	// Read-only estimate from the actual watched final draw instances; never grants readiness.
	bool GetDrawRemainingForWeapon(AWeaponRuntime* ExpectedWeapon, float& OutSeconds) const;
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void SetWeaponForPresentation(AWeaponRuntime* NewWeapon);
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void PlayReloadPresentationAudio(
		UAudioComponent* AudioComponent,
		USoundBase* Sound);
UFUNCTION(BlueprintPure, Category = "武器|表现")
	UAnimMontage* ResolveWeaponFireMontage(
		AWeaponRuntime* FormalWeapon,
		UAnimMontage* DefaultFireMontage) const;
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void PlayFirePresentation(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* CharacterMesh,
		UAnimMontage* CharacterFireMontage,
		USkeletalMeshComponent* WeaponMesh,
		UAnimMontage* DefaultWeaponFireMontage);
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	AActor* TryInitializeVisualWeapon(
		AWeaponRuntime* FormalWeapon,
		UChildActorComponent* VisualWeaponComponent);
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void PlayPresentationMontage(
		USkeletalMeshComponent* Mesh,
		UAnimMontage* Montage);
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "播放成功"))
	bool TryPlayPresentationMontage(
		USkeletalMeshComponent* Mesh,
		UAnimMontage* Montage,
		float StartTime = 0.0f);
UFUNCTION(BlueprintPure, Category = "武器|表现",
		meta = (ReturnDisplayName = "使用快速拔枪"))
	bool ShouldUseFastDrawPresentation(
		const FWeaponPresentationProfile& PresentationProfile,
		bool bInitialDrawConsumed) const;
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void PlayFastHolsterPresentation(
		USkeletalMeshComponent* CharacterMesh,
		const FWeaponPresentationProfile& Profile);
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "已接受"))
	bool BeginPresentationSwitchCommit(
		AWeaponRuntime* FormalWeapon,
		float Delay);
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void CancelPresentationSwitchCommit(bool bNotifyCancelled = true);
UFUNCTION(BlueprintPure, Category = "武器|表现",
		meta = (ReturnDisplayName = "已找到"))
	bool FindWeaponPresentationProfile(
		AWeaponRuntime* FormalWeapon,
		FWeaponPresentationProfile& OutProfile) const;
UFUNCTION(BlueprintPure, Category = "武器|表现",
		meta = (ReturnDisplayName = "已找到"))
	bool ResolveStandardReloadPresentation(
		AWeaponRuntime* FormalWeapon,
		UAnimMontage* CharacterReloadTactical,
		UAnimMontage* CharacterReloadEmpty,
		FWeaponReloadVariant& OutPresentation) const;
UFUNCTION(BlueprintCallable, BlueprintPure = false,
		Category = "武器|表现",
		meta = (ReturnDisplayName = "已找到"))
	bool ResolveVariantReloadPresentation(
		AWeaponRuntime* FormalWeapon,
		int32 MagazineCapacity,
		FWeaponReloadVariant& OutPresentation,
		int32& OutPlannedReloadCount,
		int32& OutReloadRequestId) const;
UFUNCTION(BlueprintPure, Category = "武器|表现")
	bool ShouldCompleteReloadPresentation(
		bool bCompletionAllowed,
		AWeaponRuntime* ReloadingWeapon,
		AWeaponRuntime* CurrentWeapon,
		int32 ExpectedReloadRequestId) const;
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	bool TryCommitReloadPresentationRound(
		bool bCompletionAllowed,
		AWeaponRuntime* ReloadingWeapon,
		AWeaponRuntime* CurrentWeapon,
		int32 ExpectedReloadRequestId);
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "单发已提交"))
	bool HandleReloadPresentationNotify(
		FName NotifyName,
		USceneComponent* AttachToComponent,
		bool bCompletionAllowed,
		AWeaponRuntime* ReloadingWeapon,
		AWeaponRuntime* CurrentWeapon,
		int32 ExpectedReloadRequestId);
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void StopReloadPresentationSounds();
UFUNCTION(BlueprintPure, Category = "武器|表现")
	bool HasConsumedInitialDraw(AWeaponRuntime* FormalWeapon) const;
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void MarkInitialDrawConsumed(AWeaponRuntime* FormalWeapon);
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "已开始"))
	bool BeginFastDrawTransition(AWeaponRuntime* FormalWeapon, float Delay);
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void CancelFastDrawTransition();
UFUNCTION(BlueprintPure, Category = "武器|表现")
	bool IsFastDrawTransitionPending() const;
UFUNCTION(BlueprintPure, Category = "武器|表现")
	bool CanStartReloadPresentation(
		AWeaponRuntime* FormalWeapon,
		bool bPresentationSwitching) const;
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void PrepareDrawPresentationWatch();
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void InterruptDrawPresentationWatch();
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "已开始监听"))
	bool WatchDrawPresentationCompletion(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* CharacterMesh,
		UAnimMontage* CharacterMontage,
		USkeletalMeshComponent* WeaponMesh,
		UAnimMontage* WeaponMontage,
		bool bFinalStage);
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "已开始"))
	bool StartFullHolsterPresentation(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* CharacterMesh,
		USkeletalMeshComponent* WeaponMesh,
		const FWeaponPresentationProfile& Profile);
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void CancelFullHolsterPresentation();
UFUNCTION(BlueprintPure, Category = "武器|表现")
	bool IsFullHolsterPresentationActive() const;
UFUNCTION(BlueprintCallable, Category = "武器|表现")
	void CancelReloadWeaponPresentation(bool bStopMontage = true);
UFUNCTION(BlueprintCallable, BlueprintPure = false,
		Category = "武器|表现",
		meta = (ReturnDisplayName = "已启用"))
	bool TryActivateVisualWeapon(
		AWeaponRuntime* FormalWeapon,
		UChildActorComponent* VisualWeaponComponent,
		AActor*& OutVisualWeapon,
		FWeaponPresentationProfile& OutProfile);
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FWeaponPresentationSignal OnFullHolsterPresentationReady;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FWeaponPresentationSignal OnFullHolsterPresentationCancelled;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FDrawPresentationFinishedEvent OnDrawPresentationFinished;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FWeaponPresentationSignal OnFastDrawTransitionRequested;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FWeaponPresentationSignal OnFirePresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FWeaponPresentationSignal OnReloadPresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FWeaponPresentationSignal OnRecoilPresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FWeaponPresentationSignal OnFireEffectsPresentationRequested;
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "已开始"))
	bool StartSynchronizedReloadPresentation(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* CharacterMesh,
		UAnimMontage* CharacterMontage,
		USkeletalMeshComponent* WeaponMesh,
		UAnimMontage* WeaponMontage,
		float PlayRate = 1.0f);
UFUNCTION(BlueprintCallable, Category = "武器|表现",
		meta = (ReturnDisplayName = "已开始"))
	bool StartReloadWeaponPresentation(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* WeaponMesh,
		UAnimMontage* Montage);
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "武器|表现|配置",
		meta = (DisplayName = "完整收枪完成通知名"))
	FName FullHolsterFinishedNotifyName = TEXT("FullHolsterFinished");
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FReloadWeaponPresentationEvent OnReloadRoundPresentationCommitted;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FReloadWeaponPresentationEvent OnReloadWeaponPresentationCompleted;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FPresentationSwitchCommitEvent OnPresentationSwitchCommitRequested;
UPROPERTY(BlueprintAssignable, Category = "武器|表现")
	FPresentationSwitchCommitEvent OnPresentationSwitchCommitCancelled;
protected:
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "武器|表现|配置",
		meta = (DisplayName = "视觉武器基类"))
	TSubclassOf<AActor> VisualWeaponBaseClass;
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "武器|表现|配置", meta=(DisplayName="武器表现配置"))
	TArray<FWeaponPresentationProfile> WeaponPresentationProfiles;
	virtual void EndPlay(
		const EEndPlayReason::Type EndPlayReason) override;
UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "武器|表现|配置",
		meta = (ClampMin = "0.0", DisplayName = "拔枪切换提前放行秒数"))
	float DrawSwitchReleaseLeadTime = 0.3f;
private:
UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> BoundWeapon;
UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> ReloadNotifyAudioComponents;
UPROPERTY(Transient)
	TWeakObjectPtr<UAudioComponent> ActiveReloadAudioComponent;
UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> ActiveReloadWeapon;
UPROPERTY(Transient)
	TWeakObjectPtr<UAnimInstance> ActiveReloadAnimInstance;
UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMeshComponent> ActiveReloadWeaponMesh;
	int32 ActiveReloadMontageInstanceId = INDEX_NONE;
	int32 ActiveReloadWeaponRequestId = 0;
	uint64 ReloadWeaponPlaybackSerial = 0;
	bool IsReloadWeaponPlaybackCurrent() const;
	void ReleaseReloadWeaponPlayback(bool bStopMontage);
	TWeakObjectPtr<AWeaponRuntime> ActiveSynchronizedReloadWeapon;
	int32 ActiveSynchronizedReloadRequestId = 0;
	TWeakObjectPtr<UAnimInstance> SynchronizedReloadAnimInstances[2];
	TWeakObjectPtr<UAnimMontage> SynchronizedReloadMontages[2];
	int32 SynchronizedReloadInstanceIds[2] = { INDEX_NONE, INDEX_NONE };
	FOnMontageEnded SynchronizedReloadPreviousEndDelegates[2];
	bool bSynchronizedReloadLayerPending[2] = { false, false };
	bool bSynchronizedReloadPresentationActive = false;
	bool bSynchronizedReloadPresentationLaunching = false;
	uint64 SynchronizedReloadPlaybackSerial = 0;
	bool IsSynchronizedReloadPlaybackCurrent() const;
	bool PlaySynchronizedReloadLayer(
		int32 Layer,
		UAnimInstance* AnimInstance,
		UAnimMontage* Montage,
		float PlayRate,
		uint64 PlaybackSerial);
	void ReleaseSynchronizedReloadPlayback(
		bool bStopMontages,
		bool bCancelFormalReload);
	void HandleSynchronizedReloadLayerEnded(
		uint64 PlaybackSerial,
		int32 Layer,
		int32 InstanceId,
		UAnimMontage* EndedMontage,
		bool bInterrupted);
UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AWeaponRuntime>> InitialDrawConsumedWeapons;
FTimerHandle FastDrawTransitionTimer;
UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> FastDrawTransitionWeapon;
	bool bFastDrawTransitionPending = false;
	uint64 FastDrawTransitionSerial = 0;
	void HandleFastDrawTransitionTimer();
UFUNCTION()
	void HandleFireCommitted();
UFUNCTION()
	void HandleReloadStarted();
UFUNCTION()
	void HandleReloadWeaponNotifyBegin(
		FName NotifyName,
		const FBranchingPointNotifyPayload& Payload);
TWeakObjectPtr<AWeaponRuntime> WatchedDrawWeapon;
TWeakObjectPtr<UAnimInstance> DrawWatchAnimInstances[2];
	int32 DrawWatchInstanceIds[2] = { INDEX_NONE, INDEX_NONE };
FOnMontageEnded DrawWatchPreviousEndDelegates[2];
FTimerHandle DrawSwitchReleaseTimer;
float ActiveDrawSwitchLeadTime = 0.0f;
bool TryGetDrawRemainingTime(float& OutSeconds) const;
void StartEarlyDrawSwitchRelease();
void ArmEarlyDrawSwitchRelease(float Delay);
void HandleEarlyDrawSwitchRelease(uint64 Serial);
void ClearEarlyDrawSwitchRelease();
	bool bDrawWatchLayerPending[2] = { false, false };
	uint64 DrawWatchSerial = 0;
	bool bDrawWatchFinalStage = false;
	bool bDrawWatchFailed = false;
	bool WatchDrawLayer(int32 Layer, USkeletalMeshComponent* Mesh,
		UAnimMontage* Montage);
	void HandleDrawLayerEnded(uint64 Serial, int32 Layer,
		int32 InstanceId, bool bInterrupted);
	void TryFinishDrawPresentationWatch();
TWeakObjectPtr<AWeaponRuntime> ActiveFullHolsterWeapon;
TWeakObjectPtr<UAnimInstance> FullHolsterAnimInstances[2];
	int32 FullHolsterMontageInstanceIds[2] = { INDEX_NONE, INDEX_NONE };
FOnMontageEnded FullHolsterPreviousEnded[2];
	bool bFullHolsterLayerCompleted[2] = { false, false };
	bool bFullHolsterActive = false;
	bool bFullHolsterLaunching = false;
	uint64 FullHolsterSerial = 0;
	void ReleaseFullHolsterPresentation(bool bStopMontages);
	void TryFinishFullHolsterPresentation();
	bool IsFullHolsterPresentationCurrent() const;
	bool PlayFullHolsterLayer(
		int32 Layer,
		USkeletalMeshComponent* Mesh,
		UAnimMontage* Montage);
	void HandleFullHolsterNotify(
		int32 Layer,
		FName NotifyName,
		const FBranchingPointNotifyPayload& Payload);
	void HandleFullHolsterLayerEnded(
		uint64 Serial,
		int32 Layer,
		int32 InstanceId,
		bool bInterrupted);
UFUNCTION()
	void HandleFullHolsterCharacterNotifyBegin(
		FName NotifyName,
		const FBranchingPointNotifyPayload& Payload);
UFUNCTION()
	void HandleFullHolsterWeaponNotifyBegin(
		FName NotifyName,
		const FBranchingPointNotifyPayload& Payload);
FTimerHandle PresentationSwitchCommitTimer;
TWeakObjectPtr<AWeaponRuntime> ExpectedPresentationSwitchWeapon;
	bool bPresentationSwitchCommitPending = false;
	uint64 PresentationSwitchCommitSerial = 0;
	void HandlePresentationSwitchCommitTimer(uint64 Serial);
TWeakObjectPtr<AWeaponRuntime> ReloadBlockingDrawWeapon;
TWeakObjectPtr<UAnimInstance> ReloadBlockingDrawAnimInstances[2];
	int32 ReloadBlockingDrawInstanceIds[2] = { INDEX_NONE, INDEX_NONE };
	void ClearReloadBlockingDrawSnapshot();
};


