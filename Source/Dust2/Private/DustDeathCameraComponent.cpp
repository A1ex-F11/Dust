#include "DustDeathCameraComponent.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	float FiniteOr(float Value, float Fallback) { return FMath::IsFinite(Value) ? Value : Fallback; }
}

UDustDeathCameraComponent::UDustDeathCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

FVector UDustDeathCameraComponent::GetFocusLocation() const
{
	FVector Focus = IsValid(GetOwner()) ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	if (const USkeletalMeshComponent* Mesh = FollowMesh.Get())
		Focus = !FocusBone.IsNone() && Mesh->DoesSocketExist(FocusBone)
			? Mesh->GetSocketLocation(FocusBone) : Mesh->GetComponentLocation();
	if (Focus.ContainsNaN()) Focus = IsValid(GetOwner()) ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	return Focus + FVector(0, 0, FMath::Clamp(FiniteOr(FocusHeight, 60.0f), -100.0f, 200.0f));
}

bool UDustDeathCameraComponent::BeginDeathCamera(USkeletalMeshComponent* InBodyMesh)
{
	if (bCameraActive && DeathCamera.IsValid()) return true;
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Pawn) || !IsValid(Controller) || !Controller->IsLocalController() || !GetWorld())
	{
		LastCameraDiagnostic = TEXT("Death view needs the owning local PlayerController");
		return false;
	}
	ReleaseCamera(false);
	const uint64 Serial = CameraGeneration;
	FollowMesh = IsValid(InBodyMesh) && InBodyMesh->GetOwner() == Pawn ? InBodyMesh : nullptr;
	const FRotator ViewRotation(
		FMath::Clamp(FiniteOr(CameraPitch, -15.0f), -70.0f, -5.0f),
		Controller->GetControlRotation().Yaw + FiniteOr(YawOffset, 25.0f), 0.0f);
	FActorSpawnParameters Spawn;
	Spawn.Owner = Pawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(GetFocusLocation(), ViewRotation, Spawn);
	if (!IsValid(Camera) || Serial != CameraGeneration)
	{
		if (IsValid(Camera)) Camera->Destroy();
		LastCameraDiagnostic = TEXT("Could not create death view");
		return false;
	}
	USpringArmComponent* Arm = NewObject<USpringArmComponent>(Camera, TEXT("DeathViewArm"));
	Camera->AddInstanceComponent(Arm);
	Arm->SetupAttachment(Camera->GetRootComponent());
	Arm->TargetArmLength = FMath::Clamp(FiniteOr(ArmLength, 260.0f), 80.0f, 600.0f);
	Arm->bUsePawnControlRotation = false;
	Arm->bDoCollisionTest = true;
	Arm->ProbeChannel = ECC_Camera;
	Arm->ProbeSize = FMath::Clamp(FiniteOr(ProbeRadius, 12.0f), 1.0f, 50.0f);
	Arm->PrimaryComponentTick.TickGroup = TG_PostPhysics;
	Arm->RegisterComponent();
	Arm->AddTickPrerequisiteComponent(this);
	Camera->GetCameraComponent()->AttachToComponent(Arm, FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		USpringArmComponent::SocketName);
	Camera->GetCameraComponent()->SetFieldOfView(FMath::Clamp(FiniteOr(FieldOfView, 80.0f), 30.0f, 120.0f));
	DeathCamera = Camera;
	DeathController = Controller;
	PreviousViewTarget = Controller->GetViewTarget();
	bCameraActive = true;
	bOwnsMoveIgnore = !Controller->IsMoveInputIgnored();
	bOwnsLookIgnore = !Controller->IsLookInputIgnored();
	if (bOwnsMoveIgnore) Controller->SetIgnoreMoveInput(true);
	if (bOwnsLookIgnore) Controller->SetIgnoreLookInput(true);
	// Lock the outgoing first-person POV so the live blend cannot inherit a tumbling head pose.
	Controller->SetViewTargetWithBlend(Camera, FMath::Clamp(FiniteOr(BlendSeconds, 0.4f), 0.0f, 3.0f),
		VTBlend_Cubic, 0.0f, true);
	if (Serial != CameraGeneration) return false;
	SetComponentTickEnabled(true);
	LastCameraDiagnostic = TEXT("Death view active; UI and respawn stay with the existing game flow");
	return true;
}

void UDustDeathCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bCameraActive) return;
	APlayerController* Controller = DeathController.Get();
	ACameraActor* Camera = DeathCamera.Get();
	if (!IsValid(Controller) || !IsValid(Camera) || Controller->GetPawn() != GetOwner())
	{
		ReleaseCamera(false);
		return;
	}
	const float SafeDelta = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0f, DeltaTime) : 0.0f;
	Camera->SetActorLocation(FMath::VInterpTo(Camera->GetActorLocation(), GetFocusLocation(), SafeDelta,
		FMath::Max(0.0f, FiniteOr(FollowSpeed, 6.0f))));
}

void UDustDeathCameraComponent::ReleaseCamera(bool bRestoreView)
{
	++CameraGeneration;
	ACameraActor* Camera = DeathCamera.Get();
	APlayerController* Controller = DeathController.Get();
	AActor* Previous = PreviousViewTarget.Get();
	const bool bReleaseMove = bOwnsMoveIgnore;
	const bool bReleaseLook = bOwnsLookIgnore;
	bCameraActive = false;
	bOwnsMoveIgnore = false;
	bOwnsLookIgnore = false;
	DeathCamera.Reset();
	DeathController.Reset();
	PreviousViewTarget.Reset();
	FollowMesh.Reset();
	SetComponentTickEnabled(false);
	if (IsValid(Controller))
	{
		if (bReleaseMove) Controller->SetIgnoreMoveInput(false);
		if (bReleaseLook) Controller->SetIgnoreLookInput(false);
		const APlayerCameraManager* Manager = Controller->PlayerCameraManager;
		const AActor* Pending = Manager ? Manager->PendingViewTarget.Target.Get() : nullptr;
		const bool bOurView = IsValid(Camera) && (Pending == Camera
			|| (Controller->GetViewTarget() == Camera && !IsValid(Pending)));
		if (bRestoreView && Controller->GetPawn() == GetOwner() && bOurView)
		{
			AActor* ReturnTo = IsValid(Previous) && !Previous->IsActorBeingDestroyed() ? Previous : GetOwner();
			if (IsValid(ReturnTo) && !ReturnTo->IsActorBeingDestroyed()) Controller->SetViewTarget(ReturnTo);
		}
	}
	if (IsValid(Camera)) Camera->Destroy();
}

void UDustDeathCameraComponent::ResetDeathCamera()
{
	ReleaseCamera(true);
	LastCameraDiagnostic = TEXT("Death view reset");
}

void UDustDeathCameraComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseCamera(false);
	Super::EndPlay(Reason);
}
