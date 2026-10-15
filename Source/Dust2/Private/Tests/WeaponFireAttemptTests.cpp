#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "WeaponRuntime.h"
#include "UObject/UnrealType.h"

namespace
{
struct FFireAttemptTestWorld
{
	UWorld* World = nullptr;
	AWeaponRuntime* Weapon = nullptr;
	FFireAttemptTestWorld()
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
		Weapon = World->SpawnActor<AWeaponRuntime>();
	}
	~FFireAttemptTestWorld()
	{
		if (World)
		{
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
	}
	bool SetNextFireTime(float Value)
	{
		FFloatProperty* P = Weapon
			? FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("NextFireTime"))
			: nullptr;
		if (!P) return false;
		P->SetPropertyValue_InContainer(Weapon, Value);
		return true;
	}
	float NextFireTime() const
	{
		const FFloatProperty* P = FindFProperty<FFloatProperty>(
			Weapon->GetClass(), TEXT("NextFireTime"));
		return P ? P->GetPropertyValue_InContainer(Weapon) : -1.0f;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireAttemptLastRoundTest,
	"Dust2.FireAttempt.LastRoundAndEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFireAttemptLastRoundTest::RunTest(const FString& Parameters)
{
	FFireAttemptTestWorld F;
	if (!TestNotNull(TEXT("weapon spawned"), F.Weapon)) return false;
	F.Weapon->RestoreAmmo(1, 4);
	TestTrue(TEXT("last real bullet is committed, never empty feedback"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Committed);
	TestEqual(TEXT("one bullet consumed"), F.Weapon->GetCurrentAmmo(), 0);
	const float Cooldown = F.NextFireTime();
	TestTrue(TEXT("cooldown advanced on success"), Cooldown > F.World->GetTimeSeconds());
	TestTrue(TEXT("empty during cooldown is a cooldown rejection"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Cooldown);
	TestEqual(TEXT("cooldown rejection preserves deadline"), F.NextFireTime(), Cooldown);
	if (!TestTrue(TEXT("test clears cooldown"), F.SetNextFireTime(0.0f))) return false;
	TestTrue(TEXT("legal empty attempt reports empty"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Empty);
	TestTrue(TEXT("repeated empty attempt still reports empty for caller to throttle"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Empty);
	TestEqual(TEXT("empty preserves cooldown"), F.NextFireTime(), 0.0f);
	TestEqual(TEXT("empty preserves reserve"), F.Weapon->GetReserveAmmo(), 4);
	TestEqual(TEXT("empty never consumes below zero"), F.Weapon->GetCurrentAmmo(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireAttemptPriorityTest,
	"Dust2.FireAttempt.RejectionPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFireAttemptPriorityTest::RunTest(const FString& Parameters)
{
	FFireAttemptTestWorld F;
	if (!TestNotNull(TEXT("weapon spawned"), F.Weapon)) return false;
	F.Weapon->RestoreAmmo(0, 4);
	const float Deadline = F.World->GetTimeSeconds() + 10.0f;
	if (!TestTrue(TEXT("test sets cooldown"), F.SetNextFireTime(Deadline))) return false;
	TestTrue(TEXT("reload started"), F.Weapon->TryStartReload());
	TestTrue(TEXT("reload wins over cooldown and empty"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Reloading);
	TestFalse(TEXT("legacy interface also rejects reload"), F.Weapon->TryCommitFire());
	TestEqual(TEXT("reload rejection preserves ammo"), F.Weapon->GetCurrentAmmo(), 0);
	TestEqual(TEXT("reload rejection preserves reserve"), F.Weapon->GetReserveAmmo(), 4);
	TestEqual(TEXT("reload rejection preserves deadline"), F.NextFireTime(), Deadline);
	TestTrue(TEXT("reload cancelled"), F.Weapon->CancelReload());
	TestTrue(TEXT("cooldown wins over empty after cancel"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Cooldown);
	F.SetNextFireTime(0.0f);
	TestTrue(TEXT("empty only reported after prior gates pass"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Empty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireAttemptLegacyTest,
	"Dust2.FireAttempt.LegacyAndRefill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFireAttemptLegacyTest::RunTest(const FString& Parameters)
{
	FFireAttemptTestWorld F;
	if (!TestNotNull(TEXT("weapon spawned"), F.Weapon)) return false;
	F.Weapon->RestoreAmmo(2, 3);
	TestTrue(TEXT("legacy commits exactly one shot"), F.Weapon->TryCommitFire());
	TestEqual(TEXT("legacy consumed once"), F.Weapon->GetCurrentAmmo(), 1);
	TestFalse(TEXT("legacy rejects cooldown"), F.Weapon->TryCommitFire());
	TestEqual(TEXT("legacy rejection consumes nothing"), F.Weapon->GetCurrentAmmo(), 1);
	if (!TestTrue(TEXT("test clears cooldown"), F.SetNextFireTime(0.0f))) return false;
	F.Weapon->RestoreAmmo(0, 3);
	TestFalse(TEXT("legacy rejects empty"), F.Weapon->TryCommitFire());
	TestTrue(TEXT("reload starts after empty"), F.Weapon->TryStartReload());
	TestTrue(TEXT("reload completed"), F.Weapon->FinishReload());
	TestEqual(TEXT("reserve transferred"), F.Weapon->GetCurrentAmmo(), 3);
	TestTrue(TEXT("new result accepts refilled weapon without a stale empty lock"),
		F.Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Committed);
	TestEqual(TEXT("refilled shot consumes once"), F.Weapon->GetCurrentAmmo(), 2);
	return true;
}
#endif
