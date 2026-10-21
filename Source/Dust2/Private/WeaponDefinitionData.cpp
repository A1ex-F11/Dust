#include "WeaponDefinitionData.h"
#include "WeaponAccuracyData.h"
#include "WeaponBallisticsData.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#endif

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

bool UWeaponDefinitionData::UsesAimingSpread() const
{
	return AccuracyData ? AccuracyData->bUseAimingSpread : bUseAimingSpread;
}

EFireMode UWeaponDefinitionData::GetInitialFireMode() const
{
	return ConfigurationVersion >= 1 && !AllowedFireModes.IsEmpty() ? AllowedFireModes[0] : DefaultFireMode;
}
bool UWeaponDefinitionData::SupportsBurstFire() const
{
	return AllowedFireModes.Contains(EFireMode::Burst);
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
EDataValidationResult UWeaponDefinitionData::IsDataValid(FDataValidationContext& Context) const
{
    const EDataValidationResult ParentResult = Super::IsDataValid(Context);
    bool bInvalid = ParentResult == EDataValidationResult::Invalid;
    const auto Error = [&Context, &bInvalid](const TCHAR* Message)
    {
        Context.AddError(FText::FromString(Message));
        bInvalid = true;
    };
    if (AllowedFireModes.IsEmpty()) Error(TEXT("至少配置一种开火模式。"));
    if (!FMath::IsFinite(FireInterval) || FireInterval < 0.0f) Error(TEXT("射击间隔必须是非负有限秒数。"));
    if (SupportsBurstFire() && (!FMath::IsFinite(BurstShotInterval) || BurstShotInterval < 0.0f
        || !FMath::IsFinite(BurstRecoveryInterval) || BurstRecoveryInterval < 0.0f))
        Error(TEXT("三连发间隔必须是非负有限秒数。"));
    if (MagazineCapacity <= 0) Error(TEXT("弹匣容量必须大于0。"));
    if (InitialMagazineAmmo < 0 || InitialMagazineAmmo > MagazineCapacity)
        Error(TEXT("初始弹匣弹药必须在0与弹匣容量之间。"));
    if (InitialReserveAmmo < 0) Error(TEXT("初始备弹不能为负数。"));
    if (!BallisticsData) Error(TEXT("缺少弹道配置。"));
    else
    {
        const auto Valid = [](float Value) { return FMath::IsFinite(Value) && Value >= 0.0f; };
        if (!Valid(BallisticsData->TraceDistance) || !Valid(BallisticsData->DamagePerShot)
            || !Valid(BallisticsData->ArmorPenetration) || !Valid(BallisticsData->HeadDamageMultiplier))
            Error(TEXT("弹道中的射程、伤害、穿甲和爆头倍率必须为非负有限值。"));
    }
    return bInvalid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}

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

void UWeaponDefinitionData::SplitAccuracyAndRecoilAssets()
{
#if WITH_EDITOR
	if (!IsValid(BallisticsData))
	{
		UE_LOG(LogTemp, Error, TEXT("拆分为精度与后坐资产失败：武器定义缺少有效弹道配置。"));
		return;
	}

	UWeaponRecoilData* SourceRecoil = RecoilData;
	if (!IsValid(SourceRecoil))
	{
		UE_LOG(LogTemp, Error, TEXT("拆分为精度与后坐资产失败：武器定义缺少有效后坐配置。"));
		return;
	}

	// In-memory compatibility fill only; no package is written here.
	MigrateConfiguration();

	const bool bAccuracyExternal = AccuracyData && AccuracyData->GetOutermost() != GetOutermost();
	const bool bRecoilExternal = SourceRecoil->GetOutermost() != GetOutermost();
	const auto IsPackageOccupied = [](const FString& PackageName)
	{
		if (FPackageName::DoesPackageExist(PackageName))
		{
			return true;
		}
		if (FindPackage(nullptr, *PackageName))
		{
			return true;
		}
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"),
			*PackageName, *FPackageName::GetLongPackageAssetName(PackageName));
		return FindObject<UObject>(nullptr, *ObjectPath) != nullptr;
	};

	FWeaponThreeAssetSplitPlan Plan;
	FString Error;
	if (!BuildWeaponThreeAssetSplitPlan(GetOutermost()->GetName(), GetName(), bAccuracyExternal,
		bRecoilExternal, IsPackageOccupied, Plan, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("拆分为精度与后坐资产失败：%s"), *Error);
		return;
	}
	if (!Plan.bCreateAccuracy && !Plan.bCreateRecoil)
	{
		UE_LOG(LogTemp, Display, TEXT("武器 %s 已引用独立精度与后坐资产，无需重复拆分。"), *GetName());
		return;
	}

	// Snapshot current effective values before touching either reference.
	const FWeaponIntuitiveAccuracy EffectiveHip = AccuracyData ? AccuracyData->HipAccuracy : HipAccuracy;
	const FWeaponIntuitiveAccuracy EffectiveAim = AccuracyData ? AccuracyData->AimAccuracy : AimAccuracy;
	const bool bEffectiveAimingSpread = AccuracyData ? AccuracyData->bUseAimingSpread : bUseAimingSpread;
	const float EffectiveCameraScale = AccuracyData ? AccuracyData->CameraFeedbackScale : SourceRecoil->CameraFeedbackScale;
	UWeaponCameraFeedbackPreset* EffectiveCameraPreset = AccuracyData
		? AccuracyData->CameraFeedbackPreset.Get() : SourceRecoil->CameraFeedbackPreset.Get();
	const bool bEffectiveOverrideCamera = AccuracyData ? AccuracyData->bOverrideCameraFeedback : SourceRecoil->bOverrideCameraFeedback;
	const FWeaponCameraFeedbackParameters EffectiveCameraOverrides =
		AccuracyData ? AccuracyData->CameraFeedbackOverrides : SourceRecoil->CameraFeedbackOverrides;
	const float EffectiveCameraReturn = AccuracyData ? AccuracyData->CameraReturnSpeed : SourceRecoil->CameraReturnSpeed;
	const float EffectiveSmoothKick = AccuracyData ? AccuracyData->SmoothKickDuration : SourceRecoil->SmoothKickDuration;
	const float EffectiveImmediateKick = AccuracyData ? AccuracyData->ImmediateKickFraction : SourceRecoil->ImmediateKickFraction;

	UPackage* AccuracyPackage = nullptr;
	UWeaponAccuracyData* NewAccuracy = nullptr;
	if (Plan.bCreateAccuracy)
	{
		AccuracyPackage = CreatePackage(*Plan.AccuracyPackageName);
		NewAccuracy = NewObject<UWeaponAccuracyData>(AccuracyPackage, *Plan.AccuracyAssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (NewAccuracy)
		{
			NewAccuracy->HipAccuracy = EffectiveHip;
			NewAccuracy->AimAccuracy = EffectiveAim;
			NewAccuracy->bUseAimingSpread = bEffectiveAimingSpread;
			NewAccuracy->CameraFeedbackScale = EffectiveCameraScale;
			NewAccuracy->CameraFeedbackPreset = EffectiveCameraPreset;
			NewAccuracy->bOverrideCameraFeedback = bEffectiveOverrideCamera;
			NewAccuracy->CameraFeedbackOverrides = EffectiveCameraOverrides;
			NewAccuracy->CameraReturnSpeed = EffectiveCameraReturn;
			NewAccuracy->SmoothKickDuration = EffectiveSmoothKick;
			NewAccuracy->ImmediateKickFraction = EffectiveImmediateKick;
			NewAccuracy->NormalizeDirectParameters(MovementSpreadSpeedThreshold);
		}
	}

	UPackage* RecoilPackage = nullptr;
	UWeaponRecoilData* NewRecoil = nullptr;
	if (Plan.bCreateRecoil)
	{
		RecoilPackage = CreatePackage(*Plan.RecoilPackageName);
		NewRecoil = DuplicateObject<UWeaponRecoilData>(SourceRecoil, RecoilPackage,
			*Plan.RecoilAssetName);
		if (NewRecoil)
		{
			NewRecoil->ClearFlags(RF_Transient | RF_DefaultSubObject);
			NewRecoil->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		}
	}

	// Both packages are fully prepared before any reference changes; a failed
	// creation leaves the original Definition references untouched.
	if ((Plan.bCreateAccuracy && !NewAccuracy) || (Plan.bCreateRecoil && !NewRecoil))
	{
		if (NewAccuracy)
		{
			NewAccuracy->MarkAsGarbage();
		}
		if (NewRecoil)
		{
			NewRecoil->MarkAsGarbage();
		}
		UE_LOG(LogTemp, Error, TEXT("拆分为精度与后坐资产失败：独立包创建失败，原引用保持不变。"));
		return;
	}

	Modify();
	if (NewAccuracy)
	{
		FAssetRegistryModule::AssetCreated(NewAccuracy);
		AccuracyData = NewAccuracy;
	}
	if (NewRecoil)
	{
		FAssetRegistryModule::AssetCreated(NewRecoil);
		RecoilData = NewRecoil;
	}
	if (AccuracyPackage)
	{
		AccuracyPackage->MarkPackageDirty();
	}
	if (RecoilPackage)
	{
		RecoilPackage->MarkPackageDirty();
	}
	MarkPackageDirty();
	if (UPackage* DefinitionPackage = GetOutermost())
	{
		DefinitionPackage->MarkPackageDirty();
	}

	UE_LOG(LogTemp, Display, TEXT("武器 %s 拆分为独立精度/后坐资产完成；引用已更新，未自动保存，请手动 Save All。"), *GetName());
#endif
}
