#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ShotTracerGeometry.h"
#include "ShotTracerComponent.h"
#include <limits>
#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotTracerGeometryTest, "Dust2.Tracer.FlightAndSegmentBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotTracerGeometryTest::RunTest(const FString& Parameter)
{
	float Distance, Duration, Length;
	TestTrue(TEXT("Normal flight"), FShotTracerGeometry::BuildFlight(FVector::ZeroVector,
		FVector(3000,0,0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestTrue(TEXT("Flight duration"), FMath::IsNearlyEqual(Duration, 0.1f));
	TestTrue(TEXT("Long shot"), FShotTracerGeometry::BuildFlight(FVector::ZeroVector,
		FVector(100000,0,0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestTrue(TEXT("Cosmetic duration capped"), FMath::IsNearlyEqual(Duration, 0.15f));
	for (float Age : {0.0f, 0.00001f, 0.05f, 0.15f, 1.0f})
	{
		const FVector2D Segment = FShotTracerGeometry::Segment(Distance, Duration, Length, Age);
		TestTrue(TEXT("Tail never behind muzzle"), Segment.X - Segment.Y * 0.5 >= -0.01);
		TestTrue(TEXT("Head never past impact"), Segment.X + Segment.Y * 0.5 <= Distance + 0.01);
		TestTrue(TEXT("Visible segment length bounded"), Segment.Y >= 0 && Segment.Y <= Length + 0.01);
	}
	TestTrue(TEXT("Close hit"), FShotTracerGeometry::BuildFlight(FVector::ZeroVector,
		FVector(2,0,0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestEqual(TEXT("Length clipped for close hit"), Length, 2.0f);
	TestTrue(TEXT("One-frame minimum cosmetic duration"), Duration >= 1.0f / 60.0f);
	TestFalse(TEXT("Zero distance rejected"), FShotTracerGeometry::BuildFlight(FVector::ZeroVector,
		FVector::ZeroVector, 30000, 0.15f, 80, Distance, Duration, Length));
	TestFalse(TEXT("Invalid speed rejected"), FShotTracerGeometry::BuildFlight(FVector::ZeroVector,
		FVector(10,0,0), -1, 0.15f, 80, Distance, Duration, Length));
	TestFalse(TEXT("Nonfinite endpoint rejected"), FShotTracerGeometry::BuildFlight(FVector::ZeroVector,
		FVector(std::numeric_limits<double>::infinity(),0,0), 30000, 0.15f, 80, Distance, Duration, Length));
	TestEqual(TEXT("Failed flight output cleared"), Distance, 0.0f);
	UShotTracerComponent* Component = NewObject<UShotTracerComponent>();
	TestFalse(TEXT("Unconfigured component fails safely"), Component->PlayShotTracer(FVector::ZeroVector, FVector(100,0,0)));
	Component->ClearShotTracers();
	Component->ClearShotTracers();
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotTracerAssetFlightTest, "Dust2.Tracer.SavedAssetFlightAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotTracerAssetFlightTest::RunTest(const FString& Parameter)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Code/VFX/Tracer/NS_ShotTracer.NS_ShotTracer"));
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Saved tracer asset"), System) || !TestNotNull(TEXT("Editor world"), World)) return false;
	TestTrue(TEXT("System ready to run"), System->IsReadyToRun());
	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Temporary simulation owner"), Owner)) return false;
	UNiagaraComponent* Effect = NewObject<UNiagaraComponent>(Owner);
	Effect->SetAutoActivate(false);
	Effect->SetAutoDestroy(false);
	Effect->SetAsset(System);
	Effect->RegisterComponent();
	Effect->SetVariableFloat(TEXT("User.TracerDistance"), 3000.0f);
	Effect->SetVariableFloat(TEXT("User.TracerLifetime"), 0.15f);
	Effect->SetVariableFloat(TEXT("User.TracerLength"), 80.0f);
	Effect->SetVariableFloat(TEXT("User.TracerWidth"), 1.2f);
	Effect->Activate(true);
	Effect->AdvanceSimulation(3, 0.01f);
	auto Controller = Effect->GetSystemInstanceController();
	FNiagaraSystemInstance* Instance = Controller ? Controller->GetSystemInstance_Unsafe() : nullptr;
	if (TestNotNull(TEXT("Live Niagara system instance"), Instance) && TestEqual(TEXT("One emitter"), Instance->GetEmitters().Num(), 1))
	{
		FNiagaraEmitterInstance& Emitter = *Instance->GetEmitters()[0];
		TestEqual(TEXT("One cosmetic particle"), Emitter.GetNumParticles(), 1);
		if (Emitter.GetNumParticles() == 1)
		{
			const auto& Data = Emitter.GetParticleData();
			for (const FNiagaraVariableBase& Variable : Data.GetCompiledData().Variables) AddInfo(FString::Printf(TEXT("Runtime variable %s (%s)"), *Variable.GetName().ToString(), *Variable.GetType().GetName()));
			float Age = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("Age")).GetSafe(0, -1.0f);
			FNiagaraPosition Position = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("Position")).GetSafe(0, FNiagaraPosition(-1, -1, -1));
			FVector2f Size = FNiagaraDataSetAccessor<FVector2f>::CreateReader(Data, TEXT("SpriteSize")).GetSafe(0, FVector2f(-1));
			AddInfo(FString::Printf(TEXT("Simulated age=%f position=%s size=%s"), Age, *Position.ToString(), *Size.ToString()));
			TestTrue(TEXT("Age advances once per frame"), Age > 0.0f && Age <= 0.035f);
			TestTrue(TEXT("Head stays before impact"), Position.X + Size.Y * 0.5f <= 3000.01f);
			TestTrue(TEXT("Tail stays past muzzle"), Position.X - Size.Y * 0.5f >= -0.01f);
			TestTrue(TEXT("Streak length respects requested limit"), Size.Y > 0.0f && Size.Y <= 80.01f);
		}
	}
	Effect->AdvanceSimulation(80, 0.01f);
	if (Instance) AddInfo(FString::Printf(TEXT("Final system age=%f state=%s"), Instance->GetAge(), *UEnum::GetValueAsString(Instance->GetActualExecutionState())));
	TestTrue(TEXT("One-shot system completes"), Effect->IsComplete());
	Effect->DestroyComponent();
	World->DestroyActor(Owner);
	return true;
}
#endif
#endif
