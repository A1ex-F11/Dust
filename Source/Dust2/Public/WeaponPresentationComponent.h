
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponPresentationTypes.h"
#include "WeaponPresentationComponent.generated.h"

class AWeaponRuntime;
class UChildActorComponent;
class USkeletalMeshComponent;
class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWeaponPresentationSignal);
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
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnFirePresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnReloadPresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnRecoilPresentationRequested;
UPROPERTY(BlueprintAssignable, Category = "Weapon Presentation")
	FWeaponPresentationSignal OnFireEffectsPresentationRequested;

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
UFUNCTION()
	void HandleFireCommitted();
UFUNCTION()
	void HandleReloadStarted();
};
