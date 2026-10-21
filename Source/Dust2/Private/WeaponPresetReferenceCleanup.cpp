#include "WeaponPresetReferenceCleanup.h"
#include "WeaponDefinitionData.h"
#include "WeaponAccuracyData.h"
#include "WeaponRecoilData.h"

namespace DustWeaponPresetCleanup
{
bool LocalizePosture(FWeaponIntuitiveAccuracy& Accuracy, float MovementThreshold)
{
    if (!Accuracy.PosturePreset) return false;
    const FWeaponPostureSettings Effective = Accuracy.GetPosture(MovementThreshold);
    Accuracy.PostureOverride = Effective;
    Accuracy.bUsePostureSettings = true;
    Accuracy.bOverridePosture = true;
    Accuracy.PosturePreset = nullptr;
    return true;
}

bool LocalizeCamera(UWeaponRecoilData& Recoil)
{
    if (!Recoil.CameraFeedbackPreset) return false;
    FWeaponCameraFeedbackParameters Effective;
    Effective.ReturnSpeed = Recoil.GetCameraReturnSpeed();
    Effective.SmoothKickDuration = Recoil.GetSmoothKickDuration();
    Effective.ImmediateKickFraction = Recoil.GetImmediateKickFraction();
    Recoil.CameraFeedbackOverrides = Effective;
    Recoil.bOverrideCameraFeedback = true;
    Recoil.CameraFeedbackPreset = nullptr;
    return true;
}
}

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
const FName PresetFolder(TEXT("/Game/Code/ConfigurationPresets"));

bool IsInsidePresetFolder(FName PackageName)
{
    return PackageName.ToString().StartsWith(PresetFolder.ToString() + TEXT("/"));
}

TArray<FAssetData> GetOldPresets(IAssetRegistry& Registry)
{
    TArray<FAssetData> Presets;
    Registry.GetAssetsByPath(PresetFolder, Presets, true);
    return Presets;
}

void VerifyDiskReferences(IAssetRegistry& Registry)
{
    const TArray<FAssetData> Presets = GetOldPresets(Registry);
    int32 ExternalEdges = 0;
    for (const FAssetData& Preset : Presets)
    {
        TArray<FName> Referencers;
        Registry.GetReferencers(Preset.PackageName, Referencers);
        for (const FName Referencer : Referencers)
        {
            if (IsInsidePresetFolder(Referencer)) continue;
            ++ExternalEdges;
            UE_LOG(LogTemp, Warning, TEXT("[PresetCleanup] DISK_REF %s -> %s"),
                *Referencer.ToString(), *Preset.PackageName.ToString());
        }
    }
    UE_LOG(LogTemp, Display, TEXT("[PresetCleanup] Verify presets=%d external_references=%d (disk only; unsaved changes and memory references excluded)."),
        Presets.Num(), ExternalEdges);
}

void RunPresetCleanup(const TArray<FString>& Args)
{
    if (Args.Num() != 1 || (!Args[0].Equals(TEXT("Preview"), ESearchCase::IgnoreCase)
        && !Args[0].Equals(TEXT("Apply"), ESearchCase::IgnoreCase)
        && !Args[0].Equals(TEXT("Verify"), ESearchCase::IgnoreCase)))
    {
        UE_LOG(LogTemp, Warning, TEXT("Use Dust2.CleanupWeaponPresetReferences Preview, Apply or Verify. Apply changes loaded data only; save all and restart before Verify/delete."));
        return;
    }
    if (GEditor && GEditor->PlayWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PresetCleanup] Stop PIE before cleaning asset references."));
        return;
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    if (Registry.IsLoadingAssets())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PresetCleanup] Asset registry is still loading; retry after discovery finishes."));
        return;
    }
    if (Args[0].Equals(TEXT("Verify"), ESearchCase::IgnoreCase))
    {
        VerifyDiskReferences(Registry);
        return;
    }
    const bool bApply = Args[0].Equals(TEXT("Apply"), ESearchCase::IgnoreCase);
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(TEXT("/Game/Code")));
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UWeaponDefinitionData::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UWeaponAccuracyData::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UWeaponRecoilData::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    TArray<FAssetData> Configurations;
    Registry.GetAssets(Filter, Configurations);
    TSet<FName> ConfigurationPackages;
    for (const FAssetData& Asset : Configurations) ConfigurationPackages.Add(Asset.PackageName);
    TSet<FName> Packages = ConfigurationPackages;
    // Include actual old-preset referrers under Code, e.g. a Blueprint with an embedded recoil object.
    for (const FAssetData& Preset : GetOldPresets(Registry))
    {
        TArray<FName> Referencers;
        Registry.GetReferencers(Preset.PackageName, Referencers);
        for (const FName Referencer : Referencers)
            if (Referencer.ToString().StartsWith(TEXT("/Game/Code/")) && !IsInsidePresetFolder(Referencer))
                Packages.Add(Referencer);
    }
    int32 Detached = 0, Pending = 0, DirtyPackages = 0, FailedLoads = 0, DirtyFailures = 0;
    for (const FName PackageName : Packages)
    {
        UPackage* Package = LoadPackage(nullptr, *PackageName.ToString(), LOAD_None);
        if (!Package)
        {
            ++FailedLoads;
            UE_LOG(LogTemp, Error, TEXT("[PresetCleanup] LOAD_FAILED %s"), *PackageName.ToString());
            continue;
        }
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package, Objects, true);
        int32 PackageDetached = 0;
        for (UObject* Object : Objects)
        {
            if (!IsValid(Object) || Object->HasAnyFlags(RF_Transient)) continue;
            if (UWeaponDefinitionData* D = Cast<UWeaponDefinitionData>(Object))
            {
                Pending += (D->HipAccuracy.PosturePreset != nullptr) + (D->AimAccuracy.PosturePreset != nullptr);
                if (bApply)
                {
                    D->Modify();
                    PackageDetached += DustWeaponPresetCleanup::LocalizePosture(D->HipAccuracy, D->MovementSpreadSpeedThreshold);
                    PackageDetached += DustWeaponPresetCleanup::LocalizePosture(D->AimAccuracy, D->MovementSpreadSpeedThreshold);
                }
            }
            else if (UWeaponAccuracyData* A = Cast<UWeaponAccuracyData>(Object))
            {
                const int32 References = (A->HipAccuracy.PosturePreset != nullptr)
                    + (A->AimAccuracy.PosturePreset != nullptr) + (A->CameraFeedbackPreset != nullptr);
                Pending += References;
                if (bApply)
                {
                    A->Modify();
                    A->NormalizeDirectParameters();
                    PackageDetached += References;
                }
            }
            else if (UWeaponRecoilData* R = Cast<UWeaponRecoilData>(Object))
            {
                Pending += R->CameraFeedbackPreset != nullptr;
                if (bApply && R->CameraFeedbackPreset)
                {
                    R->Modify();
                    PackageDetached += DustWeaponPresetCleanup::LocalizeCamera(*R);
                }
            }
        }
        Detached += PackageDetached;
        // Accuracy.PostLoad may already have removed in-memory references. Force its
        // package dirty as well, so Save All actually persists that earlier conversion.
        if (bApply && (ConfigurationPackages.Contains(PackageName) || PackageDetached > 0))
        {
            if (Package->MarkPackageDirty()) ++DirtyPackages;
            else
            {
                ++DirtyFailures;
                UE_LOG(LogTemp, Error, TEXT("[PresetCleanup] DIRTY_FAILED %s"), *PackageName.ToString());
            }
            UE_LOG(LogTemp, Display, TEXT("[PresetCleanup] SAVE_REQUIRED %s detached=%d"),
                *PackageName.ToString(), PackageDetached);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("[PresetCleanup] %s packages=%d pending_memory_refs=%d detached=%d dirty_packages=%d failed_loads=%d dirty_failures=%d. No packages saved or assets deleted."),
        bApply ? TEXT("Apply") : TEXT("Preview"), Packages.Num(), Pending, Detached, DirtyPackages, FailedLoads, DirtyFailures);
    if (bApply)
    {
        UE_LOG(LogTemp, Display, TEXT("[PresetCleanup] Save All, restart editor, then run Dust2.CleanupWeaponPresetReferences Verify. Delete normally only after external_references=0; never force-delete unresolved references."));
    }
    else
    {
        VerifyDiskReferences(Registry);
    }
}

FAutoConsoleCommand PresetCleanupCommand(TEXT("Dust2.CleanupWeaponPresetReferences"),
    TEXT("Preview/Apply/Verify legacy weapon preset references. Apply preserves effective local values; save all manually."),
    FConsoleCommandWithArgsDelegate::CreateStatic(&RunPresetCleanup));
}
#endif
