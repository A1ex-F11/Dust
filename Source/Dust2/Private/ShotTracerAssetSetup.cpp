// Editor-only, one-shot setup utility for the Dust2 shot tracer.
// Console commands:
//   Dust2.BuildTracerAssets  - create M_ShotTracerGlow and NS_ShotTracer (refuses overwrite)
//   Dust2.VerifyTracerAssets - read-only verification of saved assets

#if WITH_EDITOR

#include "ShotTracerComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "NiagaraNodeCustomHlsl.h"
#include "UObject/UObjectHash.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Misc/PackageName.h"
#include "NiagaraCommon.h"
#include "NiagaraConstants.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraStackFunctionInputBinder.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraTypes.h"
#include "ShaderCompiler.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace Dust2ShotTracerAssetSetup
{
	static const TCHAR* const TracerFolder = TEXT("/Game/Code/VFX/Tracer");
	static const TCHAR* const MaterialName = TEXT("M_ShotTracerGlow");
	static const TCHAR* const SystemName = TEXT("NS_ShotTracer");
	static const TCHAR* const MinimalEmitterPath = TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal");
	static const float DefaultTracerLifetime = 0.15f;
	static const float EmitterLoopDuration = 0.20f;

	static FString MakePackageName(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s"), TracerFolder, AssetName);
	}

	static FString MakeObjectPath(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), TracerFolder, AssetName, AssetName);
	}

	static bool HasPackageFile(const FString& PackageName)
	{
		return FPackageName::DoesPackageExist(PackageName);
	}

	static void AddExpression(UMaterial* Material, UMaterialExpression* Expression, int32 X, int32 Y)
	{
		Expression->Material = Material;
		Expression->MaterialExpressionEditorX = X;
		Expression->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(Expression);
	}

	static UMaterial* BuildTracerMaterial(UMaterial* Existing = nullptr)
	{
		UMaterial* Material = Existing ? Existing : NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->bUsedWithNiagaraSprites = true;
		Material->bDisableDepthTest = false;
		Material->bUseMaterialAttributes = false;

		UMaterialExpressionParticleColor* ParticleColor = NewObject<UMaterialExpressionParticleColor>(Material);
		AddExpression(Material, ParticleColor, -500, -100);

		UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
		UV->CoordinateIndex = 0;
		AddExpression(Material, UV, -500, 250);

		// Alpha blending preserves a colored body against bright backgrounds.
		// Sprite alignment follows its Y axis, so X is the width of the streak.
		UMaterialExpressionCustom* Body = NewObject<UMaterialExpressionCustom>(Material);
		Body->Description = TEXT("Tracer orange body and pale core v2");
		Body->OutputType = CMOT_Float3;
		Body->Code = TEXT("float across = abs(UV.x * 2.0 - 1.0);\nfloat core = 1.0 - smoothstep(0.12, 0.42, across);\nfloat3 body = max(Color.rgb, 0.0) * float3(0.85, 0.28, 0.04);\nreturn lerp(body, float3(3.0, 2.2, 1.25), core);");
		FCustomInput BodyUV;
		BodyUV.InputName = TEXT("UV");
		BodyUV.Input.Expression = UV;
		Body->Inputs.Add(BodyUV);
		FCustomInput BodyColor;
		BodyColor.InputName = TEXT("Color");
		BodyColor.Input.Expression = ParticleColor;
		Body->Inputs.Add(BodyColor);
		AddExpression(Material, Body, -180, -100);
		Material->GetEditorOnlyData()->EmissiveColor.Expression = Body;

		UMaterialExpressionCustom* EdgeFade = NewObject<UMaterialExpressionCustom>(Material);
		EdgeFade->Description = TEXT("Tracer soft opaque body v2");
		EdgeFade->OutputType = CMOT_Float1;
		EdgeFade->Code = TEXT("float2 q = abs(UV * 2.0 - 1.0);\nfloat sides = 1.0 - smoothstep(0.65, 1.0, q.x);\nfloat ends = 1.0 - smoothstep(0.75, 1.0, q.y);\nreturn saturate(0.92 * sides * ends * Alpha);");
		FCustomInput UVInput;
		UVInput.InputName = TEXT("UV");
		UVInput.Input.Expression = UV;
		EdgeFade->Inputs.Add(UVInput);
		FCustomInput AlphaInput;
		AlphaInput.InputName = TEXT("Alpha");
		AlphaInput.Input.Expression = ParticleColor;
		AlphaInput.Input.OutputIndex = 4;
		EdgeFade->Inputs.Add(AlphaInput);
		AddExpression(Material, EdgeFade, -180, 250);
		Material->GetEditorOnlyData()->Opacity.Expression = EdgeFade;

		Material->PostEditChange();
		Material->ForceRecompileForRendering();
		if (GShaderCompilingManager != nullptr)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		return Material;
	}

	static UNiagaraNodeOutput* FindOutput(UNiagaraScript* Script, ENiagaraScriptUsage Usage)
	{
		if (Script == nullptr)
		{
			return nullptr;
		}
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		if (Source && Source->NodeGraph)
		{
			for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
			{
				UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node);
				if (Output && Output->GetUsage() == Usage && Output->GetUsageId() == Script->GetUsageId()) return Output;
			}
		}
		return nullptr;
	}

	static bool AddAssignment(
		UNiagaraScript* Script,
		ENiagaraScriptUsage Usage,
		const TArray<FNiagaraVariable>& Variables,
		const TArray<FString>& Expressions)
	{
		if (Variables.Num() != Expressions.Num())
		{
			return false;
		}
		UNiagaraNodeOutput* Output = FindOutput(Script, Usage);
		if (Output == nullptr)
		{
			return false;
		}

		TArray<FString> Defaults;
		Defaults.Reserve(Variables.Num());
		for (const FNiagaraVariable& Variable : Variables)
		{
			Defaults.Add(FNiagaraConstants::GetAttributeDefaultValue(Variable));
		}

		UNiagaraNodeAssignment* Assignment = FNiagaraStackGraphUtilities::AddParameterModuleToStack(Variables, *Output, INDEX_NONE, Defaults);
		if (Assignment == nullptr || Assignment->GetCalledGraph() == nullptr)
		{
			return false;
		}

		UNiagaraGraph* CalledGraph = Assignment->GetCalledGraph();
		TArray<UEdGraphNode*> SetNodes;
		for (UEdGraphNode* Node : CalledGraph->Nodes)
		{
			if (Node != nullptr && Node->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapSet"))
			{
				SetNodes.Add(Node);
			}
		}
		if (SetNodes.Num() != 1)
		{
			return false;
		}

		TArray<UEdGraphPin*> InputPins;
		for (UEdGraphPin* Pin : SetNodes[0]->Pins) if (Pin && Pin->Direction == EGPD_Input) InputPins.Add(Pin);
		for (int32 Index = 0; Index < Variables.Num(); ++Index)
		{
			UEdGraphPin* ValuePin = nullptr;
			for (UEdGraphPin* Pin : InputPins)
			{
				if (Pin != nullptr && Pin->PinName == Variables[Index].GetName())
				{
					ValuePin = Pin;
					break;
				}
			}
			if (ValuePin == nullptr)
			{
				return false;
			}

			UEdGraphPin* MapSource = nullptr;
			for (UEdGraphPin* Pin : InputPins)
			{
				if (Pin && Pin != ValuePin && Pin->LinkedTo.Num() && Pin->PinName == TEXT("Source")) MapSource = Pin->LinkedTo[0];
			}
			if (!MapSource) return false;
			UNiagaraNodeCustomHlsl* DynamicInput = NewObject<UNiagaraNodeCustomHlsl>(CalledGraph);
			CalledGraph->AddNode(DynamicInput, false, false);
			DynamicInput->CreateNewGuid();
			DynamicInput->ScriptUsage = ENiagaraScriptUsage::DynamicInput;
			DynamicInput->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Map")));
			DynamicInput->Signature.Outputs.Add(FNiagaraVariable(Variables[Index].GetType(), TEXT("CustomHLSLOutput")));
			FStrProperty* CodeProperty = FindFProperty<FStrProperty>(DynamicInput->GetClass(), TEXT("CustomHlsl"));
			if (!CodeProperty) return false;
			CodeProperty->SetPropertyValue_InContainer(DynamicInput, Expressions[Index]);
			static_cast<UEdGraphNode*>(DynamicInput)->AllocateDefaultPins();
			UEdGraphPin* MapPin = DynamicInput->FindPin(TEXT("Map"), EGPD_Input);
			UEdGraphPin* ResultPin = DynamicInput->FindPin(TEXT("CustomHLSLOutput"), EGPD_Output);
			if (!MapPin || !ResultPin) return false;
			MapPin->MakeLinkTo(MapSource);
			ValuePin->BreakAllPinLinks();
			ResultPin->MakeLinkTo(ValuePin);
		}
		static_cast<UEdGraph*>(CalledGraph)->NotifyGraphChanged();
		return true;
	}

	static bool SetInputValue(UNiagaraSystem* System, UNiagaraScript* Script, const FString& EmitterName,
		UNiagaraNodeFunctionCall* Function, FName InputName, const FNiagaraTypeDefinition& Type, const uint8* Data, int32 Size)
	{
		if (System == nullptr || Script == nullptr || Function == nullptr || Data == nullptr || Type.GetSize() != Size)
		{
			return false;
		}
		FNiagaraStackFunctionInputBinder Binder;
		FText Error;
		TArray<UNiagaraScript*> Dependencies;
		if (!Binder.TryBind(Script, Dependencies, FCompileConstantResolver(System, Script->GetUsage()), EmitterName,
			Function, InputName, Type, true, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Niagara input '%s' bind failed: %s"), *InputName.ToString(), *Error.ToString());
			TArray<FNiagaraVariable> Inputs;
			TSet<FNiagaraVariable> Hidden;
			FNiagaraStackGraphUtilities::GetStackFunctionInputs(*Function, Inputs, Hidden, FCompileConstantResolver(System, Script->GetUsage()), FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
			for (const FNiagaraVariable& Input : Inputs) UE_LOG(LogTemp, Display, TEXT("[TracerAssets] Input %s hidden=%d"), *Input.GetName().ToString(), Hidden.Contains(Input));
			for (UEdGraphPin* Pin : Function->Pins) if (Pin && Pin->Direction == EGPD_Input) UE_LOG(LogTemp, Display, TEXT("[TracerAssets] Pin %s default=%s"), *Pin->PinName.ToString(), *Pin->DefaultValue);
			return false;
		}
		Binder.SetData(Data, Size);
		return true;
	}

	static bool SetFloatInput(UNiagaraSystem* System, UNiagaraScript* Script, const FString& EmitterName,
		UNiagaraNodeFunctionCall* Function, FName InputName, float Value)
	{
		return SetInputValue(System, Script, EmitterName, Function, InputName, FNiagaraTypeDefinition::GetFloatDef(),
			reinterpret_cast<const uint8*>(&Value), sizeof(Value));
	}

	static bool SetIntInput(UNiagaraSystem* System, UNiagaraScript* Script, const FString& EmitterName,
		UNiagaraNodeFunctionCall* Function, FName InputName, int32 Value)
	{
		return SetInputValue(System, Script, EmitterName, Function, InputName, FNiagaraTypeDefinition::GetIntDef(),
			reinterpret_cast<const uint8*>(&Value), sizeof(Value));
	}

	static UNiagaraNodeFunctionCall* AddModule(UNiagaraScript* Script, ENiagaraScriptUsage Usage, const TCHAR* ModulePath)
	{
		UNiagaraNodeOutput* Output = FindOutput(Script, Usage);
		UNiagaraScript* Module = LoadObject<UNiagaraScript>(nullptr, ModulePath);
		if (Output == nullptr || Module == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Could not load Niagara module '%s' or find its output."), ModulePath);
			return nullptr;
		}
		return FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Output, INDEX_NONE);
	}

	static bool SetLoopOnce(UNiagaraSystem* System, UNiagaraScript* Script, const FString& EmitterName, float Duration)
	{
		UNiagaraScriptSource* Source = Script != nullptr ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
		if (Source == nullptr || Source->NodeGraph == nullptr)
		{
			return false;
		}

		TArray<UNiagaraNodeFunctionCall*> Calls;
		Source->NodeGraph->GetNodesOfClass(Calls);
		for (UNiagaraNodeFunctionCall* Call : Calls)
		{
			UEdGraphPin* LoopPin = Call->FindPin(TEXT("Loop Behavior"), EGPD_Input);
			if (LoopPin == nullptr)
			{
				continue;
			}
			UEnum* LoopEnum = Cast<UEnum>(LoopPin->PinType.PinSubCategoryObject.Get());
			if (!LoopEnum) return false;
			int64 OnceValue = INDEX_NONE;
			for (int32 Index = 0; Index < LoopEnum->NumEnums(); ++Index)
			{
				const FString DisplayName = LoopEnum->GetDisplayNameTextByIndex(Index).ToString();
				UE_LOG(LogTemp, Display, TEXT("[TracerAssets] Loop enum %s value=%lld display=%s"), *LoopEnum->GetNameStringByIndex(Index), LoopEnum->GetValueByIndex(Index), *DisplayName);
				if (LoopEnum->GetNameStringByIndex(Index) == TEXT("Once") || DisplayName == TEXT("Once") || DisplayName == TEXT("一次") || DisplayName == TEXT("单次")) OnceValue = LoopEnum->GetValueByIndex(Index);
			}
			if (OnceValue == INDEX_NONE) return false;
			LoopPin->DefaultValue = LoopEnum->GetNameStringByValue(OnceValue);
			UEdGraphPin* LifeCyclePin = Call->FindPin(TEXT("Life Cycle Mode"), EGPD_Input);
			UEnum* LifeCycleEnum = LifeCyclePin ? Cast<UEnum>(LifeCyclePin->PinType.PinSubCategoryObject.Get()) : nullptr;
			if (!LifeCycleEnum) return false;
			bool bFoundSelf = false;
			for (int32 Index = 0; Index < LifeCycleEnum->NumEnums(); ++Index)
			{
				const FString Label = LifeCycleEnum->GetDisplayNameTextByIndex(Index).ToString();
				if (Label == TEXT("Self") || Label == TEXT("自身"))
				{
					LifeCyclePin->DefaultValue = LifeCycleEnum->GetNameStringByIndex(Index);
					bFoundSelf = true;
				}
			}
			if (!bFoundSelf) return false;
			Call->PinDefaultValueChanged(LoopPin);
			Call->PinDefaultValueChanged(LifeCyclePin);
			if (!SetFloatInput(System, Script, EmitterName, Call, TEXT("Module.Loop Duration"), Duration))
			{
				return false;
			}
			Call->MarkNodeRequiresSynchronization(TEXT("Tracer one-shot lifetime"), true);
			return true;
		}
		return false;
	}

	static bool AddUserParameter(UNiagaraSystem* System, const FNiagaraTypeDefinition& Type, FName Name, const uint8* Value, int32 Size)
	{
		if (System == nullptr || Value == nullptr || Type.GetSize() != Size)
		{
			return false;
		}
		FNiagaraVariable Variable(Type, Name);
		FNiagaraParameterStore& Store = System->GetExposedParameters();
		if (!Store.AddParameter(Variable, true, false))
		{
			return false;
		}
		return Store.SetParameterData(Value, Variable, false);
	}

	static bool AddUserParameters(UNiagaraSystem* System)
	{
		const float DefaultDistance = 4500.0f;
		const float DefaultLifetime = DefaultTracerLifetime;
		const float DefaultLength = 80.0f;
		const float DefaultWidth = 1.2f;
		const FLinearColor DefaultColor(1.0f, 0.55f, 0.18f, 1.0f);
		return AddUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerDistance"), reinterpret_cast<const uint8*>(&DefaultDistance), sizeof(DefaultDistance))
			&& AddUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerLifetime"), reinterpret_cast<const uint8*>(&DefaultLifetime), sizeof(DefaultLifetime))
			&& AddUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerLength"), reinterpret_cast<const uint8*>(&DefaultLength), sizeof(DefaultLength))
			&& AddUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerWidth"), reinterpret_cast<const uint8*>(&DefaultWidth), sizeof(DefaultWidth))
			&& AddUserParameter(System, FNiagaraTypeDefinition::GetColorDef(), TEXT("User.TracerColor"), reinterpret_cast<const uint8*>(&DefaultColor), sizeof(DefaultColor));
	}

	static bool SaveAsset(UObject* Asset, const FString& PackageName)
	{
		if (Asset == nullptr)
		{
			return false;
		}
		UPackage* Package = Asset->GetOutermost();
		if (Package == nullptr)
		{
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		Args.Error = GWarn;
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	}

	static bool CompileSucceeded(UNiagaraSystem* System)
	{
		if (System == nullptr)
		{
			return false;
		}
		System->PostEditChange();
		System->RequestCompile(true);
		System->WaitForCompilationComplete(true, true);

		TArray<UNiagaraScript*> Scripts;
		if (UNiagaraScript* Script = System->GetSystemSpawnScript()) Scripts.Add(Script);
		if (UNiagaraScript* Script = System->GetSystemUpdateScript()) Scripts.Add(Script);
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
			{
				Data->GetScripts(Scripts, false);
			}
		}

		bool bSuccess = !Scripts.IsEmpty();
		for (UNiagaraScript* Script : Scripts)
		{
			if (Script == nullptr)
			{
				bSuccess = false;
				continue;
			}
			const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
			UE_LOG(LogTemp, Display, TEXT("[TracerAssets] Compile %s: %s"), *Script->GetName(), *UEnum::GetValueAsString(Status));
			// Emitter graphs are compiled into the system scripts, not separate VM executables.
			if (Script->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript || Script->GetUsage() == ENiagaraScriptUsage::EmitterUpdateScript) continue;
			if (Status != ENiagaraScriptCompileStatus::NCS_UpToDate && Status != ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings)
			{
				bSuccess = false;
			}
		}
		return bSuccess;
	}

	static void BuildTracerAssets()
	{
		const FString MaterialPackage = MakePackageName(MaterialName);
		const FString SystemPackage = MakePackageName(SystemName);
		FString ExistingFilename;
		if (FPackageName::IsValidLongPackageName(MaterialPackage) == false || FPackageName::IsValidLongPackageName(SystemPackage) == false)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Invalid output package names; no assets were created."));
			return;
		}
		if (HasPackageFile(MaterialPackage) || HasPackageFile(SystemPackage)
			|| FindObject<UObject>(nullptr, *MakeObjectPath(MaterialName)) != nullptr
			|| FindObject<UObject>(nullptr, *MakeObjectPath(SystemName)) != nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Refusing to overwrite existing M_ShotTracerGlow or NS_ShotTracer. Existing M_TracerGlow is untouched."));
			return;
		}

		UMaterial* Material = BuildTracerMaterial();
		if (Material == nullptr || Material->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Material shader compilation failed; no package was saved."));
			return;
		}

		UNiagaraEmitter* MinimalTemplate = LoadObject<UNiagaraEmitter>(nullptr, MinimalEmitterPath);
		if (MinimalTemplate == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Official Niagara Minimal emitter template was not found: %s"), MinimalEmitterPath);
			return;
		}

		UNiagaraSystem* System = NewObject<UNiagaraSystem>(GetTransientPackage(), NAME_None, RF_Transient);
		UNiagaraSystemFactoryNew::InitializeSystem(System, true);
		UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(StaticDuplicateObject(MinimalTemplate, GetTransientPackage(), NAME_None, RF_Transient));
		if (Emitter == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Could not duplicate the Minimal emitter template."));
			return;
		}
		// Reset only the transient duplicate to a clean CPU graph using Niagara's public factory API.
		UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, false);
		FVersionedNiagaraEmitter VersionedEmitter(Emitter, Emitter->GetExposedVersion().VersionGuid);
		FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, VersionedEmitter.Version, true);
		if (System->GetEmitterHandles().Num() != 1)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Expected exactly one Niagara emitter."));
			return;
		}

		FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(0);
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (EmitterData == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Niagara emitter instance data is invalid."));
			return;
		}
		EmitterData->SimTarget = ENiagaraSimTarget::CPUSim;
		EmitterData->bLocalSpace = true;
		UNiagaraEmitter* EmitterInstance = Cast<UNiagaraEmitter>(Handle.GetInstance().Emitter);
		if (EmitterInstance == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Niagara emitter instance is invalid."));
			return;
		}

		UNiagaraSpriteRendererProperties* Renderer = NewObject<UNiagaraSpriteRendererProperties>(EmitterInstance, TEXT("TracerSpriteRenderer"), RF_Transactional);
		Renderer->Material = Material;
		Renderer->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
		Renderer->Alignment = ENiagaraSpriteAlignment::CustomAlignment;
		Renderer->SpriteSizeBinding.SetValue(SYS_PARAM_PARTICLES_SPRITE_SIZE.GetName(), Handle.GetInstance(), Renderer->SourceMode);
		Renderer->SpriteAlignmentBinding.SetValue(SYS_PARAM_PARTICLES_SPRITE_ALIGNMENT.GetName(), Handle.GetInstance(), Renderer->SourceMode);
		EmitterInstance->AddRenderer(Renderer, EmitterData->Version.VersionGuid);

		UNiagaraScript* EmitterUpdate = EmitterData->EmitterUpdateScriptProps.Script;
		UNiagaraScript* EmitterSpawn = EmitterData->EmitterSpawnScriptProps.Script;
		UNiagaraScript* ParticleSpawn = EmitterData->SpawnScriptProps.Script;
		UNiagaraScript* ParticleUpdate = EmitterData->UpdateScriptProps.Script;
		if (EmitterUpdate == nullptr || EmitterSpawn == nullptr || ParticleSpawn == nullptr || ParticleUpdate == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Minimal emitter did not provide the four expected Niagara scripts."));
			return;
		}

		UNiagaraNodeFunctionCall* EmitterState = AddModule(EmitterUpdate, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		UNiagaraNodeFunctionCall* Burst = AddModule(EmitterUpdate, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
		UNiagaraNodeFunctionCall* InitParticle = AddModule(ParticleSpawn, ENiagaraScriptUsage::ParticleSpawnScript, TEXT("/Niagara/Modules/Spawn/Initialization/InitializeParticle.InitializeParticle"));
		UNiagaraNodeFunctionCall* ParticleState = AddModule(ParticleUpdate, ENiagaraScriptUsage::ParticleUpdateScript, TEXT("/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState"));
		if (EmitterState == nullptr || Burst == nullptr || InitParticle == nullptr || ParticleState == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Required Niagara module missing; no package was saved."));
			return;
		}

		const FString EmitterUniqueName = Handle.GetUniqueInstanceName();
		int32 BurstCount = 1;
		if (!SetLoopOnce(System, EmitterUpdate, EmitterUniqueName, EmitterLoopDuration)
			|| !SetIntInput(System, EmitterUpdate, EmitterUniqueName, Burst, TEXT("Spawn Count"), BurstCount))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Could not configure one-shot emitter state / one-particle burst; no package was saved."));
			return;
		}

		const TArray<FNiagaraVariable> SpawnVariables = { SYS_PARAM_PARTICLES_LIFETIME, SYS_PARAM_PARTICLES_COLOR };
		const TArray<FString> SpawnExpressions = {
			TEXT("User.TracerLifetime"),
			TEXT("User.TracerColor")
		};
		const TArray<FNiagaraVariable> UpdateVariables = { SYS_PARAM_PARTICLES_POSITION, SYS_PARAM_PARTICLES_SPRITE_SIZE, SYS_PARAM_PARTICLES_SPRITE_ALIGNMENT };
		const TArray<FString> UpdateExpressions = {
			TEXT("float3((User.TracerDistance * saturate(Particles.NormalizedAge) + max(0.0, User.TracerDistance * saturate(Particles.NormalizedAge) - User.TracerLength)) * 0.5, 0.0, 0.0)"),
			TEXT("float2(max(0.001, User.TracerWidth), max(0.001, min(User.TracerLength, User.TracerDistance * saturate(Particles.NormalizedAge))))"),
			TEXT("float3(1.0, 0.0, 0.0)")
		};
		if (!AddAssignment(ParticleSpawn, ENiagaraScriptUsage::ParticleSpawnScript, SpawnVariables, SpawnExpressions)
			|| !AddAssignment(ParticleUpdate, ENiagaraScriptUsage::ParticleUpdateScript, UpdateVariables, UpdateExpressions))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Could not build one of the Niagara parameter-map assignments; no package was saved."));
			return;
		}

		if (!AddUserParameters(System))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Could not expose all five User.Tracer* parameters."));
			return;
		}

		UNiagaraScriptSource* EmitterSource = Cast<UNiagaraScriptSource>(EmitterUpdate->GetLatestSource());
		if (EmitterSource != nullptr && EmitterSource->NodeGraph != nullptr) static_cast<UEdGraph*>(EmitterSource->NodeGraph)->NotifyGraphChanged();
		UNiagaraScriptSource* SpawnSource = Cast<UNiagaraScriptSource>(ParticleSpawn->GetLatestSource());
		if (SpawnSource != nullptr && SpawnSource->NodeGraph != nullptr) static_cast<UEdGraph*>(SpawnSource->NodeGraph)->NotifyGraphChanged();
		UNiagaraScriptSource* UpdateSource = Cast<UNiagaraScriptSource>(ParticleUpdate->GetLatestSource());
		if (UpdateSource != nullptr && UpdateSource->NodeGraph != nullptr) static_cast<UEdGraph*>(UpdateSource->NodeGraph)->NotifyGraphChanged();

		if (!CompileSucceeded(System))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Niagara compile did not reach an up-to-date state; no packages were saved."));
			return;
		}

		UPackage* MaterialPkg = CreatePackage(*MaterialPackage);
		UPackage* SystemPkg = CreatePackage(*SystemPackage);
		if (MaterialPkg == nullptr || SystemPkg == nullptr
			|| !Material->Rename(MaterialName, MaterialPkg, REN_DontCreateRedirectors | REN_NonTransactional)
			|| !System->Rename(SystemName, SystemPkg, REN_DontCreateRedirectors | REN_NonTransactional))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Could not move the validated transient assets into their target packages; nothing was saved."));
			return;
		}

		Material->ClearFlags(RF_Transient);
		System->ClearFlags(RF_Transient);
		ForEachObjectWithOuter(Material, [](UObject* Object){ Object->ClearFlags(RF_Transient); }, true);
		ForEachObjectWithOuter(System, [](UObject* Object){ Object->ClearFlags(RF_Transient); }, true);
		Material->SetFlags(RF_Public | RF_Standalone);
		System->SetFlags(RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Material);
		FAssetRegistryModule::AssetCreated(System);
		MaterialPkg->MarkPackageDirty();
		SystemPkg->MarkPackageDirty();
		if (!SaveAsset(Material, MaterialPackage))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Failed saving M_ShotTracerGlow; NS_ShotTracer was not saved."));
			return;
		}
		if (!SaveAsset(System, SystemPackage))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] M_ShotTracerGlow saved, but NS_ShotTracer save failed. This is a partial result; do not rerun until inspected."));
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("[TracerAssets] BUILD_SUCCESS: saved %s and %s; CPU, local-space, one burst particle; particle lifetime is User.TracerLifetime; translucent orange-body/pale-core material retains depth test."), *MakeObjectPath(MaterialName), *MakeObjectPath(SystemName));
		UE_LOG(LogTemp, Display, TEXT("[TracerAssets] Existing M_TracerGlow was preserved. No weapon Blueprint or gameplay state was modified."));
	}

	static bool HasUserParameter(const UNiagaraSystem* System, const FNiagaraTypeDefinition& Type, FName Name)
	{
		if (System == nullptr)
		{
			return false;
		}
		return System->GetExposedParameters().FindParameterOffset(FNiagaraVariableBase(Type, Name)) != nullptr;
	}

	static bool VerifySystemCompile(UNiagaraSystem* System)
	{
		if (System == nullptr)
		{
			return false;
		}
		System->WaitForCompilationComplete(true, true);
		TArray<UNiagaraScript*> Scripts;
		if (UNiagaraScript* Script = System->GetSystemSpawnScript()) Scripts.Add(Script);
		if (UNiagaraScript* Script = System->GetSystemUpdateScript()) Scripts.Add(Script);
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData()) Data->GetScripts(Scripts, false);
		}
		bool bOK = !Scripts.IsEmpty();
		for (UNiagaraScript* Script : Scripts)
		{
			const ENiagaraScriptCompileStatus Status = Script ? Script->GetLastCompileStatus() : ENiagaraScriptCompileStatus::NCS_Error;
			UE_LOG(LogTemp, Display, TEXT("[TracerAssets] VERIFY compile %s: %s"), Script ? *Script->GetName() : TEXT("<null>"), *UEnum::GetValueAsString(Status));
			if (Script && (Script->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript || Script->GetUsage() == ENiagaraScriptUsage::EmitterUpdateScript)) continue;
			bOK &= Status == ENiagaraScriptCompileStatus::NCS_UpToDate || Status == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings;
		}
		return bOK && System->IsReadyToRun();
	}

	static void VerifyTracerAssets()
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, MakeObjectPath(MaterialName));
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, MakeObjectPath(SystemName));
		if (Material == nullptr || System == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] VERIFY_FAILED: one or both target assets are missing."));
			return;
		}

		const bool bUserParameters = HasUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerDistance"))
			&& HasUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerLifetime"))
			&& HasUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerLength"))
			&& HasUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerWidth"))
			&& HasUserParameter(System, FNiagaraTypeDefinition::GetColorDef(), TEXT("User.TracerColor"));
		bool bRenderer = false;
		bool bCPU = false;
		bool bLocal = false;
		if (System->GetEmitterHandles().Num() == 1)
		{
			const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(0);
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			bCPU = Data != nullptr && Data->SimTarget == ENiagaraSimTarget::CPUSim;
			bLocal = Data != nullptr && Data->bLocalSpace;
			if (Data != nullptr)
			{
				for (UNiagaraRendererProperties* RendererBase : Data->GetRenderers())
				{
					if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(RendererBase))
					{
						bRenderer |= Sprite->Material == Material
							&& Sprite->FacingMode == ENiagaraSpriteFacingMode::FaceCamera
							&& Sprite->Alignment == ENiagaraSpriteAlignment::CustomAlignment;
					}
				}
			}
		}
		const bool bMaterial = Material->BlendMode == BLEND_Translucent && Material->GetShadingModels().HasShadingModel(MSM_Unlit)
			&& Material->TwoSided && Material->bUsedWithNiagaraSprites && !Material->bDisableDepthTest
			&& !Material->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform);
		const bool bCompile = VerifySystemCompile(System);
		UE_LOG(LogTemp, Display, TEXT("[TracerAssets] VERIFY_RESULT userParams=%s renderer=%s cpu=%s local=%s translucentUnlitDepthTest=%s compile=%s"),
			bUserParameters ? TEXT("OK") : TEXT("FAIL"), bRenderer ? TEXT("OK") : TEXT("FAIL"), bCPU ? TEXT("OK") : TEXT("FAIL"),
			bLocal ? TEXT("OK") : TEXT("FAIL"), bMaterial ? TEXT("OK") : TEXT("FAIL"), bCompile ? TEXT("OK") : TEXT("FAIL"));
		if (bUserParameters && bRenderer && bCPU && bLocal && bMaterial && bCompile)
		{
			UE_LOG(LogTemp, Display, TEXT("[TracerAssets] VERIFY_SUCCESS: saved Niagara tracer asset checks passed."));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] VERIFY_FAILED: one or more required tracer properties did not match."));
		}
	}

	static void RepairGeneratedLifetime()
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *MakeObjectPath(SystemName));
		if (!System || System->GetEmitterHandles().Num() != 1 || !HasUserParameter(System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.TracerDistance"))) return;
		const FString Filename = FPackageName::LongPackageNameToFilename(MakePackageName(SystemName), FPackageName::GetAssetPackageExtension());
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("TracerBackups") / (TEXT("System-") + FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
		IFileManager::Get().MakeDirectory(*Backup, true);
		if (IFileManager::Get().Copy(*(Backup / TEXT("NS_ShotTracer.uasset")), *Filename) != COPY_OK) return;
		FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(0);
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->UpdateScriptProps.Script->GetLatestSource()) : nullptr;
		if (!Source || !Source->NodeGraph) return;
		const TArray<UEdGraphNode*> ExistingNodes = Source->NodeGraph->Nodes;
		int32 ParticleStateCount = 0;
		for (UEdGraphNode* Node : ExistingNodes)
		{
			const UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
			if (Call && Call->FunctionScript && Call->FunctionScript->GetOutermost()->GetName() == TEXT("/Niagara/Modules/Update/Lifetime/ParticleState")) ++ParticleStateCount;
		}
		for (UEdGraphNode* Node : ExistingNodes)
		{
			UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
			if (Call && Call->FunctionScript) UE_LOG(LogTemp, Display, TEXT("[TracerAssets] Existing module %s"), *Call->FunctionScript->GetPathName());
			const bool bDuplicateState = Call && Call->FunctionScript && ParticleStateCount > 1 && Call->FunctionScript->GetOutermost()->GetName() == TEXT("/Niagara/Modules/Update/Lifetime/ParticleState");
			if (Call && Call->FunctionScript && (bDuplicateState || Call->FunctionScript->GetOutermost()->GetName() == TEXT("/Niagara/Modules/Update/Lifetime/UpdateAge")))
			{
				UEdGraphPin* MapInput = nullptr;
				UEdGraphPin* MapOutput = nullptr;
				for (UEdGraphPin* Pin : Call->Pins)
				{
					if (Pin && Pin->PinType.PinSubCategoryObject.IsValid() && Pin->PinType.PinSubCategoryObject->GetName() == TEXT("NiagaraParameterMap"))
					{
						if (Pin->Direction == EGPD_Input) MapInput = Pin; else MapOutput = Pin;
					}
				}
				if (!MapInput || !MapOutput || MapInput->LinkedTo.Num() != 1) return;
				UEdGraphPin* Previous = MapInput->LinkedTo[0];
				const TArray<UEdGraphPin*> Next = MapOutput->LinkedTo;
				Call->BreakAllNodeLinks();
				for (UEdGraphPin* Pin : Next) Previous->MakeLinkTo(Pin);
				Source->NodeGraph->RemoveNode(Call);
				if (bDuplicateState) --ParticleStateCount;
				UE_LOG(LogTemp, Display, TEXT("[TracerAssets] Removed duplicate UpdateAge module."));
			}
		}
		if (!Data || !SetLoopOnce(System, Data->EmitterUpdateScriptProps.Script, Handle.GetUniqueInstanceName(), EmitterLoopDuration))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Lifetime repair configuration failed, not saved."));
			return;
		}
		if (!CompileSucceeded(System) || !System->IsReadyToRun() || !SaveAsset(System, MakePackageName(SystemName)))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] Lifetime repair failed. Backup: %s"), *Backup);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("[TracerAssets] REPAIR_SUCCESS loop=Once backup=%s"), *Backup);
	}

	static void UpgradeTracerMaterial()
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *MakeObjectPath(MaterialName));
		if (!Material) { UE_LOG(LogTemp, Error, TEXT("[TracerAssets] MATERIAL_FAILED missing material")); return; }
		const UMaterialExpressionCustom* Current = Cast<UMaterialExpressionCustom>(Material->GetEditorOnlyData()->EmissiveColor.Expression);
		if (Material->BlendMode == BLEND_Translucent && Current && Current->Description == TEXT("Tracer orange body and pale core v2"))
		{
			UE_LOG(LogTemp, Display, TEXT("[TracerAssets] MATERIAL_SUCCESS already upgraded")); return;
		}
		// Only upgrade the known generated baseline; preserve user-created material graphs.
		if (Material->BlendMode != BLEND_Additive || !Cast<UMaterialExpressionMultiply>(Material->GetEditorOnlyData()->EmissiveColor.Expression))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] MATERIAL_FAILED unexpected baseline; not modified")); return;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(MakePackageName(MaterialName), FPackageName::GetAssetPackageExtension());
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("TracerBackups") / (TEXT("Material-") + FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
		IFileManager::Get().MakeDirectory(*Backup, true);
		if (IFileManager::Get().Copy(*(Backup / TEXT("M_ShotTracerGlow.uasset")), *Filename) != COPY_OK)
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] MATERIAL_FAILED backup failed")); return;
		}
		BuildTracerMaterial(Material);
		if (Material->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform) || !SaveAsset(Material, MakePackageName(MaterialName)))
		{
			UE_LOG(LogTemp, Error, TEXT("[TracerAssets] MATERIAL_FAILED compile/save failed backup=%s"), *Backup); return;
		}
		UE_LOG(LogTemp, Display, TEXT("[TracerAssets] MATERIAL_SUCCESS orange body / pale core / alpha blend; backup=%s"), *Backup);
	}
	static FAutoConsoleCommand MaterialCommand(TEXT("Dust2.UpgradeTracerMaterial"), TEXT("Upgrade only the generated tracer material with a previous-version backup."), FConsoleCommandDelegate::CreateStatic(&UpgradeTracerMaterial));

	static FAutoConsoleCommand RepairCommand(TEXT("Dust2.RepairTracerLifetime"), TEXT("Repair generated tracer loop configuration with a previous-version backup."), FConsoleCommandDelegate::CreateStatic(&RepairGeneratedLifetime));
	static FAutoConsoleCommand BuildCommand(
		TEXT("Dust2.BuildTracerAssets"),
		TEXT("Creates new one-shot tracer material/system assets; refuses to overwrite existing targets."),
		FConsoleCommandDelegate::CreateStatic(&BuildTracerAssets));

	static FAutoConsoleCommand VerifyCommand(
		TEXT("Dust2.VerifyTracerAssets"),
		TEXT("Read-only verification of tracer assets, user parameters, renderer, depth test, and Niagara compile status."),
		FConsoleCommandDelegate::CreateStatic(&VerifyTracerAssets));
}

#endif // WITH_EDITOR
