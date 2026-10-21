#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponRecoilData.h"
#include "WeaponRuntime.h"

namespace
{
	class FScopedEnemyWeaponTestWorld
	{
	public:
		explicit FScopedEnemyWeaponTestWorld(bool bCreatePhysicsScene = false)
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(bCreatePhysicsScene)
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
				&Values);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		~FScopedEnemyWeaponTestWorld()
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

		void Advance(float Seconds) const
		{
			if (!World || !FMath::IsFinite(Seconds) || Seconds <= 0.0f)
			{
				return;
			}
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / 0.05f));
			const float StepSeconds = Seconds / Steps;
			for (int32 Index = 0; Index < Steps; ++Index)
			{
				World->Tick(LEVELTICK_All, StepSeconds);
				// Automation has no interactive world tick driver. Step registered components explicitly.
				for (const auto& Component : SteppedComponents)
					if (Component.IsValid()) Component->TickComponent(StepSeconds, LEVELTICK_All, nullptr);
			}
		}

		AActor* SpawnActorWithRoot(const FTransform& Transform = FTransform::Identity) const
		{
			if (!World)
			{
				return nullptr;
			}
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor)
			{
				return nullptr;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TestRoot"));
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorTransform(Transform);
			return Actor;
		}

		APawn* SpawnPawn() const
		{
			return Cast<APawn>(SpawnPawnActor());
		}

		UWeaponDefinitionData* MakeDefinition(int32 InitialAmmo, bool bWithFixedRecoil = false) const
		{
			UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(GetTransientPackage());
			UWeaponBallisticsData* Ballistics = NewObject<UWeaponBallisticsData>(Definition);
			Ballistics->DamagePerShot = 10.0f;
			Ballistics->ArmorPenetration = 0.0f;
			Ballistics->HeadDamageMultiplier = 2.0f;
			Ballistics->TraceDistance = 2000.0f;

			Definition->BallisticsData = Ballistics;
			Definition->MagazineCapacity = FMath::Max(1, InitialAmmo);
			Definition->InitialMagazineAmmo = InitialAmmo;
			Definition->InitialReserveAmmo = 0;
			Definition->FireInterval = 0.05f;
			Definition->AllowedFireModes = { EFireMode::FullAutomatic };
			Definition->DefaultFireMode = EFireMode::FullAutomatic;
			Definition->StationarySpreadAngle = 0.0f;
			Definition->MovingSpreadAngle = 0.0f;
			Definition->JumpSpreadAngle = 0.0f;
			Definition->MovementSpreadSpeedThreshold = 0.0f;

			UWeaponRecoilData* Recoil = NewObject<UWeaponRecoilData>(Definition);
			Recoil->bUseFixedPattern = bWithFixedRecoil;
			Recoil->bUseIndependentShotRecoil = bWithFixedRecoil;
			Recoil->PatternResetDelay = 0.7f;
			Recoil->RecoveryDelay = 0.05f;
			Recoil->ShotRecoilRecoverySpeed = 5.0f;
			Recoil->FixedPatternPitchScale = 1.0f;
			Recoil->FixedPatternYawScale = 1.0f;
			Recoil->LoopStartIndex = -1;
			Recoil->FixedPattern = {
				FVector2D(1.5f, 0.4f),
				FVector2D(2.0f, -0.1f),
				FVector2D(2.5f, 0.2f)};
			Definition->RecoilData = Recoil;
			return Definition;
		}

		UDustEnemyCombatProfile* MakeProfile() const
		{
			UDustEnemyCombatProfile* Profile = NewObject<UDustEnemyCombatProfile>(GetTransientPackage());
			Profile->ReactionDelay = 0.0f;
			Profile->SettledAimSeconds = 0.0f;
			Profile->TrackingSpeedDegPerSecond = 360.0f;
			Profile->CompensationDelay = 0.0f;
			Profile->RequiredSettledSpeed = 0.0f;
			Profile->EngagementRange = 2000.0f;
			return Profile;
		}

		UDustEnemyWeaponComponent* AddWeaponComponent(
			APawn* Pawn,
			UWeaponDefinitionData* Definition,
			UDustEnemyCombatProfile* Profile) const
		{
			if (!Pawn)
			{
				return nullptr;
			}
			UDustEnemyWeaponComponent* Component = NewObject<UDustEnemyWeaponComponent>(Pawn);
			Component->WeaponDefinition = Definition;
			Component->CombatProfile = Profile;
			Pawn->AddInstanceComponent(Component);
			Component->RegisterComponent();
			Component->SetComponentTickEnabled(false); // Avoid double ticks in this explicit-step fixture.
			SteppedComponents.Add(Component);
			return Component;
		}

		USceneComponent* AddMuzzle(APawn* Pawn, UDustEnemyWeaponComponent* Component) const
		{
			if (!Pawn || !Pawn->GetRootComponent() || !Component)
			{
				return nullptr;
			}
			USceneComponent* Muzzle = NewObject<USceneComponent>(Pawn, TEXT("TestMuzzle"));
			Muzzle->SetupAttachment(Pawn->GetRootComponent());
			Muzzle->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
			Pawn->AddInstanceComponent(Muzzle);
			Muzzle->RegisterComponent();
			Component->SetMuzzleSource(Muzzle, NAME_None);
			return Muzzle;
		}

		AWeaponRuntime* SpawnWeapon(UWeaponDefinitionData* Definition) const
		{
			if (!World || !Definition)
			{
				return nullptr;
			}
			const FTransform Transform = FTransform::Identity;
			AWeaponRuntime* Weapon = World->SpawnActorDeferred<AWeaponRuntime>(
				AWeaponRuntime::StaticClass(),
				Transform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Weapon)
			{
				Weapon->WeaponDefinition = Definition;
				Weapon->FinishSpawning(Transform);
			}
			return Weapon;
		}

	private:
		AActor* SpawnPawnActor() const
		{
			if (!World)
			{
				return nullptr;
			}
			APawn* Pawn = World->SpawnActor<APawn>();
			if (!Pawn)
			{
				return nullptr;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Pawn, TEXT("TestPawnRoot"));
			Pawn->SetRootComponent(Root);
			Root->RegisterComponent();
			return Pawn;
		}

		UWorld* World = nullptr;
		mutable TArray<TWeakObjectPtr<UDustEnemyWeaponComponent>> SteppedComponents;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponReadOnlyRecoilObservationTest,
	"Dust2.EnemyWeapon.ReadOnlyRecoilObservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponReadOnlyRecoilObservationTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(5, true);
	AWeaponRuntime* Weapon = Fixture.SpawnWeapon(Definition);
	if (!TestNotNull(TEXT("native runtime spawned with transient definition"), Weapon)) return false;

	TestTrue(TEXT("first shot commits"), Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Committed);
	const FVector2D FirstRead = Weapon->GetCurrentIndependentShotRecoil();
	const FVector2D SecondRead = Weapon->GetCurrentIndependentShotRecoil();
	TestTrue(TEXT("repeated read-only reads return the same recoil"),
		FirstRead.Equals(SecondRead, 0.001f));
	TestTrue(TEXT("first shot advanced the weapon pattern once"),
		FirstRead.Equals(FVector2D(1.5f, 0.4f), 0.001f));

	float PitchKick = 0.0f;
	float YawKick = 0.0f;
	bool bGenerated = false;
	Weapon->GenerateRecoilKick(PitchKick, YawKick, bGenerated);
	TestTrue(TEXT("read-only observation did not consume the pending kick"), bGenerated);
	TestTrue(TEXT("pending kick still belongs to the first pattern entry"),
		FMath::IsNearlyEqual(PitchKick, 1.5f, 0.001f) && FMath::IsNearlyEqual(YawKick, 0.4f, 0.001f));

	Fixture.Advance(0.1f);
	TestTrue(TEXT("second shot commits after cooldown"),
		Weapon->TryCommitFireWithResult() == EWeaponFireAttemptResult::Committed);
	Weapon->GenerateRecoilKick(PitchKick, YawKick, bGenerated);
	TestTrue(TEXT("pattern advanced only on the next successful shot"), bGenerated &&
		FMath::IsNearlyEqual(PitchKick, 2.0f, 0.001f) && FMath::IsNearlyEqual(YawKick, -0.1f, 0.001f));

	Fixture.Advance(0.8f);
	TestTrue(TEXT("read-only recoil becomes zero after the configured idle reset"),
		Weapon->GetCurrentIndependentShotRecoil().IsNearlyZero(0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponRuntimeIsolationTest,
	"Dust2.EnemyWeapon.PerEnemyRuntimeAndDefinitionIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponRuntimeIsolationTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(4);
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	APawn* EnemyA = Fixture.SpawnPawn();
	APawn* EnemyB = Fixture.SpawnPawn();
	if (!TestNotNull(TEXT("enemy A spawned"), EnemyA) || !TestNotNull(TEXT("enemy B spawned"), EnemyB)) return false;
	UDustEnemyWeaponComponent* ComponentA = Fixture.AddWeaponComponent(EnemyA, Definition, Profile);
	UDustEnemyWeaponComponent* ComponentB = Fixture.AddWeaponComponent(EnemyB, Definition, Profile);
	if (!TestNotNull(TEXT("enemy A weapon component exists"), ComponentA) ||
		!TestNotNull(TEXT("enemy B weapon component exists"), ComponentB)) return false;

	const float SharedInterval = Definition->FireInterval;
	const int32 SharedCapacity = Definition->MagazineCapacity;
	const int32 SharedInitialAmmo = Definition->InitialMagazineAmmo;
	const TObjectPtr<UWeaponRecoilData> SharedRecoil = Definition->RecoilData;

	TestTrue(TEXT("enemy A creates its native runtime"), ComponentA->InitializeWeapon());
	TestTrue(TEXT("enemy B creates its native runtime"), ComponentB->InitializeWeapon());
	AWeaponRuntime* RuntimeA = ComponentA->GetWeaponRuntime();
	AWeaponRuntime* RuntimeB = ComponentB->GetWeaponRuntime();
	if (!TestNotNull(TEXT("enemy A runtime exists"), RuntimeA) ||
		!TestNotNull(TEXT("enemy B runtime exists"), RuntimeB)) return false;
	TestTrue(TEXT("each enemy owns a different runtime instance"), RuntimeA != RuntimeB);
	TestTrue(TEXT("both runtimes read the same immutable definition"),
		RuntimeA->WeaponDefinition == Definition && RuntimeB->WeaponDefinition == Definition);
	TestEqual(TEXT("each runtime gets its own initial ammo"), RuntimeA->GetCurrentAmmo(), 4);
	TestEqual(TEXT("second runtime starts with independent ammo"), RuntimeB->GetCurrentAmmo(), 4);

	TestFalse(TEXT("engagement cannot start without a configured muzzle"),
		ComponentA->StartEngagement(Fixture.SpawnActorWithRoot(FTransform(FVector(500.0f, 0.0f, 0.0f)))));
	TestFalse(TEXT("failed muzzle validation leaves engagement stopped"), ComponentA->IsEngaging());

	TestTrue(TEXT("definition fire interval remains unchanged"), FMath::IsNearlyEqual(Definition->FireInterval, SharedInterval));
	TestEqual(TEXT("shared definition capacity remains unchanged"), Definition->MagazineCapacity, SharedCapacity);
	TestEqual(TEXT("shared definition initial ammo remains unchanged"), Definition->InitialMagazineAmmo, SharedInitialAmmo);
	TestTrue(TEXT("shared recoil asset reference remains unchanged"), Definition->RecoilData == SharedRecoil);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponFiniteAmmoStopTest,
	"Dust2.EnemyWeapon.FiniteAmmoAndStopEngagement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponFiniteAmmoStopTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(2);
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target)) return false;
	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;

	TestTrue(TEXT("valid muzzle and target start engagement"), Component->StartEngagement(Target));
	Fixture.Advance(0.1f);
	TestTrue(TEXT("a real shot is recorded"), Component->SuccessfulShots > 0);
AWeaponRuntime* Runtime = Component->GetWeaponRuntime();
	if (!TestNotNull(TEXT("enemy has a private runtime"), Runtime)) return false;
	const int32 AmmoAfterShot = Runtime->GetCurrentAmmo();
	TestTrue(TEXT("accepted shot consumes finite ammunition"), AmmoAfterShot < 2 && AmmoAfterShot >= 0);

	Component->StopEngagement();
	const int32 ShotsWhenStopped = Component->SuccessfulShots;
	Fixture.Advance(0.3f);
	TestFalse(TEXT("explicit stop clears engagement"), Component->IsEngaging());
	TestEqual(TEXT("stopped component records no further shots"), Component->SuccessfulShots, ShotsWhenStopped);
	TestEqual(TEXT("stopped component consumes no more ammunition"), Runtime->GetCurrentAmmo(), AmmoAfterShot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponRandomVolleySampleTest,
	"Dust2.EnemyWeapon.FullAutoRandomVolleySampleAndPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponRandomVolleySampleTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(30);
	Definition->FireInterval = 0.01f;
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	Profile->BurstShotCount = 3;
	Profile->MaxBurstShotCount = 6;
	Profile->BurstPauseSeconds = 0.6f;

	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target)) return false;
	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;
	if (!TestTrue(TEXT("valid muzzle and target start engagement"), Component->StartEngagement(Target))) return false;

	Fixture.Advance(0.05f);
	if (!TestEqual(TEXT("first committed shot records one success"), Component->SuccessfulShots, 1)) return false;
	const int32 SampledTarget = Component->CurrentVolleyShotTarget;
	TestTrue(TEXT("full-auto samples its target once within the 3-6 range"),
		SampledTarget >= 3 && SampledTarget <= 6);

	// A disjoint range makes a mid-volley resample observable for every initial 3-6 result.
	Profile->BurstShotCount = 9;
	Profile->MaxBurstShotCount = 9;

	for (int32 ShotIndex = 1; ShotIndex < SampledTarget; ++ShotIndex)
	{
		Fixture.Advance(0.05f);
		TestEqual(TEXT("every successful shot advances the current volley"),
			Component->SuccessfulShots, ShotIndex + 1);
		TestEqual(TEXT("the sampled target stays fixed until the final shot"),
			Component->CurrentVolleyShotTarget, ShotIndex + 1 < SampledTarget ? SampledTarget : 0);
	}
	if (!TestEqual(TEXT("the volley ends exactly at its sampled target"),
		Component->SuccessfulShots, SampledTarget)) return false;

	Fixture.Advance(0.5f);
	TestEqual(TEXT("no shot is fired during the volley pause"), Component->SuccessfulShots, SampledTarget);
	Fixture.Advance(0.11f);
	TestEqual(TEXT("fire resumes after the volley pause"), Component->SuccessfulShots, SampledTarget + 1);
	const int32 NextTarget = Component->CurrentVolleyShotTarget;
	TestEqual(TEXT("only the next volley uses the edited fixed range"), NextTarget, 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponVolleyBoundsTest,
	"Dust2.EnemyWeapon.FullAutoVolleyBoundsNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponVolleyBoundsTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(30);
	Definition->FireInterval = 0.01f;
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();

	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target)) return false;
	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;

	const auto SampleVolleyTarget = [&](int32 MinShots, int32 MaxShots) -> int32
	{
		Component->StopEngagement();
		Profile->BurstShotCount = MinShots;
		Profile->MaxBurstShotCount = MaxShots;
		if (!Component->StartEngagement(Target)) return -1;
		Fixture.Advance(0.05f);
		const int32 Sampled = Component->CurrentVolleyShotTarget;
		Component->StopEngagement();
		return Sampled;
	};

	TestEqual(TEXT("equal min and max produces a fixed target"), SampleVolleyTarget(4, 4), 4);
	const int32 Reversed = SampleVolleyTarget(6, 3);
	TestTrue(TEXT("reversed bounds normalize to the 3-6 range"), Reversed >= 3 && Reversed <= 6);

	// A one-shot volley clears its target immediately, so cover its real completion and
	// pause before checking the normalized value directly through the helper.
	Profile->BurstShotCount = 0;
	Profile->MaxBurstShotCount = 0;
	const int32 ShotsBeforeZero = Component->SuccessfulShots;
	if (!TestTrue(TEXT("zero-bound profile still starts engagement"),
		Component->StartEngagement(Target))) return false;
	Fixture.Advance(0.05f);
	TestEqual(TEXT("zero bounds produce exactly one real shot"),
		Component->SuccessfulShots, ShotsBeforeZero + 1);
	TestEqual(TEXT("the normalized one-shot volley completes and clears its target"),
		Component->CurrentVolleyShotTarget, 0);
	Fixture.Advance(0.5f);
	TestEqual(TEXT("the normalized one-shot volley still pauses"),
		Component->SuccessfulShots, ShotsBeforeZero + 1);
	Component->StopEngagement();

	TestEqual(TEXT("zero bounds normalize to one shot"),
		Component->ResolveVolleyShotTarget(EFireMode::FullAutomatic), 1);
	Profile->BurstShotCount = -5;
	Profile->MaxBurstShotCount = 0;
	TestEqual(TEXT("negative/zero bounds normalize to one shot"),
		Component->ResolveVolleyShotTarget(EFireMode::FullAutomatic), 1);
	Profile->BurstShotCount = 0;
	Profile->MaxBurstShotCount = 4;
	const int32 Mixed = Component->ResolveVolleyShotTarget(EFireMode::FullAutomatic);
	TestTrue(TEXT("a clamped lower bound never exceeds the upper bound"), Mixed >= 1 && Mixed <= 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponVolleyStopRestartTest,
	"Dust2.EnemyWeapon.VolleyStopRestartAndRepeatedStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponVolleyStopRestartTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(30);
	Definition->FireInterval = 0.01f;
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	Profile->BurstShotCount = 2;
	Profile->MaxBurstShotCount = 2;
	Profile->BurstPauseSeconds = 0.6f;

	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target)) return false;
	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;
	if (!TestTrue(TEXT("valid muzzle and target start engagement"), Component->StartEngagement(Target))) return false;

	Fixture.Advance(0.05f);
	TestEqual(TEXT("first shot starts the current volley"), Component->SuccessfulShots, 1);
	TestEqual(TEXT("the fixed target is captured once"), Component->CurrentVolleyShotTarget, 2);

	for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		TestTrue(TEXT("repeated StartEngagement for the same target stays accepted"),
			Component->StartEngagement(Target));
	TestEqual(TEXT("repeated StartEngagement does not reset the current volley target"),
		Component->CurrentVolleyShotTarget, 2);

	Fixture.Advance(0.05f);
	TestEqual(TEXT("the current volley continues after repeated StartEngagement"),
		Component->SuccessfulShots, 2);
	TestEqual(TEXT("the current volley ends at its fixed target instead of restarting"),
		Component->CurrentVolleyShotTarget, 0);

	Fixture.Advance(0.5f);
	TestEqual(TEXT("the completed volley still pauses"), Component->SuccessfulShots, 2);

	Component->StopEngagement();
	TestFalse(TEXT("explicit stop clears engagement"), Component->IsEngaging());
	TestEqual(TEXT("explicit stop clears the current volley target"), Component->CurrentVolleyShotTarget, 0);

	if (!TestTrue(TEXT("restart after a completed volley accepts the same target"),
		Component->StartEngagement(Target))) return false;
	Fixture.Advance(0.05f);
	TestEqual(TEXT("restart counts a fresh volley shot"), Component->SuccessfulShots, 3);
	TestEqual(TEXT("restart starts a fresh volley"), Component->CurrentVolleyShotTarget, 2);

	// Stop in the middle of this volley; the remaining shot must not survive.
	Component->StopEngagement();
	TestEqual(TEXT("stopping an unfinished volley clears its target"),
		Component->CurrentVolleyShotTarget, 0);
	if (!TestTrue(TEXT("restart after an unfinished volley accepts the same target"),
		Component->StartEngagement(Target))) return false;
	Fixture.Advance(0.05f);
	TestEqual(TEXT("restart after an unfinished volley counts a new first shot"),
		Component->SuccessfulShots, 4);
	TestEqual(TEXT("restart after an unfinished volley samples a new target"),
		Component->CurrentVolleyShotTarget, 2);
	Fixture.Advance(0.05f);
	TestEqual(TEXT("the restarted volley completes at its own target"),
		Component->CurrentVolleyShotTarget, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponVolleyCooldownTest,
	"Dust2.EnemyWeapon.VolleyCooldownDoesNotCountOrResample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponVolleyCooldownTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(30);
	Definition->FireInterval = 0.06f;
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	Profile->BurstShotCount = 1;
	Profile->MaxBurstShotCount = 1;
	Profile->BurstPauseSeconds = 0.6f;

	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target)) return false;
	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;
	if (!TestTrue(TEXT("valid muzzle and target start engagement"), Component->StartEngagement(Target))) return false;

	Fixture.Advance(0.05f);
	if (!TestEqual(TEXT("the first shot completes the single-shot volley"),
		Component->SuccessfulShots, 1)) return false;
	TestEqual(TEXT("a completed volley clears its target"), Component->CurrentVolleyShotTarget, 0);

	// Let the component's post-volley retry window approach, then simulate another caller
	// committing a shot so the runtime is still cooling when the AI retries.
	Fixture.Advance(0.55f);
	Fixture.Advance(0.01f);
	AWeaponRuntime* Runtime = Component->GetWeaponRuntime();
	if (!TestNotNull(TEXT("enemy has a private runtime"), Runtime)) return false;
	TestTrue(TEXT("external commit succeeds before the component retry"),
		Runtime->TryCommitFireWithResult() == EWeaponFireAttemptResult::Committed);

	Fixture.Advance(0.05f);
	TestEqual(TEXT("a cooldown attempt is not counted as a successful shot"),
		Component->SuccessfulShots, 1);
	TestEqual(TEXT("a cooldown attempt does not sample a new volley target"),
		Component->CurrentVolleyShotTarget, 0);

	Fixture.Advance(0.05f);
	TestEqual(TEXT("the component retries after the runtime cooldown"),
		Component->SuccessfulShots, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponNativeVolleyModesTest,
	"Dust2.EnemyWeapon.NativeVolleyModesAndLiveModeChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponNativeVolleyModesTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(30);
	Definition->AllowedFireModes = {
		EFireMode::FullAutomatic, EFireMode::Burst, EFireMode::SemiAutomatic};
	Definition->FireInterval = 0.2f;
	Definition->BurstShotInterval = 0.0f; // Burst uses FireInterval here, not an override.
	Definition->BurstRecoveryInterval = 0.8f;
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	Profile->BurstShotCount = 6;
	Profile->MaxBurstShotCount = 6;
	Profile->BurstPauseSeconds = 0.1f;

	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target)) return false;
	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;
	if (!TestTrue(TEXT("valid muzzle and target start engagement"), Component->StartEngagement(Target))) return false;

	Fixture.Advance(0.05f);
	TestEqual(TEXT("full-auto fires its first shot"), Component->SuccessfulShots, 1);
	TestEqual(TEXT("full-auto target is six"), Component->CurrentVolleyShotTarget, 6);

	AWeaponRuntime* Runtime = Component->GetWeaponRuntime();
	if (!TestNotNull(TEXT("enemy has a private runtime"), Runtime)) return false;
	Runtime->RestoreFireMode(EFireMode::Burst);

	Fixture.Advance(0.25f);
	TestEqual(TEXT("burst starts a native three-shot volley"), Component->SuccessfulShots, 2);
	TestEqual(TEXT("the old full-auto count is not carried into burst"),
		Component->CurrentVolleyShotTarget, 3);

	Fixture.Advance(0.25f);
	TestEqual(TEXT("burst fires its second shot"), Component->SuccessfulShots, 3);
	TestEqual(TEXT("burst volley still has one shot left"), Component->CurrentVolleyShotTarget, 3);

	Fixture.Advance(0.25f);
	TestEqual(TEXT("burst completes at three shots"), Component->SuccessfulShots, 4);
	TestEqual(TEXT("burst volley clears its target after the third shot"),
		Component->CurrentVolleyShotTarget, 0);

	Fixture.Advance(0.6f);
	TestEqual(TEXT("burst recovery interval still blocks the next volley"),
		Component->SuccessfulShots, 4);

	Fixture.Advance(0.15f);
	TestEqual(TEXT("burst resumes after the recovery interval"), Component->SuccessfulShots, 5);
	TestEqual(TEXT("the next burst volley samples three again"), Component->CurrentVolleyShotTarget, 3);

	Runtime->RestoreFireMode(EFireMode::SemiAutomatic);
	Fixture.Advance(0.15f);
	TestEqual(TEXT("semi-automatic stays one shot per volley"), Component->SuccessfulShots, 6);
	TestEqual(TEXT("semi-automatic clears its single-shot target"),
		Component->CurrentVolleyShotTarget, 0);

	Fixture.Advance(0.1f);
	TestEqual(TEXT("semi-automatic still waits for its interval"), Component->SuccessfulShots, 6);

	Fixture.Advance(0.15f);
	TestEqual(TEXT("semi-automatic fires again after its interval"), Component->SuccessfulShots, 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponEmptyVolleyTest,
	"Dust2.EnemyWeapon.EmptyMagazineCannotCompleteVolley",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponEmptyVolleyTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture;
	if (!TestNotNull(TEXT("isolated test world exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(1);
	Definition->FireInterval = 0.01f;
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	Profile->BurstShotCount = 3;
	Profile->MaxBurstShotCount = 6;
	Profile->BurstPauseSeconds = 0.6f;

	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target)) return false;
	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;
	if (!TestTrue(TEXT("valid muzzle and target start engagement"), Component->StartEngagement(Target))) return false;

	Fixture.Advance(0.05f);
	TestEqual(TEXT("one round produces one real shot"), Component->SuccessfulShots, 1);
	TestTrue(TEXT("the incomplete volley still has a sampled target"),
		Component->CurrentVolleyShotTarget >= 3 && Component->CurrentVolleyShotTarget <= 6);
	AWeaponRuntime* Runtime = Component->GetWeaponRuntime();
	if (!TestNotNull(TEXT("enemy has a private runtime"), Runtime)) return false;
	TestEqual(TEXT("the only magazine round was consumed"), Runtime->GetCurrentAmmo(), 0);

	Fixture.Advance(0.05f);
	TestFalse(TEXT("empty magazine stops engagement before another shot"), Component->IsEngaging());
	TestEqual(TEXT("empty magazine cannot fabricate successful shots"), Component->SuccessfulShots, 1);
	TestEqual(TEXT("empty magazine clears the pending volley target"),
		Component->CurrentVolleyShotTarget, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyWeaponWallOcclusionTest,
	"Dust2.EnemyWeapon.VisibilityBlockPreventsShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWeaponWallOcclusionTest::RunTest(const FString& Parameters)
{
	FScopedEnemyWeaponTestWorld Fixture(true);
	if (!TestNotNull(TEXT("test world with query physics scene exists"), Fixture.Get())) return false;

	UWeaponDefinitionData* Definition = Fixture.MakeDefinition(3);
	UDustEnemyCombatProfile* Profile = Fixture.MakeProfile();
	APawn* Enemy = Fixture.SpawnPawn();
	AActor* Target = Fixture.SpawnActorWithRoot(FTransform(FVector(600.0f, 0.0f, 0.0f)));
	AActor* Wall = Fixture.SpawnActorWithRoot(FTransform(FVector(300.0f, 0.0f, 50.0f)));
	if (!TestNotNull(TEXT("enemy spawned"), Enemy) || !TestNotNull(TEXT("target spawned"), Target) ||
		!TestNotNull(TEXT("wall actor spawned"), Wall)) return false;

	UBoxComponent* WallCollision = NewObject<UBoxComponent>(Wall, TEXT("TestBlockingWall"));
	WallCollision->SetBoxExtent(FVector(20.0f, 200.0f, 200.0f));
	WallCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallCollision->SetCollisionObjectType(ECC_WorldStatic);
	WallCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->SetRootComponent(WallCollision);
	WallCollision->RegisterComponent();
	Wall->SetActorLocation(FVector(300.0f, 0.0f, 50.0f));

	UDustEnemyWeaponComponent* Component = Fixture.AddWeaponComponent(Enemy, Definition, Profile);
	if (!TestNotNull(TEXT("weapon component exists"), Component)) return false;
	if (!TestNotNull(TEXT("muzzle source attached"), Fixture.AddMuzzle(Enemy, Component))) return false;
	if (!TestTrue(TEXT("engagement starts before the first sight check"), Component->StartEngagement(Target))) return false;

	const int32 AmmoBeforeSightCheck = Component->GetWeaponRuntime()->GetCurrentAmmo();
	Fixture.Advance(0.1f);
	TestFalse(TEXT("blocking wall terminates visible engagement"), Component->IsEngaging());
	TestEqual(TEXT("wall occlusion never commits a successful shot"), Component->SuccessfulShots, 0);
	TestEqual(TEXT("wall occlusion preserves ammunition"),
		Component->GetWeaponRuntime()->GetCurrentAmmo(), AmmoBeforeSightCheck);
	return true;
}

#endif
