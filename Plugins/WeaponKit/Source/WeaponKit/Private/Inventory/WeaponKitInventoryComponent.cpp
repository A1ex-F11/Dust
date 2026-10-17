#include "Inventory/WeaponKitInventoryComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Runtime/WeaponKitInteractionScanner.h"
#include "Runtime/WeaponKitRuntime.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeaponKitInventory, Log, All);

UWeaponKitInventoryComponent::UWeaponKitInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWeaponKitInventoryComponent::IsUsableWeaponClass(TSubclassOf<AWeaponKitRuntime> WeaponClass)
{
	UClass* Class = WeaponClass.Get();
	return Class && Class->IsChildOf(AWeaponKitRuntime::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract);
}

bool UWeaponKitInventoryComponent::IsUsablePickupActorClass(TSubclassOf<AActor> PickupClass)
{
	UClass* Class = PickupClass.Get();
	return Class && Class->IsChildOf(AActor::StaticClass()) &&
		Class->ImplementsInterface(UWeaponKitPickup::StaticClass()) &&
		!Class->HasAnyClassFlags(CLASS_Abstract);
}

bool UWeaponKitInventoryComponent::IsCarrierAlive() const
{
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return false;
	}
	if (OwnerActor->GetClass()->ImplementsInterface(UWeaponKitCarrier::StaticClass()))
	{
		return IWeaponKitCarrier::Execute_IsWeaponKitCarrierAlive(OwnerActor);
	}
	return true;
}

bool UWeaponKitInventoryComponent::HasConsumedInitialDraw(AWeaponKitRuntime* Weapon) const
{
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor) || !OwnerActor->GetClass()->ImplementsInterface(UWeaponKitCarrier::StaticClass()))
	{
		return false;
	}
	return IWeaponKitCarrier::Execute_HasConsumedWeaponInitialDraw(OwnerActor, Weapon);
}

void UWeaponKitInventoryComponent::ApplyInitialDrawConsumed(AWeaponKitRuntime* Weapon) const
{
	AActor* OwnerActor = GetOwner();
	if (IsValid(OwnerActor) && OwnerActor->GetClass()->ImplementsInterface(UWeaponKitCarrier::StaticClass()))
	{
		IWeaponKitCarrier::Execute_ApplyWeaponInitialDrawConsumed(OwnerActor, Weapon);
	}
}

bool UWeaponKitInventoryComponent::ValidateHosts(const TArray<TScriptInterface<IWeaponKitSlotHost>>& Hosts) const
{
	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor) || Hosts.Num() <= 0)
	{
		return false;
	}
	for (int32 Index = 0; Index < Hosts.Num(); ++Index)
	{
		UObject* HostObject = Hosts[Index].GetObject();
		const UActorComponent* HostComponent = Cast<UActorComponent>(HostObject);
		if (!HostObject || !HostObject->GetClass()->ImplementsInterface(UWeaponKitSlotHost::StaticClass()) ||
			!IWeaponKitSlotHost::Execute_IsSlotHostReady(HostObject) ||
			!HostComponent || HostComponent->GetOwner() != OwnerActor)
		{
			return false;
		}
		for (int32 Other = 0; Other < Index; ++Other)
		{
			if (Hosts[Other].GetObject() == HostObject)
			{
				return false;
			}
		}
	}
	return true;
}

bool UWeaponKitInventoryComponent::HostsMatch(const TArray<TScriptInterface<IWeaponKitSlotHost>>& Hosts) const
{
	if (Hosts.Num() != SlotHosts.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Hosts.Num(); ++Index)
	{
		if (Hosts[Index].GetObject() != SlotHosts[Index].GetObject())
		{
			return false;
		}
	}
	return true;
}

bool UWeaponKitInventoryComponent::InitializeInventory(
	const TArray<TScriptInterface<IWeaponKitSlotHost>>& InSlotHosts,
	int32 InitialSlotIndex)
{
	if (bInventoryBusy || bInitialized || !IsRegistered() || bCheckpointRestoreFailed || !ValidateHosts(InSlotHosts))
	{
		UE_LOG(LogWeaponKitInventory, Warning, TEXT("Inventory init rejected: busy, already initialized, unregistered, failed restore, or invalid slots."));
		return false;
	}
	if (bCheckpointLoadoutPendingInitialization && !HostsMatch(InSlotHosts))
	{
		UE_LOG(LogWeaponKitInventory, Warning, TEXT("Inventory init rejected: checkpoint slots do not match the restored hosts."));
		return false;
	}

	TArray<AWeaponKitRuntime*> Weapons;
	Weapons.SetNum(InSlotHosts.Num());
	for (int32 Index = 0; Index < InSlotHosts.Num(); ++Index)
	{
		UObject* HostObject = InSlotHosts[Index].GetObject();
		if (!IWeaponKitSlotHost::Execute_IsHostedContentValid(HostObject))
		{
			return false;
		}
		Weapons[Index] = IWeaponKitSlotHost::Execute_GetHostedWeapon(HostObject);
	}
	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		if (!IsValid(Weapons[Index]))
		{
			continue;
		}
		for (int32 Other = Index + 1; Other < Weapons.Num(); ++Other)
		{
			if (Weapons[Index] == Weapons[Other])
			{
				return false;
			}
		}
	}

	AWeaponKitRuntime* InitialWeapon = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	if (bCheckpointLoadoutPendingInitialization)
	{
		InitialWeapon = CurrentWeapon.Get();
	}
	else if (InitialSlotIndex != INDEX_NONE)
	{
		if (!InSlotHosts.IsValidIndex(InitialSlotIndex) || !IsValid(Weapons[InitialSlotIndex]))
		{
			return false;
		}
		InitialWeapon = Weapons[InitialSlotIndex];
		ResolvedIndex = InitialSlotIndex;
	}
	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	SlotHosts = InSlotHosts;
	if (!bCheckpointLoadoutPendingInitialization)
	{
		CurrentWeapon = InitialWeapon;
		ActiveSlotIndex = ResolvedIndex;
	}
	else if (IsValid(InitialWeapon))
	{
		ActiveSlotIndex = FindSlotIndex(InitialWeapon);
	}
	bInitialized = true;
	BroadcastInventoryChanged();
	bCheckpointLoadoutPendingInitialization = false;
	bCheckpointRestoreFailed = false;
	if (bExternalRestorePresentationPending)
	{
		bExternalRestorePresentationPending = false;
		if (IsValid(CurrentWeapon.Get()))
		{
			TGuardValue<bool> PresentationRefreshGuard(bEquippedPresentationRefreshRequested, true);
			OnEquippedWeaponChanged.Broadcast(CurrentWeapon.Get());
		}
	}
	return true;
}

bool UWeaponKitInventoryComponent::PrepareForExternalRestore()
{
	if (bInventoryBusy)
	{
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	if (AWeaponKitRuntime* OldWeapon = GetCurrentWeapon())
	{
		CancelCurrentWeaponActions(OldWeapon, true);
	}
	CurrentWeapon.Reset();
	ActiveSlotIndex = INDEX_NONE;
	SlotHosts.Reset();
	bInitialized = false;
	bExternalRestorePresentationPending = true;
	bCheckpointLoadoutPendingInitialization = false;
	bCheckpointRestoreFailed = false;
	return true;
}

bool UWeaponKitInventoryComponent::RestoreLoadout(
	const TArray<TScriptInterface<IWeaponKitSlotHost>>& InSlotHosts,
	const FWeaponKitLoadoutSave& Save)
{
	if (bInventoryBusy || !IsRegistered() || !ValidateHosts(InSlotHosts) || Save.Slots.Num() != InSlotHosts.Num() ||
		(Save.ActiveSlotIndex != INDEX_NONE && !InSlotHosts.IsValidIndex(Save.ActiveSlotIndex)))
	{
		UE_LOG(LogWeaponKitInventory, Warning, TEXT("Loadout restore rejected: invalid hosts or save."));
		bCheckpointRestoreFailed = true;
		return false;
	}
	for (const FWeaponKitSlotSave& SlotSave : Save.Slots)
	{
		if (SlotSave.WeaponClass && !IsUsableWeaponClass(SlotSave.WeaponClass))
		{
			bCheckpointRestoreFailed = true;
			return false;
		}
	}
	if (!PrepareForExternalRestore())
	{
		bCheckpointRestoreFailed = true;
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	TArray<AWeaponKitRuntime*> RestoredWeapons;
	RestoredWeapons.SetNum(InSlotHosts.Num());
	for (int32 Index = 0; Index < InSlotHosts.Num(); ++Index)
	{
		const FWeaponKitSlotSave& SlotSave = Save.Slots[Index];
		if (!SlotSave.WeaponClass)
		{
			IWeaponKitSlotHost::Execute_ClearHostedWeapon(InSlotHosts[Index].GetObject());
			continue;
		}
		if (!CreateWeaponInSlot(InSlotHosts[Index], SlotSave.WeaponClass, SlotSave.RuntimeState, RestoredWeapons[Index]))
		{
			for (const TScriptInterface<IWeaponKitSlotHost>& Host : InSlotHosts)
			{
				IWeaponKitSlotHost::Execute_ClearHostedWeapon(Host.GetObject());
			}
			bExternalRestorePresentationPending = false;
			bCheckpointRestoreFailed = true;
			UE_LOG(LogWeaponKitInventory, Error, TEXT("Loadout restore failed while rebuilding slot %d."), Index);
			return false;
		}
	}

	AWeaponKitRuntime* RestoredCurrent = Save.ActiveSlotIndex == INDEX_NONE ? nullptr : RestoredWeapons[Save.ActiveSlotIndex];
	for (AWeaponKitRuntime* Weapon : RestoredWeapons)
	{
		SetWeaponActorActive(Weapon, Weapon == RestoredCurrent);
	}
	SlotHosts = InSlotHosts;
	CurrentWeapon = RestoredCurrent;
	ActiveSlotIndex = IsValid(RestoredCurrent) ? Save.ActiveSlotIndex : INDEX_NONE;
	bCheckpointLoadoutPendingInitialization = true;
	return true;
}

AWeaponKitRuntime* UWeaponKitInventoryComponent::GetCurrentWeapon() const
{
	return CurrentWeapon.Get();
}

AWeaponKitRuntime* UWeaponKitInventoryComponent::GetSlotWeapon(int32 SlotIndex) const
{
	if (!SlotHosts.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}
	UObject* HostObject = SlotHosts[SlotIndex].GetObject();
	if (!HostObject)
	{
		return nullptr;
	}
	AWeaponKitRuntime* Weapon = IWeaponKitSlotHost::Execute_GetHostedWeapon(HostObject);
	return IsValid(Weapon) ? Weapon : nullptr;
}

FWeaponKitInventorySnapshot UWeaponKitInventoryComponent::GetSnapshot() const
{
	FWeaponKitInventorySnapshot Snapshot;
	Snapshot.ActiveSlotIndex = ActiveSlotIndex;
	Snapshot.CurrentWeapon = GetCurrentWeapon();
	Snapshot.SlotWeapons.Reserve(SlotHosts.Num());
	for (int32 Index = 0; Index < SlotHosts.Num(); ++Index)
	{
		Snapshot.SlotWeapons.Add(GetSlotWeapon(Index));
	}
	return Snapshot;
}

bool UWeaponKitInventoryComponent::IsOperational() const
{
	return bInitialized && IsRegistered() && IsCarrierAlive() && ValidateHosts(SlotHosts);
}

bool UWeaponKitInventoryComponent::ReadSlotWeapons(TArray<AWeaponKitRuntime*>& OutWeapons) const
{
	OutWeapons.Reset();
	if (!IsOperational())
	{
		return false;
	}
	OutWeapons.SetNum(SlotHosts.Num());
	for (int32 Index = 0; Index < SlotHosts.Num(); ++Index)
	{
		UObject* HostObject = SlotHosts[Index].GetObject();
		if (!IWeaponKitSlotHost::Execute_IsHostedContentValid(HostObject))
		{
			return false;
		}
		OutWeapons[Index] = IWeaponKitSlotHost::Execute_GetHostedWeapon(HostObject);
	}
	for (int32 Index = 0; Index < OutWeapons.Num(); ++Index)
	{
		if (!IsValid(OutWeapons[Index]))
		{
			continue;
		}
		for (int32 Other = Index + 1; Other < OutWeapons.Num(); ++Other)
		{
			if (OutWeapons[Index] == OutWeapons[Other])
			{
				return false;
			}
		}
	}
	return true;
}

int32 UWeaponKitInventoryComponent::FindSlotIndex(const AWeaponKitRuntime* Weapon) const
{
	if (!IsValid(Weapon))
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < SlotHosts.Num(); ++Index)
	{
		if (GetSlotWeapon(Index) == Weapon)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UWeaponKitInventoryComponent::FindNextOccupiedSlot(int32 FromIndex) const
{
	if (SlotHosts.Num() == 0)
	{
		return INDEX_NONE;
	}
	for (int32 Step = 1; Step <= SlotHosts.Num(); ++Step)
	{
		const int32 Index = (FromIndex + Step + SlotHosts.Num() * 2) % SlotHosts.Num();
		if (IsValid(GetSlotWeapon(Index)))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UWeaponKitInventoryComponent::CountOccupiedSlots() const
{
	int32 Count = 0;
	for (int32 Index = 0; Index < SlotHosts.Num(); ++Index)
	{
		if (IsValid(GetSlotWeapon(Index)))
		{
			++Count;
		}
	}
	return Count;
}

void UWeaponKitInventoryComponent::CancelCurrentWeaponActions(AWeaponKitRuntime* OldWeapon, bool bNotify)
{
	if (!IsValid(OldWeapon))
	{
		return;
	}
	OldWeapon->CancelReload();
	if (bNotify)
	{
		OnBeforeWeaponChange.Broadcast(OldWeapon);
	}
}

void UWeaponKitInventoryComponent::SetWeaponActorActive(AWeaponKitRuntime* Weapon, bool bActive) const
{
	if (IsValid(Weapon))
	{
		Weapon->SetActorHiddenInGame(!bActive);
	}
}

void UWeaponKitInventoryComponent::CommitCurrentWeapon(AWeaponKitRuntime* NewWeapon, bool bRefreshPresentation)
{
	AWeaponKitRuntime* OldWeapon = CurrentWeapon.Get();
	if (IsValid(OldWeapon) && OldWeapon != NewWeapon)
	{
		SetWeaponActorActive(OldWeapon, false);
	}
	CurrentWeapon = NewWeapon;
	ActiveSlotIndex = FindSlotIndex(NewWeapon);
	for (int32 Index = 0; Index < SlotHosts.Num(); ++Index)
	{
		AWeaponKitRuntime* SlotWeapon = GetSlotWeapon(Index);
		if (SlotWeapon != NewWeapon)
		{
			SetWeaponActorActive(SlotWeapon, false);
		}
	}
	SetWeaponActorActive(NewWeapon, true);
	BroadcastInventoryChanged();
	if (IsValid(NewWeapon))
	{
		TGuardValue<bool> PresentationRefreshGuard(bEquippedPresentationRefreshRequested, bRefreshPresentation);
		OnEquippedWeaponChanged.Broadcast(NewWeapon);
	}
}

void UWeaponKitInventoryComponent::BroadcastInventoryChanged()
{
	OnInventoryChanged.Broadcast(GetSnapshot());
}

bool UWeaponKitInventoryComponent::CreateWeaponInSlot(
	const TScriptInterface<IWeaponKitSlotHost>& Slot,
	TSubclassOf<AWeaponKitRuntime> WeaponClass,
	const FWeaponKitPickupState& State,
	AWeaponKitRuntime*& OutWeapon)
{
	OutWeapon = nullptr;
	UObject* HostObject = Slot.GetObject();
	if (!HostObject || !IWeaponKitSlotHost::Execute_IsSlotHostReady(HostObject) || !IsUsableWeaponClass(WeaponClass))
	{
		return false;
	}
	if (!IWeaponKitSlotHost::Execute_SpawnHostedWeapon(HostObject, WeaponClass))
	{
		IWeaponKitSlotHost::Execute_ClearHostedWeapon(HostObject);
		return false;
	}

	AWeaponKitRuntime* CreatedWeapon = IWeaponKitSlotHost::Execute_GetHostedWeapon(HostObject);
	if (!IsValid(CreatedWeapon))
	{
		IWeaponKitSlotHost::Execute_ClearHostedWeapon(HostObject);
		return false;
	}

	SetWeaponActorActive(CreatedWeapon, false);
	CreatedWeapon->RestorePickupRuntimeState(State);
	if (State.bHasSavedState && State.bInitialDrawConsumed)
	{
		ApplyInitialDrawConsumed(CreatedWeapon);
	}
	if (!IsValid(CreatedWeapon) || IWeaponKitSlotHost::Execute_GetHostedWeapon(HostObject) != CreatedWeapon)
	{
		IWeaponKitSlotHost::Execute_ClearHostedWeapon(HostObject);
		return false;
	}
	OutWeapon = CreatedWeapon;
	return true;
}

AActor* UWeaponKitInventoryComponent::SpawnStagedPickup(
	TSubclassOf<AWeaponKitRuntime> WeaponClass,
	const FWeaponKitPickupState& State) const
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(OwnerActor) || !World || !IsUsableWeaponClass(WeaponClass))
	{
		return nullptr;
	}
	const TSubclassOf<AActor>* PickupClass = PickupClassByWeaponClass.Find(WeaponClass);
	if (!PickupClass || !IsUsablePickupActorClass(*PickupClass))
	{
		return nullptr;
	}

	const FVector Forward = OwnerActor->GetActorForwardVector().GetSafeNormal();
	const FVector SpawnLocation = OwnerActor->GetActorLocation() +
		Forward * FMath::Max(0.0f, DropForwardDistance) +
		FVector::UpVector * FMath::Max(0.0f, DropUpOffset);
	const FTransform SpawnTransform(OwnerActor->GetActorRotation(), SpawnLocation);
	APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	AActor* Pickup = World->SpawnActorDeferred<AActor>(
		PickupClass->Get(),
		SpawnTransform,
		OwnerActor,
		OwnerPawn,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
	if (!IsValid(Pickup) || !Pickup->GetClass()->ImplementsInterface(UWeaponKitPickup::StaticClass()))
	{
		return nullptr;
	}

	IWeaponKitPickup::Execute_InitializeDroppedWeapon(
		Pickup,
		WeaponClass,
		State,
		OwnerPawn,
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

void UWeaponKitInventoryComponent::LaunchStagedPickup(AActor* Pickup) const
{
	if (!IsValid(Pickup))
	{
		return;
	}
	Pickup->SetActorHiddenInGame(false);
	Pickup->SetActorEnableCollision(true);
	if (Pickup->GetClass()->ImplementsInterface(UWeaponKitPickup::StaticClass()))
	{
		const FVector Forward = IsValid(GetOwner()) ? GetOwner()->GetActorForwardVector().GetSafeNormal() : FVector::ForwardVector;
		IWeaponKitPickup::Execute_LaunchDroppedWeapon(Pickup, Forward);
	}
}

void UWeaponKitInventoryComponent::ConsumePickupSource(AActor* Source)
{
	if (!IsValid(Source))
	{
		return;
	}
	if (IsValid(InteractionScanner))
	{
		InteractionScanner->UnregisterPickup(Source);
	}
	Source->Destroy();
}

void UWeaponKitInventoryComponent::ReleasePickupClaim(AActor* Source) const
{
	if (IsValid(Source) && Source->GetClass()->ImplementsInterface(UWeaponKitPickup::StaticClass()))
	{
		IWeaponKitPickup::Execute_ReleasePickupClaim(Source);
	}
}

bool UWeaponKitInventoryComponent::TryPickup(AActor* Source, bool bAllowReplaceCurrent)
{
	if (bInventoryBusy)
	{
		return false;
	}

	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsOperational() || !IsValid(Pawn) || !IsValid(Source) || Source->IsActorBeingDestroyed() ||
		!Source->GetClass()->ImplementsInterface(UWeaponKitPickup::StaticClass()))
	{
		return false;
	}

	TArray<AWeaponKitRuntime*> SlotWeapons;
	if (!ReadSlotWeapons(SlotWeapons))
	{
		return false;
	}
	AWeaponKitRuntime* Current = GetCurrentWeapon();
	if (IsValid(Current) && FindSlotIndex(Current) == INDEX_NONE)
	{
		return false;
	}

	int32 EmptySlotIndex = INDEX_NONE;
	for (int32 Index = 0; Index < SlotWeapons.Num(); ++Index)
	{
		if (!IsValid(SlotWeapons[Index]))
		{
			EmptySlotIndex = Index;
			break;
		}
	}
	if (EmptySlotIndex == INDEX_NONE && !bAllowReplaceCurrent)
	{
		return false;
	}
	if (!IWeaponKitPickup::Execute_CanBePickedUpBy(Source, Pawn, !bAllowReplaceCurrent))
	{
		return false;
	}

	const TSubclassOf<AWeaponKitRuntime> IncomingClass = IWeaponKitPickup::Execute_GetPickupWeaponClass(Source);
	if (!IsUsableWeaponClass(IncomingClass) || !IWeaponKitPickup::Execute_TryClaimPickup(Source, Pawn, !bAllowReplaceCurrent))
	{
		return false;
	}
	const FWeaponKitPickupState IncomingState = IWeaponKitPickup::Execute_GetPickupSavedState(Source);
	if (EmptySlotIndex == INDEX_NONE)
	{
		return TryReplaceCurrentFromPickup(Source, IncomingClass, IncomingState);
	}

	AWeaponKitRuntime* IncomingWeapon = nullptr;
	if (!CreateWeaponInSlot(SlotHosts[EmptySlotIndex], IncomingClass, IncomingState, IncomingWeapon))
	{
		ReleasePickupClaim(Source);
		return false;
	}
	if (IsValid(GetCurrentWeapon()))
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

bool UWeaponKitInventoryComponent::TryEquipSlot(int32 SlotIndex)
{
	return TryEquipWeapon(GetSlotWeapon(SlotIndex));
}

bool UWeaponKitInventoryComponent::TryEquipWeapon(AWeaponKitRuntime* NewWeapon)
{
	if (bInventoryBusy)
	{
		return false;
	}
	TGuardValue<bool> BusyGuard(bInventoryBusy, true);
	if (!IsOperational() || !IsValid(NewWeapon) || FindSlotIndex(NewWeapon) == INDEX_NONE)
	{
		return false;
	}
	AWeaponKitRuntime* OldWeapon = GetCurrentWeapon();
	if (OldWeapon == NewWeapon)
	{
		return true;
	}
	if (IsValid(OldWeapon) && FindSlotIndex(OldWeapon) == INDEX_NONE)
	{
		return false;
	}
	CancelCurrentWeaponActions(OldWeapon, true);
	CommitCurrentWeapon(NewWeapon);
	return true;
}

bool UWeaponKitInventoryComponent::TrySwitchWeapon()
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
	AWeaponKitRuntime* Current = GetCurrentWeapon();
	const int32 CurrentIndex = FindSlotIndex(Current);
	if (!IsValid(Current) || CurrentIndex == INDEX_NONE)
	{
		return false;
	}
	const int32 NextIndex = FindNextOccupiedSlot(CurrentIndex);
	AWeaponKitRuntime* NextWeapon = GetSlotWeapon(NextIndex);
	if (!IsValid(NextWeapon) || NextWeapon == Current)
	{
		return false;
	}
	Current->CancelReload();
	CommitCurrentWeapon(NextWeapon, false);
	return true;
}

bool UWeaponKitInventoryComponent::TryDropCurrent()
{
	if (bInventoryBusy)
	{
		return false;
	}

	AActor* DroppedPickup = nullptr;
	{
		TGuardValue<bool> BusyGuard(bInventoryBusy, true);
		APawn* Pawn = Cast<APawn>(GetOwner());
		if (!IsOperational() || !IsValid(Pawn) || CountOccupiedSlots() < 2)
		{
			return false;
		}
		AWeaponKitRuntime* Current = GetCurrentWeapon();
		const int32 CurrentIndex = FindSlotIndex(Current);
		if (!IsValid(Current) || CurrentIndex == INDEX_NONE)
		{
			return false;
		}
		const FWeaponKitPickupState DropState = Current->CapturePickupRuntimeState(HasConsumedInitialDraw(Current));
		if (!DropState.bHasSavedState)
		{
			return false;
		}
		DroppedPickup = SpawnStagedPickup(Current->GetClass(), DropState);
		if (!IsValid(DroppedPickup))
		{
			return false;
		}

		const int32 OtherIndex = FindNextOccupiedSlot(CurrentIndex);
		AWeaponKitRuntime* OtherWeapon = GetSlotWeapon(OtherIndex);
		CancelCurrentWeaponActions(Current, true);
		IWeaponKitSlotHost::Execute_ClearHostedWeapon(SlotHosts[CurrentIndex].GetObject());
		CommitCurrentWeapon(OtherWeapon);
		LaunchStagedPickup(DroppedPickup);
	}

	if (IsValid(InteractionScanner) && IsValid(DroppedPickup))
	{
		if (AActor* NearbyPickup = InteractionScanner->GetAutomaticPickup(DroppedPickup))
		{
			TryPickup(NearbyPickup, false);
		}
	}
	return true;
}

bool UWeaponKitInventoryComponent::TryReplaceCurrentFromPickup(
	AActor* Source,
	TSubclassOf<AWeaponKitRuntime> IncomingClass,
	const FWeaponKitPickupState& IncomingState)
{
	TArray<AWeaponKitRuntime*> SlotWeapons;
	if (!ReadSlotWeapons(SlotWeapons))
	{
		ReleasePickupClaim(Source);
		return false;
	}
	AWeaponKitRuntime* OldWeapon = GetCurrentWeapon();
	const int32 OldIndex = FindSlotIndex(OldWeapon);
	if (!IsValid(OldWeapon) || OldIndex == INDEX_NONE)
	{
		ReleasePickupClaim(Source);
		return false;
	}

	const FWeaponKitPickupState OldState = OldWeapon->CapturePickupRuntimeState(HasConsumedInitialDraw(OldWeapon));
	if (!OldState.bHasSavedState)
	{
		ReleasePickupClaim(Source);
		return false;
	}
	AActor* StagedOldPickup = SpawnStagedPickup(OldWeapon->GetClass(), OldState);
	if (!IsValid(StagedOldPickup))
	{
		ReleasePickupClaim(Source);
		return false;
	}

	const TSubclassOf<AWeaponKitRuntime> OldClass = OldWeapon->GetClass();
	CancelCurrentWeaponActions(OldWeapon, true);
	AWeaponKitRuntime* IncomingWeapon = nullptr;
	if (CreateWeaponInSlot(SlotHosts[OldIndex], IncomingClass, IncomingState, IncomingWeapon))
	{
		CommitCurrentWeapon(IncomingWeapon);
		LaunchStagedPickup(StagedOldPickup);
		ConsumePickupSource(Source);
		return true;
	}

	AWeaponKitRuntime* RestoredOldWeapon = nullptr;
	if (CreateWeaponInSlot(SlotHosts[OldIndex], OldClass, OldState, RestoredOldWeapon))
	{
		CommitCurrentWeapon(RestoredOldWeapon);
		StagedOldPickup->Destroy();
		ReleasePickupClaim(Source);
		return false;
	}

	IWeaponKitSlotHost::Execute_ClearHostedWeapon(SlotHosts[OldIndex].GetObject());
	const int32 OtherIndex = FindNextOccupiedSlot(OldIndex);
	CommitCurrentWeapon(GetSlotWeapon(OtherIndex));
	LaunchStagedPickup(StagedOldPickup);
	ReleasePickupClaim(Source);
	UE_LOG(LogWeaponKitInventory, Error, TEXT("Could not create the incoming weapon or restore the replaced weapon; the old weapon remains on the ground."));
	return false;
}

void UWeaponKitInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bInventoryBusy = true;
	if (AWeaponKitRuntime* Weapon = GetCurrentWeapon())
	{
		CancelCurrentWeaponActions(Weapon, false);
	}
	CurrentWeapon.Reset();
	ActiveSlotIndex = INDEX_NONE;
	SlotHosts.Reset();
	bInitialized = false;
	bExternalRestorePresentationPending = false;
	bCheckpointLoadoutPendingInitialization = false;
	bCheckpointRestoreFailed = false;
	Super::EndPlay(EndPlayReason);
}
