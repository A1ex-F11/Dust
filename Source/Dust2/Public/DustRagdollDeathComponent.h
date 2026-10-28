#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"
#include "DustRagdollDeathComponent.generated.h"

class ACharacter;
class UCapsuleComponent;
class UCharacterMovementComponent;
class USceneComponent;
class USkeletalMeshComponent;

/** Per-character death presentation. Health, damage, drops, and encounter bookkeeping stay external. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="死亡布娃娃表现"))
class DUST2_API UDustRagdollDeathComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustRagdollDeathComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡", meta=(DisplayName="自动销毁尸体"))
	bool bAutoDestroyOwner = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡", meta=(ClampMin="0.0", DisplayName="尸体保留秒数"))
	float CorpseLifetimeSeconds = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡", meta=(ClampMin="0.0", DisplayName="死亡冲量速度"))
	float DeathImpulseSpeed = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡", meta=(ClampMin="0.0", DisplayName="继承速度上限"))
	float MaxInheritedSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="死亡", meta=(DisplayName="冲量回退骨骼"))
	FName FallbackImpulseBone = TEXT("pelvis");

	/** Captured mesh for this owner only. Does not search for a substitute mesh. */
	UFUNCTION(BlueprintPure, Category="死亡")
	USkeletalMeshComponent* GetDeathMesh() const;

	UFUNCTION(BlueprintPure, Category="死亡")
	bool IsDeathStarted() const;

	UFUNCTION(BlueprintPure, Category="死亡")
	bool IsRagdollActive() const;

	/** Read-only diagnostic for initialization and the most recent death attempt. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="死亡|调试")
	FString LastDeathDiagnostic;

	/** Configure the exact body mesh and optional same-owner scene components to hide on death. */
	UFUNCTION(BlueprintCallable, Category="死亡")
	bool InitializeDeathPresentation(USkeletalMeshComponent* InBodyMesh, const TArray<USceneComponent*>& InHideOnDeath);

	/** Cache only a hit on this owner; the direction is sanitized and copied before death starts. */
	UFUNCTION(BlueprintCallable, Category="死亡")
	void CacheDeathImpact(const FHitResult& Hit, FVector ShotDirection);

	/** Stops this character's combat presentation and starts ragdoll when the configured mesh has valid physics bodies. */
	UFUNCTION(BlueprintCallable, Category="死亡")
	bool StartRagdollDeath();

	/** Reusable Pawn/checkpoint recovery for presentation state only. Does not restore AI or health. */
	UFUNCTION(BlueprintCallable, Category="死亡")
	void ResetDeathPresentation();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend struct FDustRagdollDeathTestAccess;

	struct FVisibilitySnapshot
	{
		TWeakObjectPtr<USceneComponent> Component;
		bool bVisible = true;
		bool bHiddenInGame = false;
	};

	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMeshComponent> BodyMesh;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<USceneComponent>> HideOnDeathComponents;

	TArray<FVisibilitySnapshot> SavedHideVisibility;

	TWeakObjectPtr<ACharacter> SavedCharacter;
	TWeakObjectPtr<UCharacterMovementComponent> SavedMovement;
	TWeakObjectPtr<UCapsuleComponent> SavedCapsule;
	TWeakObjectPtr<USceneComponent> SavedBodyAttachParent;
	TWeakObjectPtr<USkeletalMeshComponent> SuspendedHiddenMesh;
	FName SavedBodyAttachSocket = NAME_None;
	FTransform SavedBodyRelativeTransform = FTransform::Identity;
	FName SavedBodyCollisionProfile = NAME_None;
	FCollisionResponseContainer SavedBodyCollisionResponses;
	ECollisionChannel SavedBodyObjectType = ECC_WorldDynamic;
	ECollisionEnabled::Type SavedBodyCollisionEnabled = ECollisionEnabled::NoCollision;
	FVector SavedCharacterVelocity = FVector::ZeroVector;
	uint8 SavedMovementMode = 0;
	uint8 SavedCustomMovementMode = 0;
	bool bSavedBodyOnlyOwnerSee = false;
	bool bSavedBodyOwnerNoSee = false;
	bool bSavedBodyVisible = true;
	bool bSavedBodyHiddenInGame = false;
	bool bSavedBodyPauseAnims = false;
	bool bSavedHiddenMeshTickEnabled = false;
	bool bSavedBodyHadAttachParent = false;
	bool bHasSavedBodyState = false;
	bool bHasSavedMovementState = false;
	bool bHasSavedCapsuleState = false;
	bool bDeathStarted = false;
	bool bRagdollActive = false;
	bool bResetInProgress = false;
	bool bCachedDeathHitValid = false;
	FHitResult CachedDeathHit;
	FVector CachedDeathDirection = FVector::ZeroVector;
	ECollisionEnabled::Type SavedCapsuleCollisionEnabled = ECollisionEnabled::NoCollision;
	FTimerHandle CorpseTimerHandle;
	uint64 OperationGeneration = 0;
	uint64 CorpseTimerGeneration = 0;

	void CaptureOriginalState(ACharacter* OwnerCharacter, USkeletalMeshComponent* Mesh);
	void CaptureHideVisibility(ACharacter* OwnerCharacter);
	void HideConfiguredComponents(ACharacter* OwnerCharacter, uint64 ExpectedGeneration);
	void StopOwnerActivities(ACharacter* OwnerCharacter, uint64 ExpectedGeneration);
	void SuspendHiddenMeshAfterRagdoll(ACharacter* OwnerCharacter, USkeletalMeshComponent* DeathMesh);
	void RestoreHiddenMeshTick(ACharacter* OwnerCharacter);
	bool ConfigureBodyCollisionForDeath(USkeletalMeshComponent* Mesh, ACharacter* OwnerCharacter, uint64 ExpectedGeneration);
	void ScheduleCorpseCleanup(uint64 ExpectedGeneration);
	void ClearCorpseTimer();
	void HandleCorpseTimerExpired();
	void ApplyCachedDeathImpulse(USkeletalMeshComponent* Mesh);
	bool HasUsablePhysicsAsset(const USkeletalMeshComponent* Mesh) const;
	bool IsOperationCurrent(uint64 ExpectedGeneration, const AActor* ExpectedOwner) const;
	static bool IsFiniteVector(const FVector& Value);
	static FVector ClampMagnitudeSafe(const FVector& Value, float MaxMagnitude);
	uint64 AdvanceOperationGeneration();
};
