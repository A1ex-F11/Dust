#include "WeaponShotResolution.h"

#include "Components/ActorComponent.h"
#include "Dust2PlayerCharacter.h"
#include "DustRagdollDeathComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "WeaponBallisticsData.h"
#include "WeaponRuntime.h"

namespace
{
	const FName ReceiveDamageName(TEXT("ReceiveDamage"));
	const FName DamageName(TEXT("Damage"));
	const FName ArmorPenetrationName(TEXT("ArmorPenetration"));
	const FName HeadDamageMultiplierName(TEXT("HeadDamageMultiplier"));
	const FName HitBoneNameName(TEXT("HitBoneName"));
	const FName LegacyDeadName(TEXT("bIsDead"));
	const FName LegacyIsDeadName(TEXT("IsDead"));

	struct FReceiveDamageParams
	{
		FNumericProperty* Damage = nullptr;
		FNumericProperty* ArmorPenetration = nullptr;
		FNumericProperty* HeadDamageMultiplier = nullptr;
		FNameProperty* HitBoneName = nullptr;
	};

	enum class EReceiveDamageBridgeStatus : uint8
	{
		NotImplemented,
		InvalidContract,
		Valid
	};

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsSupportedFloatProperty(const FProperty* Property)
	{
		return Property
			&& (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>());
	}

	uint64 GetParameterSignatureFlags(const FProperty* Property)
	{
		uint64 Flags = 0;
		if (Property->HasAnyPropertyFlags(CPF_Parm)) Flags |= CPF_Parm;
		if (Property->HasAnyPropertyFlags(CPF_OutParm)) Flags |= CPF_OutParm;
		if (Property->HasAnyPropertyFlags(CPF_ReturnParm)) Flags |= CPF_ReturnParm;
		if (Property->HasAnyPropertyFlags(CPF_ReferenceParm)) Flags |= CPF_ReferenceParm;
		if (Property->HasAnyPropertyFlags(CPF_ConstParm)) Flags |= CPF_ConstParm;
		return Flags;
	}

	bool ValidateReceiveDamageFunction(UFunction* Function, FReceiveDamageParams& OutParams)
	{
		OutParams = FReceiveDamageParams();
		if (!IsValid(Function))
		{
			return false;
		}

		// This bridge only supports a synchronous instance event. In particular, do not
		// route an RPC, latent call, or delegate through a manually constructed parameter frame.
		constexpr EFunctionFlags UnsupportedFlags = FUNC_Static | FUNC_Net
			| FUNC_Delegate | FUNC_MulticastDelegate | FUNC_Exec;
		if (Function->HasAnyFunctionFlags(UnsupportedFlags))
		{
			return false;
		}

		int32 ParameterCount = 0;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			++ParameterCount;
			if (Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm | CPF_ReferenceParm))
			{
				return false;
			}

			if (Property->GetFName() == DamageName)
			{
				OutParams.Damage = CastField<FNumericProperty>(Property);
				if (!IsSupportedFloatProperty(Property)) return false;
			}
			else if (Property->GetFName() == ArmorPenetrationName)
			{
				OutParams.ArmorPenetration = CastField<FNumericProperty>(Property);
				if (!IsSupportedFloatProperty(Property)) return false;
			}
			else if (Property->GetFName() == HeadDamageMultiplierName)
			{
				OutParams.HeadDamageMultiplier = CastField<FNumericProperty>(Property);
				if (!IsSupportedFloatProperty(Property)) return false;
			}
			else if (Property->GetFName() == HitBoneNameName)
			{
				OutParams.HitBoneName = CastField<FNameProperty>(Property);
				if (!OutParams.HitBoneName) return false;
			}
			else
			{
				return false;
			}
		}

		return ParameterCount == 4
			&& OutParams.Damage
			&& OutParams.ArmorPenetration
			&& OutParams.HeadDamageMultiplier
			&& OutParams.HitBoneName;
	}

	bool HaveMatchingParameterFlags(UFunction* InterfaceFunction, UFunction* TargetFunction)
	{
		constexpr uint64 SignatureMask = CPF_Parm | CPF_OutParm | CPF_ReturnParm
			| CPF_ReferenceParm | CPF_ConstParm;
		int32 InterfaceCount = 0;
		int32 TargetCount = 0;

		for (TFieldIterator<FProperty> InterfaceIt(InterfaceFunction); InterfaceIt; ++InterfaceIt)
		{
			FProperty* InterfaceProperty = *InterfaceIt;
			if (!InterfaceProperty->HasAnyPropertyFlags(CPF_Parm)) continue;
			++InterfaceCount;

			FProperty* TargetProperty = nullptr;
			for (TFieldIterator<FProperty> TargetIt(TargetFunction); TargetIt; ++TargetIt)
			{
				FProperty* Candidate = *TargetIt;
				if (Candidate->HasAnyPropertyFlags(CPF_Parm)
					&& Candidate->GetFName() == InterfaceProperty->GetFName())
				{
					TargetProperty = Candidate;
					break;
				}
			}

			if (!TargetProperty
				|| InterfaceProperty->GetClass() != TargetProperty->GetClass()
				|| (GetParameterSignatureFlags(InterfaceProperty) & SignatureMask)
					!= (GetParameterSignatureFlags(TargetProperty) & SignatureMask))
			{
				return false;
			}
		}

		for (TFieldIterator<FProperty> TargetIt(TargetFunction); TargetIt; ++TargetIt)
		{
			if ((*TargetIt)->HasAnyPropertyFlags(CPF_Parm)) ++TargetCount;
		}
		return InterfaceCount == 4 && TargetCount == InterfaceCount;
	}

	EReceiveDamageBridgeStatus FindMatchingReceiveDamage(
		AActor* Target,
		UFunction*& OutTargetFunction,
		FReceiveDamageParams& OutParams)
	{
		OutTargetFunction = nullptr;
		OutParams = FReceiveDamageParams();
		if (!IsValid(Target)) return EReceiveDamageBridgeStatus::InvalidContract;

		UClass* TargetClass = Target->GetClass();
		UFunction* TargetFunction = Target->FindFunction(ReceiveDamageName);
		if (!IsValid(TargetClass)) return EReceiveDamageBridgeStatus::InvalidContract;
		bool bFoundReceiveDamageInterface = false;

		for (UClass* Class = TargetClass; Class; Class = Class->GetSuperClass())
		{
			for (const FImplementedInterface& ImplementedInterface : Class->Interfaces)
			{
				UClass* InterfaceClass = ImplementedInterface.Class;
				if (!IsValid(InterfaceClass)
					|| !InterfaceClass->HasAnyClassFlags(CLASS_Interface)
					|| !TargetClass->ImplementsInterface(InterfaceClass))
				{
					continue;
				}

				UFunction* InterfaceFunction = InterfaceClass->FindFunctionByName(ReceiveDamageName);
				if (!IsValid(InterfaceFunction)) continue;
				bFoundReceiveDamageInterface = true;
				if (!IsValid(TargetFunction)) continue;

				FReceiveDamageParams InterfaceParams;
				FReceiveDamageParams TargetParams;
				if (!ValidateReceiveDamageFunction(InterfaceFunction, InterfaceParams)
					|| !ValidateReceiveDamageFunction(TargetFunction, TargetParams)
					|| !HaveMatchingParameterFlags(InterfaceFunction, TargetFunction))
				{
					continue;
				}

				OutTargetFunction = TargetFunction;
				OutParams = TargetParams;
				return EReceiveDamageBridgeStatus::Valid;
			}
		}
		return bFoundReceiveDamageInterface
			? EReceiveDamageBridgeStatus::InvalidContract
			: EReceiveDamageBridgeStatus::NotImplemented;
	}

	bool IsTargetDead(AActor* Target)
	{
		if (!IsValid(Target) || Target->IsActorBeingDestroyed()) return true;

		if (const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(Target))
		{
			if (Player->IsDead()) return true;
		}

		if (const UDustRagdollDeathComponent* DeathComponent =
			Target->FindComponentByClass<UDustRagdollDeathComponent>())
		{
			if (DeathComponent->IsDeathStarted()) return true;
		}

		for (const FName PropertyName : { LegacyDeadName, LegacyIsDeadName })
		{
			if (const FBoolProperty* DeadProperty = FindFProperty<FBoolProperty>(Target->GetClass(), PropertyName))
			{
				if (DeadProperty->GetPropertyValue_InContainer(Target)) return true;
			}
		}
		return false;
	}
}

FWeaponShotResolution DustWeaponHit::Resolve(
	AWeaponRuntime* Weapon,
	const FVector& TraceStart,
	const FVector& ShotDirection,
	const UWeaponBallisticsData* Ballistics)
{
	FWeaponShotResolution Result;
	if (!IsValid(Weapon) || Weapon->IsActorBeingDestroyed() || !Weapon->GetWorld())
	{
		Result.Status = EWeaponShotResolutionStatus::InvalidTrace;
		return Result;
	}
	if (!IsValid(Ballistics))
	{
		Result.Status = EWeaponShotResolutionStatus::MissingBallistics;
		return Result;
	}

	if (!IsFiniteVector(TraceStart)
		|| !IsFiniteVector(ShotDirection)
		|| !FMath::IsFinite(Ballistics->TraceDistance)
		|| Ballistics->TraceDistance < 0.0f)
	{
		Result.Status = EWeaponShotResolutionStatus::InvalidTrace;
		return Result;
	}

	const FVector SafeDirection = ShotDirection.GetSafeNormal();
	if (!IsFiniteVector(SafeDirection) || SafeDirection.IsNearlyZero())
	{
		Result.Status = EWeaponShotResolutionStatus::InvalidTrace;
		return Result;
	}

	const float Damage = Ballistics->DamagePerShot;
	const float ArmorPenetration = Ballistics->ArmorPenetration;
	const float HeadDamageMultiplier = Ballistics->HeadDamageMultiplier;
	const bool bDamageValuesValid = FMath::IsFinite(Damage) && Damage >= 0.0f
		&& FMath::IsFinite(ArmorPenetration) && ArmorPenetration >= 0.0f
		&& FMath::IsFinite(HeadDamageMultiplier) && HeadDamageMultiplier >= 0.0f;

	const FVector TraceEnd = TraceStart + SafeDirection * Ballistics->TraceDistance;
	if (!IsFiniteVector(TraceEnd))
	{
		Result.Status = EWeaponShotResolutionStatus::InvalidTrace;
		return Result;
	}

	Result.TraceStart = TraceStart;
	Result.TraceEnd = TraceEnd;
	Result.ShotEnd = TraceEnd;
	Result.ShotDirection = SafeDirection;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponShotResolution), false);
	QueryParams.AddIgnoredActor(Weapon);
	if (AActor* AttachParent = Weapon->GetAttachParentActor()) QueryParams.AddIgnoredActor(AttachParent);
	if (AActor* Owner = Weapon->GetOwner()) QueryParams.AddIgnoredActor(Owner);
	if (APawn* Instigator = Weapon->GetInstigator()) QueryParams.AddIgnoredActor(Instigator);

	FHitResult Hit;
	Result.bTracePerformed = true;
	Weapon->GetWorld()->LineTraceSingleByChannel(
		Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	Result.Hit = Hit;
	if (!Hit.bBlockingHit)
	{
		Result.Status = EWeaponShotResolutionStatus::NoBlockingHit;
		return Result;
	}
	Result.ShotEnd = Hit.ImpactPoint;

	AActor* Target = Hit.GetActor();
	if (!Target)
	{
		Result.Status = EWeaponShotResolutionStatus::NotDamageable;
		return Result;
	}
	if (IsTargetDead(Target))
	{
		Result.Status = EWeaponShotResolutionStatus::TargetDead;
		return Result;
	}

	if (!bDamageValuesValid || Damage <= 0.0f)
	{
		Result.Status = EWeaponShotResolutionStatus::NoDamage;
		return Result;
	}

	UFunction* DamageFunction = nullptr;
	FReceiveDamageParams DamageParams;
	const EReceiveDamageBridgeStatus BridgeStatus = FindMatchingReceiveDamage(
		Target, DamageFunction, DamageParams);
	if (BridgeStatus == EReceiveDamageBridgeStatus::NotImplemented)
	{
		Result.Status = EWeaponShotResolutionStatus::NotDamageable;
		return Result;
	}
	if (BridgeStatus != EReceiveDamageBridgeStatus::Valid)
	{
		Result.Status = EWeaponShotResolutionStatus::InvalidDamageContract;
		return Result;
	}

	FStructOnScope ParameterScope(DamageFunction);
	uint8* ParameterMemory = ParameterScope.GetStructMemory();
	if (!ParameterMemory)
	{
		Result.Status = EWeaponShotResolutionStatus::InvalidDamageContract;
		return Result;
	}

	DamageParams.Damage->SetFloatingPointPropertyValue(
		DamageParams.Damage->ContainerPtrToValuePtr<void>(ParameterMemory), Damage);
	DamageParams.ArmorPenetration->SetFloatingPointPropertyValue(
		DamageParams.ArmorPenetration->ContainerPtrToValuePtr<void>(ParameterMemory), ArmorPenetration);
	DamageParams.HeadDamageMultiplier->SetFloatingPointPropertyValue(
		DamageParams.HeadDamageMultiplier->ContainerPtrToValuePtr<void>(ParameterMemory), HeadDamageMultiplier);
	DamageParams.HitBoneName->SetPropertyValue(
		DamageParams.HitBoneName->ContainerPtrToValuePtr<void>(ParameterMemory), Hit.BoneName);
	if (UDustRagdollDeathComponent* DeathComponent = Target->FindComponentByClass<UDustRagdollDeathComponent>())
	{
		DeathComponent->CacheDeathImpact(Hit, SafeDirection);
	}

	// Set the local result before dispatch. ProcessEvent may synchronously destroy Target;
	// do not read Target, Weapon, or Ballistics after the call.
	Result.bHitDamageable = true;
	Result.Status = EWeaponShotResolutionStatus::DamageDispatched;
	Target->ProcessEvent(DamageFunction, ParameterMemory);
	return Result;
}
