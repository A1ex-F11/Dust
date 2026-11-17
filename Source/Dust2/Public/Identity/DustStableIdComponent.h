#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustStableIdComponent.generated.h"

class AActor;

/**
 * One small, optional identity component shared by placed doors, checkpoints,
 * enemies and pickups. It stores only an FName identity.
 *
 * Editor placement generates a persistent id. Copy/paste in a level generates a
 * new id, but PIE duplication keeps the serialized id. Runtime spawns never
 * generate an id automatically: a deferred spawner writes the entry id before
 * FinishSpawning through the deferred tag below.
 */
UCLASS(ClassGroup=(Dust2), meta=(BlueprintSpawnableComponent, DisplayName="Dust Stable Id"))
class DUST2_API UDustStableIdComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustStableIdComponent();

	/** Hand-editable identity. Leave empty on a Blueprint class default. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="稳定ID", meta=(DisplayName="Stable ID"))
	FName StableId;

	/** Prefix used only when the editor generates a missing id. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="稳定ID", meta=(DisplayName="ID Prefix"))
	FName IdPrefix = TEXT("Dust");

	UFUNCTION(BlueprintPure, Category="稳定ID", meta=(DisplayName="Native Get Stable Id"))
	FName NativeGetStableId() const;

	UFUNCTION(BlueprintPure, Category="稳定ID", meta=(DisplayName="Native Has Stable Id"))
	bool NativeHasStableId() const;

	/** Deferred spawners and editor repair use this; it does not generate. */
	UFUNCTION(BlueprintCallable, Category="稳定ID", meta=(DisplayName="Native Set Stable Id"))
	void NativeSetStableId(FName InStableId);

	/** C++ lookup used by the Tracker, pickups and doors. */
	static FName ResolveStableIdForActor(AActor* Actor);

	/** Explicit editor repair: only fills a missing id. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="稳定ID", meta=(DisplayName="Native Ensure Stable Id"))
	bool NativeEnsureStableId();

	/** Explicit editor repair: always writes a new id. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="稳定ID", meta=(DisplayName="Native Regenerate Stable Id"))
	bool NativeRegenerateStableId();

	/** Generates a new globally-unique FName. Also used by spawn-entry repair. */
	static FName GenerateStableId(FName Prefix);

	/** Marks a deferred actor's entry id before FinishSpawning. */
	static void SetDeferredStableId(AActor* Actor, FName InStableId);

	/** Finds or creates the component on a runtime-spawned preset actor. */
	static UDustStableIdComponent* EnsureStableComponent(AActor* Actor, FName InStableId);

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;

private:
	static FName ReadDeferredStableId(const AActor* Actor);
	static void ClearDeferredStableId(AActor* Actor);
	bool CanRepairInEditor() const;
	FName GenerateUniqueId() const;
};
