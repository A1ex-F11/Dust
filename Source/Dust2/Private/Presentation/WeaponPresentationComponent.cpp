
#include "Presentation/WeaponPresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Templates/UnrealTemplate.h"
#include "Weapons/WeaponRuntime.h"
#include "Presentation/WeaponPresentationData.h"
#include "DustFireAudioPolicy.h"
#include "Presentation/ShotTracerComponent.h"

UWeaponPresentationComponent::UWeaponPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}
void UWeaponPresentationComponent::SetWeaponForPresentation(
	AWeaponRuntime* NewWeapon)
{
	if (bResettingPresentationSwitchState)
	{
		return;
	}

	if (!IsValid(NewWeapon))
	{
		NewWeapon = nullptr;
	}

	// A different formal weapon or an unbind is a newer presentation owner and must
	// invalidate the previous visual session. The active native binder's own direct
	// call is the exception: it already advanced the session serial at entry.
	const bool bOwnedByActiveBinding =
		bNativeVisualBindingSetWeaponCall && !bInsideSetWeaponForPresentation;
	const uint64 SetWeaponEntrySerial = VisualBindingSerial;
	const bool bBindingChanges = !NewWeapon || BoundWeapon.Get() != NewWeapon;
	const uint64 SetWeaponWorkSerial = SetWeaponEntrySerial +
		((bBindingChanges && !bOwnedByActiveBinding) ? 1 : 0);
	TGuardValue<bool> SetWeaponReentrancyGuard(
		bInsideSetWeaponForPresentation, true);

	if (bBindingChanges)
	{
		if (bOwnedByActiveBinding)
		{
			ReleaseTrackedVisualWeapon(true);
		}
		else
		{
			InvalidateVisualWeaponBinding(true);
		}
	}

	if (VisualBindingSerial != SetWeaponWorkSerial)
	{
		// A callback issued a newer reset/clear/weapon change while this direct
		// binder call was clearing the previous visual. Do not overwrite it.
		return;
	}

	if (BoundWeapon.Get() == NewWeapon)
	{
		// An explicit unbind still clears any stale native switch session.
		if (!NewWeapon)
		{
			bPresentationSwitchingNative = false;
			FullHolsterReadyWeapon.Reset();
		}
		return;
	}

	if (!NewWeapon)
	{
		// Old Blueprint kept switching latched across a valid weapon swap but
		// cleared it when no presentation weapon remained bound.
		bPresentationSwitchingNative = false;
		FullHolsterReadyWeapon.Reset();
	}

	// A ready token belongs to the previous bound weapon even when both the
	// old and the new binding are valid.
	FullHolsterReadyWeapon.Reset();

	ClearReloadBlockingDrawSnapshot();
	CancelPresentationSwitchCommit(false);
	ReleaseFullHolsterPresentation(true);
	if (VisualBindingSerial != SetWeaponWorkSerial)
	{
		return;
	}
	PrepareDrawPresentationWatch();
	CancelFastDrawTransition();
	CancelReloadWeaponPresentation();
	if (VisualBindingSerial != SetWeaponWorkSerial)
	{
		return;
	}
	ClearNativeGunPushback();
	if (AWeaponRuntime* OldWeapon = BoundWeapon.Get())
	{
		OldWeapon->ResetRecoilForWeaponChange();
        OldWeapon->OnFireCommitted.RemoveDynamic(
			this,
			&UWeaponPresentationComponent::HandleFireCommitted);

		OldWeapon->OnReloadStarted.RemoveDynamic(
			this,
			&UWeaponPresentationComponent::HandleReloadStarted);
	}

	NewWeapon = IsValid(NewWeapon) ? NewWeapon : nullptr;
	if (!NewWeapon) bPresentationSwitchingNative = false;
	BoundWeapon = NewWeapon;
	if (NewWeapon)
	{
		NewWeapon->ResetRecoilForWeaponChange();
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
	if (bResettingPresentationSwitchState) return;
	OnFirePresentationRequested.Broadcast();
}

void UWeaponPresentationComponent::HandleReloadStarted()
{
	if (bResettingPresentationSwitchState) return;
	ClearNativeGunPushback();
	CancelPresentationSwitchCommit(true);
	CancelFullHolsterPresentation();
	CancelFastDrawTransition();
	InterruptDrawPresentationWatch();
	CancelReloadWeaponPresentation();
	OnReloadPresentationRequested.Broadcast();
}
void UWeaponPresentationComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearNativeGunPushback();
	bPresentationSwitchingNative = false;
	FullHolsterReadyWeapon.Reset();
	ClearReloadBlockingDrawSnapshot();
	CancelPresentationSwitchCommit(false);
	ReleaseFullHolsterPresentation(true);
	PrepareDrawPresentationWatch();
	CancelFastDrawTransition();
	CancelReloadWeaponPresentation();
	InvalidateVisualWeaponBinding(false);
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

	const bool bFound = UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
		FormalWeapon->WeaponDefinition.Get(), FormalWeapon->GetClass(), WeaponPresentationProfiles, OutProfile);
	// Keep legacy Blueprint branches that inspect the returned array length consistent with the switch.
	// Only the output copy is cleared; toggling off never deletes the asset's configured variants.
	if (bFound && !OutProfile.bUseCountBasedReload) OutProfile.ReloadVariants.Reset();
	return bFound;
}
AActor* UWeaponPresentationComponent::TryInitializeVisualWeapon(
	AWeaponRuntime* FormalWeapon,
	UChildActorComponent* VisualWeaponComponent)
{
	if (bResettingPresentationSwitchState) return nullptr;
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

	if (!Profile.VisualWeaponClass)
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
static UAnimMontage* SelectWeaponFireMontage(const FWeaponPresentationProfile& Profile,
	int32 RemainingAmmo, UAnimMontage* DefaultFireMontage)
{
	if (RemainingAmmo == 0 &&
		IsValid(Profile.EmptyFireWeaponMontage.Get()))
	{
		return Profile.EmptyFireWeaponMontage.Get();
	}

	return IsValid(Profile.WeaponFireMontage.Get()) ? Profile.WeaponFireMontage.Get() : DefaultFireMontage;
}

UAnimMontage* UWeaponPresentationComponent::ResolveWeaponFireMontage(
	AWeaponRuntime* FormalWeapon,
	UAnimMontage* DefaultFireMontage) const
{
	if (!IsValid(FormalWeapon)) return DefaultFireMontage;

	const FWeaponPresentationProfile* Profile =
		UWeaponPresentationData::FindPlayerProfileWithLegacy(
			FormalWeapon->WeaponDefinition.Get(),
			FormalWeapon->GetClass(),
			WeaponPresentationProfiles);
	if (!Profile) return DefaultFireMontage;

	return SelectWeaponFireMontage(
		*Profile, FormalWeapon->GetCurrentAmmo(), DefaultFireMontage);
}

void UWeaponPresentationComponent::PlayPresentationMontage(
	USkeletalMeshComponent* Mesh,
	UAnimMontage* Montage)
{
	if (bResettingPresentationSwitchState || !IsValid(Mesh) || !IsValid(Montage))
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
bool UWeaponPresentationComponent::TryPlayPresentationMontage(
	USkeletalMeshComponent* Mesh,
	UAnimMontage* Montage,
	float StartTime)
{
	if (bResettingPresentationSwitchState || !IsValid(Mesh) || !IsValid(Montage)
		|| !FMath::IsFinite(StartTime))
	{
		return false;
	}

	const float Length = Montage->GetPlayLength();
	if (!FMath::IsFinite(Length) || Length <= 0.0f
		|| StartTime < 0.0f || StartTime >= Length)
	{
		return false;
	}

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!IsValid(AnimInstance))
	{
		return false;
	}

	const float PlayedLength = AnimInstance->Montage_Play(
		Montage,
		1.0f,
		EMontagePlayReturnType::MontageLength,
		StartTime,
		true);
	return FMath::IsFinite(PlayedLength) && PlayedLength > 0.0f;
}
void UWeaponPresentationComponent::PlayFirePresentation(
	AWeaponRuntime* FormalWeapon,
	USkeletalMeshComponent* CharacterMesh,
	UAnimMontage* CharacterFireMontage,
	USkeletalMeshComponent* WeaponMesh,
	UAnimMontage* DefaultWeaponFireMontage)
{
	if (bResettingPresentationSwitchState || !IsValid(FormalWeapon) ||
		FormalWeapon != BoundWeapon.Get())
	{
		return;
	}

	UAnimMontage* WeaponFireMontage = DefaultWeaponFireMontage;
	// Copy only the selected animation pointers before presentation callbacks can change the profile.
	if (const FWeaponPresentationProfile* Profile =
		UWeaponPresentationData::FindPlayerProfileWithLegacy(
			FormalWeapon->WeaponDefinition.Get(),
			FormalWeapon->GetClass(),
			WeaponPresentationProfiles))
	{
		WeaponFireMontage = SelectWeaponFireMontage(
			*Profile, FormalWeapon->GetCurrentAmmo(), DefaultWeaponFireMontage);
		if (IsValid(Profile->CharacterFireMontage.Get()))
		{
			CharacterFireMontage = Profile->CharacterFireMontage.Get();
		}
	}

	OnRecoilPresentationRequested.Broadcast();

	PlayPresentationMontage(
		CharacterMesh,
		CharacterFireMontage);

	DustFireAudioPolicy::Apply(ResolveCurrentVisualWeapon(), true);
	OnFireEffectsPresentationRequested.Broadcast();

	PlayPresentationMontage(
		WeaponMesh,
		WeaponFireMontage);
}
AActor* UWeaponPresentationComponent::ResolveCurrentVisualWeapon() const
{
 if (BoundVisualFormalWeapon.Get() == BoundWeapon.Get() && BoundVisualWeaponActor.IsValid())
  return BoundVisualWeaponActor.Get();
 // Support the existing Blueprint-only visual initialization as well as the native binder.
 if (IsValid(GetOwner()))
 {
  TInlineComponentArray<UChildActorComponent*> Slots(GetOwner());
  for (UChildActorComponent* Slot : Slots)
   if (Slot && Slot->GetFName() == TEXT("TacticalPresentationWeapon")) return Slot->GetChildActor();
 }
 return nullptr;
}

bool UWeaponPresentationComponent::PlayResolvedShotTracer(const FVector& ShotEnd)
{
 AActor* Visual = ResolveCurrentVisualWeapon();
 FVector Muzzle;
 if (!IsValid(Visual) || Visual->IsHidden() || !DustFireAudioPolicy::GetMuzzleLocation(Visual, Muzzle)) return false;
 UShotTracerComponent* Tracer = GetOwner()->FindComponentByClass<UShotTracerComponent>();
 if (!Tracer)
 {
  Tracer = NewObject<UShotTracerComponent>(GetOwner());
  GetOwner()->AddInstanceComponent(Tracer);
  Tracer->RegisterComponent();
 }
 return Tracer->PlayShotTracer(Muzzle, ShotEnd);
}
void UWeaponPresentationComponent::PlayNativeGunPushback(
	float Amount, float KickDuration, float ReturnSpeed)
{
	if (!bUseNativeGunPushback || !IsValid(BoundWeapon.Get()) || !FMath::IsFinite(Amount) ||
		bResettingPresentationSwitchState ||
		!FMath::IsFinite(KickDuration) || !FMath::IsFinite(ReturnSpeed) ||
		FMath::IsNearlyZero(Amount))
	{
		return;
	}
	GunPushbackStartX = NativeGunPushbackLocation.X;
	GunPushbackTargetX = Amount;
	GunPushbackElapsed = 0.0f;
	GunPushbackDuration = FMath::Max(0.0f, KickDuration);
	GunPushbackReturnSpeed = FMath::Max(0.1f, ReturnSpeed);
	bGunPushbackAttacking = GunPushbackDuration > KINDA_SMALL_NUMBER;
	if (!bGunPushbackAttacking)
	{
		NativeGunPushbackLocation.X = GunPushbackTargetX;
	}
	SetComponentTickEnabled(true);
}

void UWeaponPresentationComponent::ClearNativeGunPushback()
{
	NativeGunPushbackLocation = FVector::ZeroVector;
	GunPushbackStartX = 0.0f;
	GunPushbackTargetX = 0.0f;
	GunPushbackElapsed = 0.0f;
	GunPushbackDuration = 0.0f;
	bGunPushbackAttacking = false;
	SetComponentTickEnabled(false);
}

void UWeaponPresentationComponent::TickComponent(float DeltaTime,
	ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsValid(BoundWeapon.Get()) || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		ClearNativeGunPushback();
		return;
	}
	float RemainingTime = DeltaTime;
	if (bGunPushbackAttacking)
	{
		const float AttackTime = FMath::Min(RemainingTime,
			GunPushbackDuration - GunPushbackElapsed);
		GunPushbackElapsed += AttackTime;
		RemainingTime -= AttackTime;
		const float T = FMath::Clamp(GunPushbackElapsed / GunPushbackDuration,
			0.0f, 1.0f);
		const float EaseOut = 1.0f - FMath::Pow(1.0f - T, 3.0f);
		NativeGunPushbackLocation.X = FMath::Lerp(
			GunPushbackStartX, GunPushbackTargetX, EaseOut);
		bGunPushbackAttacking = GunPushbackElapsed < GunPushbackDuration;
	}
	if (!bGunPushbackAttacking)
	{
		NativeGunPushbackLocation.X *=
			FMath::Exp(-GunPushbackReturnSpeed * RemainingTime);
		if (FMath::Abs(NativeGunPushbackLocation.X) < KINDA_SMALL_NUMBER)
		{
			ClearNativeGunPushback();
		}
	}
}
bool UWeaponPresentationComponent::ResolveStandardReloadPresentation(
	AWeaponRuntime* FormalWeapon,
	FWeaponReloadVariant& OutPresentation) const
{
	OutPresentation = FWeaponReloadVariant{};

	FWeaponPresentationProfile Profile;
	if (!FindWeaponPresentationProfile(FormalWeapon, Profile))
	{
		if (IsValid(FormalWeapon) && BoundWeapon.Get() == FormalWeapon &&
			FormalWeapon->IsReloading())
		{
			ensureMsgf(false, TEXT("ResolveStandardReloadPresentation: no usable player profile for weapon %s, definition %s, request %d, presentation %s. Check PresentationData and the legacy profile mapping."),
				*FormalWeapon->GetPathName(), *GetPathNameSafe(FormalWeapon->WeaponDefinition.Get()),
				FormalWeapon->GetReloadRequestId(), *GetPathName());
		}
		return false;
	}

	if (Profile.bUseCountBasedReload)
	{
		return false;
	}

	const bool bIsEmpty = FormalWeapon->GetCurrentAmmo() == 0;

	OutPresentation.CharacterMontage = bIsEmpty
		? Profile.CharacterReloadEmptyMontage
		: Profile.CharacterReloadTacticalMontage;

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
	if (!Profile.bUseCountBasedReload) return false;

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
	if (bResettingPresentationSwitchState)
	{
		return;
	}
	if (!IsValid(AudioComponent))
	{
		if (!AudioComponent && IsValid(Sound))
		{
			const AWeaponRuntime* Weapon = BoundWeapon.Get();
			if (Weapon && Weapon->IsReloading())
			{
				UE_LOG(LogTemp, Warning, TEXT("PlayReloadPresentationAudio: no AudioComponent for sound %s, weapon %s, request %d, presentation %s. Check the Audio Component input."),
					*Sound->GetPathName(), *Weapon->GetPathName(),
					Weapon->GetReloadRequestId(), *GetPathName());
			}
		}
		return;
	}

	if (UAudioComponent* Previous = ActiveReloadAudioComponent.Get())
	{
		if (Previous != AudioComponent)
		{
			Previous->Stop();
		}
	}

	ActiveReloadAudioComponent = AudioComponent;
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
bool UWeaponPresentationComponent::TryCommitReloadPresentationRound(
	bool bCompletionAllowed,
	AWeaponRuntime* ReloadingWeapon,
	AWeaponRuntime* CurrentWeapon,
	int32 ExpectedReloadRequestId)
{
	if (!ShouldCompleteReloadPresentation(
		bCompletionAllowed,
		ReloadingWeapon,
		CurrentWeapon,
		ExpectedReloadRequestId))
	{
		return false;
	}

	return ReloadingWeapon->TryCommitReloadRound(
		ExpectedReloadRequestId);
}
bool UWeaponPresentationComponent::HandleReloadPresentationNotify(
	FName NotifyName,
	USceneComponent* AttachToComponent,
	bool bCompletionAllowed,
	AWeaponRuntime* ReloadingWeapon,
	AWeaponRuntime* CurrentWeapon,
	int32 ExpectedReloadRequestId)
{
	if (!ShouldCompleteReloadPresentation(
		bCompletionAllowed,
		ReloadingWeapon,
		CurrentWeapon,
		ExpectedReloadRequestId))
	{
		return false;
	}

	FWeaponPresentationProfile Profile;
	if (!FindWeaponPresentationProfile(ReloadingWeapon, Profile))
	{
		return false;
	}

	ReloadNotifyAudioComponents.RemoveAll(
		[](const TObjectPtr<UAudioComponent>& Audio)
		{
			return !IsValid(Audio.Get());
		});

	if (const TObjectPtr<USoundBase>* Sound =
		Profile.ReloadNotifySounds.Find(NotifyName))
	{
		if (IsValid(Sound->Get()) && IsValid(AttachToComponent))
		{
			UAudioComponent* SpawnedAudio =
				UGameplayStatics::SpawnSoundAttached(
					Sound->Get(),
					AttachToComponent,
					NAME_None,
					FVector::ZeroVector,
					FRotator::ZeroRotator,
					EAttachLocation::KeepRelativeOffset,
					false,
					1.0f,
					1.0f,
					0.0f,
					nullptr,
					nullptr,
					true);

			if (IsValid(SpawnedAudio))
			{
				ReloadNotifyAudioComponents.Add(SpawnedAudio);
			}
		}
	}
	if (!Profile.ReloadRoundCommitNotifyName.IsNone() &&
		NotifyName == Profile.ReloadRoundCommitNotifyName)
	{
		return TryCommitReloadPresentationRound(
			bCompletionAllowed,
			ReloadingWeapon,
			CurrentWeapon,
			ExpectedReloadRequestId);
	}

	return false;
}

void UWeaponPresentationComponent::StopReloadPresentationSounds()
{
	if (UAudioComponent* Audio = ActiveReloadAudioComponent.Get())
	{
		Audio->Stop();
	}
	ActiveReloadAudioComponent.Reset();

	for (const TObjectPtr<UAudioComponent>& Audio :
		ReloadNotifyAudioComponents)
	{
		if (IsValid(Audio.Get()))
		{
			Audio->Stop();
		}
	}
	ReloadNotifyAudioComponents.Reset();
}
bool UWeaponPresentationComponent::IsReloadWeaponPlaybackCurrent() const
{
	return ActiveReloadMontageInstanceId != INDEX_NONE &&
		ActiveReloadAnimInstance.IsValid() &&
		ActiveReloadWeaponMesh.IsValid() &&
		ShouldCompleteReloadPresentation(
			true,
			ActiveReloadWeapon.Get(),
			BoundWeapon.Get(),
			ActiveReloadWeaponRequestId);
}

bool UWeaponPresentationComponent::IsSynchronizedReloadPlaybackCurrent() const
{
	return bSynchronizedReloadPresentationActive &&
		ShouldCompleteReloadPresentation(
			true,
			ActiveSynchronizedReloadWeapon.Get(),
			BoundWeapon.Get(),
			ActiveSynchronizedReloadRequestId);
}

bool UWeaponPresentationComponent::PlaySynchronizedReloadLayer(
	int32 Layer,
	UAnimInstance* AnimInstance,
	UAnimMontage* Montage,
	float PlayRate,
	uint64 PlaybackSerial)
{
	if (Layer < 0 || Layer > 1 || !IsValid(AnimInstance) ||
		!IsValid(Montage))
	{
		return false;
	}

	const float PlayedLength = AnimInstance->Montage_Play(
		Montage,
		PlayRate,
		EMontagePlayReturnType::MontageLength,
		0.0f,
		true);
	if (PlayedLength <= 0.0f)
	{
		return false;
	}

	FAnimMontageInstance* Instance =
		AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!Instance)
	{
		return false;
	}

	const int32 InstanceId = Instance->GetInstanceID();
	if (PlaybackSerial != SynchronizedReloadPlaybackSerial ||
		!bSynchronizedReloadPresentationActive ||
		!bSynchronizedReloadPresentationLaunching)
	{
		return false;
	}

	SynchronizedReloadAnimInstances[Layer] = AnimInstance;
	SynchronizedReloadMontages[Layer] = Montage;
	SynchronizedReloadInstanceIds[Layer] = InstanceId;
	bSynchronizedReloadLayerPending[Layer] = true;
	return true;
}

bool UWeaponPresentationComponent::StartSynchronizedReloadPresentation(
	AWeaponRuntime* FormalWeapon,
	USkeletalMeshComponent* CharacterMesh,
	UAnimMontage* CharacterMontage,
	USkeletalMeshComponent* WeaponMesh,
	UAnimMontage* WeaponMontage,
	float PlayRate)
{
	if (bResettingPresentationSwitchState ||
		!IsValid(FormalWeapon) ||
		FormalWeapon != BoundWeapon.Get() ||
		!FormalWeapon->IsReloading() ||
		FormalWeapon->GetReloadRequestId() <= 0 ||
		!IsValid(CharacterMesh) ||
		!IsValid(CharacterMontage) ||
		!IsValid(WeaponMesh) ||
		!IsValid(WeaponMontage) ||
		!FMath::IsFinite(PlayRate) ||
		PlayRate <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	UAnimInstance* CharacterAnimInstance = CharacterMesh->GetAnimInstance();
	UAnimInstance* WeaponAnimInstance = WeaponMesh->GetAnimInstance();
	if (!IsValid(CharacterAnimInstance) ||
		!IsValid(WeaponAnimInstance) ||
		CharacterAnimInstance == WeaponAnimInstance)
	{
		return false;
	}

	const int32 PlaybackRequestId = FormalWeapon->GetReloadRequestId();
	if (bSynchronizedReloadPresentationActive &&
		ActiveSynchronizedReloadWeapon.Get() == FormalWeapon &&
		ActiveSynchronizedReloadRequestId == PlaybackRequestId)
	{
		return false;
	}

	CancelReloadWeaponPresentation();
	if (bSynchronizedReloadPresentationActive ||
		!ShouldCompleteReloadPresentation(
			true, FormalWeapon, BoundWeapon.Get(), PlaybackRequestId))
	{
		return false;
	}

	const uint64 PlaybackSerial = ++SynchronizedReloadPlaybackSerial;
	ActiveSynchronizedReloadWeapon = FormalWeapon;
	ActiveSynchronizedReloadRequestId = PlaybackRequestId;
	bSynchronizedReloadPresentationActive = true;
	bSynchronizedReloadPresentationLaunching = true;
	const auto IsLaunchCurrent =
		[this, FormalWeapon, PlaybackRequestId, PlaybackSerial]()
		{
			return PlaybackSerial == SynchronizedReloadPlaybackSerial &&
				bSynchronizedReloadPresentationActive &&
				bSynchronizedReloadPresentationLaunching &&
				ActiveSynchronizedReloadWeapon.Get() == FormalWeapon &&
				ActiveSynchronizedReloadRequestId == PlaybackRequestId;
		};

	if (!PlaySynchronizedReloadLayer(
			0,
			CharacterAnimInstance,
			CharacterMontage,
			PlayRate,
			PlaybackSerial) ||
		!PlaySynchronizedReloadLayer(
			1,
			WeaponAnimInstance,
			WeaponMontage,
			PlayRate,
			PlaybackSerial))
	{
		if (IsLaunchCurrent())
		{
			CancelReloadWeaponPresentation();
		}
		return false;
	}

	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		if (!IsLaunchCurrent())
		{
			return false;
		}

		UAnimInstance* AnimInstance =
			SynchronizedReloadAnimInstances[Layer].Get();
		FAnimMontageInstance* Instance = AnimInstance
			? AnimInstance->GetMontageInstanceForID(
				SynchronizedReloadInstanceIds[Layer])
			: nullptr;
		if (!Instance ||
			Instance->Montage != SynchronizedReloadMontages[Layer].Get())
		{
			if (IsLaunchCurrent())
			{
				CancelReloadWeaponPresentation();
			}
			return false;
		}

		SynchronizedReloadPreviousEndDelegates[Layer] =
			Instance->OnMontageEnded;
		const FOnMontageEnded PreviousEndDelegate =
			SynchronizedReloadPreviousEndDelegates[Layer];
		const int32 InstanceId = SynchronizedReloadInstanceIds[Layer];
		FOnMontageEnded EndDelegate;
		EndDelegate.BindWeakLambda(
			this,
			[this, PreviousEndDelegate, PlaybackSerial, Layer, InstanceId]
			(UAnimMontage* EndedMontage, bool bInterrupted)
			{
				PreviousEndDelegate.ExecuteIfBound(
					EndedMontage, bInterrupted);
				HandleSynchronizedReloadLayerEnded(
					PlaybackSerial,
					Layer,
					InstanceId,
					EndedMontage,
					bInterrupted);
			});
		Instance->OnMontageEnded = EndDelegate;

		if (Layer == 1)
		{
			SynchronizedReloadPreviousWeaponBlendOutDelegate = Instance->OnMontageBlendingOutStarted;
			Instance->OnMontageBlendingOutStarted.BindWeakLambda(this,
				[this, PreviousBlendOutDelegate = SynchronizedReloadPreviousWeaponBlendOutDelegate,
					PlaybackSerial](UAnimMontage* BlendingOutMontage, bool bInterrupted)
				{
					if (PlaybackSerial == SynchronizedReloadPlaybackSerial)
					{
						bSynchronizedReloadWeaponBlendingOutNaturally = !bInterrupted;
					}
					PreviousBlendOutDelegate.ExecuteIfBound(BlendingOutMontage, bInterrupted);
				});
		}
	}

	if (!IsLaunchCurrent() ||
		!IsSynchronizedReloadPlaybackCurrent() ||
		!bSynchronizedReloadLayerPending[0] ||
		!bSynchronizedReloadLayerPending[1])
	{
		if (IsLaunchCurrent())
		{
			CancelReloadWeaponPresentation();
		}
		return false;
	}

	bSynchronizedReloadPresentationLaunching = false;
	CharacterAnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(
		this, &UWeaponPresentationComponent::HandleSynchronizedReloadReadyNotifyBegin);
	return true;
}

void UWeaponPresentationComponent::HandleSynchronizedReloadReadyNotifyBegin(
	FName NotifyName,
	const FBranchingPointNotifyPayload& Payload)
{
	static const FName ReloadReadyNotifyName(TEXT("ReloadReady"));
	const UAnimInstance* CharacterAnimInstance = SynchronizedReloadAnimInstances[0].Get();
	if (NotifyName != ReloadReadyNotifyName || !CharacterAnimInstance ||
		Payload.SkelMeshComponent != CharacterAnimInstance->GetSkelMeshComponent() ||
		Payload.SequenceAsset != SynchronizedReloadMontages[0].Get() ||
		Payload.MontageInstanceID != SynchronizedReloadInstanceIds[0])
	{
		return;
	}

	if (bSynchronizedReloadLayerPending[1])
	{
		UAnimInstance* WeaponAnimInstance = SynchronizedReloadAnimInstances[1].Get();
		const FAnimMontageInstance* WeaponInstance = WeaponAnimInstance
			? WeaponAnimInstance->GetMontageInstanceForID(SynchronizedReloadInstanceIds[1])
			: nullptr;
		// The gun layer may finish first or be blending out naturally when the hands are ready.
		if (!WeaponInstance || WeaponInstance->Montage != SynchronizedReloadMontages[1].Get() ||
			(WeaponInstance->IsStopped() && (!bSynchronizedReloadWeaponBlendingOutNaturally ||
				WeaponAnimInstance->GetCurrentActiveMontage())))
		{
			return;
		}
	}

	FinishSynchronizedReloadPresentation();
}

void UWeaponPresentationComponent::HandleSynchronizedReloadLayerEnded(
	uint64 PlaybackSerial,
	int32 Layer,
	int32 InstanceId,
	UAnimMontage* EndedMontage,
	bool bInterrupted)
{
	if (Layer < 0 || Layer > 1 ||
		PlaybackSerial != SynchronizedReloadPlaybackSerial ||
		!bSynchronizedReloadPresentationActive ||
		!bSynchronizedReloadLayerPending[Layer] ||
		SynchronizedReloadInstanceIds[Layer] != InstanceId ||
		SynchronizedReloadMontages[Layer].Get() != EndedMontage)
	{
		return;
	}

	if (bInterrupted || !IsSynchronizedReloadPlaybackCurrent())
	{
		bSynchronizedReloadLayerPending[Layer] = false;
		CancelReloadWeaponPresentation();
		return;
	}

	UAnimInstance* AnimInstance =
		SynchronizedReloadAnimInstances[Layer].Get();
	if (IsValid(AnimInstance))
	{
		if (Layer == 0)
		{
			AnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(
				this, &UWeaponPresentationComponent::HandleSynchronizedReloadReadyNotifyBegin);
		}
		if (FAnimMontageInstance* Instance =
			AnimInstance->GetMontageInstanceForID(InstanceId))
		{
			if (Instance->Montage == EndedMontage)
			{
				if (Instance->OnMontageEnded.IsBoundToObject(this))
				{
					Instance->OnMontageEnded = SynchronizedReloadPreviousEndDelegates[Layer];
				}
				if (Layer == 1 && Instance->OnMontageBlendingOutStarted.IsBoundToObject(this))
				{
					Instance->OnMontageBlendingOutStarted = SynchronizedReloadPreviousWeaponBlendOutDelegate;
				}
			}
		}
	}

	bSynchronizedReloadLayerPending[Layer] = false;
	SynchronizedReloadAnimInstances[Layer].Reset();
	SynchronizedReloadMontages[Layer].Reset();
	SynchronizedReloadInstanceIds[Layer] = INDEX_NONE;
	SynchronizedReloadPreviousEndDelegates[Layer].Unbind();
	if (Layer == 1)
	{
		SynchronizedReloadPreviousWeaponBlendOutDelegate.Unbind();
		bSynchronizedReloadWeaponBlendingOutNaturally = false;
	}

	if (!bSynchronizedReloadLayerPending[0] && !bSynchronizedReloadLayerPending[1])
	{
		FinishSynchronizedReloadPresentation();
	}
}

void UWeaponPresentationComponent::FinishSynchronizedReloadPresentation()
{
	AWeaponRuntime* CompletedWeapon =
		ActiveSynchronizedReloadWeapon.Get();
	const int32 CompletedRequestId = ActiveSynchronizedReloadRequestId;
	const bool bCanComplete = IsSynchronizedReloadPlaybackCurrent();
	ReleaseSynchronizedReloadPlayback(false, false);
	const uint64 ReleasedPlaybackSerial = SynchronizedReloadPlaybackSerial;
	if (bCanComplete)
	{
		OnReloadWeaponPresentationCompleted.Broadcast(
			CompletedWeapon, CompletedRequestId);
		return;
	}

	if (IsValid(CompletedWeapon) && CompletedRequestId > 0 &&
		CompletedWeapon->IsReloading() &&
		CompletedWeapon->GetReloadRequestId() == CompletedRequestId)
	{
		CompletedWeapon->CancelReload();
	}
	if (SynchronizedReloadPlaybackSerial == ReleasedPlaybackSerial &&
		!bSynchronizedReloadPresentationActive &&
		!bSynchronizedReloadPresentationLaunching &&
		ActiveReloadMontageInstanceId == INDEX_NONE)
	{
		StopReloadPresentationSounds();
	}
}

void UWeaponPresentationComponent::ReleaseSynchronizedReloadPlayback(
	bool bStopMontages,
	bool bCancelFormalReload)
{
	const bool bHadPlayback =
		bSynchronizedReloadPresentationActive ||
		bSynchronizedReloadPresentationLaunching;
	const TWeakObjectPtr<AWeaponRuntime> SavedWeapon =
		ActiveSynchronizedReloadWeapon;
	const int32 SavedRequestId = ActiveSynchronizedReloadRequestId;
	const TWeakObjectPtr<UAnimInstance> SavedAnimInstances[2] =
	{
		SynchronizedReloadAnimInstances[0],
		SynchronizedReloadAnimInstances[1]
	};
	const TWeakObjectPtr<UAnimMontage> SavedMontages[2] =
	{
		SynchronizedReloadMontages[0],
		SynchronizedReloadMontages[1]
	};
	const int32 SavedInstanceIds[2] =
	{
		SynchronizedReloadInstanceIds[0],
		SynchronizedReloadInstanceIds[1]
	};
	const FOnMontageEnded SavedPreviousEndDelegates[2] =
	{
		SynchronizedReloadPreviousEndDelegates[0],
		SynchronizedReloadPreviousEndDelegates[1]
	};
	const FOnMontageBlendingOutStarted SavedPreviousWeaponBlendOutDelegate =
		SynchronizedReloadPreviousWeaponBlendOutDelegate;
	const bool bSavedLayerPending[2] =
	{
		bSynchronizedReloadLayerPending[0],
		bSynchronizedReloadLayerPending[1]
	};

	if (UAnimInstance* CharacterAnimInstance = SavedAnimInstances[0].Get())
	{
		CharacterAnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(
			this, &UWeaponPresentationComponent::HandleSynchronizedReloadReadyNotifyBegin);
	}
	SynchronizedReloadPreviousWeaponBlendOutDelegate.Unbind();
	bSynchronizedReloadWeaponBlendingOutNaturally = false;
	++SynchronizedReloadPlaybackSerial;
	ActiveSynchronizedReloadWeapon.Reset();
	ActiveSynchronizedReloadRequestId = 0;
	bSynchronizedReloadPresentationActive = false;
	bSynchronizedReloadPresentationLaunching = false;
	AWeaponRuntime* WeaponToCancel = SavedWeapon.Get();
	if (bCancelFormalReload && bHadPlayback && IsValid(WeaponToCancel) &&
		SavedRequestId > 0 && WeaponToCancel->IsReloading() &&
		WeaponToCancel->GetReloadRequestId() == SavedRequestId)
	{
		WeaponToCancel->CancelReload();
	}

	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		SynchronizedReloadAnimInstances[Layer].Reset();
		SynchronizedReloadMontages[Layer].Reset();
		SynchronizedReloadInstanceIds[Layer] = INDEX_NONE;
		SynchronizedReloadPreviousEndDelegates[Layer].Unbind();
		bSynchronizedReloadLayerPending[Layer] = false;
	}

	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		UAnimInstance* AnimInstance = SavedAnimInstances[Layer].Get();
		if (!IsValid(AnimInstance) || SavedInstanceIds[Layer] == INDEX_NONE)
		{
			continue;
		}

		FAnimMontageInstance* Instance =
			AnimInstance->GetMontageInstanceForID(SavedInstanceIds[Layer]);
		if (!Instance || Instance->Montage != SavedMontages[Layer].Get())
		{
			continue;
		}

		if (Instance->OnMontageEnded.IsBoundToObject(this))
		{
			Instance->OnMontageEnded = SavedPreviousEndDelegates[Layer];
		}
		if (Layer == 1 && Instance->OnMontageBlendingOutStarted.IsBoundToObject(this))
		{
			Instance->OnMontageBlendingOutStarted = SavedPreviousWeaponBlendOutDelegate;
		}
		if (bStopMontages && bSavedLayerPending[Layer])
		{
			Instance->Stop(FAlphaBlend(0.1f), true);
		}
	}

}

bool UWeaponPresentationComponent::StartReloadWeaponPresentation(
	AWeaponRuntime* FormalWeapon,
	USkeletalMeshComponent* WeaponMesh,
	UAnimMontage* Montage)
{
	if (bResettingPresentationSwitchState ||
		!IsValid(FormalWeapon) ||
		FormalWeapon != BoundWeapon.Get() ||
		!FormalWeapon->IsReloading() ||
		FormalWeapon->GetReloadRequestId() <= 0 ||
		!IsValid(WeaponMesh) || !IsValid(Montage))
	{
		return false;
	}

	UAnimInstance* AnimInstance = WeaponMesh->GetAnimInstance();
	if (!IsValid(AnimInstance))
	{
		return false;
	}

	const int32 PlaybackRequestId = FormalWeapon->GetReloadRequestId();
	CancelReloadWeaponPresentation();

	const float PlayedLength = AnimInstance->Montage_Play(
		Montage, 1.0f,
		EMontagePlayReturnType::MontageLength, 0.0f, true);
	if (PlayedLength <= 0.0f)
	{
		return false;
	}

	FAnimMontageInstance* Instance =
		AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!Instance)
	{
		return false;
	}
	if (Instance->OnMontageEnded.IsBound() ||
		!ShouldCompleteReloadPresentation(
			true, FormalWeapon, BoundWeapon.Get(), PlaybackRequestId))
	{
		Instance->Stop(FAlphaBlend(0.1f), true);
		return false;
	}

	ActiveReloadWeapon = FormalWeapon;
	ActiveReloadAnimInstance = AnimInstance;
	ActiveReloadWeaponMesh = WeaponMesh;
	ActiveReloadMontageInstanceId = Instance->GetInstanceID();
	ActiveReloadWeaponRequestId = PlaybackRequestId;

	const uint64 PlaybackSerial = ++ReloadWeaponPlaybackSerial;
	const int32 PlaybackInstanceId = ActiveReloadMontageInstanceId;
	const TWeakObjectPtr<AWeaponRuntime> PlaybackWeapon(FormalWeapon);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindWeakLambda(this,
		[this, PlaybackSerial, PlaybackInstanceId,
		PlaybackWeapon, PlaybackRequestId]
		(UAnimMontage*, bool bInterrupted)
		{
			if (ReloadWeaponPlaybackSerial != PlaybackSerial ||
				ActiveReloadMontageInstanceId != PlaybackInstanceId ||
				ActiveReloadWeapon != PlaybackWeapon ||
				ActiveReloadWeaponRequestId != PlaybackRequestId)
			{
				return;
			}

			const bool bCanComplete =
				!bInterrupted && IsReloadWeaponPlaybackCurrent();
			AWeaponRuntime* CompletedWeapon = PlaybackWeapon.Get();
			ReleaseReloadWeaponPlayback(false);

			if (!bCanComplete)
			{
				StopReloadPresentationSounds();
				return;
			}

			OnReloadWeaponPresentationCompleted.Broadcast(
				CompletedWeapon, PlaybackRequestId);
		});

	Instance->OnMontageEnded = EndDelegate;
	AnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(
		this, &UWeaponPresentationComponent::HandleReloadWeaponNotifyBegin);
	return true;
}

void UWeaponPresentationComponent::HandleReloadWeaponNotifyBegin(
	FName NotifyName,
	const FBranchingPointNotifyPayload& Payload)
{
	if (Payload.MontageInstanceID != ActiveReloadMontageInstanceId ||
		!IsReloadWeaponPlaybackCurrent())
	{
		return;
	}

	AWeaponRuntime* ReloadingWeapon = ActiveReloadWeapon.Get();
	const int32 RequestId = ActiveReloadWeaponRequestId;

	const bool bRoundCommitted = HandleReloadPresentationNotify(
		NotifyName,
		ActiveReloadWeaponMesh.Get(),
		true,
		ReloadingWeapon,
		BoundWeapon.Get(),
		RequestId);

	if (bRoundCommitted)
	{
		OnReloadRoundPresentationCommitted.Broadcast(
			ReloadingWeapon, RequestId);
	}
}

void UWeaponPresentationComponent::ReleaseReloadWeaponPlayback(
	bool bStopMontage)
{
	UAnimInstance* AnimInstance = ActiveReloadAnimInstance.Get();
	const int32 SavedInstanceId = ActiveReloadMontageInstanceId;
	++ReloadWeaponPlaybackSerial;
	ActiveReloadWeapon.Reset();
	ActiveReloadAnimInstance.Reset();
	ActiveReloadWeaponMesh.Reset();
	ActiveReloadMontageInstanceId = INDEX_NONE;
	ActiveReloadWeaponRequestId = 0;

	if (IsValid(AnimInstance))
	{
		AnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(
			this,
			&UWeaponPresentationComponent::HandleReloadWeaponNotifyBegin);

		if (FAnimMontageInstance* Instance =
			AnimInstance->GetMontageInstanceForID(SavedInstanceId))
		{
			Instance->OnMontageEnded.Unbind();
			if (bStopMontage)
			{
				Instance->Stop(FAlphaBlend(0.1f), true);
			}
		}
	}
}

void UWeaponPresentationComponent::CancelReloadWeaponPresentation(bool bStopMontage)
{
	const uint64 PreviousReloadSerial = ReloadWeaponPlaybackSerial;
	const uint64 PreviousSynchronizedSerial =
		SynchronizedReloadPlaybackSerial;
	ReleaseReloadWeaponPlayback(bStopMontage);
	ReleaseSynchronizedReloadPlayback(bStopMontage, true);
	if (ReloadWeaponPlaybackSerial == PreviousReloadSerial + 1 &&
		SynchronizedReloadPlaybackSerial == PreviousSynchronizedSerial + 1 &&
		ActiveReloadMontageInstanceId == INDEX_NONE &&
		!bSynchronizedReloadPresentationActive &&
		!bSynchronizedReloadPresentationLaunching)
	{
		StopReloadPresentationSounds();
	}
}
bool UWeaponPresentationComponent::ShouldUseFastDrawPresentation(
	const FWeaponPresentationProfile& PresentationProfile,
	bool bInitialDrawConsumed) const
{
	return !IsValid(PresentationProfile.DrawCharacterMontage.Get())
		|| (bInitialDrawConsumed && !PresentationProfile.bAlwaysUseFullDraw);
}
bool UWeaponPresentationComponent::HasConsumedInitialDraw(
	AWeaponRuntime* FormalWeapon) const
{
	return IsValid(FormalWeapon)
		&& InitialDrawConsumedWeapons.Contains(
			TWeakObjectPtr<AWeaponRuntime>(FormalWeapon));
}

void UWeaponPresentationComponent::MarkInitialDrawConsumed(
	AWeaponRuntime* FormalWeapon)
{
	if (!IsValid(FormalWeapon))
	{
		return;
	}

	for (auto It = InitialDrawConsumedWeapons.CreateIterator(); It; ++It)
	{
		if (!(*It).IsValid())
		{
			It.RemoveCurrent();
		}
	}

	InitialDrawConsumedWeapons.Add(
		TWeakObjectPtr<AWeaponRuntime>(FormalWeapon));
}
bool UWeaponPresentationComponent::BeginFastDrawTransition(
	AWeaponRuntime* FormalWeapon, float Delay)
{
	if (bResettingPresentationSwitchState)
	{
		return false;
	}

	CancelFastDrawTransition();
	UWorld* World = GetWorld();
	if (!World || !IsValid(FormalWeapon)
		|| BoundWeapon.Get() != FormalWeapon)
	{
		return false;
	}

	FastDrawTransitionWeapon = FormalWeapon;
	bFastDrawTransitionPending = true;
	if (Delay > 0.0f)
	{
		World->GetTimerManager().SetTimer(
			FastDrawTransitionTimer, this,
			&UWeaponPresentationComponent::HandleFastDrawTransitionTimer,
			Delay, false);
	}
	else
	{
		FastDrawTransitionTimer = World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(
				this,
				&UWeaponPresentationComponent::HandleFastDrawTransitionTimer));
	}
	return true;
}

void UWeaponPresentationComponent::CancelFastDrawTransition()
{
	++FastDrawTransitionSerial;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FastDrawTransitionTimer);
	}
	FastDrawTransitionTimer.Invalidate();
	bFastDrawTransitionPending = false;
	FastDrawTransitionWeapon.Reset();
}

bool UWeaponPresentationComponent::IsFastDrawTransitionPending() const
{
	return bFastDrawTransitionPending;
}

void UWeaponPresentationComponent::HandleFastDrawTransitionTimer()
{
	if (bResettingPresentationSwitchState) return;
	AWeaponRuntime* Weapon = FastDrawTransitionWeapon.Get();
	if (!bFastDrawTransitionPending || !IsValid(Weapon)
		|| BoundWeapon.Get() != Weapon)
	{
		CancelFastDrawTransition();
		return;
	}

	const uint64 CallbackSerial = FastDrawTransitionSerial;
	OnFastDrawTransitionRequested.Broadcast();
	if (FastDrawTransitionSerial == CallbackSerial
		&& bFastDrawTransitionPending)
	{
		CancelFastDrawTransition();
	}
}
void UWeaponPresentationComponent::PrepareDrawPresentationWatch()
{
	ClearEarlyDrawSwitchRelease();
	++DrawWatchSerial;
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		if (UAnimInstance* Anim = DrawWatchAnimInstances[Layer].Get())
		{
			if (FAnimMontageInstance* Instance =
				Anim->GetMontageInstanceForID(DrawWatchInstanceIds[Layer]))
			{
				if (Instance->OnMontageEnded.IsBoundToObject(this))
				{
					Instance->OnMontageEnded =
						DrawWatchPreviousEndDelegates[Layer];
				}
			}
		}
		DrawWatchAnimInstances[Layer].Reset();
		DrawWatchInstanceIds[Layer] = INDEX_NONE;
		DrawWatchPreviousEndDelegates[Layer].Unbind();
		bDrawWatchLayerPending[Layer] = false;
	}
	WatchedDrawWeapon.Reset();
	bDrawWatchFinalStage = false;
	bDrawWatchFailed = false;
}

bool UWeaponPresentationComponent::WatchDrawLayer(
	int32 Layer, USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
{
	if (!IsValid(Mesh) || !IsValid(Montage))
	{
		if (Mesh != nullptr || Montage != nullptr)
		{
			bDrawWatchFailed = true;
		}
		return false;
	}
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	if (!IsValid(Anim))
	{
		bDrawWatchFailed = true;
		return false;
	}
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
	if (!Instance)
	{
		bDrawWatchFailed = true;
		return false;
	}

	if (Layer == 1 && bDrawWatchLayerPending[0]
		&& DrawWatchAnimInstances[0].Get() == Anim
		&& DrawWatchInstanceIds[0] == Instance->GetInstanceID())
	{
		return true;
	}

	DrawWatchAnimInstances[Layer] = Anim;
	DrawWatchInstanceIds[Layer] = Instance->GetInstanceID();
	bDrawWatchLayerPending[Layer] = true;
	DrawWatchPreviousEndDelegates[Layer] = Instance->OnMontageEnded;
	const FOnMontageEnded Previous = DrawWatchPreviousEndDelegates[Layer];
	const uint64 Serial = DrawWatchSerial;
	const int32 InstanceId = DrawWatchInstanceIds[Layer];
	FOnMontageEnded EndDelegate;
	EndDelegate.BindWeakLambda(this,
		[this, Previous, Serial, Layer, InstanceId]
		(UAnimMontage* EndedMontage, bool bInterrupted)
		{
			Previous.ExecuteIfBound(EndedMontage, bInterrupted);
			HandleDrawLayerEnded(Serial, Layer, InstanceId, bInterrupted);
		});
	Instance->OnMontageEnded = EndDelegate;
	return true;
}

bool UWeaponPresentationComponent::WatchDrawPresentationCompletion(
	AWeaponRuntime* FormalWeapon,
	USkeletalMeshComponent* CharacterMesh,
	UAnimMontage* CharacterMontage,
	USkeletalMeshComponent* WeaponMesh,
	UAnimMontage* WeaponMontage,
	bool bFinalStage)
{
	if (bResettingPresentationSwitchState)
	{
		return false;
	}

	ClearReloadBlockingDrawSnapshot();
	PrepareDrawPresentationWatch();
	WatchedDrawWeapon = FormalWeapon;
	bDrawWatchFinalStage = bFinalStage;
	if (!IsValid(FormalWeapon) || BoundWeapon.Get() != FormalWeapon)
	{
		bDrawWatchFailed = true;
		TryFinishDrawPresentationWatch();
		return false;
	}

	WatchDrawLayer(0, CharacterMesh, CharacterMontage);
	WatchDrawLayer(1, WeaponMesh, WeaponMontage);
	const bool bWatching = bDrawWatchLayerPending[0]
		|| bDrawWatchLayerPending[1];
	if (!bWatching)
	{
		bDrawWatchFailed = true;
	}
	ReloadBlockingDrawWeapon = WatchedDrawWeapon;
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		ReloadBlockingDrawAnimInstances[Layer] =
			DrawWatchAnimInstances[Layer];
		ReloadBlockingDrawInstanceIds[Layer] =
			DrawWatchInstanceIds[Layer];
	}
	if (bWatching && bDrawWatchFinalStage)
	{
		StartEarlyDrawSwitchRelease();
	}
	TryFinishDrawPresentationWatch();
	return bWatching;
}

void UWeaponPresentationComponent::HandleDrawLayerEnded(
	uint64 Serial, int32 Layer, int32 InstanceId, bool bInterrupted)
{
	if (Serial != DrawWatchSerial
		|| DrawWatchInstanceIds[Layer] != InstanceId
		|| !bDrawWatchLayerPending[Layer])
	{
		return;
	}
	bDrawWatchLayerPending[Layer] = false;
	DrawWatchInstanceIds[Layer] = INDEX_NONE;
	DrawWatchAnimInstances[Layer].Reset();
	DrawWatchPreviousEndDelegates[Layer].Unbind();
	if (bInterrupted)
	{
		ClearEarlyDrawSwitchRelease();
	}
	bDrawWatchFailed |= bInterrupted;
	TryFinishDrawPresentationWatch();
}

void UWeaponPresentationComponent::TryFinishDrawPresentationWatch()
{
	if (!bDrawWatchFinalStage
		|| bDrawWatchLayerPending[0] || bDrawWatchLayerPending[1])
	{
		return;
	}
	const bool bCompletedNormally = !bDrawWatchFailed
		&& WatchedDrawWeapon.IsValid()
		&& WatchedDrawWeapon.Get() == BoundWeapon.Get();
	bPresentationSwitchingNative = false;
	PrepareDrawPresentationWatch();
	OnDrawPresentationFinished.Broadcast(bCompletedNormally);
}
void UWeaponPresentationComponent::ClearEarlyDrawSwitchRelease()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DrawSwitchReleaseTimer);
	}
	DrawSwitchReleaseTimer.Invalidate();
	ActiveDrawSwitchLeadTime = 0.0f;
}

bool UWeaponPresentationComponent::GetDrawRemainingForWeapon(
	AWeaponRuntime* ExpectedWeapon, float& OutSeconds) const
{
	OutSeconds = 0.0f;
	return IsValid(ExpectedWeapon) && ExpectedWeapon == BoundWeapon.Get()
		&& ExpectedWeapon == WatchedDrawWeapon.Get() && TryGetDrawRemainingTime(OutSeconds);
}

bool UWeaponPresentationComponent::TryGetDrawRemainingTime(
	float& OutSeconds) const
{
	OutSeconds = 0.0f;
	if (!bDrawWatchFinalStage || bDrawWatchFailed
		|| !WatchedDrawWeapon.IsValid()
		|| WatchedDrawWeapon.Get() != BoundWeapon.Get())
	{
		return false;
	}
	bool bHasLayer = false;
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		if (!bDrawWatchLayerPending[Layer])
		{
			continue;
		}
		UAnimInstance* Anim = DrawWatchAnimInstances[Layer].Get();
		FAnimMontageInstance* Instance = Anim
			? Anim->GetMontageInstanceForID(DrawWatchInstanceIds[Layer])
			: nullptr;
		UAnimMontage* Montage = Instance ? Instance->Montage : nullptr;
		if (!Instance || !IsValid(Montage))
		{
			return false;
		}
		const float Rate = Instance->GetPlayRate() * Montage->RateScale;
		if (!FMath::IsFinite(Rate) || Rate <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const float Remaining = FMath::Max(
			0.0f, Montage->GetPlayLength() - Instance->GetPosition()) / Rate;
		if (!FMath::IsFinite(Remaining))
		{
			return false;
		}
		OutSeconds = FMath::Max(OutSeconds, Remaining);
		bHasLayer = true;
	}
	return bHasLayer;
}

void UWeaponPresentationComponent::ArmEarlyDrawSwitchRelease(float Delay)
{
	if (UWorld* World = GetWorld())
	{
		const uint64 Serial = DrawWatchSerial;
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindWeakLambda(this,
			[this, Serial]() { HandleEarlyDrawSwitchRelease(Serial); });
		World->GetTimerManager().SetTimer(
			DrawSwitchReleaseTimer, TimerDelegate,
			FMath::Max(0.01f, Delay), false);
	}
}

void UWeaponPresentationComponent::StartEarlyDrawSwitchRelease()
{
	ClearEarlyDrawSwitchRelease();
	float Remaining = 0.0f;
	if (!FMath::IsFinite(DrawSwitchReleaseLeadTime)
		|| DrawSwitchReleaseLeadTime <= 0.0f
		|| !TryGetDrawRemainingTime(Remaining)
		|| Remaining <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	ActiveDrawSwitchLeadTime = FMath::Min(
		DrawSwitchReleaseLeadTime, Remaining * 0.5f);
	ArmEarlyDrawSwitchRelease(Remaining - ActiveDrawSwitchLeadTime);
}

void UWeaponPresentationComponent::HandleEarlyDrawSwitchRelease(uint64 Serial)
{
	if (bResettingPresentationSwitchState || Serial != DrawWatchSerial)
	{
		return;
	}
	float Remaining = 0.0f;
	if (!TryGetDrawRemainingTime(Remaining))
	{
		ClearEarlyDrawSwitchRelease();
		return;
	}
	const float Wait = Remaining - ActiveDrawSwitchLeadTime;
	if (Wait > 0.01f)
	{
		ArmEarlyDrawSwitchRelease(Wait);
		return;
	}
	bPresentationSwitchingNative = false;
	PrepareDrawPresentationWatch();
	OnDrawPresentationFinished.Broadcast(true);
}
void UWeaponPresentationComponent::InterruptDrawPresentationWatch()
{
	bPresentationSwitchingNative = false;
	ClearEarlyDrawSwitchRelease();
	if (!WatchedDrawWeapon.IsValid()
		&& !bDrawWatchLayerPending[0] && !bDrawWatchLayerPending[1])
	{
		return;
	}
	bDrawWatchFinalStage = true;
	bDrawWatchFailed = true;
	TryFinishDrawPresentationWatch();
}
	bool UWeaponPresentationComponent::StartFullHolsterPresentation(
		AWeaponRuntime * FormalWeapon,
		USkeletalMeshComponent * CharacterMesh,
		USkeletalMeshComponent * WeaponMesh,
		const FWeaponPresentationProfile & Profile)
	{
		if (bResettingPresentationSwitchState ||
			bFullHolsterActive ||
			!IsValid(FormalWeapon) ||
			FormalWeapon != BoundWeapon.Get() ||
			!IsValid(CharacterMesh) ||
			FullHolsterFinishedNotifyName.IsNone())
		{
			return false;
		}

		FullHolsterReadyWeapon.Reset();

		UAnimMontage* CharacterMontage =
			Profile.FullHolsterCharacterMontage.Get();
		UAnimMontage* WeaponMontage =
			Profile.FullHolsterWeaponMontage.Get();

		UAnimInstance* CharacterAnimInstance = nullptr;
		UAnimInstance* WeaponAnimInstance = nullptr;
		const bool bHasWeaponLayer =
			IsValid(WeaponMesh) && IsValid(WeaponMontage);

		if (IsValid(CharacterMontage))
		{
			CharacterAnimInstance = CharacterMesh->GetAnimInstance();
			if (!IsValid(CharacterAnimInstance))
			{
				return false;
			}

			if (bHasWeaponLayer)
			{
				WeaponAnimInstance = WeaponMesh->GetAnimInstance();
				if (!IsValid(WeaponAnimInstance) ||
					WeaponAnimInstance == CharacterAnimInstance)
				{

					return false;
				}
			}
		}


		PrepareDrawPresentationWatch();
		CancelFastDrawTransition();

		ReleaseFullHolsterPresentation(true);

		if (bFullHolsterActive ||
			!IsValid(FormalWeapon) ||
			FormalWeapon != BoundWeapon.Get())
		{
			return false;
		}

		const uint64 RequestSerial = ++FullHolsterSerial;
		ActiveFullHolsterWeapon = FormalWeapon;
		bFullHolsterActive = true;
		bFullHolsterLaunching = true;
		bFullHolsterLayerCompleted[0] = false;
		bFullHolsterLayerCompleted[1] = false;

		if (!IsValid(CharacterMontage))
		{
			bFullHolsterLayerCompleted[0] = true;
			bFullHolsterLayerCompleted[1] = true;
			bFullHolsterLaunching = false;
			TryFinishFullHolsterPresentation();
			return true;
		}

		if (!PlayFullHolsterLayer(
			0,
			CharacterMesh,
			CharacterMontage))
		{
			if (FullHolsterSerial == RequestSerial &&
				bFullHolsterActive)
			{
				ReleaseFullHolsterPresentation(true);
				return false;
			}
			return true;
		}

		if (bHasWeaponLayer)
		{
			if (!PlayFullHolsterLayer(
				1,
				WeaponMesh,
				WeaponMontage))
			{
				if (FullHolsterSerial == RequestSerial &&
					bFullHolsterActive)
				{
					ReleaseFullHolsterPresentation(true);
					return false;
				}
				return true;
			}
		}
		else
		{
			bFullHolsterLayerCompleted[1] = true;
		}
		if (FullHolsterSerial != RequestSerial ||
			!bFullHolsterActive)
		{
			return true;
		}

		bFullHolsterLaunching = false;
		TryFinishFullHolsterPresentation();
		return true;
	}

	void UWeaponPresentationComponent::CancelFullHolsterPresentation()
	{
		const bool bWasActive = bFullHolsterActive;
		FullHolsterReadyWeapon.Reset();
		ReleaseFullHolsterPresentation(true);

		if (bWasActive)
		{
			OnFullHolsterPresentationCancelled.Broadcast();
		}
	}

	bool UWeaponPresentationComponent::IsFullHolsterPresentationActive() const
	{
		return bFullHolsterActive;
	}

	bool UWeaponPresentationComponent::IsFullHolsterReadyToSwitch() const
	{
		AWeaponRuntime* ReadyWeapon = FullHolsterReadyWeapon.Get();
		return IsValid(ReadyWeapon) &&
			ReadyWeapon == BoundWeapon.Get();
	}

	void UWeaponPresentationComponent::ClearFullHolsterReadyToSwitch()
	{
		FullHolsterReadyWeapon.Reset();
	}

	bool UWeaponPresentationComponent::IsFullHolsterPresentationCurrent() const
	{
		AWeaponRuntime* FormalWeapon = ActiveFullHolsterWeapon.Get();
		return bFullHolsterActive &&
			IsValid(FormalWeapon) &&
			FormalWeapon == BoundWeapon.Get();
	}

	bool UWeaponPresentationComponent::PlayFullHolsterLayer(
		int32 Layer,
		USkeletalMeshComponent * Mesh,
		UAnimMontage * Montage)
	{
		if (Layer < 0 || Layer > 1 ||
			!IsFullHolsterPresentationCurrent() ||
			!IsValid(Mesh) ||
			!IsValid(Montage))
		{
			return false;
		}

		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		if (!IsValid(AnimInstance))
		{
			return false;
		}

		const uint64 PlaybackSerial = FullHolsterSerial;
		const float PlayedLength = AnimInstance->Montage_Play(
			Montage,
			1.0f,
			EMontagePlayReturnType::MontageLength,
			0.0f,
			true);

		FAnimMontageInstance* Instance =
			AnimInstance->GetActiveInstanceForMontage(Montage);
		const int32 InstanceId = Instance
			? Instance->GetInstanceID()
			: INDEX_NONE;

		const bool bStillThisRequest =
			FullHolsterSerial == PlaybackSerial &&
			IsFullHolsterPresentationCurrent();

		if (!bStillThisRequest)
		{
			return false;
		}

		if (!FMath::IsFinite(PlayedLength) || PlayedLength <= 0.0f || !Instance)
		{
			return false;
		}

		FullHolsterAnimInstances[Layer] = AnimInstance;
		FullHolsterMontageInstanceIds[Layer] = InstanceId;
		FullHolsterPreviousEnded[Layer] = Instance->OnMontageEnded;

		const FOnMontageEnded PreviousEnded =
			FullHolsterPreviousEnded[Layer];

		FOnMontageEnded EndDelegate;
		EndDelegate.BindWeakLambda(
			this,
			[this, PreviousEnded, PlaybackSerial, Layer, InstanceId]
			(UAnimMontage* EndedMontage, bool bInterrupted)
			{
				PreviousEnded.ExecuteIfBound(
					EndedMontage,
					bInterrupted);

				HandleFullHolsterLayerEnded(
					PlaybackSerial,
					Layer,
					InstanceId,
					bInterrupted);
			});
		Instance->OnMontageEnded = EndDelegate;

		if (Layer == 0)
		{
			AnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(
				this,
				&UWeaponPresentationComponent::HandleFullHolsterCharacterNotifyBegin);
		}
		else
		{
			AnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(
				this,
				&UWeaponPresentationComponent::HandleFullHolsterWeaponNotifyBegin);
		}

		return true;
	}

	void UWeaponPresentationComponent::
		HandleFullHolsterCharacterNotifyBegin(
			FName NotifyName,
			const FBranchingPointNotifyPayload & Payload)
	{
		HandleFullHolsterNotify(0, NotifyName, Payload);
	}

	void UWeaponPresentationComponent::
		HandleFullHolsterWeaponNotifyBegin(
			FName NotifyName,
			const FBranchingPointNotifyPayload & Payload)
	{
		HandleFullHolsterNotify(1, NotifyName, Payload);
	}

	void UWeaponPresentationComponent::HandleFullHolsterNotify(
		int32 Layer,
		FName NotifyName,
		const FBranchingPointNotifyPayload & Payload)
	{
		if (Layer < 0 || Layer > 1 ||
			FullHolsterFinishedNotifyName.IsNone() ||
			NotifyName != FullHolsterFinishedNotifyName ||
			!bFullHolsterActive ||
			Payload.MontageInstanceID !=
			FullHolsterMontageInstanceIds[Layer])
		{
			return;
		}
		if (!IsFullHolsterPresentationCurrent())
		{
			CancelFullHolsterPresentation();
			return;
		}

		bFullHolsterLayerCompleted[Layer] = true;
		TryFinishFullHolsterPresentation();
	}

	void UWeaponPresentationComponent::HandleFullHolsterLayerEnded(
		uint64 Serial,
		int32 Layer,
		int32 InstanceId,
		bool bInterrupted)
	{
		if (Serial != FullHolsterSerial ||
			Layer < 0 || Layer > 1 ||
			!bFullHolsterActive ||
			FullHolsterMontageInstanceIds[Layer] != InstanceId)
		{
			return;
		}
		if (!IsFullHolsterPresentationCurrent())
		{
			CancelFullHolsterPresentation();
			return;
		}

		if (bInterrupted)
		{
			CancelFullHolsterPresentation();
			return;
		}

		if (Layer == 1)
		{
			bFullHolsterLayerCompleted[1] = true;
			TryFinishFullHolsterPresentation();
			return;
		}

		if (!bFullHolsterLayerCompleted[0])
		{
			CancelFullHolsterPresentation();
		}
	}

	void UWeaponPresentationComponent::TryFinishFullHolsterPresentation()
	{
		if (bResettingPresentationSwitchState || !IsFullHolsterPresentationCurrent() ||
			bFullHolsterLaunching ||
			!bFullHolsterLayerCompleted[0] ||
			!bFullHolsterLayerCompleted[1])
		{
			return;
		}

		AWeaponRuntime* ReadyWeapon = ActiveFullHolsterWeapon.Get();
		ReleaseFullHolsterPresentation(false);
		// The readiness token belongs to this completed bound weapon and is
		// published before Blueprint observes the ready event.
		FullHolsterReadyWeapon = ReadyWeapon;
		OnFullHolsterPresentationReady.Broadcast();
	}

	void UWeaponPresentationComponent::ReleaseFullHolsterPresentation(
		bool bStopMontages)
	{
		++FullHolsterSerial;
		bFullHolsterActive = false;
		bFullHolsterLaunching = false;
		ActiveFullHolsterWeapon.Reset();
		FullHolsterReadyWeapon.Reset();

		const TWeakObjectPtr<UAnimInstance> SavedAnimInstances[2] =
		{
			FullHolsterAnimInstances[0],
			FullHolsterAnimInstances[1]
		};
		const int32 SavedInstanceIds[2] =
		{
			FullHolsterMontageInstanceIds[0],
			FullHolsterMontageInstanceIds[1]
		};
		const FOnMontageEnded SavedPreviousEnded[2] =
		{
			FullHolsterPreviousEnded[0],
			FullHolsterPreviousEnded[1]
		};

		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			FullHolsterAnimInstances[Layer].Reset();
			FullHolsterMontageInstanceIds[Layer] = INDEX_NONE;
			FullHolsterPreviousEnded[Layer].Unbind();
			bFullHolsterLayerCompleted[Layer] = false;
		}

		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			UAnimInstance* AnimInstance = SavedAnimInstances[Layer].Get();
			if (!IsValid(AnimInstance))
			{
				continue;
			}

			if (Layer == 0)
			{
				AnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(
					this,
					&UWeaponPresentationComponent::HandleFullHolsterCharacterNotifyBegin);
			}
			else
			{
				AnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(
					this,
					&UWeaponPresentationComponent::HandleFullHolsterWeaponNotifyBegin);
			}

			if (SavedInstanceIds[Layer] == INDEX_NONE)
			{
				continue;
			}

			FAnimMontageInstance* Instance =
				AnimInstance->GetMontageInstanceForID(
					SavedInstanceIds[Layer]);
			if (!Instance)
			{
				continue;
			}

			if (Instance->OnMontageEnded.IsBoundToObject(this))
			{
				Instance->OnMontageEnded = SavedPreviousEnded[Layer];
			}

			if (bStopMontages)
			{
				Instance->Stop(FAlphaBlend(0.1f), true);
			}
		}
	}
	void UWeaponPresentationComponent::PlayFastHolsterPresentation(
		USkeletalMeshComponent* CharacterMesh,
		const FWeaponPresentationProfile& Profile)
	{
		if (bResettingPresentationSwitchState)
		{
			return;
		}

		if (IsValid(Profile.FastHolsterSound.Get()))
		{
			UGameplayStatics::PlaySound2D(
				this,
				Profile.FastHolsterSound.Get());
		}

		TryPlayPresentationMontage(
			CharacterMesh,
			Profile.FastHolsterCharacterMontage.Get(),
			Profile.FastHolsterStartTime);
	}

	bool UWeaponPresentationComponent::BeginPresentationSwitchCommit(
		AWeaponRuntime* FormalWeapon,
		float Delay)
	{
		if (bResettingPresentationSwitchState ||
			!IsValid(FormalWeapon)
			|| !FMath::IsFinite(Delay)
			|| bPresentationSwitchCommitPending)
		{
			return false;
		}

		UWorld* World = nullptr;
		if (Delay > 0.0f)
		{
			World = GetWorld();
			if (!World)
			{
				return false;
			}
		}

		const uint64 RequestSerial = ++PresentationSwitchCommitSerial;
		ExpectedPresentationSwitchWeapon = FormalWeapon;
		bPresentationSwitchCommitPending = true;

		if (Delay > 0.0f)
		{
			FTimerDelegate TimerDelegate;
			TimerDelegate.BindWeakLambda(
				this,
				[this, RequestSerial]()
				{
					HandlePresentationSwitchCommitTimer(RequestSerial);
				});
			World->GetTimerManager().SetTimer(
				PresentationSwitchCommitTimer,
				TimerDelegate,
				Delay,
				false);
			return true;
		}

		HandlePresentationSwitchCommitTimer(RequestSerial);
		return true;
	}

	void UWeaponPresentationComponent::CancelPresentationSwitchCommit(
		bool bNotifyCancelled)
	{
		const bool bWasPending = bPresentationSwitchCommitPending;
		AWeaponRuntime* ExpectedWeapon =
			ExpectedPresentationSwitchWeapon.Get();

		++PresentationSwitchCommitSerial;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(
				PresentationSwitchCommitTimer);
		}
		PresentationSwitchCommitTimer.Invalidate();
		ExpectedPresentationSwitchWeapon.Reset();
		bPresentationSwitchCommitPending = false;

		if (bWasPending && bNotifyCancelled)
		{
			OnPresentationSwitchCommitCancelled.Broadcast(ExpectedWeapon);
		}
	}

	void UWeaponPresentationComponent::SetPresentationSwitching(bool bSwitching)
	{
		if (!bSwitching)
		{
			bPresentationSwitchingNative = false;
			return;
		}

		if (bResettingPresentationSwitchState ||
			!IsValid(BoundWeapon.Get()))
		{
			return;
		}

		bPresentationSwitchingNative = true;
	}

	bool UWeaponPresentationComponent::IsPresentationSwitching() const
	{
		return bPresentationSwitchingNative;
	}

	bool UWeaponPresentationComponent::IsCurrentPresentationWeapon(
		AWeaponRuntime* FormalWeapon, AWeaponRuntime* CurrentWeapon) const
	{
		return IsValid(FormalWeapon) &&
			IsValid(CurrentWeapon) &&
			FormalWeapon == CurrentWeapon &&
			FormalWeapon == BoundWeapon.Get();
	}

	bool UWeaponPresentationComponent::ResetPresentationSwitchState(
		bool bStopReloadMontage)
	{
		if (bResettingPresentationSwitchState)
		{
			return false;
		}
		TGuardValue<bool> ResetGuard(bResettingPresentationSwitchState, true);
		ClearNativeGunPushback();

		// Reset is a newer owner than any in-flight visual binding. Keep the settled
		// child refs so ClearVisualWeaponPresentation can still hide them, but make
		// the tracked session and any binding postcondition stale.
		++VisualBindingSerial;

		// Invalidate both native results before any synchronous cancellation
		// notification can read or reuse them. The functions below are also
		// guarded so a Blueprint callback cannot start new owned work mid-reset.
		bPresentationSwitchingNative = false;
		FullHolsterReadyWeapon.Reset();

		CancelPresentationSwitchCommit(true);
		CancelFullHolsterPresentation();
		CancelReloadWeaponPresentation(bStopReloadMontage);
		CancelFastDrawTransition();
		InterruptDrawPresentationWatch();
		ClearReloadBlockingDrawSnapshot();

		// Release paths and callbacks cannot re-enable either native result while
		// the guard is alive; clear again so the reset has one final settled state.
		bPresentationSwitchingNative = false;
		FullHolsterReadyWeapon.Reset();
		return true;
	}

	void UWeaponPresentationComponent::HandlePresentationSwitchCommitTimer(
		uint64 Serial)
	{
		if (bResettingPresentationSwitchState || Serial != PresentationSwitchCommitSerial
			|| !bPresentationSwitchCommitPending)
		{
			return;
		}

		AWeaponRuntime* ExpectedWeapon =
			ExpectedPresentationSwitchWeapon.Get();

		CancelPresentationSwitchCommit(false);

		if (!IsValid(ExpectedWeapon))
		{
			OnPresentationSwitchCommitCancelled.Broadcast(nullptr);
			return;
		}

		OnPresentationSwitchCommitRequested.Broadcast(ExpectedWeapon);
	}
	bool UWeaponPresentationComponent::TryActivateVisualWeapon(
		AWeaponRuntime* FormalWeapon,
		UChildActorComponent* VisualWeaponComponent,
		AActor*& OutVisualWeapon,
		FWeaponPresentationProfile& OutProfile)
	{
		OutVisualWeapon = nullptr;
		OutProfile = FWeaponPresentationProfile{};

		AActor* PlayerOwner = GetOwner();
		if (bResettingPresentationSwitchState || !IsValid(PlayerOwner)
			|| !IsValid(FormalWeapon)
			|| BoundWeapon.Get() != FormalWeapon
			|| !IsValid(VisualWeaponComponent)
			|| VisualWeaponComponent->GetOwner() != PlayerOwner
			|| !VisualWeaponBaseClass)
		{
			return false;
		}

		FWeaponPresentationProfile CandidateProfile;
		if (!FindWeaponPresentationProfile(FormalWeapon, CandidateProfile)
			|| !CandidateProfile.VisualWeaponClass
			|| !CandidateProfile.VisualWeaponClass->IsChildOf(
				VisualWeaponBaseClass.Get()))
		{
			return false;
		}

		AActor* VisualWeapon = VisualWeaponComponent->GetChildActor();
		if (!IsValid(VisualWeapon)
			|| !VisualWeapon->IsA(CandidateProfile.VisualWeaponClass.Get()))
		{
			return false;
		}

		VisualWeapon->SetOwner(PlayerOwner);
		VisualWeapon->SetActorTickEnabled(false);
		VisualWeapon->SetActorHiddenInGame(false);

		OutVisualWeapon = VisualWeapon;
		OutProfile = CandidateProfile;
		return true;
	}
	bool UWeaponPresentationComponent::IsOwnedRegisteredVisualComponent(
		const UChildActorComponent* VisualWeaponComponent) const
	{
		AActor* PlayerOwner = GetOwner();
		return IsValid(PlayerOwner)
			&& IsValid(VisualWeaponComponent)
			&& VisualWeaponComponent->IsRegistered()
			&& VisualWeaponComponent->GetOwner() == PlayerOwner;
	}
	void UWeaponPresentationComponent::ReleaseTrackedVisualWeapon(
		bool bHideVisual)
	{
		ResetNativeSway();
		UChildActorComponent* TrackedComponent =
			BoundVisualWeaponComponent.Get();
		AActor* TrackedVisual = BoundVisualWeaponActor.Get();

		BoundVisualFormalWeapon.Reset();
		BoundVisualWeaponComponent.Reset();
		BoundVisualWeaponActor.Reset();
		BoundVisualSessionSerial = 0;

		if (!bHideVisual || !IsValid(TrackedVisual)
			|| !IsValid(TrackedComponent)
			|| TrackedComponent->GetOwner() != GetOwner()
			|| TrackedVisual->GetOwner() != GetOwner()
			|| TrackedComponent->GetChildActor() != TrackedVisual)
		{
			return;
		}

		TrackedVisual->SetActorHiddenInGame(true);
		TrackedVisual->SetActorTickEnabled(false);
	}
	void UWeaponPresentationComponent::InvalidateVisualWeaponBinding(
		bool bHideVisual)
	{
		++VisualBindingSerial;
		ReleaseTrackedVisualWeapon(bHideVisual);
	}
	void UWeaponPresentationComponent::HideVisualWeaponSlot(
		UChildActorComponent* VisualWeaponComponent)
	{
		if (!IsValid(VisualWeaponComponent)
			|| VisualWeaponComponent->GetOwner() != GetOwner())
		{
			return;
		}

		if (AActor* ChildActor = VisualWeaponComponent->GetChildActor())
		{
			ChildActor->SetActorHiddenInGame(true);
			ChildActor->SetActorTickEnabled(false);
		}
	}
	bool UWeaponPresentationComponent::TryBindVisualWeaponPresentation(
		AWeaponRuntime* FormalWeapon,
		UChildActorComponent* VisualWeaponComponent,
		AActor*& OutVisualWeapon,
		FWeaponPresentationProfile& OutProfile,
		int64& OutBindingId)
	{
		OutVisualWeapon = nullptr;
		OutProfile = FWeaponPresentationProfile{};
		OutBindingId = 0;

		AActor* PlayerOwner = GetOwner();
		UWorld* ComponentWorld = GetWorld();
		if (bVisualBindingInProgress
			|| bResettingPresentationSwitchState
			|| !IsValid(PlayerOwner)
			|| !ComponentWorld
			|| !IsValid(FormalWeapon)
			|| FormalWeapon->GetWorld() != ComponentWorld
			|| !IsOwnedRegisteredVisualComponent(VisualWeaponComponent))
		{
			return false;
		}

		FWeaponPresentationProfile CandidateProfile;
		const bool bProfileUsable =
			FindWeaponPresentationProfile(FormalWeapon, CandidateProfile)
			&& IsValid(CandidateProfile.VisualWeaponClass.Get())
			&& !CandidateProfile.VisualWeaponClass->HasAnyClassFlags(
				CLASS_Abstract)
			&& IsValid(VisualWeaponBaseClass.Get())
			&& CandidateProfile.VisualWeaponClass->IsChildOf(
				VisualWeaponBaseClass.Get());

		// This call is the newer presentation owner. Any transaction still inside a
		// synchronous callback above or below must fail its postconditions.
		TGuardValue<bool> BindingReentrancyGuard(
			bVisualBindingInProgress, true);
		const uint64 RequestSerial = ++VisualBindingSerial;

		{
			// SetWeaponForPresentation must still clear the previous session when
			// the formal weapon changes, but it must not reject this binder's call.
			TGuardValue<bool> OwnedSetWeaponGuard(
				bNativeVisualBindingSetWeaponCall, true);
			SetWeaponForPresentation(FormalWeapon);
		}

		const auto IsRequestCurrent =
			[this, RequestSerial, FormalWeapon, ComponentWorld,
				VisualWeaponComponent]()
		{
			return VisualBindingSerial == RequestSerial
				&& !bResettingPresentationSwitchState
				&& BoundWeapon.Get() == FormalWeapon
				&& IsValid(FormalWeapon)
				&& FormalWeapon->GetWorld() == ComponentWorld
				&& IsOwnedRegisteredVisualComponent(VisualWeaponComponent);
		};

		if (!IsRequestCurrent())
		{
			return false;
		}

		const auto FailOwnedVisualRequest =
			[this, RequestSerial, FormalWeapon, VisualWeaponComponent]()
		{
			// A stale failure must not clear the newer owner's state.
			if (VisualBindingSerial != RequestSerial
				|| BoundWeapon.Get() != FormalWeapon)
			{
				return;
			}

			ReleaseTrackedVisualWeapon(true);
			HideVisualWeaponSlot(VisualWeaponComponent);
			// The weapon stays bound for gameplay, but a failed visual switch must
			// not leave the reload/draw gate latched forever.
			bPresentationSwitchingNative = false;
		};

		if (!bProfileUsable)
		{
			FailOwnedVisualRequest();
			return false;
		}

		AActor* VisualWeapon = VisualWeaponComponent->GetChildActor();
		if (!IsValid(VisualWeapon)
			|| !VisualWeapon->IsA(CandidateProfile.VisualWeaponClass.Get()))
		{
			// SetChildActorClass recreates registered children even for the same class.
			VisualWeapon = TryInitializeVisualWeapon(
				FormalWeapon, VisualWeaponComponent);
			if (!IsRequestCurrent())
			{
				return false;
			}
		}

		if (!IsValid(VisualWeapon)
			|| VisualWeaponComponent->GetChildActor() != VisualWeapon)
		{
			FailOwnedVisualRequest();
			return false;
		}

		AActor* ActivatedVisual = nullptr;
		FWeaponPresentationProfile ActivatedProfile;
		if (!TryActivateVisualWeapon(
				FormalWeapon,
				VisualWeaponComponent,
				ActivatedVisual,
				ActivatedProfile)
			|| ActivatedVisual != VisualWeapon
			|| !IsRequestCurrent()
			|| VisualWeaponComponent->GetChildActor() != ActivatedVisual
			|| !IsValid(ActivatedProfile.VisualWeaponClass.Get())
			|| !ActivatedVisual->IsA(ActivatedProfile.VisualWeaponClass.Get()))
		{
			FailOwnedVisualRequest();
			return false;
		}

		// Commit outputs only after every postcondition holds.
		BoundVisualFormalWeapon = FormalWeapon;
		BoundVisualWeaponComponent = VisualWeaponComponent;
		BoundVisualWeaponActor = ActivatedVisual;
		BoundVisualSessionSerial = RequestSerial;

		OutVisualWeapon = ActivatedVisual;
		OutProfile = ActivatedProfile;
		OutBindingId = static_cast<int64>(RequestSerial);
		return true;
	}
	bool UWeaponPresentationComponent::IsCurrentVisualWeapon(
		AWeaponRuntime* FormalWeapon, AActor* VisualWeapon, int64 BindingId) const
	{
		if (bVisualBindingInProgress || bResettingPresentationSwitchState)
		{
			return false;
		}

		AWeaponRuntime* TrackedFormal = BoundVisualFormalWeapon.Get();
		UChildActorComponent* TrackedComponent =
			BoundVisualWeaponComponent.Get();
		AActor* TrackedVisual = BoundVisualWeaponActor.Get();
		return BoundVisualSessionSerial != 0
			&& BindingId > 0
			&& static_cast<uint64>(BindingId) == BoundVisualSessionSerial
			&& BoundVisualSessionSerial == VisualBindingSerial
			&& IsValid(FormalWeapon)
			&& FormalWeapon == BoundWeapon.Get()
			&& FormalWeapon == TrackedFormal
			&& IsValid(VisualWeapon)
			&& VisualWeapon == TrackedVisual
			&& IsOwnedRegisteredVisualComponent(TrackedComponent)
			&& TrackedComponent->GetChildActor() == VisualWeapon;
	}
	void UWeaponPresentationComponent::ClearVisualWeaponPresentation()
	{
		ResetNativeSway();
		ClearNativeGunPushback();
		UChildActorComponent* TrackedComponent =
			BoundVisualWeaponComponent.Get();
		AActor* TrackedVisual = BoundVisualWeaponActor.Get();

		++VisualBindingSerial;
		BoundVisualFormalWeapon.Reset();
		BoundVisualWeaponComponent.Reset();
		BoundVisualWeaponActor.Reset();
		BoundVisualSessionSerial = 0;

		if (!IsValid(TrackedVisual) || !IsValid(TrackedComponent)
			|| TrackedComponent->GetOwner() != GetOwner()
			|| TrackedVisual->GetOwner() != GetOwner()
			|| TrackedComponent->GetChildActor() != TrackedVisual)
		{
			return;
		}

		TrackedVisual->SetActorHiddenInGame(true);
		TrackedVisual->SetActorTickEnabled(false);
	}
	bool UWeaponPresentationComponent::CanStartReloadPresentation(
		AWeaponRuntime* FormalWeapon,
		bool bPresentationSwitching) const
	{
		if (bResettingPresentationSwitchState || !IsValid(FormalWeapon)
			|| BoundWeapon.Get() != FormalWeapon
			|| bPresentationSwitching
			|| IsPresentationSwitching()
			|| bFullHolsterActive
			|| bPresentationSwitchCommitPending
			|| bFastDrawTransitionPending)
		{
			return false;
		}

		if (ReloadBlockingDrawWeapon.Get() == FormalWeapon)
		{
			for (int32 Layer = 0; Layer < 2; ++Layer)
			{
				UAnimInstance* AnimInstance =
					ReloadBlockingDrawAnimInstances[Layer].Get();
				const int32 InstanceId =
					ReloadBlockingDrawInstanceIds[Layer];
				if (!IsValid(AnimInstance) || InstanceId == INDEX_NONE)
				{
					continue;
				}

				FAnimMontageInstance* Instance =
					AnimInstance->GetMontageInstanceForID(InstanceId);
				if (Instance && Instance->IsValid())
				{
					return false;
				}
			}
		}

		return true;
	}
	void UWeaponPresentationComponent::ClearReloadBlockingDrawSnapshot()
	{
		ReloadBlockingDrawWeapon.Reset();
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			ReloadBlockingDrawAnimInstances[Layer].Reset();
			ReloadBlockingDrawInstanceIds[Layer] = INDEX_NONE;
		}
	}
