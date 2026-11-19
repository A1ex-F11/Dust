#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Camera/CameraComponent.h"
#include "EngineGlobals.h"
#include "Data/WeaponKitRecoilData.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/WeaponKitInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Recoil/WeaponKitCameraRecoilComponent.h"
#include "Runtime/WeaponKitChildActorSlot.h"
#include "Runtime/WeaponKitInteractionScanner.h"
#include "Runtime/WeaponKitPickup.h"
#include "Runtime/WeaponKitRuntime.h"
#include "WeaponKitTestCarrier.h"
#include "WeaponKitTestWorld.h"

namespace
{
TScriptInterface<IWeaponKitSlotHost> AsSlotHost(UWeaponKitChildActorSlot* Slot)
{
	TScriptInterface<IWeaponKitSlotHost> Host;
	Host.SetObject(Slot);
	Host.SetInterface(Cast<IWeaponKitSlotHost>(Slot));
	return Host;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitAimRecoilWriterTest,
	"WeaponKit.Recoil.AimViewWriter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitAimRecoilWriterTest::RunTest(const FString& Parameter)
{
	FWeaponKitTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	if (!TestNotNull(TEXT("Transient world created"), World))
	{
		return false;
	}

	AWeaponKitTestCarrier* Carrier = World->SpawnActor<AWeaponKitTestCarrier>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Carrier"), Carrier) || !TestNotNull(TEXT("Controller"), Controller))
	{
		return false;
	}
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Possess(Carrier);
	Controller->SetControlRotation(FRotator::ZeroRotator);

	UWeaponKitCameraRecoilComponent* Recovery = NewObject<UWeaponKitCameraRecoilComponent>(Carrier);
	Recovery->RegisterComponent();
	UCameraComponent* Camera = NewObject<UCameraComponent>(Carrier);
	Camera->RegisterComponent();
	Camera->bUsePawnControlRotation = true;

	UWeaponKitRecoilData* Recoil = NewObject<UWeaponKitRecoilData>(Carrier);
	Recoil->bUseFixedPattern = true;
	Recoil->bUseIndependentShotRecoil = true;
	Recoil->AimingRecoilScale = 0.5f;
	Recoil->SmoothKickDuration = 0.0f;
	Recoil->ImmediateKickFraction = 1.0f;
	Recoil->RecoveryDelay = 100.0f;

	Recovery->SetAiming(true);
	Recovery->ApplyRecoil(2.0f, 1.0f, Recoil, Camera);
	TestTrue(TEXT("Aimed pitch uses the aim multiplier"), FMath::IsNearlyEqual(Controller->GetControlRotation().Pitch, 1.0f));
	TestTrue(TEXT("Aimed yaw uses the aim multiplier"), FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, 0.5f));
	FMinimalViewInfo View;
	Camera->GetCameraView(0.0f, View);
	TestTrue(TEXT("Pawn-controlled camera follows that view"), View.Rotation.Equals(Controller->GetControlRotation(), 0.001f));

	Recoil->AimingRecoilScale = 0.0f;
	const FRotator BeforeZero = Controller->GetControlRotation();
	Recovery->ApplyRecoil(2.0f, 1.0f, Recoil, Camera);
	TestTrue(TEXT("Zero aim scale adds no view kick"), Controller->GetControlRotation().Equals(BeforeZero));

	Recovery->SetAiming(false);
	Recoil->AimingRecoilScale = 10.0f;
	Recovery->ApplyRecoil(2.0f, 1.0f, Recoil, Camera);
	TestTrue(TEXT("Hip independent feedback does not write control rotation"), Controller->GetControlRotation().Equals(BeforeZero));
	Recovery->ClearRecoil();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitAimRecoilCleanupTest,
	"WeaponKit.Recoil.TailCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitAimRecoilCleanupTest::RunTest(const FString& Parameter)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FWeaponKitTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	if (!TestNotNull(TEXT("Transient world created"), World))
	{
		return false;
	}
	AWeaponKitTestCarrier* Carrier = World->SpawnActor<AWeaponKitTestCarrier>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Carrier"), Carrier) || !TestNotNull(TEXT("Controller"), Controller))
	{
		return false;
	}
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Possess(Carrier);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	UWeaponKitCameraRecoilComponent* Recovery = NewObject<UWeaponKitCameraRecoilComponent>(Carrier);
	Recovery->RegisterComponent();

	UWeaponKitRecoilData* Recoil = NewObject<UWeaponKitRecoilData>();
	Recoil->bUseFixedPattern = true;
	Recoil->bUseIndependentShotRecoil = true;
	Recoil->AimingRecoilScale = 1.0f;
	Recoil->SmoothKickDuration = 1.0f;
	Recoil->ImmediateKickFraction = 0.25f;
	Recoil->RecoveryDelay = 100.0f;

	Recovery->SetAiming(true);
	Recovery->ApplyRecoil(4.0f, 0.0f, Recoil, nullptr);
	TestTrue(TEXT("Only the immediate fraction is applied"), FMath::IsNearlyEqual(Controller->GetControlRotation().Pitch, 1.0f));
	Recovery->SetAiming(false);
	for (int32 Frame = 0; Frame < 25; ++Frame)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.05f);
	}
	TestTrue(TEXT("Leaving aim cancels the tail without snapping the view"), FMath::IsNearlyEqual(Controller->GetControlRotation().Pitch, 1.0f));

	Controller->SetControlRotation(FRotator::ZeroRotator);
	Recoil->SmoothKickDuration = 0.0f;
	Recoil->ImmediateKickFraction = 1.0f;
	Recoil->RecoveryDelay = 0.1f;
	Recoil->ShotRecoilRecoverySpeed = 100.0f;
	Recoil->RecoveryPitchRatio = 1.0f;
	Recovery->SetAiming(true);
	Recovery->ApplyRecoil(2.0f, 0.0f, Recoil, nullptr);
	TestTrue(TEXT("Full kick changes the view"), FMath::IsNearlyEqual(Controller->GetControlRotation().Pitch, 2.0f));
	for (int32 Frame = 0; Frame < 8; ++Frame)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.05f);
	}
	TestTrue(TEXT("Aimed view recovers after the configured delay"), FMath::IsNearlyZero(Controller->GetControlRotation().Pitch, 0.001f));

	Recoil->RecoveryDelay = 100.0f;
	Recovery->ApplyRecoil(2.0f, 0.0f, Recoil, nullptr);
	Recovery->NotifyManualLook();
	const FRotator ManualView(-3.0f, 5.0f, 0.0f);
	Controller->SetControlRotation(ManualView);
	for (int32 Frame = 0; Frame < 8; ++Frame)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.05f);
	}
	TestTrue(TEXT("Manual look is not undone by recovery"), Controller->GetControlRotation().Equals(ManualView, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitLoadoutTest,
	"WeaponKit.Inventory.LoadoutSwitchAndDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitLoadoutTest::RunTest(const FString& Parameter)
{
	FWeaponKitTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	if (!TestNotNull(TEXT("Transient world created"), World))
	{
		return false;
	}
	AWeaponKitTestCarrier* Carrier = World->SpawnActor<AWeaponKitTestCarrier>();
	if (!TestNotNull(TEXT("Carrier"), Carrier))
	{
		return false;
	}

	UWeaponKitInventoryComponent* Inventory = NewObject<UWeaponKitInventoryComponent>(Carrier);
	Inventory->RegisterComponent();
	Inventory->PickupClassByWeaponClass.Add(AWeaponKitRuntime::StaticClass(), AWeaponKitPickup::StaticClass());

	TArray<TScriptInterface<IWeaponKitSlotHost>> Hosts;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UWeaponKitChildActorSlot* Slot = NewObject<UWeaponKitChildActorSlot>(Carrier);
		Slot->RegisterComponent();
		Hosts.Add(AsSlotHost(Slot));
	}

	FWeaponKitLoadoutSave Save;
	Save.Slots.SetNum(3);
	Save.Slots[0].WeaponClass = AWeaponKitRuntime::StaticClass();
	Save.Slots[0].RuntimeState.bHasSavedState = true;
	Save.Slots[0].RuntimeState.CurrentAmmo = 4;
	Save.Slots[0].RuntimeState.ReserveAmmo = 11;
	Save.Slots[0].RuntimeState.FireMode = EWeaponKitFireMode::Burst;
	Save.Slots[0].RuntimeState.bInitialDrawConsumed = true;
	Save.Slots[1].WeaponClass = AWeaponKitRuntime::StaticClass();
	Save.Slots[1].RuntimeState.bHasSavedState = true;
	Save.Slots[1].RuntimeState.CurrentAmmo = 9;
	Save.Slots[1].RuntimeState.ReserveAmmo = 20;
	Save.Slots[1].RuntimeState.FireMode = EWeaponKitFireMode::FullAutomatic;
	Save.ActiveSlotIndex = 0;

	TestTrue(TEXT("Three-slot loadout restores from one struct"), Inventory->RestoreLoadout(Hosts, Save));
	TestTrue(TEXT("Carrier recorded the consumed draw without a character subclass"), Carrier->bDrawConsumed);
	TestTrue(TEXT("Initialization keeps the restored active slot"), Inventory->InitializeInventory(Hosts, INDEX_NONE));
	TestEqual(TEXT("Slot count is the host count"), Inventory->GetSlotCount(), 3);
	TestEqual(TEXT("Active slot is the saved index"), Inventory->GetActiveSlotIndex(), 0);
	TestNull(TEXT("Empty class stays an empty slot"), Inventory->GetSlotWeapon(2));
	AWeaponKitRuntime* FirstWeapon = Inventory->GetCurrentWeapon();
	if (!TestNotNull(TEXT("Restored current weapon"), FirstWeapon))
	{
		return false;
	}
	TestEqual(TEXT("Saved ammo restored"), FirstWeapon->GetCurrentAmmo(), 4);
	TestEqual(TEXT("Saved reserve restored"), FirstWeapon->GetReserveAmmo(), 11);
	TestTrue(TEXT("Saved fire mode restored"), FirstWeapon->GetFireMode() == EWeaponKitFireMode::Burst);

	TestTrue(TEXT("Switch moves to the next occupied slot"), Inventory->TrySwitchWeapon());
	TestEqual(TEXT("Second slot is now active"), Inventory->GetActiveSlotIndex(), 1);
	if (!TestNotNull(TEXT("Second weapon equipped"), Inventory->GetCurrentWeapon()))
	{
		return false;
	}
	TestEqual(TEXT("Second slot ammo"), Inventory->GetCurrentWeapon()->GetCurrentAmmo(), 9);

	TestTrue(TEXT("Drop requires a mapped pickup class and leaves one weapon"), Inventory->TryDropCurrent());
	TestEqual(TEXT("Drop equips the remaining weapon"), Inventory->GetActiveSlotIndex(), 0);
	TestNull(TEXT("Dropped slot is empty"), Inventory->GetSlotWeapon(1));
	TestNotNull(TEXT("First weapon is equipped again"), Inventory->GetSlotWeapon(0));

	TArray<AActor*> Pickups;
	UGameplayStatics::GetAllActorsOfClass(World, AWeaponKitPickup::StaticClass(), Pickups);
	if (!TestEqual(TEXT("One pickup was spawned"), Pickups.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("Manual pickup fills the empty slot through the pickup interface"), Inventory->TryPickup(Pickups[0], true));
	TestNotNull(TEXT("Empty slot received the weapon"), Inventory->GetSlotWeapon(1));
	TestEqual(TEXT("Picking into an empty slot keeps the current weapon"), Inventory->GetActiveSlotIndex(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponKitScannerTest,
	"WeaponKit.Scanner.ManualPickupWithoutNameLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponKitScannerTest::RunTest(const FString& Parameter)
{
	FWeaponKitTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	if (!TestNotNull(TEXT("Transient world created"), World))
	{
		return false;
	}
	AWeaponKitTestCarrier* Carrier = World->SpawnActor<AWeaponKitTestCarrier>();
	UWeaponKitInteractionScanner* Scanner = NewObject<UWeaponKitInteractionScanner>(Carrier);
	Scanner->RegisterComponent();
	AActor* PlainActor = World->SpawnActor<AActor>();
	AWeaponKitPickup* Pickup = World->SpawnActor<AWeaponKitPickup>();
	if (!TestNotNull(TEXT("Carrier"), Carrier) || !TestNotNull(TEXT("Scanner"), Scanner) ||
		!TestNotNull(TEXT("Plain actor"), PlainActor) || !TestNotNull(TEXT("Pickup"), Pickup))
	{
		return false;
	}

	TestFalse(TEXT("Actors without the pickup interface are rejected"), Scanner->RegisterPickup(PlainActor));
	Pickup->SetActorLocation(Carrier->GetActorLocation());
	FWeaponKitPickupState State;
	State.bHasSavedState = true;
	State.CurrentAmmo = 3;
	IWeaponKitPickup::Execute_InitializeDroppedWeapon(Pickup, AWeaponKitRuntime::StaticClass(), State, nullptr, 0.0f);
	TestTrue(TEXT("Interface pickup can be registered"), Scanner->RegisterPickup(Pickup));
	TestTrue(TEXT("Nearby manual pickup is selected"), Scanner->GetManualPickup() == Pickup);

	Pickup->SetActorLocation(Carrier->GetActorLocation() + FVector(5000.0f, 0.0f, 0.0f));
	TestNull(TEXT("Distance limit rejects the pickup"), Scanner->GetManualPickup());
	Pickup->SetActorLocation(Carrier->GetActorLocation());
	Carrier->bAlive = false;
	TestNull(TEXT("Dead carrier rejects the pickup"), Scanner->GetManualPickup());
	Carrier->bAlive = true;
	TestTrue(TEXT("Living carrier sees it again"), Scanner->GetManualPickup() == Pickup);
	TestNull(TEXT("Automatic pickup needs an assigned volume, not a component name"), Scanner->GetAutomaticPickup(nullptr));
	return true;
}

#endif
