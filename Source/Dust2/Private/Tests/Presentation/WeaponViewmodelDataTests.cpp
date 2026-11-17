#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/Actor.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Presentation/WeaponPresentationData.h"
#include "Presentation/WeaponViewmodelData.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#endif
#include <limits>

namespace
{
    FWeaponPresentationProfile MakeNativeViewmodelProfile()
    {
        FWeaponPresentationProfile Profile;
        Profile.VisualWeaponClass = AActor::StaticClass();
        Profile.bUseViewmodelSettings = true;
        Profile.ViewmodelSettings.IdlePose = NewObject<UAnimSequence>();
        Profile.ViewmodelSettings.Viewmodel.PoseOffset.SetTranslation(FVector(1.0, 2.0, 3.0));
        Profile.ViewmodelSettings.Viewmodel.SprintOffset.SetTranslation(FVector(4.0, 5.0, 6.0));
        Profile.ViewmodelSettings.MovementSway.Rotation.Scale = FVector(5.0, -1.4, 2.0);
        Profile.ViewmodelSettings.AimingSway.Position.Scale = FVector(0.0, 0.015, 0.015);
        return Profile;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponNativeViewmodelSourceTest,
    "Dust2.WeaponPresentationData.ViewmodelReadsNativeDataWithoutAuthorAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponNativeViewmodelSourceTest::RunTest(const FString& Parameters)
{
    UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
    UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
    Definition->PresentationData = Data;
    Data->bEnablePlayerProfile = true;
    Data->PlayerProfile = MakeNativeViewmodelProfile();
    FWeaponPresentationProfile Resolved;
    TestTrue(TEXT("enabled native profile resolves without TSV or template"),
        UWeaponPresentationData::ResolvePlayerProfileForDefinition(Definition, Resolved)
        == EWeaponPresentationResolveResult::Resolved);
    FWeaponViewmodelSettings Out;
    if (!TestTrue(TEXT("native values are available directly"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Resolved, Out))) return false;
    TestTrue(TEXT("pose and sprint transforms survive resolution"),
        Out.Viewmodel.PoseOffset.GetTranslation().Equals(FVector(1.0, 2.0, 3.0))
        && Out.Viewmodel.SprintOffset.GetTranslation().Equals(FVector(4.0, 5.0, 6.0)));
    TestTrue(TEXT("signed sway scales remain unchanged"),
        Out.MovementSway.Rotation.Scale.Equals(FVector(5.0, -1.4, 2.0)));
    Out.Viewmodel.PoseOffset.SetTranslation(FVector(99.0, 0.0, 0.0));
    TestTrue(TEXT("editing the returned value cannot mutate shared configuration"),
        Data->PlayerProfile.ViewmodelSettings.Viewmodel.PoseOffset.GetTranslation().Equals(FVector(1.0, 2.0, 3.0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponNativeViewmodelValidationTest,
    "Dust2.WeaponPresentationData.ViewmodelValidationAndLegacyOptOut",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponNativeViewmodelValidationTest::RunTest(const FString& Parameters)
{
    FWeaponPresentationProfile Profile = MakeNativeViewmodelProfile();
    FWeaponViewmodelSettings Out = Profile.ViewmodelSettings;
    Profile.bUseViewmodelSettings = false;
    TestFalse(TEXT("opted-out profile does not supply native animation values"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Out));
    TestNull(TEXT("false result clears stale output"), Out.IdlePose.Get());
    Profile.ViewmodelSettings.IdlePose = nullptr;
    TestTrue(TEXT("legacy profile still resolves without native idle or author template"),
        UWeaponPresentationData::IsPlayerProfileUsable(Profile));
    Profile.bUseViewmodelSettings = true;
    TestFalse(TEXT("opted-in profile requires a native idle"), UWeaponPresentationData::IsPlayerProfileUsable(Profile));
    Profile = MakeNativeViewmodelProfile();
    Profile.ViewmodelSettings.AimingSway.Rotation.AdsScale = std::numeric_limits<float>::infinity();
    TestFalse(TEXT("non-finite scale fails native resolution"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Out));
    TestNull(TEXT("invalid result also clears output"), Out.IdlePose.Get());
    Profile = MakeNativeViewmodelProfile();
    Profile.ViewmodelSettings.MovementSway.Position.Stiffness.X = -1.0;
    TestFalse(TEXT("negative stiffness is rejected"), Profile.ViewmodelSettings.IsUsable());
    Profile = MakeNativeViewmodelProfile();
    TestTrue(TEXT("signed authored scales remain legal"), Profile.ViewmodelSettings.IsUsable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyNativeViewmodelIsolationTest,
    "Dust2.WeaponPresentationData.EnemyDoesNotBorrowPlayerViewmodelData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyNativeViewmodelIsolationTest::RunTest(const FString& Parameters)
{
    FWeaponPresentationProfile Player = MakeNativeViewmodelProfile();
    Player.ViewmodelData = NewObject<UWeaponViewmodelData>();
    const FWeaponPresentationProfile Enemy =
        FEnemyWeaponPresentationProfile::FromPresentationProfile(Player).ToPresentationProfile();
    TestFalse(TEXT("compact enemy does not enable the player viewmodel path"), Enemy.bUseViewmodelSettings);
    TestNull(TEXT("compact enemy does not borrow player idle pose"), Enemy.ViewmodelSettings.IdlePose.Get());
    TestNull(TEXT("compact enemy does not borrow the player asset"), Enemy.ViewmodelData.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponExternalViewmodelTest,
    "Dust2.WeaponPresentationData.ExternalViewmodelIsAuthoritative",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponExternalViewmodelTest::RunTest(const FString& Parameters)
{
    FWeaponPresentationProfile Profile = MakeNativeViewmodelProfile();
    UWeaponViewmodelData* Data = NewObject<UWeaponViewmodelData>();
    Data->Settings = Profile.ViewmodelSettings;
    Data->Settings.Viewmodel.PivotOffset = FVector(7.0, 8.0, 9.0);
    Profile.ViewmodelData = Data;
    FWeaponViewmodelSettings Out;
    TestTrue(TEXT("external native asset resolves"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Out));
    TestTrue(TEXT("external asset takes priority over inline values"),
        Out.Viewmodel.PivotOffset.Equals(FVector(7.0, 8.0, 9.0)));
    Out.Viewmodel.PivotOffset = FVector::ZeroVector;
    TestTrue(TEXT("runtime snapshot cannot modify the shared asset"),
        Data->Settings.Viewmodel.PivotOffset.Equals(FVector(7.0, 8.0, 9.0)));
    Data->Settings.IdlePose = nullptr;
    TestFalse(TEXT("invalid external asset cannot fall back to valid inline values"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Out));
    TestNull(TEXT("invalid external asset clears output"), Out.IdlePose.Get());
    TestFalse(TEXT("invalid external asset blocks player resolution"),
        UWeaponPresentationData::IsPlayerProfileUsable(Profile));
    Profile.bUseViewmodelSettings = false;
    TestTrue(TEXT("disabled native path keeps legacy behavior"),
        UWeaponPresentationData::IsPlayerProfileUsable(Profile));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOwnViewmodelEntryTest,
    "Dust2.WeaponPresentationData.EnemyReadsExplicitViewmodelWithoutBorrowingPlayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOwnViewmodelEntryTest::RunTest(const FString& Parameters)
{
    UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
    UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
    Definition->PresentationData = Data;
    Data->bEnablePlayerProfile = true;
    Data->PlayerProfile = MakeNativeViewmodelProfile();
    Data->PlayerProfile.ViewmodelData = NewObject<UWeaponViewmodelData>();
    Data->PlayerProfile.ViewmodelData->Settings = Data->PlayerProfile.ViewmodelSettings;
    Data->bEnableEnemyProfile = true;
    Data->EnemyPresentation.VisualWeaponClass = AActor::StaticClass();
    FWeaponPresentationProfile Profile;
    FWeaponViewmodelSettings Out;
    TestTrue(TEXT("old enemy visual configuration still resolves without native animation data"),
        UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Profile)
        == EWeaponPresentationResolveResult::Resolved);
    TestFalse(TEXT("enemy without an explicit asset cannot borrow the player settings"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Out));

    UWeaponViewmodelData* EnemyData = NewObject<UWeaponViewmodelData>();
    EnemyData->Settings = Data->PlayerProfile.ViewmodelSettings;
    EnemyData->Settings.Viewmodel.PivotOffset = FVector(11.0, 12.0, 13.0);
    Data->EnemyPresentation.ViewmodelData = EnemyData;
    UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Profile);
    if (!TestTrue(TEXT("enemy uses the same native reader without an author object"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Out))) return false;
    TestTrue(TEXT("resolved enemy values come from its explicit asset"),
        Out.Viewmodel.PivotOffset.Equals(FVector(11.0, 12.0, 13.0)));

    EnemyData->Settings.IdlePose = nullptr;
    UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Profile);
    TestFalse(TEXT("invalid enemy data cannot borrow valid player data"),
        UWeaponPresentationData::TryGetPlayerViewmodelSettings(Profile, Out));
    TestNull(TEXT("failed enemy read clears stale output"), Out.IdlePose.Get());
    Data->bEnableEnemyProfile = false;
    TestTrue(TEXT("disabled enemy configuration clears the resolved profile"),
        UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Profile)
        == EWeaponPresentationResolveResult::NotConfigured);
    TestNull(TEXT("disabled profile has no native asset"), Profile.ViewmodelData.Get());

    FEnemyWeaponPresentationProfile NativeOnly;
    NativeOnly.ViewmodelData = EnemyData;
    TestFalse(TEXT("native-only configuration is not mistaken for untouched compatibility data"), NativeOnly.IsDefault());
    Data->EnemyPresentation = NativeOnly;
    Data->EnemyProfile = MakeNativeViewmodelProfile();
    Data->MigrateLegacyEnemyPresentation();
    TestTrue(TEXT("legacy loading cannot overwrite an explicit native enemy asset"),
        Data->EnemyPresentation.ViewmodelData == EnemyData);
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponViewmodelSplitTest,
    "Dust2.WeaponPresentationData.ViewmodelSplitPreservesValuesAndRefusesOverwrite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponViewmodelSplitTest::RunTest(const FString& Parameters)
{
    const FString Name = TEXT("ViewmodelSplit_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* SourcePackage = CreatePackage(*(TEXT("/Game/__Automation/") + Name));
    UWeaponPresentationData* Source = NewObject<UWeaponPresentationData>(SourcePackage, *Name);
    Source->PlayerProfile = MakeNativeViewmodelProfile();
    // An unfinished setup must still migrate without discarding the filled fields.
    Source->PlayerProfile.ViewmodelSettings.IdlePose = nullptr;
    Source->SplitViewmodelAsset();
    UWeaponViewmodelData* Data = Source->PlayerProfile.ViewmodelData;
    if (!TestNotNull(TEXT("split creates and links an own asset"), Data)) return false;
    TestNull(TEXT("unfinished idle remains unfinished"), Data->Settings.IdlePose.Get());
    TestTrue(TEXT("existing pose survives the split"),
        Data->Settings.Viewmodel.PoseOffset.Equals(Source->PlayerProfile.ViewmodelSettings.Viewmodel.PoseOffset));
    TestTrue(TEXT("signed sway survives the split"),
        Data->Settings.MovementSway.Rotation.Scale.Equals(FVector(5.0, -1.4, 2.0)));
    Source->PlayerProfile.ViewmodelSettings.WalkIntensity = 42.0f;
    Source->SplitViewmodelAsset();
    TestTrue(TEXT("repeat split keeps the original reference"), Source->PlayerProfile.ViewmodelData == Data);
    TestEqual(TEXT("repeat split cannot overwrite edited asset data"), Data->Settings.WalkIntensity, 1.0f);
    Source->PlayerProfile.ViewmodelData = nullptr;
    AddExpectedError(TEXT("拆分取消："), EAutomationExpectedErrorFlags::Contains, 1);
    Source->SplitViewmodelAsset();
    TestNull(TEXT("occupied target is not reassigned or overwritten"), Source->PlayerProfile.ViewmodelData.Get());
    TestEqual(TEXT("collision leaves the existing asset unchanged"), Data->Settings.WalkIntensity, 1.0f);
    FAssetRegistryModule::AssetDeleted(Data);
    UPackage* TargetPackage = Data->GetOutermost();
    Data->ClearFlags(RF_Public | RF_Standalone);
    Data->MarkAsGarbage();
    SourcePackage->SetDirtyFlag(false);
    TargetPackage->SetDirtyFlag(false);
    SourcePackage->MarkAsGarbage();
    TargetPackage->MarkAsGarbage();
    return true;
}
#endif

#endif
