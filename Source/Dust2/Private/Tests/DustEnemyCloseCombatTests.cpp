// W09 R17 close-combat tests: point-blank shot geometry and friendly zero-damage payload.
// This TU owns its own friend name and never reuses another test's private seam.

#include "DustEnemyCloseCombatTests.h"
#include "GameFramework/Actor.h"

void UDustEnemyCloseCombatShotListener::HandleShot(FVector Origin, FVector End, const FHitResult& Hit,
	float Damage, float ArmorPenetration, float HeadDamageMultiplier, AWeaponRuntime* Weapon)
{
	++ShotCount;
	LastOrigin = Origin;
	LastEnd = End;
	LastHitActor = Hit.GetActor();
	bLastBlockingHit = Hit.bBlockingHit;
	LastDamage = Damage;
	LastArmorPenetration = ArmorPenetration;
	LastHeadDamageMultiplier = HeadDamageMultiplier;
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyVisibility.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponRecoilData.h"
#include "WeaponRuntime.h"

struct FDustEnemyCloseCombatTestAccess
{
	static bool GetShotGeometry(const UDustEnemyWeaponComponent* Weapon,
		FVector& OutMuzzle, FVector& OutAimPoint)
	{
		return Weapon && Weapon->GetShotGeometry(OutMuzzle, OutAimPoint);
	}
};

namespace
{
	struct FCloseCombatTestWorld
	{
		UWorld* World = nullptr;
		ACharacter* Observer = nullptr;
		ACharacter* Ally = nullptr;
		ADust2PlayerCharacter* Target = nullptr;
		UDustEnemyWeaponComponent* Weapon = nullptr;
		UDustEnemyCombatProfile* Profile = nullptr;
		UWeaponDefinitionData* Definition = nullptr;
		USceneComponent* Muzzle = nullptr;
		UDustEnemyCloseCombatShotListener* Listener = nullptr;

		FCloseCombatTestWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(true).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Values);
			if (!World) return;
			if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			Observer = World->SpawnActor<ACharacter>();
			Ally = World->SpawnActor<ACharacter>();
			Target = World->SpawnActor<ADust2PlayerCharacter>();
			if (!Observer || !Ally || !Target) return;

			Observer->SetActorLocation(FVector(0.0f, 0.0f, 100.0f));
			// Pin the native pawn eye height so the point-blank segment math does not depend on
			// project/Blueprint defaults.
			Observer->BaseEyeHeight = 64.0f;
			Ally->SetActorLocation(FVector(5000.0f, 0.0f, 100.0f));
			Target->SetActorLocation(FVector(600.0f, 0.0f, 100.0f));
			for (ACharacter* Character : { Observer, Ally, static_cast<ACharacter*>(Target) })
			{
				if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
					Movement->SetMovementMode(MOVE_None);
			}
			// The isolated world does not inherit the project collision profiles, so make the real
			// character capsules block the same Visibility channel the weapon trace uses.
			MakeTraceable(Target);
			MakeTraceable(Ally);

			Profile = NewObject<UDustEnemyCombatProfile>();
			Profile->ReactionDelay = 0.0f;
			Profile->SettledAimSeconds = 0.0f;
			Profile->TrackingSpeedDegPerSecond = 720.0f;
			Profile->AimHeightOffset = 50.0f;
			Profile->EngagementRange = 1200.0f;

			Definition = NewObject<UWeaponDefinitionData>();
			UWeaponBallisticsData* Ballistics = NewObject<UWeaponBallisticsData>(Definition);
			Ballistics->DamagePerShot = 17.0f;
			Ballistics->ArmorPenetration = 3.0f;
			Ballistics->HeadDamageMultiplier = 2.5f;
			Ballistics->TraceDistance = 2000.0f;
			Definition->BallisticsData = Ballistics;
			Definition->ConfigurationVersion = 0;
			Definition->ReloadStyle = EWeaponReloadStyle::Magazine;
			Definition->MagazineCapacity = 30;
			Definition->InitialMagazineAmmo = 30;
			Definition->InitialReserveAmmo = 0;
			Definition->AllowedFireModes = { EFireMode::SemiAutomatic };
			Definition->DefaultFireMode = EFireMode::SemiAutomatic;
			Definition->FireInterval = 0.05f;
			Definition->StationarySpreadAngle = 0.0f;
			Definition->MovingSpreadAngle = 0.0f;
			Definition->JumpSpreadAngle = 0.0f;
			Definition->SlowWalkSpreadAngle = 0.0f;
			Definition->MovementSpreadSpeedThreshold = 0.0f;
			Definition->bUseFiringSpread = false;
			Definition->RecoilData = NewObject<UWeaponRecoilData>(Definition);
			Definition->RecoilData->bUseFixedPattern = false;
			Definition->RecoilData->bUseIndependentShotRecoil = false;

			Weapon = NewObject<UDustEnemyWeaponComponent>(Observer);
			Weapon->WeaponDefinition = Definition;
			Weapon->CombatProfile = Profile;
			Observer->AddInstanceComponent(Weapon);
			Weapon->RegisterComponent();
			Weapon->SetComponentTickEnabled(false);

			// A real friendly enemy body: the visibility helper only classifies a pawn as friendly
			// when both sides carry the enemy weapon component.
			UDustEnemyWeaponComponent* AllyWeapon = NewObject<UDustEnemyWeaponComponent>(Ally);
			Ally->AddInstanceComponent(AllyWeapon);
			AllyWeapon->RegisterComponent();
			AllyWeapon->SetComponentTickEnabled(false);

			Muzzle = NewObject<USceneComponent>(Observer);
			Muzzle->SetupAttachment(Observer->GetRootComponent());
			Observer->AddInstanceComponent(Muzzle);
			Muzzle->RegisterComponent();
			Muzzle->SetRelativeLocation(FVector(60.0f, 0.0f, 50.0f));
			Weapon->SetMuzzleSource(Muzzle, NAME_None);

			Listener = NewObject<UDustEnemyCloseCombatShotListener>(World);
			if (Listener)
			{
				Listener->AddToRoot();
				Weapon->OnShotResolved.AddDynamic(
					Listener, &UDustEnemyCloseCombatShotListener::HandleShot);
			}
		}

		~FCloseCombatTestWorld()
		{
			if (Listener)
			{
				Listener->RemoveFromRoot();
				Listener = nullptr;
			}
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World = nullptr;
			}
		}

		bool Initialize() { return IsValid(Weapon) && Weapon->InitializeWeapon(); }

		bool StartEngagement()
		{
			return IsValid(Weapon) && IsValid(Target) && Weapon->StartEngagement(Target);
		}

		void TickWeapon()
		{
			if (IsValid(Weapon)) Weapon->TickComponent(0.1f, LEVELTICK_All, nullptr);
		}

		FVector EyeLocation() const
		{
			FVector Eye = FVector::ZeroVector;
			FRotator Rotation = FRotator::ZeroRotator;
			if (IsValid(Observer)) Observer->GetActorEyesViewPoint(Eye, Rotation);
			return Eye;
		}

		FVector AimPoint() const
		{
			return DustEnemyVisibility::GetAimPoint(Target, Profile);
		}

		AActor* SpawnWall(const FVector& Location, const FVector& Extent)
		{
			if (!World) return nullptr;
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor) return nullptr;
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Actor->SetRootComponent(Box);
			Actor->AddInstanceComponent(Box);
			Box->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Actor;
		}

		static void MakeTraceable(ACharacter* Character)
		{
			if (!Character) return;
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Capsule->SetCollisionResponseToAllChannels(ECR_Block);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCloseCombatPointBlankTest,
	"Dust2.EnemyCloseCombat.PlayerBetweenEyeAndMuzzleStillFires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyCloseCombatPointBlankTest::RunTest(const FString& Parameters)
{
	FCloseCombatTestWorld F;
	if (!TestNotNull(TEXT("isolated close-combat world"), F.World)) return false;
	if (!TestNotNull(TEXT("observer"), F.Observer) || !TestNotNull(TEXT("player target"), F.Target)
		|| !TestNotNull(TEXT("weapon"), F.Weapon) || !TestNotNull(TEXT("muzzle"), F.Muzzle)
		|| !TestNotNull(TEXT("shot listener"), F.Listener)) return false;

	// Point blank: the player body covers the short Eye->Muzzle segment while the raised aim point
	// still sits in front of the physical muzzle, so the normal muzzle origin must stay in use.
	F.Target->SetActorLocation(FVector(20.0f, 0.0f, 100.0f));
	F.Muzzle->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));

	const FVector PhysicalMuzzle = F.Muzzle->GetComponentLocation();
	const UCapsuleComponent* TargetCapsule = F.Target->GetCapsuleComponent();
	const FVector LocalMuzzle = F.Target->GetActorTransform().InverseTransformPosition(PhysicalMuzzle);
	const float Radius = TargetCapsule->GetScaledCapsuleRadius();
	const float HalfHeight = TargetCapsule->GetScaledCapsuleHalfHeight();
	const float Horizontal = FVector2D(LocalMuzzle.X, LocalMuzzle.Y).Size();
	const float Vertical = FMath::Max(0.0f, FMath::Abs(LocalMuzzle.Z) - HalfHeight);
	TestTrue(TEXT("the physical muzzle sits inside the player's blocking body"),
		Horizontal * Horizontal + Vertical * Vertical < Radius * Radius);

	if (!TestTrue(TEXT("weapon initializes"), F.Initialize())) return false;
	if (!TestTrue(TEXT("engagement starts"), F.StartEngagement())) return false;

	FVector OutMuzzle, OutAim;
	if (!TestTrue(TEXT("point-blank target does not veto the shot geometry"),
		FDustEnemyCloseCombatTestAccess::GetShotGeometry(F.Weapon, OutMuzzle, OutAim))) return false;
	TestTrue(TEXT("aim point in front of the muzzle keeps the physical muzzle origin"),
		OutMuzzle.Equals(PhysicalMuzzle, 0.1f));
	TestTrue(TEXT("aim point is the elevated player point"),
		OutAim.Equals(FVector(20.0f, 0.0f, 150.0f), 0.1f));

	F.TickWeapon();
	TestEqual(TEXT("exactly one shot commits"), F.Weapon->SuccessfulShots, 1);
	TestEqual(TEXT("exactly one round is consumed"), F.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), 29);
	TestTrue(TEXT("the engagement continues after the point-blank shot"), F.Weapon->IsEngaging());
	TestEqual(TEXT("the shot delegate fires once"), F.Listener->ShotCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCloseCombatMuzzlePastTargetTest,
	"Dust2.EnemyCloseCombat.MuzzlePastTargetDoesNotReverseShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyCloseCombatMuzzlePastTargetTest::RunTest(const FString& Parameters)
{
	FCloseCombatTestWorld F;
	if (!TestNotNull(TEXT("isolated close-combat world"), F.World)) return false;
	if (!TestNotNull(TEXT("player target"), F.Target) || !TestNotNull(TEXT("weapon"), F.Weapon)
		|| !TestNotNull(TEXT("muzzle"), F.Muzzle) || !TestNotNull(TEXT("shot listener"), F.Listener)) return false;

	// The physical muzzle is already past the very close player: Muzzle->Aim would point backwards.
	F.Target->SetActorLocation(FVector(90.0f, 0.0f, 100.0f));
	F.Muzzle->SetRelativeLocation(FVector(150.0f, 0.0f, 40.0f));

	const FVector PhysicalMuzzle = F.Muzzle->GetComponentLocation();
	const FVector Eye = F.EyeLocation();
	const FVector Aim = F.AimPoint();
	TestTrue(TEXT("fixture really has the aim point behind the physical muzzle"),
		FVector::DotProduct(Aim - PhysicalMuzzle, Aim - Eye) < 0.0f);
	// Reproduce the original gap with a real trace: with no target exemption this segment rejects.
	TestFalse(TEXT("the player body physically blocks the old Eye->Muzzle knowledge check"),
		DustEnemyVisibility::HasSight(F.Observer, nullptr, Eye, PhysicalMuzzle));

	if (!TestTrue(TEXT("weapon initializes"), F.Initialize())) return false;
	if (!TestTrue(TEXT("engagement starts"), F.StartEngagement())) return false;

	FVector OutMuzzle, OutAim;
	if (!TestTrue(TEXT("muzzle-past-target geometry is still accepted"),
		FDustEnemyCloseCombatTestAccess::GetShotGeometry(F.Weapon, OutMuzzle, OutAim))) return false;
	TestTrue(TEXT("logical shot origin falls back to the eye"), OutMuzzle.Equals(Eye, 0.1f));
	TestTrue(TEXT("logical aim still points at the player"),
		(OutAim - OutMuzzle).GetSafeNormal().X > 0.8f);

	F.TickWeapon();
	TestEqual(TEXT("the point-blank shot still commits once"), F.Weapon->SuccessfulShots, 1);
	TestEqual(TEXT("the point-blank shot consumes one round"),
		F.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), 29);
	TestTrue(TEXT("the committed shot does not reverse away from the player"),
		F.Weapon->LastShotDirection.X > 0.8f);
	TestEqual(TEXT("the shot delegate fires once"), F.Listener->ShotCount, 1);
	TestTrue(TEXT("the real trace still hits the player"), F.Listener->LastHitActor.Get() == F.Target);
	TestEqual(TEXT("the player hit keeps the normal damage payload"), F.Listener->LastDamage, 17.0f);

	// The visible muzzle must not move, and normal range must keep firing from the actual muzzle.
	TestTrue(TEXT("the presentation muzzle is unchanged"),
		F.Muzzle->GetComponentLocation().Equals(PhysicalMuzzle, 0.1f));
	F.Weapon->StopEngagement();
	F.Target->SetActorLocation(FVector(600.0f, 0.0f, 100.0f));
	if (!TestTrue(TEXT("normal-range engagement starts"), F.StartEngagement())) return false;
	if (!TestTrue(TEXT("normal-range geometry is accepted"),
		FDustEnemyCloseCombatTestAccess::GetShotGeometry(F.Weapon, OutMuzzle, OutAim))) return false;
	TestTrue(TEXT("normal range keeps the physical muzzle origin"),
		OutMuzzle.Equals(PhysicalMuzzle, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCloseCombatWallTest,
	"Dust2.EnemyCloseCombat.WallStillBlocksEyeMuzzleAndTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyCloseCombatWallTest::RunTest(const FString& Parameters)
{
	// A wall between the eye and the target rejects the shot geometry.
	{
		FCloseCombatTestWorld F;
		if (!TestNotNull(TEXT("isolated close-combat world"), F.World)) return false;
		if (!TestNotNull(TEXT("weapon"), F.Weapon) || !TestNotNull(TEXT("shot listener"), F.Listener)) return false;
		F.Target->SetActorLocation(FVector(600.0f, 0.0f, 100.0f));
		F.Muzzle->SetRelativeLocation(FVector(60.0f, 0.0f, 50.0f));
		F.SpawnWall(FVector(300.0f, 0.0f, 100.0f), FVector(25.0f, 100.0f, 150.0f));
		if (!TestTrue(TEXT("weapon initializes"), F.Initialize())) return false;
		if (!TestTrue(TEXT("engagement starts"), F.StartEngagement())) return false;

		FVector OutMuzzle, OutAim;
		TestFalse(TEXT("wall between eye and target rejects the geometry"),
			FDustEnemyCloseCombatTestAccess::GetShotGeometry(F.Weapon, OutMuzzle, OutAim));
		const int32 AmmoBefore = F.Weapon->GetWeaponRuntime()->GetCurrentAmmo();
		F.TickWeapon();
		TestEqual(TEXT("no shot is committed through the target wall"), F.Weapon->SuccessfulShots, 0);
		TestEqual(TEXT("no ammo is consumed through the target wall"),
			F.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), AmmoBefore);
		TestEqual(TEXT("no payload is broadcast through the target wall"), F.Listener->ShotCount, 0);
		TestFalse(TEXT("the engagement stops when the wall breaks geometry"), F.Weapon->IsEngaging());
	}

	// A wall that only crosses Eye->Muzzle still blocks; the target exemption cannot bypass it.
	{
		FCloseCombatTestWorld F;
		if (!TestNotNull(TEXT("isolated close-combat world"), F.World)) return false;
		if (!TestNotNull(TEXT("weapon"), F.Weapon) || !TestNotNull(TEXT("shot listener"), F.Listener)) return false;
		F.Target->SetActorLocation(FVector(300.0f, 0.0f, 100.0f));
		F.Muzzle->SetRelativeLocation(FVector(100.0f, 0.0f, 40.0f));
		F.SpawnWall(FVector(50.0f, 0.0f, 148.0f), FVector(5.0f, 20.0f, 10.0f));

		const FVector Eye = F.EyeLocation();
		const FVector Aim = F.AimPoint();
		const FVector PhysicalMuzzle = F.Muzzle->GetComponentLocation();
		TestTrue(TEXT("Eye->Aim stays clear above the near wall"),
			DustEnemyVisibility::HasSight(F.Observer, F.Target, Eye, Aim));
		TestFalse(TEXT("the near wall still blocks Eye->Muzzle"),
			DustEnemyVisibility::HasSight(F.Observer, F.Target, Eye, PhysicalMuzzle));
		if (!TestTrue(TEXT("weapon initializes"), F.Initialize())) return false;
		if (!TestTrue(TEXT("engagement starts"), F.StartEngagement())) return false;

		FVector OutMuzzle, OutAim;
		TestFalse(TEXT("blocked Eye->Muzzle rejects the geometry"),
			FDustEnemyCloseCombatTestAccess::GetShotGeometry(F.Weapon, OutMuzzle, OutAim));
		F.TickWeapon();
		TestEqual(TEXT("blocked Eye->Muzzle commits no shot"), F.Weapon->SuccessfulShots, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCloseCombatFriendlyZeroDamageTest,
	"Dust2.EnemyCloseCombat.FriendlyHitCommitsZeroDamagePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyCloseCombatFriendlyZeroDamageTest::RunTest(const FString& Parameters)
{
	FCloseCombatTestWorld F;
	if (!TestNotNull(TEXT("isolated close-combat world"), F.World)) return false;
	if (!TestNotNull(TEXT("player target"), F.Target) || !TestNotNull(TEXT("ally"), F.Ally)
		|| !TestNotNull(TEXT("weapon"), F.Weapon) || !TestNotNull(TEXT("shot listener"), F.Listener)) return false;

	F.Target->SetActorLocation(FVector(600.0f, 0.0f, 100.0f));
	F.Ally->SetActorLocation(FVector(300.0f, 0.0f, 100.0f));
	F.Muzzle->SetRelativeLocation(FVector(60.0f, 0.0f, 50.0f));

	const FVector Eye = F.EyeLocation();
	const FVector Aim = F.AimPoint();
	TestTrue(TEXT("the friendly body is recognized as friendly"),
		DustEnemyVisibility::IsFriendlyBlocker(F.Observer, F.Ally));
	TestTrue(TEXT("friendly knowledge sight ignores the ally and still sees the player"),
		DustEnemyVisibility::HasSight(F.Observer, F.Target, Eye, Aim));
	if (!TestTrue(TEXT("weapon initializes"), F.Initialize())) return false;
	if (!TestTrue(TEXT("engagement starts"), F.StartEngagement())) return false;

	FVector OutMuzzle, OutAim;
	if (!TestTrue(TEXT("the friendly body does not veto the shot geometry"),
		FDustEnemyCloseCombatTestAccess::GetShotGeometry(F.Weapon, OutMuzzle, OutAim))) return false;

	F.TickWeapon();
	TestEqual(TEXT("the real shot still commits exactly once"), F.Weapon->SuccessfulShots, 1);
	TestEqual(TEXT("the real shot still consumes exactly one round"),
		F.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), 29);
	TestEqual(TEXT("the delegate fires exactly once"), F.Listener->ShotCount, 1);
	TestTrue(TEXT("the real blocking hit is preserved"), F.Listener->bLastBlockingHit);
	TestTrue(TEXT("the real hit actor is the friendly body"),
		F.Listener->LastHitActor.Get() == F.Ally);
	TestTrue(TEXT("the real end stays at the friendly hit, not the player"),
		F.Listener->LastEnd.X < 400.0f && !F.Listener->LastEnd.Equals(Aim));
	TestEqual(TEXT("friendly damage payload is zero"), F.Listener->LastDamage, 0.0f);
	TestEqual(TEXT("friendly armor penetration payload is zero"),
		F.Listener->LastArmorPenetration, 0.0f);
	TestEqual(TEXT("friendly head multiplier payload is zero"),
		F.Listener->LastHeadDamageMultiplier, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustEnemyCloseCombatPlayerDamageTest,
	"Dust2.EnemyCloseCombat.PlayerHitKeepsNormalDamagePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustEnemyCloseCombatPlayerDamageTest::RunTest(const FString& Parameters)
{
	FCloseCombatTestWorld F;
	if (!TestNotNull(TEXT("isolated close-combat world"), F.World)) return false;
	if (!TestNotNull(TEXT("player target"), F.Target) || !TestNotNull(TEXT("weapon"), F.Weapon)
		|| !TestNotNull(TEXT("shot listener"), F.Listener)) return false;

	F.Target->SetActorLocation(FVector(600.0f, 0.0f, 100.0f));
	F.Muzzle->SetRelativeLocation(FVector(60.0f, 0.0f, 50.0f));
	if (!TestTrue(TEXT("weapon initializes"), F.Initialize())) return false;
	if (!TestTrue(TEXT("engagement starts"), F.StartEngagement())) return false;

	F.TickWeapon();
	TestEqual(TEXT("the player shot commits exactly once"), F.Weapon->SuccessfulShots, 1);
	TestEqual(TEXT("the player shot consumes exactly one round"),
		F.Weapon->GetWeaponRuntime()->GetCurrentAmmo(), 29);
	TestEqual(TEXT("the delegate fires exactly once"), F.Listener->ShotCount, 1);
	TestTrue(TEXT("the player is really hit"), F.Listener->bLastBlockingHit
		&& F.Listener->LastHitActor.Get() == F.Target);
	TestEqual(TEXT("the player hit keeps the configured damage"), F.Listener->LastDamage, 17.0f);
	TestEqual(TEXT("the player hit keeps the configured armor penetration"),
		F.Listener->LastArmorPenetration, 3.0f);
	TestEqual(TEXT("the player hit keeps the configured head multiplier"),
		F.Listener->LastHeadDamageMultiplier, 2.5f);
	return true;
}

#endif
