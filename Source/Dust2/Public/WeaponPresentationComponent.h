
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponPresentationTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotify.h"
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
};
