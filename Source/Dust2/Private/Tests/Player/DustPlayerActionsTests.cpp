#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "Camera/CameraComponent.h"
#include "Components/ChildActorComponent.h"
#include "UObject/UnrealType.h"

#include "Presentation/CameraRecoilRecoveryComponent.h"
#include "Player/Dust2PlayerCharacter.h"
#include "Weapons/FireMode.h"
#include "../../Weapons/WeaponBallisticsData.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Inventory/WeaponInventoryComponent.h"
#include "Presentation/WeaponPresentationComponent.h"
#include "Weapons/WeaponRuntime.h"

// Native-only friend access for the real fallback callbacks, serials and the transient
// semi-automatic buffer. The tests still drive production interfaces; this only lets
// them observe the pending state and replay a stale timer generation.
struct FDustPlayerActionsTestAccess
{
	static uint64 ActionsSerial(const ADust2PlayerCharacter* Player)
	{
		return Player->NativeActionsSerial;
	}

	static void TryBufferAfterDispatch(ADust2PlayerCharacter* Player,
		AWeaponRuntime* Weapon, uint64 AttemptSerial)
	{
		Player->TryBufferSemiAutoShot(Weapon, AttemptSerial);
	}

	static uint64 ReloadFallbackSerial(const ADust2PlayerCharacter* Player)
	{
		return Player->ReloadFallbackTimerSerial;
	}

	static void FireReloadFallback(ADust2PlayerCharacter* Player, uint64 Serial,
		AWeaponRuntime* Weapon, int32 RequestId)
	{
		Player->HandleReloadFallbackElapsed(Serial,
			TWeakObjectPtr<AWeaponRuntime>(Weapon), RequestId);
	}

	static bool HasPendingSemiAutoBuffer(const ADust2PlayerCharacter* Player)
	{
		return Player->bPendingSemiAutoBuffer;
	}

	static uint64 SemiAutoBufferGeneration(const ADust2PlayerCharacter* Player)
	{
		return Player->PendingSemiAutoBufferGeneration;
	}

	static float SemiAutoBufferDeadline(const ADust2PlayerCharacter* Player)
	{
		return Player->PendingSemiAutoBufferDeadline;
	}

	static void FireSemiAutoBuffer(ADust2PlayerCharacter* Player, uint64 Generation,
		AWeaponRuntime* Weapon)
	{
		Player->HandleSemiAutoBufferElapsed(Generation,
			TWeakObjectPtr<AWeaponRuntime>(Weapon));
	}

	static void ShutdownPlayerActions(ADust2PlayerCharacter* Player)
	{
		Player->ShutdownPlayerActionsNative();
	}
};

namespace
{
	struct FDustPlayerActionsWorld
	{
		UWorld* World = nullptr;
		ADust2PlayerCharacter* Player = nullptr;
		UWeaponInventoryComponent* Inventory = nullptr;
		UWeaponPresentationComponent* Presentation = nullptr;
		UWeaponPresentationComponent* Presentation2 = nullptr;
		UCameraComponent* Camera = nullptr;
		UCameraComponent* Camera2 = nullptr;
		AWeaponRuntime* Weapon1 = nullptr;
		AWeaponRuntime* Weapon2 = nullptr;
		bool bReady = false;

		explicit FDustPlayerActionsWorld(bool bEnableActions = true)
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
				nullptr, false, ERHIFeatureLevel::Num, &Values);
			if (!World || !GEngine) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			Player = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Player) return;
			Player->RestoreHealth(100.0f);

			Inventory = NewObject<UWeaponInventoryComponent>(Player);
			Presentation = NewObject<UWeaponPresentationComponent>(Player);
			Presentation2 = NewObject<UWeaponPresentationComponent>(Player);
			UCameraRecoilRecoveryComponent* Recoil =
				NewObject<UCameraRecoilRecoveryComponent>(Player);
			Camera = NewObject<UCameraComponent>(Player);
			Camera2 = NewObject<UCameraComponent>(Player);
			Inventory->RegisterComponent();
			Presentation->RegisterComponent();
			Presentation2->RegisterComponent();
			Recoil->RegisterComponent();
			Camera->RegisterComponent();
			Camera2->RegisterComponent();
			Camera->SetWorldRotation(FRotator::ZeroRotator);
			Camera2->SetWorldRotation(FRotator(5.0f, 0.0f, 0.0f));

			UChildActorComponent* Slot1 = NewObject<UChildActorComponent>(Player);
			UChildActorComponent* Slot2 = NewObject<UChildActorComponent>(Player);
			Slot1->SetChildActorClass(AWeaponRuntime::StaticClass());
			Slot2->SetChildActorClass(AWeaponRuntime::StaticClass());
			Slot1->RegisterComponent();
			Slot2->RegisterComponent();
			Weapon1 = Cast<AWeaponRuntime>(Slot1->GetChildActor());
			Weapon2 = Cast<AWeaponRuntime>(Slot2->GetChildActor());
			if (!Weapon1 || !Weapon2) return;

			ConfigureWeapon(Weapon1, EFireMode::SemiAutomatic);
			ConfigureWeapon(Weapon2, EFireMode::SemiAutomatic);
			bReady = Player->InitializePlayerInventoryBridge(
				Inventory, Slot1, Slot2, Weapon1, Recoil);
			Presentation->SetWeaponForPresentation(Weapon1);
			Presentation2->SetWeaponForPresentation(Weapon1);
			if (bEnableActions)
			{
				bReady = bReady &&
					Player->InitializePlayerActionsNative(Presentation, Camera);
			}
		}

		~FDustPlayerActionsWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
			}
		}

		static void ConfigureWeapon(AWeaponRuntime* Weapon, EFireMode Mode)
		{
			UWeaponDefinitionData* Definition =
				NewObject<UWeaponDefinitionData>(Weapon);
			Definition->FireInterval = 0.1f;
			Definition->BurstShotInterval = 0.05f;
			Definition->BurstRecoveryInterval = 0.2f;
			Definition->MagazineCapacity = 30;
			Definition->InitialMagazineAmmo = 30;
			Definition->InitialReserveAmmo = 90;
			Definition->DefaultFireMode = Mode;
			Definition->AllowedFireModes = {
				EFireMode::SemiAutomatic,
				EFireMode::FullAutomatic,
				EFireMode::Burst };
			UWeaponBallisticsData* Ballistics =
				NewObject<UWeaponBallisticsData>(Definition);
			Ballistics->DamagePerShot = 10.0f;
			Ballistics->ArmorPenetration = 0.0f;
			Ballistics->HeadDamageMultiplier = 2.0f;
			Ballistics->TraceDistance = 2000.0f;
			Definition->BallisticsData = Ballistics;
			Weapon->WeaponDefinition = Definition;
			Weapon->ApplyDefinitionSettings();
			Weapon->RestoreAmmo(30, 90);
			Weapon->RestoreFireMode(Mode);
		}

		void Tick(float DeltaSeconds = 0.01f)
		{
			if (!World) return;
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
		}

		static void SetMode(AWeaponRuntime* Weapon, EFireMode Mode)
		{
			Weapon->RestoreFireMode(Mode);
		}

		static void SetAmmo(AWeaponRuntime* Weapon, int32 Current, int32 Reserve)
		{
			Weapon->RestoreAmmo(Current, Reserve);
		}

		static void SetNextFireTime(AWeaponRuntime* Weapon, float Value)
		{
			if (FFloatProperty* Property = FindFProperty<FFloatProperty>(
				Weapon->GetClass(), TEXT("NextFireTime")))
			{
				Property->SetPropertyValue_InContainer(Weapon, Value);
			}
		}

		static float GetNextFireTime(const AWeaponRuntime* Weapon)
		{
			const FFloatProperty* Property = FindFProperty<FFloatProperty>(
				Weapon->GetClass(), TEXT("NextFireTime"));
			return Property ? Property->GetPropertyValue_InContainer(Weapon) : -1.0f;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsInitialPresentationTest,
	"Dust2.PlayerActions.InitializeBindsEquippedWeaponPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsInitialPresentationTest::RunTest(const FString&)
{
	FDustPlayerActionsWorld F(false);
	if (!TestTrue(TEXT("inventory ready before action initialization"), F.bReady)) return false;

	F.Presentation->SetWeaponForPresentation(nullptr);
	TestFalse(TEXT("equipped weapon has no presentation binding yet"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon1, F.Weapon1));
	if (!TestTrue(TEXT("action initialization succeeds"),
		F.Player->InitializePlayerActionsNative(F.Presentation, F.Camera))) return false;

	TestTrue(TEXT("action initialization binds the weapon already equipped in inventory"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon1, F.Weapon1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsDisabledTest,
	"Dust2.PlayerActions.DisabledNoSideEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsDisabledTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("InitializePlayerActionsNative rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F(false);
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	TestFalse(TEXT("actions stay dormant without explicit init"),
		F.Player->IsPlayerActionsNativeEnabled());
	TestFalse(TEXT("invalid presentation rejected"),
		F.Player->InitializePlayerActionsNative(nullptr, F.Camera));
	TestFalse(TEXT("invalid camera rejected"),
		F.Player->InitializePlayerActionsNative(F.Presentation, nullptr));
	TestFalse(TEXT("failed init leaves actions dormant"),
		F.Player->IsPlayerActionsNativeEnabled());

	const int32 AmmoBefore = F.Weapon1->GetCurrentAmmo();
	const EFireMode ModeBefore = F.Weapon1->GetFireMode();
	TestFalse(TEXT("new entry is dormant before opt-in"),
		F.Player->NativePlayerFirePressed());
	TestFalse(TEXT("disabled entry does not latch held input"), F.Player->IsSprintBlockedForFire());
	F.Player->NativePlayerFireTriggered();
	F.Player->NativePlayerFireReleased();
	F.Player->NativePlayerCycleFireMode();
	TestEqual(TEXT("disabled press consumes nothing"),
		F.Weapon1->GetCurrentAmmo(), AmmoBefore);
	TestTrue(TEXT("disabled cycle keeps mode"),
		F.Weapon1->GetFireMode() == ModeBefore);
	TestFalse(TEXT("disabled reload rejected"),
		F.Player->NativePlayerStartReload(0.1f));
	TestFalse(TEXT("weapon is not reloading"), F.Weapon1->IsReloading());
	TestFalse(TEXT("disabled set-driven rejected"),
		F.Player->NativePlayerSetReloadAnimationDriven(F.Weapon1, 1, true));
	TestFalse(TEXT("disabled complete rejected"),
		F.Player->NativePlayerCompleteReload(F.Weapon1, 1));
	F.Player->NativePlayerCancelReload();
	F.Player->NativePlayerCancelActions();
	TestEqual(TEXT("cancel is a no-op before opt-in"),
		F.Weapon1->GetCurrentAmmo(), AmmoBefore);
	TestEqual(TEXT("no native committed shot before opt-in"),
		F.Player->GetNativeCommittedShotCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiTest,
	"Dust2.PlayerActions.SingleShotDryFireAndCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 2, 10);

	TestTrue(TEXT("semi press accepted"), F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("one bullet consumed"), F.Weapon1->GetCurrentAmmo(), 1);
	TestEqual(TEXT("one native commit"), F.Player->GetNativeCommittedShotCount(), 1);
	TestEqual(TEXT("one resolve notification"),
		F.Player->GetNativeShotResolvedNotifiedCount(), 1);
	TestEqual(TEXT("no dry fire on a real shot"),
		F.Player->GetNativeDryFireCount(), 0);

	TestFalse(TEXT("duplicate Started rejected"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("duplicate Started cannot double fire"),
		F.Weapon1->GetCurrentAmmo(), 1);
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("semi ignores Triggered"), F.Weapon1->GetCurrentAmmo(), 1);
	F.Player->NativePlayerFireReleased();

	TestTrue(TEXT("repress accepted"), F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("cooldown rejection consumes nothing"),
		F.Weapon1->GetCurrentAmmo(), 1);
	TestEqual(TEXT("cooldown rejection is not a commit"),
		F.Player->GetNativeCommittedShotCount(), 1);
	TestEqual(TEXT("cooldown is not buffered as dry fire"),
		F.Player->GetNativeDryFireCount(), 0);
	F.Player->NativePlayerFireReleased();

	F.Tick(0.12f);
	TestTrue(TEXT("press after cooldown accepted"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("last bullet consumed"), F.Weapon1->GetCurrentAmmo(), 0);
	TestEqual(TEXT("two native commits"),
		F.Player->GetNativeCommittedShotCount(), 2);
	F.Player->NativePlayerFireReleased();

	F.Tick(0.12f);
	TestTrue(TEXT("empty press is still a valid press"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("empty attempt is not a commit"),
		F.Player->GetNativeCommittedShotCount(), 2);
	TestEqual(TEXT("first allowed empty request notifies once"),
		F.Player->GetNativeDryFireCount(), 1);
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("semi Triggered cannot duplicate dry fire"),
		F.Player->GetNativeDryFireCount(), 1);
	F.Player->NativePlayerFireReleased();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsAutoTest,
	"Dust2.PlayerActions.AutomaticFirstTriggerAndDryFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsAutoTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::FullAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 6, 10);

	TestTrue(TEXT("automatic press accepted"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("Started consumes one shot"), F.Weapon1->GetCurrentAmmo(), 5);
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("same-frame Triggered is suppressed once"),
		F.Weapon1->GetCurrentAmmo(), 5);
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("second same-frame Triggered hits runtime cooldown"),
		F.Weapon1->GetCurrentAmmo(), 5);

	F.Tick(0.11f);
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("held Triggered fires after cooldown"),
		F.Weapon1->GetCurrentAmmo(), 4);
	F.Player->NativePlayerFireReleased();
	F.Tick(0.11f);

	TestTrue(TEXT("repress accepted"), F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("new press consumes one"), F.Weapon1->GetCurrentAmmo(), 3);
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("new press keeps the one-shot suppression"),
		F.Weapon1->GetCurrentAmmo(), 3);
	F.Tick(0.11f);
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("Triggered resumes on the next cooldown window"),
		F.Weapon1->GetCurrentAmmo(), 2);
	F.Player->NativePlayerFireReleased();

	// Empty automatic fire: the Started dry fire is the only sound for this press.
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 0, 10);
	F.Tick(0.11f);
	TestTrue(TEXT("empty automatic press accepted"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("Started notifies one dry fire"),
		F.Player->GetNativeDryFireCount(), 1);
	F.Player->NativePlayerFireTriggered();
	F.Player->NativePlayerFireTriggered();
	TestEqual(TEXT("same-frame Triggered never repeats the dry-fire sound"),
		F.Player->GetNativeDryFireCount(), 1);
	TestEqual(TEXT("empty attempts never commit"),
		F.Player->GetNativeCommittedShotCount(), 4);
	F.Player->NativePlayerFireReleased();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsPendingFireTest,
	"Dust2.PlayerActions.ReleaseCancelsSprintPending",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsPendingFireTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 3, 10);

	// Close the explicit sprint-pose exit gate without putting the actor in Run.
	F.Player->ReportSprintExitPresentationReady(false);
	TestFalse(TEXT("pending press is gated by the sprint pose"),
		F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("one press is cached"), F.Player->HasPendingSprintFire());
	F.Player->NativePlayerFireReleased();
	TestFalse(TEXT("release drops the cached press"),
		F.Player->HasPendingSprintFire());
	F.Tick(0.1f);
	TestEqual(TEXT("canceled pending press never fires"),
		F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("canceled pending press is not a commit"),
		F.Player->GetNativeCommittedShotCount(), 0);

	// The enabled flush hands the pending press to the native dispatcher.
	TestFalse(TEXT("second pending press accepted"),
		F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("second press cached"), F.Player->HasPendingSprintFire());
	F.Player->ReportSprintExitPresentationReady(true);
	F.Tick(0.02f);
	TestFalse(TEXT("gate exit consumes the pending press"),
		F.Player->HasPendingSprintFire());
	TestEqual(TEXT("enabled flush fires natively"),
		F.Weapon1->GetCurrentAmmo(), 2);
	TestEqual(TEXT("one committed shot after flush"),
		F.Player->GetNativeCommittedShotCount(), 1);
	F.Player->NativePlayerFireReleased();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsBurstTest,
	"Dust2.PlayerActions.BurstGroupCountingAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsBurstTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::Burst);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 10, 20);

	TestTrue(TEXT("burst press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("opening shot starts the group"),
		F.Player->IsBurstGroupActiveNative());
	TestEqual(TEXT("opening shot counted"),
		F.Player->GetBurstCommittedShotsNative(), 1);
	F.Player->NativePlayerFireReleased();
	F.Tick(0.06f);
	TestEqual(TEXT("release still lets the group continue"),
		F.Player->GetBurstCommittedShotsNative(), 2);
	const int32 AmmoDuringGroup = F.Weapon1->GetCurrentAmmo();
	TestTrue(TEXT("another press is received while the group runs"), F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("repeat press cannot insert an extra shot"), F.Weapon1->GetCurrentAmmo(), AmmoDuringGroup);
	TestEqual(TEXT("repeat press cannot restart the group counter"), F.Player->GetBurstCommittedShotsNative(), 2);
	F.Player->NativePlayerFireReleased();

	// Force a cooldown on the weapon: that attempt must not advance the 3-shot count.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.1f);
	F.Tick(0.06f);
	TestEqual(TEXT("cooldown rejection is not counted"),
		F.Player->GetBurstCommittedShotsNative(), 2);
	TestTrue(TEXT("group survives a cooldown retry"),
		F.Player->IsBurstGroupActiveNative());

	F.Tick(0.06f);
	TestFalse(TEXT("third committed shot ends the group"),
		F.Player->IsBurstGroupActiveNative());
	TestEqual(TEXT("three successful commits total"),
		F.Player->GetNativeCommittedShotCount(), 3);
	TestEqual(TEXT("resolved misses still counted as committed shots"),
		F.Player->GetNativeShotResolvedNotifiedCount(), 3);

	F.Tick(0.1f);
	TestEqual(TEXT("no fourth shot without input"),
		F.Player->GetNativeCommittedShotCount(), 3);
	TestTrue(TEXT("press during recovery is accepted but rejected by runtime"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("recovery cooldown consumes nothing"),
		F.Player->GetNativeCommittedShotCount(), 3);
	F.Player->NativePlayerFireReleased();

	F.Tick(0.15f);
	TestTrue(TEXT("press after recovery accepted"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("new group starts after recovery"),
		F.Player->GetNativeCommittedShotCount(), 4);
	TestTrue(TEXT("new burst group active"),
		F.Player->IsBurstGroupActiveNative());
	F.Player->NativePlayerFireReleased();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSwitchTest,
	"Dust2.PlayerActions.SwitchCancelsBurstAndReloadTimers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSwitchTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::Burst);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 10, 20);
	TestTrue(TEXT("press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("burst group active"), F.Player->IsBurstGroupActiveNative());
	F.Player->NativePlayerFireReleased();

	TestTrue(TEXT("switch accepted"), F.Inventory->TrySwitchWeaponNative());
	TestFalse(TEXT("switch cancels the burst group"),
		F.Player->IsBurstGroupActiveNative());
	const int32 ShotsAfterSwitch = F.Player->GetNativeCommittedShotCount();
	F.Tick(0.3f);
	TestEqual(TEXT("canceled burst timer never fires again"),
		F.Player->GetNativeCommittedShotCount(), ShotsAfterSwitch);

	TestTrue(TEXT("switch back accepted"), F.Inventory->TrySwitchWeaponNative());
	F.Presentation->SetWeaponForPresentation(
		F.Inventory->GetCurrentWeaponNative());
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);
	TestTrue(TEXT("reload starts"), F.Player->NativePlayerStartReload(0.2f));
	TestTrue(TEXT("fallback timer armed"),
		F.Player->IsReloadFallbackTimerActiveNative());
	TestTrue(TEXT("switch during reload accepted"),
		F.Inventory->TrySwitchWeaponNative());
	TestFalse(TEXT("switch clears the fallback timer"),
		F.Player->IsReloadFallbackTimerActiveNative());
	TestFalse(TEXT("switch cancels the weapon transaction"),
		F.Weapon1->IsReloading());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsDeathTest,
	"Dust2.PlayerActions.DeathCancelsBurstAndReloadTimers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsDeathTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::Burst);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 10, 20);
	TestTrue(TEXT("burst press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("burst group active"), F.Player->IsBurstGroupActiveNative());
	F.Player->NativePlayerFireReleased();

	bool bDiedNow = false;
	TestTrue(TEXT("lethal damage applied"), F.Player->ApplyDamage(1000.0f, bDiedNow));
	TestTrue(TEXT("death confirmed"), bDiedNow);
	TestFalse(TEXT("death cancels the burst group"),
		F.Player->IsBurstGroupActiveNative());
	TestFalse(TEXT("death clears the held input"),
		F.Player->IsSprintBlockedForFire());
	const int32 ShotsAtDeath = F.Player->GetNativeCommittedShotCount();
	F.Tick(0.4f);
	TestEqual(TEXT("no burst timer survives death"),
		F.Player->GetNativeCommittedShotCount(), ShotsAtDeath);

	F.Player->RestoreHealth(100.0f);
	F.Presentation->SetWeaponForPresentation(
		F.Inventory->GetCurrentWeaponNative());
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);
	TestTrue(TEXT("reload starts after restore"),
		F.Player->NativePlayerStartReload(0.5f));
	TestTrue(TEXT("fallback armed"), F.Player->IsReloadFallbackTimerActiveNative());
	bDiedNow = false;
	TestTrue(TEXT("lethal damage applied during reload"),
		F.Player->ApplyDamage(1000.0f, bDiedNow));
	TestTrue(TEXT("death confirmed during reload"), bDiedNow);
	TestFalse(TEXT("death clears the fallback timer"),
		F.Player->IsReloadFallbackTimerActiveNative());
	TestFalse(TEXT("death cancels the weapon reload"), F.Weapon1->IsReloading());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsReloadValidationTest,
	"Dust2.PlayerActions.ReloadRequestValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsReloadValidationTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 90);
	TestTrue(TEXT("reload starts"), F.Player->NativePlayerStartReload(5.0f));
	const int32 RequestId = F.Player->GetActiveReloadRequestIdNative();
	TestTrue(TEXT("request id published"), RequestId > 0);
	TestTrue(TEXT("active request matches the weapon"),
		F.Weapon1->GetReloadRequestId() == RequestId);

	TestFalse(TEXT("future request id rejected"),
		F.Player->NativePlayerCompleteReload(F.Weapon1, RequestId + 1));
	TestTrue(TEXT("mismatched completion leaves the transaction"),
		F.Weapon1->IsReloading());
	TestFalse(TEXT("other weapon rejected"),
		F.Player->NativePlayerCompleteReload(F.Weapon2, RequestId));
	TestFalse(TEXT("animation-driven on another weapon rejected"),
		F.Player->NativePlayerSetReloadAnimationDriven(F.Weapon2, RequestId, true));
	TestFalse(TEXT("animation-driven with a stale request rejected"),
		F.Player->NativePlayerSetReloadAnimationDriven(F.Weapon1, RequestId + 1, true));
	TestTrue(TEXT("animation-driven accepted for the active request"),
		F.Player->NativePlayerSetReloadAnimationDriven(F.Weapon1, RequestId, true));
	TestFalse(TEXT("taking animation ownership clears the fallback"),
		F.Player->IsReloadFallbackTimerActiveNative());

	const int32 AmmoChangedBefore = F.Player->GetNativeAmmoChangedCount();
	TestTrue(TEXT("matching completion accepted"),
		F.Player->NativePlayerCompleteReload(F.Weapon1, RequestId));
	TestEqual(TEXT("reload committed ammo"), F.Weapon1->GetCurrentAmmo(), 30);
	TestEqual(TEXT("HUD ammo notification once"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChangedBefore + 1);
	TestEqual(TEXT("context cleared after completion"),
		F.Player->GetActiveReloadRequestIdNative(), 0);
	TestFalse(TEXT("duplicate completion rejected"),
		F.Player->NativePlayerCompleteReload(F.Weapon1, RequestId));
	TestEqual(TEXT("duplicate completion cannot add ammo"),
		F.Weapon1->GetCurrentAmmo(), 30);
	TestEqual(TEXT("duplicate completion cannot notify twice"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChangedBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsReloadCompletionTest,
	"Dust2.PlayerActions.ReloadAnimationAndFallbackSingleCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsReloadCompletionTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;

	// Animation-driven request: the fallback is disarmed and completes exactly once.
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 90);
	TestTrue(TEXT("animation-driven reload starts"),
		F.Player->NativePlayerStartReload(0.05f));
	const int32 AnimationRequest = F.Player->GetActiveReloadRequestIdNative();
	TestTrue(TEXT("animation takes ownership"),
		F.Player->NativePlayerSetReloadAnimationDriven(F.Weapon1, AnimationRequest, true));
	TestFalse(TEXT("fallback disarmed by animation ownership"),
		F.Player->IsReloadFallbackTimerActiveNative());
	const int32 AmmoChangedBefore = F.Player->GetNativeAmmoChangedCount();
	F.Tick(0.1f);
	TestTrue(TEXT("fallback time passing does not complete animation request"),
		F.Weapon1->IsReloading());
	TestEqual(TEXT("no ammo before the montage completion"),
		F.Weapon1->GetCurrentAmmo(), 5);
	F.Presentation->OnReloadWeaponPresentationCompleted.Broadcast(
		F.Weapon1, AnimationRequest);
	TestFalse(TEXT("animation completion finishes the request"),
		F.Weapon1->IsReloading());
	TestEqual(TEXT("ammo committed once"), F.Weapon1->GetCurrentAmmo(), 30);
	TestEqual(TEXT("one HUD refresh"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChangedBefore + 1);
	F.Presentation->OnReloadWeaponPresentationCompleted.Broadcast(
		F.Weapon1, AnimationRequest);
	TestEqual(TEXT("stale animation completion cannot commit twice"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChangedBefore + 1);

	// Fallback-owned request: an unrelated animation-completed event cannot finish it.
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 4, 90);
	TestTrue(TEXT("fallback reload starts"),
		F.Player->NativePlayerStartReload(0.2f));
	const int32 FallbackRequest = F.Player->GetActiveReloadRequestIdNative();
	TestFalse(TEXT("fallback request is not animation-driven"),
		F.Player->IsReloadAnimationDrivenNative());
	F.Presentation->OnReloadWeaponPresentationCompleted.Broadcast(
		F.Weapon1, FallbackRequest);
	TestTrue(TEXT("animation event cannot complete a fallback request"),
		F.Weapon1->IsReloading());
	TestEqual(TEXT("unrelated event leaves ammo untouched"),
		F.Weapon1->GetCurrentAmmo(), 4);
	TestEqual(TEXT("unrelated event does not refresh HUD"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChangedBefore + 1);
	F.Tick(0.25f);
	TestFalse(TEXT("fallback completes its own request"), F.Weapon1->IsReloading());
	TestEqual(TEXT("fallback commits once"), F.Weapon1->GetCurrentAmmo(), 30);
	TestEqual(TEXT("fallback refreshes HUD once"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChangedBefore + 2);
	F.Presentation->OnReloadWeaponPresentationCompleted.Broadcast(
		F.Weapon1, FallbackRequest);
	TestEqual(TEXT("late animation event cannot complete twice"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChangedBefore + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsStaleFallbackTest,
	"Dust2.PlayerActions.StaleFallbackCannotCompleteNewRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsStaleFallbackTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);
	TestTrue(TEXT("first reload starts"),
		F.Player->NativePlayerStartReload(5.0f));
	const int32 FirstRequest = F.Player->GetActiveReloadRequestIdNative();
	const uint64 FirstSerial =
		FDustPlayerActionsTestAccess::ReloadFallbackSerial(F.Player);
	TestTrue(TEXT("external finish simulates a replaced request"),
		F.Weapon1->FinishReload());

	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 90);
	TestTrue(TEXT("new reload on the same weapon starts"),
		F.Player->NativePlayerStartReload(5.0f));
	const int32 SecondRequest = F.Player->GetActiveReloadRequestIdNative();
	TestTrue(TEXT("request id advanced"), SecondRequest != FirstRequest);
	TestTrue(TEXT("new request active"),
		F.Player->IsReloadFallbackTimerActiveNative());

	// Replay the old timer callback for the same weapon: it must not finish request 2.
	FDustPlayerActionsTestAccess::FireReloadFallback(
		F.Player, FirstSerial, F.Weapon1, FirstRequest);
	TestTrue(TEXT("stale fallback cannot complete the new request"),
		F.Weapon1->IsReloading());
	TestEqual(TEXT("new request context preserved"),
		F.Player->GetActiveReloadRequestIdNative(), SecondRequest);
	TestTrue(TEXT("new request still completes through its own context"),
		F.Player->NativePlayerCompleteReload(F.Weapon1, SecondRequest));
	TestFalse(TEXT("new request finished"), F.Weapon1->IsReloading());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsInitializeTest,
	"Dust2.PlayerActions.InitializeIdempotentAndComponentSwap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsInitializeTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F(false);
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;

	TestTrue(TEXT("first init accepted"),
		F.Player->InitializePlayerActionsNative(F.Presentation, F.Camera));
	TestTrue(TEXT("same init is idempotent"),
		F.Player->InitializePlayerActionsNative(F.Presentation, F.Camera));
	TestTrue(TEXT("third same init is idempotent"),
		F.Player->InitializePlayerActionsNative(F.Presentation, F.Camera));
	TestFalse(TEXT("invalid re-init does not disable the working slice"),
		F.Player->InitializePlayerActionsNative(nullptr, F.Camera));
	TestTrue(TEXT("slice still enabled"), F.Player->IsPlayerActionsNativeEnabled());

	// Repeated AddUnique bindings must not complete a reload multiple times.
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);
	TestTrue(TEXT("reload starts"),
		F.Player->NativePlayerStartReload(5.0f));
	const int32 Request = F.Player->GetActiveReloadRequestIdNative();
	TestTrue(TEXT("animation driven"),
		F.Player->NativePlayerSetReloadAnimationDriven(F.Weapon1, Request, true));
	int32 AmmoChanged = F.Player->GetNativeAmmoChangedCount();
	F.Presentation->OnReloadWeaponPresentationCompleted.Broadcast(
		F.Weapon1, Request);
	TestEqual(TEXT("duplicate init binds the completion once"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChanged + 1);

	// A rejected camera replacement leaves the working configuration intact.
	TestFalse(TEXT("different camera cannot replace active configuration"),
		F.Player->InitializePlayerActionsNative(F.Presentation, F.Camera2));
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);
	F.Tick(0.2f);
	TestTrue(TEXT("fire still works after camera swap"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("camera swap consumes one shot"),
		F.Weapon1->GetCurrentAmmo(), 4);
	F.Player->NativePlayerFireReleased();

	// A rejected presentation replacement must not move the completion binding.
	TestFalse(TEXT("different presentation cannot replace active configuration"),
		F.Player->InitializePlayerActionsNative(F.Presentation2, F.Camera2));
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);
	TestTrue(TEXT("reload starts after presentation swap"),
		F.Player->NativePlayerStartReload(5.0f));
	const int32 SwappedRequest = F.Player->GetActiveReloadRequestIdNative();
	TestTrue(TEXT("animation driven after swap"),
		F.Player->NativePlayerSetReloadAnimationDriven(
			F.Weapon1, SwappedRequest, true));
	AmmoChanged = F.Player->GetNativeAmmoChangedCount();
	F.Presentation2->OnReloadWeaponPresentationCompleted.Broadcast(
		F.Weapon1, SwappedRequest);
	TestTrue(TEXT("old presentation is unbound"),
		F.Weapon1->IsReloading());
	TestEqual(TEXT("old presentation cannot refresh HUD"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChanged);
	F.Presentation->OnReloadWeaponPresentationCompleted.Broadcast(
		F.Weapon1, SwappedRequest);
	TestFalse(TEXT("new presentation completes the request"),
		F.Weapon1->IsReloading());
	TestEqual(TEXT("new presentation refreshes HUD once"),
		F.Player->GetNativeAmmoChangedCount(), AmmoChanged + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsModeTest,
	"Dust2.PlayerActions.FireModeChangeCancelsAndNotifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsModeTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 10, 20);

	TestTrue(TEXT("semi press accepted"), F.Player->NativePlayerFirePressed());
	F.Player->NativePlayerFireReleased();
	F.Player->NativePlayerCycleFireMode();
	TestTrue(TEXT("semi cycles to automatic"),
		F.Weapon1->GetFireMode() == EFireMode::FullAutomatic);
	TestEqual(TEXT("one mode-change notification"),
		F.Player->GetNativeFireModeChangedCount(), 1);
	TestFalse(TEXT("cycle clears the held input"),
		F.Player->IsSprintBlockedForFire());

	// Burst group is canceled before the mode changes.
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::Burst);
	F.Tick(0.2f);
	TestTrue(TEXT("burst press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("burst group active"), F.Player->IsBurstGroupActiveNative());
	F.Player->NativePlayerFireReleased();
	F.Player->NativePlayerCycleFireMode();
	TestTrue(TEXT("burst cycles to semi"),
		F.Weapon1->GetFireMode() == EFireMode::SemiAutomatic);
	TestFalse(TEXT("cycling cancels the burst group"),
		F.Player->IsBurstGroupActiveNative());
	TestEqual(TEXT("second mode-change notification"),
		F.Player->GetNativeFireModeChangedCount(), 2);

	// A single allowed mode is not a change and must not notify.
	F.Weapon1->WeaponDefinition->AllowedFireModes = { EFireMode::SemiAutomatic };
	F.Weapon1->ApplyDefinitionSettings();
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	F.Player->NativePlayerCycleFireMode();
	TestTrue(TEXT("single-mode weapon keeps its mode"),
		F.Weapon1->GetFireMode() == EFireMode::SemiAutomatic);
	TestEqual(TEXT("no notification without a real change"),
		F.Player->GetNativeFireModeChangedCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferWindowTest,
	"Dust2.PlayerActions.SemiAutoBufferDelayedSingleShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferWindowTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);

	TestTrue(TEXT("first shot accepted"), F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("first shot spends one bullet"), F.Weapon1->GetCurrentAmmo(), 4);
	TestEqual(TEXT("first shot committed"), F.Player->GetNativeCommittedShotCount(), 1);
	F.Player->NativePlayerFireReleased();

	// Enter the final 30 ms of the runtime's own 100 ms cooldown.
	F.Tick(0.07f);
	TestTrue(TEXT("press inside the buffer window is accepted"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("a buffered press never spends ammo early"),
		F.Weapon1->GetCurrentAmmo(), 4);
	TestEqual(TEXT("a buffered press is not a commit yet"),
		F.Player->GetNativeCommittedShotCount(), 1);
	TestTrue(TEXT("exactly one pending request is recorded"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));

	F.Player->NativePlayerFireReleased();
	TestTrue(TEXT("a normal native release preserves the buffered press"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));

	F.Tick(0.01f);
	TestEqual(TEXT("the buffer does not fire before the runtime cooldown"),
		F.Weapon1->GetCurrentAmmo(), 4);
	F.Tick(0.015f);
	TestEqual(TEXT("the buffer does not raise the fire rate"),
		F.Weapon1->GetCurrentAmmo(), 4);
	TestTrue(TEXT("the pending request survives until cooldown expiry"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));

	F.Tick(0.01f);
	TestFalse(TEXT("the one-shot timer consumes the pending request"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestEqual(TEXT("the delayed shot spends exactly one bullet"),
		F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("the delayed shot commits exactly once"),
		F.Player->GetNativeCommittedShotCount(), 2);
	TestEqual(TEXT("the delayed shot resolves exactly once"),
		F.Player->GetNativeShotResolvedNotifiedCount(), 2);

	F.Tick(0.2f);
	TestEqual(TEXT("release cannot replay the already consumed press"),
		F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("no extra buffered shot appears later"),
		F.Player->GetNativeCommittedShotCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferRejectTest,
	"Dust2.PlayerActions.SemiAutoBufferTooEarlyRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferRejectTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);

	TestTrue(TEXT("first shot accepted"), F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("first shot spends one bullet"), F.Weapon1->GetCurrentAmmo(), 4);
	F.Player->NativePlayerFireReleased();

	// More than 40 ms remains: this is a normal rejected press, not a buffer.
	F.Tick(0.04f);
	TestTrue(TEXT("the press itself is still accepted"),
		F.Player->NativePlayerFirePressed());
	TestFalse(TEXT("a cooldown beyond 40 ms is not buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestEqual(TEXT("a rejected early press spends no ammo"),
		F.Weapon1->GetCurrentAmmo(), 4);
	F.Player->NativePlayerFireReleased();

	F.Tick(0.08f);
	TestEqual(TEXT("an early rejected press never fires later"),
		F.Weapon1->GetCurrentAmmo(), 4);
	TestEqual(TEXT("an early rejected press is not a commit"),
		F.Player->GetNativeCommittedShotCount(), 1);

	TestTrue(TEXT("a fresh press after the cooldown fires"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("the fresh press spends one bullet"), F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("the fresh press commits once"),
		F.Player->GetNativeCommittedShotCount(), 2);
	F.Player->NativePlayerFireReleased();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferSinglePendingTest,
	"Dust2.PlayerActions.SemiAutoBufferSinglePendingNoRenewal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferSinglePendingTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);

	TestTrue(TEXT("first shot accepted"), F.Player->NativePlayerFirePressed());
	F.Player->NativePlayerFireReleased();
	F.Tick(0.07f);

	TestTrue(TEXT("first buffered click accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("first buffered click recorded"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	const uint64 FirstGeneration =
		FDustPlayerActionsTestAccess::SemiAutoBufferGeneration(F.Player);
	const float FirstDeadline =
		FDustPlayerActionsTestAccess::SemiAutoBufferDeadline(F.Player);
	TestTrue(TEXT("the recorded deadline is a real future time"), FirstDeadline > 0.0f);
	F.Player->NativePlayerFireTriggered();
	TestTrue(TEXT("semi Triggered cannot duplicate the pending request"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("semi Triggered cannot renew the generation"),
		FDustPlayerActionsTestAccess::SemiAutoBufferGeneration(F.Player) == FirstGeneration);
	TestEqual(TEXT("semi Triggered cannot renew the deadline"),
		FDustPlayerActionsTestAccess::SemiAutoBufferDeadline(F.Player), FirstDeadline);
	F.Player->NativePlayerFireReleased();

	TestTrue(TEXT("second click accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("second click cannot clear the first pending request"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("second click cannot create a second generation"),
		FDustPlayerActionsTestAccess::SemiAutoBufferGeneration(F.Player) == FirstGeneration);
	TestEqual(TEXT("second click cannot renew the deadline"),
		FDustPlayerActionsTestAccess::SemiAutoBufferDeadline(F.Player), FirstDeadline);
	TestEqual(TEXT("second click spends nothing"), F.Weapon1->GetCurrentAmmo(), 4);
	F.Player->NativePlayerFireReleased();

	TestTrue(TEXT("third click accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("third click keeps the same generation"),
		FDustPlayerActionsTestAccess::SemiAutoBufferGeneration(F.Player) == FirstGeneration);
	TestEqual(TEXT("third click keeps the same deadline"),
		FDustPlayerActionsTestAccess::SemiAutoBufferDeadline(F.Player), FirstDeadline);
	F.Player->NativePlayerFireReleased();

	F.Tick(0.01f);
	TestEqual(TEXT("rapid clicks do not spend ammo early"), F.Weapon1->GetCurrentAmmo(), 4);
	F.Tick(0.025f);
	TestEqual(TEXT("rapid clicks produce exactly one delayed shot"),
		F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("rapid clicks produce one extra commit"),
		F.Player->GetNativeCommittedShotCount(), 2);
	TestFalse(TEXT("the one pending request is consumed once"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));

	F.Tick(0.2f);
	TestEqual(TEXT("no further shot appears from the extra clicks"),
		F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("committed shot count stays at two"),
		F.Player->GetNativeCommittedShotCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferExpiredTest,
	"Dust2.PlayerActions.SemiAutoBufferExpiredDiscarded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferExpiredTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);

	TestTrue(TEXT("first shot accepted"), F.Player->NativePlayerFirePressed());
	F.Player->NativePlayerFireReleased();
	F.Tick(0.07f);

	TestTrue(TEXT("window press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("window press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->NativePlayerFireReleased();

	// A single long frame lands after the queue-time + 40 ms validity window.
	F.Tick(0.05f);
	TestFalse(TEXT("an expired buffer is discarded"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestEqual(TEXT("a discarded buffer never spends ammo"),
		F.Weapon1->GetCurrentAmmo(), 4);
	TestEqual(TEXT("a discarded buffer is not a commit"),
		F.Player->GetNativeCommittedShotCount(), 1);

	F.Tick(0.2f);
	TestEqual(TEXT("the expired buffer cannot catch up later"),
		F.Weapon1->GetCurrentAmmo(), 4);
	TestEqual(TEXT("no late commit appears"), F.Player->GetNativeCommittedShotCount(), 1);

	TestTrue(TEXT("a normal press still works after the discard"),
		F.Player->NativePlayerFirePressed());
	TestEqual(TEXT("the normal press spends one bullet"), F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("the normal press commits once"),
		F.Player->GetNativeCommittedShotCount(), 2);
	F.Player->NativePlayerFireReleased();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferStaleGenerationTest,
	"Dust2.PlayerActions.SemiAutoBufferStaleGenerationInert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferStaleGenerationTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);

	TestTrue(TEXT("first shot accepted"), F.Player->NativePlayerFirePressed());
	F.Player->NativePlayerFireReleased();
	F.Tick(0.07f);

	TestTrue(TEXT("old request press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("old request pending"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	const uint64 OldGeneration =
		FDustPlayerActionsTestAccess::SemiAutoBufferGeneration(F.Player);
	F.Player->NativePlayerCancelActions();
	TestFalse(TEXT("cancel clears the old request"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));

	F.Tick(0.01f);
	TestTrue(TEXT("new request press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("new request pending"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	const uint64 NewGeneration =
		FDustPlayerActionsTestAccess::SemiAutoBufferGeneration(F.Player);
	const float NewDeadline =
		FDustPlayerActionsTestAccess::SemiAutoBufferDeadline(F.Player);
	TestTrue(TEXT("the new request owns a new generation"), NewGeneration != OldGeneration);
	F.Player->NativePlayerFireReleased();

	// Replay the old timer callback while the new request is still pending.
	FDustPlayerActionsTestAccess::FireSemiAutoBuffer(F.Player, OldGeneration, F.Weapon1);
	TestTrue(TEXT("a stale generation cannot consume the new pending request"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestEqual(TEXT("a stale generation cannot change the new deadline"),
		FDustPlayerActionsTestAccess::SemiAutoBufferDeadline(F.Player), NewDeadline);
	TestEqual(TEXT("a stale generation spends no ammo"), F.Weapon1->GetCurrentAmmo(), 4);

	F.Tick(0.015f);
	TestEqual(TEXT("the new request has not fired early"), F.Weapon1->GetCurrentAmmo(), 4);
	FDustPlayerActionsTestAccess::FireSemiAutoBuffer(F.Player, OldGeneration, F.Weapon1);
	TestTrue(TEXT("a stale replay leaves the new pending request intact"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));

	F.Tick(0.015f);
	TestFalse(TEXT("the new request fires through its own timer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestEqual(TEXT("the new request spends one bullet"), F.Weapon1->GetCurrentAmmo(), 3);
	TestEqual(TEXT("the new request commits once"),
		F.Player->GetNativeCommittedShotCount(), 2);

	FDustPlayerActionsTestAccess::FireSemiAutoBuffer(F.Player, OldGeneration, F.Weapon1);
	TestEqual(TEXT("a stale replay after consumption does nothing"),
		F.Weapon1->GetCurrentAmmo(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferCleanupInputTest,
	"Dust2.PlayerActions.SemiAutoBufferInputCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferCleanupInputTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 10, 20);

	// Simulate the cooldown result returning after a ready-check BP canceled for menu.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	const uint64 CancelledAttempt = FDustPlayerActionsTestAccess::ActionsSerial(F.Player);
	F.Player->NotifyFireReleased();
	FDustPlayerActionsTestAccess::TryBufferAfterDispatch(
		F.Player, F.Weapon1, CancelledAttempt);
	TestFalse(TEXT("menu cancellation cannot be undone by a returning cooldown attempt"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Tick(0.025f);
	TestEqual(TEXT("a canceled in-flight attempt cannot spend ammo later"),
		F.Weapon1->GetCurrentAmmo(), 10);

	// Legacy menu/input release entry clears the buffer.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("menu-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("menu-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->NotifyFireReleased();
	TestFalse(TEXT("legacy NotifyFireReleased clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Tick(0.05f);
	TestEqual(TEXT("legacy release prevents the buffered shot"),
		F.Weapon1->GetCurrentAmmo(), 10);

	// Native cancel clears the buffer and keeps the slice enabled.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("cancel-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("cancel-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->NativePlayerCancelActions();
	TestFalse(TEXT("NativePlayerCancelActions clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("cancel keeps the native slice enabled"),
		F.Player->IsPlayerActionsNativeEnabled());
	F.Tick(0.05f);
	TestEqual(TEXT("cancel prevents the buffered shot"), F.Weapon1->GetCurrentAmmo(), 10);

	// Cycling fire mode clears the buffer before the mode changes.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("cycle-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("cycle-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->NativePlayerCycleFireMode();
	TestFalse(TEXT("NativePlayerCycleFireMode clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("cycling still changes the fire mode"),
		F.Weapon1->GetFireMode() == EFireMode::FullAutomatic);
	F.Tick(0.05f);
	TestEqual(TEXT("cycle prevents the buffered semi shot"),
		F.Weapon1->GetCurrentAmmo(), 10);

	// An empty magazine is never stored as a buffered shot.
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 0, 20);
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("empty press accepted"), F.Player->NativePlayerFirePressed());
	TestFalse(TEXT("an empty magazine is not buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->NativePlayerFireReleased();
	F.Tick(0.05f);
	TestEqual(TEXT("an empty buffered click can never consume ammo"),
		F.Weapon1->GetCurrentAmmo(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferCleanupWeaponReloadTest,
	"Dust2.PlayerActions.SemiAutoBufferWeaponAndReloadCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferCleanupWeaponReloadTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 10, 20);

	// Switching away from the buffered weapon's owner clears the buffer.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("switch-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("switch-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("weapon switch accepted"), F.Inventory->TrySwitchWeaponNative());
	TestTrue(TEXT("switch changed the current weapon"),
		F.Inventory->GetCurrentWeaponNative() == F.Weapon2);
	TestFalse(TEXT("weapon switch clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Tick(0.05f);
	TestEqual(TEXT("switch prevents the buffered shot on the old weapon"),
		F.Weapon1->GetCurrentAmmo(), 10);
	F.Player->NativePlayerFireReleased();

	TestTrue(TEXT("switch back accepted"), F.Inventory->TrySwitchWeaponNative());
	TestTrue(TEXT("switch back restored the first weapon"),
		F.Inventory->GetCurrentWeaponNative() == F.Weapon1);
	F.Presentation->SetWeaponForPresentation(
		F.Inventory->GetCurrentWeaponNative());
	F.Player->NativePlayerFireReleased();

	// A successful reload is a hard stop for the buffered semi shot.
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 90);
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("reload-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("reload-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("successful reload starts"), F.Player->NativePlayerStartReload(0.2f));
	TestTrue(TEXT("the weapon is reloading"), F.Weapon1->IsReloading());
	TestFalse(TEXT("successful reload clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->NativePlayerFireReleased();
	F.Tick(0.25f);
	TestFalse(TEXT("fallback reload completed"), F.Weapon1->IsReloading());
	TestEqual(TEXT("no buffered shot interrupts the completed reload"),
		F.Weapon1->GetCurrentAmmo(), 30);

	// An external OnReloadStarted entry clears the buffer as well.
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 5, 10);
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("external-reload press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("external-reload press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("external reload starts"), F.Weapon1->TryStartReload());
	TestFalse(TEXT("external OnReloadStarted clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestTrue(TEXT("external reload is active"), F.Weapon1->IsReloading());
	TestTrue(TEXT("external reload canceled for cleanup"), F.Weapon1->CancelReload());
	F.Player->NativePlayerFireReleased();

	// A rejected reload must not destroy the still-available buffered shot.
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 30, 30);
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("failed-reload press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("failed-reload press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestFalse(TEXT("full-magazine reload is rejected"),
		F.Player->NativePlayerStartReload(0.2f));
	TestTrue(TEXT("a failed reload preserves the buffered shot"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->NativePlayerFireReleased();
	F.Tick(0.025f);
	TestEqual(TEXT("the preserved buffer fires once after the failed reload"),
		F.Weapon1->GetCurrentAmmo(), 29);
	TestEqual(TEXT("the preserved buffer commits once"),
		F.Player->GetNativeCommittedShotCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerActionsSemiBufferCleanupLifecycleTest,
	"Dust2.PlayerActions.SemiAutoBufferDeathRestoreShutdownCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerActionsSemiBufferCleanupLifecycleTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FDustPlayerActionsWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
	FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::SemiAutomatic);
	FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 10, 20);

	// Death runs through the existing NativePlayerCancelActions chain.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("death-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("death-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	bool bDiedNow = false;
	TestTrue(TEXT("lethal damage applied"), F.Player->ApplyDamage(1000.0f, bDiedNow));
	TestTrue(TEXT("death confirmed"), bDiedNow);
	TestFalse(TEXT("death clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Tick(0.05f);
	TestEqual(TEXT("death prevents the buffered shot"), F.Weapon1->GetCurrentAmmo(), 10);

	F.Player->RestoreHealth(100.0f);
	TestFalse(TEXT("restore revives the player"), F.Player->IsDead());

	// Restore is also an existing cleanup entry.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("restore-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("restore-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Player->RestoreHealth(100.0f);
	TestFalse(TEXT("restore clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	F.Tick(0.05f);
	TestEqual(TEXT("restore prevents the buffered shot"), F.Weapon1->GetCurrentAmmo(), 10);

	// Shutdown is the EndPlay/unbind chain's authoritative cleanup.
	FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1,
		F.World->GetTimeSeconds() + 0.02f);
	TestTrue(TEXT("shutdown-path press accepted"), F.Player->NativePlayerFirePressed());
	TestTrue(TEXT("shutdown-path press buffered"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	FDustPlayerActionsTestAccess::ShutdownPlayerActions(F.Player);
	TestFalse(TEXT("shutdown clears the buffer"),
		FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
	TestFalse(TEXT("shutdown disables the native slice"),
		F.Player->IsPlayerActionsNativeEnabled());
	F.Tick(0.05f);
	TestEqual(TEXT("shutdown prevents the buffered shot"), F.Weapon1->GetCurrentAmmo(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerFireAnimationLifecycleTest,
    "Dust2.PlayerActions.FireAnimationLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerFireAnimationLifecycleTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
        ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
    FDustPlayerActionsWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    TestFalse(TEXT("initial pose is idle"), F.Player->IsFireAnimationActive());
    auto Fire = [&]()
    {
        FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1, 0.0f);
        F.Player->NativePlayerFirePressed();
        TestTrue(TEXT("committed held shot activates pose"), F.Player->IsFireAnimationActive());
    };
    Fire();
    F.Player->NativePlayerFireReleased();
    TestFalse(TEXT("release clears pose"), F.Player->IsFireAnimationActive());
    Fire();
    F.Player->NotifyFireReleased();
    TestFalse(TEXT("legacy/menu release clears pose"), F.Player->IsFireAnimationActive());
    Fire();
    TestTrue(TEXT("reload starts"), F.Player->NativePlayerStartReload(0.2f));
    TestFalse(TEXT("reload clears pose"), F.Player->IsFireAnimationActive());
    F.Player->NativePlayerCancelActions();
    Fire();
    F.Player->NativePlayerCancelActions();
    TestFalse(TEXT("switch/cancel clears pose"), F.Player->IsFireAnimationActive());
    Fire();
    F.Player->NativePlayerCycleFireMode();
    TestFalse(TEXT("mode change clears pose"), F.Player->IsFireAnimationActive());
    Fire();
    bool bDiedNow = false;
    F.Player->ApplyDamage(1000.0f, bDiedNow);
    TestTrue(TEXT("death occurred"), bDiedNow);
    TestFalse(TEXT("death clears pose"), F.Player->IsFireAnimationActive());
    F.Player->RestoreHealth(100.0f);
    TestFalse(TEXT("restore starts idle"), F.Player->IsFireAnimationActive());
    Fire();
    FDustPlayerActionsTestAccess::ShutdownPlayerActions(F.Player);
    TestFalse(TEXT("shutdown clears pose"), F.Player->IsFireAnimationActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerFireAnimationReleasedTailTest,
    "Dust2.PlayerActions.FireAnimationReleasedShotTails",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerFireAnimationReleasedTailTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"),
        ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
    FDustPlayerActionsWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1, F.World->GetTimeSeconds() + 0.02f);
    F.Player->NativePlayerFirePressed();
    TestFalse(TEXT("cooldown buffer does not activate pose"), F.Player->IsFireAnimationActive());
    TestTrue(TEXT("buffer queued"), FDustPlayerActionsTestAccess::HasPendingSemiAutoBuffer(F.Player));
    F.Player->NativePlayerFireReleased();
    F.Tick(0.025f);
    TestEqual(TEXT("released buffer still commits"), F.Weapon1->GetCurrentAmmo(), 29);
    TestFalse(TEXT("released buffer cannot reactivate pose"), F.Player->IsFireAnimationActive());
    F.Tick(0.2f);
    FDustPlayerActionsWorld::SetMode(F.Weapon1, EFireMode::Burst);
    FDustPlayerActionsWorld::SetNextFireTime(F.Weapon1, 0.0f);
    F.Player->NativePlayerFirePressed();
    TestTrue(TEXT("held burst activates pose"), F.Player->IsFireAnimationActive());
    F.Player->NativePlayerFireReleased();
    F.Tick(0.06f);
    F.Tick(0.06f);
    TestEqual(TEXT("burst tail still completes"), F.Weapon1->GetCurrentAmmo(), 26);
    TestFalse(TEXT("released burst cannot reactivate pose"), F.Player->IsFireAnimationActive());
    F.Tick(0.3f);
    FDustPlayerActionsWorld::SetAmmo(F.Weapon1, 0, 20);
    F.Player->NativePlayerFirePressed();
    TestFalse(TEXT("dry fire does not activate pose"), F.Player->IsFireAnimationActive());
    return true;
}
#endif
