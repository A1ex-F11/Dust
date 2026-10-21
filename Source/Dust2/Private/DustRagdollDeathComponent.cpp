#include "DustRagdollDeathComponent.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyReloadComponent.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "TimerManager.h"
#include "WeaponInventoryComponent.h"
#include "WeaponPresentationComponent.h"
#include "WeaponRuntime.h"

namespace
{
	constexpr float MaxDeathImpulseSpeed = 1000.0f;
}

UDustRagdollDeathComponent::UDustRagdollDeathComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

USkeletalMeshComponent* UDustRagdollDeathComponent::GetDeathMesh() const
{
	return BodyMesh.Get();
}

bool UDustRagdollDeathComponent::IsDeathStarted() const
{
	return bDeathStarted;
}

bool UDustRagdollDeathComponent::IsRagdollActive() const
{
	return bRagdollActive;
}

bool UDustRagdollDeathComponent::InitializeDeathPresentation(
	USkeletalMeshComponent* InBodyMesh,
	const TArray<USceneComponent*>& InHideOnDeath)
{
	if (bDeathStarted || bResetInProgress)
	{
		LastDeathDiagnostic = TEXT("Initialization rejected while death presentation is active or resetting.");
		return false;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		LastDeathDiagnostic = TEXT("Initialization requires an ACharacter owner.");
		return false;
	}

	if (!IsValid(InBodyMesh) || InBodyMesh->GetOwner() != OwnerCharacter)
	{
		LastDeathDiagnostic = TEXT("BodyMesh must be a valid skeletal mesh owned by this character.");
		return false;
	}

	TArray<TWeakObjectPtr<USceneComponent>> NewHideComponents;
	for (USceneComponent* Component : InHideOnDeath)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		if (Component->GetOwner() != OwnerCharacter)
		{
			LastDeathDiagnostic = TEXT("HideOnDeath accepts only scene components owned by this character.");
			return false;
		}

		bool bIsBodyOrAncestor = false;
		for (USceneComponent* Cursor = InBodyMesh; Cursor; Cursor = Cursor->GetAttachParent())
		{
			if (Cursor == Component)
			{
				bIsBodyOrAncestor = true;
				break;
			}
		}

		if (bIsBodyOrAncestor)
		{
			LastDeathDiagnostic = TEXT("HideOnDeath cannot include BodyMesh or one of its ancestors.");
			return false;
		}

		NewHideComponents.AddUnique(TWeakObjectPtr<USceneComponent>(Component));
	}

	BodyMesh = InBodyMesh;
	HideOnDeathComponents = MoveTemp(NewHideComponents);
	LastDeathDiagnostic = TEXT("Death presentation initialized. PhysicsAsset bodies will be checked when death starts.");
	return true;
}

void UDustRagdollDeathComponent::CacheDeathImpact(const FHitResult& Hit, FVector ShotDirection)
{
	if (bDeathStarted || bResetInProgress)
	{
		return;
	}

	bCachedDeathHitValid = false;
	CachedDeathHit = FHitResult();
	CachedDeathDirection = FVector::ZeroVector;

	if (Hit.GetActor() != GetOwner() || !IsFiniteVector(ShotDirection))
	{
		return;
	}

	const FVector SafeDirection = ShotDirection.GetSafeNormal();
	if (SafeDirection.IsNearlyZero() || !IsFiniteVector(SafeDirection))
	{
		return;
	}

	CachedDeathHit = Hit;
	CachedDeathDirection = SafeDirection;
	bCachedDeathHitValid = true;
}

bool UDustRagdollDeathComponent::StartRagdollDeath()
{
	if (bResetInProgress)
	{
		return false;
	}

	if (bDeathStarted)
	{
		return bRagdollActive;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		LastDeathDiagnostic = TEXT("Death start rejected because this component has no valid ACharacter owner.");
		return false;
	}

	bDeathStarted = true;
	bRagdollActive = false;
	const uint64 StartGeneration = AdvanceOperationGeneration();
	LastDeathDiagnostic = TEXT("Death presentation started; stopping combat and movement.");

	USkeletalMeshComponent* ConfiguredMesh = BodyMesh.Get();
	CaptureOriginalState(OwnerCharacter, ConfiguredMesh);
	ScheduleCorpseCleanup(StartGeneration);

	StopOwnerActivities(OwnerCharacter, StartGeneration);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	USkeletalMeshComponent* Mesh = BodyMesh.Get();
	if (!IsValid(Mesh) || Mesh->GetOwner() != OwnerCharacter)
	{
		LastDeathDiagnostic = TEXT("Death cleanup ran, but no valid same-owner BodyMesh is configured.");
		return false;
	}

	if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(0.0f);
		if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
		{
			return false;
		}
	}

	Mesh->bPauseAnims = true;
	Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	if (!ConfigureBodyCollisionForDeath(Mesh, OwnerCharacter, StartGeneration))
	{
		return false;
	}
	Mesh->SetOnlyOwnerSee(false);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetOwnerNoSee(false);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetVisibility(true, false);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetHiddenInGame(false, false);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	HideConfiguredComponents(OwnerCharacter, StartGeneration);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	if (!HasUsablePhysicsAsset(Mesh))
	{
		LastDeathDiagnostic = TEXT("Death cleanup and corpse timer are active, but the configured BodyMesh has no PhysicsAsset with a valid collision body; ragdoll was skipped.");
		return false;
	}

	const FVector InheritedVelocity = ClampMagnitudeSafe(
		SavedCharacterVelocity,
		FMath::IsFinite(MaxInheritedSpeed) ? FMath::Max(0.0f, MaxInheritedSpeed) : 0.0f);

	// Mark before any physics transition so a synchronous Reset can undo a partially applied ragdoll.
	bRagdollActive = true;
	Mesh->SetAllBodiesPhysicsBlendWeight(1.0f);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetAllBodiesSimulatePhysics(true);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetSimulatePhysics(true);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->WakeAllRigidBodies();
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	if (!Mesh->IsAnySimulatingPhysics())
	{
		bRagdollActive = false;
		LastDeathDiagnostic = TEXT("Death cleanup completed, but the configured PhysicsAsset produced no simulating bodies; corpse timer remains active.");
		return false;
	}

	if (!InheritedVelocity.IsNearlyZero())
	{
		Mesh->SetAllPhysicsLinearVelocity(InheritedVelocity, false);
		if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
		{
			return false;
		}
	}

	LastDeathDiagnostic = TEXT("Ragdoll is active. Character velocity was clamped and transferred to the body mesh.");
	ApplyCachedDeathImpulse(Mesh);
	if (!IsOperationCurrent(StartGeneration, OwnerCharacter))
	{
		return false;
	}

	return true;
}

void UDustRagdollDeathComponent::ResetDeathPresentation()
{
	if (!bDeathStarted || bResetInProgress)
	{
		return;
	}

	// Invalidate callbacks before clearing timers or touching physics/attachments.
	bDeathStarted = false;
	const uint64 ResetGeneration = AdvanceOperationGeneration();
	const bool bWasRagdollActive = bRagdollActive;
	bRagdollActive = false;
	bResetInProgress = true;
	ClearCorpseTimer();

	ACharacter* OwnerCharacter = SavedCharacter.Get();
	if (!IsValid(OwnerCharacter) || OwnerCharacter != GetOwner())
	{
		SavedHideVisibility.Reset();
		bHasSavedBodyState = false;
		bHasSavedMovementState = false;
		bHasSavedCapsuleState = false;
		bCachedDeathHitValid = false;
		CachedDeathHit = FHitResult();
		CachedDeathDirection = FVector::ZeroVector;
		bResetInProgress = false;
		LastDeathDiagnostic = TEXT("Death presentation state cleared; the original character owner is no longer valid.");
		return;
	}

	USkeletalMeshComponent* Mesh = BodyMesh.Get();
	const bool bMeshIsSameOwner = IsValid(Mesh) && Mesh->GetOwner() == OwnerCharacter;
	USceneComponent* CurrentParent = bMeshIsSameOwner ? Mesh->GetAttachParent() : nullptr;
	const bool bCurrentAttachmentIsSafe = !CurrentParent || CurrentParent->GetOwner() == OwnerCharacter;

	if (bMeshIsSameOwner && bCurrentAttachmentIsSafe && bWasRagdollActive)
	{
		Mesh->SetAllBodiesSimulatePhysics(false);
		Mesh->SetSimulatePhysics(false);
		Mesh->SetAllBodiesPhysicsBlendWeight(0.0f);
	}

	if (bMeshIsSameOwner && bCurrentAttachmentIsSafe && bHasSavedBodyState)
	{
		Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

		USceneComponent* OriginalParent = SavedBodyAttachParent.Get();
		if (bSavedBodyHadAttachParent && IsValid(OriginalParent) && OriginalParent->GetOwner() == OwnerCharacter)
		{
			if (Mesh->AttachToComponent(OriginalParent, FAttachmentTransformRules::KeepRelativeTransform, SavedBodyAttachSocket))
			{
				Mesh->SetRelativeTransform(SavedBodyRelativeTransform);
			}
		}
		// With no surviving original parent, keep the current world pose. This never moves the owner
		// back to the death location if a checkpoint has already moved it.

		Mesh->SetCollisionProfileName(SavedBodyCollisionProfile, false);
		Mesh->SetCollisionObjectType(SavedBodyObjectType);
		Mesh->SetCollisionResponseToChannels(SavedBodyCollisionResponses);
		Mesh->SetCollisionEnabled(SavedBodyCollisionEnabled);
		Mesh->SetOnlyOwnerSee(bSavedBodyOnlyOwnerSee);
		Mesh->SetOwnerNoSee(bSavedBodyOwnerNoSee);
		Mesh->SetVisibility(bSavedBodyVisible, false);
		Mesh->SetHiddenInGame(bSavedBodyHiddenInGame, false);
		Mesh->bPauseAnims = bSavedBodyPauseAnims;
	}

	for (const FVisibilitySnapshot& Snapshot : SavedHideVisibility)
	{
		USceneComponent* Component = Snapshot.Component.Get();
		if (IsValid(Component) && Component->GetOwner() == OwnerCharacter)
		{
			Component->SetVisibility(Snapshot.bVisible, false);
			Component->SetHiddenInGame(Snapshot.bHiddenInGame, false);
		}
	}

	UCapsuleComponent* Capsule = SavedCapsule.Get();
	if (bHasSavedCapsuleState && IsValid(Capsule) && Capsule->GetOwner() == OwnerCharacter)
	{
		Capsule->SetCollisionEnabled(SavedCapsuleCollisionEnabled);
	}

	UCharacterMovementComponent* Movement = SavedMovement.Get();
	if (bHasSavedMovementState && IsValid(Movement) && Movement->GetOwner() == OwnerCharacter)
	{
		Movement->SetMovementMode(static_cast<EMovementMode>(SavedMovementMode), SavedCustomMovementMode);
	}

	if (OperationGeneration == ResetGeneration)
	{
		SavedHideVisibility.Reset();
		bHasSavedBodyState = false;
		bHasSavedMovementState = false;
		bHasSavedCapsuleState = false;
		bCachedDeathHitValid = false;
		CachedDeathHit = FHitResult();
		CachedDeathDirection = FVector::ZeroVector;
		bResetInProgress = false;
		LastDeathDiagnostic = TEXT("Death presentation reset. AI and health state were left untouched.");
	}
}

void UDustRagdollDeathComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearCorpseTimer();
	bCachedDeathHitValid = false;
	CachedDeathHit = FHitResult();
	CachedDeathDirection = FVector::ZeroVector;
	SavedHideVisibility.Reset();
	Super::EndPlay(EndPlayReason);
}

void UDustRagdollDeathComponent::CaptureOriginalState(ACharacter* OwnerCharacter, USkeletalMeshComponent* Mesh)
{
	SavedCharacter = OwnerCharacter;

	if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
	{
		SavedMovement = Movement;
		SavedMovementMode = static_cast<uint8>(Movement->MovementMode.GetValue());
		SavedCustomMovementMode = Movement->CustomMovementMode;
		SavedCharacterVelocity = IsFiniteVector(Movement->Velocity) ? Movement->Velocity : FVector::ZeroVector;
		bHasSavedMovementState = true;
	}
	else
	{
		SavedMovement.Reset();
		SavedMovementMode = 0;
		SavedCustomMovementMode = 0;
		SavedCharacterVelocity = FVector::ZeroVector;
		bHasSavedMovementState = false;
	}

	if (UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent())
	{
		SavedCapsule = Capsule;
		SavedCapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
		bHasSavedCapsuleState = true;
	}
	else
	{
		SavedCapsule.Reset();
		SavedCapsuleCollisionEnabled = ECollisionEnabled::NoCollision;
		bHasSavedCapsuleState = false;
	}

	bHasSavedBodyState = IsValid(Mesh) && Mesh->GetOwner() == OwnerCharacter;
	if (bHasSavedBodyState)
	{
		SavedBodyAttachParent = Mesh->GetAttachParent();
		bSavedBodyHadAttachParent = Mesh->GetAttachParent() != nullptr;
		SavedBodyAttachSocket = Mesh->GetAttachSocketName();
		SavedBodyRelativeTransform = Mesh->GetRelativeTransform();
		SavedBodyCollisionProfile = Mesh->GetCollisionProfileName();
		SavedBodyCollisionResponses = Mesh->GetCollisionResponseToChannels();
		SavedBodyObjectType = Mesh->GetCollisionObjectType();
		SavedBodyCollisionEnabled = Mesh->GetCollisionEnabled();
		bSavedBodyOnlyOwnerSee = Mesh->bOnlyOwnerSee;
		bSavedBodyOwnerNoSee = Mesh->bOwnerNoSee;
		bSavedBodyVisible = Mesh->GetVisibleFlag();
		bSavedBodyHiddenInGame = Mesh->bHiddenInGame;
		bSavedBodyPauseAnims = Mesh->bPauseAnims;
	}
	else
	{
		SavedBodyAttachParent.Reset();
		bSavedBodyHadAttachParent = false;
		SavedBodyAttachSocket = NAME_None;
		SavedBodyRelativeTransform = FTransform::Identity;
	}

	CaptureHideVisibility(OwnerCharacter);
}

void UDustRagdollDeathComponent::CaptureHideVisibility(ACharacter* OwnerCharacter)
{
	SavedHideVisibility.Reset();
	TSet<USceneComponent*> CapturedComponents;
	USkeletalMeshComponent* Mesh = BodyMesh.Get();

	for (const TWeakObjectPtr<USceneComponent>& WeakRoot : HideOnDeathComponents)
	{
		USceneComponent* Root = WeakRoot.Get();
		if (!IsValid(Root) || Root->GetOwner() != OwnerCharacter)
		{
			continue;
		}

		TArray<USceneComponent*> ComponentsToCapture;
		Root->GetChildrenComponents(true, ComponentsToCapture);
		// GetChildrenComponents resets its output; add the configured component afterwards.
		ComponentsToCapture.AddUnique(Root);

		for (USceneComponent* Component : ComponentsToCapture)
		{
			if (!IsValid(Component) || Component->GetOwner() != OwnerCharacter || Component == Mesh || CapturedComponents.Contains(Component))
			{
				continue;
			}

			bool bIsBodyAncestor = false;
			for (USceneComponent* Cursor = Mesh; Cursor; Cursor = Cursor->GetAttachParent())
			{
				if (Cursor == Component)
				{
					bIsBodyAncestor = true;
					break;
				}
			}

			if (bIsBodyAncestor)
			{
				continue;
			}

			CapturedComponents.Add(Component);
			FVisibilitySnapshot& Snapshot = SavedHideVisibility.AddDefaulted_GetRef();
			Snapshot.Component = Component;
			Snapshot.bVisible = Component->GetVisibleFlag();
			Snapshot.bHiddenInGame = Component->bHiddenInGame;
		}
	}
}

void UDustRagdollDeathComponent::HideConfiguredComponents(ACharacter* OwnerCharacter, uint64 ExpectedGeneration)
{
	for (const FVisibilitySnapshot& Snapshot : SavedHideVisibility)
	{
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}

		USceneComponent* Component = Snapshot.Component.Get();
		if (IsValid(Component) && Component->GetOwner() == OwnerCharacter)
		{
			Component->SetVisibility(false, false);
			if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
			{
				return;
			}

			Component->SetHiddenInGame(true, false);
			if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
			{
				return;
			}
		}
	}
}

void UDustRagdollDeathComponent::StopOwnerActivities(ACharacter* OwnerCharacter, uint64 ExpectedGeneration)
{
	if (AController* Controller = OwnerCharacter->GetController())
	{
		if (UDustEnemyCombatAdapterComponent* Adapter = Controller->FindComponentByClass<UDustEnemyCombatAdapterComponent>())
		{
			Adapter->StopCombatAI(EDustEnemyAIStopReason::EnemyDeath);
			if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
			{
				return;
			}
		}
	}

	if (UCharacterMovementComponent* Movement = SavedMovement.Get())
	{
		if (IsValid(Movement) && Movement->GetOwner() == OwnerCharacter)
		{
			Movement->StopMovementImmediately();
			if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
			{
				return;
			}
			Movement->DisableMovement();
			if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
			{
				return;
			}
		}
	}

	if (UCapsuleComponent* Capsule = SavedCapsule.Get())
	{
		if (IsValid(Capsule) && Capsule->GetOwner() == OwnerCharacter)
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
			{
				return;
			}
		}
	}

	if (UDustEnemyWeaponComponent* EnemyWeapon = OwnerCharacter->FindComponentByClass<UDustEnemyWeaponComponent>())
	{
		EnemyWeapon->SetCombatEnabled(false);
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}
	}

	if (UDustEnemyReloadComponent* EnemyReload = OwnerCharacter->FindComponentByClass<UDustEnemyReloadComponent>())
	{
		EnemyReload->StopAllOperations();
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}
	}

	if (UWeaponInventoryComponent* Inventory = OwnerCharacter->FindComponentByClass<UWeaponInventoryComponent>())
	{
		TArray<AWeaponRuntime*> Runtimes;
		Runtimes.Add(Inventory->GetSlotWeaponNative(0));
		Runtimes.Add(Inventory->GetSlotWeaponNative(1));
		Runtimes.Add(Inventory->GetCurrentWeaponNative());

		TSet<AWeaponRuntime*> CancelledRuntimes;
		for (AWeaponRuntime* Runtime : Runtimes)
		{
			if (!IsValid(Runtime) || Runtime->GetOwner() != OwnerCharacter || CancelledRuntimes.Contains(Runtime))
			{
				continue;
			}

			CancelledRuntimes.Add(Runtime);
			Runtime->CancelReload();
			if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
			{
				return;
			}
		}
	}

	if (UWeaponPresentationComponent* Presentation = OwnerCharacter->FindComponentByClass<UWeaponPresentationComponent>())
	{
		Presentation->CancelPresentationSwitchCommit();
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}

		Presentation->CancelFastDrawTransition();
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}

		Presentation->CancelFullHolsterPresentation();
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}

		Presentation->CancelReloadWeaponPresentation();
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}

		Presentation->StopReloadPresentationSounds();
		if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
		{
			return;
		}
	}
}

bool UDustRagdollDeathComponent::ConfigureBodyCollisionForDeath(
	USkeletalMeshComponent* Mesh,
	ACharacter* OwnerCharacter,
	uint64 ExpectedGeneration)
{
	Mesh->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName, false);
	if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	if (!IsOperationCurrent(ExpectedGeneration, OwnerCharacter))
	{
		return false;
	}

	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	return IsOperationCurrent(ExpectedGeneration, OwnerCharacter);
}

void UDustRagdollDeathComponent::ScheduleCorpseCleanup(uint64 ExpectedGeneration)
{
	if (!bAutoDestroyOwner || !FMath::IsFinite(CorpseLifetimeSeconds) || CorpseLifetimeSeconds <= 0.0f)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		CorpseTimerGeneration = ExpectedGeneration;
		World->GetTimerManager().SetTimer(
			CorpseTimerHandle,
			this,
			&UDustRagdollDeathComponent::HandleCorpseTimerExpired,
			CorpseLifetimeSeconds,
			false);
	}
}

void UDustRagdollDeathComponent::ClearCorpseTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CorpseTimerHandle);
	}
	CorpseTimerHandle.Invalidate();
	CorpseTimerGeneration = 0;
}

void UDustRagdollDeathComponent::HandleCorpseTimerExpired()
{
	if (!bDeathStarted || CorpseTimerGeneration == 0 || CorpseTimerGeneration != OperationGeneration || !bAutoDestroyOwner)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (IsValid(OwnerActor))
	{
		OwnerActor->Destroy();
	}
}

void UDustRagdollDeathComponent::ApplyCachedDeathImpulse(USkeletalMeshComponent* Mesh)
{
	// Retain the latest impulse result for component inspection.
	const auto ReportImpulse = [this](const FString& Result)
	{
		LastDeathDiagnostic += TEXT(" ") + Result;
	};
	if (!bCachedDeathHitValid || CachedDeathHit.GetActor() != GetOwner() || !IsFiniteVector(CachedDeathDirection))
	{
		ReportImpulse(FString::Printf(TEXT("SKIPPED NoValidCachedHit Cached=%d HitOwnerMatches=%d"),
			bCachedDeathHitValid ? 1 : 0, CachedDeathHit.GetActor() == GetOwner() ? 1 : 0));
		return;
	}

	const FVector Direction = CachedDeathDirection.GetSafeNormal();
	const float ImpulseSpeed = FMath::Clamp(
		FMath::IsFinite(DeathImpulseSpeed) ? DeathImpulseSpeed : 0.0f,
		0.0f,
		MaxDeathImpulseSpeed);
	if (Direction.IsNearlyZero() || ImpulseSpeed <= 0.0f)
	{
		ReportImpulse(FString::Printf(TEXT("SKIPPED ZeroDirectionOrSpeed ConfiguredSpeed=%.1f EffectiveSpeed=%.1f Direction=%s"),
			DeathImpulseSpeed, ImpulseSpeed, *Direction.ToString()));
		return;
	}

	UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
	FName ImpulseBone = FallbackImpulseBone;
	if (CachedDeathHit.GetComponent() == Mesh && !CachedDeathHit.BoneName.IsNone() &&
		PhysicsAsset && PhysicsAsset->FindBodyIndex(CachedDeathHit.BoneName) != INDEX_NONE)
	{
		if (FBodyInstance* HitBody = Mesh->GetBodyInstance(CachedDeathHit.BoneName))
		{
			if (HitBody->IsValidBodyInstance())
			{
				ImpulseBone = CachedDeathHit.BoneName;
			}
		}
	}

	if (!PhysicsAsset || PhysicsAsset->FindBodyIndex(ImpulseBone) == INDEX_NONE)
	{
		ReportImpulse(FString::Printf(TEXT("SKIPPED NoPhysicsBody HitBone=%s FallbackBone=%s HitWasBodyMesh=%d"),
			*CachedDeathHit.BoneName.ToString(), *FallbackImpulseBone.ToString(), CachedDeathHit.GetComponent() == Mesh ? 1 : 0));
		return;
	}

	FBodyInstance* ImpulseBody = Mesh->GetBodyInstance(ImpulseBone);
	if (!ImpulseBody || !ImpulseBody->IsValidBodyInstance())
	{
		ReportImpulse(FString::Printf(TEXT("SKIPPED InvalidBodyInstance Bone=%s"), *ImpulseBone.ToString()));
		return;
	}

	Mesh->AddImpulse(Direction * ImpulseSpeed, ImpulseBone, true);
	ReportImpulse(FString::Printf(TEXT("SUBMITTED Bone=%s BodySimulating=%d Speed=%.1f Direction=%s HitWasBodyMesh=%d HitBone=%s"),
		*ImpulseBone.ToString(), Mesh->IsSimulatingPhysics(ImpulseBone) ? 1 : 0, ImpulseSpeed,
		*Direction.ToString(), CachedDeathHit.GetComponent() == Mesh ? 1 : 0, *CachedDeathHit.BoneName.ToString()));
}

bool UDustRagdollDeathComponent::HasUsablePhysicsAsset(const USkeletalMeshComponent* Mesh) const
{
	if (!IsValid(Mesh))
	{
		return false;
	}

	const UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
	if (!IsValid(PhysicsAsset))
	{
		return false;
	}

	for (const TObjectPtr<USkeletalBodySetup>& BodySetupObject : PhysicsAsset->SkeletalBodySetups)
	{
		const USkeletalBodySetup* BodySetup = BodySetupObject.Get();
		if (IsValid(BodySetup) && BodySetup->AggGeom.GetElementCount() > 0)
		{
			return true;
		}
	}

	return false;
}

bool UDustRagdollDeathComponent::IsOperationCurrent(uint64 ExpectedGeneration, const AActor* ExpectedOwner) const
{
	const AActor* CurrentOwner = GetOwner();
	return bDeathStarted && OperationGeneration == ExpectedGeneration && IsValid(this) &&
		IsValid(ExpectedOwner) && CurrentOwner == ExpectedOwner && IsValid(CurrentOwner) && !CurrentOwner->IsActorBeingDestroyed();
}

bool UDustRagdollDeathComponent::IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}

FVector UDustRagdollDeathComponent::ClampMagnitudeSafe(const FVector& Value, float MaxMagnitude)
{
	if (!IsFiniteVector(Value) || !FMath::IsFinite(MaxMagnitude) || MaxMagnitude <= 0.0f)
	{
		return FVector::ZeroVector;
	}

	const float MaxMagnitudeSquared = MaxMagnitude * MaxMagnitude;
	const float ValueSquared = Value.SizeSquared();
	if (!FMath::IsFinite(ValueSquared) || ValueSquared <= MaxMagnitudeSquared)
	{
		return ValueSquared >= 0.0f && FMath::IsFinite(ValueSquared) ? Value : FVector::ZeroVector;
	}

	return Value.GetSafeNormal() * MaxMagnitude;
}

uint64 UDustRagdollDeathComponent::AdvanceOperationGeneration()
{
	++OperationGeneration;
	if (OperationGeneration == 0)
	{
		++OperationGeneration;
	}
	return OperationGeneration;
}
