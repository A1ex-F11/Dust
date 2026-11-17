#include "DustFireAudioPolicy.h"

#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace DustFireAudioPolicy
{
static USoundConcurrency* GetEnemyConcurrency()
{
 // Every enemy and weapon variant must share the same group, not one group per actor.
 static TWeakObjectPtr<USoundConcurrency> Shared;
 if (!Shared.IsValid())
 {
  USoundConcurrency* Group = NewObject<USoundConcurrency>(GetTransientPackage());
  Group->Concurrency.MaxCount = 5;
  Group->Concurrency.bLimitToOwner = false;
  Group->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
  Group->Concurrency.VoiceStealReleaseTime = 0.02f;
  Shared = Group;
 }
 return Shared.Get();
}

void Apply(AActor* VisualWeapon, bool bPlayerWeapon)
{
 if (!IsValid(VisualWeapon) || VisualWeapon->IsTemplate()) return;
 const FName Marker(bPlayerWeapon ? TEXT("DustPlayerFireAudio") : TEXT("DustEnemyFireAudio"));
 if (VisualWeapon->Tags.Contains(Marker)) return;
 bool bApplied = false;
 // These are the sound fields in the installed BP_TacticalShooterWeapon. Check the
 // reflected type as well: a missing/renamed Blueprint property must fail safely.
 const FName Fields[] = {TEXT("FireUnsuppressed"), TEXT("FireSuppressed"),
  TEXT("UnsuppressedFire"), TEXT("SuppressedFire")};
 for (FName Field : Fields)
 {
  FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(VisualWeapon->GetClass(), Field);
  if (!Property || !Property->PropertyClass->IsChildOf(USoundBase::StaticClass())) continue;
  USoundBase* Source = Cast<USoundBase>(Property->GetObjectPropertyValue_InContainer(VisualWeapon));
  if (!IsValid(Source)) continue;
  USoundBase* Sound = DuplicateObject<USoundBase>(Source, VisualWeapon, MakeUniqueObjectName(VisualWeapon, Source->GetClass(), Source->GetFName()));
  if (!Sound) continue;
  Sound->ClearFlags(RF_Public | RF_Standalone);
  Sound->SetFlags(RF_Transient);
  Sound->Priority = bPlayerWeapon ? 100.0f : 1.0f;
  Sound->bBypassVolumeScaleForPriority = bPlayerWeapon;
  if (!bPlayerWeapon)
  {
   // Priority protects voices from culling; it does not fix audible masking.
   if (USoundCue* Cue = Cast<USoundCue>(Sound)) Cue->VolumeMultiplier *= 0.45f;
   else if (USoundWave* Wave = Cast<USoundWave>(Sound)) Wave->Volume *= 0.45f;
   if (Sound->bOverrideConcurrency)
   {
    USoundConcurrency* AuthoredLimit = NewObject<USoundConcurrency>(Sound);
    AuthoredLimit->Concurrency = Sound->ConcurrencyOverrides;
    Sound->ConcurrencySet.Add(AuthoredLimit);
    Sound->bOverrideConcurrency = false;
   }
   Sound->ConcurrencySet.Add(GetEnemyConcurrency());
  }
  Property->SetObjectPropertyValue_InContainer(VisualWeapon, Sound);
  bApplied = true;
 }
 if (bApplied) VisualWeapon->Tags.AddUnique(Marker);
 else if (!VisualWeapon->Tags.Contains(TEXT("DustFireAudioMissing")))
 {
  VisualWeapon->Tags.Add(TEXT("DustFireAudioMissing"));
  UE_LOG(LogTemp, Warning, TEXT("Gunfire audio policy: no supported fire sound field on %s"), *VisualWeapon->GetPathName());
 }
}

void ApplyToEnemy(AActor* Enemy)
{
 if (!IsValid(Enemy)) return;
 // The installed BP_EnemyCharacter owns this visual child slot. Do not touch its
 // formal WeaponRuntime, damage bridge, or hearing/noise report.
 TInlineComponentArray<UChildActorComponent*> Slots(Enemy);
 for (UChildActorComponent* Slot : Slots)
  if (Slot && Slot->GetFName() == TEXT("EnemyVisualWeapon")) Apply(Slot->GetChildActor(), false);
}

bool GetMuzzleLocation(AActor* VisualWeapon, FVector& OutLocation)
{
 if (!IsValid(VisualWeapon)) return false;
 const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(VisualWeapon->GetClass(), TEXT("MuzzleFlash"));
 const USceneComponent* Muzzle = Property
  ? Cast<USceneComponent>(Property->GetObjectPropertyValue_InContainer(VisualWeapon)) : nullptr;
 if (!IsValid(Muzzle) || Muzzle->GetOwner() != VisualWeapon) return false;
 OutLocation = Muzzle->GetComponentLocation();
 return !OutLocation.ContainsNaN();
}
}