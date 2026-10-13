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
    // Keep the previous explicit cancel-on-reload policy covered as well.
    if (FBoolProperty* Policy = FindFProperty<FBoolProperty>(
        F.Player->GetClass(), TEXT("bKeepAimDuringReload")))
        Policy->SetPropertyValue_InContainer(F.Player, false);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimKeepReloadTest, "Dust2.Aim.KeepAimDuringReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAimKeepReloadTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    SetAimTestInt(F.Weapon1, TEXT("MagazineCapacity"), 10);
    SetAimTestInt(F.Weapon1, TEXT("CurrentAmmo"), 4);
    SetAimTestInt(F.Weapon1, TEXT("ReserveAmmo"), 20);
    TestTrue(TEXT("aim before reload"), F.Press());
    TestTrue(TEXT("reload accepted while aimed"), F.Weapon1->TryStartReload());
    TestTrue(TEXT("A policy retains aim during reload"), F.Player->IsAiming());
    TestTrue(TEXT("formal reload transaction remains active"), F.Weapon1->IsReloading());
    TestEqual(TEXT("starting reload does not grant ammo early"), F.Weapon1->GetCurrentAmmo(), 4);
    TestTrue(TEXT("finish reload through original authority"), F.Weapon1->FinishReload());
    TestTrue(TEXT("A policy stays aimed without another press"), F.Player->IsAiming());
    TestEqual(TEXT("original ammo commit retained"), F.Weapon1->GetCurrentAmmo(), 10);
    SetAimTestInt(F.Weapon1, TEXT("CurrentAmmo"), 4);
    TestTrue(TEXT("second aimed reload accepted"), F.Weapon1->TryStartReload());
    F.Release();
    TestFalse(TEXT("release during reload exits aim"), F.Player->IsAiming());
    TestTrue(TEXT("right press during reload enters aim"), F.Press());
    F.Release();
    TestFalse(TEXT("release during reload exits again"), F.Player->IsAiming());
    TestTrue(TEXT("right press during reload can enter repeatedly"), F.Press());
    F.Weapon1->FinishReload();
    TestTrue(TEXT("aim entered during reload remains at completion"), F.Player->IsAiming());
    F.Release();
    TestTrue(TEXT("repress after completion enters"), F.Press());
    TestTrue(TEXT("switch still accepted"), F.Inventory->TrySwitchWeaponNative());
    TestFalse(TEXT("A policy does not weaken switch cancellation"), F.Player->IsAiming());
    return true;
}
namespace
{
    UWeaponDefinitionData* ConfigureDualSpread(FAimTestWorld& F)
    {
        UWeaponDefinitionData* D = NewObject<UWeaponDefinitionData>(F.Weapon1);
        D->FireInterval = 0.01f;
        D->RecoilData = nullptr;
        D->bUseAimingSpread = true;
        D->StationarySpreadAngle = D->MovingSpreadAngle =
            D->JumpSpreadAngle = D->SlowWalkSpreadAngle = 6.0f;
        D->AimingSpread.StationarySpreadAngle = D->AimingSpread.MovingSpreadAngle =
            D->AimingSpread.JumpSpreadAngle = D->AimingSpread.SlowWalkSpreadAngle = 0.0f;
        F.Weapon1->WeaponDefinition = D;
        F.Weapon1->ApplyDefinitionSettings();
        return D;
    }

    bool HasSpread(AWeaponRuntime* Weapon, float Speed = 0.0f, bool bFalling = false)
    {
        bool bVaried = false;
        for (int32 Index = 0; Index < 32; ++Index)
        {
            FVector Direction;
            bool bGenerated = false;
            Weapon->GenerateShotDirection(FVector::ForwardVector, Speed,
                bFalling, false, false, Direction, bGenerated);
            if (!bGenerated) return false;
            bVaried |= !Direction.Equals(FVector::ForwardVector, 0.00001f);
        }
        return bVaried;
    }

    bool AllDirectionsExact(AWeaponRuntime* Weapon, float Speed = 0.0f, bool bFalling = false)
    {
        for (int32 Index = 0; Index < 32; ++Index)
        {
            FVector Direction;
            bool bGenerated = false;
            Weapon->GenerateShotDirection(FVector::ForwardVector, Speed,
                bFalling, false, false, Direction, bGenerated);
            if (!bGenerated || !Direction.Equals(FVector::ForwardVector, 0.00001f)) return false;
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualSpreadSnapshotTest, "Dust2.Aim.DualSpread.ShotSnapshotAndFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDualSpreadSnapshotTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    UWeaponDefinitionData* D = ConfigureDualSpread(F);
    TestTrue(TEXT("hip shot accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("aim after hip shot commit"), F.Press());
    TestTrue(TEXT("already committed hip shot retains hip cone"), HasSpread(F.Weapon1));
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("aim shot accepted"), F.Weapon1->TryCommitFire());
    F.Release();
    TestTrue(TEXT("committed aim shot stays exact after release"), AllDirectionsExact(F.Weapon1));
    TestTrue(TEXT("re-enter aim"), F.Press());
    D->bUseAimingSpread = false;
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("fallback shot accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("disabled feature uses legacy hip spread while aiming"), HasSpread(F.Weapon1));
    D->bUseAimingSpread = true;
    UWeaponDefinitionData* Captured = NewObject<UWeaponDefinitionData>();
    F.Weapon1->CaptureDefinition(Captured);
    TestTrue(TEXT("capture preserves aim opt-in"), Captured->bUseAimingSpread);
    TestEqual(TEXT("capture preserves aim spread"), Captured->AimingSpread.MovingSpreadAngle, 0.0f);
    TestEqual(TEXT("shared hip definition was not mutated by shooting"), D->StationarySpreadAngle, 6.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualSpreadBloomTest, "Dust2.Aim.DualSpread.BloomDoesNotResetOnToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDualSpreadBloomTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    UWeaponDefinitionData* D = ConfigureDualSpread(F);
    D->bUseFiringSpread = D->AimingSpread.bUseFiringSpread = true;
    D->SpreadPerShot = 0.5f;
    D->MaxFiringSpread = 2.0f;
    D->SpreadRecoveryDelay = D->AimingSpread.SpreadRecoveryDelay = 0.5f;
    D->SpreadRecoverySpeed = D->AimingSpread.SpreadRecoverySpeed = 1.0f;
    D->AimingSpread.SpreadPerShot = 0.1f;
    D->AimingSpread.MaxFiringSpread = 0.4f;
    TestTrue(TEXT("hip accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("hip bloom advances"), FMath::IsNearlyEqual(F.Weapon1->GetCurrentFiringSpread(), 0.5f));
    F.Press();
    TestTrue(TEXT("aim bloom also remembers hip shot"), FMath::IsNearlyEqual(F.Weapon1->GetCurrentFiringSpread(), 0.1f));
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("aim accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("aim bloom advances"), FMath::IsNearlyEqual(F.Weapon1->GetCurrentFiringSpread(), 0.2f));
    F.Release();
    TestTrue(TEXT("hip remembers aim shot"), FMath::IsNearlyEqual(F.Weapon1->GetCurrentFiringSpread(), 1.0f));
    F.Press();
    TestTrue(TEXT("toggle does not clear aim bloom"), FMath::IsNearlyEqual(F.Weapon1->GetCurrentFiringSpread(), 0.2f));
    SetAimTestInt(F.Weapon1, TEXT("CurrentAmmo"), 0);
    TestFalse(TEXT("empty shot rejected"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("rejected shot does not grow aim bloom"), FMath::IsNearlyEqual(F.Weapon1->GetCurrentFiringSpread(), 0.2f));
    for (int32 Frame = 0; Frame < 15; ++Frame)
        F.World->Tick(LEVELTICK_All, 0.05f);
    TestTrue(TEXT("aim bloom recovers by its own profile"), FMath::IsNearlyZero(F.Weapon1->GetCurrentFiringSpread()));
    F.Release();
    TestTrue(TEXT("hip bloom recovers independently"), FMath::IsNearlyEqual(F.Weapon1->GetCurrentFiringSpread(), 0.75f, 0.001f));
    F.Weapon1->ResetRecoilPattern();
    TestTrue(TEXT("reset clears hip"), FMath::IsNearlyZero(F.Weapon1->GetCurrentFiringSpread()));
    F.Press();
    TestTrue(TEXT("reset also clears aim"), FMath::IsNearlyZero(F.Weapon1->GetCurrentFiringSpread()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualSpreadSniperTest, "Dust2.Aim.DualSpread.SniperExactAimRandomHip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDualSpreadSniperTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    UWeaponDefinitionData* D = ConfigureDualSpread(F);
    D->bIgnoreShotRecoilWhenAiming = true;
    D->RecoilData = NewObject<UWeaponRecoilData>(D);
    D->RecoilData->bUseFixedPattern = true;
    D->RecoilData->bUseIndependentShotRecoil = true;
    D->RecoilData->FixedPattern = {FVector2D(3.0f, 0.0f)};
    D->RecoilData->FixedPatternPitchScale = 1.0f;
    D->RecoilData->FixedPatternYawScale = 1.0f;
    D->RecoilData->PatternResetDelay = 100.0f;
    F.Weapon1->ApplyDefinitionSettings();
    TestTrue(TEXT("hip shot accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("sniper hip is random"), HasSpread(F.Weapon1));
    TestTrue(TEXT("sniper enters aim"), F.Press());
    for (int32 Shot = 0; Shot < 3; ++Shot)
    {
        F.World->Tick(LEVELTICK_All, 0.02f);
        TestTrue(TEXT("aim shot accepted"), F.Weapon1->TryCommitFire());
        TestTrue(TEXT("moving aimed sniper ignores random and accumulated recoil"), AllDirectionsExact(F.Weapon1, 600.0f));
        TestTrue(TEXT("airborne aimed sniper uses exact configured direction"), AllDirectionsExact(F.Weapon1, 600.0f, true));
    }
    float Pitch = 0, Yaw = 0;
    bool bGenerated = false;
    F.Weapon1->GenerateRecoilKick(Pitch, Yaw, bGenerated);
    TestTrue(TEXT("aim still emits its configured visual recoil kick"), bGenerated && FMath::IsNearlyEqual(Pitch, 3.0f));
    TestTrue(TEXT("switch accepted"), F.Inventory->TrySwitchWeaponNative());
    TestFalse(TEXT("switch cancels aim"), F.Player->IsAiming());
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("old weapon hip shot accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("detached old weapon no longer retains exact aim state"), HasSpread(F.Weapon1));
    return true;
}
#endif
