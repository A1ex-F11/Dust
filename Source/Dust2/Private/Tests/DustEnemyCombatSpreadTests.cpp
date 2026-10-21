#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustEnemyCombatAdapterComponent.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyVisibility.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"
#include "WeaponRecoilData.h"
#include "WeaponRuntime.h"

struct FDustEnemyCombatSpreadTestAccess
{
 static void Bind(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
  APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon, AActor* Target)
 {
  Adapter->Controller=Controller; Adapter->BoundPawn=Pawn; Adapter->Tree=Tree;
  Adapter->WeaponComponent=Weapon; Adapter->Target=Target; Adapter->bInitialized=true;
  Adapter->bConfirmedTarget=true; Adapter->bSightCurrentlySensed=true;
  Adapter->PathFollowing=Controller->GetPathFollowingComponent();
 }
 static bool NeedsSpread(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->ShouldCombatSpread(); }
 static void Cooldown(UDustEnemyCombatAdapterComponent* Adapter,bool Crowd) { Adapter->ApplySpacingCooldown(Crowd); }
 static float AllowedAt(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->NextSpacingAllowedAt; }
 static bool Speed(UDustEnemyCombatAdapterComponent* Adapter,bool Crowd) { return Adapter->ApplySpacingMovementSpeed(Crowd); }
 static void Restore(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->RestoreSpacingMovementSpeed(); }
 static void SetActive(UDustEnemyCombatAdapterComponent* Adapter,bool Active) { Adapter->bCreateSpaceActive=Active; }
 static void ClearTarget(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->Target.Reset(); }
};

namespace
{
	struct FCombatSpreadWorld
	{
		UWorld* World = nullptr;
		AAIController* Controller = nullptr;
		ACharacter* Observer = nullptr;
		ACharacter* Ally = nullptr;
		ADust2PlayerCharacter* Target = nullptr;
		UDustEnemyWeaponComponent* Weapon = nullptr;
		UDustEnemyCombatAdapterComponent* Adapter = nullptr;
		FCombatSpreadWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(true).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			Controller = World->SpawnActor<AAIController>();
			Observer = World->SpawnActor<ACharacter>();
			Ally = World->SpawnActor<ACharacter>();
			Target = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Controller || !Observer || !Ally || !Target) return;
			Observer->SetActorLocation(FVector(0, 0, 100));
			Ally->SetActorLocation(FVector(300, 0, 100));
			Target->SetActorLocation(FVector(600, 0, 100));
			for (ACharacter* Pawn : {Observer, Ally, static_cast<ACharacter*>(Target)})
				Pawn->GetCharacterMovement()->SetMovementMode(MOVE_None);
			Controller->Possess(Observer);
			Weapon = NewObject<UDustEnemyWeaponComponent>(Observer);
			Observer->AddInstanceComponent(Weapon); Weapon->RegisterComponent(); Weapon->SetComponentTickEnabled(false);
			Weapon->CombatProfile = NewObject<UDustEnemyCombatProfile>();
			Weapon->CombatProfile->ReactionDelay = 0;
			Weapon->CombatProfile->CreateSpaceTriggerDistance = 0;
			Weapon->CombatProfile->SettledAimSeconds = 0;
			UDustEnemyWeaponComponent* AllyWeapon = NewObject<UDustEnemyWeaponComponent>(Ally);
			Ally->AddInstanceComponent(AllyWeapon); AllyWeapon->RegisterComponent(); AllyWeapon->SetComponentTickEnabled(false);
			Box(Ally, FVector::ZeroVector);
			UStateTreeAIComponent* Tree = NewObject<UStateTreeAIComponent>(Controller);
			Tree->SetStartLogicAutomatically(false); Controller->AddInstanceComponent(Tree); Tree->RegisterComponent();
			Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
			Controller->AddInstanceComponent(Adapter); Adapter->RegisterComponent();
			FDustEnemyCombatSpreadTestAccess::Bind(Adapter, Controller, Observer, Tree, Weapon, Target);
		}
		~FCombatSpreadWorld() { if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); } }
		UBoxComponent* Box(AActor* Owner, const FVector& Relative)
		{
			UBoxComponent* Collision = NewObject<UBoxComponent>(Owner);
			Collision->SetBoxExtent(FVector(25, 100, 150));
			Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Collision->SetCollisionResponseToAllChannels(ECR_Block);
			if (Owner->GetRootComponent()) Collision->SetupAttachment(Owner->GetRootComponent());
			else Owner->SetRootComponent(Collision);
			Owner->AddInstanceComponent(Collision); Collision->RegisterComponent(); Collision->SetRelativeLocation(Relative);
			return Collision;
		}
		bool PrepareWeapon()
		{
			UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
			Definition->BallisticsData = NewObject<UWeaponBallisticsData>(Definition);
			Definition->BallisticsData->TraceDistance = 2000.0f;
			Definition->MagazineCapacity = 30; Definition->InitialMagazineAmmo = 30; Definition->InitialReserveAmmo = 30;
			Definition->RecoilData = NewObject<UWeaponRecoilData>(Definition);
			Definition->RecoilData->bUseFixedPattern = false;
			Definition->RecoilData->bUseIndependentShotRecoil = false;
			Weapon->WeaponDefinition = Definition;
			USceneComponent* Muzzle = NewObject<USceneComponent>(Weapon->GetOwner());
			Muzzle->SetupAttachment(Weapon->GetOwner()->GetRootComponent());
			Weapon->GetOwner()->AddInstanceComponent(Muzzle); Muzzle->RegisterComponent();
			Muzzle->SetRelativeLocation(FVector(0, 0, 50));
			Weapon->SetMuzzleSource(Muzzle, NAME_None);
			return Weapon->InitializeWeapon();
		}
		bool UseHigherIdPawn()
		{
			Ally->SetActorLocation(FVector(100,0,100));
			Controller->Possess(Ally);
			Weapon = Ally->FindComponentByClass<UDustEnemyWeaponComponent>();
			Weapon->CombatProfile = NewObject<UDustEnemyCombatProfile>();
			Weapon->CombatProfile->CreateSpaceTriggerDistance = 0.0f;
			FDustEnemyCombatSpreadTestAccess::Bind(Adapter,Controller,Ally,
				Controller->FindComponentByClass<UStateTreeAIComponent>(),Weapon,Target);
			return PrepareWeapon();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatSpreadAdmissionTest,
	"Dust2.CombatSpread.StablePriorityAndExitSafeAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustCombatSpreadAdmissionTest::RunTest(const FString& Parameters)
{
	FCombatSpreadWorld F;
	if (!F.World || !F.Adapter || !F.Weapon || !F.Target || !F.Ally || !F.Observer) return false;
	if (!TestTrue(TEXT("valid runtime"), F.PrepareWeapon())) return false;
	TestFalse(TEXT("distant teammate does not trigger"), F.Adapter->CanCreateSpace());
	F.Ally->SetActorLocation(FVector(100,0,100));
	TestFalse(TEXT("lower id keeps position instead of symmetric yielding"), F.Adapter->CanCreateSpace());
	if (!TestTrue(TEXT("higher id source initialized"), F.UseHigherIdPawn())) return false;
	TestTrue(TEXT("higher id yields without weapon engagement"), F.Adapter->CanCreateSpace());
	F.Weapon->StopEngagement();
	TestTrue(TEXT("Engage Exit cannot invalidate stable admission"), F.Adapter->CanCreateSpace());
	F.Weapon->CombatProfile->bEnableCombatSpread = false;
	TestFalse(TEXT("opt-out retains standing combat"), F.Adapter->CanCreateSpace());
	F.Weapon->CombatProfile->bEnableCombatSpread = true;
	FDustEnemyCombatSpreadTestAccess::SetActive(F.Adapter,true);
	TestFalse(TEXT("existing request cannot be overlapped"), F.Adapter->CanCreateSpace());
	FDustEnemyCombatSpreadTestAccess::SetActive(F.Adapter,false);
	TestEqual(TEXT("admission query did not consume ammo"), F.Weapon->GetWeaponRuntime()->GetCurrentAmmo(),30);
	TestTrue(TEXT("consume one real round"), F.Weapon->GetWeaponRuntime()->TryCommitFire());
	TestTrue(TEXT("actual reload transaction starts"), F.Weapon->GetWeaponRuntime()->TryStartReload());
	TestFalse(TEXT("reload owns pawn and blocks a new spread"), F.Adapter->CanCreateSpace());
	F.Weapon->GetWeaponRuntime()->CancelReload();
	TestTrue(TEXT("admission resumes after reload cancel"), F.Adapter->CanCreateSpace());
	FDustEnemyCombatSpreadTestAccess::Cooldown(F.Adapter,true);
	TestFalse(TEXT("mode cooldown suppresses reentry"), F.Adapter->CanCreateSpace());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatSpreadNoNavTest,
	"Dust2.CombatSpread.NoDestinationLeavesCombatAndAppliesCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustCombatSpreadNoNavTest::RunTest(const FString& Parameters)
{
	FCombatSpreadWorld F;
	if (!F.World || !F.Adapter || !F.Ally || !F.Observer || !F.Target) return false;
	if (!TestTrue(TEXT("higher id setup"), F.UseHigherIdPawn())) return false;
	F.Weapon->CombatProfile->ReactionDelay = 0;
	TestTrue(TEXT("standing engagement can start"), F.Weapon->StartEngagement(F.Target));
	TestTrue(TEXT("crowded admission"), F.Adapter->CanCreateSpace());
	TestEqual(TEXT("real no-nav world cannot issue a move"), F.Adapter->BeginCreateSpace(),uint64(0));
	TestTrue(TEXT("failed selection retains existing engagement"), F.Weapon->IsEngaging());
	TestEqual(TEXT("failed spread applies its own cooldown"),FDustEnemyCombatSpreadTestAccess::AllowedAt(F.Adapter),static_cast<float>(F.World->GetTimeSeconds() + 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatSpreadFilteringTest,
	"Dust2.CombatSpread.DestroyedAndNonFriendlyDoNotTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustCombatSpreadFilteringTest::RunTest(const FString& Parameters)
{
	FCombatSpreadWorld F;
	if (!F.World || !F.Adapter || !F.Ally || !F.Observer || !F.Target) return false;
	if (!TestTrue(TEXT("higher id setup"), F.UseHigherIdPawn())) return false;
	TestTrue(TEXT("near live friendly triggers"), FDustEnemyCombatSpreadTestAccess::NeedsSpread(F.Adapter));
	F.Observer->Destroy();
	TestFalse(TEXT("destroyed teammate excluded"), FDustEnemyCombatSpreadTestAccess::NeedsSpread(F.Adapter));
	ACharacter* Other = F.World->SpawnActor<ACharacter>();
	if (!Other) return false;
	Other->SetActorLocation(FVector(0,0,100));
	TestFalse(TEXT("non-enemy-weapon pawn does not trigger"), FDustEnemyCombatSpreadTestAccess::NeedsSpread(F.Adapter));
	FDustEnemyCombatSpreadTestAccess::ClearTarget(F.Adapter);
	TestFalse(TEXT("no confirmed target means no combat movement"), F.Adapter->CanCreateSpace());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustCombatSpreadOwnershipTest,
	"Dust2.CombatSpread.ModeSpeedAndCooldownRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustCombatSpreadOwnershipTest::RunTest(const FString& Parameters)
{
	FCombatSpreadWorld F;
	if (!F.World || !F.Adapter || !F.Observer || !F.Weapon) return false;
	UCharacterMovementComponent* Movement = F.Observer->GetCharacterMovement();
	Movement->MaxWalkSpeed = 420.0f;
	F.Weapon->CombatProfile->CreateSpaceMoveSpeed = 600.0f;
	F.Weapon->CombatProfile->CombatSpreadMoveSpeed = 180.0f;
	TestTrue(TEXT("spread speed applied"), FDustEnemyCombatSpreadTestAccess::Speed(F.Adapter,true));
	TestEqual(TEXT("spread does not inherit retreat sprint"), Movement->MaxWalkSpeed,180.0f);
	FDustEnemyCombatSpreadTestAccess::Restore(F.Adapter);
	TestEqual(TEXT("original speed restored"), Movement->MaxWalkSpeed,420.0f);
	TestTrue(TEXT("normal retreat speed applied"), FDustEnemyCombatSpreadTestAccess::Speed(F.Adapter,false));
	TestEqual(TEXT("old retreat speed retained"), Movement->MaxWalkSpeed,600.0f);
	FDustEnemyCombatSpreadTestAccess::Restore(F.Adapter);
	F.Weapon->CombatProfile->CreateSpaceCooldownSeconds = 0.5f;
	F.Weapon->CombatProfile->CombatSpreadCooldownSeconds = 2.0f;
	FDustEnemyCombatSpreadTestAccess::Cooldown(F.Adapter,true);
	TestEqual(TEXT("spread cooldown belongs to spread mode"), FDustEnemyCombatSpreadTestAccess::AllowedAt(F.Adapter), static_cast<float>(F.World->GetTimeSeconds() + 2.0));
	FDustEnemyCombatSpreadTestAccess::Cooldown(F.Adapter,false);
	TestEqual(TEXT("retreat cooldown unchanged"), FDustEnemyCombatSpreadTestAccess::AllowedAt(F.Adapter), static_cast<float>(F.World->GetTimeSeconds() + 0.5));
	return true;
}
#endif
