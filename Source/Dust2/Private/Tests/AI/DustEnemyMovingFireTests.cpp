#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "AI/DustEnemyCombatAdapterComponent.h"
#include "AI/DustEnemyCombatProfile.h"
#include "AI/DustEnemyStateTreeTasks.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "../../Weapons/WeaponBallisticsData.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Weapons/WeaponRecoilData.h"
#include "Weapons/WeaponRuntime.h"

// This TU owns its own friend name and never reuses another test's private seam.
struct FDustEnemyMovingFireTestAccess
{
	static bool CanUseMovingFire(const UDustEnemyWeaponComponent* Weapon,
		const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Weapon && Weapon->CanUseMovingFire(Adapter);
	}

	static bool IsAimAligned(const UDustEnemyWeaponComponent* Weapon,
		const FVector& Muzzle, const FVector& AimPoint,
		const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Weapon && Weapon->IsAimAligned(Muzzle, AimPoint, Adapter);
	}
};

namespace
{
	/** Compact isolated world for the moving-fire weapon/task rules. Navigation and animation
	 * acceptance remain PIE work; this fixture only needs a live pawn, muzzle, weapon and trace. */
	class FMovingFireTestWorld
	{
	public:
		~FMovingFireTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World = nullptr;
			}
		}

		bool Build(float ReactionDelay, float SettledAimSeconds, int32 Ammo = 20)
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(true).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return false;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			Enemy = World->SpawnActor<ACharacter>();
			Target = World->SpawnActor<ACharacter>();
			if (!Enemy || !Target) return false;

			Enemy->SetActorLocation(FVector(0.0f, 0.0f, 100.0f));
			Enemy->BaseEyeHeight = 64.0f;
			Target->SetActorLocation(FVector(600.0f, 0.0f, 100.0f));
			for (ACharacter* Character : { Enemy, Target })
			{
				UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
				if (!Movement) return false;
				// The fixture sets the real pawn velocity explicitly; do not let gravity/tick clobber it.
				Movement->SetMovementMode(MOVE_None);
				Movement->SetComponentTickEnabled(false);
				Movement->PrimaryComponentTick.bCanEverTick = false;
			}

			Profile = NewObject<UDustEnemyCombatProfile>(GetTransientPackage());
			Profile->ReactionDelay = ReactionDelay;
			Profile->SettledAimSeconds = SettledAimSeconds;
			Profile->TrackingSpeedDegPerSecond = 720.0f;
			Profile->CompensationDelay = 0.0f;
			Profile->CompensationFraction = 0.0f;
			Profile->CompensationSpeedDegPerSecond = 0.0f;
			Profile->CompensationDriftDegrees = 0.0f;
			Profile->DriftResponseSpeed = 0.0f;
			Profile->AimHeightOffset = 50.0f;
			Profile->RequiredSettledSpeed = 15.0f;
			Profile->EngagementRange = 2000.0f;
			Profile->FireAlignmentAngleDegrees = 15.0f;
			Profile->BurstShotCount = 1;
			Profile->MaxBurstShotCount = 1;

			Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
			UWeaponBallisticsData* Ballistics = NewObject<UWeaponBallisticsData>(Definition);
			Ballistics->DamagePerShot = 0.0f;
			Ballistics->ArmorPenetration = 0.0f;
			Ballistics->HeadDamageMultiplier = 1.0f;
			Ballistics->TraceDistance = 2000.0f;
			Definition->BallisticsData = Ballistics;
			Definition->ConfigurationVersion = 0;
			Definition->ReloadStyle = EWeaponReloadStyle::Magazine;
			Definition->MagazineCapacity = FMath::Max(1, Ammo);
			Definition->InitialMagazineAmmo = Ammo;
			Definition->InitialReserveAmmo = 0;
			Definition->AllowedFireModes = { EFireMode::SemiAutomatic };
			Definition->DefaultFireMode = EFireMode::SemiAutomatic;
			Definition->FireInterval = 0.2f;
			Definition->StationarySpreadAngle = 0.0f;
			Definition->MovingSpreadAngle = 0.0f;
			Definition->JumpSpreadAngle = 0.0f;
			Definition->SlowWalkSpreadAngle = 0.0f;
			Definition->MovementSpreadSpeedThreshold = 0.0f;
			Definition->bUseFiringSpread = false;
			UWeaponRecoilData* Recoil = NewObject<UWeaponRecoilData>(Definition);
			Recoil->bUseFixedPattern = false;
			Recoil->bUseIndependentShotRecoil = false;
			Definition->RecoilData = Recoil;

			Weapon = NewObject<UDustEnemyWeaponComponent>(Enemy);
			Weapon->WeaponDefinition = Definition;
			Weapon->CombatProfile = Profile;
			Enemy->AddInstanceComponent(Weapon);
			Weapon->RegisterComponent();
			Weapon->SetComponentTickEnabled(false);
			Weapon->PrimaryComponentTick.bCanEverTick = false;

			Muzzle = NewObject<USceneComponent>(Enemy, TEXT("MovingFireTestMuzzle"));
			Muzzle->SetupAttachment(Enemy->GetRootComponent());
			Enemy->AddInstanceComponent(Muzzle);
			Muzzle->RegisterComponent();
			Muzzle->SetRelativeLocation(FVector(60.0f, 0.0f, 50.0f));
			Weapon->SetMuzzleSource(Muzzle, NAME_None);
			return true;
		}

		void Advance(float Seconds) const
		{
			if (!World || !Weapon || !FMath::IsFinite(Seconds) || Seconds <= 0.0f) return;
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / 0.05f));
			const float StepSeconds = Seconds / Steps;
			for (int32 Index = 0; Index < Steps; ++Index)
			{
				World->Tick(LEVELTICK_All, StepSeconds);
				Weapon->TickComponent(StepSeconds, LEVELTICK_All, nullptr);
			}
		}

		UWorld* World = nullptr;
		ACharacter* Enemy = nullptr;
		ACharacter* Target = nullptr;
		UDustEnemyWeaponComponent* Weapon = nullptr;
		UDustEnemyCombatProfile* Profile = nullptr;
		UWeaponDefinitionData* Definition = nullptr;
		USceneComponent* Muzzle = nullptr;
	};

	int32 CurrentAmmo(const UDustEnemyWeaponComponent* Weapon)
	{
		return Weapon && IsValid(Weapon->GetWeaponRuntime()) ? Weapon->GetWeaponRuntime()->GetCurrentAmmo() : -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyMovingFireStationaryGateTest,
	"Dust2.EnemyMovingFire.StationaryGateBlocksWalkingSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyMovingFireStationaryGateTest::RunTest(const FString& Parameters)
{
	FMovingFireTestWorld Fixture;
	if (!TestTrue(TEXT("stationary-gate world builds"), Fixture.Build(0.0f, 0.4f))) return false;
	if (!TestNotNull(TEXT("enemy"), Fixture.Enemy) || !TestNotNull(TEXT("target"), Fixture.Target)
		|| !TestNotNull(TEXT("weapon"), Fixture.Weapon)) return false;

	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	if (!TestNotNull(TEXT("character movement"), Movement)) return false;
	Movement->Velocity = FVector(200.0f, 0.0f, 0.0f);
	TestTrue(TEXT("the fixture is physically walking"), Fixture.Enemy->GetVelocity().Size2D() > 15.0f);

	if (!TestTrue(TEXT("normal engagement starts"), Fixture.Weapon->StartEngagement(Fixture.Target))) return false;
	const int32 AmmoBefore = CurrentAmmo(Fixture.Weapon);

	Fixture.Advance(1.0f);
	TestEqual(TEXT("walking still requires the normal stationary settle gate"),
		Fixture.Weapon->SuccessfulShots, 0);
	TestEqual(TEXT("the stationary gate consumes no ammo"), CurrentAmmo(Fixture.Weapon), AmmoBefore);

	Movement->Velocity = FVector::ZeroVector;
	Fixture.Advance(0.6f);
	TestTrue(TEXT("standing still lets the normal engagement fire"), Fixture.Weapon->SuccessfulShots > 0);
	TestTrue(TEXT("the normal shot consumes real ammo"), CurrentAmmo(Fixture.Weapon) < AmmoBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyMovingFireLeaseGatesTest,
	"Dust2.EnemyMovingFire.ReactionAlignmentConfirmationSpeedFallAndRangeGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyMovingFireLeaseGatesTest::RunTest(const FString& Parameters)
{
	FMovingFireTestWorld Fixture;
	if (!TestTrue(TEXT("lease world builds"), Fixture.Build(0.5f, 1.0f))) return false;
	if (!TestNotNull(TEXT("enemy"), Fixture.Enemy) || !TestNotNull(TEXT("target"), Fixture.Target)
		|| !TestNotNull(TEXT("weapon"), Fixture.Weapon)) return false;

	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	if (!TestNotNull(TEXT("character movement"), Movement)) return false;
	Movement->Velocity = FVector(200.0f, 0.0f, 0.0f);
	if (!TestTrue(TEXT("moving engagement starts"), Fixture.Weapon->StartEngagement(Fixture.Target))) return false;

	const uint64 Token = Fixture.Weapon->BeginMovingFire(Fixture.Target, 200.0f);
	if (!TestTrue(TEXT("a moving-fire token is issued"), Token != 0)) return false;
	TestTrue(TEXT("the token owns this engagement"), Fixture.Weapon->IsMovingFireLeaseActive(Token, Fixture.Target));

	Fixture.Advance(0.2f);
	TestEqual(TEXT("the lease never skips the reaction delay"), Fixture.Weapon->SuccessfulShots, 0);

	Fixture.Advance(0.4f);
	TestTrue(TEXT("reaction elapsed + aligned + in range commits while walking"),
		Fixture.Weapon->SuccessfulShots > 0);

	const int32 ShotsBeforeFast = Fixture.Weapon->SuccessfulShots;
	Movement->Velocity = FVector(300.0f, 0.0f, 0.0f);
	Fixture.Advance(0.3f);
	TestEqual(TEXT("physically too fast denies shots instead of using RequiredSettle=0"),
		Fixture.Weapon->SuccessfulShots, ShotsBeforeFast);

	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(200.0f, 0.0f, 0.0f);
	const int32 ShotsBeforeFalling = Fixture.Weapon->SuccessfulShots;
	Fixture.Advance(0.3f);
	TestEqual(TEXT("falling denies moving-fire shots"), Fixture.Weapon->SuccessfulShots, ShotsBeforeFalling);

	Movement->SetMovementMode(MOVE_None);
	Movement->Velocity = FVector(200.0f, 0.0f, 0.0f);
	const int32 ShotsBeforeRecovery = Fixture.Weapon->SuccessfulShots;
	Fixture.Advance(0.4f);
	TestTrue(TEXT("returning to the walk cap restores moving fire"),
		Fixture.Weapon->SuccessfulShots > ShotsBeforeRecovery);

	// Alignment is adapter-backed. Use a dummy non-null adapter only for the pure alignment math;
	// the no-adapter production path intentionally keeps the legacy behavior.
	UDustEnemyCombatAdapterComponent* DummyAdapter = NewObject<UDustEnemyCombatAdapterComponent>(Fixture.World);
	const FVector MuzzleLocation = Fixture.Muzzle->GetComponentLocation();
	const FVector AimPoint = Fixture.Target->GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);
	TestTrue(TEXT("tracker and body align with the real aim point"),
		FDustEnemyMovingFireTestAccess::IsAimAligned(Fixture.Weapon, MuzzleLocation, AimPoint, DummyAdapter));
	Fixture.Enemy->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
	TestFalse(TEXT("a body facing away is not aligned"),
		FDustEnemyMovingFireTestAccess::IsAimAligned(Fixture.Weapon, MuzzleLocation, AimPoint, DummyAdapter));
	Fixture.Enemy->SetActorRotation(FRotator::ZeroRotator);

	TestFalse(TEXT("an adapter-backed lease requires an initialized/confirmed adapter"),
		FDustEnemyMovingFireTestAccess::CanUseMovingFire(Fixture.Weapon, DummyAdapter));
	TestTrue(TEXT("the legacy no-adapter path keeps its permission"),
		FDustEnemyMovingFireTestAccess::CanUseMovingFire(Fixture.Weapon, nullptr));

	const int32 ShotsBeforeRange = Fixture.Weapon->SuccessfulShots;
	Fixture.Target->SetActorLocation(FVector(3000.0f, 0.0f, 100.0f));
	Fixture.Advance(0.3f);
	TestEqual(TEXT("out-of-range approach geometry denies shots"), Fixture.Weapon->SuccessfulShots, ShotsBeforeRange);
	TestTrue(TEXT("out-of-range approach preserves the lease without firing"),
		Fixture.Weapon->IsMovingFireLeaseActive(Token, Fixture.Target));
	TestTrue(TEXT("out-of-range approach preserves reaction state"), Fixture.Weapon->IsEngaging());
	Fixture.Target->SetActorLocation(FVector(600.0f, 0.0f, 100.0f));
	Fixture.Advance(0.3f);
	TestTrue(TEXT("returning to range resumes the same engagement"), Fixture.Weapon->SuccessfulShots > ShotsBeforeRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyMovingFireTokenTest,
	"Dust2.EnemyMovingFire.StopClearsAndStaleEndPreservesNewerLease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyMovingFireTokenTest::RunTest(const FString& Parameters)
{
	FMovingFireTestWorld Fixture;
	if (!TestTrue(TEXT("token world builds"), Fixture.Build(0.0f, 0.5f))) return false;
	if (!TestNotNull(TEXT("target"), Fixture.Target) || !TestNotNull(TEXT("weapon"), Fixture.Weapon)) return false;

	if (!TestTrue(TEXT("first engagement starts"), Fixture.Weapon->StartEngagement(Fixture.Target))) return false;
	const uint64 FirstToken = Fixture.Weapon->BeginMovingFire(Fixture.Target, 200.0f);
	if (!TestTrue(TEXT("first lease starts"), FirstToken != 0)) return false;

	Fixture.Weapon->StopEngagement();
	TestFalse(TEXT("StopEngagement clears the lease"),
		Fixture.Weapon->IsMovingFireLeaseActive(FirstToken, Fixture.Target));
	TestFalse(TEXT("StopEngagement clears all moving-fire permission"), Fixture.Weapon->HasMovingFireLease());
	TestFalse(TEXT("permission is gone after the stop"),
		FDustEnemyMovingFireTestAccess::CanUseMovingFire(Fixture.Weapon, nullptr));

	if (!TestTrue(TEXT("same-target re-engagement starts"), Fixture.Weapon->StartEngagement(Fixture.Target))) return false;
	const uint64 SecondToken = Fixture.Weapon->BeginMovingFire(Fixture.Target, 200.0f);
	if (!TestTrue(TEXT("second lease starts"), SecondToken != 0)) return false;
	TestTrue(TEXT("lease identity is monotonic"), SecondToken > FirstToken);

	Fixture.Weapon->EndMovingFire(FirstToken);
	TestTrue(TEXT("a stale EndMovingFire token cannot clear the newer lease"),
		Fixture.Weapon->IsMovingFireLeaseActive(SecondToken, Fixture.Target));
	TestTrue(TEXT("the newer same-target lease keeps permission"), Fixture.Weapon->HasMovingFireLease());

	Fixture.Weapon->EndMovingFire(SecondToken);
	TestFalse(TEXT("the owning token ends its lease"),
		Fixture.Weapon->IsMovingFireLeaseActive(SecondToken, Fixture.Target));
	TestFalse(TEXT("the owning task exit also stops its engagement"), Fixture.Weapon->IsEngaging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyMovingFireTaskLifecycleTest,
	"Dust2.EnemyMovingFire.TaskBackgroundCompletionAndConditionalMovementCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustEnemyMovingFireTaskLifecycleTest::RunTest(const FString& Parameters)
{
	FDustEnemyMovingFireTask Task;
	TestTrue(TEXT("the background task ticks"), Task.bShouldCallTick);
	TestTrue(TEXT("bound properties are copied before ExitState"), Task.bShouldCopyBoundPropertiesOnExitState);
#if WITH_EDITORONLY_DATA
	TestFalse(TEXT("the task is not considered for state completion"), Task.bConsideredForCompletion);
	TestFalse(TEXT("the completion flag is not user-editable"), Task.bCanEditConsideredForCompletion);
#endif

	FMovingFireTestWorld Fixture;
	if (!TestTrue(TEXT("cleanup world builds"), Fixture.Build(0.0f, 0.4f))) return false;
	AAIController* Controller = Fixture.World->SpawnActor<AAIController>();
	ACharacter* Character = Fixture.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("character"), Character)) return false;
	Controller->Possess(Character);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!TestNotNull(TEXT("movement"), Movement)) return false;

	Movement->MaxWalkSpeed = 600.0f;
	Controller->bAllowStrafe = false;
	FDustEnemyMovingFireTaskInstanceData Owned;
	Owned.WalkSpeed = 200.0f;
	if (!TestTrue(TEXT("movement ownership is captured"),
		Owned.CaptureMovementOwnership(Character, Controller, Owned.WalkSpeed))) return false;
	TestEqual(TEXT("the task applies its walk speed"), Movement->MaxWalkSpeed, 200.0f);
	TestTrue(TEXT("the task applies strafe for side tracking"), Controller->bAllowStrafe != 0);

	Owned.ReleaseOwnedResources();
	TestEqual(TEXT("exact original speed is restored"), Movement->MaxWalkSpeed, 600.0f);
	TestFalse(TEXT("exact original strafe is restored"), Controller->bAllowStrafe != 0);

	// An external newer speed/strafe change must survive the task's conditional restore.
	FDustEnemyMovingFireTaskInstanceData External;
	Movement->MaxWalkSpeed = 600.0f;
	Controller->bAllowStrafe = true;
	if (!TestTrue(TEXT("second movement ownership is captured"),
		External.CaptureMovementOwnership(Character, Controller, 200.0f))) return false;
	Movement->MaxWalkSpeed = 350.0f; // newer external speed
	Controller->bAllowStrafe = false; // newer external strafe
	External.ReleaseOwnedResources();
	TestEqual(TEXT("a newer external speed is not overwritten"), Movement->MaxWalkSpeed, 350.0f);
	TestFalse(TEXT("a newer external strafe is not overwritten"), Controller->bAllowStrafe != 0);

	FDustEnemyMovingFireTaskInstanceData Invalid;
	TestFalse(TEXT("a task without an initialized adapter fails safely"),
		Invalid.StartOwnedMovingFire());
	return true;
}

#endif
