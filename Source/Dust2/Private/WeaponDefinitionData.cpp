#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"
#include "UObject/UnrealType.h"

UWeaponDefinitionData::UWeaponDefinitionData()
{
	RecoilData = CreateDefaultSubobject<UWeaponRecoilData>(TEXT("Recoil"));
	RecoilData->SetFlags(RF_Public);
	BallisticsData = CreateDefaultSubobject<UWeaponBallisticsData>(TEXT("Ballistics"));
	BallisticsData->SetFlags(RF_Public);
}

FWeaponSpreadProfile FWeaponIntuitiveAccuracy::BuildProfile(float ShotInterval, float MovementThreshold) const
{
	const auto Safe = [](float V) { return FMath::IsFinite(V) ? FMath::Max(0.0f, V) : 0.0f; };
	FWeaponSpreadProfile P;
	P.bUseAdditiveStateSpread = true;
	P.StationarySpreadAngle = Safe(BaseSpreadAngle);
	const FWeaponPostureSettings Pose = GetPosture(MovementThreshold);
	P.MovingSpreadAngle = Safe(Pose.MovingExtraAngle);
	P.SlowWalkSpreadAngle = Safe(Pose.SlowWalkExtraAngle);
	P.JumpSpreadAngle = Safe(Pose.AirborneExtraAngle);
	P.MovementSpreadSpeedThreshold = Safe(Pose.MovementThreshold);
	P.MaxFiringSpread = FMath::Min(89.0f, Safe(MaxFiringExtraAngle));
	P.bUseFiringSpread = P.MaxFiringSpread > 0.0f;
	P.SpreadRecoveryDelay = 0.0f;
	const float Seconds = FMath::Max(0.01f, Safe(FullRecoverySeconds));
	P.SpreadRecoverySpeed = P.MaxFiringSpread / Seconds;
	const int32 Count = FMath::Clamp(ShotsToMaximum, 1, 1000);
	// First shot adds p; each subsequent one adds p-r*interval. Account for recovery.
	const double PerShot = static_cast<double>(P.MaxFiringSpread) / Count
		+ static_cast<double>(P.SpreadRecoverySpeed) * Safe(ShotInterval) * (Count - 1) / Count;
	P.SpreadPerShot = static_cast<float>(FMath::Min(PerShot, static_cast<double>(P.MaxFiringSpread)));
	if (bPreserveMigratedFiring)
	{
		P.bUseFiringSpread = MigratedFiring.bUseFiringSpread;
		P.SpreadPerShot = MigratedFiring.SpreadPerShot;
		P.MaxFiringSpread = MigratedFiring.MaxFiringSpread;
		P.SpreadRecoveryDelay = MigratedFiring.SpreadRecoveryDelay;
		P.SpreadRecoverySpeed = MigratedFiring.SpreadRecoverySpeed;
	}
	return P;
}

FWeaponPostureSettings FWeaponIntuitiveAccuracy::GetPosture(float LegacyThreshold) const
{
	if (bUsePostureSettings)
		return PosturePreset && !bOverridePosture ? PosturePreset->Settings : PostureOverride;
	FWeaponPostureSettings S;
	S.MovingExtraAngle = MovingExtraAngle;
	S.SlowWalkExtraAngle = SlowWalkExtraAngle;
	S.AirborneExtraAngle = AirborneExtraAngle;
	S.MovementThreshold = LegacyThreshold;
	return S;
}

void FWeaponIntuitiveAccuracy::MigratePosture(float LegacyThreshold)
{
	if (bUsePostureSettings) return;
	PostureOverride = GetPosture(LegacyThreshold);
	bUsePostureSettings = true;
}

FWeaponIntuitiveAccuracy FWeaponIntuitiveAccuracy::FromLegacy(const FWeaponSpreadProfile& P, float Interval)
{
	FWeaponIntuitiveAccuracy A;
	A.BaseSpreadAngle = FMath::Max(0.0f, P.StationarySpreadAngle);
	const float Base = P.bUseAdditiveStateSpread ? 0.0f : A.BaseSpreadAngle;
	A.MovingExtraAngle = FMath::Max(0.0f, P.MovingSpreadAngle - Base);
	A.SlowWalkExtraAngle = FMath::Max(0.0f, P.SlowWalkSpreadAngle - Base);
	A.AirborneExtraAngle = FMath::Max(0.0f, P.JumpSpreadAngle - Base);
	A.MaxFiringExtraAngle = P.bUseFiringSpread ? FMath::Max(0.0f,P.MaxFiringSpread) : 0.0f;
	A.FullRecoverySeconds = P.SpreadRecoverySpeed > 0.0f ? A.MaxFiringExtraAngle / P.SpreadRecoverySpeed : 1.0f;
	const float Net = P.SpreadPerShot - P.SpreadRecoverySpeed * FMath::Max(0.0f, Interval-P.SpreadRecoveryDelay);
	A.ShotsToMaximum = Net > UE_SMALL_NUMBER ? FMath::Clamp(FMath::CeilToInt(A.MaxFiringExtraAngle/Net),1,1000) : 1000;
	A.MigratedFiring = P;
	A.bPreserveMigratedFiring = true;
	A.MigratePosture(P.MovementSpreadSpeedThreshold);
	return A;
}

void UWeaponDefinitionData::PostLoad()
{
	Super::PostLoad();
	MigrateConfiguration();
}

void UWeaponDefinitionData::MigrateConfiguration()
{
	if (ConfigurationVersion >= 1) return;
	const int32 Start = AllowedFireModes.IndexOfByKey(DefaultFireMode);
	if (Start > 0)
	{
		const TArray<EFireMode> Old = AllowedFireModes;
		for(int32 I=0; I<Old.Num(); ++I) AllowedFireModes[I] = Old[(Start+I)%Old.Num()];
	}
	else if (Start == INDEX_NONE) AllowedFireModes.Insert(DefaultFireMode,0);
	ReloadStyle = !bReloadOneRoundAtATime ? EWeaponReloadStyle::Magazine
		: bUseBatchReloadForFullEmptyReload ? EWeaponReloadStyle::PerRoundOrEmptyBatch : EWeaponReloadStyle::PerRound;
	const float Interval = DefaultFireMode == EFireMode::Burst && BurstShotInterval > 0 ? BurstShotInterval : FireInterval;
	if (!bUseIntuitiveHipAccuracy)
	{
		FWeaponSpreadProfile P;
		P.StationarySpreadAngle=StationarySpreadAngle; P.MovingSpreadAngle=MovingSpreadAngle;
		P.SlowWalkSpreadAngle=SlowWalkSpreadAngle; P.JumpSpreadAngle=JumpSpreadAngle;
		P.MovementSpreadSpeedThreshold=MovementSpreadSpeedThreshold;
		P.bUseFiringSpread=bUseFiringSpread; P.SpreadPerShot=SpreadPerShot;
		P.MaxFiringSpread=MaxFiringSpread; P.SpreadRecoveryDelay=SpreadRecoveryDelay; P.SpreadRecoverySpeed=SpreadRecoverySpeed;
		HipAccuracy=FWeaponIntuitiveAccuracy::FromLegacy(P,Interval);
	}
	else HipAccuracy.MigratePosture(MovementSpreadSpeedThreshold);
	if (!bUseIntuitiveAimAccuracy) AimAccuracy=FWeaponIntuitiveAccuracy::FromLegacy(AimingSpread,Interval);
	else AimAccuracy.MigratePosture(MovementSpreadSpeedThreshold);
	bUseIntuitiveHipAccuracy=true; bUseIntuitiveAimAccuracy=true;
	ConfigurationVersion=1;
}

EFireMode UWeaponDefinitionData::GetInitialFireMode() const
{
	return ConfigurationVersion >= 1 && !AllowedFireModes.IsEmpty() ? AllowedFireModes[0] : DefaultFireMode;
}
bool UWeaponDefinitionData::UsesPerRoundReload() const
{
	return ConfigurationVersion >= 1 ? ReloadStyle != EWeaponReloadStyle::Magazine : bReloadOneRoundAtATime;
}
bool UWeaponDefinitionData::UsesEmptyBatchReload() const
{
	return ConfigurationVersion >= 1 ? ReloadStyle == EWeaponReloadStyle::PerRoundOrEmptyBatch : bUseBatchReloadForFullEmptyReload;
}

#if WITH_EDITOR
void UWeaponDefinitionData::PreEditChange(FProperty* PropertyAboutToChange)
{
	MigrateConfiguration();
	Super::PreEditChange(PropertyAboutToChange);
}

void UWeaponDefinitionData::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	const FName Field = Event.Property ? Event.Property->GetFName() : NAME_None;
	const FName Member = Event.MemberProperty ? Event.MemberProperty->GetFName() : NAME_None;
	// Only editing the four author-facing firing knobs retires exact legacy timing.
	if (Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, BaseSpreadAngle)
		|| Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, MaxFiringExtraAngle)
		|| Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, ShotsToMaximum)
		|| Field == GET_MEMBER_NAME_CHECKED(FWeaponIntuitiveAccuracy, FullRecoverySeconds))
	{
		if (Member == GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData,HipAccuracy)) HipAccuracy.bPreserveMigratedFiring=false;
		if (Member == GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData,AimAccuracy)) AimAccuracy.bPreserveMigratedFiring=false;
	}
	if (ConfigurationVersion < 1) MigrateConfiguration();
	Super::PostEditChangeProperty(Event);
}
#endif
