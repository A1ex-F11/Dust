#include "WeaponDefinitionData.h"

#if WITH_EDITOR
#include "WeaponCameraFeedbackPreset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace WeaponConfigurationUpgrade
{
static const TCHAR* Definitions[] = {
	TEXT("/Game/Code/Texts/AK105/DA_AK105Definition.DA_AK105Definition"),
	TEXT("/Game/Code/Texts/TR15/DA_TR15Definition.DA_TR15Definition"),
	TEXT("/Game/Code/Texts/R08/DA_R08Definition.DA_R08Definition"),
	TEXT("/Game/Code/Texts/Viper/DA_ViperDefinition.DA_ViperDefinition"),
	TEXT("/Game/Code/Texts/SRM-12/DA_SRM-12Definition.DA_SRM-12Definition")
};

static bool Save(UObject* Asset)
{
	FSavePackageArgs Args;
	Args.TopLevelFlags=RF_Public|RF_Standalone;
	Args.SaveFlags=SAVE_None;
	const FString Filename=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	return UPackage::SavePackage(Asset->GetOutermost(),Asset,*Filename,Args);
}

static UWeaponAccuracyPreset* GetPosturePreset(const FWeaponPostureSettings& S)
{
	const FString Key=FString::Printf(TEXT("%.9g,%.9g,%.9g,%.9g"),S.MovingExtraAngle,S.SlowWalkExtraAngle,S.AirborneExtraAngle,S.MovementThreshold);
	const FString PackageName=FString::Printf(TEXT("/Game/Code/ConfigurationPresets/DA_Posture_%08X"),FCrc::StrCrc32(*Key));
	const FString Name=FPackageName::GetLongPackageAssetName(PackageName);
	if(UWeaponAccuracyPreset* Existing=LoadObject<UWeaponAccuracyPreset>(nullptr,*(PackageName+TEXT(".")+Name),nullptr,LOAD_NoWarn))
	{
		const auto& E=Existing->Settings;
		return E.MovingExtraAngle==S.MovingExtraAngle && E.SlowWalkExtraAngle==S.SlowWalkExtraAngle
			&& E.AirborneExtraAngle==S.AirborneExtraAngle && E.MovementThreshold==S.MovementThreshold ? Existing : nullptr;
	}
	UPackage* Package=CreatePackage(*PackageName);
	UWeaponAccuracyPreset* Asset=NewObject<UWeaponAccuracyPreset>(Package,*Name,RF_Public|RF_Standalone);
	Asset->Settings=S;
	FAssetRegistryModule::AssetCreated(Asset);
	return Save(Asset) ? Asset : nullptr;
}

static bool UpgradeCamera(UWeaponDefinitionData* D)
{
	UWeaponRecoilData* R=D->RecoilData;
	if(R->CameraFeedbackPreset || R->bOverrideCameraFeedback) return true;
	const float Return=R->GetCameraReturnSpeed(), Duration=R->GetSmoothKickDuration(), Immediate=R->GetImmediateKickFraction();
	const FString Key=FString::Printf(TEXT("%.9g,%.9g,%.9g"),Return,Duration,Immediate);
	const FString PackageName=FString::Printf(TEXT("/Game/Code/ConfigurationPresets/DA_Camera_%08X"),FCrc::StrCrc32(*Key));
	const FString Name=FPackageName::GetLongPackageAssetName(PackageName);
	UWeaponCameraFeedbackPreset* Preset=LoadObject<UWeaponCameraFeedbackPreset>(nullptr,*(PackageName+TEXT(".")+Name),nullptr,LOAD_NoWarn);
	if(!Preset)
	{
		UPackage* Package=CreatePackage(*PackageName);
		Preset=NewObject<UWeaponCameraFeedbackPreset>(Package,*Name,RF_Public|RF_Standalone);
		Preset->ReturnSpeed=Return; Preset->SmoothKickDuration=Duration; Preset->ImmediateKickFraction=Immediate;
		FAssetRegistryModule::AssetCreated(Preset);
		if(!Save(Preset)) {UE_LOG(LogTemp,Error,TEXT("Camera preset save failed")); return false;}
	}
	if(!R->IsIn(D))
	{
		R=DuplicateObject<UWeaponRecoilData>(R,D);
		R->ClearFlags(RF_Standalone); R->SetFlags(RF_Public);
		D->RecoilData=R;
	}
	if(!R->MigrateCameraFeedbackToPreset(Preset)) {UE_LOG(LogTemp,Error,TEXT("Camera preset collision or mismatch"));return false;}
	return true;
}

static void Run(const TArray<FString>& Args)
{
	const bool bApply=Args.Num()==1 && Args[0].Equals(TEXT("Apply"),ESearchCase::IgnoreCase);
	const bool bVerify=Args.Num()==1 && Args[0].Equals(TEXT("Verify"),ESearchCase::IgnoreCase);
	if(Args.Num()!=1 || (!bApply && !bVerify && !Args[0].Equals(TEXT("Preview"),ESearchCase::IgnoreCase)))
	{
		UE_LOG(LogTemp,Error,TEXT("Use Dust2.UpgradeWeaponConfiguration Preview or Apply; backup five definition packages before Apply."));
		return;
	}
	TArray<UWeaponDefinitionData*> Loaded;
	for(const TCHAR* Path:Definitions)
	{
		UWeaponDefinitionData* D=LoadObject<UWeaponDefinitionData>(nullptr,Path);
		if(!D || !D->RecoilData) { UE_LOG(LogTemp,Error,TEXT("Configuration upgrade missing %s"),Path); return; }
		D->MigrateConfiguration();
		if(bVerify && (!D->HipAccuracy.PosturePreset || !D->AimAccuracy.PosturePreset || !D->RecoilData->CameraFeedbackPreset))
		{
			UE_LOG(LogTemp,Error,TEXT("Configuration saved preset reference missing: %s"),Path); return;
		}
		if(!D->RecoilData->bUseFixedPattern || !D->RecoilData->bUseIndependentShotRecoil)
		{
			UE_LOG(LogTemp,Error,TEXT("Configuration upgrade refuses to change non-fixed/non-independent recoil: %s"),Path); return;
		}
		Loaded.Add(D);
		const auto Hip=D->HipAccuracy.BuildProfile(D->FireInterval,10.0f);
		const auto Aim=D->AimAccuracy.BuildProfile(D->FireInterval,10.0f);
		UE_LOG(LogTemp,Display,TEXT("[ConfigFusion] %s modes=%d initial=%d reload=%d ammo=%d/%d reserve=%d hip=%.6g/%.6g/%.6g/%.6g aim=%.6g/%.6g/%.6g/%.6g fixed=%d independent=%d"),
			Path,D->AllowedFireModes.Num(),int32(D->GetInitialFireMode()),int32(D->ReloadStyle),D->InitialMagazineAmmo,D->MagazineCapacity,D->InitialReserveAmmo,
			Hip.StationarySpreadAngle,Hip.SpreadPerShot,Hip.MaxFiringSpread,Hip.SpreadRecoveryDelay,
			Aim.StationarySpreadAngle,Aim.SpreadPerShot,Aim.MaxFiringSpread,Aim.SpreadRecoveryDelay,
			D->RecoilData->bUseFixedPattern,D->RecoilData->bUseIndependentShotRecoil);
		if(bVerify)
		{
			UE_LOG(LogTemp,Display,TEXT("[ConfigFusionRefs] %s hip=%s aim=%s camera=%s"),Path,
				*D->HipAccuracy.PosturePreset->GetPathName(),*D->AimAccuracy.PosturePreset->GetPathName(),*D->RecoilData->CameraFeedbackPreset->GetPathName());
		}
	}
	if(bVerify) {UE_LOG(LogTemp,Display,TEXT("[ConfigFusion] Verify complete: 5 definitions and all preset references reloaded, no packages saved."));return;}
	if(!bApply) {UE_LOG(LogTemp,Display,TEXT("[ConfigFusion] Preview complete: 5 definitions, no packages saved."));return;}
	for(UWeaponDefinitionData* D:Loaded)
	{
		for(FWeaponIntuitiveAccuracy* A:{&D->HipAccuracy,&D->AimAccuracy})
		{
			if(A->PosturePreset || A->bOverridePosture) continue;
			A->PosturePreset=GetPosturePreset(A->GetPosture(D->MovementSpreadSpeedThreshold));
			if(!A->PosturePreset) {UE_LOG(LogTemp,Error,TEXT("Configuration preset save or collision failure"));return;}
		}
		// Camera migration below uses its own exact-value preset and never changes trajectory.
		if(!UpgradeCamera(D)) return;
		if(!Save(D)) {UE_LOG(LogTemp,Error,TEXT("Configuration save failed %s"),*D->GetPathName());return;}
	}
	UE_LOG(LogTemp,Display,TEXT("[ConfigFusion] Apply complete: 5 definitions saved."));
}
static FAutoConsoleCommand Command(TEXT("Dust2.UpgradeWeaponConfiguration"),TEXT("Preview or Apply unified weapon configuration; backup packages first."),FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}
#endif
