#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Presentation/WeaponVisualRecoilComponent.h"
#include "Weapons/WeaponRuntime.h"
#include "Inventory/WeaponInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include <limits>

namespace
{
	struct FVisualRecoilWorld
	{
		UWorld* World = nullptr;
		FVisualRecoilWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
			if (World)
			{
				if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			}
		}
		~FVisualRecoilWorld()
		{
			if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); }
		}
	};
	UWeaponVisualRecoilData* TestData()
	{
		UWeaponVisualRecoilData* Data = NewObject<UWeaponVisualRecoilData>();
		Data->SingleRotation = NewObject<UCurveVector>(Data);
		Data->SingleLocation = NewObject<UCurveVector>(Data);
		for (UCurveVector* Curve : {Data->SingleRotation.Get(), Data->SingleLocation.Get()})
		{
			for (FRichCurve& Axis : Curve->FloatCurves)
			{
				Axis.AddKey(0, 0); Axis.AddKey(0.05f, 1); Axis.AddKey(0.2f, 0);
			}
		}
		Data->Hip.Pitch = FVector2D(3, 3);
		Data->Hip.KickBack = FVector2D(-1, -1);
		Data->Aim = Data->Hip;
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualRecoilValidationTest, "Dust2.Presentation.VisualRecoil.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVisualRecoilValidationTest::RunTest(const FString&)
{
	UWeaponVisualRecoilData* Data = TestData();
	TestTrue(TEXT("Authored curve data usable"), Data->IsUsable());
	Data->PlayRate = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Reject NaN play rate"), Data->IsUsable());
	Data->PlayRate = 1;
	Data->PushbackDamping = Data->PushbackInterpSpeed = 0;
	Data->NoiseDamping = Data->NoiseInterpSpeed = FVector2D::ZeroVector;
	TestTrue(TEXT("Disabled layers allow zero speeds and no auto curves"), Data->IsUsable());
	Data->PushbackAmount = -1;
	TestFalse(TEXT("Active pushback requires positive speeds"), Data->IsUsable());
	Data->PushbackAmount = 0;
	Data->NoiseAmount.X = 1;
	TestFalse(TEXT("Active noise axis requires positive speeds"), Data->IsUsable());
	Data->NoiseDamping.X = Data->NoiseInterpSpeed.X = 10;
	TestTrue(TEXT("Inactive noise axis can retain zero speeds"), Data->IsUsable());
	Data->NoiseDamping.Y = -1;
	TestFalse(TEXT("Disabled axis still rejects negative speed"), Data->IsUsable());
	Data->NoiseDamping.Y = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Disabled axis still rejects NaN speed"), Data->IsUsable());
	Data->NoiseDamping.Y = 0;
	Data->SingleRotation = nullptr;
	TestFalse(TEXT("Missing mandatory curve fails closed"), Data->IsUsable());
#if WITH_EDITOR && WITH_EDITORONLY_DATA
	UWeaponVisualRecoilData* Destination = TestData();
	UCurveVector* PreservedCurve = Destination->SingleRotation;
	Destination->PushbackAmount = -2;
	Destination->LegacyImportSource = Data; // Wrong schema must not overwrite a configured destination.
	Destination->ImportLegacyVisualRecoil();
	TestTrue(TEXT("Failed import preserves curve"), Destination->SingleRotation.Get() == PreservedCurve);
	TestEqual(TEXT("Failed import preserves parameters"), Destination->PushbackAmount, -2.0f);
	TestNotNull(TEXT("Failed import retains source for correction"), Destination->LegacyImportSource.Get());
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualRecoilLifecycleTest, "Dust2.Presentation.VisualRecoil.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVisualRecoilLifecycleTest::RunTest(const FString&)
{
	FVisualRecoilWorld TestWorld;
	if (!TestNotNull(TEXT("World"), TestWorld.World)) return false;
	AActor* Owner = TestWorld.World->SpawnActor<AActor>();
	AWeaponRuntime* Weapon = TestWorld.World->SpawnActor<AWeaponRuntime>();
	if (!TestNotNull(TEXT("Owner"), Owner) || !TestNotNull(TEXT("Weapon"), Weapon)) return false;
	Weapon->SetOwner(Owner);
	UWeaponVisualRecoilComponent* Visual = NewObject<UWeaponVisualRecoilComponent>(Owner);
	Owner->AddInstanceComponent(Visual); Visual->RegisterComponent();
	TestFalse(TEXT("Cannot play before init"), Visual->PlayVisualRecoil());
	TestTrue(TEXT("Valid init"), Visual->InitializeVisualRecoil(Weapon, TestData()));
	const int32 Ammo = Weapon->GetCurrentAmmo();
	TestTrue(TEXT("Shot starts visual"), Visual->PlayVisualRecoil());
	Visual->TickComponent(0.03f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Curve moves gun body"), Visual->RecoilAnimation.Equals(FTransform::Identity));
	const FTransform BeforeShot = Visual->RecoilAnimation;
	TestTrue(TEXT("Second shot accepted"), Visual->PlayVisualRecoil());
	TestTrue(TEXT("Shot restart preserves visible pose at the event"), Visual->RecoilAnimation.Equals(BeforeShot));
	TestEqual(TEXT("Presentation never consumes ammo"), Weapon->GetCurrentAmmo(), Ammo);
	Visual->StopVisualRecoil();
	for (int32 I = 0; I < 180; ++I) Visual->TickComponent(1.0f / 60, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Release returns to neutral"), Visual->RecoilAnimation.Equals(FTransform::Identity, 0.001f));
	TestFalse(TEXT("Settled component stops ticking"), Visual->IsComponentTickEnabled());
	Visual->PlayVisualRecoil(); Visual->TickComponent(0.03f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Null rebind rejected"), Visual->InitializeVisualRecoil(nullptr, TestData()));
	TestTrue(TEXT("Failed rebind clears previous weapon output"), Visual->RecoilAnimation.Equals(FTransform::Identity));
	TestFalse(TEXT("Failed rebind cannot keep firing old visual"), Visual->PlayVisualRecoil());
	Visual->InitializeVisualRecoil(Weapon, TestData()); Visual->PlayVisualRecoil();
	Weapon->SetOwner(nullptr);
	Visual->TickComponent(0.03f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Dropped weapon clears visual"), Visual->RecoilAnimation.Equals(FTransform::Identity));
	TestFalse(TEXT("Dropped weapon disables tick"), Visual->IsComponentTickEnabled());
	Weapon->SetOwner(Owner);
	TestTrue(TEXT("Owner binding restored"), Visual->InitializeVisualRecoil(Weapon, TestData()));
	Visual->PlayVisualRecoil();
	UWeaponInventoryComponent* Inventory = NewObject<UWeaponInventoryComponent>(Owner);
	Owner->AddInstanceComponent(Inventory); Inventory->RegisterComponent();
	Visual->TickComponent(0.03f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Inventory with no equipped weapon clears output despite matching Actor owner"), Visual->RecoilAnimation.Equals(FTransform::Identity));
	TestFalse(TEXT("Actor owner cannot bypass inventory equipment state"), Visual->InitializeVisualRecoil(Weapon, TestData()));
	TestFalse(TEXT("Unequipped inventory weapon cannot play"), Visual->PlayVisualRecoil());
	return true;
}
#endif
