#include "Presentation/WeaponPresentationComponent.h"
#include "Presentation/WeaponPresentationData.h"

bool UWeaponPresentationComponent::InitializeNativeSway(
	AWeaponRuntime* FormalWeapon, AActor* VisualWeapon, int64 BindingId)
{
	// A stale initialization must not clear the newer weapon's state.
	if (!IsCurrentVisualWeapon(FormalWeapon, VisualWeapon, BindingId)) return false;
	FWeaponPresentationProfile Profile;
	FWeaponViewmodelSettings Settings;
	if (!FindWeaponPresentationProfile(FormalWeapon, Profile)
		|| !UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Settings))
	{
		ResetNativeSway();
		return false;
	}
	ResetNativeSway();
	NativeSwaySettings = Settings;
	NativeSwayWeapon = FormalWeapon;
	NativeSwayVisual = VisualWeapon;
	NativeSwayBindingId = BindingId;
	bNativeSwayInitialized = true;
	return true;
}

void UWeaponPresentationComponent::ResetNativeSway()
{
	bNativeSwayInitialized = false;
	NativeSwaySettings = FWeaponViewmodelSettings{};
	NativeSwayRuntime.Reset();
	NativeSwayWeapon.Reset();
	NativeSwayVisual.Reset();
	NativeSwayBindingId = 0;
	NativeMoveSway = NativeAimSway = FTransform::Identity;
}

bool UWeaponPresentationComponent::IsNativeSwayCurrent() const
{
	return bNativeSwayInitialized && IsCurrentVisualWeapon(
		NativeSwayWeapon.Get(), NativeSwayVisual.Get(), NativeSwayBindingId);
}

void UWeaponPresentationComponent::ComputeNativeMoveSwayTarget(
	FVector PositionInput, FVector RotationInput, bool bAiming, float DeltaSeconds)
{
	if (IsNativeSwayCurrent()) NativeSwayRuntime.ComputeMoveTarget(
		NativeSwaySettings.MovementSway, PositionInput, RotationInput, bAiming, DeltaSeconds);
}

void UWeaponPresentationComponent::ComputeNativeMoveSway(float DeltaSeconds)
{
	if (IsNativeSwayCurrent() && NativeSwayRuntime.ComputeMove(NativeSwaySettings.MovementSway, DeltaSeconds))
		NativeMoveSway = NativeSwayRuntime.Move.GetTransform();
}

void UWeaponPresentationComponent::ComputeNativeAimSwayTarget(
	FVector PositionDelta, FVector RotationDelta, bool bAiming, float DeltaSeconds)
{
	if (IsNativeSwayCurrent()) NativeSwayRuntime.ComputeAimTarget(
		NativeSwaySettings.AimingSway, PositionDelta, RotationDelta, bAiming, DeltaSeconds);
}

void UWeaponPresentationComponent::ComputeNativeAimSway(float DeltaSeconds)
{
	if (IsNativeSwayCurrent() && NativeSwayRuntime.ComputeAim(NativeSwaySettings.AimingSway, DeltaSeconds))
		NativeAimSway = NativeSwayRuntime.Aim.GetTransform();
}
