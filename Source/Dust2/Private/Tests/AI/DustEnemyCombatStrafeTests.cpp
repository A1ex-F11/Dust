#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AIController.h"
#include "Components/SceneComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "Player/Dust2PlayerCharacter.h"
#include "AI/DustEnemyCombatAdapterComponent.h"
#include "AI/DustEnemyCombatProfile.h"
#include "AI/DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Weapons/WeaponDefinitionData.h"
#include "../../Weapons/WeaponBallisticsData.h"
#include "Weapons/WeaponRecoilData.h"
#include "Weapons/WeaponRuntime.h"

#include <limits>

/**
 * Candidate-only, bare-world coverage for the Create Space lateral-strafe extension.
 *
 * The world has a NavigationSystem but no navmesh, so admission/geometry/mode ownership are
 * exercised without faking a successful path. Real navmesh projection, path length, animation
 * and PIE acceptance remain J's runtime checks.
 */
struct FDustEnemyCombatStrafeTestAccess
{
	static void Bind(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
		APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon, AActor* Target)
	{
		Adapter->Controller = Controller;
		Adapter->BoundPawn = Pawn;
		Adapter->Tree = Tree;
		Adapter->WeaponComponent = Weapon;
		Adapter->Target = Target;
		Adapter->PathFollowing = Controller ? Controller->GetPathFollowingComponent() : nullptr;
		Adapter->bInitialized = true;
		Adapter->bStopped = false;
		Adapter->bConfirmedTarget = true;
		Adapter->bSightCurrentlySensed = true;
		Adapter->bInLossGrace = false;
		Adapter->bDeferredReloadSightLoss = false;
		Adapter->LastKnownLocation = Target ? Target->GetActorLocation() : FVector::ZeroVector;
		Adapter->LastConfirmedTime = Adapter->GetWorld() ? Adapter->GetWorld()->GetTimeSeconds() : 0.0f;
	}
	static bool Can(UDustEnemyCombatAdapterComponent* Adapter, bool bConfirmed)
	{
		return Adapter->CanCreateSpace(bConfirmed);
	}
	static bool ShouldStrafe(UDustEnemyCombatAdapterComponent* Adapter, bool bConfirmed)
	{
		return Adapter->ShouldCombatStrafe(bConfirmed);
	}
	static bool IsStrafe(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->bSpacingIsCombatStrafe;
	}
	static bool IsActive(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->IsCreateSpaceActive();
	}
	static void SetActiveFlag(UDustEnemyCombatAdapterComponent* Adapter, bool bActive)
	{
		Adapter->bCreateSpaceActive = bActive;
	}
	static float AllowedAt(const UDustEnemyCombatAdapterComponent* Adapter)
	{
		return Adapter->NextSpacingAllowedAt;
	}
	static void ResetCooldown(UDustEnemyCombatAdapterComponent* Adapter)
	{
		Adapter->NextSpacingAllowedAt = 0.0f;
	}
	static void ApplyCooldown(UDustEnemyCombatAdapterComponent* Adapter, bool bSpread, bool bStrafe)
	{
		Adapter->ApplySpacingCooldown(bSpread, bStrafe);
	}
	static bool ApplySpeed(UDustEnemyCombatAdapterComponent* Adapter, bool bSpread, bool bStrafe)
	{
		return Adapter->ApplySpacingMovementSpeed(bSpread, bStrafe);
	}
	static void RestoreSpeed(UDustEnemyCombatAdapterComponent* Adapter)
	{
		Adapter->RestoreSpacingMovementSpeed();
	}
	static FVector Direction(const FVector& PawnLocation, const FVector& TargetLocation, bool bRight)
	{
		return UDustEnemyCombatAdapterComponent::ComputeCombatStrafeDirection(
			PawnLocation, TargetLocation, bRight);
	}
	static void NormalizeRange(float Min, float Max, float& OutMin, float& OutMax)
	{
		UDustEnemyCombatAdapterComponent::NormalizeCombatStrafeDistanceRange(
			Min, Max, OutMin, OutMax);
	}
	static void Activate(UDustEnemyCombatAdapterComponent* Adapter, uint64 Token)
	{
		Adapter->ActiveSpacingToken = Token;
		Adapter->SpacingGeneration = Token;
		Adapter->SpacingMoveId = FAIRequestID::InvalidRequest;
		Adapter->ImmediateSpacingMoveId = FAIRequestID::InvalidRequest;
		Adapter->SpacingStatus = EDustCreateSpaceStatus::Running;
		Adapter->bCreateSpaceActive = true;
		Adapter->bIssuingSpacingMove = false;
		Adapter->bSpacingIsCombatSpread = false;
		Adapter->bSpacingIsCombatStrafe = true;
		Adapter->SpacingTarget = Adapter->Target;
		Adapter->SpacingElapsedSeconds = 0.0f;
	}
	static void SetNewerToken(UDustEnemyCombatAdapterComponent* Adapter, uint64 Token)
	{
		Adapter->ActiveSpacingToken = Token;
		Adapter->SpacingGeneration = Token;
	}
	static EDustCreateSpaceStatus Update(UDustEnemyCombatAdapterComponent* Adapter, uint64 Token)
	{
		return Adapter->UpdateCreateSpace(Token, 0.1f);
	}
};

namespace
{
	struct FCombatStrafeWorld
	{
		UWorld* World = nullptr;
		AAIController* Controller = nullptr;
		ACharacter* Pawn = nullptr;
		ADust2PlayerCharacter* Target = nullptr;
		UStateTreeAIComponent* Tree = nullptr;
		UDustEnemyWeaponComponent* Weapon = nullptr;
		UDustEnemyCombatAdapterComponent* Adapter = nullptr;
		UCharacterMovementComponent* Movement = nullptr;

		bool Build()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.CreateNavigation(true)
				.CreateAISystem(true)
				.ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return false;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			Controller = World->SpawnActor<AAIController>();
			Pawn = World->SpawnActor<ACharacter>();
			Target = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Controller || !Pawn || !Target) return false;
			Controller->Possess(Pawn);
			Pawn->SetActorLocation(FVector(0, 0, 100));
			Target->SetActorLocation(FVector(600, 0, 100));
			Movement = Pawn->GetCharacterMovement();
			if (!Movement) return false;
			Movement->SetMovementMode(MOVE_None);
			Movement->GravityScale = 0.0f;
			Movement->MaxWalkSpeed = 420.0f;
			Target->GetCharacterMovement()->SetMovementMode(MOVE_None);

			Tree = NewObject<UStateTreeAIComponent>(Controller);
			Tree->SetStartLogicAutomatically(false);
			Controller->AddInstanceComponent(Tree);
			Tree->RegisterComponent();

			Weapon = NewObject<UDustEnemyWeaponComponent>(Pawn);
			Pawn->AddInstanceComponent(Weapon);
			Weapon->RegisterComponent();
			Weapon->SetComponentTickEnabled(false);
			Weapon->PrimaryComponentTick.bCanEverTick = false;
			Weapon->CombatProfile = NewObject<UDustEnemyCombatProfile>(Weapon);
			Weapon->CombatProfile->bEnableCombatSpread = false;
			Weapon->CombatProfile->bEnableCombatStrafe = true;
			Weapon->CombatProfile->CreateSpaceTriggerDistance = 300.0f;
			Weapon->CombatProfile->CreateSpaceCooldownSeconds = 3.0f;
			Weapon->CombatProfile->CombatStrafeMinDistance = 100.0f;
			Weapon->CombatProfile->CombatStrafeMaxDistance = 200.0f;
			Weapon->CombatProfile->CombatStrafeMoveSpeed = 200.0f;
			Weapon->CombatProfile->CombatStrafePauseMinSeconds = 1.0f;
			Weapon->CombatProfile->CombatStrafePauseMaxSeconds = 2.0f;

			UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>(Weapon);
			Definition->BallisticsData = NewObject<UWeaponBallisticsData>(Definition);
			Definition->BallisticsData->TraceDistance = 2000.0f;
			Definition->MagazineCapacity = 30;
			Definition->InitialMagazineAmmo = 30;
			Definition->InitialReserveAmmo = 30;
			Definition->RecoilData = NewObject<UWeaponRecoilData>(Definition);
			Definition->RecoilData->bUseFixedPattern = false;
			Definition->RecoilData->bUseIndependentShotRecoil = false;
			Weapon->WeaponDefinition = Definition;
			USceneComponent* Muzzle = NewObject<USceneComponent>(Pawn);
			Muzzle->SetupAttachment(Pawn->GetRootComponent());
			Pawn->AddInstanceComponent(Muzzle);
			Muzzle->RegisterComponent();
			Muzzle->SetRelativeLocation(FVector(0, 0, 50));
			Weapon->SetMuzzleSource(Muzzle, NAME_None);
			if (!Weapon->InitializeWeapon()) return false;

			Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
			Controller->AddInstanceComponent(Adapter);
			Adapter->RegisterComponent();
			FDustEnemyCombatStrafeTestAccess::Bind(Adapter, Controller, Pawn, Tree, Weapon, Target);
			return Adapter->IsInitialized();
		}

		~FCombatStrafeWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatStrafeGeometryTest,
	"Dust2.CombatStrafe.DirectionalGeometryAndFiniteRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustCombatStrafeGeometryTest::RunTest(const FString& Parameters)
{
	const FVector Pawn(0, 0, 100);
	const FVector Target(100, 0, 100);
	const FVector Right = FDustEnemyCombatStrafeTestAccess::Direction(Pawn, Target, true);
	const FVector Left = FDustEnemyCombatStrafeTestAccess::Direction(Pawn, Target, false);
	TestTrue(TEXT("Facing +X selects the pawn's right on +Y"), Right.Equals(FVector(0, 1, 0), 0.01f));
	TestTrue(TEXT("The opposite side is exact -Y"), Left.Equals(FVector(0, -1, 0), 0.01f));
	TestTrue(TEXT("Both sides are horizontal and opposite"), (Right + Left).IsNearlyZero(0.01f)
		&& FMath::IsNearlyZero(FVector::DotProduct(Right, Target - Pawn), 0.01f));
	TestTrue(TEXT("The direction helper is finite and unit length"),
		FMath::IsFinite(Right.SizeSquared()) && FMath::IsNearlyEqual(Right.SizeSquared(), 1.0f, 0.001f));

	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();
	float Min = 0.0f;
	float Max = 0.0f;
	FDustEnemyCombatStrafeTestAccess::NormalizeRange(Nan, Inf, Min, Max);
	TestEqual(TEXT("Non-finite minimum falls back to 100"), Min, 100.0f);
	TestEqual(TEXT("Non-finite maximum falls back to 200"), Max, 200.0f);
	FDustEnemyCombatStrafeTestAccess::NormalizeRange(250.0f, 120.0f, Min, Max);
	TestEqual(TEXT("Inverted values normalize to the lower bound"), Min, 120.0f);
	TestEqual(TEXT("Inverted values normalize to the upper bound"), Max, 250.0f);
	FDustEnemyCombatStrafeTestAccess::NormalizeRange(-10.0f, 50.0f, Min, Max);
	TestTrue(TEXT("Negative values cannot produce a reversed/zero range"),
		Min > 0.0f && Min <= Max && FMath::IsFinite(Min) && FMath::IsFinite(Max));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatStrafeAdmissionTest,
	"Dust2.CombatStrafe.AdmissionPriorityReloadRangeAndCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustCombatStrafeAdmissionTest::RunTest(const FString& Parameters)
{
	FCombatStrafeWorld F;
	if (!TestTrue(TEXT("strafe fixture initialized"), F.Build())) return false;
	if (!TestNotNull(TEXT("strafe admission world"), F.World)
		|| !TestNotNull(TEXT("strafe admission adapter"), F.Adapter)
		|| !TestNotNull(TEXT("strafe admission weapon"), F.Weapon)
		|| !TestNotNull(TEXT("strafe admission target"), F.Target)) return false;

	TestTrue(TEXT("A confirmed target outside the close trigger and inside range admits lateral strafe"),
		FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));
	TestTrue(TEXT("Lateral eligibility is separate from the passed confirmation flag"),
		FDustEnemyCombatStrafeTestAccess::ShouldStrafe(F.Adapter, true));
	TestFalse(TEXT("Unconfirmed callers cannot use the lateral read path"),
		FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, false));
	TestFalse(TEXT("Unconfirmed lateral admission is rejected"),
		FDustEnemyCombatStrafeTestAccess::ShouldStrafe(F.Adapter, false));

	F.Weapon->CombatProfile->bEnableCombatStrafe = false;
	TestFalse(TEXT("The lateral enable flag disables the extension"),
		FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));
	F.Weapon->CombatProfile->bEnableCombatStrafe = true;

	F.Weapon->CombatProfile->CreateSpaceTriggerDistance = 700.0f;
	TestTrue(TEXT("Close retreat remains admitted"), FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));
	TestFalse(TEXT("Close retreat wins over lateral strafe"),
		FDustEnemyCombatStrafeTestAccess::ShouldStrafe(F.Adapter, true));
	F.Weapon->CombatProfile->CreateSpaceTriggerDistance = 300.0f;

	F.Target->SetActorLocation(FVector(1300, 0, 100));
	TestFalse(TEXT("Outside the existing weapon range no lateral strafe is admitted"),
		FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));
	TestFalse(TEXT("The range check rejects the lateral helper too"),
		FDustEnemyCombatStrafeTestAccess::ShouldStrafe(F.Adapter, true));
	F.Target->SetActorLocation(FVector(600, 0, 100));

	TestTrue(TEXT("Ammo is available before reload"), F.Weapon->GetWeaponRuntime()->GetCurrentAmmo() > 0);
	TestTrue(TEXT("Commit one round for the reload fixture"), F.Weapon->GetWeaponRuntime()->TryCommitFire());
	TestTrue(TEXT("Start the actual reload transaction"), F.Weapon->GetWeaponRuntime()->TryStartReload());
	TestFalse(TEXT("Reload excludes lateral strafe"), FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));
	F.Weapon->GetWeaponRuntime()->CancelReload();
	TestTrue(TEXT("Admission resumes after reload cancel"), FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));

	FDustEnemyCombatStrafeTestAccess::SetActiveFlag(F.Adapter, true);
	TestFalse(TEXT("An active Create Space request blocks another lateral strafe"),
		FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));
	FDustEnemyCombatStrafeTestAccess::SetActiveFlag(F.Adapter, false);

	const float Before = F.World->GetTimeSeconds();
	FDustEnemyCombatStrafeTestAccess::ApplyCooldown(F.Adapter, false, true);
	const float Cooldown = FDustEnemyCombatStrafeTestAccess::AllowedAt(F.Adapter) - Before;
	TestTrue(TEXT("A strafe cooldown uses the sampled 1-2s pause"),
		Cooldown >= 0.99f && Cooldown <= 2.01f);
	TestFalse(TEXT("The shared cooldown blocks lateral re-entry"),
		FDustEnemyCombatStrafeTestAccess::Can(F.Adapter, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatStrafeOwnershipTest,
	"Dust2.CombatStrafe.ModeSpeedRestorationPauseAndKeepEngagement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustCombatStrafeOwnershipTest::RunTest(const FString& Parameters)
{
	FCombatStrafeWorld F;
	if (!TestTrue(TEXT("strafe fixture initialized"), F.Build())) return false;
	if (!TestNotNull(TEXT("strafe ownership world"), F.World)
		|| !TestNotNull(TEXT("strafe ownership adapter"), F.Adapter)
		|| !TestNotNull(TEXT("strafe ownership movement"), F.Movement)
		|| !TestNotNull(TEXT("strafe ownership weapon"), F.Weapon)) return false;

	F.Weapon->CombatProfile->CreateSpaceMoveSpeed = 600.0f;
	F.Weapon->CombatProfile->CombatSpreadMoveSpeed = 180.0f;
	F.Weapon->CombatProfile->CombatStrafeMoveSpeed = 200.0f;
	TestTrue(TEXT("Strafe speed applies"), FDustEnemyCombatStrafeTestAccess::ApplySpeed(F.Adapter, false, true));
	TestEqual(TEXT("Strafe uses its own 200 speed"), F.Movement->MaxWalkSpeed, 200.0f);
	FDustEnemyCombatStrafeTestAccess::RestoreSpeed(F.Adapter);
	TestEqual(TEXT("Strafe restores the original 420 speed"), F.Movement->MaxWalkSpeed, 420.0f);

	TestTrue(TEXT("Spread speed applies"), FDustEnemyCombatStrafeTestAccess::ApplySpeed(F.Adapter, true, false));
	TestEqual(TEXT("Spread keeps its 180 default"), F.Movement->MaxWalkSpeed, 180.0f);
	FDustEnemyCombatStrafeTestAccess::RestoreSpeed(F.Adapter);
	TestTrue(TEXT("Retreat speed applies"), FDustEnemyCombatStrafeTestAccess::ApplySpeed(F.Adapter, false, false));
	TestEqual(TEXT("Retreat keeps its configured speed"), F.Movement->MaxWalkSpeed, 600.0f);
	FDustEnemyCombatStrafeTestAccess::RestoreSpeed(F.Adapter);

	TestTrue(TEXT("Strafe ownership applies before the external write"),
		FDustEnemyCombatStrafeTestAccess::ApplySpeed(F.Adapter, false, true));
	F.Movement->MaxWalkSpeed = 360.0f;
	FDustEnemyCombatStrafeTestAccess::RestoreSpeed(F.Adapter);
	TestEqual(TEXT("External MaxWalkSpeed survives strafe restoration"), F.Movement->MaxWalkSpeed, 360.0f);
	F.Movement->MaxWalkSpeed = 420.0f;

	TestTrue(TEXT("Standing engagement starts for the keep-engagement check"),
		F.Weapon->StartEngagement(F.Target));
	TestTrue(TEXT("The weapon faces the strafe target"), F.Weapon->IsEngaging()
		&& F.Weapon->GetEngagementTarget() == F.Target);
	FDustEnemyCombatStrafeTestAccess::Activate(F.Adapter, 5);
	TestTrue(TEXT("The strafe owns temporary speed"), FDustEnemyCombatStrafeTestAccess::ApplySpeed(F.Adapter, false, true));
	const float BeforeComplete = F.World->GetTimeSeconds();
	F.Adapter->CompleteCreateSpace(5);
	TestFalse(TEXT("Completion clears the strafe mode flag"), FDustEnemyCombatStrafeTestAccess::IsStrafe(F.Adapter));
	TestFalse(TEXT("Completion releases the request"), FDustEnemyCombatStrafeTestAccess::IsActive(F.Adapter));
	TestEqual(TEXT("Completion restores the pre-strafe speed"), F.Movement->MaxWalkSpeed, 420.0f);
	TestTrue(TEXT("Completion keeps the existing engagement/volley"), F.Weapon->IsEngaging()
		&& F.Weapon->GetEngagementTarget() == F.Target);
	const float Pause = FDustEnemyCombatStrafeTestAccess::AllowedAt(F.Adapter) - BeforeComplete;
	TestTrue(TEXT("Completion samples the strafe pause exactly into the shared cooldown"),
		Pause >= 0.99f && Pause <= 2.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatStrafeNoNavTest,
	"Dust2.CombatStrafe.NoSafeDestinationUsesStrafePause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustCombatStrafeNoNavTest::RunTest(const FString& Parameters)
{
	FCombatStrafeWorld F;
	if (!TestTrue(TEXT("strafe fixture initialized"), F.Build())) return false;
	if (!TestNotNull(TEXT("strafe no-nav world"), F.World)
		|| !TestNotNull(TEXT("strafe no-nav adapter"), F.Adapter)
		|| !TestNotNull(TEXT("strafe no-nav weapon"), F.Weapon)) return false;

	F.Weapon->CombatProfile->CreateSpaceCooldownSeconds = 7.0f;
	F.Weapon->CombatProfile->CombatStrafePauseMinSeconds = 1.0f;
	F.Weapon->CombatProfile->CombatStrafePauseMaxSeconds = 2.0f;

	// Close retreat has priority even when the lateral profile is enabled.
	F.Target->SetActorLocation(FVector(200, 0, 100));
	TestTrue(TEXT("Inside the close trigger the retreat gate is still admitted"),
		F.Adapter->CanCreateSpace());
	TestFalse(TEXT("The lateral helper yields to close retreat"),
		FDustEnemyCombatStrafeTestAccess::ShouldStrafe(F.Adapter, true));
	const float BeforeRetreat = F.World->GetTimeSeconds();
	TestEqual(TEXT("A failed close retreat owns no token"), F.Adapter->BeginCreateSpace(), uint64(0));
	const float RetreatCooldown = FDustEnemyCombatStrafeTestAccess::AllowedAt(F.Adapter) - BeforeRetreat;
	TestTrue(TEXT("The close fallback uses the retreat cooldown, not the strafe pause"),
		RetreatCooldown >= 6.99f && RetreatCooldown <= 7.01f);

	F.Target->SetActorLocation(FVector(600, 0, 100));
	FDustEnemyCombatStrafeTestAccess::ResetCooldown(F.Adapter);
	TestTrue(TEXT("The no-navmesh world still admits lateral eligibility"),
		F.Adapter->CanCreateSpace());
	const float BeforeStrafe = F.World->GetTimeSeconds();
	TestEqual(TEXT("No safe lateral destination fails before owning a token"),
		F.Adapter->BeginCreateSpace(), uint64(0));
	TestFalse(TEXT("A failed lateral selection owns no request"), F.Adapter->IsCreateSpaceActive());
	const float Cooldown = FDustEnemyCombatStrafeTestAccess::AllowedAt(F.Adapter) - BeforeStrafe;
	TestTrue(TEXT("A failed lateral selection uses the sampled strafe pause, not the retreat cooldown"),
		Cooldown >= 0.99f && Cooldown <= 2.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatStrafeStaleTokenTest,
	"Dust2.CombatStrafe.StaleTokenCannotReleaseNewerRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDustCombatStrafeStaleTokenTest::RunTest(const FString& Parameters)
{
	FCombatStrafeWorld F;
	if (!TestTrue(TEXT("strafe fixture initialized"), F.Build())) return false;
	if (!TestNotNull(TEXT("strafe stale-token world"), F.World)
		|| !TestNotNull(TEXT("strafe stale-token adapter"), F.Adapter)
		|| !TestNotNull(TEXT("strafe stale-token weapon"), F.Weapon)) return false;

	FDustEnemyCombatStrafeTestAccess::Activate(F.Adapter, 11);
	FDustEnemyCombatStrafeTestAccess::SetNewerToken(F.Adapter, 22);
	TestTrue(TEXT("The old token cannot update the newer request"),
		FDustEnemyCombatStrafeTestAccess::Update(F.Adapter, 11) == EDustCreateSpaceStatus::Failed);
	TestTrue(TEXT("The newer request survives the stale update"),
		FDustEnemyCombatStrafeTestAccess::IsActive(F.Adapter));
	F.Adapter->CancelCreateSpace(11);
	TestTrue(TEXT("The old token cannot cancel the newer request"),
		FDustEnemyCombatStrafeTestAccess::IsActive(F.Adapter));
	F.Adapter->CancelCreateSpace(22);
	TestFalse(TEXT("The current token still releases its own request"),
		FDustEnemyCombatStrafeTestAccess::IsActive(F.Adapter));
	TestFalse(TEXT("Release clears the strafe mode flag"), FDustEnemyCombatStrafeTestAccess::IsStrafe(F.Adapter));

	// The next close-retreat failure must use the normal cooldown, proving no strafe flag leaked.
	F.Target->SetActorLocation(FVector(200, 0, 100));
	F.Weapon->CombatProfile->CreateSpaceCooldownSeconds = 5.0f;
	FDustEnemyCombatStrafeTestAccess::ResetCooldown(F.Adapter);
	const float BeforeRetreat = F.World->GetTimeSeconds();
	TestEqual(TEXT("The post-strafe close retreat fails without a token"),
		F.Adapter->BeginCreateSpace(), uint64(0));
	const float RetreatCooldown = FDustEnemyCombatStrafeTestAccess::AllowedAt(F.Adapter) - BeforeRetreat;
	TestTrue(TEXT("The post-strafe retreat uses its own cooldown"),
		RetreatCooldown >= 4.99f && RetreatCooldown <= 5.01f);
	return true;
}

#endif
