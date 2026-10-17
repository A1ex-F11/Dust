#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "WeaponRuntime.h"
#include "WeaponDefinitionData.h"
#include "WeaponPresentationComponent.h"
#include "WeaponRecoilData.h"

namespace
{
struct FCrosshairTestWorld
{
    UWorld* World = nullptr;
    FCrosshairTestWorld()
    {
        UWorld::InitializationValues Values;
        Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
            nullptr, false, ERHIFeatureLevel::Num, &Values);
        if (World && GEngine)
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
        }
    }
    ~FCrosshairTestWorld()
    {
        if (World)
        {
            World->DestroyWorld(false);
            if (GEngine) GEngine->DestroyWorldContext(World);
        }
    }
    void Advance(float Seconds)
    {
        // A test world's game delta is clamped; use normal frame-sized steps.
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / 0.05f));
        for (int32 I = 0; I < Steps; ++I) World->Tick(LEVELTICK_All, Seconds / Steps);
    }
    AWeaponRuntime* Spawn(UWeaponDefinitionData* Definition)
    {
        if (!World) return nullptr;
        AWeaponRuntime* Weapon = World->SpawnActor<AWeaponRuntime>();
        if (Weapon)
        {
            Weapon->WeaponDefinition = Definition;
            Weapon->ApplyDefinitionSettings();
        }
        return Weapon;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrosshairSpreadTest,
    "Dust2.Crosshair.Runtime.TotalSpreadAndCommitOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairSpreadTest::RunTest(const FString& Parameters)
{
    FCrosshairTestWorld F;
    UWeaponDefinitionData* D = NewObject<UWeaponDefinitionData>();
    D->StationarySpreadAngle = 0.2f;
    D->MovingSpreadAngle = 3.0f;
    D->SlowWalkSpreadAngle = 1.0f;
    D->JumpSpreadAngle = 12.0f;
    D->MovementSpreadSpeedThreshold = 10.0f;
    D->bUseFiringSpread = true;
    D->SpreadPerShot = 0.4f;
    D->MaxFiringSpread = 0.8f;
    D->SpreadRecoveryDelay = 0.0f;
    D->SpreadRecoverySpeed = 0.2f;
    D->FireInterval = 0.1f;
    AWeaponRuntime* W = F.Spawn(D);
    if (!TestNotNull(TEXT("Weapon"), W)) return false;
    TestEqual(TEXT("Stationary half angle"), W->GetCurrentTotalSpreadAngle(0, false, false, false), 0.2f);
    TestEqual(TEXT("Threshold is inclusive"), W->GetCurrentTotalSpreadAngle(10, false, false, true), 0.2f);
    TestEqual(TEXT("Slow walk is not zero"), W->GetCurrentTotalSpreadAngle(100, false, false, true), 1.0f);
    TestEqual(TEXT("Moving"), W->GetCurrentTotalSpreadAngle(100, false, false, false), 3.0f);
    TestEqual(TEXT("Air overrides slow walk and legacy crouch"), W->GetCurrentTotalSpreadAngle(100, true, true, true), 12.0f);
    TestTrue(TEXT("Accepted first shot"), W->TryCommitFire());
    FVector Shot;
    bool bGenerated = false;
    float LargestAngle = 0.0f;
    for (int32 I = 0; I < 256; ++I)
    {
        W->GenerateShotDirection(FVector::ForwardVector, 0, false, false, false, Shot, bGenerated);
        TestTrue(TEXT("Generated"), bGenerated);
        const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            FVector::DotProduct(Shot, FVector::ForwardVector), -1.0, 1.0)));
        LargestAngle = FMath::Max(LargestAngle, Angle);
    }
    TestTrue(TEXT("First shot uses pre-increment cone"), LargestAngle <= 0.201f);
    TestTrue(TEXT("Getter displays next-shot bloom"), FMath::IsNearlyEqual(
        W->GetCurrentTotalSpreadAngle(0, false, false, false), 0.6f));
    TestFalse(TEXT("Cooldown refused"), W->TryCommitFire());
    TestTrue(TEXT("Refusal does not erase or add displayed bloom"), FMath::IsNearlyEqual(
        W->GetCurrentTotalSpreadAngle(0, false, false, false), 0.6f));
    F.Advance(0.5f);
    TestTrue(TEXT("Time recovery without another shot"), FMath::IsNearlyEqual(W->GetCurrentFiringSpread(), 0.3f));
    W->TryCommitFire();
    F.Advance(0.11f);
    W->TryCommitFire();
    TestTrue(TEXT("Capped at configured maximum"), FMath::IsNearlyEqual(W->GetCurrentFiringSpread(), 0.8f));
    W->ResetRecoilPattern();
    D->MovingSpreadAngle = 200.0f;
    D->StationarySpreadAngle = -2.0f;
    W->ApplyDefinitionSettings();
    TestEqual(TEXT("Upper bound matches shot cone"), W->GetCurrentTotalSpreadAngle(100, false, false, false), 89.0f);
    TestEqual(TEXT("Negative state/speed safe"), W->GetCurrentTotalSpreadAngle(-1, false, false, false), 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrosshairPickupSpreadTest,
    "Dust2.Crosshair.Runtime.SwitchAndDropPreserveBothProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairPickupSpreadTest::RunTest(const FString& Parameters)
{
    FCrosshairTestWorld F;
    UWeaponDefinitionData* D = NewObject<UWeaponDefinitionData>();
    D->FireInterval = 0.0f;
    D->bUseFiringSpread = true;
    D->SpreadPerShot = 0.5f;
    D->MaxFiringSpread = 1.0f;
    D->SpreadRecoveryDelay = 0.5f;
    D->SpreadRecoverySpeed = 0.2f;
    D->bUseAimingSpread = true;
    D->AimingSpread.bUseFiringSpread = true;
    D->AimingSpread.SpreadPerShot = 0.2f;
    D->AimingSpread.MaxFiringSpread = 1.0f;
    D->AimingSpread.SpreadRecoveryDelay = 0.5f;
    D->AimingSpread.SpreadRecoverySpeed = 0.1f;
    AWeaponRuntime* A = F.Spawn(D);
    AWeaponRuntime* B = F.Spawn(D);
    if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B)) return false;
    UWeaponPresentationComponent* Presentation = NewObject<UWeaponPresentationComponent>(B);
    Presentation->RegisterComponent();
    Presentation->SetWeaponForPresentation(A);
    TestTrue(TEXT("Shot accepted"), A->TryCommitFire());
    Presentation->SetWeaponForPresentation(B);
    TestEqual(TEXT("Switching out preserves A bloom"), A->GetCurrentFiringSpread(), 0.5f);
    TestEqual(TEXT("B owns separate bloom"), B->GetCurrentFiringSpread(), 0.0f);
    F.Advance(0.25f);
    const FWeaponPickupState Snapshot = A->CapturePickupRuntimeState(true);
    const int32 Ammo = A->GetCurrentAmmo();
    A->Destroy();
    F.Advance(0.30f);
    AWeaponRuntime* Restored = F.Spawn(D);
    if (!TestNotNull(TEXT("Restored dropped weapon"), Restored)) return false;
    Restored->RestorePickupRuntimeState(Snapshot);
    Presentation->SetWeaponForPresentation(Restored);
    TestEqual(TEXT("Ammo unaffected"), Restored->GetCurrentAmmo(), Ammo);
    TestTrue(TEXT("Hip snapshot uses original shot age, including ground time"),
        FMath::IsNearlyEqual(Restored->GetCurrentFiringSpread(), 0.49f, 0.0001f));
    const FWeaponPickupState ReCaptured = Restored->CapturePickupRuntimeState(true);
    TestTrue(TEXT("Aim shot clock survives rebuild"), ReCaptured.bHasAimSpreadShot
        && FMath::IsNearlyEqual(ReCaptured.AimSecondsSinceShot, 0.55f, 0.0001f));
    TestEqual(TEXT("Aim accumulation survives rebuild independently"), ReCaptured.AimAccumulatedSpread, 0.2f);
    F.Advance(3.0f);
    TestEqual(TEXT("Eventually fully recovered"), Restored->GetCurrentFiringSpread(), 0.0f);
    FWeaponPickupState Legacy;
    Legacy.bHasSavedState = true;
    Legacy.CurrentAmmo = 5;
    Restored->RestorePickupRuntimeState(Legacy);
    TestEqual(TEXT("Legacy saved pickups start with no spread"), Restored->GetCurrentFiringSpread(), 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrosshairCurrentAssetReadTest,
    "Dust2.Crosshair.CurrentAssets.ReadOnlyProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrosshairCurrentAssetReadTest::RunTest(const FString& Parameters)
{
    const TCHAR* Paths[] = {
        TEXT("/Game/Code/Texts/AK105/DA_AK105Definition.DA_AK105Definition"),
        TEXT("/Game/Code/Texts/TR15/DA_TR15Definition.DA_TR15Definition"),
        TEXT("/Game/Code/Texts/R08/DA_R08Definition.DA_R08Definition"),
        TEXT("/Game/Code/Texts/Viper/DA_ViperDefinition.DA_ViperDefinition"),
        TEXT("/Game/Code/Texts/SRM-12/DA_SRM-12Definition.DA_SRM-12Definition")};
    for (const TCHAR* Path : Paths)
    {
        const UWeaponDefinitionData* D = LoadObject<UWeaponDefinitionData>(nullptr, Path);
        if (!TestNotNull(Path, D)) continue;
        UE_LOG(LogTemp, Display, TEXT("[W08Asset] %s interval=%.4f burst=%.4f hip=(%.4f,%.4f,%.4f,%.4f) bloom=%d per=%.4f max=%.4f delay=%.4f speed=%.4f aimIndependent=%d aimBloom=%d aimPer=%.4f aimMax=%.4f aimDelay=%.4f aimSpeed=%.4f"),
            Path, D->FireInterval, D->BurstShotInterval, D->StationarySpreadAngle,
            D->MovingSpreadAngle, D->SlowWalkSpreadAngle, D->JumpSpreadAngle,
            D->bUseFiringSpread, D->SpreadPerShot, D->MaxFiringSpread, D->SpreadRecoveryDelay,
            D->SpreadRecoverySpeed, D->bUseAimingSpread, D->AimingSpread.bUseFiringSpread,
            D->AimingSpread.SpreadPerShot, D->AimingSpread.MaxFiringSpread,
            D->AimingSpread.SpreadRecoveryDelay, D->AimingSpread.SpreadRecoverySpeed);
        if (D->RecoilData)
            UE_LOG(LogTemp, Display, TEXT("[W08Recoil] %s pitchScale=%.4f yawScale=%.4f shotRecovery=%.4f delay=%.4f cameraReturn=%.4f"),
                Path, D->RecoilData->FixedPatternPitchScale, D->RecoilData->FixedPatternYawScale,
                D->RecoilData->ShotRecoilRecoverySpeed, D->RecoilData->RecoveryDelay,
                D->RecoilData->CameraReturnSpeed);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdditiveAccuracyTest,
    "Dust2.Crosshair.Accuracy.AdditiveStateAndCrouchRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAdditiveAccuracyTest::RunTest(const FString& Parameters)
{
    FCrosshairTestWorld F;
    UWeaponDefinitionData* D = NewObject<UWeaponDefinitionData>();
    D->bUseIntuitiveHipAccuracy = true;
    D->HipAccuracy.BaseSpreadAngle = 8.0f;
    D->HipAccuracy.MovingExtraAngle = 3.0f;
    D->HipAccuracy.SlowWalkExtraAngle = 1.0f;
    D->HipAccuracy.AirborneExtraAngle = 5.0f;
    D->HipAccuracy.MaxFiringExtraAngle = 0.8f;
    D->HipAccuracy.ShotsToMaximum = 5;
    D->HipAccuracy.FullRecoverySeconds = 1.0f;
    D->FireInterval = 0.1f;
    AWeaponRuntime* W = F.Spawn(D);
    if (!TestNotNull(TEXT("Weapon"), W)) return false;
    TestEqual(TEXT("Base is a floor"), W->GetCurrentTotalSpreadAngle(0, false, false, false), 8.0f);
    TestEqual(TEXT("Moving adds instead of replacing base"), W->GetCurrentTotalSpreadAngle(100, false, false, false), 11.0f);
    TestEqual(TEXT("Slow walk adds less than normal movement"), W->GetCurrentTotalSpreadAngle(100, false, false, true), 9.0f);
    TestEqual(TEXT("Air stacks with horizontal movement; held slow-walk does not discount air"),
        W->GetCurrentTotalSpreadAngle(100, true, false, true), 16.0f);
    TestEqual(TEXT("Stationary air has no horizontal penalty"), W->GetCurrentTotalSpreadAngle(0, true, false, false), 13.0f);
    TestEqual(TEXT("Legacy crouch input cannot grant extra precision"),
        W->GetCurrentTotalSpreadAngle(100, false, true, false), 11.0f);
    W->TryCommitFire();
    const float Bloom = W->GetCurrentFiringSpread();
    TestTrue(TEXT("Movement change keeps the same shot accumulation"), FMath::IsNearlyEqual(
        W->GetCurrentTotalSpreadAngle(100, true, false, false), 16.0f + Bloom));
    const float AfterQuery = W->GetCurrentFiringSpread();
    TestEqual(TEXT("Display queries do not mutate bloom"), AfterQuery, Bloom);

    // Existing independent ADS zero cone must remain independent from the new hip profile.
    D->bUseAimingSpread = true;
    D->AimingSpread.StationarySpreadAngle = 0;
    D->AimingSpread.MovingSpreadAngle = 0;
    D->AimingSpread.SlowWalkSpreadAngle = 0;
    D->AimingSpread.JumpSpreadAngle = 0;
    D->AimingSpread.bUseFiringSpread = false;
    const FWeaponSpreadProfile LegacyAim = D->AimingSpread;
    TestFalse(TEXT("Old aim profile stays legacy until separately opted in"), LegacyAim.bUseAdditiveStateSpread);
    D->bUseIntuitiveHipAccuracy = false;
    W->ResetRecoilPattern();
    W->ApplyDefinitionSettings();
    TestEqual(TEXT("Crouch removed from legacy profile too"),
        W->GetCurrentTotalSpreadAngle(100, false, true, false), D->MovingSpreadAngle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntuitiveCalibrationTest,
    "Dust2.Crosshair.Accuracy.ShotCountCalibrationAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIntuitiveCalibrationTest::RunTest(const FString& Parameters)
{
    FCrosshairTestWorld F;
    UWeaponDefinitionData* D = NewObject<UWeaponDefinitionData>();
    D->bUseIntuitiveHipAccuracy = true;
    D->FireInterval = 0.1f;
    D->HipAccuracy.MaxFiringExtraAngle = 0.7f;
    D->HipAccuracy.ShotsToMaximum = 14;
    D->HipAccuracy.FullRecoverySeconds = 1.0f;
    const FWeaponSpreadProfile P = D->HipAccuracy.BuildProfile(0.1f, 10.0f);
    TestTrue(TEXT("Derived increase accounts for between-shot recovery"), P.SpreadPerShot > 0.7f / 14);
    TestEqual(TEXT("No wait before recovering"), P.SpreadRecoveryDelay, 0.0f);
    TestTrue(TEXT("Recovery rate is maximum divided by seconds"), FMath::IsNearlyEqual(P.SpreadRecoverySpeed, 0.7f));
    AWeaponRuntime* W = F.Spawn(D);
    if (!TestNotNull(TEXT("Weapon"), W)) return false;
    for (int32 I = 0; I < 14; ++I)
    {
        // Small numerical slack avoids an engine time rounding refusing an otherwise legal shot.
        if (I > 0) F.Advance(0.10001f);
        TestTrue(TEXT("Reference-rate shot accepted"), W->TryCommitFire());
        if (I < 13) TestTrue(TEXT("Does not hit the cap too early"), W->GetCurrentFiringSpread() < 0.7f);
    }
    TestTrue(TEXT("Reference count reaches the configured cap within clock precision"),
        FMath::IsNearlyEqual(W->GetCurrentFiringSpread(), 0.7f, 0.001f));
    F.Advance(1.01f);
    TestEqual(TEXT("Full accumulation recovers by configured time"), W->GetCurrentFiringSpread(), 0.0f);

    D->HipAccuracy.MaxFiringExtraAngle = 0.0f;
    const FWeaponSpreadProfile Disabled = D->HipAccuracy.BuildProfile(0.1f, 10);
    TestFalse(TEXT("Zero maximum cleanly disables random firing accumulation"), Disabled.bUseFiringSpread);
    D->HipAccuracy.ShotsToMaximum = -10;
    D->HipAccuracy.FullRecoverySeconds = -1;
    const FWeaponSpreadProfile Safe = D->HipAccuracy.BuildProfile(-1, -1);
    TestTrue(TEXT("Invalid tuning stays finite"), FMath::IsFinite(Safe.SpreadRecoverySpeed)
        && FMath::IsFinite(Safe.SpreadPerShot) && Safe.MovementSpreadSpeedThreshold == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntuitiveKeepsFixedPatternTest,
    "Dust2.Crosshair.Accuracy.IntuitiveModeKeepsLearnableShotPattern",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIntuitiveKeepsFixedPatternTest::RunTest(const FString& Parameters)
{
    FCrosshairTestWorld F;
    UWeaponDefinitionData* D = NewObject<UWeaponDefinitionData>();
    D->bUseIntuitiveHipAccuracy = true;
    D->HipAccuracy.BaseSpreadAngle = 0;
    D->HipAccuracy.MaxFiringExtraAngle = 0;
    D->FireInterval = 0.01f;
    D->RecoilData->bUseFixedPattern = true;
    D->RecoilData->bUseIndependentShotRecoil = true;
    D->RecoilData->FixedPattern = { FVector2D(0.3, 0.1), FVector2D(0.4, -0.1) };
    D->RecoilData->FixedPatternPitchScale = 1;
    D->RecoilData->FixedPatternYawScale = 1;
    D->RecoilData->PatternResetDelay = 10;
    D->RecoilData->RecoveryDelay = 10;
    AWeaponRuntime* W = F.Spawn(D);
    if (!TestNotNull(TEXT("Weapon"), W)) return false;
    FVector Actual;
    bool bGenerated = false;
    W->TryCommitFire();
    W->GenerateShotDirection(FVector::ForwardVector, 0, false, false, false, Actual, bGenerated);
    TestTrue(TEXT("First shot still uses prior zero pattern offset"), bGenerated && Actual.Equals(FVector::ForwardVector, 0.00001));
    F.Advance(0.02f);
    W->TryCommitFire();
    W->GenerateShotDirection(FVector::ForwardVector, 0, false, false, false, Actual, bGenerated);
    const FVector Expected = FRotator(0.3f, 0.1f, 0).Vector();
    TestTrue(TEXT("Second shot still follows fixed sequence, not an arbitrary random cone"),
        bGenerated && Actual.Equals(Expected, 0.00001));
    FVector Zero;
    W->GenerateShotDirection(FVector::ZeroVector, 0, false, false, false, Zero, bGenerated);
    TestFalse(TEXT("Zero direction remains refused"), bGenerated);
    return true;
}
#endif
