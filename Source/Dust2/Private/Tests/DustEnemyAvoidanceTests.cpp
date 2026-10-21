#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AIController.h"
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

struct FDustEnemyAvoidanceTestAccess
{
 static void Bind(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
  APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon, AActor* Target)
 {
  Adapter->Controller=Controller; Adapter->BoundPawn=Pawn; Adapter->Tree=Tree;
  Adapter->WeaponComponent=Weapon; Adapter->Target=Target; Adapter->bInitialized=true;
 }
 static void Apply(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->ApplyLocalAvoidance(); }
 static void Restore(UDustEnemyCombatAdapterComponent* Adapter) { Adapter->RestoreLocalAvoidance(); }
 static bool Clear(UDustEnemyCombatAdapterComponent* Adapter, FVector A, FVector B)
 { return Adapter->IsSpacingSegmentClear(A,B,25.0f,50.0f); }
};
namespace
{
	struct FAvoidanceWorld
	{
		UWorld* World = nullptr;
		AAIController* Controller = nullptr;
		ACharacter* Observer = nullptr;
		ACharacter* Ally = nullptr;
		ADust2PlayerCharacter* Target = nullptr;
		UDustEnemyWeaponComponent* Weapon = nullptr;
		UDustEnemyCombatAdapterComponent* Adapter = nullptr;
		FAvoidanceWorld()
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
			Weapon->CombatProfile->SettledAimSeconds = 0;
			UDustEnemyWeaponComponent* AllyWeapon = NewObject<UDustEnemyWeaponComponent>(Ally);
			Ally->AddInstanceComponent(AllyWeapon); AllyWeapon->RegisterComponent(); AllyWeapon->SetComponentTickEnabled(false);
			Box(Ally, FVector::ZeroVector);
			UStateTreeAIComponent* Tree = NewObject<UStateTreeAIComponent>(Controller);
			Tree->SetStartLogicAutomatically(false); Controller->AddInstanceComponent(Tree); Tree->RegisterComponent();
			Adapter = NewObject<UDustEnemyCombatAdapterComponent>(Controller);
			Controller->AddInstanceComponent(Adapter); Adapter->RegisterComponent();
			FDustEnemyAvoidanceTestAccess::Bind(Adapter, Controller, Observer, Tree, Weapon, Target);
		}
		~FAvoidanceWorld() { if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); } }
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
		AActor* Wall()
		{
			AActor* Actor = World->SpawnActor<AActor>();
			Box(Actor, FVector::ZeroVector); Actor->SetActorLocation(FVector(450, 0, 100)); return Actor;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAvoidanceOwnershipTest,
	"Dust2.EnemyAvoidance.EnableRestoreAndOptOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyAvoidanceOwnershipTest::RunTest(const FString& Parameters)
{
	FAvoidanceWorld F;
	if (!F.Adapter || !F.Observer || !F.Weapon) return false;
	UCharacterMovementComponent* Movement = F.Observer->GetCharacterMovement();
	Movement->SetAvoidanceEnabled(false);
	Movement->AvoidanceConsiderationRadius = 123.0f;
	F.Weapon->CombatProfile->LocalAvoidanceRadius = 500.0f;
	FDustEnemyAvoidanceTestAccess::Apply(F.Adapter);
	TestTrue(TEXT("RVO enabled on controlled enemy"), bool(Movement->bUseRVOAvoidance));
	TestEqual(TEXT("profile consideration radius applied"), Movement->AvoidanceConsiderationRadius, 500.0f);
	FDustEnemyAvoidanceTestAccess::Restore(F.Adapter);
	TestFalse(TEXT("original disabled state restored"), bool(Movement->bUseRVOAvoidance));
	TestEqual(TEXT("original radius restored"), Movement->AvoidanceConsiderationRadius, 123.0f);
	FDustEnemyAvoidanceTestAccess::Apply(F.Adapter);
	Movement->AvoidanceConsiderationRadius = 777.0f;
	FDustEnemyAvoidanceTestAccess::Restore(F.Adapter);
	TestEqual(TEXT("external radius edit survives cleanup"), Movement->AvoidanceConsiderationRadius, 777.0f);
	F.Weapon->CombatProfile->bEnableLocalAvoidance = false;
	FDustEnemyAvoidanceTestAccess::Apply(F.Adapter);
	TestFalse(TEXT("profile opt-out keeps RVO disabled"), bool(Movement->bUseRVOAvoidance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyAvoidanceRouteTest,
	"Dust2.EnemyAvoidance.CorridorDefersFriendsButRejectsWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyAvoidanceRouteTest::RunTest(const FString& Parameters)
{
	FAvoidanceWorld F;
	if (!F.Adapter || !F.Observer || !F.Weapon) return false;
	const FVector Start(0,0,100), End(450,0,100);
	TestFalse(TEXT("without RVO friendly body blocks corridor"), FDustEnemyAvoidanceTestAccess::Clear(F.Adapter,Start,End));
	FDustEnemyAvoidanceTestAccess::Apply(F.Adapter);
	TestTrue(TEXT("with RVO corridor may defer friendly body to local steering"), FDustEnemyAvoidanceTestAccess::Clear(F.Adapter,Start,End));
	F.Wall();
	TestFalse(TEXT("wall beyond friendly remains blocking"), FDustEnemyAvoidanceTestAccess::Clear(F.Adapter,Start,End));
	FDustEnemyAvoidanceTestAccess::Restore(F.Adapter);
	return true;
}
#endif
