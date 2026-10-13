#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/ChildActorComponent.h"
#include "Dust2PlayerCharacter.h"
#include "WeaponInventoryComponent.h"
#include "WeaponPresentationComponent.h"
#include "CameraRecoilRecoveryComponent.h"
#include "UObject/UnrealType.h"

namespace
{
    struct FAimTestWorld
    {
        UWorld* World = nullptr;
        ADust2PlayerCharacter* Player = nullptr;
        UWeaponInventoryComponent* Inventory = nullptr;
        UWeaponPresentationComponent* Presentation = nullptr;
        AWeaponRuntime* Weapon1 = nullptr;
        AWeaponRuntime* Weapon2 = nullptr;
        bool bReady = false;

        FAimTestWorld()
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
            UCameraRecoilRecoveryComponent* Recoil =
                NewObject<UCameraRecoilRecoveryComponent>(Player);
            Inventory->RegisterComponent();
            Presentation->RegisterComponent();
            Recoil->RegisterComponent();
            UChildActorComponent* Slot1 = NewObject<UChildActorComponent>(Player);
            UChildActorComponent* Slot2 = NewObject<UChildActorComponent>(Player);
            Slot1->SetChildActorClass(AWeaponRuntime::StaticClass());
            Slot2->SetChildActorClass(AWeaponRuntime::StaticClass());
            Slot1->RegisterComponent();
            Slot2->RegisterComponent();
            Weapon1 = Cast<AWeaponRuntime>(Slot1->GetChildActor());
            Weapon2 = Cast<AWeaponRuntime>(Slot2->GetChildActor());
            if (!Weapon1 || !Weapon2) return;
            bReady = Player->InitializePlayerInventoryBridge(
                Inventory, Slot1, Slot2, Weapon1, Recoil);
            Presentation->SetWeaponForPresentation(Weapon1);
            bReady = bReady && Player->InitializeAimState(Presentation);
        }

        ~FAimTestWorld()
        {
            if (World)
            {
                World->DestroyWorld(false);
                if (GEngine) GEngine->DestroyWorldContext(World);
            }
        }

        void Release() { Player->SetAimInputPressed(false, false); }
        bool Press(bool bSwitching = false)
        { return Player->SetAimInputPressed(true, bSwitching); }
    };

    void SetAimTestInt(AWeaponRuntime* Weapon, FName Name, int32 Value)
    {
        if (FIntProperty* Property = FindFProperty<FIntProperty>(Weapon->GetClass(), Name))
            Property->SetPropertyValue_InContainer(Weapon, Value);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimInputTest, "Dust2.Aim.InputAndTransitionGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAimInputTest::RunTest(const FString& Parameters)
{
    // The native test pawn intentionally has no Blueprint mirror properties.
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    TestFalse(TEXT("missing configuration rejected"), F.Player->InitializeAimState(nullptr));
    TestTrue(TEXT("legal press enters"), F.Press());
    F.Player->CancelAim();
    TestFalse(TEXT("cancel exits"), F.Player->IsAiming());
    TestTrue(TEXT("cancel retains held latch"), F.Player->IsAimInputHeld());
    TestFalse(TEXT("repeat pressed does not reenter"), F.Press());
    F.Release();
    TestFalse(TEXT("release clears latch"), F.Player->IsAimInputHeld());
    TestFalse(TEXT("switching blocks"), F.Press(true));
    TestFalse(TEXT("blocked press is not queued"), F.Press());
    F.Release();
    TestTrue(TEXT("presentation commit pending"),
        F.Presentation->BeginPresentationSwitchCommit(F.Weapon1, 2.0f));
    TestFalse(TEXT("full transition blocks despite switching false"), F.Press());
    F.Presentation->CancelPresentationSwitchCommit(false);
    TestFalse(TEXT("completion does not auto aim"), F.Press());
    F.Release();
    SetAimTestInt(F.Weapon1, TEXT("CurrentAmmo"), 0);
    TestTrue(TEXT("empty weapon may still aim"), F.Press());
    F.Release();
    TestFalse(TEXT("release restores state"), F.Player->IsAiming());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimReloadTest, "Dust2.Aim.ReloadAndWeaponIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAimReloadTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    SetAimTestInt(F.Weapon1, TEXT("MagazineCapacity"), 10);
    SetAimTestInt(F.Weapon1, TEXT("CurrentAmmo"), 10);
    SetAimTestInt(F.Weapon1, TEXT("ReserveAmmo"), 20);
    TestTrue(TEXT("aim before rejected reload"), F.Press());
    TestFalse(TEXT("full magazine rejects reload"), F.Weapon1->TryStartReload());
    TestTrue(TEXT("rejected reload preserves aim"), F.Player->IsAiming());
    SetAimTestInt(F.Weapon1, TEXT("CurrentAmmo"), 4);
    TestTrue(TEXT("accepted reload"), F.Weapon1->TryStartReload());
    TestFalse(TEXT("accepted reload immediately cancels aim"), F.Player->IsAiming());
    F.Release();
    TestFalse(TEXT("reload blocks a new press"), F.Press());
    F.Weapon1->CancelReload();
    TestFalse(TEXT("cancel reload does not resume held aim"), F.Press());
    F.Release();
    TestTrue(TEXT("repress after reload"), F.Press());
    TestTrue(TEXT("actual switch accepted"), F.Inventory->TrySwitchWeaponNative());
    TestFalse(TEXT("actual switch cancels"), F.Player->IsAiming());
    F.Presentation->SetWeaponForPresentation(F.Weapon2);
    F.Release();
    TestTrue(TEXT("new weapon aims"), F.Press());
    F.Weapon1->OnReloadStarted.Broadcast();
    TestTrue(TEXT("old weapon callback detached"), F.Player->IsAiming());
    F.Weapon2->OnReloadStarted.Broadcast();
    TestFalse(TEXT("current weapon callback cancels"), F.Player->IsAiming());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimLifecycleTest, "Dust2.Aim.DeathRestoreAndDestroyedWeapon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAimLifecycleTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    TestTrue(TEXT("aim before death"), F.Press());
    bool bDiedNow = false;
    F.Player->ApplyDamage(100.0f, bDiedNow);
    TestTrue(TEXT("death confirmed"), bDiedNow);
    TestFalse(TEXT("death cancels aim"), F.Player->IsAiming());
    F.Release();
    TestFalse(TEXT("dead player cannot aim"), F.Press());
    F.Player->RestoreHealth(100.0f);
    TestFalse(TEXT("restore does not resume held input"), F.Press());
    F.Release();
    TestTrue(TEXT("repress after restoration"), F.Press());
    F.Player->RestoreHealth(80.0f);
    TestFalse(TEXT("snapshot restoration clears aim"), F.Player->IsAiming());
    F.Release();
    TestTrue(TEXT("aim before weapon destruction"), F.Press());
    F.Weapon1->Destroy();
    TestFalse(TEXT("destroyed weapon clears aim"), F.Player->IsAiming());
    F.Release();
    TestFalse(TEXT("missing weapon cannot aim"), F.Press());
    return true;
}
#endif
