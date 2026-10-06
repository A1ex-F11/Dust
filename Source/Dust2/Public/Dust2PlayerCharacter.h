// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Dust2PlayerCharacter.generated.h"
class AWeaponRuntime;
UCLASS()
class DUST2_API ADust2PlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADust2PlayerCharacter();
	UFUNCTION(BlueprintPure,Category="Health")
	float GetMaxHealth() const;
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const;
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const;
	UFUNCTION(BlueprintCallable,Category="Health")
	bool ApplyDamage(float Amount, bool& bDiedNow);
	UFUNCTION(BlueprintCallable,Category="Health")
	bool ApplyHeal(float Amount);
	UFUNCTION(BlueprintCallable,Category="Health")
	void RestoreHealth(float SavedHealth);
	UFUNCTION(BlueprintNativeEvent, BlueprintPure,
		Category = "Weapon|Reload")
	bool CanStartReloadRequest(AWeaponRuntime* FormalWeapon) const;
	virtual bool CanStartReloadRequest_Implementation(
		AWeaponRuntime* FormalWeapon) const;
protected:
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Health")
	float MaxHealth;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Health")
	float CurrentHealth;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Health")
	bool bDeathRequested;
};
