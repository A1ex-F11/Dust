
#include "WeaponPresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/AudioComponent.h"
#include "WeaponRuntime.h"

UWeaponPresentationComponent::UWeaponPresentationComponent()
{
	
	PrimaryComponentTick.bCanEverTick = false;
}
void UWeaponPresentationComponent::SetWeaponForPresentation(
	AWeaponRuntime* NewWeapon)
{
	if (!IsValid(NewWeapon))
	{
		NewWeapon = nullptr;
	}

	if (BoundWeapon.Get() == NewWeapon)
	{
		return;
	}
	if (AWeaponRuntime* OldWeapon = BoundWeapon.Get())
	{
		OldWeapon->OnFireCommitted.RemoveDynamic(
			this,
			&UWeaponPresentationComponent::HandleFireCommitted);

		OldWeapon->OnReloadStarted.RemoveDynamic(
			this,
			&UWeaponPresentationComponent::HandleReloadStarted);
	}

	BoundWeapon = NewWeapon;
	if (NewWeapon)
	{
		NewWeapon->OnFireCommitted.AddUniqueDynamic(
			this,
			&UWeaponPresentationComponent::HandleFireCommitted);

		NewWeapon->OnReloadStarted.AddUniqueDynamic(
			this,
			&UWeaponPresentationComponent::HandleReloadStarted);
	}
}
void UWeaponPresentationComponent::HandleFireCommitted()
{
	OnFirePresentationRequested.Broadcast();
}

void UWeaponPresentationComponent::HandleReloadStarted()
{
	OnReloadPresentationRequested.Broadcast();
}
void UWeaponPresentationComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	SetWeaponForPresentation(nullptr);
	BoundWeapon.Reset();

	Super::EndPlay(EndPlayReason);
}
bool UWeaponPresentationComponent::FindWeaponPresentationProfile(
	AWeaponRuntime* FormalWeapon,
	FWeaponPresentationProfile& OutProfile) const
{
	OutProfile = FWeaponPresentationProfile{};

	if (!IsValid(FormalWeapon))
	{
		return false;
	}

	for (const FWeaponPresentationProfile& Profile
		: WeaponPresentationProfiles)
	{
		if (!Profile.FormalWeaponClass)
		{
			continue;
		}

		if (FormalWeapon->IsA(Profile.FormalWeaponClass.Get()))
		{
			OutProfile = Profile;
			return true;
		}
	}

	return false;
}
AActor* UWeaponPresentationComponent::TryInitializeVisualWeapon(
	AWeaponRuntime* FormalWeapon,
	UChildActorComponent* VisualWeaponComponent)
{
	AActor* PlayerOwner = GetOwner();

	if (!IsValid(PlayerOwner) ||
		!IsValid(VisualWeaponComponent) ||
		!VisualWeaponBaseClass)
	{
		return nullptr;
	}

	FWeaponPresentationProfile Profile;
	if (!FindWeaponPresentationProfile(FormalWeapon, Profile))
	{
		return nullptr;
	}

	if (!Profile.bUseTacticalPresentation ||
		!Profile.VisualWeaponClass)
	{
		return nullptr;
	}

	if (!Profile.VisualWeaponClass->IsChildOf(
		VisualWeaponBaseClass.Get()))
	{
		return nullptr;
	}

	VisualWeaponComponent->SetChildActorClass(
		Profile.VisualWeaponClass.Get());

	AActor* VisualWeapon = VisualWeaponComponent->GetChildActor();
	if (!IsValid(VisualWeapon))
	{
		return nullptr;
	}

	VisualWeapon->SetOwner(PlayerOwner);
	VisualWeapon->SetActorTickEnabled(true);

	return VisualWeapon;
}
UAnimMontage* UWeaponPresentationComponent::ResolveWeaponFireMontage(
	AWeaponRuntime* FormalWeapon,
	UAnimMontage* DefaultFireMontage) const
{

	FWeaponPresentationProfile Profile;
	if (!FindWeaponPresentationProfile(FormalWeapon, Profile))
	{
		return DefaultFireMontage;
	}

	if (FormalWeapon->GetCurrentAmmo() == 0 &&
		IsValid(Profile.EmptyFireWeaponMontage.Get()))
	{
		return Profile.EmptyFireWeaponMontage.Get();
	}

	return DefaultFireMontage;
}
void UWeaponPresentationComponent::PlayPresentationMontage(
	USkeletalMeshComponent* Mesh,
	UAnimMontage* Montage)
{
	if (!IsValid(Mesh) || !IsValid(Montage))
	{
		return;
	}

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!IsValid(AnimInstance))
	{
		return;
	}

	AnimInstance->Montage_Play(
		Montage,
		1.0f,
		EMontagePlayReturnType::MontageLength,
		0.0f,
		true);
}
void UWeaponPresentationComponent::PlayFirePresentation(
	AWeaponRuntime* FormalWeapon,
	USkeletalMeshComponent* CharacterMesh,
	UAnimMontage* CharacterFireMontage,
	USkeletalMeshComponent* WeaponMesh,
	UAnimMontage* DefaultWeaponFireMontage)
{
	if (!IsValid(FormalWeapon) ||
		FormalWeapon != BoundWeapon.Get())
	{
		return;
	}

	UAnimMontage* WeaponFireMontage =
		ResolveWeaponFireMontage(
			FormalWeapon,
			DefaultWeaponFireMontage);

	OnRecoilPresentationRequested.Broadcast();

	PlayPresentationMontage(
		CharacterMesh,
		CharacterFireMontage);

	OnFireEffectsPresentationRequested.Broadcast();

	PlayPresentationMontage(
		WeaponMesh,
		WeaponFireMontage);
}
bool UWeaponPresentationComponent::ResolveStandardReloadPresentation(
	AWeaponRuntime* FormalWeapon,
	UAnimMontage* CharacterReloadTactical,
	UAnimMontage* CharacterReloadEmpty,
	FWeaponReloadVariant& OutPresentation) const
{
	OutPresentation = FWeaponReloadVariant{};

	FWeaponPresentationProfile Profile;
	if (!FindWeaponPresentationProfile(FormalWeapon, Profile))
	{
		return false;
	}

	if (Profile.ReloadVariants.Num() > 0)
	{
		return false;
	}

	const bool bIsEmpty = FormalWeapon->GetCurrentAmmo() == 0;

	OutPresentation.CharacterMontage = bIsEmpty
		? CharacterReloadEmpty
		: CharacterReloadTactical;

	OutPresentation.WeaponMontage = bIsEmpty
		? Profile.ReloadEmptyMontage
		: Profile.ReloadTacticalMontage;

	OutPresentation.ReloadSound = bIsEmpty
		? Profile.ReloadEmptySound
		: Profile.ReloadTacticalSound;

	return true;
}
bool UWeaponPresentationComponent::ResolveVariantReloadPresentation(
	AWeaponRuntime* FormalWeapon,
	int32 MagazineCapacity,
	FWeaponReloadVariant& OutPresentation,
	int32& OutPlannedReloadCount,
	int32& OutReloadRequestId) const
{
	OutPresentation = FWeaponReloadVariant{};
	OutPlannedReloadCount = 0;
	OutReloadRequestId = 0;

	FWeaponPresentationProfile Profile;
	if (!FindWeaponPresentationProfile(FormalWeapon, Profile))
	{
		return false;
	}

	const int32 MissingAmmo = FMath::Max(
		0, MagazineCapacity - FormalWeapon->GetCurrentAmmo());

	const int32 AvailableAmmo = FMath::Max(
		0, FormalWeapon->GetReserveAmmo());

	const int32 PlannedReloadCount = FMath::Min(
		MissingAmmo, AvailableAmmo);

	const int32 VariantIndex = PlannedReloadCount - 1;

	if (!Profile.ReloadVariants.IsValidIndex(VariantIndex))
	{
		return false;
	}

	OutPresentation = Profile.ReloadVariants[VariantIndex];
	OutPlannedReloadCount = PlannedReloadCount;
	OutReloadRequestId = FormalWeapon->GetReloadRequestId();

	return true;
}
void UWeaponPresentationComponent::PlayReloadPresentationAudio(
	UAudioComponent* AudioComponent,
	USoundBase* Sound)
{
	if (!IsValid(AudioComponent))
	{
		return;
	}

	AudioComponent->Stop();
	AudioComponent->SetSound(Sound);

	if (IsValid(Sound))
	{
		AudioComponent->Play(0.0f);
	}
}
bool UWeaponPresentationComponent::ShouldCompleteReloadPresentation(
	bool bCompletionAllowed,
	AWeaponRuntime* ReloadingWeapon,
	AWeaponRuntime* CurrentWeapon,
	int32 ExpectedReloadRequestId) const
{
	if (!bCompletionAllowed ||
		!IsValid(ReloadingWeapon) ||
		!IsValid(CurrentWeapon))
	{
		return false;
	}

	if (ReloadingWeapon != CurrentWeapon ||
		!ReloadingWeapon->IsReloading())
	{
		return false;
	}

	return ExpectedReloadRequestId > 0 &&
		ReloadingWeapon->GetReloadRequestId() ==
		ExpectedReloadRequestId;
}