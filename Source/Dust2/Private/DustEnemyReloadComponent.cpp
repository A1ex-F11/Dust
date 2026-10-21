#include "DustEnemyReloadComponent.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "WeaponDefinitionData.h"
#include "WeaponRuntime.h"

DEFINE_LOG_CATEGORY_STATIC(LogDustEnemyReload, Log, All);

namespace
{
	constexpr float MinAnimationPlayRate = 0.01f;

	/** A visual mesh belongs to this enemy or to a visual weapon actor attached to it. Never a player mesh. */
	bool IsEnemyVisualMesh(const USkeletalMeshComponent* Mesh, const AActor* Enemy)
	{
		if (!IsValid(Mesh) || !IsValid(Enemy)) return false;
		const AActor* MeshOwner = Mesh->GetOwner();
		if (!IsValid(MeshOwner)) return false;
		if (MeshOwner == Enemy) return true;
		// Reject another pawn's body outright.
		if (Cast<APawn>(MeshOwner) && MeshOwner != Enemy) return false;
		if (MeshOwner->GetOwner() == Enemy) return true;
		const USceneComponent* MeshRoot = MeshOwner->GetRootComponent();
		return IsValid(MeshRoot) && MeshRoot->GetAttachParentActor() == Enemy;
	}
}

UDustEnemyReloadComponent::UDustEnemyReloadComponent()
{
	// StateTree tasks drive all updates; no free-running behavior state machine.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

UDustEnemyWeaponComponent* UDustEnemyReloadComponent::GetWeaponComponent() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return IsValid(Pawn) ? Pawn->FindComponentByClass<UDustEnemyWeaponComponent>() : nullptr;
}

AWeaponRuntime* UDustEnemyReloadComponent::GetWeaponRuntime() const
{
	const UDustEnemyWeaponComponent* Weapon = GetWeaponComponent();
	return IsValid(Weapon) ? Weapon->GetWeaponRuntime() : nullptr;
}

bool UDustEnemyReloadComponent::IsCombatLifeValid() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn)) return false;
	const UDustEnemyCombatAdapterComponent* Adapter = nullptr;
	if (const AAIController* Controller = Cast<AAIController>(Pawn->GetController()))
		Adapter = Controller->FindComponentByClass<UDustEnemyCombatAdapterComponent>();
	if (!Adapter) Adapter = Pawn->FindComponentByClass<UDustEnemyCombatAdapterComponent>();
	// The adapter's real read-only gate: life + not interrupting + legal known target + not searching.
	return IsValid(Adapter) && Adapter->CanRunReloadLogic();
}

void UDustEnemyReloadComponent::SetReloadAnimationSources(USkeletalMeshComponent* CharacterMeshComponent, USkeletalMeshComponent* WeaponMeshComponent)
{
	CharacterMesh = IsEnemyVisualMesh(CharacterMeshComponent, GetOwner()) ? CharacterMeshComponent : nullptr;
	WeaponMesh = IsEnemyVisualMesh(WeaponMeshComponent, GetOwner()) ? WeaponMeshComponent : nullptr;
	if (CharacterMeshComponent && !CharacterMesh.IsValid())
		LastReloadDiagnostic = TEXT("Rejected character mesh: not owned by this enemy");
	if (WeaponMeshComponent && !WeaponMesh.IsValid())
		LastReloadDiagnostic = TEXT("Rejected weapon mesh: not this enemy's attached visual weapon");
}

bool UDustEnemyReloadComponent::CanReload() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn) || !GetWorld() || !IsCombatLifeValid()) return false;

	const UDustEnemyWeaponComponent* Weapon = GetWeaponComponent();
	const AWeaponRuntime* Runtime = IsValid(Weapon) ? Weapon->GetWeaponRuntime() : nullptr;
	const UWeaponDefinitionData* Definition = IsValid(Weapon) ? Weapon->WeaponDefinition.Get() : nullptr;
	if (!IsValid(Runtime) || !IsValid(Definition) || Runtime->WeaponDefinition.Get() != Definition) return false;
	// Both modes still require a genuinely empty magazine.
	if (Runtime->GetCurrentAmmo() > 0) return false;
	if (!bUseInfiniteReserveAmmo && Runtime->GetReserveAmmo() <= 0) return false;

	if (Definition->UsesEmptyBatchReload())
	{
		// R08-style reloads commit one fixed batch; with the infinite source the captured
		// amount is the full magazine, while finite mode keeps min(missing, reserve).
		return ComputeReloadFillAmount(*Runtime) > 0;
	}

	// Original ordinary-magazine path. Pure per-round/sniper definitions are not extended by
	// this policy in either mode.
	return !Definition->UsesPerRoundReload();
}

int32 UDustEnemyReloadComponent::ComputeReloadFillAmount(const AWeaponRuntime& Runtime) const
{
	const int32 Missing = FMath::Max(0, Runtime.GetMagazineCapacity() - Runtime.GetCurrentAmmo());
	if (bUseInfiniteReserveAmmo) return Missing;
	return FMath::Min(Missing, FMath::Max(0, Runtime.GetReserveAmmo()));
}

int32 UDustEnemyReloadComponent::ComputeEmptyBatchReloadAmount(const AWeaponRuntime& Runtime) const
{
	return FMath::Min(Runtime.GetMagazineCapacity() - Runtime.GetCurrentAmmo(), Runtime.GetReserveAmmo());
}

bool UDustEnemyReloadComponent::IsReloadReady() const
{
	if (!CanReload()) return false;
	const UWorld* World = GetWorld();
	return IsValid(World) && World->GetTimeSeconds() >= NextReloadAllowedAt;
}

void UDustEnemyReloadComponent::ApplyFailureCooldown()
{
	if (UWorld* World = GetWorld())
		NextReloadAllowedAt = World->GetTimeSeconds() + (FMath::IsFinite(FailureCooldownSeconds) ? FMath::Max(0.01f, FailureCooldownSeconds) : 2.0f);
}

float UDustEnemyReloadComponent::ComputeWatchdogSeconds() const
{
	float LongestSeconds = 0.0f;
	const auto AddMontageDuration = [this, &LongestSeconds](const UAnimMontage* Montage)
	{
		if (!IsValid(Montage)) return;
		const float PlayRate = FMath::Max(MinAnimationPlayRate, ActiveReloadPlayRate);
		const float RateScale = FMath::Max(MinAnimationPlayRate, Montage->RateScale);
		LongestSeconds = FMath::Max(LongestSeconds, Montage->GetPlayLength() / (PlayRate * RateScale));
	};
	// Captured request resources, never the live component fields or the DataAsset.
	AddMontageDuration(ActiveCharacterMontage.Get());
	AddMontageDuration(ActiveWeaponMontage.Get());
	// Grace only; expiry cancels the transaction rather than committing ammo.
	return LongestSeconds + 1.0f;
}

bool UDustEnemyReloadComponent::PlayReloadMontage(uint64 Token, UAnimInstance* AnimInstance, UAnimMontage* Montage, bool bCharacterPath)
{
	if (!IsValid(AnimInstance) || !IsValid(Montage)) return false;

	// Record existing instances so we can identify exactly the one this call creates even if
	// Montage_Play synchronously runs external OnMontageStarted callbacks.
	TSet<int32> ExistingInstanceIds;
	for (const FAnimMontageInstance* Existing : AnimInstance->MontageInstances)
		if (Existing) ExistingInstanceIds.Add(Existing->GetInstanceID());

	const float PlayRate = FMath::Max(MinAnimationPlayRate, ActiveReloadPlayRate);
	// Do not stop unrelated montages on this enemy; only our exact instance is tracked.
	const float PlayLength = AnimInstance->Montage_Play(
		Montage, PlayRate, EMontagePlayReturnType::MontageLength, 0.0f, false);
	if (PlayLength <= 0.0f) return false;

	FAnimMontageInstance* CreatedInstance = nullptr;
	for (FAnimMontageInstance* Candidate : AnimInstance->MontageInstances)
	{
		if (Candidate && Candidate->Montage == Montage && !ExistingInstanceIds.Contains(Candidate->GetInstanceID()))
		{
			CreatedInstance = Candidate;
			break;
		}
	}
	if (!CreatedInstance) return false;

	if (Token != ActiveReloadToken)
	{
		// A synchronous external stop already invalidated our transaction; still stop exactly the
		// instance created here so no new montage is left running.
		CreatedInstance->OnMontageEnded.Unbind();
		FMontageBlendSettings StopSettings(0.0f);
		CreatedInstance->Stop(StopSettings, true);
		return false;
	}

	if (!CreatedInstance->IsPlaying()) return false;

	FOnMontageEnded EndDelegate;
	if (bCharacterPath)
		EndDelegate.BindUObject(this, &UDustEnemyReloadComponent::HandleCharacterMontageEnded, Token);
	else
		EndDelegate.BindUObject(this, &UDustEnemyReloadComponent::HandleWeaponMontageEnded, Token);
	// Bind to the exact instance we just created; a later replay of the same asset is unaffected.
	CreatedInstance->OnMontageEnded = EndDelegate;

	if (bCharacterPath)
	{
		CharacterAnimInstance = AnimInstance;
		CharacterMontageInstanceId = CreatedInstance->GetInstanceID();
	}
	else
	{
		WeaponAnimInstance = AnimInstance;
		WeaponMontageInstanceId = CreatedInstance->GetInstanceID();
	}
	return true;
}

void UDustEnemyReloadComponent::StopOwnedMontages()
{
	const auto StopExactInstance = [](UAnimInstance* AnimInstance, int32 InstanceId)
	{
		if (!IsValid(AnimInstance) || InstanceId == INDEX_NONE) return;
		FAnimMontageInstance* Instance = AnimInstance->GetMontageInstanceForID(InstanceId);
		// Our instance is already gone: never stop a newer instance of the same asset.
		if (!Instance) return;
		Instance->OnMontageEnded.Unbind();
		FMontageBlendSettings StopSettings(0.0f);
		Instance->Stop(StopSettings, true);
	};
	StopExactInstance(CharacterAnimInstance.Get(), CharacterMontageInstanceId);
	StopExactInstance(WeaponAnimInstance.Get(), WeaponMontageInstanceId);
}

void UDustEnemyReloadComponent::HandleCharacterMontageEnded(UAnimMontage* Montage, bool bInterrupted, uint64 Token)
{
	OnMontagePathEnded(Token, Montage, bInterrupted, true);
}

void UDustEnemyReloadComponent::HandleWeaponMontageEnded(UAnimMontage* Montage, bool bInterrupted, uint64 Token)
{
	OnMontagePathEnded(Token, Montage, bInterrupted, false);
}

void UDustEnemyReloadComponent::OnMontagePathEnded(uint64 Token, UAnimMontage* Montage, bool bInterrupted, bool bCharacterPath)
{
	// Stale callback from a cancelled/previous transaction is inert.
	if (Token == 0 || Token != ActiveReloadToken) return;
	const UAnimMontage* Expected = bCharacterPath ? ActiveCharacterMontage.Get() : ActiveWeaponMontage.Get();
	if (!IsValid(Expected) || Montage != Expected) return;

	if (bInterrupted)
	{
		bMontageInterrupted = true;
		LastReloadDiagnostic = TEXT("Reload montage interrupted; transaction will cancel without ammo commit");
		return;
	}

	if (bCharacterPath) bCharacterMontageDone = true;
	else bWeaponMontageDone = true;
	// Every configured montage must finish naturally before the task may commit.
	if (bCharacterMontageDone && bWeaponMontageDone) bWaitingForMontageCommit = true;
}

int32 UDustEnemyReloadComponent::PredictNextReloadRequestId(const AWeaponRuntime* Runtime)
{
	if (!IsValid(Runtime)) return 0;
	// Mirrors AWeaponRuntime::TryStartReload's MAX_int32 -> 1 wrap rule.
	const int32 Current = Runtime->GetReloadRequestId();
	return Current == MAX_int32 ? 1 : Current + 1;
}

bool UDustEnemyReloadComponent::IsReloadIdentityCurrent(uint64 Token) const
{
	if (Token == 0 || Token != ActiveReloadToken) return false;
	if (!IsCombatLifeValid()) return false;
	const AWeaponRuntime* Runtime = ActiveRuntime.Get();
	const UDustEnemyWeaponComponent* Weapon = ActiveWeaponComponent.Get();
	const UWeaponDefinitionData* Definition = ActiveReloadDefinition.Get();
	return IsValid(Runtime) && IsValid(Weapon) && IsValid(Definition)
		&& Weapon->GetWeaponRuntime() == Runtime
		&& Weapon->WeaponDefinition.Get() == Definition
		&& Runtime->WeaponDefinition.Get() == Definition
		&& Runtime->GetReloadRequestId() == ActiveReloadRequestId
		&& Runtime->IsInfiniteReloadSource() == bActiveReloadUsesInfiniteSource
		&& Runtime->IsReloading();
}

uint64 UDustEnemyReloadComponent::TryStartReload()
{
	if (ActiveReloadToken != 0) return 0;
	UWorld* World = GetWorld();
	if (!IsValid(World) || !CanReload()) return 0;
	if (World->GetTimeSeconds() < NextReloadAllowedAt) return 0;

	UDustEnemyWeaponComponent* Weapon = GetWeaponComponent();
	AWeaponRuntime* Runtime = IsValid(Weapon) ? Weapon->GetWeaponRuntime() : nullptr;
	UWeaponDefinitionData* Definition = IsValid(Weapon) ? Weapon->WeaponDefinition.Get() : nullptr;
	if (!IsValid(Weapon) || !IsValid(Runtime) || !IsValid(Definition)) return 0;

	// Resolve the whole request before touching the runtime: for an empty-batch or infinite
	// request the actual fill amount is fixed here; both supported styles then read the same
	// two fixed actions. No ammo changes and no transaction starts during this step.
	const bool bEmptyBatch = Definition->UsesEmptyBatchReload();
	const bool bInfiniteSource = bUseInfiniteReserveAmmo;
	int32 Amount = 0;
	if (bInfiniteSource || bEmptyBatch)
	{
		Amount = ComputeReloadFillAmount(*Runtime);
		if (Amount <= 0)
		{
			LastReloadDiagnostic = bInfiniteSource
				? TEXT("Infinite empty reload has no positive refill amount; no fallback")
				: TEXT("Empty-batch reload has no positive amount; no fallback");
			ApplyFailureCooldown();
			return 0;
		}
	}
	UAnimMontage* const SelectedCharacterMontage = CharacterReloadMontage;
	UAnimMontage* const SelectedWeaponMontage = WeaponReloadMontage;

	const bool bNeedsCharacter = IsValid(SelectedCharacterMontage);
	const bool bNeedsWeapon = IsValid(SelectedWeaponMontage);
	UAnimInstance* CharacterAnim = nullptr;
	UAnimInstance* WeaponAnim = nullptr;

	// Configured actions require valid sources; never silently fall back to the logic timer.
	if (bNeedsCharacter)
	{
		const USkeletalMeshComponent* Mesh = CharacterMesh.Get();
		CharacterAnim = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
		if (!IsValid(CharacterAnim))
		{
			LastReloadDiagnostic = TEXT("Character reload montage configured but enemy character mesh/AnimInstance is missing; failed with cooldown");
			ApplyFailureCooldown();
			return 0;
		}
	}
	if (bNeedsWeapon)
	{
		const USkeletalMeshComponent* Mesh = WeaponMesh.Get();
		WeaponAnim = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
		if (!IsValid(WeaponAnim))
		{
			LastReloadDiagnostic = TEXT("Weapon reload montage configured but attached weapon mesh/AnimInstance is missing; failed with cooldown");
			ApplyFailureCooldown();
			return 0;
		}
	}

	// Take ownership BEFORE the runtime entry: the runtime synchronously broadcasts OnReloadStarted
	// and an external listener may cancel/restart inside that callback.
	const uint64 Token = ++ReloadGeneration;
	ActiveReloadToken = Token;
	ActiveWeaponComponent = Weapon;
	ActiveRuntime = Runtime;
	ActiveReloadDefinition = Definition;
	ActiveReloadRequestId = PredictNextReloadRequestId(Runtime);
	ActiveReloadAmount = (bEmptyBatch || bInfiniteSource) ? Amount : 0;
	ActiveReloadPlayRate = FMath::IsFinite(ReloadAnimationPlayRate)
		? FMath::Max(MinAnimationPlayRate, ReloadAnimationPlayRate)
		: 1.0f;
	bActiveReloadUsesEmptyBatch = !bInfiniteSource && bEmptyBatch;
	// Set before the runtime publishes OnReloadStarted so external listeners already observe
	// this request's captured source mode.
	bActiveReloadUsesInfiniteSource = bInfiniteSource;
	ActiveCharacterMontage = SelectedCharacterMontage;
	ActiveWeaponMontage = SelectedWeaponMontage;
	bHasCharacterMontage = bNeedsCharacter;
	bHasWeaponMontage = bNeedsWeapon;
	bCharacterMontageDone = !bNeedsCharacter;
	bWeaponMontageDone = !bNeedsWeapon;
	bWaitingForMontageCommit = false;
	bMontageInterrupted = false;
	ReloadElapsedSeconds = 0.0f;
	bMontageMode = bNeedsCharacter || bNeedsWeapon;
	WatchdogSeconds = bMontageMode ? ComputeWatchdogSeconds() : 0.0f;
	LogicalRemainingSeconds = FMath::Max(0.0f, LogicalReloadSeconds);

	const bool bStarted = bInfiniteSource
		? Runtime->TryStartInfiniteEmptyReload()
		: (bEmptyBatch ? Runtime->TryStartEmptyBatchReload(Amount) : Runtime->TryStartReload());
	if (!bStarted || !IsReloadIdentityCurrent(Token))
	{
		// Only release this attempt's identity; a synchronously started newer request is left alone.
		if (ActiveReloadToken == Token) AbandonReloadOperation();
		return 0;
	}

	if (bMontageMode)
	{
		const bool bCharacterPlayed = !bNeedsCharacter
			|| PlayReloadMontage(Token, CharacterAnim, SelectedCharacterMontage, true);
		// Montage playback can run external animation callbacks; re-check ownership after each play.
		if (!bCharacterPlayed || !IsReloadIdentityCurrent(Token) || bMontageInterrupted)
		{
			if (ActiveReloadToken == Token) AbandonReloadOperation();
			LastReloadDiagnostic = TEXT("Reload montage failed to play or was synchronously stopped; cancelled with cooldown");
			return 0;
		}

		const bool bWeaponPlayed = !bNeedsWeapon
			|| PlayReloadMontage(Token, WeaponAnim, SelectedWeaponMontage, false);
		if (!bWeaponPlayed || !IsReloadIdentityCurrent(Token) || bMontageInterrupted)
		{
			if (ActiveReloadToken == Token) AbandonReloadOperation();
			LastReloadDiagnostic = TEXT("Reload montage failed to play or was synchronously stopped; cancelled with cooldown");
			return 0;
		}
	}

	// Final ownership/identity re-check before handing the token to the task.
	if (!IsReloadIdentityCurrent(Token))
	{
		if (ActiveReloadToken == Token) AbandonReloadOperation();
		return 0;
	}

	LastReloadDiagnostic = bMontageMode
		? TEXT("Reloading; commit waits for every configured montage to end naturally")
		: TEXT("Reloading; logic timer mode (no montage configured)");
	return Token;
}

EDustEnemyReloadStatus UDustEnemyReloadComponent::UpdateReload(uint64 Token, float DeltaTime)
{
	if (Token == 0 || Token != ActiveReloadToken) return EDustEnemyReloadStatus::Failed;

	if (!IsReloadIdentityCurrent(Token))
	{
		// Drops only our stale ownership; a runtime request that already changed is never cancelled.
		AbandonReloadOperation();
		return EDustEnemyReloadStatus::Failed;
	}

	const float SafeDelta = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0f, DeltaTime) : 0.0f;
	if (bMontageMode)
	{
		if (bMontageInterrupted)
		{
			AbandonReloadOperation();
			return EDustEnemyReloadStatus::Failed;
		}
		ReloadElapsedSeconds += SafeDelta;
		if (!bWaitingForMontageCommit && ReloadElapsedSeconds > WatchdogSeconds)
		{
			LastReloadDiagnostic = TEXT("Reload montage watchdog expired; cancel only, never commit ammo");
			AbandonReloadOperation();
			return EDustEnemyReloadStatus::Failed;
		}
		return bWaitingForMontageCommit ? EDustEnemyReloadStatus::Succeeded : EDustEnemyReloadStatus::Running;
	}

	LogicalRemainingSeconds -= SafeDelta;
	return LogicalRemainingSeconds <= 0.0f ? EDustEnemyReloadStatus::Succeeded : EDustEnemyReloadStatus::Running;
}

bool UDustEnemyReloadComponent::FinishReload(uint64 Token)
{
	if (Token == 0 || Token != ActiveReloadToken) return false;

	if (!IsReloadIdentityCurrent(Token))
	{
		AbandonReloadOperation();
		return false;
	}

	if (bMontageInterrupted)
	{
		LastReloadDiagnostic = TEXT("Reload montage was interrupted; finish cancelled without ammo");
		AbandonReloadOperation();
		return false;
	}

	if (bActiveReloadUsesInfiniteSource)
	{
		// The captured request must still be this exact unlimited-source transaction and the
		// magazine must still be empty with exactly the captured full-magazine refill amount.
		// Later config edits cannot change the committed amount or source.
		AWeaponRuntime* Runtime = ActiveRuntime.Get();
		const int32 Missing = IsValid(Runtime)
			? FMath::Max(0, Runtime->GetMagazineCapacity() - Runtime->GetCurrentAmmo()) : 0;
		if (!IsValid(Runtime) || !Runtime->IsInfiniteReloadSource()
			|| Runtime->GetCurrentAmmo() != 0
			|| ActiveReloadAmount <= 0 || Missing != ActiveReloadAmount)
		{
			LastReloadDiagnostic = TEXT("Infinite reload source/amount changed; cancelled without ammo");
			AbandonReloadOperation();
			return false;
		}
	}
	else if (bActiveReloadUsesEmptyBatch)
	{
		// Reserve/magazine/rule changes during the transaction would make the committed amount
		// differ from the captured batch amount and the fixed montage that was played.
		const UWeaponDefinitionData* Definition = ActiveReloadDefinition.Get();
		const int32 CurrentAmount = IsValid(Definition)
			? ComputeEmptyBatchReloadAmount(*ActiveRuntime.Get()) : 0;
		if (!IsValid(Definition) || !Definition->UsesEmptyBatchReload()
			|| ActiveRuntime->GetCurrentAmmo() != 0 || ActiveRuntime->GetReserveAmmo() <= 0
			|| CurrentAmount != ActiveReloadAmount)
		{
			LastReloadDiagnostic = TEXT("Empty-batch reload amount/rule changed; cancelled without ammo");
			AbandonReloadOperation();
			return false;
		}
	}

	// Only a completed transaction may commit; the task calls this after Update reports success.
	const bool bCompletionReached = bMontageMode
		? bWaitingForMontageCommit
		: LogicalRemainingSeconds <= 0.0f;
	if (!bCompletionReached)
	{
		AbandonReloadOperation();
		return false;
	}

	AWeaponRuntime* Runtime = ActiveRuntime.Get();
	const bool bCommitted = IsValid(Runtime) && Runtime->FinishReload();
	// Success releases busy without cooldown; the runtime is now the only owner of the new ammo.
	ActiveReloadToken = 0;
	++ReloadGeneration;
	ClearReloadOperationState();
	if (!bCommitted) ApplyFailureCooldown();
	LastReloadDiagnostic = bCommitted
		? TEXT("Reload committed once by WeaponRuntime::FinishReload")
		: TEXT("WeaponRuntime refused final commit; no ammo change");
	return bCommitted;
}

void UDustEnemyReloadComponent::CancelReload(uint64 Token)
{
	if (Token == 0 || Token != ActiveReloadToken) return;
	AbandonReloadOperation();
	LastReloadDiagnostic = TEXT("Reload cancelled without ammo commit; cooldown applied");
}

void UDustEnemyReloadComponent::AbandonReloadOperation()
{
	if (ActiveReloadToken == 0) return;

	AWeaponRuntime* Runtime = ActiveRuntime.Get();
	UDustEnemyWeaponComponent* Weapon = ActiveWeaponComponent.Get();
	const int32 RequestId = ActiveReloadRequestId;

	// Invalidate the token BEFORE stopping montages so callbacks from our own stop are stale.
	ActiveReloadToken = 0;
	++ReloadGeneration;
	StopOwnedMontages();

	// Cancel only this exact transaction; if an external path already created a newer request, leave it.
	const bool bStillOurs = IsValid(Runtime) && Runtime->IsReloading()
		&& Runtime->GetReloadRequestId() == RequestId
		&& (!IsValid(Weapon) || Weapon->GetWeaponRuntime() == Runtime);
	if (bStillOurs) Runtime->CancelReload();

	ClearReloadOperationState();
	ApplyFailureCooldown();
}

void UDustEnemyReloadComponent::ClearReloadOperationState()
{
	ActiveReloadRequestId = 0;
	ActiveWeaponComponent.Reset();
	ActiveRuntime.Reset();
	ActiveReloadDefinition.Reset();
	ActiveCharacterMontage = nullptr;
	ActiveWeaponMontage = nullptr;
	ActiveReloadAmount = 0;
	ActiveReloadPlayRate = 1.0f;
	bActiveReloadUsesEmptyBatch = false;
	bActiveReloadUsesInfiniteSource = false;
	CharacterAnimInstance.Reset();
	WeaponAnimInstance.Reset();
	CharacterMontageInstanceId = INDEX_NONE;
	WeaponMontageInstanceId = INDEX_NONE;
	bMontageMode = false;
	bHasCharacterMontage = false;
	bHasWeaponMontage = false;
	bCharacterMontageDone = true;
	bWeaponMontageDone = true;
	bWaitingForMontageCommit = false;
	bMontageInterrupted = false;
	ReloadElapsedSeconds = 0.0f;
	WatchdogSeconds = 0.0f;
	LogicalRemainingSeconds = 0.0f;
}

void UDustEnemyReloadComponent::StopAllOperations()
{
	if (ActiveReloadToken != 0) CancelReload(ActiveReloadToken);
}

void UDustEnemyReloadComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopAllOperations();
	CharacterMesh.Reset();
	WeaponMesh.Reset();
	Super::EndPlay(Reason);
}
