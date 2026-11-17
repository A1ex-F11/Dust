#include "Identity/DustStableIdComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Guid.h"

DEFINE_LOG_CATEGORY_STATIC(LogDustStableId, Log, All);

namespace
{
	const TCHAR* DeferredIdTagPrefix = TEXT("DustStableId:");
}

UDustStableIdComponent::UDustStableIdComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FName UDustStableIdComponent::GenerateStableId(FName Prefix)
{
	const FString PrefixString = Prefix.IsNone() ? TEXT("Dust") : Prefix.ToString();
	return FName(*FString::Printf(TEXT("%s_%s"), *PrefixString,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}

FName UDustStableIdComponent::ReadDeferredStableId(const AActor* Actor)
{
	if (!IsValid(Actor)) return NAME_None;
	const FString Prefix(DeferredIdTagPrefix);
	for (const FName& Tag : Actor->Tags)
	{
		const FString TagString = Tag.ToString();
		if (TagString.StartsWith(Prefix))
		{
			const FString Value = TagString.RightChop(Prefix.Len());
			return Value.IsEmpty() ? NAME_None : FName(*Value);
		}
	}
	return NAME_None;
}

void UDustStableIdComponent::ClearDeferredStableId(AActor* Actor)
{
	if (!IsValid(Actor)) return;
	const FString Prefix(DeferredIdTagPrefix);
	Actor->Tags.RemoveAll([&Prefix](const FName& Tag)
	{
		return Tag.ToString().StartsWith(Prefix);
	});
}

void UDustStableIdComponent::SetDeferredStableId(AActor* Actor, FName InStableId)
{
	if (!IsValid(Actor) || InStableId.IsNone()) return;
	ClearDeferredStableId(Actor);
	Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("%s%s"), DeferredIdTagPrefix,
		*InStableId.ToString())));
}

FName UDustStableIdComponent::ResolveStableIdForActor(AActor* Actor)
{
	if (!IsValid(Actor)) return NAME_None;

	// A deferred entry id must win over any stale Blueprint class default.
	const FName DeferredId = ReadDeferredStableId(Actor);
	if (!DeferredId.IsNone()) return DeferredId;

	if (const UDustStableIdComponent* Component = Actor->FindComponentByClass<UDustStableIdComponent>())
	{
		return Component->StableId;
	}
	return NAME_None;
}

FName UDustStableIdComponent::NativeGetStableId() const
{
	if (const AActor* Owner = GetOwner())
	{
		const FName DeferredId = ReadDeferredStableId(Owner);
		if (!DeferredId.IsNone()) return DeferredId;
	}
	return StableId;
}

bool UDustStableIdComponent::NativeHasStableId() const
{
	return !NativeGetStableId().IsNone();
}

void UDustStableIdComponent::NativeSetStableId(FName InStableId)
{
	if (HasAnyFlags(RF_ClassDefaultObject) ||
		(GetOwner() && GetOwner()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		return;
	}

	StableId = InStableId;
	if (AActor* Owner = GetOwner())
	{
		ClearDeferredStableId(Owner);
	}
}

bool UDustStableIdComponent::CanRepairInEditor() const
{
	if (HasAnyFlags(RF_ClassDefaultObject)) return false;

	const AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAnyFlags(RF_ClassDefaultObject)) return false;

	const UWorld* World = GetWorld();
	return World && World->IsEditorWorld();
}

FName UDustStableIdComponent::GenerateUniqueId() const
{
	return GenerateStableId(IdPrefix);
}

bool UDustStableIdComponent::NativeEnsureStableId()
{
	if (HasAnyFlags(RF_ClassDefaultObject) ||
		(GetOwner() && GetOwner()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		return false;
	}
	if (!StableId.IsNone()) return true;

	if (AActor* Owner = GetOwner())
	{
		const FName DeferredId = ReadDeferredStableId(Owner);
		if (!DeferredId.IsNone())
		{
			StableId = DeferredId;
			ClearDeferredStableId(Owner);
			return true;
		}
	}

	if (CanRepairInEditor())
	{
		Modify();
	}
	else
	{
		UE_LOG(LogDustStableId, Warning,
			TEXT("Explicit stable-id repair on %s outside an editor world; no automatic runtime generation exists."),
			*GetNameSafe(GetOwner()));
	}

	StableId = GenerateUniqueId();
	return true;
}

bool UDustStableIdComponent::NativeRegenerateStableId()
{
	if (HasAnyFlags(RF_ClassDefaultObject) ||
		(GetOwner() && GetOwner()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		return false;
	}
	if (CanRepairInEditor())
	{
		Modify();
	}
	else
	{
		UE_LOG(LogDustStableId, Warning,
			TEXT("Explicit stable-id regeneration on %s outside an editor world."),
			*GetNameSafe(GetOwner()));
	}

	if (AActor* Owner = GetOwner())
	{
		ClearDeferredStableId(Owner);
	}
	StableId = GenerateUniqueId();
	return true;
}

UDustStableIdComponent* UDustStableIdComponent::EnsureStableComponent(AActor* Actor, FName InStableId)
{
	if (!IsValid(Actor) || InStableId.IsNone() || Actor->HasAnyFlags(RF_ClassDefaultObject)) return nullptr;

	if (UDustStableIdComponent* Existing = Actor->FindComponentByClass<UDustStableIdComponent>())
	{
		Existing->NativeSetStableId(InStableId);
		return Existing;
	}

	UDustStableIdComponent* Component = NewObject<UDustStableIdComponent>(Actor);
	Component->StableId = InStableId;
	Actor->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

void UDustStableIdComponent::OnRegister()
{
	Super::OnRegister();

	// Editor placement/load fills missing ids. PIE and Game worlds never enter here.
	// Do not open an undo transaction while a level is loading; an explicit repair
	// button uses Modify() instead.
	if (CanRepairInEditor() && StableId.IsNone())
	{
		StableId = GenerateUniqueId();
		MarkPackageDirty();
	}
}

void UDustStableIdComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return;

	// A deferred spawner may have written the entry id before FinishSpawning.
	// Adopting it here keeps runtime identity unchanged and clears the marker.
	const FName DeferredId = ReadDeferredStableId(Owner);
	if (!DeferredId.IsNone())
	{
		StableId = DeferredId;
		ClearDeferredStableId(Owner);
	}
}

void UDustStableIdComponent::PostEditImport()
{
	Super::PostEditImport();

	if (!CanRepairInEditor()) return;
	if (StableId.IsNone())
	{
		NativeEnsureStableId();
	}
	else
	{
		// Alt-drag / copy-paste must not leave two actors with the same identity.
		NativeRegenerateStableId();
	}
}

void UDustStableIdComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// PIE world duplication and reload keep the serialized id.
	if (bDuplicateForPIE || !CanRepairInEditor()) return;
	if (StableId.IsNone())
	{
		NativeEnsureStableId();
	}
	else
	{
		NativeRegenerateStableId();
	}
}
