#include "Presentation/DustFirstPersonFovComponent.h"

#include "Camera/CameraComponent.h"

UDustFirstPersonFovComponent::UDustFirstPersonFovComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDustFirstPersonFovComponent::InitializeFirstPersonFov(UCameraComponent* InCamera)
{
    if (!ensureMsgf(IsValid(InCamera) && InCamera->GetOwner() == GetOwner(),
        TEXT("First-person FOV requires a camera owned by the same actor.")))
    {
        return;
    }

    Camera = InCamera;
    TargetFov = InCamera->FieldOfView;
    SetComponentTickEnabled(false);
}

void UDustFirstPersonFovComponent::UpdateTargetFov(float NewFov, float InterpSpeed)
{
    if (!Camera.IsValid() || !FMath::IsFinite(NewFov) || !FMath::IsFinite(InterpSpeed))
    {
        return;
    }

    TargetFov = NewFov;
    FovInterpSpeed = InterpSpeed;
    SetComponentTickEnabled(true);
}

void UDustFirstPersonFovComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UCameraComponent* ActiveCamera = Camera.Get();
    if (!ActiveCamera)
    {
        SetComponentTickEnabled(false);
        return;
    }

    const float NextFov = FMath::FInterpTo(
        ActiveCamera->FieldOfView, TargetFov, DeltaTime, FovInterpSpeed);
    const bool bReachedTarget = FMath::IsNearlyEqual(NextFov, TargetFov);
    ActiveCamera->SetFieldOfView(bReachedTarget ? TargetFov : NextFov);
    if (bReachedTarget)
    {
        SetComponentTickEnabled(false);
    }
}
