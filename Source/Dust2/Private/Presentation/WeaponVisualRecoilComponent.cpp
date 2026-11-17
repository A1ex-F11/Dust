#include "Presentation/WeaponVisualRecoilComponent.h"
#include "Weapons/WeaponRuntime.h"
#include "Inventory/WeaponInventoryComponent.h"
#include "Player/Dust2PlayerCharacter.h"
#include "Engine/World.h"

namespace
{
	bool IsEquippedWeapon(AActor* Owner, const AWeaponRuntime* Weapon)
	{
		if (!IsValid(Owner) || !IsValid(Weapon)) return false;
		// The inventory owns equipment state; Actor ownership is not the equip contract.
		if (const UWeaponInventoryComponent* Inventory = Owner->FindComponentByClass<UWeaponInventoryComponent>())
			return Inventory->GetCurrentWeaponNative() == Weapon;
		if (Owner->IsA<ADust2PlayerCharacter>()) return false;
		return Weapon->GetOwner() == Owner;
	}
	float Decay(float Speed, float Dt) { return FMath::Exp(-Speed * Dt); }
	double Range(const FVector2D& V) { return FMath::FRandRange(FMath::Min(V.X, V.Y), FMath::Max(V.X, V.Y)); }
	double Range(const FVector4& V)
	{
		return FMath::RandBool() ? Range(FVector2D(V.X, V.Y)) : Range(FVector2D(V.Z, V.W));
	}
	float CurveEnd(const UCurveVector* Curve)
	{
		float Min = 0, Max = 0;
		if (Curve) Curve->GetTimeRange(Min, Max);
		return FMath::IsFinite(Max) ? FMath::Max(0.0f, Max) : 0.0f;
	}
	FVector Sample(const UCurveVector* Curve, float Time)
	{
		const FVector V = Curve ? Curve->GetVectorValue(Time) : FVector::ZeroVector;
		return V.ContainsNaN() ? FVector::ZeroVector : V;
	}
}

UWeaponVisualRecoilComponent::UWeaponVisualRecoilComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UWeaponVisualRecoilComponent::InitializeVisualRecoil(AWeaponRuntime* Weapon, UWeaponVisualRecoilData* Data)
{
	ResetVisualRecoil();
	if (!IsEquippedWeapon(GetOwner(), Weapon) || !IsValid(Data) || !Data->IsUsable()) return false;
	BoundWeapon = Weapon;
	RecoilData = Data;
	if (const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner())) bAiming = Player->IsAiming();
	return true;
}

bool UWeaponVisualRecoilComponent::CanUpdate() const
{
	const AWeaponRuntime* Weapon = BoundWeapon.Get();
	if (!IsEquippedWeapon(GetOwner(), Weapon) || !IsValid(RecoilData)) return false;
	const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
	return !Player || !Player->IsDead();
}

bool UWeaponVisualRecoilComponent::PlayVisualRecoil()
{
	if (!CanUpdate() || !GetWorld()) { ResetVisualRecoil(); return false; }
	const float Interval = BoundWeapon->GetEffectiveFireInterval();
	if (!FMath::IsFinite(Interval) || Interval <= 0) return false;
	ShotInterval = FMath::Max(0.001f, Interval);
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bAuto = BoundWeapon->GetFireMode() != EFireMode::SemiAutomatic && bHasShot
		&& Now >= LastShotTime && Now - LastShotTime <= ShotInterval * 1.5;
	if (const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner())) bAiming = Player->IsAiming();
	const FVisualRecoilPoseSettings& Pose = bAiming ? RecoilData->Aim : RecoilData->Hip;
	const double OldRoll = TargetRotation.X;
	TargetRotation = FVector(Range(Pose.Roll), Range(Pose.Pitch), Range(Pose.Yaw));
	if (RecoilData->bAlternateRoll && OldRoll * TargetRotation.X > 0) TargetRotation.X *= -1;
	TargetLocation = FVector(Range(Pose.KickBack), Range(Pose.KickRight), Range(Pose.KickUp));
	if (bAuto)
	{
		TargetRotation *= RecoilData->AutoRotationScale;
		TargetLocation *= RecoilData->AutoLocationScale;
	}
	RotationCurve = bAuto && RecoilData->AutoRotation ? RecoilData->AutoRotation : RecoilData->SingleRotation;
	LocationCurve = bAuto && RecoilData->AutoLocation ? RecoilData->AutoLocation : RecoilData->SingleLocation;
	EndTime = FMath::Max(CurveEnd(RotationCurve), CurveEnd(LocationCurve));
	if (EndTime <= UE_SMALL_NUMBER) return false;
	Playback = bAuto ? FMath::Min(ShotInterval + RecoilData->PlaybackOffset, EndTime * 0.9f) : 0.0f;
	// Preserve the current curve output when a new shot changes curves or amplitudes.
	StartRotation = RawRotation - TargetRotation * Sample(RotationCurve, Playback);
	StartLocation = RawLocation - TargetLocation * Sample(LocationCurve, Playback);
	// Remaining duration also works for curves with an automatic-fire start offset.
	EndTime -= Playback;
	NoiseTarget = FVector2D(FMath::FRandRange(-1.0f, 1.0f) * RecoilData->NoiseAmount.X,
		FMath::FRandRange(-1.0f, 1.0f) * RecoilData->NoiseAmount.Y);
	Sway += FVector2D(Range(RecoilData->PitchSwayRange), Range(RecoilData->YawSwayRange));
	Progress += FVector2D(RecoilData->ProgressPitchPerShot, RecoilData->ProgressUpPerShot);
	Sway.X = FMath::Clamp(Sway.X, -90.0, 90.0); Sway.Y = FMath::Clamp(Sway.Y, -90.0, 90.0);
	Progress.X = FMath::Clamp(Progress.X, -100.0, 100.0); Progress.Y = FMath::Clamp(Progress.Y, -100.0, 100.0);
	PushbackTarget = RecoilData->PushbackAmount;
	LastShotTime = Now;
	bHasShot = bPlaying = true;
	SetComponentTickEnabled(true);
	return true;
}

void UWeaponVisualRecoilComponent::StopVisualRecoil()
{
	// Let the current curve and damping finish; no abrupt zero on trigger release.
	bHasShot = false;
}

void UWeaponVisualRecoilComponent::SetVisualRecoilAiming(bool bNewAiming) { bAiming = bNewAiming; }

void UWeaponVisualRecoilComponent::UpdateVisual(float Dt)
{
	if (bPlaying)
	{
		const float Step = FMath::Min(Dt * RecoilData->PlayRate, EndTime);
		const float Remaining = FMath::Max(0.0f, EndTime - Step);
		const float Weight = EndTime > UE_SMALL_NUMBER ? Remaining / EndTime : 0.0f;
		StartRotation *= Weight;
		StartLocation *= Weight;
		Playback += Step;
		EndTime = Remaining;
		RawRotation = TargetRotation * Sample(RotationCurve, Playback) + StartRotation;
		RawLocation = TargetLocation * Sample(LocationCurve, Playback) + StartLocation;
		bPlaying = Remaining > UE_SMALL_NUMBER;
	}
	else
	{
		RawRotation *= Decay(RecoilData->SmoothingSpeed, Dt);
		RawLocation *= Decay(RecoilData->SmoothingSpeed, Dt);
	}
	const float Alpha = 1.0f - Decay(RecoilData->SmoothingSpeed, Dt);
	SmoothedRotation = FMath::Lerp(SmoothedRotation, RawRotation, Alpha);
	SmoothedLocation = FMath::Lerp(SmoothedLocation, RawLocation, Alpha);
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		NoiseTarget[Axis] *= Decay(static_cast<float>(RecoilData->NoiseDamping[Axis]), Dt);
		const double AimScale = bAiming ? RecoilData->NoiseAimScale : 1.0;
		NoiseCurrent[Axis] = FMath::Lerp(NoiseCurrent[Axis], NoiseTarget[Axis] * AimScale,
			1.0 - Decay(static_cast<float>(RecoilData->NoiseInterpSpeed[Axis]), Dt));
	}
	PushbackTarget *= Decay(RecoilData->PushbackDamping, Dt);
	PushbackCurrent = FMath::Lerp(PushbackCurrent, PushbackTarget, 1.0f - Decay(RecoilData->PushbackInterpSpeed, Dt));
	Sway *= Decay(RecoilData->SwayReturnSpeed, Dt);
	Progress *= Decay(RecoilData->ProgressReturnSpeed, Dt);
	const FVector Angles = SmoothedRotation + FVector(0, Sway.X + Progress.X, Sway.Y);
	const FRotator Rotation(Angles.Y, Angles.Z, Angles.X);
	const FVector Pivot = bAiming ? RecoilData->Aim.PivotOffset : RecoilData->Hip.PivotOffset;
	const FVector Location = SmoothedLocation + FVector(PushbackCurrent, NoiseCurrent.X, NoiseCurrent.Y + Progress.Y)
		+ Pivot - Rotation.RotateVector(Pivot);
	const FTransform Space(RecoilData->SpaceRotation);
	RecoilAnimation = Space.Inverse() * FTransform(Rotation, Location) * Space;
	if (RecoilAnimation.ContainsNaN()) { ResetVisualRecoil(); return; }
	if (!bPlaying && RawLocation.IsNearlyZero(0.0001) && RawRotation.IsNearlyZero(0.0001)
		&& SmoothedLocation.IsNearlyZero(0.0001) && SmoothedRotation.IsNearlyZero(0.0001)
		&& NoiseCurrent.IsNearlyZero(0.0001) && Sway.IsNearlyZero(0.0001) && Progress.IsNearlyZero(0.0001)
		&& FMath::Abs(PushbackCurrent) < 0.0001f)
	{
		RecoilAnimation = FTransform::Identity;
		SetComponentTickEnabled(false);
	}
}

void UWeaponVisualRecoilComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt, TickType, TickFunction);
	if (!CanUpdate()) { ResetVisualRecoil(); return; }
	if (!FMath::IsFinite(Dt) || Dt <= 0) return;
	UpdateVisual(Dt);
}

void UWeaponVisualRecoilComponent::ResetVisualRecoil()
{
	BoundWeapon.Reset(); RecoilData = nullptr; RotationCurve = LocationCurve = nullptr;
	bAiming = bHasShot = bPlaying = false;
	LastShotTime = 0; Playback = EndTime = 0;
	TargetRotation = TargetLocation = StartRotation = StartLocation = FVector::ZeroVector;
	RawRotation = RawLocation = SmoothedRotation = SmoothedLocation = FVector::ZeroVector;
	NoiseTarget = NoiseCurrent = Sway = Progress = FVector2D::ZeroVector;
	PushbackTarget = PushbackCurrent = 0;
	RecoilAnimation = FTransform::Identity;
	SetComponentTickEnabled(false);
}

void UWeaponVisualRecoilComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ResetVisualRecoil();
	Super::EndPlay(Reason);
}
