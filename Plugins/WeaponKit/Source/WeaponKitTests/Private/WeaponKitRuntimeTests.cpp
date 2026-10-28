#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Data/WeaponKitBallisticsData.h"
#include "Data/WeaponKitDefinitionData.h"
#include "Data/WeaponKitRecoilData.h"
#include "Runtime/WeaponKitRuntime.h"
#include "WeaponKitTestWorld.h"

namespace
{
UWeaponKitDefinitionData* MakeWeaponKitDefinition()
{
	UWeaponKitDefinitionData* Definition = NewObject<UWeaponKitDefinitionData>(GetTransientPackage());
	Definition->WeaponDisplayName = FText::FromString(TEXT("Automation test weapon"));
	Definition->DefaultFireMode = EWeaponKitFireMode::FullAutomatic;
	Definition->AllowedFireModes = { EWeaponKitFireMode::FullAutomatic, EWeaponKitFireMode::Burst };
	Definition->MagazineCapacity = 12;
	Definition->InitialMagazineAmmo = 9;
	Definition->InitialReserveAmmo = 40;
	Definition->FireInterval = 0.25f;
	Definition->BurstShotInterval = 0.08f;
	Definition->BurstRecoveryInterval = 0.31f;
	Definition->HipFire.StationarySpreadAngle = 0.1f;
	Definition->HipFire.MovingSpreadAngle = 1.1f;
	Definition->HipFire.JumpSpreadAngle = 5.0f;
	Definition->HipFire.SlowWalkSpreadAngle = 0.4f;
	Definition->HipFire.MovementSpreadSpeedThreshold = 12.0f;
	Definition->bLockReloadAmountAtStart = true;
	return Definition;
}

AWeaponKitRuntime* SpawnWeaponKitWeapon(
	FAutomationTestBase& Test,
	UWorld* World,
	UWeaponKitDefinitionData* Definition,
	bool bRestoreBeforeFinish = false,
	int32 RestoredAmmo = 0,
	int32 RestoredReserve = 0,
	EWeaponKitFireMode RestoredFireMode = EWeaponKitFireMode::SemiAutomatic)
{
	if (!World)
	{
		Test.AddError(TEXT("Transient weapon test world is unavailable."));
		return nullptr;
	}
	AWeaponKitRuntime* Weapon = World->SpawnActorDeferred<AWeaponKitRuntime>(
		AWeaponKitRuntime::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Weapon)
	{
		Test.AddError(TEXT("AWeaponKitRuntime could not be deferred-spawned."));
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

float AngleDegrees(const FVector& A, const FVector& B)
{
	const float Dot = FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0f, 1.0f);
	return FMath::RadiansToDegrees(FMath::Acos(Dot));
}

bool HasSpread(AWeaponKitRuntime* Weapon, float Speed = 0.0f, bool bFalling = false)
{
	bool bVaried = false;
	for (int32 Index = 0; Index < 32; ++Index)
	{
		FVector Direction = FVector::ZeroVector;
		bool bGenerated = false;
		Weapon->GenerateShotDirection(FVector::ForwardVector, Speed, bFalling, false, false, Direction, bGenerated);
		if (!bGenerated)
		{
			return false;
		}
		bVaried |= !Direction.Equals(FVector::ForwardVector, 0.00001f);
	}
	return bVaried;
}

bool AllDirectionsExact(AWeaponKitRuntime* Weapon, const FVector& Base, float Speed = 0.0f, bool bFalling = false)
{
	for (int32 Index = 0; Index < 32; ++Index)
	{
		FVector Direction = FVector::ZeroVector;
		bool bGenerated = false;
		Weapon->GenerateShotDirection(Base, Speed, bFalling, false, false, Direction, bGenerated);
		if (!bGenerated || !Direction.Equals(Base, 0.00001f))
		{
			return false;
		}
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitInitialStateTest,
	"WeaponKit.Runtime.InitialConfigAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitInitialStateTest::RunTest(const FString& Parameter)
{
	FWeaponKitTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponKitDefinitionData* Definition = MakeWeaponKitDefinition();
	TestTrue(TEXT("Inline recoil subobject is public"), Definition->RecoilData && Definition->RecoilData->HasAnyFlags(RF_Public));
	TestTrue(TEXT("Inline ballistics subobject is public"), Definition->BallisticsData && Definition->BallisticsData->HasAnyFlags(RF_Public));

	AWeaponKitRuntime* FreshWeapon = SpawnWeaponKitWeapon(*this, TestWorld.Get(), Definition);
	AWeaponKitRuntime* RestoredWeapon = SpawnWeaponKitWeapon(*this, TestWorld.Get(), Definition, true, 4, 21, EWeaponKitFireMode::Burst);
	if (!TestNotNull(TEXT("Fresh weapon spawned"), FreshWeapon) || !TestNotNull(TEXT("Restored weapon spawned"), RestoredWeapon))
	{
		return false;
	}

	TestEqual(TEXT("Initial magazine ammo comes from definition"), FreshWeapon->GetCurrentAmmo(), 9);
	TestEqual(TEXT("Initial reserve ammo comes from definition"), FreshWeapon->GetReserveAmmo(), 40);
	TestTrue(TEXT("Initial fire mode comes from definition"), FreshWeapon->GetFireMode() == EWeaponKitFireMode::FullAutomatic);
	TestEqual(TEXT("Magazine capacity comes from definition"), FreshWeapon->GetMagazineCapacity(), 12);
	TestTrue(TEXT("Effective fire interval comes from definition"), FMath::IsNearlyEqual(FreshWeapon->GetEffectiveFireInterval(), 0.25f));
	TestTrue(TEXT("Burst recovery is read from the definition"), FMath::IsNearlyEqual(FreshWeapon->GetBurstRecoveryInterval(), 0.31f));
	TestEqual(TEXT("Ballistics object is the definition subobject"), FreshWeapon->GetBallistics(), Definition->BallisticsData.Get());

	TestEqual(TEXT("Saved current ammo survives PostInitializeComponents"), RestoredWeapon->GetCurrentAmmo(), 4);
	TestEqual(TEXT("Saved reserve ammo survives PostInitializeComponents"), RestoredWeapon->GetReserveAmmo(), 21);
	TestTrue(TEXT("Saved fire mode survives PostInitializeComponents"), RestoredWeapon->GetFireMode() == EWeaponKitFireMode::Burst);
	TestEqual(TEXT("Restored instance retains definition capacity"), RestoredWeapon->GetMagazineCapacity(), 12);

	RestoredWeapon->ApplyDefinitionSettings();
	TestEqual(TEXT("Reapplying static config does not refill current ammo"), RestoredWeapon->GetCurrentAmmo(), 4);
	TestEqual(TEXT("Reapplying static config does not overwrite reserve ammo"), RestoredWeapon->GetReserveAmmo(), 21);
	TestTrue(TEXT("Reapplying static config does not reset current fire mode"), RestoredWeapon->GetFireMode() == EWeaponKitFireMode::Burst);

	TestTrue(TEXT("First instance commits a shot"), RestoredWeapon->TryCommitFire());
	TestEqual(TEXT("Fired instance consumes only its own ammo"), RestoredWeapon->GetCurrentAmmo(), 3);
	TestEqual(TEXT("Other instance keeps its initial ammo"), FreshWeapon->GetCurrentAmmo(), 9);
	TestEqual(TEXT("Shared definition initial ammo stays immutable"), Definition->InitialMagazineAmmo, 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitFiringSpreadLifecycleTest,
	"WeaponKit.Runtime.ContinuousFireSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitFiringSpreadLifecycleTest::RunTest(const FString& Parameter)
{
	FWeaponKitTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}

	UWeaponKitDefinitionData* Definition = MakeWeaponKitDefinition();
	Definition->MagazineCapacity = 20;
	Definition->InitialMagazineAmmo = 20;
	Definition->InitialReserveAmmo = 40;
	Definition->FireInterval = 0.3f;
	Definition->HipFire.StationarySpreadAngle = 0.0f;
	Definition->HipFire.MovingSpreadAngle = 0.0f;
	Definition->HipFire.JumpSpreadAngle = 0.0f;
	Definition->HipFire.SlowWalkSpreadAngle = 0.0f;
	Definition->HipFire.bUseFiringSpread = true;
	Definition->HipFire.SpreadPerShot = 0.5f;
	Definition->HipFire.MaxFiringSpread = 0.8f;
	Definition->HipFire.SpreadRecoveryDelay = 0.5f;
	Definition->HipFire.SpreadRecoverySpeed = 2.0f;

	AWeaponKitRuntime* Weapon = SpawnWeaponKitWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Definition-bound weapon spawned"), Weapon))
	{
		return false;
	}

	FVector Direction = FVector::ZeroVector;
	bool bGenerated = false;
	TestTrue(TEXT("First shot is accepted"), Weapon->TryCommitFire());
	Weapon->GenerateShotDirection(FVector::ForwardVector, 0.0f, false, false, false, Direction, bGenerated);
	TestTrue(TEXT("First shot direction is generated"), bGenerated);
	TestTrue(TEXT("First shot uses zero pre-shot firing spread"), Direction.Equals(FVector::ForwardVector, 0.00001f));
	TestTrue(TEXT("Successful first shot adds spread for the next shot"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.5f, 0.0001f));
	TestFalse(TEXT("Rate-limited request is rejected"), Weapon->TryCommitFire());
	TestTrue(TEXT("Rejected request does not add firing spread"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.5f, 0.0001f));

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
	TestTrue(TEXT("Generated continuous directions stay inside the committed spread cone"), MaximumObservedAngle <= 0.5f + 0.0001f);
	TestTrue(TEXT("Spread reaches its configured cap after the second successful shot"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.8f, 0.0001f));

	for (int32 RecoveryStep = 0; RecoveryStep < 20; ++RecoveryStep)
	{
		TestWorld.Get()->Tick(LEVELTICK_All, 0.05f);
	}
	TestTrue(TEXT("Spread fully recovers after delay and speed elapse"), FMath::IsNearlyZero(Weapon->GetCurrentFiringSpread(), 0.0001f));
	Weapon->ResetRecoilPattern();
	TestTrue(TEXT("Explicit reset clears current spread"), FMath::IsNearlyZero(Weapon->GetCurrentFiringSpread(), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitAimSpreadTest,
	"WeaponKit.Runtime.AimSpreadSnapshotAndBloom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitAimSpreadTest::RunTest(const FString& Parameter)
{
	FWeaponKitTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}
	UWeaponKitDefinitionData* Definition = MakeWeaponKitDefinition();
	Definition->FireInterval = 0.01f;
	Definition->RecoilData = nullptr;
	Definition->bUseSeparateAimingSpread = true;
	Definition->HipFire.StationarySpreadAngle = 6.0f;
	Definition->HipFire.MovingSpreadAngle = 6.0f;
	Definition->HipFire.JumpSpreadAngle = 6.0f;
	Definition->HipFire.SlowWalkSpreadAngle = 6.0f;
	Definition->Aiming.StationarySpreadAngle = 0.0f;
	Definition->Aiming.MovingSpreadAngle = 0.0f;
	Definition->Aiming.JumpSpreadAngle = 0.0f;
	Definition->Aiming.SlowWalkSpreadAngle = 0.0f;

	AWeaponKitRuntime* Weapon = SpawnWeaponKitWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Weapon spawned"), Weapon))
	{
		return false;
	}

	TestTrue(TEXT("Hip shot accepted"), Weapon->TryCommitFire());
	Weapon->SetAiming(true);
	TestTrue(TEXT("Already committed hip shot retains the hip cone"), HasSpread(Weapon));
	TestWorld.Get()->Tick(LEVELTICK_All, 0.02f);
	TestTrue(TEXT("Aim shot accepted"), Weapon->TryCommitFire());
	Weapon->SetAiming(false);
	TestTrue(TEXT("Committed aim shot stays exact after release"), AllDirectionsExact(Weapon, FVector::ForwardVector));

	Definition->HipFire.bUseFiringSpread = Definition->Aiming.bUseFiringSpread = true;
	Definition->HipFire.SpreadPerShot = 0.5f;
	Definition->HipFire.MaxFiringSpread = 2.0f;
	Definition->HipFire.SpreadRecoveryDelay = Definition->Aiming.SpreadRecoveryDelay = 0.5f;
	Definition->HipFire.SpreadRecoverySpeed = Definition->Aiming.SpreadRecoverySpeed = 1.0f;
	Definition->Aiming.SpreadPerShot = 0.1f;
	Definition->Aiming.MaxFiringSpread = 0.4f;
	Weapon->ResetRecoilPattern();
	Weapon->SetAiming(false);
	TestWorld.Get()->Tick(LEVELTICK_All, 0.02f);

	TestTrue(TEXT("Bloom hip shot accepted"), Weapon->TryCommitFire());
	TestTrue(TEXT("Hip bloom advances"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.5f));
	Weapon->SetAiming(true);
	TestTrue(TEXT("Aim bloom remembers the hip shot"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.1f));
	TestWorld.Get()->Tick(LEVELTICK_All, 0.02f);
	TestTrue(TEXT("Bloom aim shot accepted"), Weapon->TryCommitFire());
	TestTrue(TEXT("Aim bloom advances on its own profile"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.2f));
	Weapon->SetAiming(false);
	TestTrue(TEXT("Hip bloom remembers the aim shot"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 1.0f));
	Weapon->SetAiming(true);
	TestTrue(TEXT("Toggling aim does not clear aim bloom"), FMath::IsNearlyEqual(Weapon->GetCurrentFiringSpread(), 0.2f));
	Definition->bUseSeparateAimingSpread = false;
	TestWorld.Get()->Tick(LEVELTICK_All, 0.02f);
	TestTrue(TEXT("Shared-profile shot accepted"), Weapon->TryCommitFire());
	TestTrue(TEXT("Disabling the separate profile uses hip spread while aiming"), HasSpread(Weapon));
	TestEqual(TEXT("Shooting does not mutate the shared definition"), Definition->HipFire.StationarySpreadAngle, 6.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitAimDirectionTest,
	"WeaponKit.Runtime.AimIgnoresAccumulatedShotRecoil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitAimDirectionTest::RunTest(const FString& Parameter)
{
	FWeaponKitTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient world created"), TestWorld.Get()))
	{
		return false;
	}
	UWeaponKitDefinitionData* Definition = MakeWeaponKitDefinition();
	Definition->FireInterval = 0.01f;
	Definition->bUseSeparateAimingSpread = false;
	Definition->HipFire.StationarySpreadAngle = 0.0f;
	Definition->HipFire.MovingSpreadAngle = 0.0f;
	Definition->HipFire.JumpSpreadAngle = 0.0f;
	Definition->HipFire.SlowWalkSpreadAngle = 0.0f;
	Definition->RecoilData = NewObject<UWeaponKitRecoilData>(Definition);
	Definition->RecoilData->bUseFixedPattern = true;
	Definition->RecoilData->bUseIndependentShotRecoil = true;
	Definition->RecoilData->FixedPattern = { FVector2D(3.0f, 1.0f) };
	Definition->RecoilData->FixedPatternPitchScale = 1.0f;
	Definition->RecoilData->FixedPatternYawScale = 1.0f;
	Definition->RecoilData->PatternResetDelay = 100.0f;
	Definition->RecoilData->RecoveryDelay = 100.0f;

	AWeaponKitRuntime* Weapon = SpawnWeaponKitWeapon(*this, TestWorld.Get(), Definition);
	if (!TestNotNull(TEXT("Weapon spawned"), Weapon))
	{
		return false;
	}
	Weapon->ApplyDefinitionSettings();

	const FVector ViewDirection = FRotator(7.0f, 24.0f, 0.0f).Vector();
	auto Generate = [&]()
	{
		FVector Direction = FVector::ZeroVector;
		bool bGenerated = false;
		Weapon->GenerateShotDirection(ViewDirection, 0.0f, false, false, false, Direction, bGenerated);
		TestTrue(TEXT("Direction generated"), bGenerated);
		return Direction;
	};

	TestTrue(TEXT("First hip shot accepted"), Weapon->TryCommitFire());
	TestTrue(TEXT("First hip shot starts at the base direction"), Generate().Equals(ViewDirection, 0.00001f));
	TestWorld.Get()->Tick(LEVELTICK_All, 0.02f);
	TestTrue(TEXT("Second hip shot accepted"), Weapon->TryCommitFire());
	Weapon->SetAiming(true);
	TestFalse(TEXT("Committed hip shot keeps its recoil after aiming"), Generate().Equals(ViewDirection, 0.00001f));
	TestWorld.Get()->Tick(LEVELTICK_All, 0.02f);
	TestTrue(TEXT("Aim shot accepted"), Weapon->TryCommitFire());
	Weapon->SetAiming(false);
	TestTrue(TEXT("Committed aim shot follows the input direction"), Generate().Equals(ViewDirection, 0.00001f));
	float Pitch = 0.0f;
	float Yaw = 0.0f;
	bool bKick = false;
	Weapon->GenerateRecoilKick(Pitch, Yaw, bKick);
	TestTrue(TEXT("Visual recoil kick is still produced"), bKick && FMath::IsNearlyEqual(Pitch, 3.0f));
	TestTrue(TEXT("Public aim API is what clears the aimed state"), !Weapon->IsAiming());
	return true;
}

#endif
