#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/UnrealType.h"
#include "Weapons/FireMode.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Weapons/WeaponFireRateMath.h"
#include "Weapons/WeaponRuntime.h"
#include <limits>

namespace
{
	class FScopedWeaponFireRateTestWorld
	{
	public:
		FScopedWeaponFireRateTestWorld()
		{
			UWorld::InitializationValues InitValues;
			InitValues
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false);

			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				NAME_None,
				nullptr,
				false,
				ERHIFeatureLevel::Num,
				&InitValues);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			if (World)
			{
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		~FScopedWeaponFireRateTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World = nullptr;
			}
		}

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};

	static UWeaponDefinitionData* MakeFireRateDefinition()
	{
		UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
		Definition->DefaultFireMode = EFireMode::FullAutomatic;
		Definition->AllowedFireModes = { EFireMode::FullAutomatic, EFireMode::Burst };
		Definition->MagazineCapacity = 20;
		Definition->InitialMagazineAmmo = 20;
		Definition->InitialReserveAmmo = 0;
		return Definition;
	}

	static AWeaponRuntime* SpawnFireRateWeapon(
		FAutomationTestBase& Test,
		UWorld* World,
		UWeaponDefinitionData* Definition)
	{
		if (!World)
		{
			Test.AddError(TEXT("Transient fire-rate test world is unavailable."));
			return nullptr;
		}

		AWeaponRuntime* Weapon = World->SpawnActorDeferred<AWeaponRuntime>(
			AWeaponRuntime::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Weapon)
		{
			Test.AddError(TEXT("AWeaponRuntime could not be deferred-spawned."));
			return nullptr;
		}

		Weapon->WeaponDefinition = Definition;
		Weapon->FinishSpawning(FTransform::Identity);
		return Weapon;
	}

	static bool SetWeaponFloat(AWeaponRuntime* Weapon, FName Name, float Value)
	{
		FFloatProperty* Property = Weapon
			? FindFProperty<FFloatProperty>(Weapon->GetClass(), Name)
			: nullptr;
		if (!Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(Weapon, Value);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponFireRateRpmPriorityTest,
	"Dust2.WeaponFireRate.RpmDrivesCooldownAndEffectiveRpm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponFireRateRpmPriorityTest::RunTest(const FString& Parameter)
{
	FScopedWeaponFireRateTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponDefinitionData* Definition = MakeFireRateDefinition();
	Definition->FireInterval = 0.15f;
	Definition->RoundsPerMinute = 600.0f;
	AWeaponRuntime* Weapon = SpawnFireRateWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("RPM weapon spawned"), Weapon))
	{
		return false;
	}

	TestTrue(TEXT("600 RPM resolves to 0.1 seconds"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveFireInterval(), 0.1f, 0.000001f));
	TestTrue(TEXT("Effective RPM matches the effective interval"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveRoundsPerMinute(), 600.0f, 0.01f));
	TestTrue(TEXT("A valid RPM wins over the retained legacy seconds"),
		!FMath::IsNearlyEqual(Weapon->GetEffectiveFireInterval(), Definition->FireInterval, 0.000001f));

	TestTrue(TEXT("First shot accepted"), Weapon->TryCommitFire());
	TestFalse(TEXT("Shot inside the RPM cooldown rejected"), Weapon->TryCommitFire());
	TestWorld.Get()->Tick(LEVELTICK_All, 0.05f);
	TestFalse(TEXT("Half of the cooldown is not enough"), Weapon->TryCommitFire());
	TestWorld.Get()->Tick(LEVELTICK_All, 0.06f);
	TestTrue(TEXT("Shot after the cooldown accepted"), Weapon->TryCommitFire());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponFireRateLegacyAndBurstTest,
	"Dust2.WeaponFireRate.LegacyFallbackAndBurstSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponFireRateLegacyAndBurstTest::RunTest(const FString& Parameter)
{
	FScopedWeaponFireRateTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponDefinitionData* Definition = MakeFireRateDefinition();
	Definition->RoundsPerMinute = 0.0f;
	Definition->FireInterval = 0.25f;
	Definition->BurstShotInterval = 0.08f;
	AWeaponRuntime* Weapon = SpawnFireRateWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Legacy fire-rate weapon spawned"), Weapon))
	{
		return false;
	}

	TestTrue(TEXT("Unconfigured RPM uses the retained seconds"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveFireInterval(), 0.25f, 0.000001f));
	TestTrue(TEXT("Effective RPM mirrors the retained seconds"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveRoundsPerMinute(), 240.0f, 0.01f));

	Weapon->RestoreFireMode(EFireMode::Burst);
	TestTrue(TEXT("BurstShotInterval stays a seconds value"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveFireInterval(), 0.08f, 0.000001f));
	TestTrue(TEXT("Burst mode reports its own cadence"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveRoundsPerMinute(), 750.0f, 0.01f));

	Definition->BurstShotInterval = 0.0f;
	Weapon->ApplyDefinitionSettings();
	Weapon->RestoreFireMode(EFireMode::Burst);
	TestTrue(TEXT("Zero BurstShotInterval falls back to normal cadence"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveFireInterval(), 0.25f, 0.000001f));
	TestTrue(TEXT("Effective RPM follows the same normal fallback"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveRoundsPerMinute(), 240.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponFireRatePostLoadMigrationTest,
	"Dust2.WeaponFireRate.PostLoadMigratesLegacyOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponFireRatePostLoadMigrationTest::RunTest(const FString& Parameter)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();

	UWeaponDefinitionData* Legacy = MakeFireRateDefinition();
	Legacy->RoundsPerMinute = 0.0f;
	Legacy->FireInterval = 0.15f;
	Legacy->PostLoad();
	TestTrue(TEXT("Legacy FireInterval converts to RPM"),
		FMath::IsNearlyEqual(Legacy->RoundsPerMinute, 400.0f, 0.01f));
	TestEqual(TEXT("Legacy serialized FireInterval is preserved"), Legacy->FireInterval, 0.15f);
	TestTrue(TEXT("Migration records the RPM as configured"), Legacy->bHasExplicitRoundsPerMinute);
	TestTrue(TEXT("Migrated RPM keeps the same effective seconds"),
		FMath::IsNearlyEqual(Legacy->GetNormalFireIntervalSeconds(), 0.15f, 0.000001f));

	UWeaponDefinitionData* Configured = MakeFireRateDefinition();
	Configured->RoundsPerMinute = 500.0f;
	Configured->FireInterval = 0.15f;
	Configured->PostLoad();
	TestTrue(TEXT("PostLoad does not overwrite a valid new RPM"),
		FMath::IsNearlyEqual(Configured->RoundsPerMinute, 500.0f));
	TestTrue(TEXT("The retained new RPM stays authoritative after PostLoad"),
		FMath::IsNearlyEqual(Configured->GetNormalFireIntervalSeconds(), 0.12f, 0.000001f));

	UWeaponDefinitionData* InvalidRpm = MakeFireRateDefinition();
	InvalidRpm->RoundsPerMinute = -5.0f;
	InvalidRpm->FireInterval = 0.2f;
	InvalidRpm->PostLoad();
	TestTrue(TEXT("Invalid RPM is not silently replaced"),
		FMath::IsNearlyEqual(InvalidRpm->RoundsPerMinute, -5.0f));
	TestTrue(TEXT("Invalid RPM falls back to the retained seconds"),
		FMath::IsNearlyEqual(InvalidRpm->GetNormalFireIntervalSeconds(), 0.2f, 0.000001f));

	UWeaponDefinitionData* InvalidLegacy = MakeFireRateDefinition();
	InvalidLegacy->RoundsPerMinute = 0.0f;
	InvalidLegacy->FireInterval = NaN;
	InvalidLegacy->PostLoad();
	TestTrue(TEXT("Invalid legacy seconds are not converted into fabricated RPM"),
		FMath::IsNearlyEqual(InvalidLegacy->RoundsPerMinute, 0.0f));
	TestTrue(TEXT("Invalid legacy seconds use the 0.001s floor"),
		FMath::IsNearlyEqual(InvalidLegacy->GetNormalFireIntervalSeconds(), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponFireRateSafeBoundariesTest,
	"Dust2.WeaponFireRate.InvalidValuesNeverCreateInfiniteFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponFireRateSafeBoundariesTest::RunTest(const FString& Parameter)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();

	TestTrue(TEXT("Zero RPM resolves to the retained seconds"),
		FMath::IsNearlyEqual(DustWeaponFireRate::ResolveNormalInterval(0.0f, 0.25f), 0.25f));
	TestTrue(TEXT("Negative RPM resolves to the retained seconds"),
		FMath::IsNearlyEqual(DustWeaponFireRate::ResolveNormalInterval(-1.0f, 0.25f), 0.25f));
	TestTrue(TEXT("NaN RPM resolves to the retained seconds"),
		FMath::IsNearlyEqual(DustWeaponFireRate::ResolveNormalInterval(NaN, 0.25f), 0.25f));
	TestTrue(TEXT("Infinite RPM resolves to the retained seconds"),
		FMath::IsNearlyEqual(DustWeaponFireRate::ResolveNormalInterval(Infinity, 0.25f), 0.25f));
	TestTrue(TEXT("Zero legacy seconds use the 0.001s floor"),
		FMath::IsNearlyEqual(DustWeaponFireRate::ResolveNormalInterval(0.0f, 0.0f), 0.001f));
	TestTrue(TEXT("Invalid legacy seconds use the 0.001s floor"),
		FMath::IsNearlyEqual(DustWeaponFireRate::ResolveNormalInterval(NaN, NaN), 0.001f));
	TestTrue(TEXT("Interval conversion understands the floor"),
		FMath::IsNearlyEqual(DustWeaponFireRate::IntervalToRoundsPerMinute(0.0f), 60000.0f, 0.1f));
	TestTrue(TEXT("A huge RPM cannot schedule below the floor"),
		FMath::IsNearlyEqual(DustWeaponFireRate::RoundsPerMinuteToInterval(1.0e9f), 0.001f));

	FScopedWeaponFireRateTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponDefinitionData* Definition = MakeFireRateDefinition();
	Definition->RoundsPerMinute = 0.0f;
	Definition->FireInterval = 0.0f;
	AWeaponRuntime* Weapon = SpawnFireRateWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Floored weapon spawned"), Weapon))
	{
		return false;
	}

	TestTrue(TEXT("Zero/zero uses the 0.001s floor"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveFireInterval(), 0.001f));
	TestTrue(TEXT("Effective RPM matches the floor"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveRoundsPerMinute(), 60000.0f, 0.1f));
	TestTrue(TEXT("First shot accepted"), Weapon->TryCommitFire());
	TestFalse(TEXT("Immediate refire rejected"), Weapon->TryCommitFire());
	TestWorld.Get()->Tick(LEVELTICK_All, 0.0005f);
	TestFalse(TEXT("Half the floor is still rejected"), Weapon->TryCommitFire());
	TestWorld.Get()->Tick(LEVELTICK_All, 0.0006f);
	TestTrue(TEXT("Refire after the floor accepted"), Weapon->TryCommitFire());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponFireRateCaptureTest,
	"Dust2.WeaponFireRate.CaptureKeepsRpmAndLegacySecondsAligned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponFireRateCaptureTest::RunTest(const FString& Parameter)
{
	FScopedWeaponFireRateTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	AWeaponRuntime* Weapon = SpawnFireRateWeapon(*this, TestWorld.Get(), nullptr);
	if (!TestNotNull(TEXT("Unbound weapon spawned for capture"), Weapon))
	{
		return false;
	}
	TestTrue(TEXT("Legacy runtime fire rate set through its reflected field"),
		SetWeaponFloat(Weapon, TEXT("FireInterval"), 0.38f));

	UWeaponDefinitionData* Captured = NewObject<UWeaponDefinitionData>(GetTransientPackage());
	Weapon->CaptureDefinition(Captured);
	TestEqual(TEXT("Capture preserves the exact legacy seconds"), Captured->FireInterval, 0.38f);
	TestTrue(TEXT("Capture writes the equivalent RPM"),
		FMath::IsNearlyEqual(Captured->RoundsPerMinute, 60.0f / 0.38f, 0.001f));
	TestTrue(TEXT("Capture marks the RPM as configured"), Captured->bHasExplicitRoundsPerMinute);

	AWeaponRuntime* Rebuilt = SpawnFireRateWeapon(*this, TestWorld.Get(), Captured);
	if (!TestNotNull(TEXT("Captured definition can drive a runtime"), Rebuilt))
	{
		return false;
	}
	TestTrue(TEXT("Captured data resolves to the same interval"),
		FMath::IsNearlyEqual(Rebuilt->GetEffectiveFireInterval(), 0.38f, 0.000001f));

	// A bound definition with a stale legacy mirror must capture the resolved pair, not the stale
	// seconds value.
	UWeaponDefinitionData* BoundSource = MakeFireRateDefinition();
	BoundSource->RoundsPerMinute = 600.0f;
	BoundSource->FireInterval = 0.15f;
	AWeaponRuntime* BoundWeapon = SpawnFireRateWeapon(*this, TestWorld.Get(), BoundSource);
	if (!TestNotNull(TEXT("Bound source weapon spawned"), BoundWeapon))
	{
		return false;
	}
	UWeaponDefinitionData* BoundCaptured = NewObject<UWeaponDefinitionData>(GetTransientPackage());
	BoundWeapon->CaptureDefinition(BoundCaptured);
	TestTrue(TEXT("Bound capture keeps the source RPM"),
		FMath::IsNearlyEqual(BoundCaptured->RoundsPerMinute, 600.0f, 0.01f));
	TestTrue(TEXT("Bound capture replaces a stale mirror with the resolved seconds"),
		FMath::IsNearlyEqual(BoundCaptured->FireInterval, 0.1f, 0.000001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponFireRateAccuracyProfileTest,
	"Dust2.WeaponFireRate.AccuracyProfileUsesEffectiveInterval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponFireRateAccuracyProfileTest::RunTest(const FString& Parameter)
{
	FScopedWeaponFireRateTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	const auto MakeAccuracyDefinition = [](float RoundsPerMinute)
	{
		UWeaponDefinitionData* Definition = MakeFireRateDefinition();
		Definition->bUseIntuitiveHipAccuracy = true;
		Definition->HipAccuracy.BaseSpreadAngle = 0.0f;
		Definition->HipAccuracy.MaxFiringExtraAngle = 1.0f;
		Definition->HipAccuracy.ShotsToMaximum = 10;
		Definition->HipAccuracy.FullRecoverySeconds = 1.0f;
		Definition->RoundsPerMinute = RoundsPerMinute;
		Definition->FireInterval = 0.15f;
		return Definition;
	};

	AWeaponRuntime* Slow = SpawnFireRateWeapon(*this, TestWorld.Get(), MakeAccuracyDefinition(300.0f));
	AWeaponRuntime* Fast = SpawnFireRateWeapon(*this, TestWorld.Get(), MakeAccuracyDefinition(600.0f));
	if (!TestNotNull(TEXT("Slow accuracy weapon spawned"), Slow) ||
		!TestNotNull(TEXT("Fast accuracy weapon spawned"), Fast))
	{
		return false;
	}

	TestTrue(TEXT("Slow weapon commits a shot"), Slow->TryCommitFire());
	TestTrue(TEXT("Fast weapon commits a shot"), Fast->TryCommitFire());

	// BuildProfile derives SpreadPerShot from the effective interval. The slower cadence gets a
	// larger per-shot increment because there is more recovery time between shots.
	const float SlowPerShot = Slow->GetCurrentFiringSpread();
	const float FastPerShot = Fast->GetCurrentFiringSpread();
	TestTrue(TEXT("Accuracy profile consumes the RPM-derived interval"), SlowPerShot > FastPerShot);
	return true;
}

#endif
