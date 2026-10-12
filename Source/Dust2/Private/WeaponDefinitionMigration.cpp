#include "WeaponRuntime.h"

#if WITH_EDITOR

#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponRecoilData.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace WeaponDefinitionMigration
{
struct FWeaponTarget
{
	const TCHAR* WeaponName;
	const TCHAR* RelativeBlueprintPath;
	const TCHAR* DefinitionAssetName;
	bool bUseContinuousFireSpread;
	float EmptyPatternSeedPitch;
};

struct FWeaponPlan
{
	const FWeaponTarget* Target = nullptr;
	UBlueprint* Blueprint = nullptr;
	AWeaponRuntime* Defaults = nullptr;
	UWeaponDefinitionData* Definition = nullptr;
	bool bNeedsBinding = false;
	bool bCreateDefinition = false;
	bool bSaveDefinition = false;
	bool bRepairDefinitionPublicFlags = false;
	bool bBackupDefinition = false;
};

static const FWeaponTarget WeaponTargets[] =
{
	{ TEXT("AK105"), TEXT("Code/Texts/AK105/BP_Weapon_AK105"), TEXT("DA_AK105Definition"), false, 0.0f },
	{ TEXT("R08"), TEXT("Code/Texts/R08/Bp_Weapon_R08"), TEXT("DA_R08Definition"), true, 0.65f },
	{ TEXT("SRM-12"), TEXT("Code/Texts/SRM-12/Bp_Weapon_SRM-12"), TEXT("DA_SRM-12Definition"), false, 0.0f },
	{ TEXT("TR15"), TEXT("Code/Texts/TR15/BP_Weapon_TR15"), TEXT("DA_TR15Definition"), false, 0.0f },
	{ TEXT("Viper"), TEXT("Code/Texts/Viper/BP_Weapon_Viper"), TEXT("DA_ViperDefinition"), true, 0.4f },
};

static const FName MigrationMarkerKey(TEXT("Dust2WeaponDefinitionMigrationSource"));

static FString LongBlueprintPackageName(const FWeaponTarget& Target)
{
	return FString::Printf(TEXT("/Game/%s"), Target.RelativeBlueprintPath);
}

static FString BlueprintObjectPath(const FWeaponTarget& Target)
{
	const FString PackageName = LongBlueprintPackageName(Target);
	return FString::Printf(TEXT("%s.%s"), *PackageName, *FPaths::GetBaseFilename(Target.RelativeBlueprintPath));
}

static FString DefinitionLongPackageName(const FWeaponTarget& Target)
{
	const FString BlueprintPackageName = LongBlueprintPackageName(Target);
	return FString::Printf(TEXT("%s/%s"), *FPaths::GetPath(BlueprintPackageName), Target.DefinitionAssetName);
}

static FString DefinitionObjectPath(const FWeaponTarget& Target)
{
	const FString PackageName = DefinitionLongPackageName(Target);
	return FString::Printf(TEXT("%s.%s"), *PackageName, Target.DefinitionAssetName);
}

static FString PackageFilename(const FString& LongPackageName)
{
	return FPackageName::LongPackageNameToFilename(
		LongPackageName,
		FPackageName::GetAssetPackageExtension());
}

static UWeaponDefinitionData* FindDefinition(const FWeaponTarget& Target)
{
	const FString ObjectPath = DefinitionObjectPath(Target);
	if (UWeaponDefinitionData* Loaded = FindObject<UWeaponDefinitionData>(nullptr, *ObjectPath))
	{
		return Loaded;
	}
	return LoadObject<UWeaponDefinitionData>(nullptr, *ObjectPath);
}

static bool HasMatchingMigrationMarker(const UWeaponDefinitionData* Definition, const FWeaponTarget& Target)
{
	if (!Definition || !Definition->GetOutermost())
	{
		return false;
	}
	const FString& Marker = Definition->GetOutermost()->GetMetaData().GetValue(Definition, MigrationMarkerKey);
	return Marker == BlueprintObjectPath(Target);
}

static void ApplyPerWeaponOverrides(UWeaponDefinitionData* Definition, const FWeaponTarget& Target)
{
	if (!Definition || !Target.bUseContinuousFireSpread)
	{
		return;
	}

	// R08 and Viper keep an accurate first shot; only accepted follow-up shots add spread.
	Definition->StationarySpreadAngle = 0.0f;
	Definition->bUseFiringSpread = true;
	Definition->SpreadPerShot = 0.15f;
	Definition->MaxFiringSpread = 0.8f;
	Definition->SpreadRecoveryDelay = FMath::Max(Definition->FireInterval + 0.05f, 0.35f);
	Definition->SpreadRecoverySpeed = 2.0f;

	if (!Definition->RecoilData)
	{
		Definition->RecoilData = NewObject<UWeaponRecoilData>(Definition, TEXT("Recoil"), RF_Transactional);
	}
	UWeaponRecoilData* Recoil = Definition->RecoilData;
	Recoil->bUseFixedPattern = true;
	Recoil->bUseIndependentShotRecoil = true;
	Recoil->PatternResetDelay = 0.0f;
	if (Recoil->FixedPattern.IsEmpty())
	{
		Recoil->FixedPattern.Add(FVector2D(Target.EmptyPatternSeedPitch, 0.0f));
	}
}

static bool SaveAssetPackage(UObject* Asset)
{
	if (!Asset || !Asset->GetOutermost())
	{
		return false;
	}
	UPackage* Package = Asset->GetOutermost();
	const FString Filename = PackageFilename(Package->GetName());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bSlowTask = false;
	const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：保存失败：%s"), *Filename);
	}
	return bSaved;
}

static bool BackupBlueprints(const TArray<FWeaponPlan>& Plans, FString& OutBackupDirectory)
{
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	const FString BaseBackupDirectory = FPaths::Combine(
		TEXT("D:/AI/UE5/文档备份/W04-后坐统一-20261011"),
		FString::Printf(TEXT("配置资产备份-%s"), *Timestamp));
	OutBackupDirectory = BaseBackupDirectory;
	for (int32 CollisionIndex = 1; IFileManager::Get().DirectoryExists(*OutBackupDirectory); ++CollisionIndex)
	{
		OutBackupDirectory = FString::Printf(TEXT("%s-%02d"), *BaseBackupDirectory, CollisionIndex);
	}

	if (!IFileManager::Get().MakeDirectory(*OutBackupDirectory, true))
	{
		UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：无法创建备份目录：%s"), *OutBackupDirectory);
		return false;
	}

	TArray<FString> ManifestLines;
	ManifestLines.Add(TEXT("Dust2 枪械配置迁移备份清单"));
	ManifestLines.Add(FString::Printf(TEXT("时间：%s"), *Timestamp));
	ManifestLines.Add(TEXT("本次备份待绑定修改的武器蓝图，以及需要修复并重存的、带本迁移标记的既有配置资产。"));

	const FString ContentDirectory = FPaths::ProjectContentDir();
	for (const FWeaponPlan& Plan : Plans)
	{
		if (!Plan.bNeedsBinding)
		{
			continue;
		}

		const FString RelativeFilename = FString(Plan.Target->RelativeBlueprintPath) + FPackageName::GetAssetPackageExtension();
		const FString SourceFilename = FPaths::Combine(ContentDirectory, RelativeFilename);
		const FString DestinationFilename = FPaths::Combine(OutBackupDirectory, RelativeFilename);
		if (!FPaths::FileExists(SourceFilename))
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：找不到待修改蓝图的原始文件：%s"), *SourceFilename);
			return false;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestinationFilename), true);
		if (IFileManager::Get().Copy(*DestinationFilename, *SourceFilename, true, true) != COPY_OK ||
			!FPaths::FileExists(DestinationFilename))
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：蓝图备份失败：%s"), *SourceFilename);
			return false;
		}
		ManifestLines.Add(FString::Printf(TEXT("%s -> %s"), *SourceFilename, *DestinationFilename));
	}

	for (const FWeaponPlan& Plan : Plans)
	{
		if (!Plan.bBackupDefinition)
		{
			continue;
		}

		const FString RelativeDefinitionFilename = FPaths::Combine(
			FPaths::GetPath(Plan.Target->RelativeBlueprintPath),
			FString(Plan.Target->DefinitionAssetName) + FPackageName::GetAssetPackageExtension());
		const FString SourceFilename = FPaths::Combine(ContentDirectory, RelativeDefinitionFilename);
		const FString DestinationFilename = FPaths::Combine(OutBackupDirectory, RelativeDefinitionFilename);
		if (!FPaths::FileExists(SourceFilename))
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：找不到待修复配置资产的原始文件：%s"), *SourceFilename);
			return false;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestinationFilename), true);
		if (IFileManager::Get().Copy(*DestinationFilename, *SourceFilename, true, true) != COPY_OK ||
			!FPaths::FileExists(DestinationFilename))
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：既有配置资产备份失败：%s"), *SourceFilename);
			return false;
		}
		ManifestLines.Add(FString::Printf(TEXT("既有迁移配置资产（添加 RF_Public 前备份）：%s -> %s"), *SourceFilename, *DestinationFilename));
	}

	ManifestLines.Add(TEXT("配置资产："));
	for (const FWeaponPlan& Plan : Plans)
	{
		ManifestLines.Add(FString::Printf(TEXT("%s: %s"), Plan.Target->WeaponName, *DefinitionObjectPath(*Plan.Target)));
	}

	const FString ManifestPath = FPaths::Combine(OutBackupDirectory, TEXT("manifest.txt"));
	FString ManifestText;
	for (const FString& Line : ManifestLines)
	{
		if (!ManifestText.IsEmpty())
		{
			ManifestText += TEXT("\r\n");
		}
		ManifestText += Line;
	}
	if (!FFileHelper::SaveStringToFile(
		ManifestText,
		*ManifestPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：备份清单写入失败：%s"), *ManifestPath);
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("枪械配置迁移：原蓝图备份完成：%s"), *OutBackupDirectory);
	return true;
}

static void MigrateWeaponDefinitions()
{
	TArray<FWeaponPlan> Plans;
	Plans.Reserve(UE_ARRAY_COUNT(WeaponTargets));
	TArray<FString> PreflightErrors;

	// Load and validate every class and destination before creating or saving any asset.
	for (const FWeaponTarget& Target : WeaponTargets)
	{
		FWeaponPlan Plan;
		Plan.Target = &Target;
		Plan.Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath(Target));
		if (!Plan.Blueprint || !Plan.Blueprint->GeneratedClass ||
			!Plan.Blueprint->GeneratedClass->IsChildOf(AWeaponRuntime::StaticClass()))
		{
			PreflightErrors.Add(FString::Printf(TEXT("%s：找不到有效的 AWeaponRuntime 蓝图类：%s"), Target.WeaponName, *BlueprintObjectPath(Target)));
			Plans.Add(Plan);
			continue;
		}

		Plan.Defaults = Plan.Blueprint->GeneratedClass->GetDefaultObject<AWeaponRuntime>();
		if (!Plan.Defaults)
		{
			PreflightErrors.Add(FString::Printf(TEXT("%s：无法读取蓝图默认对象。"), Target.WeaponName));
			Plans.Add(Plan);
			continue;
		}
		const FObjectPropertyBase* BallisticsProperty = FindFProperty<FObjectPropertyBase>(
			Plan.Defaults->GetClass(), TEXT("BallisticsData"));
		const FObjectPropertyBase* RecoilProperty = FindFProperty<FObjectPropertyBase>(
			Plan.Defaults->GetClass(), TEXT("RecoilData"));
		const UObject* SourceBallistics = BallisticsProperty
			? BallisticsProperty->GetObjectPropertyValue_InContainer(Plan.Defaults)
			: nullptr;
		const UObject* SourceRecoil = RecoilProperty
			? RecoilProperty->GetObjectPropertyValue_InContainer(Plan.Defaults)
			: nullptr;
		if (!BallisticsProperty ||
			!BallisticsProperty->PropertyClass ||
			!BallisticsProperty->PropertyClass->IsChildOf(UWeaponBallisticsData::StaticClass()) ||
			!SourceBallistics ||
			!SourceBallistics->IsA(UWeaponBallisticsData::StaticClass()))
		{
			PreflightErrors.Add(FString::Printf(TEXT("%s：蓝图默认对象缺少有效的 BallisticsData 伤害资产；为避免默认零值覆盖伤害，整批迁移已停止。"), Target.WeaponName));
		}
		if (!RecoilProperty ||
			!RecoilProperty->PropertyClass ||
			!RecoilProperty->PropertyClass->IsChildOf(UWeaponRecoilData::StaticClass()) ||
			!SourceRecoil ||
			!SourceRecoil->IsA(UWeaponRecoilData::StaticClass()))
		{
			PreflightErrors.Add(FString::Printf(TEXT("%s：蓝图默认对象缺少有效的 RecoilData 后坐资产，整批迁移已停止。"), Target.WeaponName));
		}

		const FString ExpectedDefinitionPath = DefinitionObjectPath(Target);
		if (Plan.Defaults->WeaponDefinition)
		{
			if (Plan.Defaults->WeaponDefinition->GetPathName() != ExpectedDefinitionPath)
			{
				PreflightErrors.Add(FString::Printf(
					TEXT("%s：已经绑定其他配置资产（%s）；为避免覆盖现有设置，整批迁移已停止。"),
					Target.WeaponName,
					*Plan.Defaults->WeaponDefinition->GetPathName()));
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("枪械配置迁移：%s 已绑定目标配置，跳过。"), Target.WeaponName);
				if (HasMatchingMigrationMarker(Plan.Defaults->WeaponDefinition, Target))
				{
					const bool bDefinitionExistsOnDisk = FPaths::FileExists(PackageFilename(DefinitionLongPackageName(Target)));
					if (!Plan.Defaults->WeaponDefinition->RecoilData || !Plan.Defaults->WeaponDefinition->BallisticsData)
					{
						PreflightErrors.Add(FString::Printf(TEXT("%s：已绑定的迁移配置内嵌后坐或伤害对象为空；为避免写入不完整数据，整批迁移已停止。"), Target.WeaponName));
					}
					else if (!Plan.Defaults->WeaponDefinition->RecoilData->HasAnyFlags(RF_Public) ||
						!Plan.Defaults->WeaponDefinition->BallisticsData->HasAnyFlags(RF_Public))
					{
						Plan.bRepairDefinitionPublicFlags = true;
						Plan.bSaveDefinition = true;
						Plan.bBackupDefinition = bDefinitionExistsOnDisk;
						UE_LOG(LogTemp, Warning, TEXT("枪械配置迁移：已绑定的 %s 迁移配置内嵌对象缺 RF_Public，将先备份现有配置资产，再修复并重存。"), Target.WeaponName);
					}
				}
			}
			Plan.bNeedsBinding = false;
			Plan.Definition = Plan.Defaults->WeaponDefinition;
			Plans.Add(Plan);
			continue;
		}

		Plan.bNeedsBinding = true;
		if (Plan.Blueprint->GetOutermost()->IsDirty())
		{
			PreflightErrors.Add(FString::Printf(TEXT("%s：蓝图包有未保存改动；请先保存或关闭该资产，再运行迁移。"), Target.WeaponName));
		}

		const FString DefinitionFilename = PackageFilename(DefinitionLongPackageName(Target));
		const bool bDefinitionExistsOnDisk = FPaths::FileExists(DefinitionFilename);
		Plan.Definition = FindDefinition(Target);
		if (bDefinitionExistsOnDisk || Plan.Definition)
		{
			if (!Plan.Definition)
			{
				PreflightErrors.Add(FString::Printf(TEXT("%s：目标配置资产文件已存在但无法加载，拒绝覆盖：%s"), Target.WeaponName, *DefinitionFilename));
			}
			else if (!HasMatchingMigrationMarker(Plan.Definition, Target))
			{
				PreflightErrors.Add(FString::Printf(TEXT("%s：目标配置资产已存在且不是本迁移创建，拒绝覆盖：%s"), Target.WeaponName, *ExpectedDefinitionPath));
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("枪械配置迁移：%s 将继续使用上次迁移生成的资产：%s"), Target.WeaponName, *ExpectedDefinitionPath);
				if (Plan.Definition->GetOutermost()->IsDirty() && bDefinitionExistsOnDisk)
				{
					PreflightErrors.Add(FString::Printf(TEXT("%s：上次迁移生成的配置资产在磁盘上已有文件且当前有未保存改动；为避免覆盖，整批迁移已停止。"), Target.WeaponName));
				}
				Plan.bSaveDefinition = !bDefinitionExistsOnDisk;
				if (!Plan.Definition->RecoilData || !Plan.Definition->BallisticsData)
				{
					PreflightErrors.Add(FString::Printf(TEXT("%s：迁移配置的内嵌后坐或伤害对象为空；为避免写入不完整数据，整批迁移已停止。"), Target.WeaponName));
				}
				else if (!Plan.Definition->RecoilData->HasAnyFlags(RF_Public) ||
					!Plan.Definition->BallisticsData->HasAnyFlags(RF_Public))
				{
					Plan.bRepairDefinitionPublicFlags = true;
					Plan.bSaveDefinition = true;
					Plan.bBackupDefinition = bDefinitionExistsOnDisk;
					UE_LOG(LogTemp, Warning, TEXT("枪械配置迁移：%s 的内嵌后坐/伤害对象缺 RF_Public，将先备份现有配置资产，再修复并重存。"), Target.WeaponName);
				}
			}
		}
		else
		{
			Plan.bCreateDefinition = true;
			Plan.bSaveDefinition = true;
		}
		Plans.Add(Plan);
	}

	if (!PreflightErrors.IsEmpty())
	{
		for (const FString& Error : PreflightErrors)
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移预检失败：%s"), *Error);
		}
		UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：预检未通过；没有创建、绑定或保存任何资产。"));
		return;
	}

	bool bHasChanges = false;
	for (const FWeaponPlan& Plan : Plans)
	{
		bHasChanges |= Plan.bNeedsBinding || Plan.bCreateDefinition || Plan.bSaveDefinition;
	}
	if (!bHasChanges)
	{
		UE_LOG(LogTemp, Display, TEXT("枪械配置迁移：五把枪均已绑定目标配置，无需修改。"));
		return;
	}

	FString BackupDirectory;
	if (!BackupBlueprints(Plans, BackupDirectory))
	{
		UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：备份未完整通过；没有创建或保存配置资产。"));
		return;
	}

	// Repair only metadata-marked assets, and only after their on-disk files have been backed up.
	for (FWeaponPlan& Plan : Plans)
	{
		if (!Plan.bRepairDefinitionPublicFlags || !Plan.Definition)
		{
			continue;
		}
		Plan.Definition->RecoilData->SetFlags(RF_Public);
		Plan.Definition->BallisticsData->SetFlags(RF_Public);
		Plan.Definition->GetOutermost()->MarkPackageDirty();
	}

	// Build every new configuration from the real Blueprint CDO values before saving any config.
	for (FWeaponPlan& Plan : Plans)
	{
		if (!Plan.bCreateDefinition)
		{
			continue;
		}

		UPackage* Package = CreatePackage(*DefinitionLongPackageName(*Plan.Target));
		if (!Package)
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：无法创建配置包：%s"), *DefinitionLongPackageName(*Plan.Target));
			return;
		}
		Plan.Definition = NewObject<UWeaponDefinitionData>(
			Package,
			FName(Plan.Target->DefinitionAssetName),
			RF_Public | RF_Standalone | RF_Transactional);
		if (!Plan.Definition)
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：无法创建配置对象：%s"), *DefinitionObjectPath(*Plan.Target));
			return;
		}

		Plan.Defaults = Plan.Blueprint->GeneratedClass->GetDefaultObject<AWeaponRuntime>();
		Plan.Defaults->CaptureDefinition(Plan.Definition);
		ApplyPerWeaponOverrides(Plan.Definition, *Plan.Target);
		Package->GetMetaData().SetValue(Plan.Definition, MigrationMarkerKey, *BlueprintObjectPath(*Plan.Target));
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Plan.Definition);
		UE_LOG(LogTemp, Display, TEXT("枪械配置迁移：已从 %s 的实际蓝图默认值构建 %s。"), Plan.Target->WeaponName, *DefinitionObjectPath(*Plan.Target));
	}

	// Save all new data assets first. A failed partial run can safely reuse only assets carrying this source marker.
	for (FWeaponPlan& Plan : Plans)
	{
		if (!Plan.bSaveDefinition)
		{
			continue;
		}
		if (!SaveAssetPackage(Plan.Definition))
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：配置资产阶段失败；蓝图尚未开始绑定。重跑时只会复用带匹配迁移标记的资产。"));
			return;
		}
	}

	for (FWeaponPlan& Plan : Plans)
	{
		if (!Plan.bNeedsBinding)
		{
			continue;
		}
		if (!Plan.Definition || !Plan.Blueprint || !Plan.Blueprint->GeneratedClass)
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：%s 缺少已保存配置或蓝图类，绑定停止。"), Plan.Target->WeaponName);
			return;
		}

		AWeaponRuntime* Defaults = Plan.Blueprint->GeneratedClass->GetDefaultObject<AWeaponRuntime>();
		Defaults->Modify();
		Defaults->WeaponDefinition = Plan.Definition;
		Defaults->ApplyDefinitionSettings();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Plan.Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Plan.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		if (Plan.Blueprint->Status == BS_Error || !Plan.Blueprint->GeneratedClass)
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：%s 蓝图编译失败；备份位于 %s；本次可能已有前序资产保存。"), Plan.Target->WeaponName, *BackupDirectory);
			return;
		}

		// Reacquire the generated CDO after compilation, then persist the binding and refreshed static defaults.
		Defaults = Plan.Blueprint->GeneratedClass->GetDefaultObject<AWeaponRuntime>();
		Defaults->Modify();
		Defaults->WeaponDefinition = Plan.Definition;
		Defaults->ApplyDefinitionSettings();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Plan.Blueprint);
		if (!SaveAssetPackage(Plan.Blueprint))
		{
			UE_LOG(LogTemp, Error, TEXT("枪械配置迁移：%s 蓝图保存失败；备份位于 %s。"), Plan.Target->WeaponName, *BackupDirectory);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("枪械配置迁移：%s 已绑定并保存。"), Plan.Target->WeaponName);
	}

	UE_LOG(LogTemp, Display, TEXT("枪械配置迁移：绑定流程已完成。备份：%s。请运行 Dust2.VerifyWeaponDefinitions 检查保存后的绑定和数值。"), *BackupDirectory);
}

static bool CompareObjectProperties(const UObject* Expected, const UObject* Actual, const TSet<FName>& ExcludedProperties, FString& OutMismatch)
{
	if (!Expected || !Actual || Expected->GetClass() != Actual->GetClass())
	{
		OutMismatch = TEXT("对象为空或类型不同");
		return false;
	}

	for (TFieldIterator<FProperty> It(Expected->GetClass()); It; ++It)
	{
		const FProperty* Property = *It;
		if (ExcludedProperties.Contains(Property->GetFName()) ||
			Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient) ||
			Property->IsA<FObjectPropertyBase>())
		{
			continue;
		}

		const void* ExpectedValue = Property->ContainerPtrToValuePtr<void>(Expected);
		const void* ActualValue = Property->ContainerPtrToValuePtr<void>(Actual);
		if (!Property->Identical(ExpectedValue, ActualValue))
		{
			OutMismatch = Property->GetName();
			return false;
		}
	}
	return true;
}

static bool VerifyWeaponDefinition(const FWeaponTarget& Target)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath(Target));
	if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AWeaponRuntime::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("配置验证失败：%s 蓝图无法加载。"), Target.WeaponName);
		return false;
	}

	const AWeaponRuntime* Defaults = Blueprint->GeneratedClass->GetDefaultObject<AWeaponRuntime>();
	const UWeaponDefinitionData* Definition = Defaults ? Defaults->WeaponDefinition : nullptr;
	if (!Definition)
	{
		UE_LOG(LogTemp, Error, TEXT("配置验证失败：%s 未绑定枪械配置。"), Target.WeaponName);
		return false;
	}
	if (Definition->GetPathName() != DefinitionObjectPath(Target))
	{
		UE_LOG(LogTemp, Error, TEXT("配置验证失败：%s 绑定到非目标资产 %s。"), Target.WeaponName, *Definition->GetPathName());
		return false;
	}

	UWeaponDefinitionData* Captured = NewObject<UWeaponDefinitionData>(GetTransientPackage());
	Defaults->CaptureDefinition(Captured);
	TSet<FName> ExcludedDefinitionProperties =
	{
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, BallisticsData),
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, RecoilData),
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, bUseFiringSpread),
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, SpreadPerShot),
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, MaxFiringSpread),
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, SpreadRecoveryDelay),
		GET_MEMBER_NAME_CHECKED(UWeaponDefinitionData, SpreadRecoverySpeed),
	};
	FString Mismatch;
	if (!CompareObjectProperties(Captured, Definition, ExcludedDefinitionProperties, Mismatch))
	{
		UE_LOG(LogTemp, Error, TEXT("配置验证失败：%s 的蓝图默认值与配置资产不一致，字段：%s。"), Target.WeaponName, *Mismatch);
		return false;
	}

	if (!CompareObjectProperties(Captured->BallisticsData, Definition->BallisticsData, {}, Mismatch) ||
		!CompareObjectProperties(Captured->RecoilData, Definition->RecoilData, {}, Mismatch))
	{
		UE_LOG(LogTemp, Error, TEXT("配置验证失败：%s 的伤害或后坐内嵌数据与配置资产不一致，字段：%s。"), Target.WeaponName, *Mismatch);
		return false;
	}

	if (Definition->bUseFiringSpread != Target.bUseContinuousFireSpread)
	{
		UE_LOG(LogTemp, Error, TEXT("配置验证失败：%s 的连续射击散布启用状态不符合本次约定。"), Target.WeaponName);
		return false;
	}
	if (Target.bUseContinuousFireSpread)
	{
		const float ExpectedDelay = FMath::Max(Definition->FireInterval + 0.05f, 0.35f);
		if (!FMath::IsNearlyEqual(Definition->StationarySpreadAngle, 0.0f) ||
			!FMath::IsNearlyEqual(Definition->SpreadPerShot, 0.15f) ||
			!FMath::IsNearlyEqual(Definition->MaxFiringSpread, 0.8f) ||
			!FMath::IsNearlyEqual(Definition->SpreadRecoveryDelay, ExpectedDelay) ||
			!FMath::IsNearlyEqual(Definition->SpreadRecoverySpeed, 2.0f) ||
			!Definition->RecoilData || !Definition->RecoilData->bUseFixedPattern ||
			!Definition->RecoilData->bUseIndependentShotRecoil ||
			!FMath::IsNearlyEqual(Definition->RecoilData->PatternResetDelay, 0.0f) ||
			Definition->RecoilData->FixedPattern.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("配置验证失败：%s 的首发精度、连续射击散布或独立后坐参数不符合本次约定。"), Target.WeaponName);
			return false;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("配置验证通过：%s -> %s。"), Target.WeaponName, *Definition->GetPathName());
	return true;
}

static void VerifyWeaponDefinitions()
{
	bool bAllValid = true;
	for (const FWeaponTarget& Target : WeaponTargets)
	{
		bAllValid &= VerifyWeaponDefinition(Target);
	}
	if (bAllValid)
	{
		UE_LOG(LogTemp, Display, TEXT("枪械配置只读验证通过：五把武器绑定及玩法数据一致。"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("枪械配置只读验证未通过；未修改任何资产。"));
	}
}

static FAutoConsoleCommand MigrateWeaponDefinitionsCommand(
	TEXT("Dust2.MigrateWeaponDefinitions"),
	TEXT("从五把武器蓝图的实际默认值创建并绑定集中玩法配置资产；运行前请先保存相关蓝图。"),
	FConsoleCommandDelegate::CreateStatic(&MigrateWeaponDefinitions));

static FAutoConsoleCommand VerifyWeaponDefinitionsCommand(
	TEXT("Dust2.VerifyWeaponDefinitions"),
	TEXT("只读检查五把武器的配置绑定、内嵌数据和蓝图默认值；不会保存或修改资产。"),
	FConsoleCommandDelegate::CreateStatic(&VerifyWeaponDefinitions));
} // namespace WeaponDefinitionMigration

#endif // WITH_EDITOR
