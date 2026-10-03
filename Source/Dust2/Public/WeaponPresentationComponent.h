
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
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class DUST2_API UWeaponPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UWeaponPresentationComponent();
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void SetWeaponForPresentation(AWeaponRuntime* NewWeapon);
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void PlayReloadPresentationAudio(
		UAudioComponent* AudioComponent,
		USoundBase* Sound);
UFUNCTION(BlueprintPure, Category = "Weapon Presentation")
	UAnimMontage* ResolveWeaponFireMontage(
		AWeaponRuntime* FormalWeapon,
		UAnimMontage* DefaultFireMontage) const;
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void PlayFirePresentation(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* CharacterMesh,
		UAnimMontage* CharacterFireMontage,
		USkeletalMeshComponent* WeaponMesh,
		UAnimMontage* DefaultWeaponFireMontage);
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	AActor* TryInitializeVisualWeapon(
		AWeaponRuntime* FormalWeapon,
		UChildActorComponent* VisualWeaponComponent);
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void PlayPresentationMontage(
		USkeletalMeshComponent* Mesh,
		UAnimMontage* Montage);
UFUNCTION(BlueprintPure, Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Use Fast Draw"))
	bool ShouldUseFastDrawPresentation(
		const FWeaponPresentationProfile& PresentationProfile,
		bool bInitialDrawConsumed) const;
UFUNCTION(BlueprintPure, Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Found"))
	bool FindWeaponPresentationProfile(
		AWeaponRuntime* FormalWeapon,
		FWeaponPresentationProfile& OutProfile) const;
UFUNCTION(BlueprintPure, Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Found"))
	bool ResolveStandardReloadPresentation(
		AWeaponRuntime* FormalWeapon,
		UAnimMontage* CharacterReloadTactical,
		UAnimMontage* CharacterReloadEmpty,
		FWeaponReloadVariant& OutPresentation) const;
UFUNCTION(BlueprintCallable, BlueprintPure = false,
		Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Found"))
	bool ResolveVariantReloadPresentation(
		AWeaponRuntime* FormalWeapon,
		int32 MagazineCapacity,
		FWeaponReloadVariant& OutPresentation,
		int32& OutPlannedReloadCount,
		int32& OutReloadRequestId) const;
UFUNCTION(BlueprintPure, Category = "Weapon Presentation")
	bool ShouldCompleteReloadPresentation(
		bool bCompletionAllowed,
		AWeaponRuntime* ReloadingWeapon,
		AWeaponRuntime* CurrentWeapon,
		int32 ExpectedReloadRequestId) const;
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	bool TryCommitReloadPresentationRound(
		bool bCompletionAllowed,
		AWeaponRuntime* ReloadingWeapon,
		AWeaponRuntime* CurrentWeapon,
		int32 ExpectedReloadRequestId);
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Round Committed"))
	bool HandleReloadPresentationNotify(
		FName NotifyName,
		USceneComponent* AttachToComponent,
		bool bCompletionAllowed,
		AWeaponRuntime* ReloadingWeapon,
		AWeaponRuntime* CurrentWeapon,
		int32 ExpectedReloadRequestId);
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void StopReloadPresentationSounds();
UFUNCTION(BlueprintPure, Category = "Weapon Presentation")
	bool HasConsumedInitialDraw(AWeaponRuntime* FormalWeapon) const;
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void MarkInitialDrawConsumed(AWeaponRuntime* FormalWeapon);
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Started"))
	bool BeginFastDrawTransition(AWeaponRuntime* FormalWeapon, float Delay);
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void CancelFastDrawTransition();
UFUNCTION(BlueprintPure, Category = "Weapon Presentation")
	bool IsFastDrawTransitionPending() const;
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void PrepareDrawPresentationWatch();
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
	void InterruptDrawPresentationWatch();
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Watching"))
	bool WatchDrawPresentationCompletion(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* CharacterMesh,
		UAnimMontage* CharacterMontage,
		USkeletalMeshComponent* WeaponMesh,
		UAnimMontage* WeaponMontage,
		bool bFinalStage);
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FDrawPresentationFinishedEvent OnDrawPresentationFinished;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnFastDrawTransitionRequested;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnFirePresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnReloadPresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnRecoilPresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnFireEffectsPresentationRequested;
UFUNCTION(BlueprintCallable, Category = "Weapon Presentation",
		meta = (ReturnDisplayName = "Started"))
	bool StartReloadWeaponPresentation(
		AWeaponRuntime* FormalWeapon,
		USkeletalMeshComponent* WeaponMesh,
		UAnimMontage* Montage);

UFUNCTION(BlueprintCallable, Category = "Weapon Presentation")
    void CancelReloadWeaponPresentation(bool bStopMontage = true);
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FReloadWeaponPresentationEvent OnReloadRoundPresentationCommitted;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FReloadWeaponPresentationEvent OnReloadWeaponPresentationCompleted;
protected:

UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "Weapon Presentation|Configuration",
		meta = (DisplayName = "视觉武器基类"))
	TSubclassOf<AActor> VisualWeaponBaseClass;
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "Weapon Presentation|Configuration")
	TArray<FWeaponPresentationProfile> WeaponPresentationProfiles;

	virtual void EndPlay(
		const EEndPlayReason::Type EndPlayReason) override;
UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "Weapon Presentation|Configuration",
		meta = (ClampMin = "0.0", DisplayName = "拔枪切换提前放行秒数"))
	float DrawSwitchReleaseLeadTime = 0.3f;
private:

UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> BoundWeapon;
UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AWeaponRuntime>> InitialDrawConsumedWeapons;
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

	UFUNCTION()
	void HandleReloadWeaponNotifyBegin(
		FName NotifyName,
		const FBranchingPointNotifyPayload& Payload);
UFUNCTION()
	void HandleFireCommitted();
UFUNCTION()
	void HandleReloadStarted();
FTimerHandle FastDrawTransitionTimer;
UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponRuntime> FastDrawTransitionWeapon;
	bool bFastDrawTransitionPending = false;
	uint64 FastDrawTransitionSerial = 0;
	void HandleFastDrawTransitionTimer();
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
};
