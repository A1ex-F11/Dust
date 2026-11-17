#include "Combat/DustEnemyHealthComponent.h"

#include "DamageLibrary.h"
#include "DustActorState.h"

void UDustEnemyHealthComponent::InitializeNativeEnemyHealth(float InitialHealth, bool bInHasHelmet, bool bInHasBodyArmor)
{
	if (bInitialized) return;
	if (!ensureMsgf(FMath::IsFinite(InitialHealth) && InitialHealth > 0.0f,
		TEXT("%s: Initialize Native Enemy Health needs positive finite health (got %g)."),
		*GetPathName(), InitialHealth)) return;
	if (!ensureMsgf(OnNativeEnemyHealthChanged.IsBound(),
		TEXT("%s: Connect On Native Enemy Health Changed to the enemy death graph before initializing."),
		*GetPathName())) return;

	CurrentHealth = InitialHealth;
	bHasHelmet = bInHasHelmet;
	bHasBodyArmor = bInHasBodyArmor;
	bInitialized = true;
}

void UDustEnemyHealthComponent::KillFromEnvironment()
{
	if (!bInitialized || IsDead() || DustActorState::IsDead(GetOwner())) return;

	CurrentHealth = 0.0f;
	OnNativeEnemyHealthChanged.Broadcast(CurrentHealth, true);
}

float UDustEnemyHealthComponent::ApplyNativeEnemyDamage(float Damage, FName HitBoneName, float ArmorPenetration,
	float HeadDamageMultiplier, bool& bKilledNow)
{
	bKilledNow = false;
	if (!bInitialized || IsDead() || DustActorState::IsDead(GetOwner())) return 0.0f;
	if (!FMath::IsFinite(Damage) || Damage <= 0.0f
		|| !FMath::IsFinite(ArmorPenetration) || ArmorPenetration < 0.0f
		|| !FMath::IsFinite(HeadDamageMultiplier) || HeadDamageMultiplier < 0.0f) return 0.0f;

	const float FinalDamage = UDamageLibrary::CalculateFinalDamage(
		Damage, HitBoneName, ArmorPenetration, HeadDamageMultiplier, bHasHelmet, bHasBodyArmor);
	if (!FMath::IsFinite(FinalDamage)) return 0.0f;

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, PreviousHealth - FinalDamage);
	const float AppliedDamage = PreviousHealth - CurrentHealth;
	if (AppliedDamage <= 0.0f) return 0.0f;

	// Commit before the Blueprint event, which can reenter damage handling or destroy the owner.
	bKilledNow = CurrentHealth <= 0.0f;
	OnNativeEnemyHealthChanged.Broadcast(CurrentHealth, bKilledNow);
	return AppliedDamage;
}
