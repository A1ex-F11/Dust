#pragma once

#include "CoreMinimal.h"

class AActor;

// Applies only to transient visual-weapon instances, never to authored sound assets.
namespace DustFireAudioPolicy
{
 void Apply(AActor* VisualWeapon, bool bPlayerWeapon);
 void ApplyToEnemy(AActor* Enemy);
 bool GetMuzzleLocation(AActor* VisualWeapon, FVector& OutLocation);
}