#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "UObject/UnrealType.h"
#include "Presentation/WeaponPresentationComponent.h"
#include "Presentation/WeaponPresentationTypes.h"
#include "Weapons/WeaponRuntime.h"

namespace
{
	bool SetVisualWeaponBaseClass(
		UWeaponPresentationComponent* Presentation,
		UClass* BaseClass)
	{
		if (!Presentation)
		{
			return false;
		}
		FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
			Presentation->GetClass(), TEXT("VisualWeaponBaseClass"));
		if (!Property)
		{
			return false;
		}
		Property->SetObjectPropertyValue_InContainer(
			Presentation, BaseClass);
		return true;
	}

	bool SetWeaponPresentationProfiles(
		UWeaponPresentationComponent* Presentation,
		const TArray<FWeaponPresentationProfile>& Profiles)
	{
		if (!Presentation)
		{
			return false;
		}
		FArrayProperty* Property = FindFProperty<FArrayProperty>(
			Presentation->GetClass(), TEXT("WeaponPresentationProfiles"));
		FStructProperty* Inner = Property
			? CastField<FStructProperty>(Property->Inner)
			: nullptr;
		if (!Inner
			|| Inner->Struct != FWeaponPresentationProfile::StaticStruct())
		{
			return false;
		}

		FScriptArrayHelper Array(
			Property,
			Property->ContainerPtrToValuePtr<void>(Presentation));
		Array.EmptyValues();
		for (const FWeaponPresentationProfile& Profile : Profiles)
		{
			const int32 Index = Array.AddValue();
			Inner->CopyCompleteValue(Array.GetRawPtr(Index), &Profile);
		}
		return true;
	}

	/**
	 * Isolated native fixture for the binding transaction. The visual profile uses
	 * the plain AActor class, so no Blueprint/asset dependency is needed: the real
	 * UChildActorComponent path is still exercised.
	 */
	struct FVisualBindingWorld
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		AActor* OtherOwner = nullptr;
		AWeaponRuntime* Weapon1 = nullptr;
		AWeaponRuntime* Weapon2 = nullptr;
		UWeaponPresentationComponent* Presentation = nullptr;
		UChildActorComponent* VisualSlot = nullptr;
		UChildActorComponent* OtherSlot = nullptr;
		FWeaponPresentationProfile Profile;
		bool bReady = false;
		int64 BindingId = 0;

		FVisualBindingWorld()
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
			OtherOwner = World->SpawnActor<AActor>();
			Weapon1 = World->SpawnActor<AWeaponRuntime>();
			Weapon2 = World->SpawnActor<AWeaponRuntime>();
			if (!Owner || !OtherOwner || !Weapon1 || !Weapon2)
			{
				return;
			}

			USceneComponent* Root = NewObject<USceneComponent>(Owner);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();

			Presentation = NewObject<UWeaponPresentationComponent>(Owner);
			Presentation->RegisterComponent();

			VisualSlot = NewObject<UChildActorComponent>(Owner);
			VisualSlot->SetupAttachment(Root);
			VisualSlot->RegisterComponent();

			USceneComponent* OtherRoot =
				NewObject<USceneComponent>(OtherOwner);
			OtherOwner->SetRootComponent(OtherRoot);
			OtherRoot->RegisterComponent();

			OtherSlot = NewObject<UChildActorComponent>(OtherOwner);
			OtherSlot->SetupAttachment(OtherRoot);
			OtherSlot->SetChildActorClass(AActor::StaticClass());
			OtherSlot->RegisterComponent();

			Profile.FormalWeaponClass = AWeaponRuntime::StaticClass();
			Profile.VisualWeaponClass = AActor::StaticClass();
			Profile.bUseFullHolster = true;
			Profile.FastDrawTransitionTime = 0.25f;

			bReady = Presentation->IsRegistered()
				&& VisualSlot->IsRegistered()
				&& OtherSlot->IsRegistered()
				&& SetVisualWeaponBaseClass(
					Presentation, AActor::StaticClass())
				&& ApplyProfiles();
		}

		~FVisualBindingWorld()
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

		bool ApplyProfiles()
		{
			if (!Presentation)
			{
				return false;
			}
			TArray<FWeaponPresentationProfile> Profiles;
			Profiles.Add(Profile);
			return SetWeaponPresentationProfiles(Presentation, Profiles);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponVisualBindingOwnedSuccessTest,
	"Dust2.WeaponVisualBinding.OwnedSuccessAndReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponVisualBindingOwnedSuccessTest::RunTest(const FString& Parameters)
{
	FVisualBindingWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	AActor* OutVisual = nullptr;
	FWeaponPresentationProfile OutProfile;
	if (!TestTrue(TEXT("owned formal weapon binds on the first request"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, OutVisual, OutProfile, F.BindingId))) return false;
	TestTrue(TEXT("success returns the current child actor"),
		IsValid(OutVisual) && OutVisual == F.VisualSlot->GetChildActor());
	TestTrue(TEXT("success returns the resolved profile"),
		OutProfile.VisualWeaponClass.Get() == AActor::StaticClass()
		&& OutProfile.bUseFullHolster
		&& FMath::IsNearlyEqual(
			OutProfile.FastDrawTransitionTime, 0.25f));
	TestTrue(TEXT("the exact returned actor is current"),
		F.Presentation->IsCurrentVisualWeapon(F.Weapon1, OutVisual, F.BindingId));
	TestFalse(TEXT("successful activation shows the visual"),
		OutVisual->IsHidden());
	TestTrue(TEXT("the formal gameplay weapon stays bound"),
		F.Presentation->IsCurrentPresentationWeapon(F.Weapon1, F.Weapon1));

	AActor* FirstVisual = OutVisual;
	const int64 FirstBindingId = F.BindingId;
	AActor* ReusedVisual = nullptr;
	FWeaponPresentationProfile ReusedProfile;
	TestTrue(TEXT("a repeated same request succeeds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, ReusedVisual, ReusedProfile, F.BindingId));
	TestTrue(TEXT("a repeated same request reuses the same actor"),
		ReusedVisual == FirstVisual);
	TestFalse(TEXT("a delayed result from the old batch is rejected even for the reused actor"),
		F.Presentation->IsCurrentVisualWeapon(F.Weapon1, FirstVisual, FirstBindingId));
	TestTrue(TEXT("the reused actor is still current"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, FirstVisual, F.BindingId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponVisualBindingSwapTest,
	"Dust2.WeaponVisualBinding.SwapRetainsSwitchSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponVisualBindingSwapTest::RunTest(const FString& Parameters)
{
	FVisualBindingWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	AActor* FirstVisual = nullptr;
	FWeaponPresentationProfile FirstProfile;
	if (!TestTrue(TEXT("first weapon binds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, FirstVisual, FirstProfile, F.BindingId)))
	{
		return false;
	}

	F.Presentation->SetPresentationSwitching(true);
	AActor* SwappedVisual = nullptr;
	FWeaponPresentationProfile SwappedProfile;
	TestTrue(TEXT("a valid formal swap succeeds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon2, F.VisualSlot, SwappedVisual, SwappedProfile, F.BindingId));
	TestTrue(TEXT("the unchanged visual class reuses the actor"),
		SwappedVisual == FirstVisual);
	TestTrue(TEXT("the native switch latch survives a valid swap"),
		F.Presentation->IsPresentationSwitching());
	TestTrue(TEXT("the new formal weapon owns the visual identity"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon2, SwappedVisual, F.BindingId));
	TestFalse(TEXT("the old formal weapon is no longer current"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, SwappedVisual, F.BindingId));
	TestTrue(TEXT("the gameplay binding follows the swap"),
		F.Presentation->IsCurrentPresentationWeapon(
			F.Weapon2, F.Weapon2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponVisualBindingInvalidConfigTest,
	"Dust2.WeaponVisualBinding.InvalidVisualConfigFailsOwnedSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponVisualBindingInvalidConfigTest::RunTest(const FString& Parameters)
{
	FVisualBindingWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	AActor* BoundVisual = nullptr;
	FWeaponPresentationProfile BoundProfile;
	if (!TestTrue(TEXT("seed bind succeeds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, BoundVisual, BoundProfile, F.BindingId)))
	{
		return false;
	}

	F.Presentation->SetPresentationSwitching(true);
	const int64 OldBindingId = F.BindingId;

	// Missing visual configuration fails presentation without losing gameplay ownership.
	F.Profile.VisualWeaponClass = nullptr;
	TestTrue(TEXT("invalid profile applied"), F.ApplyProfiles());
	AActor* OutVisual = BoundVisual;
	FWeaponPresentationProfile OutProfile = F.Profile;
	TestFalse(TEXT("invalid config fails the binding"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, OutVisual, OutProfile, F.BindingId));
	TestNull(TEXT("failure clears the visual output"), OutVisual);
	TestEqual(TEXT("failure clears the binding id"), F.BindingId, int64(0));
	TestNull(TEXT("failure clears the profile output"),
		OutProfile.VisualWeaponClass.Get());
	TestTrue(TEXT("the formal weapon remains bound after visual failure"),
		F.Presentation->IsCurrentPresentationWeapon(
			F.Weapon1, F.Weapon1));
	TestFalse(TEXT("the previous visual session is no longer current"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, OldBindingId));
	TestTrue(TEXT("the owned old visual is hidden"), BoundVisual->IsHidden());
	TestTrue(TEXT("the child actor is not destroyed"),
		F.VisualSlot->GetChildActor() == BoundVisual);
	TestFalse(TEXT("the failed switch latch cannot stay stuck"),
		F.Presentation->IsPresentationSwitching());

	// Repeating an invalid request must leave the slot safely cleared.
	TestTrue(TEXT("missing visual profile applied"), F.ApplyProfiles());
	OutVisual = BoundVisual;
	OutProfile = F.Profile;
	TestFalse(TEXT("missing visual config fails the binding"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, OutVisual, OutProfile, F.BindingId));
	TestNull(TEXT("missing visual clears the visual output"), OutVisual);
	TestNull(TEXT("missing visual clears the profile output"),
		OutProfile.VisualWeaponClass.Get());
	TestTrue(TEXT("the formal weapon remains bound after missing visual"),
		F.Presentation->IsCurrentPresentationWeapon(
			F.Weapon1, F.Weapon1));
	TestTrue(TEXT("the old owned visual stays hidden"), BoundVisual->IsHidden());

	// A later valid request must be able to recover the same slot.
	F.Profile.VisualWeaponClass = AActor::StaticClass();
	TestTrue(TEXT("valid profile restored"), F.ApplyProfiles());
	AActor* RecoveredVisual = nullptr;
	FWeaponPresentationProfile RecoveredProfile;
	if (!TestTrue(TEXT("a valid request recovers the slot"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, RecoveredVisual, RecoveredProfile, F.BindingId))) return false;
	TestTrue(TEXT("recovery reuses the owned child actor"),
		RecoveredVisual == BoundVisual);
	TestFalse(TEXT("recovery shows the child actor"),
		RecoveredVisual->IsHidden());
	TestTrue(TEXT("recovery restores identity"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, RecoveredVisual, F.BindingId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponVisualBindingForeignSlotTest,
	"Dust2.WeaponVisualBinding.ForeignAndUnregisteredSlotRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponVisualBindingForeignSlotTest::RunTest(const FString& Parameters)
{
	FVisualBindingWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	AActor* BoundVisual = nullptr;
	FWeaponPresentationProfile BoundProfile;
	if (!TestTrue(TEXT("seed bind succeeds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, BoundVisual, BoundProfile, F.BindingId)))
	{
		return false;
	}
	F.Presentation->SetPresentationSwitching(true);
	const int64 ActiveBindingId = F.BindingId;

	AActor* OtherVisual = F.OtherSlot->GetChildActor();
	if (!TestTrue(TEXT("foreign slot has its own visual"),
		IsValid(OtherVisual)))
	{
		return false;
	}

	AActor* OutVisual = BoundVisual;
	FWeaponPresentationProfile OutProfile = F.Profile;
	TestFalse(TEXT("a foreign-owned slot is rejected"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.OtherSlot, OutVisual, OutProfile, F.BindingId));
	TestNull(TEXT("foreign rejection clears the visual output"), OutVisual);
	TestNull(TEXT("foreign rejection clears the profile output"),
		OutProfile.VisualWeaponClass.Get());
	TestTrue(TEXT("the active visual session is unchanged"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, ActiveBindingId));
	TestFalse(TEXT("the other owner's visual is not hidden"),
		OtherVisual->IsHidden());
	TestTrue(TEXT("the switch latch is not cleared by a rejected argument"),
		F.Presentation->IsPresentationSwitching());
	TestTrue(TEXT("the bound formal weapon is unchanged"),
		F.Presentation->IsCurrentPresentationWeapon(
			F.Weapon1, F.Weapon1));

	UChildActorComponent* Unregistered =
		NewObject<UChildActorComponent>(F.Owner);
	OutVisual = BoundVisual;
	OutProfile = F.Profile;
	TestFalse(TEXT("an unregistered slot is rejected"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, Unregistered, OutVisual, OutProfile, F.BindingId));
	TestNull(TEXT("unregistered rejection clears the visual output"),
		OutVisual);
	TestNull(TEXT("unregistered rejection clears the profile output"),
		OutProfile.VisualWeaponClass.Get());
	TestTrue(TEXT("the active visual session survives rejection"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, ActiveBindingId));

	OutVisual = BoundVisual;
	OutProfile = F.Profile;
	TestFalse(TEXT("a null formal weapon is rejected"),
		F.Presentation->TryBindVisualWeaponPresentation(
			nullptr, F.VisualSlot, OutVisual, OutProfile, F.BindingId));
	TestNull(TEXT("null formal rejection clears the visual output"),
		OutVisual);
	TestTrue(TEXT("the active visual session survives null formal"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, ActiveBindingId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponVisualBindingClearUnbindTest,
	"Dust2.WeaponVisualBinding.ClearAndUnbindInvalidateIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponVisualBindingClearUnbindTest::RunTest(const FString& Parameters)
{
	FVisualBindingWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	AActor* BoundVisual = nullptr;
	FWeaponPresentationProfile BoundProfile;
	if (!TestTrue(TEXT("seed bind succeeds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, BoundVisual, BoundProfile, F.BindingId)))
	{
		return false;
	}
	F.Presentation->SetPresentationSwitching(true);

	F.Presentation->ClearVisualWeaponPresentation();
	TestFalse(TEXT("clear invalidates the tracked identity"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, F.BindingId));
	TestTrue(TEXT("clear hides the tracked owned actor"),
		BoundVisual->IsHidden());
	TestTrue(TEXT("clear does not destroy the child actor"),
		F.VisualSlot->GetChildActor() == BoundVisual);
	TestTrue(TEXT("clear leaves the gameplay binding alone"),
		F.Presentation->IsCurrentPresentationWeapon(
			F.Weapon1, F.Weapon1));
	TestTrue(TEXT("clear does not blindly reset the switch latch"),
		F.Presentation->IsPresentationSwitching());

	F.Presentation->SetWeaponForPresentation(nullptr);
	TestFalse(TEXT("unbind invalidates the tracked identity"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, F.BindingId));
	TestFalse(TEXT("unbind clears the native switch latch"),
		F.Presentation->IsPresentationSwitching());
	TestFalse(TEXT("unbind clears the gameplay binding"),
		F.Presentation->IsCurrentPresentationWeapon(
			F.Weapon1, F.Weapon1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponVisualBindingReplacedChildTest,
	"Dust2.WeaponVisualBinding.ReplacedChildFailsIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponVisualBindingReplacedChildTest::RunTest(const FString& Parameters)
{
	FVisualBindingWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	AActor* BoundVisual = nullptr;
	FWeaponPresentationProfile BoundProfile;
	if (!TestTrue(TEXT("seed bind succeeds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, BoundVisual, BoundProfile, F.BindingId)))
	{
		return false;
	}

	F.VisualSlot->SetChildActorClass(nullptr);
	TestFalse(TEXT("a destroyed child fails the identity predicate"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, F.BindingId));

	F.VisualSlot->SetChildActorClass(AActor::StaticClass());
	AActor* Replacement = F.VisualSlot->GetChildActor();
	TestTrue(TEXT("the replacement child was constructed"),
		IsValid(Replacement));
	TestFalse(TEXT("an untracked replacement is not the current visual"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, Replacement, F.BindingId));
	TestFalse(TEXT("null is never the current visual"),
		F.Presentation->IsCurrentVisualWeapon(F.Weapon1, nullptr, F.BindingId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponVisualBindingResetSessionTest,
	"Dust2.WeaponVisualBinding.ResetInvalidatesSettledSessionWithoutHiding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponVisualBindingResetSessionTest::RunTest(const FString& Parameters)
{
	FVisualBindingWorld F;
	if (!TestTrue(TEXT("fixture ready"), F.bReady))
	{
		return false;
	}

	AActor* BoundVisual = nullptr;
	FWeaponPresentationProfile BoundProfile;
	if (!TestTrue(TEXT("seed bind succeeds"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, BoundVisual, BoundProfile, F.BindingId)))
	{
		return false;
	}
	F.Presentation->SetPresentationSwitching(true);

	F.Presentation->ResetPresentationSwitchState();
	TestFalse(TEXT("reset invalidates the tracked session"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, BoundVisual, F.BindingId));
	TestFalse(TEXT("reset does not hide a settled visual"),
		BoundVisual->IsHidden());
	TestTrue(TEXT("reset keeps the gameplay binding"),
		F.Presentation->IsCurrentPresentationWeapon(
			F.Weapon1, F.Weapon1));
	TestFalse(TEXT("reset clears the native switch latch"),
		F.Presentation->IsPresentationSwitching());

	AActor* ReboundVisual = nullptr;
	FWeaponPresentationProfile ReboundProfile;
	TestTrue(TEXT("a later bind can re-establish the session"),
		F.Presentation->TryBindVisualWeaponPresentation(
			F.Weapon1, F.VisualSlot, ReboundVisual, ReboundProfile, F.BindingId));
	TestTrue(TEXT("the re-bind reuses the same child actor"),
		ReboundVisual == BoundVisual);
	TestTrue(TEXT("the re-bind restores the identity predicate"),
		F.Presentation->IsCurrentVisualWeapon(
			F.Weapon1, ReboundVisual, F.BindingId));
	return true;
}

#endif
