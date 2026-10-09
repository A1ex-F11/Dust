#include "WeaponInventoryComponent.h"

#include "Components/ChildActorComponent.h"
#include "Dust2PlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Templates/UnrealTemplate.h"
#include "WeaponPickupActor.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeaponInventoryComponent, Log, All);

namespace
{
	bool IsUsableWeaponClass(TSubclassOf<AWeaponRuntime> WeaponClass)
	{
		UClass* Class = WeaponClass.Get();
		return Class && Class->IsChildOf(AWeaponRuntime::StaticClass()) &&
			!Class->HasAnyClassFlags(CLASS_Abstract);
	}

	bool IsUsablePickupClass(TSubclassOf<AWeaponPickupActor> PickupClass)
	{
		UClass* Class = PickupClass.Get();
		return Class && Class->IsChildOf(AWeaponPickupActor::StaticClass()) &&
			!Class->HasAnyClassFlags(CLASS_Abstract);
	}
}

UWeaponInventoryComponent::UWeaponInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWeaponInventoryComponent::InitializeNativeInventory(
	UChildActorComponent* Slot1,
	UChildActorComponent* Slot2,
	AWeaponRuntime* InitialCurrent)
{
	if (bInventoryBusy || bInitialized || !IsRegistered())
	{
		UE_LOG(LogWeaponInventoryComponent, Warning,
			TEXT("Inventory init rejected: busy, already initialized, or unregistered component."));
		return false;
	}

	ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
	if (!IsValid(Player) || !IsValid(Slot1) || !IsValid(Slot2) || Slot1 == Slot2 ||
		!Slot1->IsRegistered() || !Slot2->IsRegistered() ||
		Slot1->GetOwner() != Player || Slot2->GetOwner() != Player ||
		!OnInventoryChanged.IsBound() || !OnBeforeWeaponChange.IsBound() ||
		!OnEquippedWeaponChanged.IsBound())
	{
		UE_LOG(LogWeaponInventoryComponent, Warning,
			TEXT("Inventory init requires two registered player-owned slots and all three bound delegates."));
		return false;
	}

	AWeaponRuntime* Slot1Weapon = Cast<AWeaponRuntime>(Slot1->GetChildActor());
	AWeaponRuntime* Slot2Weapon = Cast<AWeaponRuntime>(Slot2->GetChildActor());
	if ((IsValid(Slot1->GetChildActor()) && !IsValid(Slot1Weapon)) ||
		(IsValid(Slot2->GetChildActor()) && !IsValid(Slot2Weapon)) ||
		(IsValid(Slot1Weapon) && Slot1Weapon == Slot2Weapon) ||
		(InitialCurrent && (!IsValid(InitialCurrent) ||
			(InitialCurrent != Slot1Weapon && InitialCurrent != Slot2Weapon))))
	{
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	Slot1Component = Slot1;
	Slot2Component = Slot2;
	CurrentWeapon = InitialCurrent;
	bInitialized = true;
	BroadcastInventoryChanged();
	return true;
}

bool UWeaponInventoryComponent::PrepareForExternalRestore()
{
	if (bInventoryBusy)
	{
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	AWeaponRuntime* OldWeapon = GetCurrentWeaponNative();
	if (IsValid(OldWeapon))
	{
		CancelCurrentWeaponActions(OldWeapon, true);
	}

	CurrentWeapon.Reset();
	Slot1Component = nullptr;
	Slot2Component = nullptr;
	bInitialized = false;
	return true;
}

AWeaponRuntime* UWeaponInventoryComponent::GetCurrentWeaponNative() const
{
	return CurrentWeapon.Get();
}

AWeaponRuntime* UWeaponInventoryComponent::GetSlotWeaponNative(int32 SlotIndex) const
{
	UChildActorComponent* Slot = GetSlotComponent(SlotIndex);
	AWeaponRuntime* Weapon = IsValid(Slot)
		? Cast<AWeaponRuntime>(Slot->GetChildActor())
		: nullptr;
	return IsValid(Weapon) ? Weapon : nullptr;
}

bool UWeaponInventoryComponent::IsInventoryBusy() const
{
	return bInventoryBusy;
}

bool UWeaponInventoryComponent::ShouldRefreshEquippedPresentation() const
{
	return bEquippedPresentationRefreshRequested;
}

bool UWeaponInventoryComponent::TryPickupNative(
	AWeaponPickupActor* Source,
	bool bAllowReplaceCurrent)
{
	if (bInventoryBusy)
	{
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	if (!IsOperational())
	{
		return false;
	}

	ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || !IsValid(Source) ||
		Source->IsActorBeingDestroyed())
	{
		return false;
	}

	AWeaponRuntime* Slot1Weapon = nullptr;
	AWeaponRuntime* Slot2Weapon = nullptr;
	if (!ReadSlotWeapons(Slot1Weapon, Slot2Weapon))
	{
		return false;
	}

	AWeaponRuntime* Current = GetCurrentWeaponNative();
	if (IsValid(Current) && Current != Slot1Weapon && Current != Slot2Weapon)
	{
		return false;
	}
	if (bAllowReplaceCurrent && Player->GetNearbyWeaponPickup() != Source)
	{
		return false;
	}
	if (!Source->CanBePickedUpBy(Player, !bAllowReplaceCurrent))
	{
		return false;
	}
	if (!bAllowReplaceCurrent && IsValid(Slot1Weapon) && IsValid(Slot2Weapon))
	{
		return false;
	}

	const TSubclassOf<AWeaponRuntime> IncomingClass = Source->GetPickupWeaponClass();
	if (!IsUsableWeaponClass(IncomingClass) ||
		!Source->TryClaimPickup(Player, !bAllowReplaceCurrent))
	{
		return false;
	}

	const FWeaponPickupState IncomingState = Source->GetPickupSavedState();
	if (IsValid(Slot1Weapon) && IsValid(Slot2Weapon))
	{
		return TryReplaceCurrentFromPickup(Source, IncomingClass, IncomingState);
	}

	const int32 EmptySlotIndex = IsValid(Slot1Weapon) ? 1 : 0;
	UChildActorComponent* EmptySlot = GetSlotComponent(EmptySlotIndex);
	AWeaponRuntime* IncomingWeapon = nullptr;
	if (!CreateWeaponInSlot(EmptySlot, IncomingClass, IncomingState, IncomingWeapon))
	{
		ReleasePickupClaim(Source);
		return false;
	}

	Current = GetCurrentWeaponNative();
	if (IsValid(Current))
	{
		SetWeaponActorActive(IncomingWeapon, false);
		BroadcastInventoryChanged();
	}
	else
	{
		CommitCurrentWeapon(IncomingWeapon);
	}

	ConsumePickupSource(Source);
	return true;
}

bool UWeaponInventoryComponent::TryEquipWeaponNative(AWeaponRuntime* NewWeapon)
{
	if (bInventoryBusy)
	{
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	if (!IsOperational() || !IsValid(NewWeapon))
	{
		return false;
	}

	AWeaponRuntime* Slot1Weapon = nullptr;
	AWeaponRuntime* Slot2Weapon = nullptr;
	if (!ReadSlotWeapons(Slot1Weapon, Slot2Weapon) ||
		(NewWeapon != Slot1Weapon && NewWeapon != Slot2Weapon))
	{
		return false;
	}

	AWeaponRuntime* OldWeapon = GetCurrentWeaponNative();
	if (OldWeapon == NewWeapon)
	{
		return true;
	}
	if (IsValid(OldWeapon) && OldWeapon != Slot1Weapon && OldWeapon != Slot2Weapon)
	{
		return false;
	}

	CancelCurrentWeaponActions(OldWeapon, true);
	CommitCurrentWeapon(NewWeapon);
	return true;
}

bool UWeaponInventoryComponent::TrySwitchWeaponNative()
{
	if (bInventoryBusy)
	{
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	if (!IsOperational())
	{
		return false;
	}

	AWeaponRuntime* Slot1Weapon = nullptr;
	AWeaponRuntime* Slot2Weapon = nullptr;
	if (!ReadSlotWeapons(Slot1Weapon, Slot2Weapon))
	{
		return false;
	}

	AWeaponRuntime* Current = GetCurrentWeaponNative();
	if (!IsValid(Current) || (Current != Slot1Weapon && Current != Slot2Weapon))
	{
		return false;
	}

	AWeaponRuntime* OtherWeapon = Current == Slot1Weapon ? Slot2Weapon : Slot1Weapon;
	if (!IsValid(OtherWeapon) || OtherWeapon == Current)
	{
		return false;
	}

	// The existing child SwitchWeapon owns Holster and the delayed visual commit.
	// Do not stop it or request Draw again from this formal-state commit.
	Current->CancelReload();
	CommitCurrentWeapon(OtherWeapon, false);
	return true;
}

bool UWeaponInventoryComponent::TryDropCurrentNative()
{
	if (bInventoryBusy)
	{
		return false;
	}

	AWeaponPickupActor* DroppedPickup = nullptr;
	ADust2PlayerCharacter* Player = nullptr;
	{
		TGuardValue<bool> BusyGuard(bInventoryBusy, true);
		if (!IsOperational())
		{
			return false;
		}

		Player = Cast<ADust2PlayerCharacter>(GetOwner());
		if (!IsValid(Player) || Player->IsDead())
		{
			return false;
		}

		AWeaponRuntime* Slot1Weapon = nullptr;
		AWeaponRuntime* Slot2Weapon = nullptr;
		if (!ReadSlotWeapons(Slot1Weapon, Slot2Weapon) ||
			!IsValid(Slot1Weapon) || !IsValid(Slot2Weapon) ||
			Slot1Weapon == Slot2Weapon)
		{
			return false;
		}

		AWeaponRuntime* Current = GetCurrentWeaponNative();
		if (!IsValid(Current) || (Current != Slot1Weapon && Current != Slot2Weapon))
		{
			return false;
		}

		const FWeaponPickupState DropState =
			Current->CapturePickupRuntimeState(Player->HasConsumedPickupInitialDraw(Current));
		if (!DropState.bHasSavedState)
		{
			return false;
		}

		DroppedPickup = SpawnStagedPickup(Current->GetClass(), DropState);
		if (!IsValid(DroppedPickup))
		{
			return false;
		}

		UChildActorComponent* CurrentSlot = GetSlotComponent(
			Current == Slot1Weapon ? 0 : 1);
		AWeaponRuntime* OtherWeapon = Current == Slot1Weapon ? Slot2Weapon : Slot1Weapon;
		CancelCurrentWeaponActions(Current, true);
		CurrentSlot->SetChildActorClass(nullptr);
		CommitCurrentWeapon(OtherWeapon);
		LaunchStagedPickup(DroppedPickup);
	}

	// Query after the transaction guard ends so the dropped actor can be excluded
	// before the shared pickup transaction checks nearby candidates.
	if (IsValid(Player) && IsValid(DroppedPickup))
	{
		AWeaponPickupActor* NearbyPickup = Cast<AWeaponPickupActor>(
			Player->GetNearbyWeaponPickupForAutoFill(DroppedPickup));
		if (IsValid(NearbyPickup))
		{
			TryPickupNative(NearbyPickup, false);
		}
	}
	return true;
}

void UWeaponInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bInventoryBusy = true;
	if (AWeaponRuntime* Weapon = GetCurrentWeaponNative())
	{
		CancelCurrentWeaponActions(Weapon, false);
	}
	CurrentWeapon.Reset();
	Slot1Component = nullptr;
	Slot2Component = nullptr;
	bInitialized = false;
	Super::EndPlay(EndPlayReason);
}

bool UWeaponInventoryComponent::IsOperational() const
{
	const ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
	return bInitialized && IsRegistered() && IsValid(Player) &&
		!Player->IsDead() &&
		IsValid(Slot1Component) && IsValid(Slot2Component) &&
		Slot1Component->IsRegistered() && Slot2Component->IsRegistered() &&
		Slot1Component->GetOwner() == Player && Slot2Component->GetOwner() == Player;
}

bool UWeaponInventoryComponent::ReadSlotWeapons(
	AWeaponRuntime*& OutSlot1,
	AWeaponRuntime*& OutSlot2) const
{
	OutSlot1 = nullptr;
	OutSlot2 = nullptr;
	if (!IsOperational())
	{
		return false;
	}

	AActor* Slot1Actor = Slot1Component->GetChildActor();
	AActor* Slot2Actor = Slot2Component->GetChildActor();
	OutSlot1 = Cast<AWeaponRuntime>(Slot1Actor);
	OutSlot2 = Cast<AWeaponRuntime>(Slot2Actor);
	if ((IsValid(Slot1Actor) && !IsValid(OutSlot1)) ||
		(IsValid(Slot2Actor) && !IsValid(OutSlot2)) ||
		(IsValid(OutSlot1) && OutSlot1 == OutSlot2))
	{
		return false;
	}
	return true;
}

UChildActorComponent* UWeaponInventoryComponent::GetSlotComponent(int32 SlotIndex) const
{
	if (SlotIndex == 0)
	{
		return Slot1Component;
	}
	if (SlotIndex == 1)
	{
		return Slot2Component;
	}
	return nullptr;
}

void UWeaponInventoryComponent::CancelCurrentWeaponActions(
	AWeaponRuntime* OldWeapon,
	bool bNotifyBlueprint)
{
	if (!IsValid(OldWeapon))
	{
		return;
	}
	OldWeapon->CancelReload();

	if (bNotifyBlueprint && IsValid(OldWeapon))
	{
		OnBeforeWeaponChange.Broadcast(OldWeapon);
	}
}

void UWeaponInventoryComponent::SetWeaponActorActive(
	AWeaponRuntime* Weapon,
	bool bActive) const
{
	if (!IsValid(Weapon))
	{
		return;
	}
	Weapon->SetActorHiddenInGame(!bActive);
}

void UWeaponInventoryComponent::CommitCurrentWeapon(
	AWeaponRuntime* NewWeapon,
	bool bRefreshPresentation)
{
	AWeaponRuntime* OldWeapon = CurrentWeapon.Get();
	if (IsValid(OldWeapon) && OldWeapon != NewWeapon)
	{
		SetWeaponActorActive(OldWeapon, false);
	}
	CurrentWeapon = NewWeapon;
	AWeaponRuntime* Slot1Weapon = GetSlotWeaponNative(0);
	AWeaponRuntime* Slot2Weapon = GetSlotWeaponNative(1);
	if (Slot1Weapon != NewWeapon)
	{
		SetWeaponActorActive(Slot1Weapon, false);
	}
	if (Slot2Weapon != NewWeapon)
	{
		SetWeaponActorActive(Slot2Weapon, false);
	}
	SetWeaponActorActive(NewWeapon, true);
	BroadcastInventoryChanged();
	if (IsValid(NewWeapon))
	{
		TGuardValue<bool> PresentationRefreshGuard(
			bEquippedPresentationRefreshRequested, bRefreshPresentation);
		OnEquippedWeaponChanged.Broadcast(NewWeapon);
	}
}

void UWeaponInventoryComponent::BroadcastInventoryChanged()
{
	OnInventoryChanged.Broadcast(
		GetSlotWeaponNative(0),
		GetSlotWeaponNative(1),
		GetCurrentWeaponNative());
}

bool UWeaponInventoryComponent::CreateWeaponInSlot(
	UChildActorComponent* Slot,
	TSubclassOf<AWeaponRuntime> WeaponClass,
	const FWeaponPickupState& State,
	AWeaponRuntime*& OutWeapon)
{
	OutWeapon = nullptr;
	if (!IsValid(Slot) || !Slot->IsRegistered() || !IsUsableWeaponClass(WeaponClass))
	{
		return false;
	}

	Slot->SetChildActorClass(nullptr);
	Slot->SetChildActorClass(WeaponClass);
	AWeaponRuntime* CreatedWeapon = Cast<AWeaponRuntime>(Slot->GetChildActor());
	if (!IsValid(CreatedWeapon))
	{
		Slot->SetChildActorClass(nullptr);
		return false;
	}

	SetWeaponActorActive(CreatedWeapon, false);
	CreatedWeapon->RestorePickupRuntimeState(State);
	if (State.bHasSavedState && State.bInitialDrawConsumed)
	{
		if (ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner()))
		{
			Player->ApplyPickupInitialDrawConsumed(CreatedWeapon);
		}
	}

	if (!IsValid(CreatedWeapon) || Slot->GetChildActor() != CreatedWeapon)
	{
		Slot->SetChildActorClass(nullptr);
		return false;
	}

	OutWeapon = CreatedWeapon;
	return true;
}

AWeaponPickupActor* UWeaponInventoryComponent::SpawnStagedPickup(
	TSubclassOf<AWeaponRuntime> WeaponClass,
	const FWeaponPickupState& State) const
{
	ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!IsValid(Player) || !World || !IsUsableWeaponClass(WeaponClass))
	{
		return nullptr;
	}

	const TSubclassOf<AWeaponPickupActor>* PickupClass =
		PickupClassByWeaponClass.Find(WeaponClass);
	if (!PickupClass || !IsUsablePickupClass(*PickupClass))
	{
		return nullptr;
	}

	const FVector Forward = Player->GetActorForwardVector().GetSafeNormal();
	const FVector SpawnLocation = Player->GetActorLocation() +
		Forward * FMath::Max(0.0f, DropForwardDistance) +
		FVector::UpVector * FMath::Max(0.0f, DropUpOffset);
	const FTransform SpawnTransform(Player->GetActorRotation(), SpawnLocation);
	AWeaponPickupActor* Pickup = World->SpawnActorDeferred<AWeaponPickupActor>(
		PickupClass->Get(),
		SpawnTransform,
		Player,
		Player,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
	if (!IsValid(Pickup))
	{
		return nullptr;
	}

	Pickup->InitializeDroppedWeapon(
		WeaponClass,
		State,
		Player,
		FMath::Max(0.0f, AutoPickupBlockSeconds));
	Pickup->SetActorHiddenInGame(true);
	Pickup->FinishSpawning(SpawnTransform);
	if (!IsValid(Pickup))
	{
		return nullptr;
	}

	Pickup->SetActorHiddenInGame(true);
	Pickup->SetActorEnableCollision(false);
	return Pickup;
}

void UWeaponInventoryComponent::LaunchStagedPickup(AWeaponPickupActor* Pickup) const
{
	if (!IsValid(Pickup))
	{
		return;
	}

	Pickup->SetActorHiddenInGame(false);
	Pickup->SetActorEnableCollision(true);
	if (const AActor* OwnerActor = GetOwner())
	{
		Pickup->LaunchDroppedWeapon(OwnerActor->GetActorForwardVector().GetSafeNormal());
	}
}

void UWeaponInventoryComponent::ConsumePickupSource(AWeaponPickupActor* Source)
{
	if (!IsValid(Source))
	{
		return;
	}
	if (ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner()))
	{
		Player->UnregisterNearbyWeaponPickup(Source);
	}
	Source->Destroy();
}

void UWeaponInventoryComponent::ReleasePickupClaim(AWeaponPickupActor* Source) const
{
	if (IsValid(Source))
	{
		Source->ReleasePickupClaim();
	}
}

bool UWeaponInventoryComponent::TryReplaceCurrentFromPickup(
	AWeaponPickupActor* Source,
	TSubclassOf<AWeaponRuntime> IncomingClass,
	const FWeaponPickupState& IncomingState)
{
	ADust2PlayerCharacter* Player = Cast<ADust2PlayerCharacter>(GetOwner());
	AWeaponRuntime* Slot1Weapon = nullptr;
	AWeaponRuntime* Slot2Weapon = nullptr;
	if (!IsValid(Player) || !ReadSlotWeapons(Slot1Weapon, Slot2Weapon))
	{
		ReleasePickupClaim(Source);
		return false;
	}

	AWeaponRuntime* OldWeapon = GetCurrentWeaponNative();
	if (!IsValid(OldWeapon) ||
		(OldWeapon != Slot1Weapon && OldWeapon != Slot2Weapon))
	{
		ReleasePickupClaim(Source);
		return false;
	}

	const FWeaponPickupState OldState = OldWeapon->CapturePickupRuntimeState(
		Player->HasConsumedPickupInitialDraw(OldWeapon));
	if (!OldState.bHasSavedState)
	{
		ReleasePickupClaim(Source);
		return false;
	}

	AWeaponPickupActor* StagedOldPickup =
		SpawnStagedPickup(OldWeapon->GetClass(), OldState);
	if (!IsValid(StagedOldPickup))
	{
		ReleasePickupClaim(Source);
		return false;
	}

	UChildActorComponent* ReplacementSlot =
		GetSlotComponent(OldWeapon == Slot1Weapon ? 0 : 1);
	const TSubclassOf<AWeaponRuntime> OldClass = OldWeapon->GetClass();
	CancelCurrentWeaponActions(OldWeapon, true);

	AWeaponRuntime* IncomingWeapon = nullptr;
	if (CreateWeaponInSlot(
		ReplacementSlot, IncomingClass, IncomingState, IncomingWeapon))
	{
		CommitCurrentWeapon(IncomingWeapon);
		LaunchStagedPickup(StagedOldPickup);
		ConsumePickupSource(Source);
		return true;
	}

	AWeaponRuntime* RestoredOldWeapon = nullptr;
	if (CreateWeaponInSlot(ReplacementSlot, OldClass, OldState, RestoredOldWeapon))
	{
		CommitCurrentWeapon(RestoredOldWeapon);
		StagedOldPickup->Destroy();
		ReleasePickupClaim(Source);
		return false;
	}

	ReplacementSlot->SetChildActorClass(nullptr);
	AWeaponRuntime* OtherWeapon = OldWeapon == Slot1Weapon ? Slot2Weapon : Slot1Weapon;
	CommitCurrentWeapon(OtherWeapon);
	LaunchStagedPickup(StagedOldPickup);
	ReleasePickupClaim(Source);
	UE_LOG(
		LogWeaponInventoryComponent,
		Error,
		TEXT("Could not create the incoming weapon or restore the replaced weapon; the old weapon remains on the ground."));
	return false;
}
bool UWeaponInventoryComponent::SyncLegacyWeaponReferences()
{
	AActor* Player = GetOwner();
	if (!bInitialized || !IsValid(Player))
	{
		return false;
	}

	const FName Names[] = {
		TEXT("WeaponSlot1Ref"), TEXT("WeaponSlot2Ref"), TEXT("WeaponRef")
	};
	AWeaponRuntime* Values[] = {
		GetSlotWeaponNative(0), GetSlotWeaponNative(1), GetCurrentWeaponNative()
	};
	FObjectPropertyBase* Properties[3] = {};
	// Validate all mirrors before writing any of them.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Properties[Index] = FindFProperty<FObjectPropertyBase>(Player->GetClass(), Names[Index]);
		FObjectPropertyBase* Property = Properties[Index];
		if (!Property || Property->ArrayDim != 1 || !Property->PropertyClass ||
			!Property->PropertyClass->IsChildOf(AWeaponRuntime::StaticClass()) ||
			(Values[Index] && !Values[Index]->IsA(Property->PropertyClass)))
		{
			UE_LOG(LogWeaponInventoryComponent, Warning,
				TEXT("Inventory mirror sync rejected: incompatible or missing property %s on %s."),
				*Names[Index].ToString(), *Player->GetClass()->GetName());
			return false;
		}
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Properties[Index]->SetObjectPropertyValue_InContainer(Player, Values[Index]);
	}
	return true;
}
