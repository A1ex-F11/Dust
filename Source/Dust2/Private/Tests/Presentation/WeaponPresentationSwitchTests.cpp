#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "Presentation/WeaponPresentationComponent.h"
#include "Presentation/WeaponPresentationTypes.h"
#include "Weapons/WeaponRuntime.h"

namespace
{
	/**
	 * Lightweight world fixture: the presentation component only needs a valid
	 * world, bound runtime weapons and a registered optional character mesh.
	 * No animation assets are loaded, so these tests exercise native state only.
	 */
	struct FPresentationSwitchWorld
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UWeaponPresentationComponent* Presentation = nullptr;
		USkeletalMeshComponent* CharacterMesh = nullptr;
		AWeaponRuntime* Weapon1 = nullptr;
		AWeaponRuntime* Weapon2 = nullptr;
		bool bReady = false;

		FPresentationSwitchWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
				nullptr, false, ERHIFeatureLevel::Num, &Values);
			if (!World || !GEngine)
			{
				return;
			}

			GEngine->CreateNewWorldContext(EWorldType::Game)
				.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			Owner = World->SpawnActor<AActor>();
			Weapon1 = World->SpawnActor<AWeaponRuntime>();
			Weapon2 = World->SpawnActor<AWeaponRuntime>();
			if (!Owner || !Weapon1 || !Weapon2)
			{
				return;
			}

			Presentation = NewObject<UWeaponPresentationComponent>(Owner);
			CharacterMesh = NewObject<USkeletalMeshComponent>(Owner);
			USceneComponent* Root = NewObject<USceneComponent>(Owner);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			CharacterMesh->SetupAttachment(Root);
			Presentation->RegisterComponent();
			CharacterMesh->RegisterComponent();
			Presentation->SetWeaponForPresentation(Weapon1);
			bReady = Presentation->IsRegistered() &&
				CharacterMesh->IsRegistered() &&
				IsValid(Weapon1) && IsValid(Weapon2);
		}

		~FPresentationSwitchWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationSwitchGateTest,
	"Dust2.WeaponPresentation.SwitchNativeGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationSwitchGateTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	TestFalse(TEXT("native switching defaults off"),
		F.Presentation->IsPresentationSwitching());
	TestTrue(TEXT("bound weapon permits reload presentation"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, false));

	F.Presentation->SetPresentationSwitching(true);
	TestTrue(TEXT("native switching owns the true state"),
		F.Presentation->IsPresentationSwitching());
	TestFalse(TEXT("native switching blocks the gate with a false Blueprint flag"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, false));
	TestFalse(TEXT("native switching keeps blocking with a true Blueprint flag"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, true));

	F.Presentation->SetPresentationSwitching(false);
	TestFalse(TEXT("native switching clears explicitly"),
		F.Presentation->IsPresentationSwitching());
	TestTrue(TEXT("gate reopens after the native switch clears"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, false));
	TestFalse(TEXT("the existing Blueprint parameter still blocks on its own"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, true));

	F.Presentation->SetWeaponForPresentation(nullptr);
	F.Presentation->SetPresentationSwitching(true);
	TestFalse(TEXT("enabling without a bound weapon is rejected"),
		F.Presentation->IsPresentationSwitching());
	TestFalse(TEXT("unbound presentation blocks the reload gate"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationCurrentWeaponTest,
	"Dust2.WeaponPresentation.CurrentWeaponPredicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationCurrentWeaponTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	TestTrue(TEXT("bound identity is current"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon1, F.Weapon1));
	TestFalse(TEXT("inventory current and bound must both match"),
        F.Presentation->IsCurrentPresentationWeapon(F.Weapon1, F.Weapon2));
    TestFalse(TEXT("null is not current"),
		F.Presentation->IsCurrentPresentationWeapon(nullptr, nullptr));
	TestFalse(TEXT("another valid weapon is not current"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon2, F.Weapon2));

	F.Presentation->SetWeaponForPresentation(F.Weapon2);
	TestFalse(TEXT("old weapon stops being current"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon1, F.Weapon1));
	TestTrue(TEXT("new bound weapon is current"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon2, F.Weapon2));

	F.Presentation->SetWeaponForPresentation(nullptr);
	TestFalse(TEXT("empty binding has no current weapon"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon2, F.Weapon2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationHolsterReadinessTest,
	"Dust2.WeaponPresentation.HolsterReadinessWithoutMontages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationHolsterReadinessTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	TestFalse(TEXT("no readiness before a holster"),
		F.Presentation->IsFullHolsterReadyToSwitch());

	FWeaponPresentationProfile Profile;
	TestTrue(TEXT("holster without montages completes synchronously"),
		F.Presentation->StartFullHolsterPresentation(
			F.Weapon1, F.CharacterMesh, nullptr, Profile));
	TestFalse(TEXT("completed holster is no longer active"),
		F.Presentation->IsFullHolsterPresentationActive());
	// StartFullHolsterPresentation broadcasts OnFullHolsterPresentationReady
	// synchronously, so the token must already be visible on return.
	TestTrue(TEXT("readiness is recorded before the ready broadcast returns"),
		F.Presentation->IsFullHolsterReadyToSwitch());

	F.Presentation->CancelFullHolsterPresentation();
	TestFalse(TEXT("cancellation invalidates readiness"),
		F.Presentation->IsFullHolsterReadyToSwitch());

	TestTrue(TEXT("a second holster can complete after cancellation"),
		F.Presentation->StartFullHolsterPresentation(
			F.Weapon1, F.CharacterMesh, nullptr, Profile));
	TestTrue(TEXT("second completion publishes readiness"),
		F.Presentation->IsFullHolsterReadyToSwitch());
	F.Presentation->SetWeaponForPresentation(nullptr);
	TestFalse(TEXT("unbind invalidates readiness"),
		F.Presentation->IsFullHolsterReadyToSwitch());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationHolsterStaleWeaponTest,
	"Dust2.WeaponPresentation.HolsterReadinessRejectsStaleWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationHolsterStaleWeaponTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	FWeaponPresentationProfile Profile;
	TestTrue(TEXT("first holster completes"),
		F.Presentation->StartFullHolsterPresentation(
			F.Weapon1, F.CharacterMesh, nullptr, Profile));
	TestTrue(TEXT("first weapon is ready"),
		F.Presentation->IsFullHolsterReadyToSwitch());

	F.Presentation->SetWeaponForPresentation(F.Weapon2);
	TestFalse(TEXT("binding a different weapon invalidates the old token"),
		F.Presentation->IsFullHolsterReadyToSwitch());
	TestTrue(TEXT("second holster completes for the new weapon"),
		F.Presentation->StartFullHolsterPresentation(
			F.Weapon2, F.CharacterMesh, nullptr, Profile));
	TestTrue(TEXT("second weapon is ready"),
		F.Presentation->IsFullHolsterReadyToSwitch());

	F.Presentation->SetWeaponForPresentation(F.Weapon1);
	TestFalse(TEXT("the token does not follow the old weapon back"),
		F.Presentation->IsFullHolsterReadyToSwitch());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationResetTest,
	"Dust2.WeaponPresentation.ResetCancelsPendingWorkIdempotently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationResetTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	FWeaponPresentationProfile Profile;
	TestTrue(TEXT("holster readiness exists before reset"),
		F.Presentation->StartFullHolsterPresentation(
			F.Weapon1, F.CharacterMesh, nullptr, Profile));
	TestTrue(TEXT("holster readiness is set before reset"),
		F.Presentation->IsFullHolsterReadyToSwitch());

	F.Presentation->SetPresentationSwitching(true);
	TestTrue(TEXT("delayed switch commit is accepted"),
		F.Presentation->BeginPresentationSwitchCommit(F.Weapon1, 10.0f));
	TestTrue(TEXT("fast draw transition is accepted"),
		F.Presentation->BeginFastDrawTransition(F.Weapon1, 10.0f));
	TestTrue(TEXT("fast draw is pending"),
		F.Presentation->IsFastDrawTransitionPending());
	TestFalse(TEXT("pending switch work blocks the reload gate"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, false));

	F.Presentation->ResetPresentationSwitchState();
	TestFalse(TEXT("reset clears native switching"),
		F.Presentation->IsPresentationSwitching());
	TestFalse(TEXT("reset clears holster readiness"),
		F.Presentation->IsFullHolsterReadyToSwitch());
	TestFalse(TEXT("reset cancels the fast draw"),
		F.Presentation->IsFastDrawTransitionPending());
	TestTrue(TEXT("reset leaves the reload gate open"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, false));

	// Repeat on already-settled state: no new work, no recursive reset.
	F.Presentation->ResetPresentationSwitchState(true);
	TestFalse(TEXT("repeat reset keeps switching clear"),
		F.Presentation->IsPresentationSwitching());
	TestFalse(TEXT("repeat reset keeps fast draw clear"),
		F.Presentation->IsFastDrawTransitionPending());
	TestTrue(TEXT("repeat reset keeps the gate open"),
		F.Presentation->CanStartReloadPresentation(F.Weapon1, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationSwitchSwapTest,
	"Dust2.WeaponPresentation.SwitchingSurvivesValidSwap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationSwitchSwapTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	F.Presentation->SetPresentationSwitching(true);
	F.Presentation->SetWeaponForPresentation(F.Weapon2);
	TestTrue(TEXT("a valid weapon swap keeps the native switch session"),
		F.Presentation->IsPresentationSwitching());
	TestTrue(TEXT("the swap updates current weapon identity"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon2, F.Weapon2));

	F.Presentation->SetWeaponForPresentation(nullptr);
	TestFalse(TEXT("unbind clears the native switch session"),
		F.Presentation->IsPresentationSwitching());
	TestFalse(TEXT("unbind clears current weapon identity"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon2, F.Weapon2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationDrawWatchClearTest,
	"Dust2.WeaponPresentation.FinalDrawWatchClearsSwitching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationDrawWatchClearTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	F.Presentation->SetPresentationSwitching(true);
	TestFalse(TEXT("a final watch without montages does not claim to be watching"),
		F.Presentation->WatchDrawPresentationCompletion(
			F.Weapon1, nullptr, nullptr, nullptr, nullptr, true));
	TestFalse(TEXT("final draw completion clears native switching"),
		F.Presentation->IsPresentationSwitching());

	F.Presentation->SetPresentationSwitching(true);
	F.Presentation->InterruptDrawPresentationWatch();
	TestFalse(TEXT("an explicit draw interruption clears native switching"),
		F.Presentation->IsPresentationSwitching());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationResetRebindTest,
	"Dust2.WeaponPresentation.ResetRejectsCallbackRebind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationResetRebindTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;

	// A cancellation listener attempts to bind the cancelled request's weapon.
	F.Presentation->OnPresentationSwitchCommitCancelled.AddDynamic(
		F.Presentation, &UWeaponPresentationComponent::SetWeaponForPresentation);
	TestTrue(TEXT("request for another weapon is pending"),
		F.Presentation->BeginPresentationSwitchCommit(F.Weapon2, 10.0f));
	F.Presentation->ResetPresentationSwitchState();
	TestTrue(TEXT("reset rejects callback rebind"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon1, F.Weapon1));
	F.Presentation->OnPresentationSwitchCommitCancelled.RemoveDynamic(
		F.Presentation, &UWeaponPresentationComponent::SetWeaponForPresentation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationReadyCallbackTest,
	"Dust2.WeaponPresentation.ReadyCallbackCanClearToken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationReadyCallbackTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;

	FWeaponPresentationProfile Profile;
	F.Presentation->OnFullHolsterPresentationReady.AddDynamic(
		F.Presentation, &UWeaponPresentationComponent::ClearFullHolsterReadyToSwitch);
	TestTrue(TEXT("holster completes synchronously"),
		F.Presentation->StartFullHolsterPresentation(
			F.Weapon1, F.CharacterMesh, nullptr, Profile));
	TestFalse(TEXT("callback clears token without completion writing it back"),
		F.Presentation->IsFullHolsterReadyToSwitch());
	F.Presentation->OnFullHolsterPresentationReady.RemoveDynamic(
		F.Presentation, &UWeaponPresentationComponent::ClearFullHolsterReadyToSwitch);

	F.Presentation->SetPresentationSwitching(true);
	TestTrue(TEXT("next holster can publish a token"),
		F.Presentation->StartFullHolsterPresentation(
			F.Weapon1, F.CharacterMesh, nullptr, Profile));
	F.Presentation->ClearFullHolsterReadyToSwitch();
	TestFalse(TEXT("explicit clear invalidates readiness"),
		F.Presentation->IsFullHolsterReadyToSwitch());
	TestTrue(TEXT("clearing only readiness preserves switch session"),
		F.Presentation->IsPresentationSwitching());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPresentationGunPushbackTest,
	"Dust2.WeaponPresentation.GunPushbackContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponPresentationGunPushbackTest::RunTest(const FString& Parameters)
{
	FPresentationSwitchWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;

	F.Presentation->PlayNativeGunPushback(4.0f, 0.1f, 10.0f);
	TestTrue(TEXT("native pushback is opt-in"),
		F.Presentation->NativeGunPushbackLocation.IsNearlyZero());
	F.Presentation->bUseNativeGunPushback = true;
	F.Presentation->PlayNativeGunPushback(4.0f, 0.1f, 10.0f);
	F.Presentation->TickComponent(0.05f, LEVELTICK_All, nullptr);
	const double First = F.Presentation->NativeGunPushbackLocation.X;
	TestTrue(TEXT("shot moves the gun during attack"), First > 0.0f && First < 4.0f);

	F.Presentation->PlayNativeGunPushback(4.0f, 0.1f, 10.0f);
	TestEqual(TEXT("next shot starts at the current offset"),
		F.Presentation->NativeGunPushbackLocation.X, First);
	F.Presentation->TickComponent(0.05f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("second shot continues without snapping to zero"),
		F.Presentation->NativeGunPushbackLocation.X > First);
	F.Presentation->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("pushback returns toward neutral"),
		F.Presentation->NativeGunPushbackLocation.X < 4.0f);

	F.Presentation->SetWeaponForPresentation(F.Weapon2);
	TestTrue(TEXT("weapon change clears the visual offset"),
		F.Presentation->NativeGunPushbackLocation.IsNearlyZero());
	F.Presentation->PlayNativeGunPushback(4.0f, 0.0f, 10.0f);
	TestEqual(TEXT("zero attack duration applies the pushback immediately"),
		F.Presentation->NativeGunPushbackLocation.X, 4.0);
	F.Presentation->ResetPresentationSwitchState();
	TestTrue(TEXT("action reset clears the visual offset"),
		F.Presentation->NativeGunPushbackLocation.IsNearlyZero());
	return true;
}

#endif
