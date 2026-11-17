#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Combat/DustShotDamageReceiptLibrary.h"
#include "Player/Dust2PlayerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustShotReceiptScopeTest,
    "Dust2.CombatFeedback.ReceiptScopeAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustShotReceiptScopeTest::RunTest(const FString&)
{
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, false, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("receipt world"), World)) return false;
    if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    AActor* First = World->SpawnActor<AActor>();
    AActor* Second = World->SpawnActor<AActor>();
    if (!First || !Second)
    {
        World->DestroyWorld(false);
        if (GEngine) GEngine->DestroyWorldContext(World);
        return false;
    }
    {
        DustShotDamageReceipt::FScopedCapture Outer(First);
        UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(Second, 10, 0, false);
        UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First, 10, 10, false);
        UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First, 10, 0, true);
        UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First,
            std::numeric_limits<float>::quiet_NaN(), 0, false);
        TestFalse(TEXT("wrong target, no loss, dead and NaN cannot confirm"), Outer.GetResult().bConfirmed);
        {
            DustShotDamageReceipt::FScopedCapture Empty(nullptr);
            UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First, 10, 0, false);
        }
        TestFalse(TEXT("null nested dispatch is a scope barrier"), Outer.GetResult().bConfirmed);
        {
            DustShotDamageReceipt::FScopedCapture Inner(Second);
            UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First, 10, 0, false);
            UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(Second, 20, 5, false);
            TestEqual(TEXT("inner owns its actual damage"), Inner.GetResult().ActualDamage, 15.0f);
            TestFalse(TEXT("inner hit is not kill"), Inner.GetResult().bKilled);
        }
        TestFalse(TEXT("nested receipt does not leak to outer shot"), Outer.GetResult().bConfirmed);
        UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First, 10, 0, false);
        UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First, 100, 1, false);
        TestTrue(TEXT("restored outer confirms lethal damage"), Outer.GetResult().bKilled);
        TestEqual(TEXT("duplicate report cannot overwrite first receipt"), Outer.GetResult().ActualDamage, 10.0f);
    }
    UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(First, 10, 0, false);
    {
        DustShotDamageReceipt::FScopedCapture Next(First);
        TestFalse(TEXT("out of scope report does not persist"), Next.GetResult().bConfirmed);
    }
    ADust2PlayerCharacter* Player = World->SpawnActor<ADust2PlayerCharacter>();
    if (TestNotNull(TEXT("native player receiver"), Player))
    {
        DustShotDamageReceipt::FScopedCapture PlayerCapture(Player);
        bool bDiedNow = false;
        const float Before = Player->GetCurrentHealth();
        TestTrue(TEXT("native player applies lethal damage"), Player->ApplyDamage(1000.0f, bDiedNow));
        TestTrue(TEXT("native player death is acknowledged"), bDiedNow && PlayerCapture.GetResult().bKilled);
        TestEqual(TEXT("native receipt uses actual remaining health"), PlayerCapture.GetResult().ActualDamage, Before);
    }
    World->DestroyWorld(false);
    if (GEngine) GEngine->DestroyWorldContext(World);
    return true;
}
#endif
