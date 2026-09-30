#include "Dust2PlayerCharacter.h"

ADust2PlayerCharacter::ADust2PlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	MaxHealth = 100.0f;
	CurrentHealth = 30.0f;
	bDeathRequested = false;
}
float ADust2PlayerCharacter::GetMaxHealth() const
{
	return MaxHealth;
}
float ADust2PlayerCharacter::GetCurrentHealth() const
{
	return CurrentHealth;
}
bool ADust2PlayerCharacter::IsDead() const
{
	return CurrentHealth <= 0.0f;
}
bool ADust2PlayerCharacter::ApplyDamage(float Amount, bool& bDiedNow)
{
	bDiedNow = false;
	const float PreviousHealth = CurrentHealth;
	const float SafeAmount = FMath::Max(0.0f, Amount);

	CurrentHealth = FMath::Clamp(CurrentHealth - SafeAmount, 0.0f, MaxHealth);

	const bool bHealthChanged = !FMath::IsNearlyEqual(CurrentHealth, PreviousHealth);

	bDiedNow = bHealthChanged
		&& PreviousHealth > 0.0f
		&& CurrentHealth <= 0.0f
		&& !bDeathRequested;
	if (bDiedNow)
	{
		bDeathRequested = true;
	}
	return bHealthChanged;
}
bool ADust2PlayerCharacter::ApplyHeal(float Amount)
{
	const float PreviousHealth = CurrentHealth;
	const float SafeAmount = FMath::Max(0.0f, Amount);

	if (IsDead())
	{
		return false;
	}

	CurrentHealth = FMath::Clamp(CurrentHealth + SafeAmount, 0.0f, MaxHealth);
	return !FMath::IsNearlyEqual(CurrentHealth, PreviousHealth);
}
void ADust2PlayerCharacter::RestoreHealth(float SavedHealth)
{
	CurrentHealth = FMath::Clamp(SavedHealth, 0.0f, MaxHealth);
	bDeathRequested = false;
}
