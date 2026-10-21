#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustDeathCameraComponent.h"
#include "DustRagdollDeathComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

struct FDustRagdollDeathTestAccess
{
 static bool HasTimer(const UDustRagdollDeathComponent* Component)
 { return Component->GetWorld()->GetTimerManager().IsTimerActive(Component->CorpseTimerHandle); }
 static float Remaining(const UDustRagdollDeathComponent* Component)
 { return Component->GetWorld()->GetTimerManager().GetTimerRemaining(Component->CorpseTimerHandle); }
 static FTimerHandle Handle(const UDustRagdollDeathComponent* Component) { return Component->CorpseTimerHandle; }
};

namespace
{
 struct FDeathWorld
 {
  UWorld* World = nullptr;
  FDeathWorld()
  {
   UWorld::InitializationValues Values;
   Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
    .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
   World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
   if (!World) return;
   if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
   World->InitializeActorsForPlay(FURL()); World->BeginPlay();
  }
  ~FDeathWorld()
  { if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); } }
  UDustRagdollDeathComponent* AddDeath(ACharacter* Character)
  {
   auto* Death = NewObject<UDustRagdollDeathComponent>(Character);
   Character->AddInstanceComponent(Death); Death->RegisterComponent(); return Death;
  }
 };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeathMissingAssetReset,
 "Dust2.Death.MissingAssetCleanupAndReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustDeathMissingAssetReset::RunTest(const FString&)
{
 FDeathWorld Fixture;
 if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
 ACharacter* Pawn = Fixture.World->SpawnActor<ACharacter>();
 if (!TestNotNull(TEXT("Character"), Pawn)) return false;
 auto* Death = Fixture.AddDeath(Pawn);
 auto* Mesh = Pawn->GetMesh();
 const auto OriginalCollision = Pawn->GetCapsuleComponent()->GetCollisionEnabled();
 const FTransform Relative = Mesh->GetRelativeTransform();
 Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
 Mesh->SetOwnerNoSee(true); Mesh->SetHiddenInGame(true);
 TArray<USceneComponent*> Hide;
 TestTrue(TEXT("Own mesh config accepted without PhysicsAsset"), Death->InitializeDeathPresentation(Mesh, Hide));
 TestFalse(TEXT("No asset does not claim successful simulation"), Death->StartRagdollDeath());
 TestTrue(TEXT("Death still latched"), Death->IsDeathStarted());
 TestTrue(TEXT("Enemy cleanup timer still scheduled"), FDustRagdollDeathTestAccess::HasTimer(Death));
 TestTrue(TEXT("Capsule disabled"), Pawn->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
 TestTrue(TEXT("Movement disabled"), Pawn->GetCharacterMovement()->MovementMode == MOVE_None);
 TestFalse(TEXT("Body shown to owner"), Mesh->bOwnerNoSee);
 TestFalse(TEXT("Body unhidden"), Mesh->bHiddenInGame);
 const float Remaining = FDustRagdollDeathTestAccess::Remaining(Death);
 const FTimerHandle Handle = FDustRagdollDeathTestAccess::Handle(Death);
 Death->StartRagdollDeath();
 TestTrue(TEXT("Repeated start keeps the same timer request"), FDustRagdollDeathTestAccess::Handle(Death) == Handle);
 TestEqual(TEXT("Repeated start does not renew corpse timer"), FDustRagdollDeathTestAccess::Remaining(Death), Remaining);
 Pawn->SetActorLocation(FVector(1000, 2000, 3000));
 const FVector CheckpointLocation = Pawn->GetActorLocation();
 Death->ResetDeathPresentation();
 TestFalse(TEXT("Timer cancelled on reuse"), FDustRagdollDeathTestAccess::HasTimer(Death));
 TestFalse(TEXT("Death reset"), Death->IsDeathStarted());
 TestTrue(TEXT("Capsule restored"), Pawn->GetCapsuleComponent()->GetCollisionEnabled() == OriginalCollision);
 TestTrue(TEXT("Movement restored"), Pawn->GetCharacterMovement()->MovementMode == MOVE_Flying);
 TestTrue(TEXT("Body render flags restored"), Mesh->bOwnerNoSee && Mesh->bHiddenInGame);
 TestTrue(TEXT("Body reattached with prior relative transform"), Mesh->GetAttachParent() == Pawn->GetCapsuleComponent()
  && Mesh->GetRelativeTransform().Equals(Relative));
 TestTrue(TEXT("Reset preserves checkpoint relocation"), Pawn->GetActorLocation().Equals(CheckpointLocation));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeathOwnershipAndRetention,
 "Dust2.Death.OwnershipAndRetention", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustDeathOwnershipAndRetention::RunTest(const FString&)
{
 FDeathWorld Fixture;
 if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
 ACharacter* Pawn = Fixture.World->SpawnActor<ACharacter>();
 ACharacter* Other = Fixture.World->SpawnActor<ACharacter>();
 if (!Pawn || !Other) return false;
 auto* Death = Fixture.AddDeath(Pawn);
 TArray<USceneComponent*> Hide;
 TestFalse(TEXT("Foreign mesh rejected"), Death->InitializeDeathPresentation(Other->GetMesh(), Hide));
 Hide.Add(Pawn->GetCapsuleComponent());
 TestFalse(TEXT("Body ancestor cannot be hidden"), Death->InitializeDeathPresentation(Pawn->GetMesh(), Hide));
 Hide.Reset(); Death->InitializeDeathPresentation(Pawn->GetMesh(), Hide);
 Death->bAutoDestroyOwner = false;
 Death->StartRagdollDeath();
 TestFalse(TEXT("Player retention policy schedules no destruction"), FDustRagdollDeathTestAccess::HasTimer(Death));
 TestFalse(TEXT("Active death cannot be reconfigured"), Death->InitializeDeathPresentation(Pawn->GetMesh(), Hide));
 Death->ResetDeathPresentation();
 TestTrue(TEXT("Configuration available after reset"), Death->InitializeDeathPresentation(Pawn->GetMesh(), Hide));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustPlayerDeathHealthReuse,
 "Dust2.Death.PlayerHealthReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustPlayerDeathHealthReuse::RunTest(const FString&)
{
 FDeathWorld Fixture;
 if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
 ADust2PlayerCharacter* Pawn = Fixture.World->SpawnActor<ADust2PlayerCharacter>();
 if (!TestNotNull(TEXT("Player"), Pawn)) return false;
 TArray<USceneComponent*> Hide;
 Pawn->DeathPresentation->InitializeDeathPresentation(Pawn->GetMesh(), Hide);
 bool DiedNow = false;
 Pawn->ApplyDamage(100000.0f, DiedNow);
 TestTrue(TEXT("First lethal hit owns death notification"), DiedNow);
 TestTrue(TEXT("Configured native death hook ran"), Pawn->DeathPresentation->IsDeathStarted());
 TestFalse(TEXT("Player has no corpse deletion timer"), FDustRagdollDeathTestAccess::HasTimer(Pawn->DeathPresentation));
 TestFalse(TEXT("No controller yields clean camera failure"), Pawn->DeathCamera->IsDeathCameraActive());
 Pawn->ApplyDamage(1.0f, DiedNow);
 TestFalse(TEXT("Repeated lethal damage does not repeat death notification"), DiedNow);
 Pawn->RestoreHealth(100.0f);
 TestFalse(TEXT("Positive checkpoint health resets presentation"), Pawn->DeathPresentation->IsDeathStarted());
 TestFalse(TEXT("Health restored"), Pawn->IsDead());
 Pawn->ApplyDamage(100000.0f, DiedNow);
 TestTrue(TEXT("Reused pawn can die once in its new life"), DiedNow);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDustDeathHideRootsAndRestore,
 "Dust2.Death.HideConfiguredRootsAndRestore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDustDeathHideRootsAndRestore::RunTest(const FString&)
{
 FDeathWorld Fixture;
 if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
 ACharacter* Pawn = Fixture.World->SpawnActor<ACharacter>();
 if (!TestNotNull(TEXT("Character"), Pawn)) return false;
 auto* Death = Fixture.AddDeath(Pawn);
 Death->bAutoDestroyOwner = false;
 auto AddScene = [Pawn](USceneComponent* Parent)
 {
  auto* Component = NewObject<USceneComponent>(Pawn);
  Pawn->AddInstanceComponent(Component);
  Component->SetupAttachment(Parent); Component->RegisterComponent();
  return Component;
 };
 USceneComponent* ExtraBody = AddScene(Pawn->GetCapsuleComponent());
 USceneComponent* Child = AddScene(ExtraBody);
 USceneComponent* LeafRoot = AddScene(Pawn->GetCapsuleComponent());
 ExtraBody->SetVisibility(true, false); ExtraBody->SetHiddenInGame(false, false);
 Child->SetVisibility(false, false); Child->SetHiddenInGame(false, false);
 LeafRoot->SetVisibility(true, false); LeafRoot->SetHiddenInGame(true, false);
 TArray<USceneComponent*> Hide = { ExtraBody, LeafRoot, Child };
 TestTrue(TEXT("Configure own extra body and independent leaf"), Death->InitializeDeathPresentation(Pawn->GetMesh(), Hide));
 Death->StartRagdollDeath(); // No asset needed to exercise the actual death visibility path.
 for (USceneComponent* Component : Hide)
  TestTrue(TEXT("Every configured component including roots is hidden"), !Component->GetVisibleFlag() && Component->bHiddenInGame);
 TestTrue(TEXT("Actual death mesh remains visible"), Pawn->GetMesh()->GetVisibleFlag() && !Pawn->GetMesh()->bHiddenInGame);
 Death->ResetDeathPresentation();
 TestTrue(TEXT("Extra body restored"), ExtraBody->GetVisibleFlag() && !ExtraBody->bHiddenInGame);
 TestTrue(TEXT("Child keeps distinct original visibility"), !Child->GetVisibleFlag() && !Child->bHiddenInGame);
 TestTrue(TEXT("Leaf keeps original hidden-in-game flag"), LeafRoot->GetVisibleFlag() && LeafRoot->bHiddenInGame);
 return true;
}
#endif
