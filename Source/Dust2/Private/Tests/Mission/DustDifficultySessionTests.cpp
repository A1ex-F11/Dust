#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"

#include "AI/DustEnemyCombatProfile.h"
#include "AI/DustEnemyCombatAdapterComponent.h"
#include "AIController.h"
#include "Components/StateTreeAIComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Combat/DustEnemyHealthComponent.h"
#include "../Combat/WeaponShotTestReceiver.h"
#include "Mission/Dust2GameMode.h"
#include "Mission/DustEnemyDifficulty.h"
#include "Mission/DustGameSessionSubsystem.h"
#include "Mission/DustMissionRetryAction.h"
#include "Player/Dust2PlayerCharacter.h"
#include "Weapons/FireMode.h"
#include "Weapons/WeaponDefinitionData.h"
#include "Weapons/WeaponRecoilData.h"
#include "Weapons/WeaponRuntime.h"
#include "../../Weapons/WeaponBallisticsData.h"

// Native-only access for observing the component's live-engagement branch and forcing it in a
// fixture that does not need a full weapon runtime. Production calls still use the public API.
struct FDustDifficultySessionTestAccess
{
	static void BindAdapter(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
		ACharacter* Pawn, UDustEnemyWeaponComponent* Weapon, UStateTreeAIComponent* Tree)
	{
		Adapter->Controller = Controller;
		Adapter->BoundPawn = Pawn;
		Adapter->WeaponComponent = Weapon;
		Adapter->Tree = Tree;
		Adapter->bInitialized = true;
		Adapter->bStopped = false;
	}
	static void SetEngaging(UDustEnemyWeaponComponent* Component, bool bEngaging)
	{
		if (Component) Component->bEngaging = bEngaging;
	}

	static bool IsEngaging(const UDustEnemyWeaponComponent* Component)
	{
		return Component && Component->bEngaging;
	}
};

namespace
{
	constexpr auto DustDifficultyTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UDustEnemyCombatProfile* MakeDifficultyProfile(float ReactionDelay)
	{
		UDustEnemyCombatProfile* Profile =
			NewObject<UDustEnemyCombatProfile>(GetTransientPackage());
		Profile->ReactionDelay = ReactionDelay;
		return Profile;
	}

	UWeaponDefinitionData* MakeDifficultyWeaponDefinition()
	{
		UWeaponDefinitionData* Definition =
			NewObject<UWeaponDefinitionData>(GetTransientPackage());
		UWeaponBallisticsData* Ballistics = NewObject<UWeaponBallisticsData>(Definition);
		Ballistics->DamagePerShot = 10.0f;
		Ballistics->ArmorPenetration = 0.0f;
		Ballistics->HeadDamageMultiplier = 2.0f;
		Ballistics->TraceDistance = 2000.0f;

		Definition->BallisticsData = Ballistics;
		Definition->MagazineCapacity = 10;
		Definition->InitialMagazineAmmo = 10;
		Definition->InitialReserveAmmo = 0;
		Definition->FireInterval = 0.1f;
		Definition->AllowedFireModes = {EFireMode::FullAutomatic};
		Definition->DefaultFireMode = EFireMode::FullAutomatic;

		UWeaponRecoilData* Recoil = NewObject<UWeaponRecoilData>(Definition);
		Definition->RecoilData = Recoil;
		return Definition;
	}

	UDustEnemyWeaponComponent* AddDifficultyWeapon(
		AActor* Owner, UDustEnemyCombatProfile* Profile)
	{
		if (!Owner) return nullptr;
		UDustEnemyWeaponComponent* Weapon = NewObject<UDustEnemyWeaponComponent>(Owner);
		Weapon->CombatProfile = Profile;
		Owner->AddInstanceComponent(Weapon);
		Weapon->RegisterComponent();
		return Weapon;
	}

	struct FDustDifficultySessionWorld
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		UDustGameSessionSubsystem* Session = nullptr;

		FDustDifficultySessionWorld()
		{
			if (!GEngine) return;
			GameInstance = NewObject<UGameInstance>(GEngine);
			if (!GameInstance) return;
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
			if (!World) return;
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Session = GameInstance->GetSubsystem<UDustGameSessionSubsystem>();
		}

		~FDustDifficultySessionWorld()
		{
			if (World) World->EndPlay(EEndPlayReason::Quit);
			if (GameInstance) GameInstance->Shutdown();
			if (World && GEngine) GEngine->DestroyWorldContext(World);
			if (World) World->DestroyWorld(false);
		}

		AActor* SpawnWeaponOwner(UDustEnemyCombatProfile* Profile) const
		{
			AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
			if (!Owner) return nullptr;
			AddDifficultyWeapon(Owner, Profile);
			return Owner;
		}

		APawn* SpawnWeaponPawn(UDustEnemyCombatProfile* Profile) const
		{
			APawn* Pawn = World ? World->SpawnActor<APawn>() : nullptr;
			if (!Pawn) return nullptr;
			USceneComponent* Root = NewObject<USceneComponent>(Pawn, TEXT("DifficultyTestPawnRoot"));
			Pawn->SetRootComponent(Root);
			Root->RegisterComponent();
			AddDifficultyWeapon(Pawn, Profile);
			return Pawn;
		}
	};

	struct FDustDifficultyBareWorld
	{
		UWorld* World = nullptr;

		FDustDifficultyBareWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FDustDifficultyBareWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
	};

	struct FDustMissionRetryWorld
	{
		UWorld* World = nullptr;
		ADust2GameMode* Mode = nullptr;

		FDustMissionRetryWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Mode = World->SpawnActor<ADust2GameMode>();
		}

		~FDustMissionRetryWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}

		ADust2PlayerCharacter* SpawnPlayer(float Health = 100.0f) const
		{
			ADust2PlayerCharacter* Player = World ? World->SpawnActor<ADust2PlayerCharacter>() : nullptr;
			if (Player) Player->RestoreHealth(Health);
			return Player;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDifficultyConfigureAndReselect,
	"Dust2.Difficulty.ConfigureBeginAndReselect", DustDifficultyTestFlags)
bool FDustDifficultyConfigureAndReselect::RunTest(const FString&)
{
	FDustDifficultySessionWorld F;
	if (!TestNotNull(TEXT("GameInstance"), F.GameInstance)) return false;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	if (!TestNotNull(TEXT("Session subsystem"), F.Session)) return false;

	UDustEnemyCombatProfile* Easy = MakeDifficultyProfile(0.2f);
	UDustEnemyCombatProfile* Normal = MakeDifficultyProfile(0.4f);
	UDustEnemyCombatProfile* Hard = MakeDifficultyProfile(0.8f);

	TestTrue(TEXT("Default selection is Normal"),
		F.Session->GetSelectedDifficulty() == EDustEnemyDifficulty::Normal);
	TestFalse(TEXT("No session before BeginNewSession"), F.Session->IsSessionActive());
	TestNull(TEXT("No selected profile before BeginNewSession"),
		F.Session->GetSelectedCombatProfile());

	TestFalse(TEXT("Invalid enum rejected"),
		F.Session->BeginNewSession(static_cast<EDustEnemyDifficulty>(255)));
	TestFalse(TEXT("Unconfigured profile rejected"),
		F.Session->BeginNewSession(EDustEnemyDifficulty::Easy));
	TestFalse(TEXT("Rejected begin leaves the session inactive"), F.Session->IsSessionActive());
	TestTrue(TEXT("Rejected begin keeps the default selection"),
		F.Session->GetSelectedDifficulty() == EDustEnemyDifficulty::Normal);
	TestNull(TEXT("Rejected begin keeps no selected profile"),
		F.Session->GetSelectedCombatProfile());

	TestTrue(TEXT("Three profiles configured"),
		F.Session->ConfigureDifficultyProfiles(Easy, Normal, Hard));
	TestTrue(TEXT("Easy lookup"), F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Easy) == Easy);
	TestTrue(TEXT("Normal lookup"), F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Normal) == Normal);
	TestTrue(TEXT("Hard lookup"), F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Hard) == Hard);
	TestNull(TEXT("Invalid difficulty has no profile"),
		F.Session->GetDifficultyProfile(static_cast<EDustEnemyDifficulty>(255)));

	TestTrue(TEXT("Normal session begins"), F.Session->BeginNewSession(EDustEnemyDifficulty::Normal));
	TestTrue(TEXT("Session is active"), F.Session->IsSessionActive());
	TestTrue(TEXT("Selected difficulty committed"),
		F.Session->GetSelectedDifficulty() == EDustEnemyDifficulty::Normal);
	TestTrue(TEXT("Selected profile committed"), F.Session->GetSelectedCombatProfile() == Normal);

	TestFalse(TEXT("Active session refuses another BeginNewSession"),
		F.Session->BeginNewSession(EDustEnemyDifficulty::Hard));
	TestTrue(TEXT("Refused switch keeps the selected difficulty"),
		F.Session->GetSelectedDifficulty() == EDustEnemyDifficulty::Normal);

	TestFalse(TEXT("Active session refuses a different profile triple"),
		F.Session->ConfigureDifficultyProfiles(Hard, Easy, Normal));
	TestTrue(TEXT("Refused configure keeps Easy"),
		F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Easy) == Easy);
	TestTrue(TEXT("Refused configure keeps Normal"),
		F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Normal) == Normal);
	TestTrue(TEXT("Refused configure keeps Hard"),
		F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Hard) == Hard);
	TestTrue(TEXT("Identical active configuration is idempotent"),
		F.Session->ConfigureDifficultyProfiles(Easy, Normal, Hard));

	F.Session->EndSessionForDifficultySelection();
	TestFalse(TEXT("End clears the active session"), F.Session->IsSessionActive());
	TestNull(TEXT("End clears the selected profile"), F.Session->GetSelectedCombatProfile());
	TestTrue(TEXT("End keeps the last selected difficulty"),
		F.Session->GetSelectedDifficulty() == EDustEnemyDifficulty::Normal);
	TestTrue(TEXT("End keeps the three configured profiles"),
		F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Easy) == Easy
		&& F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Normal) == Normal
		&& F.Session->GetDifficultyProfile(EDustEnemyDifficulty::Hard) == Hard);

	TestTrue(TEXT("Inactive session accepts a new profile triple"),
		F.Session->ConfigureDifficultyProfiles(Hard, Normal, Easy));
	TestTrue(TEXT("Reselection begins after End"),
		F.Session->BeginNewSession(EDustEnemyDifficulty::Hard));
	TestTrue(TEXT("Reselection uses the newly configured Hard slot"),
		F.Session->GetSelectedCombatProfile() == Easy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDifficultyMissingProfile,
	"Dust2.Difficulty.MissingProfileDoesNotChangeState", DustDifficultyTestFlags)
bool FDustDifficultyMissingProfile::RunTest(const FString&)
{
	FDustDifficultySessionWorld F;
	if (!TestNotNull(TEXT("Session subsystem"), F.Session)) return false;

	UDustEnemyCombatProfile* Normal = MakeDifficultyProfile(0.4f);
	UDustEnemyCombatProfile* Hard = MakeDifficultyProfile(0.8f);

	TestTrue(TEXT("Partial configuration is accepted but incomplete"),
		F.Session->ConfigureDifficultyProfiles(nullptr, Normal, nullptr));

	TestFalse(TEXT("Easy is missing"),
		F.Session->BeginNewSession(EDustEnemyDifficulty::Easy));
	TestFalse(TEXT("Missing Easy leaves the session inactive"), F.Session->IsSessionActive());
	TestTrue(TEXT("Missing Easy keeps the selected difficulty"),
		F.Session->GetSelectedDifficulty() == EDustEnemyDifficulty::Normal);
	TestNull(TEXT("Missing Easy keeps no selected profile"),
		F.Session->GetSelectedCombatProfile());

	TestFalse(TEXT("Hard is missing"),
		F.Session->BeginNewSession(EDustEnemyDifficulty::Hard));
	TestFalse(TEXT("Missing Hard leaves the session inactive"), F.Session->IsSessionActive());

	TestTrue(TEXT("Configured Normal still begins"),
		F.Session->BeginNewSession(EDustEnemyDifficulty::Normal));
	TestTrue(TEXT("Normal profile selected"), F.Session->GetSelectedCombatProfile() == Normal);

	F.Session->EndSessionForDifficultySelection();
	TestTrue(TEXT("Inactive session accepts a Hard-only configuration"),
		F.Session->ConfigureDifficultyProfiles(nullptr, nullptr, Hard));
	TestFalse(TEXT("Hard-only configuration cannot start Easy"),
		F.Session->BeginNewSession(EDustEnemyDifficulty::Easy));
	TestFalse(TEXT("Still inactive after the failed Easy request"), F.Session->IsSessionActive());
	TestTrue(TEXT("Failed request keeps the previous last difficulty"),
		F.Session->GetSelectedDifficulty() == EDustEnemyDifficulty::Normal);
	TestNull(TEXT("Failed request keeps no selected profile"),
		F.Session->GetSelectedCombatProfile());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDifficultyEnemyApplication,
	"Dust2.Difficulty.AppliesToPreplacedAndLateEnemies", DustDifficultyTestFlags)
bool FDustDifficultyEnemyApplication::RunTest(const FString&)
{
	FDustDifficultySessionWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	if (!TestNotNull(TEXT("Session subsystem"), F.Session)) return false;

	UDustEnemyCombatProfile* DefaultA = MakeDifficultyProfile(0.3f);
	UDustEnemyCombatProfile* Easy = MakeDifficultyProfile(0.2f);
	UDustEnemyCombatProfile* Normal = MakeDifficultyProfile(0.4f);
	UDustEnemyCombatProfile* Hard = MakeDifficultyProfile(0.8f);

	AActor* OwnerA = F.SpawnWeaponOwner(DefaultA);
	AActor* OwnerB = F.SpawnWeaponOwner(DefaultA);
	if (!TestNotNull(TEXT("Pre-placed enemy A"), OwnerA)) return false;
	if (!TestNotNull(TEXT("Pre-placed enemy B"), OwnerB)) return false;
	UDustEnemyWeaponComponent* WeaponA = OwnerA->FindComponentByClass<UDustEnemyWeaponComponent>();
	UDustEnemyWeaponComponent* WeaponB = OwnerB->FindComponentByClass<UDustEnemyWeaponComponent>();
	if (!TestNotNull(TEXT("Weapon A"), WeaponA)) return false;
	if (!TestNotNull(TEXT("Weapon B"), WeaponB)) return false;

	TestFalse(TEXT("Inactive session does not apply a profile"), WeaponA->ApplySelectedSessionDifficulty());
	TestTrue(TEXT("Inactive session leaves A's instance default"),
		WeaponA->CombatProfile == DefaultA);

	TestTrue(TEXT("Configure three profiles"),
		F.Session->ConfigureDifficultyProfiles(Easy, Normal, Hard));
	TestTrue(TEXT("Begin Hard"), F.Session->BeginNewSession(EDustEnemyDifficulty::Hard));
	TestTrue(TEXT("Pre-placed enemy A receives Hard"), WeaponA->CombatProfile == Hard);
	TestTrue(TEXT("Pre-placed enemy B receives Hard"), WeaponB->CombatProfile == Hard);

	APawn* LatePawn = F.SpawnWeaponPawn(DefaultA);
	if (!TestNotNull(TEXT("Late enemy pawn"), LatePawn)) return false;
	UDustEnemyWeaponComponent* LateWeapon = LatePawn->FindComponentByClass<UDustEnemyWeaponComponent>();
	if (!TestNotNull(TEXT("Late weapon"), LateWeapon)) return false;
	LateWeapon->WeaponDefinition = MakeDifficultyWeaponDefinition();

	TestTrue(TEXT("Late enemy initializes"), LateWeapon->InitializeWeapon());
	TestTrue(TEXT("InitializeWeapon applies the active session profile"), LateWeapon->CombatProfile == Hard);
	TestNotNull(TEXT("Late enemy runtime exists"), LateWeapon->GetWeaponRuntime());

	// Force the instance default and re-enter the initialized-runtime early-out. The apply call
	// must still run before that return.
	LateWeapon->CombatProfile = DefaultA;
	TestTrue(TEXT("Repeated InitializeWeapon succeeds"), LateWeapon->InitializeWeapon());
	TestTrue(TEXT("InitializeWeapon applies before the early-out"), LateWeapon->CombatProfile == Hard);

	F.Session->EndSessionForDifficultySelection();
	TestTrue(TEXT("Pre-placed enemy keeps the last applied profile"), WeaponA->CombatProfile == Hard);
	TestTrue(TEXT("Inactive session can be reconfigured"),
		F.Session->ConfigureDifficultyProfiles(DefaultA, Easy, Hard));
	TestTrue(TEXT("Begin Easy after End"), F.Session->BeginNewSession(EDustEnemyDifficulty::Easy));
	TestTrue(TEXT("Pre-placed enemy A reselected to the new Easy profile"),
		WeaponA->CombatProfile == DefaultA);
	TestTrue(TEXT("Pre-placed enemy B reselected to the new Easy profile"),
		WeaponB->CombatProfile == DefaultA);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDifficultyNoGameInstance,
	"Dust2.Difficulty.NoGameInstanceKeepsDefaultProfile", DustDifficultyTestFlags)
bool FDustDifficultyNoGameInstance::RunTest(const FString&)
{
	FDustDifficultyBareWorld F;
	if (!TestNotNull(TEXT("World without a game instance"), F.World)) return false;

	UDustEnemyCombatProfile* DefaultA = MakeDifficultyProfile(0.3f);
	AActor* Owner = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Enemy owner"), Owner)) return false;
	UDustEnemyWeaponComponent* Weapon = AddDifficultyWeapon(Owner, DefaultA);
	if (!TestNotNull(TEXT("Enemy weapon"), Weapon)) return false;

	TestFalse(TEXT("No game instance falls back without applying"),
		Weapon->ApplySelectedSessionDifficulty());
	TestTrue(TEXT("Fallback keeps the existing CombatProfile"), Weapon->CombatProfile == DefaultA);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDifficultyEngagement,
	"Dust2.Difficulty.SameProfileKeepsEngagement", DustDifficultyTestFlags)
bool FDustDifficultyEngagement::RunTest(const FString&)
{
	FDustDifficultySessionWorld F;
	if (!TestNotNull(TEXT("Session subsystem"), F.Session)) return false;

	UDustEnemyCombatProfile* DefaultA = MakeDifficultyProfile(0.3f);
	UDustEnemyCombatProfile* Hard = MakeDifficultyProfile(0.8f);
	TestTrue(TEXT("Configure two profiles and the session default"),
		F.Session->ConfigureDifficultyProfiles(DefaultA, DefaultA, Hard));
	TestTrue(TEXT("Begin Hard"), F.Session->BeginNewSession(EDustEnemyDifficulty::Hard));

	AActor* Owner = F.SpawnWeaponOwner(DefaultA);
	if (!TestNotNull(TEXT("Enemy owner"), Owner)) return false;
	UDustEnemyWeaponComponent* Weapon = Owner->FindComponentByClass<UDustEnemyWeaponComponent>();
	if (!TestNotNull(TEXT("Enemy weapon"), Weapon)) return false;

	FDustDifficultySessionTestAccess::SetEngaging(Weapon, true);
	TestTrue(TEXT("Changed profile applies"), Weapon->ApplySelectedSessionDifficulty());
	TestFalse(TEXT("Changed profile stops engagement before assignment"),
		FDustDifficultySessionTestAccess::IsEngaging(Weapon));
	TestTrue(TEXT("Changed profile assigned"), Weapon->CombatProfile == Hard);

	FDustDifficultySessionTestAccess::SetEngaging(Weapon, true);
	TestTrue(TEXT("Identical profile is accepted"), Weapon->ApplySelectedSessionDifficulty());
	TestTrue(TEXT("Identical profile keeps live engagement"),
		FDustDifficultySessionTestAccess::IsEngaging(Weapon));
	TestTrue(TEXT("Identical profile keeps the selected asset"), Weapon->CombatProfile == Hard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDifficultyCapturedAvoidance,
	"Dust2.Difficulty.RefreshesPreplacedAvoidance", DustDifficultyTestFlags)
bool FDustDifficultyCapturedAvoidance::RunTest(const FString&)
{
	FDustDifficultySessionWorld F;
	if (!TestNotNull(TEXT("Session subsystem"), F.Session)) return false;
	UDustEnemyCombatProfile* Original = MakeDifficultyProfile(0.4f);
	UDustEnemyCombatProfile* Hard = MakeDifficultyProfile(0.2f);
	Hard->LocalAvoidanceRadius = 750.0f;
	ACharacter* Pawn = F.World->SpawnActor<ACharacter>();
	AAIController* Controller = F.World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Character"), Pawn) || !TestNotNull(TEXT("Controller"), Controller)) return false;
	Controller->Possess(Pawn);
	UDustEnemyWeaponComponent* Weapon = AddDifficultyWeapon(Pawn, Original);
	UDustEnemyCombatAdapterComponent* Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
	UStateTreeAIComponent* Tree = NewObject<UStateTreeAIComponent>(Controller);
	Controller->AddInstanceComponent(Adapter);
	Controller->AddInstanceComponent(Tree);
	FDustDifficultySessionTestAccess::BindAdapter(Adapter, Controller, Pawn, Weapon, Tree);
	UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
	Movement->SetAvoidanceEnabled(false);
	Movement->AvoidanceConsiderationRadius = 321.0f;
	Adapter->RefreshCombatProfileSettings();
	TestTrue(TEXT("Original profile captured"), FMath::IsNearlyEqual(Movement->AvoidanceConsiderationRadius, Original->LocalAvoidanceRadius));
	F.Session->ConfigureDifficultyProfiles(Original, Original, Hard);
	TestTrue(TEXT("Select Hard"), F.Session->BeginNewSession(EDustEnemyDifficulty::Hard));
	TestTrue(TEXT("New radius applied to existing initialized AI"), FMath::IsNearlyEqual(Movement->AvoidanceConsiderationRadius, 750.0f));
	TestTrue(TEXT("New profile enables avoidance"), Movement->bUseRVOAvoidance);
	F.Session->EndSessionForDifficultySelection();
	UDustEnemyCombatProfile* Disabled = MakeDifficultyProfile(0.5f);
	Disabled->bEnableLocalAvoidance = false;
	F.Session->ConfigureDifficultyProfiles(Disabled, Original, Hard);
	TestTrue(TEXT("Select disabled profile"), F.Session->BeginNewSession(EDustEnemyDifficulty::Easy));
	TestFalse(TEXT("Prior owned avoidance restored"), Movement->bUseRVOAvoidance);
	TestTrue(TEXT("Prior radius restored"), FMath::IsNearlyEqual(Movement->AvoidanceConsiderationRadius, 321.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionRetryActionMapping,
	"Dust2.Mission.RetryActionMappingAndNoSideEffects", DustDifficultyTestFlags)
bool FDustMissionRetryActionMapping::RunTest(const FString&)
{
	FDustMissionRetryWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;

	TestFalse(TEXT("Unresolved result is not a victory"), F.Mode->HasResolvedMissionResult());
	TestTrue(TEXT("Constructor placeholder is Defused"),
		F.Mode->GetMissionResult() == EMissionResult::Defused);
	TestTrue(TEXT("Unresolved without checkpoint is unavailable"),
		F.Mode->GetMissionRetryAction(false) == EDustMissionRetryAction::Unavailable);
	TestTrue(TEXT("Unresolved with checkpoint is unavailable"),
		F.Mode->GetMissionRetryAction(true) == EDustMissionRetryAction::Unavailable);

	F.Mode->RestoreMissionState(EMissionState::ReturnToA);
	TestTrue(TEXT("Victory result accepted"), F.Mode->TryResolveMissionResult(EMissionResult::Defused));
	TestTrue(TEXT("Resolved victory is reported"), F.Mode->HasResolvedMissionResult());
	TestTrue(TEXT("Victory is Defused"), F.Mode->GetMissionResult() == EMissionResult::Defused);
	TestTrue(TEXT("Victory without checkpoint chooses difficulty"),
		F.Mode->GetMissionRetryAction(false) == EDustMissionRetryAction::ChooseDifficulty);
	TestTrue(TEXT("Victory with checkpoint still chooses difficulty"),
		F.Mode->GetMissionRetryAction(true) == EDustMissionRetryAction::ChooseDifficulty);
	TestTrue(TEXT("Read-only query keeps the victory"),
		F.Mode->GetMissionRetryAction(false) == EDustMissionRetryAction::ChooseDifficulty);

	F.Mode->RestoreMissionState(EMissionState::C4Planted);
	TestTrue(TEXT("Exploded failure accepted"), F.Mode->TryResolveMissionResult(EMissionResult::Exploded));
	TestTrue(TEXT("Exploded without checkpoint restarts"),
		F.Mode->GetMissionRetryAction(false) == EDustMissionRetryAction::RestartFromBeginning);
	TestTrue(TEXT("Exploded with checkpoint restores"),
		F.Mode->GetMissionRetryAction(true) == EDustMissionRetryAction::RestoreCheckpoint);

	F.Mode->RestoreMissionState(EMissionState::C4Planted);
	TestTrue(TEXT("PlayerDied failure accepted"), F.Mode->TryResolveMissionResult(EMissionResult::PlayerDied));
	TestTrue(TEXT("PlayerDied without checkpoint restarts"),
		F.Mode->GetMissionRetryAction(false) == EDustMissionRetryAction::RestartFromBeginning);
	TestTrue(TEXT("PlayerDied with checkpoint restores"),
		F.Mode->GetMissionRetryAction(true) == EDustMissionRetryAction::RestoreCheckpoint);

	F.Mode->RestoreMissionState(EMissionState::Resupply);
	TestFalse(TEXT("Restore clears the result lock"), F.Mode->HasResolvedMissionResult());
	TestTrue(TEXT("Restored mission is unavailable"),
		F.Mode->GetMissionRetryAction(true) == EDustMissionRetryAction::Unavailable);

	TestTrue(TEXT("Fresh failure accepted before reset"),
		F.Mode->TryResolveMissionResult(EMissionResult::Exploded));
	F.Mode->NativeResetMissionProgress();
	TestFalse(TEXT("Reset clears the result lock"), F.Mode->HasResolvedMissionResult());
	TestTrue(TEXT("Reset mission is unavailable"),
		F.Mode->GetMissionRetryAction(false) == EDustMissionRetryAction::Unavailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionExplosionPlantedOnce,
	"Dust2.Mission.BombExplosionOnlyPlantedAndOnce", DustDifficultyTestFlags)
bool FDustMissionExplosionPlantedOnce::RunTest(const FString&)
{
	FDustMissionRetryWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	ADust2PlayerCharacter* Player = F.SpawnPlayer();
	if (!TestNotNull(TEXT("Player"), Player)) return false;

	const auto SpawnArmoredEnemy = [&F](float X)
	{
		auto* Enemy = F.World->SpawnActor<AWeaponShotTestReceiver>(
			FVector(X, 0.0, 0.0), FRotator::ZeroRotator);
		if (!Enemy) return Enemy;
		auto* Health = NewObject<UDustEnemyHealthComponent>(Enemy);
		Enemy->AddInstanceComponent(Health);
		Health->RegisterComponent();
		Health->OnNativeEnemyHealthChanged.AddDynamic(Enemy, &AWeaponShotTestReceiver::TrackNativeEnemyHealth);
		Health->InitializeNativeEnemyHealth(100.0f, true, true);
		return Enemy;
	};
	AWeaponShotTestReceiver* NearbyEnemy = SpawnArmoredEnemy(300.0f);
	AWeaponShotTestReceiver* FarEnemy = SpawnArmoredEnemy(25000.0f);
	if (!TestTrue(TEXT("Both armored enemies spawned"), NearbyEnemy && FarEnemy)) return false;

	bool bDiedNow = true;
	TestFalse(TEXT("Explosion before C4 is rejected"),
		F.Mode->NativeResolveBombExplosion(Player, bDiedNow));
	TestFalse(TEXT("Rejected explosion reports no death"), bDiedNow);
	TestFalse(TEXT("Rejected explosion leaves the result unresolved"),
		F.Mode->HasResolvedMissionResult());
	TestTrue(TEXT("Rejected explosion leaves health untouched"),
		FMath::IsNearlyEqual(Player->GetCurrentHealth(), 100.0f));
	TestEqual(TEXT("Rejected explosion leaves enemies alive"), NearbyEnemy->NativeDeathCount, 0);

	F.Mode->RestoreMissionState(EMissionState::ReturnToA);
	TestFalse(TEXT("Explosion outside C4Planted is rejected"),
		F.Mode->NativeResolveBombExplosion(Player, bDiedNow));
	TestFalse(TEXT("Return-to-A rejection leaves the result unresolved"),
		F.Mode->HasResolvedMissionResult());

	F.Mode->RestoreMissionState(EMissionState::C4Planted);
	TestTrue(TEXT("Planted C4 resolves the explosion"),
		F.Mode->NativeResolveBombExplosion(Player, bDiedNow));
	TestTrue(TEXT("Explosion reports immediate death"), bDiedNow);
	TestTrue(TEXT("Explosion result is terminal"), F.Mode->HasResolvedMissionResult());
	TestTrue(TEXT("Explosion result is Exploded"),
		F.Mode->GetMissionResult() == EMissionResult::Exploded);
	TestTrue(TEXT("Player died through ApplyDamage"), Player->IsDead());
	TestTrue(TEXT("Lethal damage leaves zero health"),
		FMath::IsNearlyEqual(Player->GetCurrentHealth(), 0.0f));
	TestEqual(TEXT("Nearby enemy receives its death event"), NearbyEnemy->NativeDeathCount, 1);
	TestEqual(TEXT("Far enemy receives its death event"), FarEnemy->NativeDeathCount, 1);
	TestEqual(TEXT("Armor does not prevent environmental death"), FarEnemy->LastNativeHealth, 0.0f);

	ADust2PlayerCharacter* SecondPlayer = F.SpawnPlayer();
	if (!TestNotNull(TEXT("Second player"), SecondPlayer)) return false;
	TestFalse(TEXT("Repeated explosion call is rejected"),
		F.Mode->NativeResolveBombExplosion(SecondPlayer, bDiedNow));
	TestFalse(TEXT("Repeated explosion reports no new death"), bDiedNow);
	TestTrue(TEXT("Repeated call leaves the second player untouched"),
		FMath::IsNearlyEqual(SecondPlayer->GetCurrentHealth(), 100.0f));
	TestEqual(TEXT("Repeated explosion does not send another enemy death event"), FarEnemy->NativeDeathCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionExplosionInvalidAndForeign,
	"Dust2.Mission.BombExplosionInvalidAndForeignWorld", DustDifficultyTestFlags)
bool FDustMissionExplosionInvalidAndForeign::RunTest(const FString&)
{
	FDustMissionRetryWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	F.Mode->RestoreMissionState(EMissionState::C4Planted);

	bool bDiedNow = true;
	TestTrue(TEXT("Null player may still resolve"),
		F.Mode->NativeResolveBombExplosion(nullptr, bDiedNow));
	TestFalse(TEXT("Null player reports no death"), bDiedNow);
	TestTrue(TEXT("Null player resolution is terminal"), F.Mode->HasResolvedMissionResult());
	TestTrue(TEXT("Null player resolution is Exploded"),
		F.Mode->GetMissionResult() == EMissionResult::Exploded);

	ADust2PlayerCharacter* LatePlayer = F.SpawnPlayer();
	if (!TestNotNull(TEXT("Late player"), LatePlayer)) return false;
	TestFalse(TEXT("Already resolved mission rejects a later player"),
		F.Mode->NativeResolveBombExplosion(LatePlayer, bDiedNow));
	TestTrue(TEXT("Later player keeps full health"),
		FMath::IsNearlyEqual(LatePlayer->GetCurrentHealth(), 100.0f));

	F.Mode->NativeResetMissionProgress();
	F.Mode->RestoreMissionState(EMissionState::C4Planted);

	FDustMissionRetryWorld Other;
	if (!TestNotNull(TEXT("Foreign world"), Other.World)) return false;
	ADust2PlayerCharacter* ForeignPlayer = Other.SpawnPlayer();
	if (!TestNotNull(TEXT("Foreign player"), ForeignPlayer)) return false;

	TestFalse(TEXT("Foreign-world player rejects resolution"),
		F.Mode->NativeResolveBombExplosion(ForeignPlayer, bDiedNow));
	TestFalse(TEXT("Foreign rejection leaves the result unresolved"),
		F.Mode->HasResolvedMissionResult());
	TestFalse(TEXT("Foreign rejection reports no death"), bDiedNow);
	TestTrue(TEXT("Foreign rejection leaves its player untouched"),
		FMath::IsNearlyEqual(ForeignPlayer->GetCurrentHealth(), 100.0f));
	TestTrue(TEXT("Foreign rejection keeps C4Planted"),
		F.Mode->GetMissionState() == EMissionState::C4Planted);

	ADust2PlayerCharacter* LocalPlayer = F.SpawnPlayer();
	if (!TestNotNull(TEXT("Local player"), LocalPlayer)) return false;
	TestTrue(TEXT("Local player resolves after the foreign rejection"),
		F.Mode->NativeResolveBombExplosion(LocalPlayer, bDiedNow));
	TestTrue(TEXT("Local player dies"), LocalPlayer->IsDead());
	TestTrue(TEXT("Foreign player remains untouched"),
		FMath::IsNearlyEqual(ForeignPlayer->GetCurrentHealth(), 100.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionExplosionAfterVictory,
	"Dust2.Mission.BombExplosionAfterVictoryDoesNotDamage", DustDifficultyTestFlags)
bool FDustMissionExplosionAfterVictory::RunTest(const FString&)
{
	FDustMissionRetryWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	F.Mode->RestoreMissionState(EMissionState::C4Planted);
	TestTrue(TEXT("Victory accepted"), F.Mode->TryResolveMissionResult(EMissionResult::Defused));

	ADust2PlayerCharacter* Player = F.SpawnPlayer();
	if (!TestNotNull(TEXT("Player"), Player)) return false;

	bool bDiedNow = true;
	TestFalse(TEXT("Explosion after victory is rejected"),
		F.Mode->NativeResolveBombExplosion(Player, bDiedNow));
	TestFalse(TEXT("Post-victory explosion reports no death"), bDiedNow);
	TestTrue(TEXT("Victory result is preserved"),
		F.Mode->GetMissionResult() == EMissionResult::Defused);
	TestTrue(TEXT("Post-victory player keeps full health"),
		FMath::IsNearlyEqual(Player->GetCurrentHealth(), 100.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustMissionExplosionInvulnerable,
	"Dust2.Mission.BombExplosionBypassesInvulnerableFlag", DustDifficultyTestFlags)
bool FDustMissionExplosionInvulnerable::RunTest(const FString&)
{
	FDustMissionRetryWorld F;
	if (!TestNotNull(TEXT("GameMode"), F.Mode)) return false;
	F.Mode->RestoreMissionState(EMissionState::C4Planted);

	ADust2PlayerCharacter* Player = F.SpawnPlayer();
	if (!TestNotNull(TEXT("Player"), Player)) return false;
	Player->bInvulnerable = true;

	bool bDiedNow = false;
	TestTrue(TEXT("Explosion resolves despite the debug flag"),
		F.Mode->NativeResolveBombExplosion(Player, bDiedNow));
	TestTrue(TEXT("Explosion reports death"), bDiedNow);
	TestTrue(TEXT("Explosion kills through ApplyDamage"), Player->IsDead());
	TestTrue(TEXT("Debug invulnerability flag is restored"), Player->bInvulnerable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustTutorialSessionSeen,
	"Dust2.Tutorial.SessionSeenAndReset", DustDifficultyTestFlags)
bool FDustTutorialSessionSeen::RunTest(const FString&)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	auto* Session = NewObject<UDustGameSessionSubsystem>(Instance);
	const FName WeaponKey(TEXT("Tutorial_Viper"));
	TestFalse(TEXT("New run has not seen the weapon"), Session->HasSeenTutorial(WeaponKey));
	Session->MarkTutorialSeen(WeaponKey);
	Session->MarkTutorialSeen(NAME_None);
	TestTrue(TEXT("Weapon recorded for the current run"), Session->HasSeenTutorial(WeaponKey));
	TestFalse(TEXT("Empty tutorial id ignored"), Session->HasSeenTutorial(NAME_None));
	Session->EndSessionForDifficultySelection();
	TestFalse(TEXT("Complete restart clears the record"), Session->HasSeenTutorial(WeaponKey));
	return true;
}

#endif
