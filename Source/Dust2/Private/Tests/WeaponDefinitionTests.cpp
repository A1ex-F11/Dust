#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/UnrealType.h"
#include "FireMode.h"
#include "WeaponDefinitionData.h"
#include "WeaponRecoilData.h"
#include "WeaponRuntime.h"
#include "FiringSpreadMath.h"
#include <initializer_list>
#include <limits>

namespace
{
	class FScopedWeaponDefinitionTestWorld
	{
	public:
		FScopedWeaponDefinitionTestWorld()
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
				// Spawned actors only receive PostInitializeComponents after the
				// world has initialized actors for play.
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		~FScopedWeaponDefinitionTestWorld()
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

	static bool SetEnumValue(FProperty* Property, void* ValueAddress, uint8 Value)
	{
		if (!Property || !ValueAddress)
		{
			return false;
		}

		FNumericProperty* NumericProperty = nullptr;
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			NumericProperty = EnumProperty->GetUnderlyingProperty();
		}
		else
		{
			NumericProperty = CastField<FNumericProperty>(Property);
		}
		if (!NumericProperty)
		{
			return false;
		}

		NumericProperty->SetIntPropertyValue(ValueAddress, static_cast<int64>(Value));
		return true;
	}

	static bool SetWeaponDefinitionTestInt(AWeaponRuntime* Weapon, FName Name, int32 Value)
	{
		FIntProperty* Property = Weapon
			? FindFProperty<FIntProperty>(Weapon->GetClass(), Name)
			: nullptr;
		if (!Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(Weapon, Value);
		return true;
	}

	static bool GetWeaponInt(const AWeaponRuntime* Weapon, FName Name, int32& OutValue)
	{
		const FIntProperty* Property = Weapon
			? FindFProperty<FIntProperty>(Weapon->GetClass(), Name)
			: nullptr;
		if (!Property)
		{
			return false;
		}
		OutValue = Property->GetPropertyValue_InContainer(Weapon);
		return true;
	}

	static bool SetWeaponDefinitionTestFloat(AWeaponRuntime* Weapon, FName Name, float Value)
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

	static bool SetWeaponBool(AWeaponRuntime* Weapon, FName Name, bool Value)
	{
		FBoolProperty* Property = Weapon
			? FindFProperty<FBoolProperty>(Weapon->GetClass(), Name)
			: nullptr;
		if (!Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(Weapon, Value);
		return true;
	}

	static bool SetWeaponMode(AWeaponRuntime* Weapon, FName Name, EFireMode Value)
	{
		FProperty* Property = Weapon
			? FindFProperty<FProperty>(Weapon->GetClass(), Name)
			: nullptr;
		return Property && SetEnumValue(
			Property,
			Property->ContainerPtrToValuePtr<void>(Weapon),
			static_cast<uint8>(Value));
	}

	static bool SetWeaponModeArray(
		AWeaponRuntime* Weapon,
		FName Name,
		std::initializer_list<EFireMode> Values)
	{
		FArrayProperty* Property = Weapon
			? FindFProperty<FArrayProperty>(Weapon->GetClass(), Name)
			: nullptr;
		if (!Property)
		{
			return false;
		}

		FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Weapon));
		Array.EmptyValues();
		for (const EFireMode Value : Values)
		{
			const int32 Index = Array.AddValue();
			if (!SetEnumValue(Property->Inner, Array.GetRawPtr(Index), static_cast<uint8>(Value)))
			{
				return false;
			}
		}
		return true;
	}

	static bool SetWeaponText(AWeaponRuntime* Weapon, FName Name, const FText& Value)
	{
		FTextProperty* Property = Weapon
			? FindFProperty<FTextProperty>(Weapon->GetClass(), Name)
			: nullptr;
		if (!Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(Weapon, Value);
		return true;
	}

	static bool SetWeaponDefinitionTestObject(AWeaponRuntime* Weapon, FName Name, UObject* Value)
	{
		FObjectPropertyBase* Property = Weapon
			? FindFProperty<FObjectPropertyBase>(Weapon->GetClass(), Name)
			: nullptr;
		if (!Property)
		{
			return false;
		}
		Property->SetObjectPropertyValue_InContainer(Weapon, Value);
		return true;
	}

	static AWeaponRuntime* SpawnWeapon(
		FAutomationTestBase& Test,
		UWorld* World,
		UWeaponDefinitionData* Definition,
		bool bRestoreBeforeFinish = false,
		int32 RestoredAmmo = 0,
		int32 RestoredReserve = 0,
		EFireMode RestoredFireMode = EFireMode::SemiAutomatic)
	{
		if (!World)
		{
			Test.AddError(TEXT("Transient weapon definition test world is unavailable."));
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
		if (bRestoreBeforeFinish)
		{
			Weapon->RestoreAmmo(RestoredAmmo, RestoredReserve);
			Weapon->RestoreFireMode(RestoredFireMode);
		}
		Weapon->FinishSpawning(FTransform::Identity);
		return Weapon;
	}

	static UWeaponDefinitionData* MakeBasicDefinition()
	{
		UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
		Definition->WeaponDisplayName = FText::FromString(TEXT("Automation test weapon"));
		Definition->DefaultFireMode = EFireMode::FullAutomatic;
		Definition->AllowedFireModes = { EFireMode::FullAutomatic, EFireMode::Burst };
		Definition->MagazineCapacity = 12;
		Definition->InitialMagazineAmmo = 9;
		Definition->InitialReserveAmmo = 40;
		Definition->FireInterval = 0.25f;
		Definition->BurstShotInterval = 0.08f;
		Definition->BurstRecoveryInterval = 0.31f;
		Definition->StationarySpreadAngle = 0.1f;
		Definition->MovingSpreadAngle = 1.1f;
		Definition->JumpSpreadAngle = 5.0f;
		Definition->SlowWalkSpreadAngle = 0.4f;
		Definition->MovementSpreadSpeedThreshold = 12.0f;
		Definition->bLockReloadAmountAtStart = true;
		Definition->bReloadOneRoundAtATime = false;
		Definition->bUseBatchReloadForFullEmptyReload = true;
		return Definition;
	}

	static float AngleDegrees(const FVector& A, const FVector& B)
	{
		const float Dot = FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0f, 1.0f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponDefinitionLegacyFallbackTest,
	"Dust2.WeaponDefinition.UnboundKeepsLegacyDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponDefinitionLegacyFallbackTest::RunTest(const FString& Parameter)
{
	FScopedWeaponDefinitionTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	AWeaponRuntime* Weapon = TestWorld.Get()->SpawnActorDeferred<AWeaponRuntime>(
		AWeaponRuntime::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Unbound weapon deferred-spawned"), Weapon))
	{
		return false;
	}
	Weapon->WeaponDefinition = nullptr;
	const bool bSetLegacyValues =
		SetWeaponDefinitionTestInt(Weapon, TEXT("CurrentAmmo"), 7) &&
		SetWeaponDefinitionTestInt(Weapon, TEXT("MagazineCapacity"), 11) &&
		SetWeaponDefinitionTestInt(Weapon, TEXT("ReserveAmmo"), 26) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("FireInterval"), 0.37f) &&
		SetWeaponMode(Weapon, TEXT("CurrentFireMode"), EFireMode::Burst);
	TestTrue(TEXT("Legacy test values set before construction finishes"), bSetLegacyValues);
	Weapon->FinishSpawning(FTransform::Identity);

	int32 MagazineCapacity = 0;
	TestNull(TEXT("No gameplay definition is bound"), Weapon->WeaponDefinition.Get());
	TestEqual(TEXT("Legacy current ammo remains unchanged"), Weapon->GetCurrentAmmo(), 7);
	TestTrue(TEXT("Legacy magazine capacity remains unchanged"),
		GetWeaponInt(Weapon, TEXT("MagazineCapacity"), MagazineCapacity) && MagazineCapacity == 11);
	TestEqual(TEXT("Legacy reserve ammo remains unchanged"), Weapon->GetReserveAmmo(), 26);
	TestTrue(TEXT("Legacy fire interval remains unchanged"),
		FMath::IsNearlyEqual(Weapon->GetEffectiveFireInterval(), 0.37f));
	TestTrue(TEXT("Legacy fire mode remains unchanged"), Weapon->GetFireMode() == EFireMode::Burst);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponDefinitionInitialAndRestoredStateTest,
	"Dust2.WeaponDefinition.InitialConfigAndRuntimeRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponDefinitionInitialAndRestoredStateTest::RunTest(const FString& Parameter)
{
	FScopedWeaponDefinitionTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponDefinitionData* Definition = MakeBasicDefinition();
	AWeaponRuntime* FreshWeapon = SpawnWeapon(*this, TestWorld.Get(), Definition);
	AWeaponRuntime* RestoredWeapon = SpawnWeapon(
		*this,
		TestWorld.Get(),
		Definition,
		true,
		4,
		21,
		EFireMode::Burst);
	if (!TestNotNull(TEXT("Fresh weapon spawned"), FreshWeapon) ||
		!TestNotNull(TEXT("Restored weapon spawned"), RestoredWeapon))
	{
		return false;
	}

	int32 MagazineCapacity = 0;
	TestEqual(TEXT("Initial magazine ammo comes from definition"), FreshWeapon->GetCurrentAmmo(), 9);
	TestEqual(TEXT("Initial reserve ammo comes from definition"), FreshWeapon->GetReserveAmmo(), 40);
	TestTrue(TEXT("Initial fire mode comes from definition"), FreshWeapon->GetFireMode() == EFireMode::FullAutomatic);
	TestTrue(TEXT("Magazine capacity comes from definition"),
		GetWeaponInt(FreshWeapon, TEXT("MagazineCapacity"), MagazineCapacity) && MagazineCapacity == 12);
	TestTrue(TEXT("Effective fire interval comes from definition"),
		FMath::IsNearlyEqual(FreshWeapon->GetEffectiveFireInterval(), 0.25f));

	TestEqual(TEXT("Saved current ammo survives PostInitializeComponents"), RestoredWeapon->GetCurrentAmmo(), 4);
	TestEqual(TEXT("Saved reserve ammo survives PostInitializeComponents"), RestoredWeapon->GetReserveAmmo(), 21);
	TestTrue(TEXT("Saved fire mode survives PostInitializeComponents"), RestoredWeapon->GetFireMode() == EFireMode::Burst);
	TestTrue(TEXT("Restored instance retains definition capacity"),
		GetWeaponInt(RestoredWeapon, TEXT("MagazineCapacity"), MagazineCapacity) && MagazineCapacity == 12);

	RestoredWeapon->ApplyDefinitionSettings();
	TestEqual(TEXT("Reapplying static config does not refill current ammo"), RestoredWeapon->GetCurrentAmmo(), 4);
	TestEqual(TEXT("Reapplying static config does not overwrite reserve ammo"), RestoredWeapon->GetReserveAmmo(), 21);
	TestTrue(TEXT("Reapplying static config does not reset current fire mode"), RestoredWeapon->GetFireMode() == EFireMode::Burst);

	TestTrue(TEXT("First instance commits a shot"), RestoredWeapon->TryCommitFire());
	TestEqual(TEXT("Fired instance consumes only its own ammo"), RestoredWeapon->GetCurrentAmmo(), 3);
	TestEqual(TEXT("Other instance keeps its initial ammo"), FreshWeapon->GetCurrentAmmo(), 9);
	TestEqual(TEXT("Shared definition initial ammo stays immutable"), Definition->InitialMagazineAmmo, 9);
	TestEqual(TEXT("Shared definition reserve stays immutable"), Definition->InitialReserveAmmo, 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponDefinitionFiringSpreadLifecycleTest,
	"Dust2.WeaponDefinition.ContinuousFireSpreadLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponDefinitionFiringSpreadLifecycleTest::RunTest(const FString& Parameter)
{
	FScopedWeaponDefinitionTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponDefinitionData* Definition = MakeBasicDefinition();
	Definition->MagazineCapacity = 20;
	Definition->InitialMagazineAmmo = 20;
	Definition->InitialReserveAmmo = 40;
	Definition->FireInterval = 0.3f;
	Definition->StationarySpreadAngle = 0.0f;
	Definition->MovingSpreadAngle = 0.0f;
	Definition->JumpSpreadAngle = 0.0f;
	Definition->SlowWalkSpreadAngle = 0.0f;
	Definition->bUseFiringSpread = true;
	Definition->SpreadPerShot = 0.5f;
	Definition->MaxFiringSpread = 0.8f;
	Definition->SpreadRecoveryDelay = 0.5f;
	Definition->SpreadRecoverySpeed = 2.0f;
	AWeaponRuntime* Weapon = SpawnWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Definition-bound weapon spawned"), Weapon))
	{
		return false;
	}

	FVector Direction = FVector::ZeroVector;
	bool bGenerated = false;
	TestTrue(TEXT("First shot is accepted"), Weapon->TryCommitFire());
	Weapon->GenerateShotDirection(FVector::ForwardVector, 0.0f, false, false, false, Direction, bGenerated);
	TestTrue(TEXT("First shot direction is generated"), bGenerated);
	TestTrue(TEXT("First shot uses zero pre-shot firing spread"),
		Direction.Equals(FVector::ForwardVector, 0.00001f));
	TestTrue(TEXT("Successful first shot adds spread for the next shot"),
		FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.5f, 0.0001f));

	TestFalse(TEXT("Rate-limited request is rejected"), Weapon->TryCommitFire());
	TestTrue(TEXT("Rejected request does not add firing spread"),
		FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.5f, 0.0001f));

	TestWorld.Get()->Tick(LEVELTICK_All, 0.31f);
	TestTrue(TEXT("Next legal continuous shot is accepted"), Weapon->TryCommitFire());
	float MaximumObservedAngle = 0.0f;
	bool bObservedSpreadOffset = false;
	for (int32 SampleIndex = 0; SampleIndex < 16; ++SampleIndex)
	{
		Weapon->GenerateShotDirection(FVector::ForwardVector, 0.0f, false, false, false, Direction, bGenerated);
		TestTrue(TEXT("Continuous shot direction is generated"), bGenerated);
		const float Angle = AngleDegrees(FVector::ForwardVector, Direction);
		MaximumObservedAngle = FMath::Max(MaximumObservedAngle, Angle);
		bObservedSpreadOffset |= Angle > 0.0001f;
	}
	TestTrue(TEXT("Continuous shot uses prior shot spread to vary direction"), bObservedSpreadOffset);
	TestTrue(TEXT("Generated continuous directions stay inside the committed spread cone"),
		MaximumObservedAngle <= 0.5f + 0.0001f);
	TestTrue(TEXT("Spread reaches its configured cap after the second successful shot"),
		FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.8f, 0.0001f));

	TestFalse(TEXT("Second rate-limited request is rejected"), Weapon->TryCommitFire());
	TestTrue(TEXT("Repeated rejected requests do not grow spread past cap"),
		FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.8f, 0.0001f));
	TestWorld.Get()->Tick(LEVELTICK_All, 0.31f);
	TestTrue(TEXT("Third shot is accepted"), Weapon->TryCommitFire());
	TestTrue(TEXT("Successful shots cannot exceed configured spread cap"),
		Weapon->GetCurrentFiringSpread() <= 0.8f + 0.0001f);

	for (int32 RecoveryStep = 0; RecoveryStep < 20; ++RecoveryStep)
	{
		TestWorld.Get()->Tick(LEVELTICK_All, 0.05f);
	}
	TestTrue(TEXT("Spread fully recovers after delay and speed elapse"),
		FMath::IsNearlyZero(Weapon->GetCurrentFiringSpread(), 0.0001f));
	TestTrue(TEXT("First shot after recovery is accepted"), Weapon->TryCommitFire());
	Weapon->GenerateShotDirection(FVector::ForwardVector, 0.0f, false, false, false, Direction, bGenerated);
	TestTrue(TEXT("First post-recovery direction is generated"), bGenerated);
	TestTrue(TEXT("First post-recovery shot is accurate before adding its own spread"),
		Direction.Equals(FVector::ForwardVector, 0.00001f));

	Weapon->ResetRecoilPattern();
	TestTrue(TEXT("Explicit reset clears current spread"),
		FMath::IsNearlyZero(Weapon->GetCurrentFiringSpread(), 0.0001f));
	TestWorld.Get()->Tick(LEVELTICK_All, 0.31f);
	TestTrue(TEXT("A shot after explicit reset is accepted"), Weapon->TryCommitFire());
	TestTrue(TEXT("Successful shot creates spread before reload"), Weapon->GetCurrentFiringSpread() > 0.0f);
	TestTrue(TEXT("Reload starts for the partially used magazine"), Weapon->TryStartReload());
	TestTrue(TEXT("Starting reload clears accumulated firing spread"),
		FMath::IsNearlyZero(Weapon->GetCurrentFiringSpread(), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponDefinitionCaptureDeepCopyTest,
	"Dust2.WeaponDefinition.CaptureCopiesNativeAndRecoilData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponDefinitionCaptureDeepCopyTest::RunTest(const FString& Parameter)
{
	FScopedWeaponDefinitionTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	AWeaponRuntime* Weapon = SpawnWeapon(*this, TestWorld.Get(), nullptr);
	if (!TestNotNull(TEXT("Unbound weapon spawned for capture"), Weapon))
	{
		return false;
	}

	UWeaponRecoilData* SourceRecoil = NewObject<UWeaponRecoilData>(Weapon);
	SourceRecoil->bUseFixedPattern = true;
	SourceRecoil->FixedPattern = { FVector2D(0.7f, -0.1f), FVector2D(0.9f, 0.15f) };
	SourceRecoil->PatternResetDelay = 0.42f;
	const bool bSetSourceWeapon =
		SetWeaponText(Weapon, TEXT("WeaponDisplayName"), FText::FromString(TEXT("Captured weapon"))) &&
		SetWeaponMode(Weapon, TEXT("CurrentFireMode"), EFireMode::Burst) &&
		SetWeaponModeArray(Weapon, TEXT("AllowedFireModes"), { EFireMode::SemiAutomatic, EFireMode::Burst }) &&
		SetWeaponDefinitionTestInt(Weapon, TEXT("CurrentAmmo"), 6) &&
		SetWeaponDefinitionTestInt(Weapon, TEXT("MagazineCapacity"), 9) &&
		SetWeaponDefinitionTestInt(Weapon, TEXT("ReserveAmmo"), 47) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("FireInterval"), 0.38f) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("BurstShotInterval"), 0.09f) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("StationarySpreadAngle"), 0.2f) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("MovingSpreadAngle"), 1.3f) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("JumpSpreadAngle"), 6.0f) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("SlowWalkSpreadAngle"), 0.5f) &&
		SetWeaponDefinitionTestFloat(Weapon, TEXT("MovementSpreadSpeedThreshold"), 13.0f) &&
		SetWeaponBool(Weapon, TEXT("bLockReloadAmountAtStart"), true) &&
		SetWeaponBool(Weapon, TEXT("bReloadOneRoundAtATime"), true) &&
		SetWeaponBool(Weapon, TEXT("bUseBatchReloadForFullEmptyReload"), false) &&
		SetWeaponDefinitionTestObject(Weapon, TEXT("RecoilData"), SourceRecoil);
	TestTrue(TEXT("Source weapon values set through existing reflected properties"), bSetSourceWeapon);

	UWeaponDefinitionData* Captured = NewObject<UWeaponDefinitionData>(GetTransientPackage());
	Weapon->CaptureDefinition(Captured);
	TestEqual(TEXT("Capture copies current magazine as initial magazine ammo"), Captured->InitialMagazineAmmo, 6);
	TestEqual(TEXT("Capture copies current reserve as initial reserve ammo"), Captured->InitialReserveAmmo, 47);
	TestEqual(TEXT("Capture copies magazine capacity"), Captured->MagazineCapacity, 9);
	TestTrue(TEXT("Capture copies default fire mode"), Captured->DefaultFireMode == EFireMode::Burst);
	TestEqual(TEXT("Capture copies fire interval"), Captured->FireInterval, 0.38f);
	TestEqual(TEXT("Capture copies burst interval"), Captured->BurstShotInterval, 0.09f);
	TestTrue(TEXT("Capture copies allowed fire modes"),
		Captured->AllowedFireModes.Num() == 2 &&
		Captured->AllowedFireModes[0] == EFireMode::SemiAutomatic &&
		Captured->AllowedFireModes[1] == EFireMode::Burst);
	TestTrue(TEXT("Capture copies recoil settings into a distinct asset object"),
		Captured->RecoilData && Captured->RecoilData != SourceRecoil);
	if (Captured->RecoilData)
	{
		TestEqual(TEXT("Captured recoil pattern length matches source"), Captured->RecoilData->FixedPattern.Num(), 2);
		TestTrue(TEXT("Captured recoil pattern data matches source"),
			Captured->RecoilData->FixedPattern[0].Equals(FVector2D(0.7f, -0.1f), 0.0001f));
		TestTrue(TEXT("Captured recoil timing matches source"),
			FMath::IsNearlyEqual(Captured->RecoilData->PatternResetDelay, 0.42f, 0.0001f));
		SourceRecoil->FixedPattern[0].X = 8.0f;
		TestTrue(TEXT("Editing source recoil does not mutate captured recoil"),
			FMath::IsNearlyEqual(Captured->RecoilData->FixedPattern[0].X, 0.7f, 0.0001f));
		Captured->RecoilData->FixedPattern[0].Y = -9.0f;
		TestTrue(TEXT("Editing captured recoil does not mutate source recoil"),
			FMath::IsNearlyEqual(SourceRecoil->FixedPattern[0].Y, -0.1f, 0.0001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFiringSpreadMathBoundaryTest,
	"Dust2.WeaponDefinition.FiringSpreadMathBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFiringSpreadMathBoundaryTest::RunTest(const FString& Parameter)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Negative spread inputs clamp to zero"),
		FMath::IsNearlyZero(FFiringSpreadMath::AddShot(-0.4f, -0.2f, 5.0f)));
	TestTrue(TEXT("NaN accumulated spread is safe"),
		FMath::IsNearlyZero(FFiringSpreadMath::AddShot(NaN, 0.4f, 5.0f)));
	TestTrue(TEXT("NaN per-shot spread is safe"),
		FMath::IsNearlyZero(FFiringSpreadMath::AddShot(0.4f, NaN, 5.0f)));
	TestTrue(TEXT("NaN maximum is safe"),
		FMath::IsNearlyZero(FFiringSpreadMath::AddShot(0.4f, 0.4f, NaN)));
	TestTrue(TEXT("Spread addition clamps at its maximum"),
		FMath::IsNearlyEqual(FFiringSpreadMath::AddShot(0.7f, 0.5f, 0.8f), 0.8f));

	TestTrue(TEXT("Recovery does not begin at or before its delay"),
		FMath::IsNearlyEqual(FFiringSpreadMath::Recover(0.8f, 0.5f, 0.5f, 2.0f, 1.0f), 0.8f) &&
		FMath::IsNearlyEqual(FFiringSpreadMath::Recover(0.8f, 0.4f, 0.5f, 2.0f, 1.0f), 0.8f));
	TestTrue(TEXT("Recovery after the delay uses only elapsed recovery time"),
		FMath::IsNearlyEqual(FFiringSpreadMath::Recover(0.8f, 0.75f, 0.5f, 2.0f, 1.0f), 0.3f, 0.0001f));
	TestTrue(TEXT("Recovery result never goes below zero"),
		FMath::IsNearlyZero(FFiringSpreadMath::Recover(0.8f, 10.0f, 0.5f, 2.0f, 1.0f)));
	TestTrue(TEXT("NaN and negative recovery inputs are safe"),
		FMath::IsNearlyZero(FFiringSpreadMath::Recover(NaN, NaN, -1.0f, NaN, 1.0f)));
	return true;
}

#endif
