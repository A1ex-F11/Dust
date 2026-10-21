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

struct FDustEnemySightTestAccess
{
	static void Bind(UDustEnemyCombatAdapterComponent* Adapter, AAIController* Controller,
		APawn* Pawn, UStateTreeAIComponent* Tree, UDustEnemyWeaponComponent* Weapon, AActor* Target)
	{
		Adapter->Controller = Controller;
		Adapter->BoundPawn = Pawn;
		Adapter->Tree = Tree;
		Adapter->WeaponComponent = Weapon;
		Adapter->Target = Target;
		Adapter->LastKnownLocation = Target->GetActorLocation();
		Adapter->bInitialized = true;
		Adapter->bConfirmedTarget = true;
		Adapter->bSightCurrentlySensed = true;
	}
	static bool InGrace(const UDustEnemyCombatAdapterComponent* Adapter) { return Adapter->bInLossGrace; }
};

namespace
{
	struct FSightWorld
	{
		UWorld* World = nullptr;
		AAIController* Controller = nullptr;
		ACharacter* Observer = nullptr;
		ACharacter* Ally = nullptr;
		ADust2PlayerCharacter* Target = nullptr;
		UDustEnemyWeaponComponent* Weapon = nullptr;
		UDustEnemyCombatAdapterComponent* Adapter = nullptr;
		FSightWorld()
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
			FDustEnemySightTestAccess::Bind(Adapter, Controller, Observer, Tree, Weapon, Target);
		}
		~FSightWorld() { if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); } }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyFriendlySightTest,
	"Dust2.EnemySight.FriendlyOccluderKeepsKnowledgeAndPhysicalCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyFriendlySightTest::RunTest(const FString& Parameters)
{
	FSightWorld F;
	if (!F.Adapter || !F.Target || !F.Observer) return false;
	FVector Eye; FRotator Rotation; F.Observer->GetActorEyesViewPoint(Eye, Rotation);
	const FVector Aim = DustEnemyVisibility::GetAimPoint(F.Target, F.Weapon->CombatProfile);
	FHitResult PhysicalHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DustFriendlySightTest), true, F.Observer);
	TestTrue(TEXT("fixture has a physically blocking friendly body"), F.World->LineTraceSingleByChannel(PhysicalHit, Eye, Aim, ECC_Visibility, Params));
	TestTrue(TEXT("first physical hit belongs to ally"), PhysicalHit.GetActor() == F.Ally);
	TestTrue(TEXT("knowledge sight ignores friendly bodies"), F.Adapter->HasConfirmedTarget());
	TestFalse(TEXT("friendly body does not open memory grace"), F.Adapter->NeedsReacquire());
	FCanBeSeenFromContext Context; Context.IgnoreActor = F.Observer; Context.ObserverLocation = Eye;
	FVector Seen; int32 Checks = 0, Async = 0; float Strength = 0;
	TestTrue(TEXT("engine Sight target callback uses the same friendly filter"),
		F.Target->CanBeSeenFrom(Context, Seen, Checks, Async, Strength) == UAISense_Sight::EVisibilityResult::Visible);
	TestTrue(TEXT("actual LOS trace count is charged to the perception budget"), Checks >= 2);
	F.Wall();
	TestFalse(TEXT("a wall beyond the ally still blocks knowledge"), F.Adapter->HasConfirmedTarget());
	TestTrue(TEXT("memory is selectable before adapter tick creates grace"), F.Adapter->NeedsReacquire());
	TestFalse(TEXT("the read-only query has not changed grace state"), FDustEnemySightTestAccess::InGrace(F.Adapter));
	TestTrue(TEXT("engine callback also respects the wall"),
		F.Target->CanBeSeenFrom(Context, Seen, Checks, Async, Strength) == UAISense_Sight::EVisibilityResult::NotVisible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyRangeAgreementTest,
	"Dust2.EnemySight.RangeUsesTheSameElevatedAimPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyRangeAgreementTest::RunTest(const FString& Parameters)
{
	FSightWorld F;
	if (!F.Weapon || !F.Target || !F.Observer) return false;
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	Definition->BallisticsData = NewObject<UWeaponBallisticsData>(Definition);
	Definition->BallisticsData->TraceDistance = 2000;
	F.Target->SetActorLocation(F.Observer->GetActorLocation() + FVector(1200, 0, 0));
	F.Weapon->CombatProfile->EngagementRange = 1200;
	TestFalse(TEXT("actor center at boundary is outside the elevated aim-point range"),
		DustEnemyVisibility::IsInRange(F.Observer, F.Target, F.Weapon->CombatProfile, Definition));
	F.Target->SetActorLocation(F.Observer->GetActorLocation() + FVector(1190, 0, 0));
	TestTrue(TEXT("just inside the real aim-point range is eligible"),
		DustEnemyVisibility::IsInRange(F.Observer, F.Target, F.Weapon->CombatProfile, Definition));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyFriendlyFireAllowedTest,
	"Dust2.EnemySight.FriendlyInFrontOrBehindDoesNotVetoFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyFriendlyFireAllowedTest::RunTest(const FString& Parameters)
{
	for (const float AllyX : {300.0f, -100.0f})
	{
		FSightWorld F;
		if (!F.Adapter || !F.Weapon || !F.Observer || !F.Target) return false;
		F.Ally->SetActorLocation(FVector(AllyX, 0, 100));
		UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
		Definition->BallisticsData = NewObject<UWeaponBallisticsData>(Definition);
		Definition->BallisticsData->TraceDistance = 2000;
		Definition->MagazineCapacity = 3; Definition->InitialMagazineAmmo = 3; Definition->InitialReserveAmmo = 0;
		Definition->RecoilData = NewObject<UWeaponRecoilData>(Definition);
		Definition->RecoilData->bUseFixedPattern = false;
		Definition->RecoilData->bUseIndependentShotRecoil = false;
		F.Weapon->WeaponDefinition = Definition;
		USceneComponent* Muzzle = NewObject<USceneComponent>(F.Observer);
		Muzzle->SetupAttachment(F.Observer->GetRootComponent()); F.Observer->AddInstanceComponent(Muzzle); Muzzle->RegisterComponent();
		Muzzle->SetRelativeLocation(FVector(0, 0, 50));
		F.Weapon->SetMuzzleSource(Muzzle, NAME_None);
		if (!TestTrue(TEXT("weapon initializes"), F.Weapon->InitializeWeapon())) return false;
		if (!TestTrue(TEXT("engagement starts despite friendly knowledge occluder"), F.Weapon->StartEngagement(F.Target))) return false;
		F.Weapon->SetComponentTickEnabled(false);
		F.Weapon->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestTrue(FString::Printf(TEXT("friendly at X=%.0f does not block ammo commit"), AllyX), F.Weapon->GetWeaponRuntime()->GetCurrentAmmo() < 3);
		TestTrue(TEXT("firing preserves the engagement"), F.Weapon->IsEngaging());
		TestTrue(TEXT("a real shot resolves while the friendly remains in place"), F.Weapon->SuccessfulShots > 0);
	}
	return true;
}
#endif
