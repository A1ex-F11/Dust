
#include "WeaponPresentationComponent.h"
#include "Components/ChildActorComponent.h"
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