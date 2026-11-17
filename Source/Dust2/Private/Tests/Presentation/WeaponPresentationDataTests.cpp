#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Presentation/WeaponPresentationData.h"
#include "Presentation/WeaponPresentationComponent.h"
#include "Weapons/WeaponRuntime.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	FWeaponPresentationProfile MakePresentation(float Marker)
	{
		FWeaponPresentationProfile Profile;
		Profile.FormalWeaponClass = AWeaponRuntime::StaticClass();
		Profile.VisualWeaponClass = AActor::StaticClass();
		Profile.FastDrawStartTime = Marker;
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationLegacySelectionTest,
	"Dust2.WeaponPresentationData.LegacySelectionAndDisabledSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPresentationLegacySelectionTest::RunTest(const FString& Parameters)
{
	const TArray<FWeaponPresentationProfile> Legacy{MakePresentation(11.0f)};
	FWeaponPresentationProfile Out;
	TestTrue(TEXT("no definition preserves class-based mapping"), UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
		nullptr, AWeaponRuntime::StaticClass(), Legacy, Out));
	TestTrue(TEXT("legacy entry copied"), Out.FastDrawStartTime == 11.0);
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
	Definition->PresentationData = Data;
	Data->PlayerProfile = MakePresentation(22.0f);
	TestFalse(TEXT("new player side defaults disabled"), Data->bEnablePlayerProfile);
	TestFalse(TEXT("new enemy side defaults disabled"), Data->bEnableEnemyProfile);
	TestTrue(TEXT("disabled side preserves legacy"), UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
		Definition, AWeaponRuntime::StaticClass(), Legacy, Out));
	TestTrue(TEXT("disabled data does not override entry"), Out.FastDrawStartTime == 11.0);
	TestFalse(TEXT("missing class cannot match legacy"), UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
		Definition, nullptr, Legacy, Out));
	TestNull(TEXT("failed lookup clears previous result"), Out.VisualWeaponClass.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationExplicitInvalidTest,
	"Dust2.WeaponPresentationData.ExplicitInvalidBlocksWrongGunFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPresentationExplicitInvalidTest::RunTest(const FString& Parameters)
{
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
	Definition->PresentationData = Data;
	Data->bEnablePlayerProfile = true;
	FWeaponPresentationProfile Out = MakePresentation(99.0f);
	const TArray<FWeaponPresentationProfile> Legacy{MakePresentation(11.0f)};
	TestFalse(TEXT("enabled missing visual must not use valid legacy"), UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
		Definition, AWeaponRuntime::StaticClass(), Legacy, Out));
	TestNull(TEXT("invalid clears output"), Out.VisualWeaponClass.Get());
	Data->PlayerProfile = MakePresentation(22.0f);
	TestTrue(TEXT("explicit asset uses the player enable switch"), UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
		Definition, AWeaponRuntime::StaticClass(), Legacy, Out));
	TestTrue(TEXT("valid data overrides a matching legacy entry"), UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
		Definition, AWeaponRuntime::StaticClass(), Legacy, Out));
	TestTrue(TEXT("new side chosen"), Out.FastDrawStartTime == 22.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationPointerSelectionTest,
	"Dust2.WeaponPresentationData.NativePointerLookupKeepsAssetAndLegacySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPresentationPointerSelectionTest::RunTest(const FString& Parameters)
{
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
	Definition->PresentationData = Data;

	UAnimMontage* AssetFire = NewObject<UAnimMontage>(Data);
	Data->bEnablePlayerProfile = true;
	Data->PlayerProfile = MakePresentation(22.0f);
	Data->PlayerProfile.WeaponFireMontage = AssetFire;

	TArray<FWeaponPresentationProfile> Legacy;
	Legacy.Add(MakePresentation(11.0f));
	Legacy.Last().FormalWeaponClass = nullptr;
	Legacy.Add(MakePresentation(12.0f));
	UAnimMontage* FirstMatchingFire = NewObject<UAnimMontage>(Data);
	Legacy.Last().WeaponFireMontage = FirstMatchingFire;
	Legacy.Add(MakePresentation(13.0f));

	const FWeaponPresentationProfile* Profile =
		UWeaponPresentationData::FindPlayerProfileWithLegacy(
			Definition, AWeaponRuntime::StaticClass(), Legacy);
	TestTrue(TEXT("enabled asset is returned without a profile copy"), Profile == &Data->PlayerProfile);
	TestTrue(TEXT("asset pointer keeps the configured fire action"),
		Profile && Profile->WeaponFireMontage.Get() == AssetFire);

	FWeaponPresentationProfile ByValue = MakePresentation(99.0f);
	TestTrue(TEXT("by-value lookup still resolves the same asset"),
		UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
			Definition, AWeaponRuntime::StaticClass(), Legacy, ByValue));
	TestTrue(TEXT("by-value lookup copies the asset fire action"), ByValue.WeaponFireMontage.Get() == AssetFire);

	Data->bEnablePlayerProfile = false;
	Profile = UWeaponPresentationData::FindPlayerProfileWithLegacy(
		Definition, AWeaponRuntime::StaticClass(), Legacy);
	TestTrue(TEXT("disabled side returns the first matching legacy element"), Profile == &Legacy[1]);
	TestTrue(TEXT("legacy pointer keeps that element's fire action"),
		Profile && Profile->WeaponFireMontage.Get() == FirstMatchingFire);
	FWeaponPresentationProfile LegacyByValue;
	TestTrue(TEXT("by-value legacy lookup selects the same element"),
		UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
			Definition, AWeaponRuntime::StaticClass(), Legacy, LegacyByValue));
	TestTrue(TEXT("by-value legacy copy carries the first matching action"),
		LegacyByValue.WeaponFireMontage.Get() == FirstMatchingFire);
	TestTrue(TEXT("no definition still resolves through the legacy array"),
		UWeaponPresentationData::FindPlayerProfileWithLegacy(
			nullptr, AWeaponRuntime::StaticClass(), Legacy) == &Legacy[1]);

	FWeaponPresentationProfile DisabledOut = MakePresentation(98.0f);
	TestTrue(TEXT("by-value definition lookup reports not configured"),
		UWeaponPresentationData::ResolvePlayerProfileForDefinition(Definition, DisabledOut)
			== EWeaponPresentationResolveResult::NotConfigured);
	TestNull(TEXT("not-configured by-value output is cleared"), DisabledOut.VisualWeaponClass.Get());
	TestNull(TEXT("missing formal class cannot match a legacy entry"),
		UWeaponPresentationData::FindPlayerProfileWithLegacy(nullptr, nullptr, Legacy));
	UWeaponDefinitionData* NoPresentation = NewObject<UWeaponDefinitionData>();
	TestTrue(TEXT("definition without presentation data keeps legacy mapping"),
		UWeaponPresentationData::FindPlayerProfileWithLegacy(
			NoPresentation, AWeaponRuntime::StaticClass(), Legacy) == &Legacy[1]);

	Data->bEnablePlayerProfile = true;
	Data->PlayerProfile.VisualWeaponClass = nullptr;
	TestNull(TEXT("explicit invalid asset blocks a matching legacy entry"),
		UWeaponPresentationData::FindPlayerProfileWithLegacy(
			Definition, AWeaponRuntime::StaticClass(), Legacy));
	FWeaponPresentationProfile InvalidOut = MakePresentation(97.0f);
	TestFalse(TEXT("by-value legacy lookup still fails for explicit invalid data"),
		UWeaponPresentationData::ResolvePlayerProfileWithLegacy(
			Definition, AWeaponRuntime::StaticClass(), Legacy, InvalidOut));
	TestNull(TEXT("blocked by-value output stays cleared"), InvalidOut.VisualWeaponClass.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationSidesTest,
	"Dust2.WeaponPresentationData.SameDefinitionKeepsPlayerEnemyAnimationsSeparate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPresentationSidesTest::RunTest(const FString& Parameters)
{
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
	Definition->PresentationData = Data;
	Data->bEnablePlayerProfile = Data->bEnableEnemyProfile = true;
	Data->PlayerProfile = MakePresentation(1.0f);
	Data->EnemyPresentation = FEnemyWeaponPresentationProfile::FromPresentationProfile(MakePresentation(2.0f));
	Data->PlayerProfile.CharacterReloadEmptyMontage = NewObject<UAnimMontage>(Data);
	Data->EnemyPresentation.CharacterReloadEmptyMontage = NewObject<UAnimMontage>(Data);
	FWeaponPresentationProfile Player, Enemy;
	TestTrue(TEXT("player resolves"), UWeaponPresentationData::ResolvePlayerProfileForDefinition(Definition, Player)
		== EWeaponPresentationResolveResult::Resolved);
	TestTrue(TEXT("enemy resolves"), UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Enemy)
		== EWeaponPresentationResolveResult::Resolved);
	TestTrue(TEXT("different character actions remain separate"), Player.CharacterReloadEmptyMontage != Enemy.CharacterReloadEmptyMontage);
	Data->bEnableEnemyProfile = false;
	TestTrue(TEXT("disabled enemy does not borrow player"), UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Enemy)
		== EWeaponPresentationResolveResult::NotConfigured);
	TestNull(TEXT("disabled enemy clears output"), Enemy.CharacterReloadEmptyMontage.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPresentationDefinitionTest,
	"Dust2.WeaponPresentationData.EnemyUsesSelectedDefinitionAndReadOnlyCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPresentationDefinitionTest::RunTest(const FString& Parameters)
{
	UDustEnemyWeaponComponent* Enemy = NewObject<UDustEnemyWeaponComponent>();
	UWeaponDefinitionData* First = NewObject<UWeaponDefinitionData>();
	UWeaponDefinitionData* Second = NewObject<UWeaponDefinitionData>();
	UWeaponPresentationData* FirstData = NewObject<UWeaponPresentationData>();
	UWeaponPresentationData* SecondData = NewObject<UWeaponPresentationData>();
	First->PresentationData = FirstData;
	Second->PresentationData = SecondData;
	FirstData->bEnableEnemyProfile = SecondData->bEnableEnemyProfile = true;
	FirstData->EnemyPresentation = FEnemyWeaponPresentationProfile::FromPresentationProfile(MakePresentation(1.0f));
	SecondData->EnemyPresentation = FEnemyWeaponPresentationProfile::FromPresentationProfile(MakePresentation(2.0f));
	FirstData->EnemyPresentation.WeaponFireMontage = NewObject<UAnimMontage>(FirstData);
	SecondData->EnemyPresentation.WeaponFireMontage = NewObject<UAnimMontage>(SecondData);
	const int32 ReserveBefore = First->InitialReserveAmmo;
	FWeaponPresentationProfile Out;
	Enemy->WeaponDefinition = First;
	TestTrue(TEXT("first definition resolves without a runtime class"), Enemy->ResolveEnemyPresentationProfile(Out));
	TestTrue(TEXT("first action carried"), Out.WeaponFireMontage == FirstData->EnemyPresentation.WeaponFireMontage);
	Out.WeaponFireMontage = nullptr;
	TestTrue(TEXT("editing copy does not change shared asset"), FirstData->EnemyPresentation.WeaponFireMontage != nullptr);
	TestEqual(TEXT("resolution never edits gameplay reserve"), First->InitialReserveAmmo, ReserveBefore);
	// Configuration-time selection only; this test does not claim runtime weapon switching support.
	Enemy->WeaponDefinition = Second;
	TestTrue(TEXT("same component resolves second definition"), Enemy->ResolveEnemyPresentationProfile(Out));
	TestTrue(TEXT("second mapping chosen"), Out.WeaponFireMontage == SecondData->EnemyPresentation.WeaponFireMontage);
	SecondData->EnemyPresentation.VisualWeaponClass = nullptr;
	TestFalse(TEXT("missing visual cannot select other gun"), Enemy->ResolveEnemyPresentationProfile(Out));
	TestNull(TEXT("failed enemy output cleared"), Out.VisualWeaponClass.Get());
	TestFalse(TEXT("cannot apply reload resources without own Pawn reload component"), Enemy->ApplyReloadPresentationFromProfile());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerPresentationActionSelectionTest,
	"Dust2.WeaponPresentationData.PlayerActionFallbackAndLastRoundPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerPresentationActionSelectionTest::RunTest(const FString& Parameters)
{
	UWorld::InitializationValues Values;
	Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("isolated presentation test world"), World)) return false;
	struct FWorldCleanup
	{
		UWorld* World;
		~FWorldCleanup() { World->DestroyWorld(false); }
	} Cleanup{World};
	AWeaponRuntime* Runtime = World->SpawnActor<AWeaponRuntime>();
	if (!TestNotNull(TEXT("native runtime"), Runtime)) return false;
	UWeaponPresentationComponent* Presentation = NewObject<UWeaponPresentationComponent>(Runtime);
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
	Definition->PresentationData = Data;
	Data->bEnablePlayerProfile = true;
	Data->PlayerProfile = MakePresentation(1.0f);
	Runtime->WeaponDefinition = Definition;
	UAnimMontage* DefaultFire = NewObject<UAnimMontage>();
	Runtime->RestoreAmmo(1, 30);
	TestTrue(TEXT("missing profile fire action preserves caller fallback"),
		Presentation->ResolveWeaponFireMontage(Runtime, DefaultFire) == DefaultFire);
	UAnimMontage* ProfileFire = NewObject<UAnimMontage>(Data);
	UAnimMontage* LastRoundFire = NewObject<UAnimMontage>(Data);
	Data->PlayerProfile.WeaponFireMontage = ProfileFire;
	Data->PlayerProfile.EmptyFireWeaponMontage = LastRoundFire;
	const TArray<FWeaponPresentationProfile> NoLegacyProfiles;
	const FWeaponPresentationProfile* NativeFireProfile =
		UWeaponPresentationData::FindPlayerProfileWithLegacy(
			Runtime->WeaponDefinition.Get(), Runtime->GetClass(), NoLegacyProfiles);
	TestTrue(TEXT("native pointer selects the same asset profile used for fire"), NativeFireProfile == &Data->PlayerProfile);
	TestTrue(TEXT("normal fire action comes from the pointer profile"),
		NativeFireProfile && NativeFireProfile->WeaponFireMontage.Get() == ProfileFire);
	TestTrue(TEXT("normal fire from data"), Presentation->ResolveWeaponFireMontage(Runtime, DefaultFire) == ProfileFire);
	Runtime->RestoreAmmo(0, 30);
	TestTrue(TEXT("last successful round profile retains precedence"), Presentation->ResolveWeaponFireMontage(Runtime, DefaultFire) == LastRoundFire);
	TestTrue(TEXT("last-round action comes from the pointer profile"),
		NativeFireProfile && NativeFireProfile->EmptyFireWeaponMontage.Get() == LastRoundFire);
	FWeaponReloadVariant Reload;
	TestTrue(TEXT("standard reload resolves"), Presentation->ResolveStandardReloadPresentation(Runtime, Reload));
	TestNull(TEXT("missing empty character action stays null without author fallback"), Reload.CharacterMontage.Get());
	Data->PlayerProfile.CharacterReloadEmptyMontage = NewObject<UAnimMontage>(Data);
	Data->PlayerProfile.ReloadEmptyMontage = NewObject<UAnimMontage>(Data);
	TestTrue(TEXT("configured empty actions resolve"), Presentation->ResolveStandardReloadPresentation(Runtime, Reload));
	TestTrue(TEXT("character empty action from profile"), Reload.CharacterMontage == Data->PlayerProfile.CharacterReloadEmptyMontage);
	TestTrue(TEXT("weapon empty action still from profile"), Reload.WeaponMontage == Data->PlayerProfile.ReloadEmptyMontage);
	Runtime->RestoreAmmo(1, 30);
	TestTrue(TEXT("tactical missing action resolves"), Presentation->ResolveStandardReloadPresentation(Runtime, Reload));
	TestNull(TEXT("missing tactical character action stays null without author fallback"), Reload.CharacterMontage.Get());
	Data->PlayerProfile.CharacterReloadTacticalMontage = NewObject<UAnimMontage>(Data);
	TestTrue(TEXT("configured tactical action resolves"), Presentation->ResolveStandardReloadPresentation(Runtime, Reload));
	TestTrue(TEXT("tactical profile action chosen"), Reload.CharacterMontage == Data->PlayerProfile.CharacterReloadTacticalMontage);
	Data->PlayerProfile.ReloadVariants.Add(FWeaponReloadVariant{});
	Data->PlayerProfile.bUseCountBasedReload = true;
	TestFalse(TEXT("per-count variants keep standard reload path disabled"), Presentation->ResolveStandardReloadPresentation(Runtime, Reload));
	TestNull(TEXT("failed variant selection clears output"), Reload.CharacterMontage.Get());

	// Exercise the real component resolver, including the compatibility output used by Blueprint.
	Definition->MagazineCapacity = 8;
	Data->PlayerProfile.ReloadVariants.SetNum(8);
	for (FWeaponReloadVariant& Variant : Data->PlayerProfile.ReloadVariants)
	{
		Variant.CharacterMontage = NewObject<UAnimMontage>(Data);
		Variant.WeaponMontage = NewObject<UAnimMontage>(Data);
	}
	int32 Planned = 99, Request = 99;
	Runtime->RestoreAmmo(6, 20);
	TestTrue(TEXT("six of eight selects two-round reload"), Presentation->ResolveVariantReloadPresentation(Runtime, 8, Reload, Planned, Request));
	TestEqual(TEXT("actual missing count"), Planned, 2);
	TestTrue(TEXT("array index one means two rounds"), Reload.WeaponMontage == Data->PlayerProfile.ReloadVariants[1].WeaponMontage);
	Runtime->RestoreAmmo(0, 1);
	TestTrue(TEXT("limited reserve selects actual one round"), Presentation->ResolveVariantReloadPresentation(Runtime, 8, Reload, Planned, Request));
	TestEqual(TEXT("reserve limits planned count"), Planned, 1);
	TestTrue(TEXT("one-round character action"), Reload.CharacterMontage == Data->PlayerProfile.ReloadVariants[0].CharacterMontage);
	Runtime->RestoreAmmo(0, 20);
	TestTrue(TEXT("empty eight-round weapon selects eighth entry"), Presentation->ResolveVariantReloadPresentation(Runtime, 8, Reload, Planned, Request));
	TestEqual(TEXT("full refill planned count"), Planned, 8);
	TestTrue(TEXT("index seven means eight rounds"), Reload.WeaponMontage == Data->PlayerProfile.ReloadVariants[7].WeaponMontage);
	Data->PlayerProfile.bUseCountBasedReload = false;
	TestFalse(TEXT("off switch refuses count resolver despite cached entries"), Presentation->ResolveVariantReloadPresentation(Runtime, 8, Reload, Planned, Request));
	TestEqual(TEXT("failed selection clears count"), Planned, 0);
	TestEqual(TEXT("failed selection clears request"), Request, 0);
	FWeaponPresentationProfile Effective;
	TestTrue(TEXT("profile lookup still valid"), Presentation->FindWeaponPresentationProfile(Runtime, Effective));
	TestEqual(TEXT("legacy Blueprint array-length branch sees disabled variants"), Effective.ReloadVariants.Num(), 0);
	TestEqual(TEXT("shared asset retains all configured entries"), Data->PlayerProfile.ReloadVariants.Num(), 8);
	TestTrue(TEXT("native pointer still targets the shared asset profile"), NativeFireProfile == &Data->PlayerProfile);
	TestEqual(TEXT("native pointer keeps configured variants while the Blueprint output clears its copy"),
		NativeFireProfile ? NativeFireProfile->ReloadVariants.Num() : -1, 8);
	TestTrue(TEXT("off switch returns standard reload path"), Presentation->ResolveStandardReloadPresentation(Runtime, Reload));
	Data->PlayerProfile.bUseCountBasedReload = true;
	Data->PlayerProfile.ReloadVariants.SetNum(2);
	TestFalse(TEXT("missing selected entry fails instead of picking another action"), Presentation->ResolveVariantReloadPresentation(Runtime, 8, Reload, Planned, Request));
	TestNull(TEXT("missing entry clears previous action"), Reload.WeaponMontage.Get());
	TestFalse(TEXT("enabled missing variant cannot silently use standard animation"), Presentation->ResolveStandardReloadPresentation(Runtime, Reload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPresentationMigrationTest,
	"Dust2.WeaponPresentationData.CompactEnemyMigratesLegacyWithoutOverwritingNewData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPresentationMigrationTest::RunTest(const FString& Parameters)
{
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
	Data->bEnableEnemyProfile = true;
	Data->PlayerProfile = MakePresentation(77.0f);
	// This is the old serialized property's exact name/type, kept hidden for asset loading.
	Data->EnemyProfile = MakePresentation(12.0f);
	Data->EnemyProfile.WeaponFireMontage = NewObject<UAnimMontage>(Data);
	Data->EnemyProfile.CharacterFireMontage = NewObject<UAnimMontage>(Data);
	Data->EnemyProfile.CharacterReloadEmptyMontage = NewObject<UAnimMontage>(Data);
	Data->EnemyProfile.ReloadEmptyMontage = NewObject<UAnimMontage>(Data);
	USoundWave* LegacySound = NewObject<USoundWave>(Data);
	Data->EnemyProfile.ReloadEmptySound = LegacySound;
	Data->EnemyProfile.DrawCharacterMontage = NewObject<UAnimMontage>(Data);
	// Legacy hidden payload is player-shaped; migration keeps only the fixed enemy fields.
	Data->EnemyProfile.bUseCountBasedReload = true;
	Data->EnemyProfile.ReloadVariants.Add(FWeaponReloadVariant{});
	const FWeaponPresentationProfile Legacy = Data->EnemyProfile;
	Data->MigrateLegacyEnemyPresentation();
	FWeaponPresentationProfile Out = Data->EnemyPresentation.ToPresentationProfile();
	TestTrue(TEXT("visual class retained"), Out.VisualWeaponClass == Legacy.VisualWeaponClass);
	TestTrue(TEXT("fire retained"), Out.WeaponFireMontage == Legacy.WeaponFireMontage);
	TestTrue(TEXT("character fire retained"), Out.CharacterFireMontage == Legacy.CharacterFireMontage);
	TestTrue(TEXT("character empty reload retained"), Out.CharacterReloadEmptyMontage == Legacy.CharacterReloadEmptyMontage);
	TestTrue(TEXT("weapon empty reload retained"), Out.ReloadEmptyMontage == Legacy.ReloadEmptyMontage);
	TestTrue(TEXT("empty reload sound retained"), Out.ReloadEmptySound == LegacySound);
	TestFalse(TEXT("legacy count switch is not migrated to the enemy profile"), Out.bUseCountBasedReload);
	TestEqual(TEXT("legacy count variants are not migrated to the enemy profile"), Out.ReloadVariants.Num(), 0);
	TestNull(TEXT("player-only draw excluded from enemy view"), Out.DrawCharacterMontage.Get());
	TestNull(TEXT("old compatibility payload consumed"), Data->EnemyProfile.VisualWeaponClass.Get());
	TestTrue(TEXT("player side untouched"), Data->PlayerProfile.FastDrawStartTime == 77.0f);
	Data->MigrateLegacyEnemyPresentation();
	TestTrue(TEXT("repeat migration keeps compact data"), Data->EnemyPresentation.WeaponFireMontage == Legacy.WeaponFireMontage);
	// New data has priority over a stale old payload, including a deliberate partial setup.
	Data->EnemyPresentation = FEnemyWeaponPresentationProfile{};
	UAnimMontage* ConfiguredFire = NewObject<UAnimMontage>(Data);
	Data->EnemyPresentation.CharacterFireMontage = ConfiguredFire;
	Data->EnemyProfile = MakePresentation(99.0f);
	Data->MigrateLegacyEnemyPresentation();
	TestTrue(TEXT("existing compact data not overwritten"), Data->EnemyPresentation.CharacterFireMontage == ConfiguredFire);
	Data->EnemyPresentation = FEnemyWeaponPresentationProfile{};
	Data->MigrateLegacyEnemyPresentation();
	TestTrue(TEXT("deliberately clearing compact data does not resurrect consumed legacy"), Data->EnemyPresentation.IsDefault());
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	Definition->PresentationData = Data;
	TestTrue(TEXT("cleared enemy entry is invalid"), UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Out)
		== EWeaponPresentationResolveResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCountReloadVersionMigrationTest,
	"Dust2.WeaponPresentationData.CountReloadSwitchDefaultsAndLegacyVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCountReloadVersionMigrationTest::RunTest(const FString& Parameters)
{
	FWeaponPresentationProfile Fresh;
	TestFalse(TEXT("new profiles default off"), Fresh.bUseCountBasedReload);
	TArray<uint8> OldBytes;
	FMemoryReader OldReader(OldBytes);
	Fresh.ReloadVariants.Add(FWeaponReloadVariant{});
	Fresh.PostSerialize(OldReader);
	TestTrue(TEXT("old unversioned profile with variants retains count-based behavior"), Fresh.bUseCountBasedReload);
	FWeaponPresentationProfile OldStandard;
	OldStandard.PostSerialize(OldReader);
	TestFalse(TEXT("old empty array stays standard"), OldStandard.bUseCountBasedReload);
	// Modern saved profiles may deliberately keep cached variants while the switch is off.
	TArray<uint8> NewBytes;
	FMemoryWriter NewWriter(NewBytes);
	TestFalse(TEXT("serialize hook retains ordinary tagged serialization"), Fresh.Serialize(NewWriter));
	FMemoryReader NewReader(NewBytes);
	NewReader.SetCustomVersions(NewWriter.GetCustomVersions());
	Fresh.bUseCountBasedReload = false;
	Fresh.PostSerialize(NewReader);
	TestFalse(TEXT("modern explicit off is not re-enabled by cached variants"), Fresh.bUseCountBasedReload);
	TestEqual(TEXT("compatibility never deletes configured actions"), Fresh.ReloadVariants.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFixedPresentationFieldsTest,
	"Dust2.WeaponPresentationData.EnemyKeepsCompactFieldsAndDoesNotBorrowPlayerCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFixedPresentationFieldsTest::RunTest(const FString& Parameters)
{
	// Compact enemy config contains only the visual class, native viewmodel, two fire actions,
	// two empty-reload actions and the optional reload sound.
	int32 EnemyFieldCount = 0;
	for (TFieldIterator<FProperty> It(FEnemyWeaponPresentationProfile::StaticStruct()); It; ++It)
		++EnemyFieldCount;
	TestEqual(TEXT("enemy profile is the compact 7-field config"), EnemyFieldCount, 7);

	FEnemyWeaponPresentationProfile Enemy;
	TestTrue(TEXT("empty enemy profile stays default"), Enemy.IsDefault());
	Enemy.CharacterFireMontage = NewObject<UAnimMontage>();
	TestFalse(TEXT("character fire alone makes enemy config non-default"), Enemy.IsDefault());
	FWeaponPresentationProfile FireProfile = Enemy.ToPresentationProfile();
	TestTrue(TEXT("enemy output carries character fire"), FireProfile.CharacterFireMontage == Enemy.CharacterFireMontage);
	TestTrue(TEXT("enemy character fire round-trips"),
		FEnemyWeaponPresentationProfile::FromPresentationProfile(FireProfile).CharacterFireMontage == Enemy.CharacterFireMontage);
	Enemy.CharacterFireMontage = nullptr;
	TestNull(TEXT("new empty reload sound defaults to none"), Enemy.ReloadEmptySound.Get());
	USoundWave* EnemySound = NewObject<USoundWave>();
	Enemy.ReloadEmptySound = EnemySound;
	TestFalse(TEXT("a sound alone makes the compact profile non-default"), Enemy.IsDefault());
	Enemy.ReloadEmptySound = nullptr;
	Enemy.CharacterReloadEmptyMontage = NewObject<UAnimMontage>();
	TestFalse(TEXT("a fixed reload action makes the compact profile non-default"), Enemy.IsDefault());

	FWeaponPresentationProfile Profile = Enemy.ToPresentationProfile();
	TestTrue(TEXT("ToPresentationProfile carries the fixed character action"),
		Profile.CharacterReloadEmptyMontage == Enemy.CharacterReloadEmptyMontage);
	Enemy.ReloadEmptySound = EnemySound;
	Profile = Enemy.ToPresentationProfile();
	TestTrue(TEXT("ToPresentationProfile carries the optional empty reload sound"),
		Profile.ReloadEmptySound == EnemySound);
	FEnemyWeaponPresentationProfile SoundRoundTrip = FEnemyWeaponPresentationProfile::FromPresentationProfile(Profile);
	TestTrue(TEXT("FromPresentationProfile carries the optional empty reload sound"),
		SoundRoundTrip.ReloadEmptySound == EnemySound);
	TestFalse(TEXT("enemy output never enables the player count switch"), Profile.bUseCountBasedReload);
	TestEqual(TEXT("enemy output never carries player count variants"), Profile.ReloadVariants.Num(), 0);

	FWeaponPresentationProfile PlayerShaped = MakePresentation(3.0f);
	PlayerShaped.bUseCountBasedReload = true;
	PlayerShaped.ReloadVariants.Add(FWeaponReloadVariant{});
	PlayerShaped.ReloadVariants.Add(FWeaponReloadVariant{});
	FEnemyWeaponPresentationProfile RoundTrip = FEnemyWeaponPresentationProfile::FromPresentationProfile(PlayerShaped);
	FWeaponPresentationProfile RoundOut = RoundTrip.ToPresentationProfile();
	TestFalse(TEXT("FromPresentationProfile ignores the player count switch"), RoundOut.bUseCountBasedReload);
	TestEqual(TEXT("FromPresentationProfile ignores player count variants"), RoundOut.ReloadVariants.Num(), 0);
	TestTrue(TEXT("fixed player-shaped fields still round-trip"), RoundOut.VisualWeaponClass == PlayerShaped.VisualWeaponClass);

	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	UWeaponPresentationData* Data = NewObject<UWeaponPresentationData>();
	Definition->PresentationData = Data;
	Data->bEnableEnemyProfile = true;
	Data->EnemyPresentation = Enemy;
	Data->EnemyPresentation.VisualWeaponClass = AActor::StaticClass();
	FWeaponPresentationProfile Out;
	TestTrue(TEXT("enemy profile resolves"), UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Out)
		== EWeaponPresentationResolveResult::Resolved);
	TestFalse(TEXT("resolver output never enables the enemy count switch"), Out.bUseCountBasedReload);
	TestEqual(TEXT("resolver output never carries count variants"), Out.ReloadVariants.Num(), 0);

	// A configured player count reload must never leak into the enemy output.
	Data->bEnablePlayerProfile = true;
	Data->PlayerProfile = MakePresentation(3.0f);
	Data->PlayerProfile.bUseCountBasedReload = true;
	Data->PlayerProfile.ReloadVariants.Add(FWeaponReloadVariant{});
	Data->PlayerProfile.ReloadVariants.Add(FWeaponReloadVariant{});
	TestTrue(TEXT("enemy still resolves alongside a player count profile"),
		UWeaponPresentationData::ResolveEnemyProfileForDefinition(Definition, Out) == EWeaponPresentationResolveResult::Resolved);
	TestFalse(TEXT("enemy never borrows the player count switch"), Out.bUseCountBasedReload);
	TestEqual(TEXT("enemy never borrows the player count array"), Out.ReloadVariants.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationRemovedAuthorFieldsTest,
	"Dust2.WeaponPresentationData.RemovedAuthorConfigurationFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresentationRemovedAuthorFieldsTest::RunTest(const FString& Parameters)
{
	for (const UScriptStruct* Struct : {FWeaponPresentationProfile::StaticStruct(), FEnemyWeaponPresentationProfile::StaticStruct()})
	{
		for (const FName Name : {FName(TEXT("AuthorAnimationSettings")), FName(TEXT("VisualWeaponAttachSocket")),
			FName(TEXT("VisualWeaponRelativeTransform")), FName(TEXT("bUseTacticalPresentation"))})
		{
			TestNull(*FString::Printf(TEXT("%s no longer exposes %s"), *Struct->GetName(), *Name.ToString()),
				FindFProperty<FProperty>(Struct, Name));
		}
	}
	return true;
}

#endif
