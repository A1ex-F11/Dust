#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustRagdollDeathComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponRuntime.h"
#include "WeaponShotTestReceiver.h"

#include <limits>

namespace
{
	struct FWeaponShotTestWorld
	{
		UWorld* World = nullptr;
		AWeaponRuntime* Weapon = nullptr;
		UWeaponDefinitionData* Definition = nullptr;
		UWeaponBallisticsData* Ballistics = nullptr;

		explicit FWeaponShotTestWorld(bool bCreateBallistics = true)
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
				nullptr, false, ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			Weapon = World->SpawnActor<AWeaponRuntime>();
			if (!Weapon) return;
			Definition = NewObject<UWeaponDefinitionData>(Weapon);
			Definition->MagazineCapacity = 5;
			Definition->InitialMagazineAmmo = 5;
			Definition->InitialReserveAmmo = 10;
			Definition->FireInterval = 0.0f;
			if (bCreateBallistics)
			{
				Ballistics = NewObject<UWeaponBallisticsData>(Definition);
				Ballistics->DamagePerShot = 25.5f;
				Ballistics->ArmorPenetration = 3.25f;
				Ballistics->HeadDamageMultiplier = 2.75f;
				Ballistics->TraceDistance = 1000.0f;
				Definition->BallisticsData = Ballistics;
			}
			Weapon->WeaponDefinition = Definition;
			Weapon->ApplyDefinitionSettings();
			Weapon->RestoreAmmo(5, 10);
		}

		~FWeaponShotTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
			}
		}

		AWeaponShotTestReceiver* SpawnReceiver(float X = 500.0f)
		{
			if (!World) return nullptr;
			AWeaponShotTestReceiver* Receiver = World->SpawnActor<AWeaponShotTestReceiver>();
			if (Receiver) Receiver->SetActorLocation(FVector(X, 0.0, 0.0));
			return Receiver;
		}

		AActor* SpawnVisibilityBlocker(float X)
		{
			if (!World) return nullptr;
			AActor* Blocker = World->SpawnActor<AActor>();
			if (!Blocker) return nullptr;
			UBoxComponent* Box = NewObject<UBoxComponent>(Blocker, TEXT("ShotTestWall"));
			Blocker->AddInstanceComponent(Box);
			Blocker->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(20.0, 80.0, 80.0));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Box->RegisterComponent();
			Blocker->SetActorLocation(FVector(X, 0.0, 0.0));
			return Blocker;
		}
	};

	bool CommitShot(FAutomationTestBase& Test, AWeaponRuntime* Weapon)
	{
		return Test.TestTrue(TEXT("shot commits before native resolution"),
			Weapon && Weapon->TryCommitFire());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionCommitBridgeTest,
	"Dust2.WeaponShot.CommitTraceAndDamageBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionCommitBridgeTest::RunTest(const FString&)
{
	FWeaponShotTestWorld Fixture;
	if (!TestNotNull(TEXT("transient world"), Fixture.World)
		|| !TestNotNull(TEXT("weapon runtime"), Fixture.Weapon)) return false;

	AWeaponShotTestReceiver* Receiver = Fixture.SpawnReceiver();
	AActor* OwnerBlocker = Fixture.SpawnVisibilityBlocker(150.0f);
	if (!TestNotNull(TEXT("damageable receiver"), Receiver)
		|| !TestNotNull(TEXT("owner blocker"), OwnerBlocker)) return false;
	Fixture.Weapon->SetOwner(OwnerBlocker);
	UDustRagdollDeathComponent* DeathComponent = NewObject<UDustRagdollDeathComponent>(Receiver);
	Receiver->AddInstanceComponent(DeathComponent);
	DeathComponent->RegisterComponent();
	Receiver->bMarkDeadOnDamage = true;
	Receiver->bAttemptNestedCommitOnDamage = true;
	Receiver->ReentryWeapon = Fixture.Weapon;

	const FWeaponShotResolution BeforeCommit = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("uncommitted request cannot trace or signal damage"),
		BeforeCommit.Status == EWeaponShotResolutionStatus::NoPendingShot
		&& !BeforeCommit.bTracePerformed && !BeforeCommit.bHitDamageable);
	TestEqual(TEXT("uncommitted request does not consume ammo"), Fixture.Weapon->GetCurrentAmmo(), 5);
	TestEqual(TEXT("uncommitted request does not call target"), Receiver->DamageCallCount, 0);

	if (!CommitShot(*this, Fixture.Weapon)) return false;
	const int32 AmmoAfterCommit = Fixture.Weapon->GetCurrentAmmo();
	TestEqual(TEXT("committed shot consumes exactly one round"), AmmoAfterCommit, 4);
	const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);

	TestTrue(TEXT("trace ran"), Result.bTracePerformed);
	TestTrue(TEXT("blocking hit came from target beyond ignored owner"), Result.Hit.GetActor() == Receiver);
	TestTrue(TEXT("shot endpoint is the blocking impact point"), Result.ShotEnd.Equals(Result.Hit.ImpactPoint, 0.01));
	TestTrue(TEXT("one live target received one positive request"), Result.bHitDamageable);
	TestTrue(TEXT("result reports synchronous dispatch"), Result.Status == EWeaponShotResolutionStatus::DamageDispatched);
	TestEqual(TEXT("one committed shot consumed one round"), Fixture.Weapon->GetCurrentAmmo(), AmmoAfterCommit);
	TestEqual(TEXT("damage passed unchanged"), Receiver->LastDamage, Fixture.Ballistics->DamagePerShot);
	TestEqual(TEXT("armor penetration passed unchanged"), Receiver->LastArmorPenetration, Fixture.Ballistics->ArmorPenetration);
	TestEqual(TEXT("head multiplier passed unchanged"), Receiver->LastHeadDamageMultiplier, Fixture.Ballistics->HeadDamageMultiplier);
	TestTrue(TEXT("bone name passed unchanged"), Receiver->LastHitBoneName == Result.Hit.BoneName);
	TestEqual(TEXT("interface was called exactly once"), Receiver->DamageCallCount, 1);
	TestEqual(TEXT("damage callback reentrant fire returns Busy"), Receiver->ReentrantFireResult,
		static_cast<uint8>(EWeaponFireAttemptResult::Busy));
	TestTrue(TEXT("lethal response does not erase this shot's hit prompt"), Receiver->bIsDead && Result.bHitDamageable);

	const FWeaponShotResolution Repeated = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("same credential cannot trace a second time"),
		Repeated.Status == EWeaponShotResolutionStatus::NoPendingShot && !Repeated.bTracePerformed);
	TestEqual(TEXT("same credential cannot dispatch damage twice"), Receiver->DamageCallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionCommitCallbackReentryTest,
	"Dust2.WeaponShot.CommitCallbackReentryPreservesOuterShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionCommitCallbackReentryTest::RunTest(const FString&)
{
	FWeaponShotTestWorld Fixture;
	if (!Fixture.Weapon) return false;
	AWeaponShotTestReceiver* Receiver = Fixture.SpawnReceiver();
	UWeaponShotFireReentryListener* Listener = NewObject<UWeaponShotFireReentryListener>(Fixture.Weapon);
	if (!TestNotNull(TEXT("damageable receiver"), Receiver)
		|| !TestNotNull(TEXT("fire callback listener"), Listener)) return false;
	Listener->Weapon = Fixture.Weapon;
	Fixture.Weapon->OnFireCommitted.AddDynamic(
		Listener, &UWeaponShotFireReentryListener::AttemptNestedCommit);

	const EWeaponFireAttemptResult CommitResult = Fixture.Weapon->TryCommitFireWithResult();
	TestTrue(TEXT("outer fire commit succeeds"), CommitResult == EWeaponFireAttemptResult::Committed);
	TestEqual(TEXT("fire notification ran once"), Listener->BroadcastCount, 1);
	TestEqual(TEXT("OnFireCommitted nested fire returns Busy"), Listener->ReentrantFireResult,
		static_cast<uint8>(EWeaponFireAttemptResult::Busy));
	TestEqual(TEXT("nested fire did not consume a second round"), Fixture.Weapon->GetCurrentAmmo(), 4);

	const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("outer shot credential remains available for one resolution"),
		Result.Status == EWeaponShotResolutionStatus::DamageDispatched && Result.bHitDamageable);
	TestEqual(TEXT("outer shot dispatched damage once"), Receiver->DamageCallCount, 1);
	const FWeaponShotResolution Repeated = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("completed outer shot leaves no reusable credential"),
		Repeated.Status == EWeaponShotResolutionStatus::NoPendingShot && !Repeated.bTracePerformed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionWorldHitOutcomesTest,
	"Dust2.WeaponShot.WallAirAndCorpseOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionWorldHitOutcomesTest::RunTest(const FString&)
{
	{
		FWeaponShotTestWorld Fixture;
		AActor* Wall = Fixture.SpawnVisibilityBlocker(300.0f);
		if (!Fixture.Weapon || !TestNotNull(TEXT("wall actor"), Wall) || !CommitShot(*this, Fixture.Weapon)) return false;
		const int32 AmmoAfterCommit = Fixture.Weapon->GetCurrentAmmo();
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, FVector::ForwardVector);
		TestTrue(TEXT("wall is the actual first blocking hit"), Result.Hit.GetActor() == Wall);
		TestTrue(TEXT("wall hit does not raise damageable prompt"), !Result.bHitDamageable);
		TestTrue(TEXT("non-interface blocker is not treated as damageable"), Result.Status == EWeaponShotResolutionStatus::NotDamageable);
		TestEqual(TEXT("wall shot still consumed its committed round"), Fixture.Weapon->GetCurrentAmmo(), AmmoAfterCommit);
	}

	{
		FWeaponShotTestWorld Fixture;
		if (!Fixture.Weapon || !CommitShot(*this, Fixture.Weapon)) return false;
		const int32 AmmoAfterCommit = Fixture.Weapon->GetCurrentAmmo();
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, FVector::ForwardVector);
		TestTrue(TEXT("empty world trace reports no blocking hit"), Result.Status == EWeaponShotResolutionStatus::NoBlockingHit);
		TestTrue(TEXT("empty trace keeps endpoint for tracer"), Result.ShotEnd.Equals(Result.TraceEnd, 0.01));
		TestTrue(TEXT("empty trace does not raise damageable prompt"), !Result.bHitDamageable);
		TestEqual(TEXT("air shot still consumed its committed round"), Fixture.Weapon->GetCurrentAmmo(), AmmoAfterCommit);
	}

	{
		FWeaponShotTestWorld Fixture;
		AWeaponShotTestReceiver* Corpse = Fixture.SpawnReceiver();
		if (!Fixture.Weapon || !TestNotNull(TEXT("corpse blocker"), Corpse)) return false;
		Corpse->bIsDead = true;
		if (!CommitShot(*this, Fixture.Weapon)) return false;
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, FVector::ForwardVector);
		TestTrue(TEXT("corpse remains the physical hit"), Result.Hit.GetActor() == Corpse);
		TestTrue(TEXT("corpse result preserves hit endpoint"), Result.ShotEnd.Equals(Result.Hit.ImpactPoint, 0.01));
		TestTrue(TEXT("legacy dead bool blocks damage dispatch"), Result.Status == EWeaponShotResolutionStatus::TargetDead);
		TestTrue(TEXT("corpse does not raise damageable prompt"), !Result.bHitDamageable);
		TestEqual(TEXT("corpse receiver was not called"), Corpse->DamageCallCount, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionPlayerDeadGateTest,
	"Dust2.WeaponShot.NativePlayerDeadGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionPlayerDeadGateTest::RunTest(const FString&)
{
	FWeaponShotTestWorld Fixture;
	if (!TestNotNull(TEXT("transient world"), Fixture.World) || !Fixture.Weapon) return false;
	AWeaponShotTestPlayerReceiver* Player = Fixture.World->SpawnActor<AWeaponShotTestPlayerReceiver>();
	if (!TestNotNull(TEXT("player damage receiver"), Player)) return false;
	Player->SetActorLocation(FVector(500.0, 0.0, 0.0));
	bool bDiedNow = false;
	Player->ApplyDamage(100000.0f, bDiedNow);
	if (Player->DeathPresentation) Player->DeathPresentation->ResetDeathPresentation();
	if (!TestTrue(TEXT("native player health reports dead"), Player->IsDead())
		|| !TestTrue(TEXT("test isolates the native player gate from death presentation"),
			!Player->DeathPresentation || !Player->DeathPresentation->IsDeathStarted())) return false;
	if (!CommitShot(*this, Fixture.Weapon)) return false;

	const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("dead player still blocks the physical trace"), Result.Hit.GetActor() == Player);
	TestTrue(TEXT("native IsDead filters the target"), Result.Status == EWeaponShotResolutionStatus::TargetDead);
	TestFalse(TEXT("dead player does not raise damageable prompt"), Result.bHitDamageable);
	TestEqual(TEXT("dead player's damage interface was not called"), Player->DamageCallCount, 0);

	Player->RestoreHealth(100.0f);
	if (!TestTrue(TEXT("positive health restores the player for the death-component case"),
		!Player->IsDead()) || !TestNotNull(TEXT("player death component"), Player->DeathPresentation.Get())) return false;
	Player->DeathPresentation->StartRagdollDeath();
	if (!TestTrue(TEXT("death component enters its started state"),
		Player->DeathPresentation->IsDeathStarted())) return false;
	if (!CommitShot(*this, Fixture.Weapon)) return false;
	const FWeaponShotResolution ComponentDeathResult = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("death-component owner still blocks the physical trace"),
		ComponentDeathResult.Hit.GetActor() == Player);
	TestTrue(TEXT("death component filters a living player independently of health"),
		ComponentDeathResult.Status == EWeaponShotResolutionStatus::TargetDead);
	TestFalse(TEXT("death-component filtering does not raise damageable prompt"),
		ComponentDeathResult.bHitDamageable);
	TestEqual(TEXT("death-component filtering leaves the damage interface untouched"),
		Player->DamageCallCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionInvalidInputsTest,
	"Dust2.WeaponShot.InvalidTraceAndMissingBallistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionInvalidInputsTest::RunTest(const FString&)
{
	{
		FWeaponShotTestWorld Fixture(false);
		if (!Fixture.Weapon || !CommitShot(*this, Fixture.Weapon)) return false;
		const int32 AmmoAfterCommit = Fixture.Weapon->GetCurrentAmmo();
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, FVector::ForwardVector);
		TestTrue(TEXT("missing ballistics is reported"), Result.Status == EWeaponShotResolutionStatus::MissingBallistics);
		TestFalse(TEXT("missing ballistics performs no trace"), Result.bTracePerformed);
		TestFalse(TEXT("missing ballistics raises no damageable prompt"), Result.bHitDamageable);
		TestEqual(TEXT("missing configuration does not refund a committed shot"), Fixture.Weapon->GetCurrentAmmo(), AmmoAfterCommit);
	}

	{
		FWeaponShotTestWorld Fixture;
		if (!Fixture.Weapon || !CommitShot(*this, Fixture.Weapon)) return false;
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, FVector::ZeroVector);
		TestTrue(TEXT("zero direction is rejected"), Result.Status == EWeaponShotResolutionStatus::InvalidTrace);
		TestFalse(TEXT("zero direction never traces"), Result.bTracePerformed);
	}

	{
		FWeaponShotTestWorld Fixture;
		if (!Fixture.Weapon || !CommitShot(*this, Fixture.Weapon)) return false;
		const FVector NonFiniteDirection(std::numeric_limits<double>::infinity(), 0.0, 0.0);
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, NonFiniteDirection);
		TestTrue(TEXT("nonfinite direction is rejected"), Result.Status == EWeaponShotResolutionStatus::InvalidTrace);
		TestFalse(TEXT("nonfinite direction never traces"), Result.bTracePerformed);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionInterfaceContractTest,
	"Dust2.WeaponShot.RejectsSameNamedNonInterface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionInterfaceContractTest::RunTest(const FString&)
{
	FWeaponShotTestWorld Fixture;
	if (!Fixture.Weapon || !TestNotNull(TEXT("transient world"), Fixture.World)) return false;
	AWeaponShotTestLookalike* Lookalike = Fixture.World->SpawnActor<AWeaponShotTestLookalike>();
	if (!TestNotNull(TEXT("same-named non-interface actor"), Lookalike)) return false;
	Lookalike->SetActorLocation(FVector(500.0, 0.0, 0.0));
	if (!CommitShot(*this, Fixture.Weapon)) return false;

	const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("lookalike is still the physical hit"), Result.Hit.GetActor() == Lookalike);
	TestTrue(TEXT("same name without implemented contract is not damageable"),
		Result.Status == EWeaponShotResolutionStatus::NotDamageable);
	TestFalse(TEXT("lookalike does not raise damageable prompt"), Result.bHitDamageable);
	TestEqual(TEXT("arbitrary method was never invoked"), Lookalike->DamageCallCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionInvalidInterfaceContractTest,
	"Dust2.WeaponShot.RejectsIncompatibleInterfaceContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionInvalidInterfaceContractTest::RunTest(const FString&)
{
	FWeaponShotTestWorld Fixture;
	if (!Fixture.Weapon || !TestNotNull(TEXT("transient world"), Fixture.World)) return false;
	AWeaponShotTestMalformedReceiver* Receiver = Fixture.World->SpawnActor<AWeaponShotTestMalformedReceiver>();
	if (!TestNotNull(TEXT("malformed interface receiver"), Receiver)) return false;
	Receiver->SetActorLocation(FVector(500.0, 0.0, 0.0));
	if (!CommitShot(*this, Fixture.Weapon)) return false;

	const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
		FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("malformed receiver remains the physical hit"), Result.Hit.GetActor() == Receiver);
	TestTrue(TEXT("extra required parameter invalidates the contract"),
		Result.Status == EWeaponShotResolutionStatus::InvalidDamageContract);
	TestFalse(TEXT("incompatible contract does not raise damageable prompt"), Result.bHitDamageable);
	TestEqual(TEXT("incompatible event was never dispatched"), Receiver->DamageCallCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponShotResolutionPendingCredentialTest,
	"Dust2.WeaponShot.ReloadAndAmmoRestoreInvalidatePendingShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponShotResolutionPendingCredentialTest::RunTest(const FString&)
{
	{
		FWeaponShotTestWorld Fixture;
		if (!Fixture.Weapon || !CommitShot(*this, Fixture.Weapon)) return false;
		TestTrue(TEXT("reload starts after a committed shot"), Fixture.Weapon->TryStartReload());
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, FVector::ForwardVector);
		TestTrue(TEXT("reload invalidates the old native-shot credential"),
			Result.Status == EWeaponShotResolutionStatus::NoPendingShot && !Result.bTracePerformed);
	}

	{
		FWeaponShotTestWorld Fixture;
		if (!Fixture.Weapon || !CommitShot(*this, Fixture.Weapon)) return false;
		Fixture.Weapon->RestoreAmmo(3, 8);
		const FWeaponShotResolution Result = Fixture.Weapon->ResolveCommittedShotNative(
			FVector::ZeroVector, FVector::ForwardVector);
		TestTrue(TEXT("ammo restoration invalidates the old native-shot credential"),
			Result.Status == EWeaponShotResolutionStatus::NoPendingShot && !Result.bTracePerformed);
		TestEqual(TEXT("restored ammo remains unchanged"), Fixture.Weapon->GetCurrentAmmo(), 3);
	}
	return true;
}
#endif
