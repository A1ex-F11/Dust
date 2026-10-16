#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/ChildActorComponent.h"
#include "Dust2PlayerCharacter.h"
#include "WeaponInventoryComponent.h"
#include "WeaponPresentationComponent.h"
#include "CameraRecoilRecoveryComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "EngineGlobals.h"
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
        void Tick(float DeltaSeconds = 0.001f)
        { ++GFrameCounter; World->Tick(LEVELTICK_All, DeltaSeconds); }
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimedViewDirectionTest, "Dust2.Aim.ViewDirection.HipUnchangedAimFollowsInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAimedViewDirectionTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    UWeaponDefinitionData* D = ConfigureDualSpread(F);
    D->StationarySpreadAngle = D->MovingSpreadAngle = D->JumpSpreadAngle = D->SlowWalkSpreadAngle = 0.0f;
    D->bUseAimingSpread = false; // Direction policy must not depend on spread opt-in.
    D->bIgnoreShotRecoilWhenAiming = false; // Legacy asset setting cannot reintroduce hidden ADS recoil.
    D->RecoilData = NewObject<UWeaponRecoilData>(D);
    D->RecoilData->bUseFixedPattern = true;
    D->RecoilData->bUseIndependentShotRecoil = true;
    D->RecoilData->FixedPattern = { FVector2D(3.0f, 1.0f) };
    D->RecoilData->FixedPatternPitchScale = D->RecoilData->FixedPatternYawScale = 1.0f;
    D->RecoilData->PatternResetDelay = 100.0f;
    D->RecoilData->RecoveryDelay = 100.0f;
    F.Weapon1->ApplyDefinitionSettings();
    const FVector ViewDirection = FRotator(7.0f, 24.0f, 0.0f).Vector();
    auto Generate = [&]() {
        FVector Direction;
        bool bGenerated = false;
        F.Weapon1->GenerateShotDirection(ViewDirection, 0.0f, false, false, false, Direction, bGenerated);
        TestTrue(TEXT("direction generated"), bGenerated);
        return Direction;
    };
    TestTrue(TEXT("first hip shot accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("first hip shot starts at base direction"), Generate().Equals(ViewDirection, 0.00001f));
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("second hip accepted"), F.Weapon1->TryCommitFire());
    TestTrue(TEXT("enter aim after hip commit"), F.Press());
    TestFalse(TEXT("committed hip retains independent recoil after entering aim"), Generate().Equals(ViewDirection, 0.00001f));
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("aim shot accepted"), F.Weapon1->TryCommitFire());
    F.Release();
    TestTrue(TEXT("committed aim follows input even after release"), Generate().Equals(ViewDirection, 0.00001f));
    float Pitch = 0.0f, Yaw = 0.0f;
    bool bKick = false;
    F.Weapon1->GenerateRecoilKick(Pitch, Yaw, bKick);
    TestTrue(TEXT("visual recoil feedback preserved"), bKick && FMath::IsNearlyEqual(Pitch, 3.0f));
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("returned hip accepted"), F.Weapon1->TryCommitFire());
    TestFalse(TEXT("returning hip retains accumulated recoil"), Generate().Equals(ViewDirection, 0.00001f));
    TestTrue(TEXT("enter aim with spread enabled"), F.Press());
    D->bUseAimingSpread = true;
    D->AimingSpread.StationarySpreadAngle = 1.0f;
    F.World->Tick(LEVELTICK_All, 0.02f);
    TestTrue(TEXT("spread aim shot accepted"), F.Weapon1->TryCommitFire());
    const float ConeCos = FMath::Cos(FMath::DegreesToRadians(1.01f));
    for (int32 Sample = 0; Sample < 32; ++Sample)
        TestTrue(TEXT("ADS spread stays around view rather than hidden recoil"), FVector::DotProduct(Generate(), ViewDirection) >= ConeCos);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimedRecoilWriterTest, "Dust2.Aim.Recoil.ViewWriterAndMultiplier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAimedRecoilWriterTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    APlayerController* PC = F.World->SpawnActor<APlayerController>();
    PC->Player = NewObject<ULocalPlayer>(GEngine);
    PC->Possess(F.Player);
    PC->SetControlRotation(FRotator::ZeroRotator);
    UCameraRecoilRecoveryComponent* Recovery = F.Player->FindComponentByClass<UCameraRecoilRecoveryComponent>();
    UCameraComponent* Camera = NewObject<UCameraComponent>(F.Player);
    Camera->RegisterComponent();
    Camera->bUsePawnControlRotation = true;
    UWeaponDefinitionData* D = ConfigureDualSpread(F);
    D->RecoilData = NewObject<UWeaponRecoilData>(D);
    UWeaponRecoilData* R = D->RecoilData;
    R->bUseFixedPattern = R->bUseIndependentShotRecoil = true;
    R->FixedPattern = { FVector2D(2.0f, 1.0f) };
    R->FixedPatternPitchScale = R->FixedPatternYawScale = 1.0f;
    R->AimingRecoilScale = 0.5f;
    R->SmoothKickDuration = 0.0f;
    R->ImmediateKickFraction = 1.0f;
    R->RecoveryDelay = R->PatternResetDelay = 100.0f;
    F.Weapon1->ApplyDefinitionSettings();
    const float Interval = F.Weapon1->GetEffectiveFireInterval();
    TestTrue(TEXT("aim entered"), F.Press());
    for (int32 Shot = 0; Shot < 2; ++Shot)
    {
        F.World->Tick(LEVELTICK_All, 0.02f);
        const FVector Before = PC->GetControlRotation().Vector();
        TestTrue(TEXT("accepted shot"), F.Weapon1->TryCommitFire());
        FVector Actual; bool bGenerated = false;
        F.Weapon1->GenerateShotDirection(Before, 0.0f, false, false, false, Actual, bGenerated);
        TestTrue(TEXT("this shot follows pre-feedback view without double recoil"), bGenerated && Actual.Equals(Before, 0.00001f));
        float Pitch = 0.0f, Yaw = 0.0f; bool bKick = false;
        F.Weapon1->GenerateRecoilKick(Pitch, Yaw, bKick);
        TestTrue(TEXT("existing trajectory provides kick"), bKick);
        Recovery->ApplyRecoilWithCamera(Pitch, Yaw, R, Camera);
        TestTrue(TEXT("view pitch uses separate aim multiplier"), FMath::IsNearlyEqual(PC->GetControlRotation().Pitch, float(Shot + 1)));
        TestTrue(TEXT("view yaw uses same trajectory multiplier"), FMath::IsNearlyEqual(PC->GetControlRotation().Yaw, float(Shot + 1) * 0.5f));
        FMinimalViewInfo POV;
        Camera->GetCameraView(0.0f, POV);
        TestTrue(TEXT("controlled camera follows the same view"), POV.Rotation.Equals(PC->GetControlRotation(), 0.0001f));
    }
    TestEqual(TEXT("ADS does not change fire interval"), F.Weapon1->GetEffectiveFireInterval(), Interval);
    R->AimingRecoilScale = 0.0f;
    const FRotator BeforeZero = PC->GetControlRotation();
    Recovery->ApplyRecoilWithCamera(2.0f, 1.0f, R, Camera);
    TestTrue(TEXT("zero scale produces no view kick"), PC->GetControlRotation().Equals(BeforeZero));
    F.Release();
    R->AimingRecoilScale = 10.0f;
    Recovery->ApplyRecoilWithCamera(2.0f, 1.0f, R, Camera);
    TestTrue(TEXT("hip independent feedback does not write control rotation"), PC->GetControlRotation().Equals(BeforeZero));
    Recovery->ClearRecoil();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimedRecoilCleanupTest, "Dust2.Aim.Recoil.TailCleanupAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAimedRecoilCleanupTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    APlayerController* PC = F.World->SpawnActor<APlayerController>();
    PC->Player = NewObject<ULocalPlayer>(GEngine);
    PC->Possess(F.Player);
    PC->SetControlRotation(FRotator::ZeroRotator);
    UCameraRecoilRecoveryComponent* Recovery = F.Player->FindComponentByClass<UCameraRecoilRecoveryComponent>();
    UWeaponRecoilData* R = NewObject<UWeaponRecoilData>();
    R->bUseFixedPattern = R->bUseIndependentShotRecoil = true;
    R->AimingRecoilScale = 1.0f;
    R->SmoothKickDuration = 1.0f;
    R->ImmediateKickFraction = 0.25f;
    R->RecoveryDelay = 100.0f;
    F.Press();
    Recovery->ApplyRecoilWithCamera(4.0f, 0.0f, R, nullptr);
    TestTrue(TEXT("only immediate fraction applied"), FMath::IsNearlyEqual(PC->GetControlRotation().Pitch, 1.0f));
    F.Release();
    for (int32 Frame = 0; Frame < 25; ++Frame) { ++GFrameCounter; F.World->Tick(LEVELTICK_All, 0.05f); }
    TestTrue(TEXT("release cancels ADS tail without snapping view"), FMath::IsNearlyEqual(PC->GetControlRotation().Pitch, 1.0f));
    PC->SetControlRotation(FRotator::ZeroRotator);
    R->SmoothKickDuration = 0.0f;
    R->ImmediateKickFraction = 1.0f;
    R->RecoveryDelay = 0.1f;
    R->ShotRecoilRecoverySpeed = 100.0f;
    R->RecoveryPitchRatio = 1.0f;
    F.Press();
    Recovery->ApplyRecoilWithCamera(2.0f, 0.0f, R, nullptr);
    TestTrue(TEXT("full kick changes view"), FMath::IsNearlyEqual(PC->GetControlRotation().Pitch, 2.0f));
    for (int32 Frame = 0; Frame < 8; ++Frame) { ++GFrameCounter; F.World->Tick(LEVELTICK_All, 0.05f); }
    TestTrue(FString::Printf(TEXT("ADS view recovers after configured delay; actual=%f time=%f"), PC->GetControlRotation().Pitch, F.World->GetTimeSeconds()), FMath::IsNearlyZero(PC->GetControlRotation().Pitch, 0.001f));
    R->RecoveryDelay = 100.0f;
    Recovery->ApplyRecoilWithCamera(2.0f, 0.0f, R, nullptr);
    Recovery->NotifyManualLook();
    const FRotator ManualView(-3.0f, 5.0f, 0.0f);
    PC->SetControlRotation(ManualView);
    for (int32 Frame = 0; Frame < 8; ++Frame) { ++GFrameCounter; F.World->Tick(LEVELTICK_All, 0.05f); }
    TestTrue(TEXT("manual compensation is not undone by old recovery"), PC->GetControlRotation().Equals(ManualView, 0.001f));
    F.Player->CancelAim();
    return true;
}

// Feeds one consumed movement input so GetLastMovementInputVector() is deterministic
// without ticking CharacterMovement. bForce bypasses the missing controller.
static void FeedMoveInput(ADust2PlayerCharacter* Player, const FVector& WorldDirection)
{
    Player->AddMovementInput(WorldDirection, 1.0f, true);
    Player->Internal_ConsumeMovementInputVector();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSprintDirectionGateTest,
    "Dust2.Gait.SprintRequiresForwardInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSprintDirectionGateTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    const FVector Forward = F.Player->GetActorForwardVector();
    const FVector Right = F.Player->GetActorRightVector();

    const auto SprintGaitFor = [&F](const FVector& Direction)
    {
        F.Player->SetSprintHeld(false);
        FeedMoveInput(F.Player, Direction);
        F.Player->SetSprintHeld(true);
        return F.Player->GetCurrentGait();
    };

    TestTrue(TEXT("no input stays Walk"), F.Player->GetCurrentGait() != EDustMovementGait::Run);
    TestTrue(TEXT("forward enters Run"), SprintGaitFor(Forward) == EDustMovementGait::Run);
    TestTrue(TEXT("diagonal forward enters Run"), SprintGaitFor((Forward + Right * 0.5f).GetSafeNormal()) == EDustMovementGait::Run);
    TestTrue(TEXT("backward stays Walk"), SprintGaitFor(-Forward) == EDustMovementGait::Walk);
    TestTrue(TEXT("pure strafe stays Walk"), SprintGaitFor(Right) == EDustMovementGait::Walk);
    F.Player->SetSprintHeld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSprintActionExitGateTest,
    "Dust2.Gait.SprintActionExitGateAndPendingAim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSprintActionExitGateTest::RunTest(const FString& Parameters)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;

    // A report from the previous Walk pose cannot survive a new Run.
    F.Player->ReportSprintExitPresentationReady(false);
    FeedMoveInput(F.Player, F.Player->GetActorForwardVector()); // Run now requires forward input (G09).
    F.Player->SetSprintHeld(true);
    TestTrue(TEXT("sprint intent enters Run"), F.Player->GetCurrentGait() == EDustMovementGait::Run);

    TestFalse(TEXT("aim press during Run is deferred"), F.Press());
    TestTrue(TEXT("pending aim is visible"), F.Player->HasPendingSprintAim());
    TestFalse(TEXT("deferred aim has not switched ADS"), F.Player->IsAiming());
    TestTrue(TEXT("pending aim exits logical Run"), F.Player->GetCurrentGait() == EDustMovementGait::Walk);
    TestFalse(TEXT("fire also waits during the same exit"), F.Player->NotifyFirePressed());
    TestTrue(TEXT("one fire request is pending beside aim"), F.Player->HasPendingSprintFire());
    F.Player->ReportSprintExitPresentationReady(false);
    F.Tick();
    TestTrue(TEXT("false animation report keeps aim pending"), F.Player->HasPendingSprintAim());
    TestTrue(TEXT("false animation report keeps fire pending"), F.Player->HasPendingSprintFire());
    TestFalse(TEXT("false report keeps ADS off"), F.Player->IsAiming());

    F.Release();
    TestFalse(TEXT("aim release cancels only pending aim"), F.Player->HasPendingSprintAim());
    TestTrue(TEXT("held fire still blocks Run"), F.Player->HasPendingSprintFire());
    TestEqual(TEXT("releasing one action does not resume Run while the other is pending"),
        F.Player->GetCurrentGait(), EDustMovementGait::Walk);
    F.Player->NotifyFireReleased();
    TestFalse(TEXT("fire release cancels its pending shot"), F.Player->HasPendingSprintFire());
    TestEqual(TEXT("releasing the final action restores held sprint"),
        F.Player->GetCurrentGait(), EDustMovementGait::Run);
    F.Player->ReportSprintExitPresentationReady(true);
    TestFalse(TEXT("stale true report while Run is ignored"), F.Player->CanProcessSprintAction());

    // Release Run, then verify the ready notification is deferred until next tick.
    F.Player->SetSprintHeld(false);
    F.Player->ReportSprintExitPresentationReady(false);
    TestFalse(TEXT("aim is pending while logical Walk still has sprint pose"), F.Press());
    TestTrue(TEXT("walk exit holds a pending aim"), F.Player->HasPendingSprintAim());
    F.Player->ReportSprintExitPresentationReady(true);
    TestFalse(TEXT("post-evaluate report does not activate aim inline"), F.Player->IsAiming());
    F.Tick();
    TestTrue(TEXT("next game-thread tick consumes aim once"), F.Player->IsAiming());
    TestFalse(TEXT("consumed aim no longer pending"), F.Player->HasPendingSprintAim());
    F.Player->ReportSprintExitPresentationReady(true);
    F.Tick();
    TestTrue(TEXT("repeated ready reports do not duplicate the aim transition"), F.Player->IsAiming());

    F.Player->SetSprintHeld(true);
    TestTrue(TEXT("held sprint stays suppressed while aiming"), F.Player->GetCurrentGait() == EDustMovementGait::Walk);
    F.Release();
    TestTrue(TEXT("aim release restores held sprint"), F.Player->GetCurrentGait() == EDustMovementGait::Run);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSprintFireAnimationExitGateTest,
    "Dust2.Gait.SprintFireWaitsForAnimationExit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSprintFireAnimationExitGateTest::RunTest(const FString& Parameters)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    F.Player->ReportSprintExitPresentationReady(false);
    FeedMoveInput(F.Player, F.Player->GetActorForwardVector()); // Run now requires forward input (G09).
    F.Player->SetSprintHeld(true);
    TestFalse(TEXT("Run fire is cached"), F.Player->NotifyFirePressed());
    TestTrue(TEXT("one cached shot exists"), F.Player->HasPendingSprintFire());
    TestTrue(TEXT("fire leaves Run immediately"), F.Player->GetCurrentGait() == EDustMovementGait::Walk);

    F.Player->ReportSprintExitPresentationReady(false);
    F.Tick();
    TestTrue(TEXT("false report does not flush shot"), F.Player->HasPendingSprintFire());
    TestFalse(TEXT("false report keeps common gate closed"), F.Player->CanProcessSprintAction());

    // Releasing the fire input follows Rule A and returns to Run if Shift remains held.
    F.Player->NotifyFireReleased();
    TestFalse(TEXT("release cancels cached shot"), F.Player->HasPendingSprintFire());
    TestTrue(TEXT("release resumes held sprint"), F.Player->GetCurrentGait() == EDustMovementGait::Run);

    // A later press after logical Walk must still wait for the actual exit report.
    F.Player->SetSprintHeld(false);
    F.Player->ReportSprintExitPresentationReady(false);
    TestFalse(TEXT("logical Walk with sprint pose still caches fire"), F.Player->NotifyFirePressed());
    TestTrue(TEXT("walk-state shot remains pending"), F.Player->HasPendingSprintFire());
    F.Player->ReportSprintExitPresentationReady(false);
    F.Tick();
    TestTrue(TEXT("animation has not exited yet"), F.Player->HasPendingSprintFire());
    F.Player->ReportSprintExitPresentationReady(true);
    TestTrue(TEXT("ready is processed on the next tick"), F.Player->HasPendingSprintFire());
    F.Tick();
    TestFalse(TEXT("ready consumes the cached request once"), F.Player->HasPendingSprintFire());
    TestTrue(TEXT("shared gate opens only after report true"), F.Player->CanProcessSprintAction());
    F.Player->ReportSprintExitPresentationReady(true);
    F.Tick();
    TestFalse(TEXT("repeated true reports cannot consume again"), F.Player->HasPendingSprintFire());
    F.Player->NotifyFireReleased();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPendingSprintAimLifecycleTest,
    "Dust2.Gait.PendingAimLifecycleAndReloadPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPendingSprintAimLifecycleTest::RunTest(const FString& Parameters)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    {
        FAimTestWorld F;
        if (!TestTrue(TEXT("switch fixture ready"), F.bReady)) return false;
        F.Player->ReportSprintExitPresentationReady(false);
        F.Player->SetSprintHeld(true);
        TestFalse(TEXT("valid aim can be deferred"), F.Press());
        TestTrue(TEXT("pending aim captured before switch"), F.Player->HasPendingSprintAim());
        TestTrue(TEXT("switch accepted"), F.Inventory->TrySwitchWeaponNative());
        TestFalse(TEXT("weapon switch cancels pending aim"), F.Player->HasPendingSprintAim());
        TestFalse(TEXT("switch cannot activate stale ADS"), F.Player->IsAiming());
    }
    {
        FAimTestWorld F;
        if (!TestTrue(TEXT("death fixture ready"), F.bReady)) return false;
        F.Player->ReportSprintExitPresentationReady(false);
        F.Player->SetSprintHeld(true);
        TestFalse(TEXT("valid aim can be deferred before death"), F.Press());
        bool bDiedNow = false;
        F.Player->ApplyDamage(100.0f, bDiedNow);
        TestTrue(TEXT("death occurred"), bDiedNow);
        TestFalse(TEXT("death clears pending aim"), F.Player->HasPendingSprintAim());
        F.Player->RestoreHealth(100.0f);
        TestFalse(TEXT("restore does not resurrect pending aim"), F.Player->HasPendingSprintAim());
    }
    {
        FAimTestWorld F;
        if (!TestTrue(TEXT("destroy fixture ready"), F.bReady)) return false;
        F.Player->ReportSprintExitPresentationReady(false);
        F.Player->SetSprintHeld(true);
        TestFalse(TEXT("valid aim can be deferred before destroy"), F.Press());
        F.Weapon1->Destroy();
        TestFalse(TEXT("destroyed captured weapon clears pending aim"), F.Player->HasPendingSprintAim());
        TestFalse(TEXT("destroyed weapon cannot activate ADS"), F.Player->IsAiming());
    }
    {
        FAimTestWorld F;
        if (!TestTrue(TEXT("reload fixture ready"), F.bReady)) return false;
        SetAimTestInt(F.Weapon1, TEXT("MagazineCapacity"), 10);
        SetAimTestInt(F.Weapon1, TEXT("CurrentAmmo"), 4);
        SetAimTestInt(F.Weapon1, TEXT("ReserveAmmo"), 20);
        F.Player->ReportSprintExitPresentationReady(false);
        F.Player->SetSprintHeld(true);
        TestFalse(TEXT("aim press is pending during Run"), F.Press());
        TestTrue(TEXT("A policy reload starts"), F.Weapon1->TryStartReload());
        TestTrue(TEXT("A policy keeps the pending request eligible"), F.Player->HasPendingSprintAim());
        F.Player->ReportSprintExitPresentationReady(true);
        F.Tick();
        TestTrue(TEXT("A policy can enter ADS after pose exit while reload remains active"), F.Player->IsAiming());
        TestTrue(TEXT("reload remains authoritative"), F.Weapon1->IsReloading());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSprintModifierPriorityDuringActionLockTest,
    "Dust2.Gait.SprintModifierPrioritySurvivesActionLock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSprintModifierPriorityDuringActionLockTest::RunTest(const FString& Parameters)
{
    AddExpectedMessage(TEXT("Inventory mirror sync rejected"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    FAimTestWorld F;
    if (!TestTrue(TEXT("fixture ready"), F.bReady)) return false;
    F.Player->SetSlowWalkHeld(true);
    FeedMoveInput(F.Player, F.Player->GetActorForwardVector()); // Run now requires forward input (G09).
    F.Player->SetSprintHeld(true); // Sprint is now the most recently pressed modifier.
    TestTrue(TEXT("sprint wins before the action lock"), F.Player->GetCurrentGait() == EDustMovementGait::Run);
    TestFalse(TEXT("aim waits for the sprint pose to exit"), F.Press());
    TestTrue(TEXT("action lock suppresses Run"), F.Player->GetCurrentGait() == EDustMovementGait::SlowWalk);
    F.Release();
    TestEqual(TEXT("removing the action lock restores last-pressed Sprint priority"),
        F.Player->GetCurrentGait(), EDustMovementGait::Run);
    F.Player->SetSprintHeld(false);
    TestTrue(TEXT("releasing Sprint exposes held SlowWalk"), F.Player->GetCurrentGait() == EDustMovementGait::SlowWalk);
    F.Player->SetSlowWalkHeld(false);
    TestTrue(TEXT("releasing both modifiers returns Walk"), F.Player->GetCurrentGait() == EDustMovementGait::Walk);
    return true;
}
#endif

