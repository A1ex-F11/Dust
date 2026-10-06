
#include "WeaponPresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
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
	CancelPresentationSwitchCommit(false);
	ReleaseFullHolsterPresentation(true);
	PrepareDrawPresentationWatch();
	CancelFastDrawTransition();
	CancelReloadWeaponPresentation();
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
	CancelPresentationSwitchCommit(false);
	ReleaseFullHolsterPresentation(true);
	PrepareDrawPresentationWatch();
	CancelFastDrawTransition();
	CancelReloadWeaponPresentation();
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
bool UWeaponPresentationComponent::TryPlayPresentationMontage(
	USkeletalMeshComponent* Mesh,
	UAnimMontage* Montage,
	float StartTime)
{
	if (!IsValid(Mesh) || !IsValid(Montage)
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

bool UWeaponPresentationComponent::StartReloadWeaponPresentation(
	AWeaponRuntime* FormalWeapon,
	USkeletalMeshComponent* WeaponMesh,
	UAnimMontage* Montage)
{
	if (!IsValid(FormalWeapon) ||
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

	// ��ʹ����ί���Ѹ��ƽ����У���serialҲ��ʹ��ʧЧ��
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
	ReleaseReloadWeaponPlayback(bStopMontage);
	StopReloadPresentationSounds();
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
	if (Serial != DrawWatchSerial)
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
	PrepareDrawPresentationWatch();
	OnDrawPresentationFinished.Broadcast(true);
}
void UWeaponPresentationComponent::InterruptDrawPresentationWatch()
{
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
		if (bFullHolsterActive ||
			!IsValid(FormalWeapon) ||
			FormalWeapon != BoundWeapon.Get() ||
			!IsValid(CharacterMesh) ||
			FullHolsterFinishedNotifyName.IsNone())
		{
			return false;
		}

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
		if (!IsFullHolsterPresentationCurrent() ||
			bFullHolsterLaunching ||
			!bFullHolsterLayerCompleted[0] ||
			!bFullHolsterLayerCompleted[1])
		{
			return;
		}

		ReleaseFullHolsterPresentation(false);
		OnFullHolsterPresentationReady.Broadcast();
	}

	void UWeaponPresentationComponent::ReleaseFullHolsterPresentation(
		bool bStopMontages)
	{
		++FullHolsterSerial;
		bFullHolsterActive = false;
		bFullHolsterLaunching = false;
		ActiveFullHolsterWeapon.Reset();

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
		if (!IsValid(FormalWeapon)
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

	void UWeaponPresentationComponent::HandlePresentationSwitchCommitTimer(
		uint64 Serial)
	{
		if (Serial != PresentationSwitchCommitSerial
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
	if (!IsValid(PlayerOwner)
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
		|| !CandidateProfile.bUseTacticalPresentation
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
