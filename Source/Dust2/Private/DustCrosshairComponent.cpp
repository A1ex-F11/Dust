#include "DustCrosshairComponent.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DynamicCrosshairWidget.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "WeaponInventoryComponent.h"
#include "WeaponPresentationComponent.h"
#include "WeaponRuntime.h"

namespace
{
float SafeCrosshairSeconds(float Value)
{
    return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
}
}

UDustCrosshairComponent::UDustCrosshairComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

bool UDustCrosshairComponent::InitializeCrosshair(UDynamicCrosshairWidget* Widget,
    UWeaponInventoryComponent* Inventory, UWeaponPresentationComponent* Presentation,
    UCameraComponent* ShootingCamera)
{
    StopCrosshair();
    ADust2PlayerCharacter* OwnerPlayer = Cast<ADust2PlayerCharacter>(GetOwner());
    if (!IsValid(OwnerPlayer) || !IsValid(Widget) || !IsValid(Inventory)
        || !IsValid(Presentation) || !IsValid(ShootingCamera)
        || Inventory->GetOwner() != OwnerPlayer || Presentation->GetOwner() != OwnerPlayer
        || ShootingCamera->GetOwner() != OwnerPlayer
        || Widget->GetOwningPlayerPawn() != OwnerPlayer)
        return false;

    Player = OwnerPlayer;
    CrosshairWidget = Widget;
    InventorySource = Inventory;
    PresentationSource = Presentation;
    CameraSource = ShootingCamera;
    DisplayedWeapon = Inventory->GetCurrentWeaponNative();
    DisplayState.Initialize(Widget->GetCrosshairGap());
    Inventory->OnEquippedWeaponChanged.AddUniqueDynamic(this,
        &UDustCrosshairComponent::HandleEquippedWeaponChanged);
    Presentation->OnDrawPresentationFinished.AddUniqueDynamic(this,
        &UDustCrosshairComponent::HandleDrawFinished);
    SetComponentTickEnabled(true);
    return true;
}

void UDustCrosshairComponent::StopCrosshair()
{
    SetComponentTickEnabled(false);
    if (UWeaponInventoryComponent* Inventory = InventorySource.Get())
        Inventory->OnEquippedWeaponChanged.RemoveDynamic(this,
            &UDustCrosshairComponent::HandleEquippedWeaponChanged);
    if (UWeaponPresentationComponent* Presentation = PresentationSource.Get())
        Presentation->OnDrawPresentationFinished.RemoveDynamic(this,
            &UDustCrosshairComponent::HandleDrawFinished);
    Player.Reset();
    CrosshairWidget.Reset();
    InventorySource.Reset();
    PresentationSource.Reset();
    CameraSource.Reset();
    DisplayedWeapon.Reset();
    DisplayState.Initialize(FVector2D::ZeroVector);
    bHasFirstProjection = false;
    bFinishAfterProjection = false;
}

void UDustCrosshairComponent::HandleEquippedWeaponChanged(AWeaponRuntime* NewWeapon)
{
    if (DisplayedWeapon.Get() == NewWeapon) return;
    DisplayedWeapon = NewWeapon;
    bFinishAfterProjection = false;
    if (bHasFirstProjection)
    {
        if (CrosshairWidget.IsValid()) DisplayState.Initialize(CrosshairWidget->GetCrosshairGap());
        DisplayState.BeginTransition(SafeCrosshairSeconds(SwitchTransitionSeconds));
    }
}

void UDustCrosshairComponent::HandleDrawFinished(bool bCompletedNormally)
{
    // This is an existing animation completion fact, not a new readiness gate.
    if (bCompletedNormally && InventorySource.IsValid()
        && DisplayedWeapon.Get() == InventorySource->GetCurrentWeaponNative())
        bFinishAfterProjection = true;
}

bool UDustCrosshairComponent::BuildTargetGap(FVector2D& OutGap) const
{
    UDynamicCrosshairWidget* Widget = CrosshairWidget.Get();
    ADust2PlayerCharacter* OwnerPlayer = Player.Get();
    UCameraComponent* Camera = CameraSource.Get();
    if (!Widget || !OwnerPlayer || !Camera) return false;
    OutGap = Widget->MinimumGap;
    const AWeaponRuntime* Weapon = DisplayedWeapon.Get();
    if (!Weapon) return true; // Keep the established empty-hand visibility policy.
    const UCharacterMovementComponent* Movement = OwnerPlayer->GetCharacterMovement();
    if (!Movement) return false;
    const float TotalAngle = Weapon->GetCurrentTotalSpreadAngle(
        OwnerPlayer->GetVelocity().Size2D(), Movement->IsFalling(), false, OwnerPlayer->IsSlowWalking());
    const float Angle = FCrosshairDisplayAngle::WithFiringFeedback(TotalAngle,
        Weapon->GetCurrentFiringSpread(), FiringSpreadDisplayScale);
    APlayerController* Controller = Widget->GetOwningPlayer();
    if (!Controller || !Controller->IsLocalController()) return false;
    const FGeometry& Geometry = Widget->GetCachedGeometry();
    const FVector2D Size = Geometry.GetLocalSize();
    if (Size.X <= 0.0 || Size.Y <= 0.0) return false;
    const FGeometry Viewport = UWidgetLayoutLibrary::GetViewportWidgetGeometry(Controller);
    if (Viewport.GetLocalSize().X <= 0.0 || Viewport.GetLocalSize().Y <= 0.0) return false;

    constexpr double Distance = 1000.0;
    const FVector Center = Camera->GetComponentLocation() + Camera->GetForwardVector() * Distance;
    const double Radius = Distance * FMath::Tan(FMath::DegreesToRadians(static_cast<double>(Angle)));
    FVector2D CenterUI, RightUI, UpUI;
    if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, Center, CenterUI, false)
        || !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller,
            Center + Camera->GetRightVector() * Radius, RightUI, false)
        || !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller,
            Center + Camera->GetUpVector() * Radius, UpUI, false))
        return false;
    // Projection already removes DPI scaling. Convert only additional parent geometry.
    const FVector2D CenterLocal = Geometry.AbsoluteToLocal(Viewport.LocalToAbsolute(CenterUI));
    const FVector2D RightLocal = Geometry.AbsoluteToLocal(Viewport.LocalToAbsolute(RightUI));
    const FVector2D UpLocal = Geometry.AbsoluteToLocal(Viewport.LocalToAbsolute(UpUI));
    const FVector2D ProjectedGap((RightLocal - CenterLocal).Size(), (UpLocal - CenterLocal).Size());
    if (!FMath::IsFinite(ProjectedGap.X) || !FMath::IsFinite(ProjectedGap.Y)) return false;
    OutGap = FVector2D(FMath::Max(Widget->MinimumGap.X, ProjectedGap.X),
        FMath::Max(Widget->MinimumGap.Y, ProjectedGap.Y));
    return true;
}

void UDustCrosshairComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Player.IsValid() || !CrosshairWidget.IsValid() || !InventorySource.IsValid()
        || !PresentationSource.IsValid() || !CameraSource.IsValid())
    {
        StopCrosshair();
        return;
    }
    if (Player->IsDead()) return;
    AWeaponRuntime* Current = InventorySource->GetCurrentWeaponNative();
    if (Current != DisplayedWeapon.Get()) HandleEquippedWeaponChanged(Current);
    FVector2D Target;
    if (!BuildTargetGap(Target)) return;
    if (!bHasFirstProjection)
    {
        DisplayState.Initialize(Target);
        bHasFirstProjection = true;
    }
    float DrawRemaining = 0.0f;
    if (PresentationSource->GetDrawRemainingForWeapon(Current, DrawRemaining))
        DisplayState.ConstrainRemainingTime(FMath::Max(0.0f,
            DrawRemaining - SafeCrosshairSeconds(FinishBeforeDrawSeconds)));
    if (bFinishAfterProjection)
    {
        DisplayState.FinishTransition(Target);
        bFinishAfterProjection = false;
    }
    CrosshairWidget->SetCrosshairGap(DisplayState.Advance(Target, DeltaTime, FollowRate));
    // Keep transition state aligned with renderer's safe minimum (the actual shown position).
    // MinimumGap is normally static; no visibility is written by this adapter.
}

void UDustCrosshairComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopCrosshair();
    Super::EndPlay(EndPlayReason);
}
