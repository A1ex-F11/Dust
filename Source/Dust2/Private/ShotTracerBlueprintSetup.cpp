#include "ShotTracerComponent.h"

#if WITH_EDITOR

#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_BreakStruct.h"
#include "EdGraphSchema_K2.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace ShotTracerBlueprintSetup
{
static const TCHAR* WeaponBlueprintPath = TEXT("/Game/Code/Player/Player/Bp_WeaponBase.Bp_WeaponBase");
static const TCHAR* PlayerBlueprintPath = TEXT("/Game/Code/Texts/BP_Dust2_TacticalPresentationTest.BP_Dust2_TacticalPresentationTest");
static const TCHAR* PresentationWeaponClassPath = TEXT("/Game/KINEMATION/TacticalShooterPack/Blueprints/BP_TacticalShooterWeapon.BP_TacticalShooterWeapon_C");
static const TCHAR* TracerSystemPath = TEXT("/Game/Code/VFX/Tracer/NS_ShotTracer.NS_ShotTracer");
static const TCHAR* MarkerPrefix = TEXT("[Dust2ShotTracer:v1]");
static const TCHAR* ComponentMarker = TEXT("Dust2ShotTracerSetup_v1");
static const FName TracerVariableName(TEXT("ShotTracer"));

struct FGraphNodes
{
	UK2Node_ExecutionSequence* Sequence = nullptr;
	UK2Node_CallFunction* AttachParent = nullptr;
	UK2Node_DynamicCast* CastPlayer = nullptr;
	UK2Node_VariableGet* TacticalWeapon = nullptr;
	UK2Node_CallFunction* IsWeaponValid = nullptr;
	UK2Node_IfThenElse* WeaponValidBranch = nullptr;
	UK2Node_VariableGet* MuzzleFlash = nullptr;
	UK2Node_CallFunction* IsMuzzleValid = nullptr;
	UK2Node_IfThenElse* MuzzleValidBranch = nullptr;
	UK2Node_CallFunction* MuzzleLocation = nullptr;
	UK2Node_VariableGet* ShotTracer = nullptr;
	UK2Node_CallFunction* ShotEnd = nullptr;
	UK2Node_CallFunction* PlayTracer = nullptr;
};

static UEdGraphPin* FindPin(UEdGraphNode* Node, const FName PinName)
{
	return Node ? Node->FindPin(PinName) : nullptr;
}

static bool PinsLinked(const UEdGraphPin* A, const UEdGraphPin* B)
{
	return A && B && A->LinkedTo.Contains(const_cast<UEdGraphPin*>(B)) &&
		B->LinkedTo.Contains(const_cast<UEdGraphPin*>(A));
}

static UEdGraphNode* FindMarkerNode(UEdGraph* Graph, const TCHAR* Marker)
{
	if (!Graph) return nullptr;
	UEdGraphNode* Found = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeComment == Marker)
		{
			if (Found) return nullptr; // Duplicate marker is a hard verification failure.
			Found = Node;
		}
	}
	return Found;
}

static void StampNode(UEdGraphNode* Node, const TCHAR* Suffix)
{
	Node->NodeComment = FString::Printf(TEXT("%s.%s"), MarkerPrefix, Suffix);
}

static bool IsMarkedComponent(const USCS_Node* Node)
{
	const UShotTracerComponent* Template = Node ? Cast<UShotTracerComponent>(Node->ComponentTemplate) : nullptr;
	return Template && Template->ComponentTags.Contains(FName(ComponentMarker));
}

static USCS_Node* FindMarkedTracerNode(UBlueprint* Blueprint)
{
	USCS_Node* Found = nullptr;
	if (!Blueprint || !Blueprint->SimpleConstructionScript) return nullptr;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (IsMarkedComponent(Node))
		{
			if (Found) return nullptr;
			Found = Node;
		}
	}
	return Found;
}

static UBlueprint* LoadWeaponBlueprint()
{
	return LoadObject<UBlueprint>(nullptr, WeaponBlueprintPath);
}

static bool FindBaseNodes(UBlueprint* Blueprint, UEdGraph*& OutGraph,
	UK2Node_CallFunction*& OutTrace, UK2Node_IfThenElse*& OutDamageBranch,
	UK2Node_CallFunction*& OutBreakHit, UEdGraphPin*& OutTraceEndSource, FString& OutError)
{
	OutGraph = nullptr;
	OutTrace = nullptr;
	OutDamageBranch = nullptr;
	OutBreakHit = nullptr;
	OutTraceEndSource = nullptr;
	if (!Blueprint)
	{
		OutError = TEXT("武器基类蓝图未加载。");
		return false;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetName() == TEXT("RequestFire")) { OutGraph = Graph; break; }
	}
	if (!OutGraph)
	{
		OutError = TEXT("找不到 RequestFire 函数图。");
		return false;
	}

	TArray<UK2Node_CallFunction*> Traces;
	for (UEdGraphNode* Node : OutGraph->Nodes)
	{
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			if (Call->GetTargetFunction() ==
				UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, LineTraceSingle)))
			{
				Traces.Add(Call);
			}
		}
	}
	if (Traces.Num() != 1)
	{
		OutError = FString::Printf(TEXT("RequestFire 预期恰有一个 LineTraceSingle，实际为 %d；停止以免接错。"), Traces.Num());
		return false;
	}
	OutTrace = Traces[0];
	UEdGraphPin* TraceExecOut = FindPin(OutTrace, TEXT("then"));
	UEdGraphPin* TraceHitOut = FindPin(OutTrace, TEXT("OutHit"));
	UEdGraphPin* TraceResultOut = FindPin(OutTrace, TEXT("ReturnValue"));
	if (!TraceExecOut || !TraceHitOut || !TraceResultOut || TraceExecOut->LinkedTo.Num() != 1)
	{
		OutError = TEXT("LineTraceSingle 的 then、OutHit 或 ReturnValue 连线与已检查的原图不一致。");
		return false;
	}
	UEdGraphNode* ExecTarget = TraceExecOut->LinkedTo[0]->GetOwningNode();
	OutDamageBranch = Cast<UK2Node_IfThenElse>(ExecTarget);
	if (UK2Node_ExecutionSequence* ExistingSequence = Cast<UK2Node_ExecutionSequence>(ExecTarget))
	{
		UEdGraphPin* ThenOne = ExistingSequence->GetThenPinGivenIndex(1);
		if (!ThenOne || ThenOne->LinkedTo.Num() != 1) { OutDamageBranch = nullptr; }
		else OutDamageBranch = Cast<UK2Node_IfThenElse>(ThenOne->LinkedTo[0]->GetOwningNode());
	}
	if (!OutDamageBranch ||
		!PinsLinked(TraceResultOut, FindPin(OutDamageBranch, TEXT("Condition"))))
	{
		OutError = TEXT("Trace 之后不再是原有的命中/伤害分支；停止修改。");
		return false;
	}
	for (UEdGraphPin* Link : TraceHitOut->LinkedTo)
	{
		UK2Node_CallFunction* Break = Cast<UK2Node_CallFunction>(Link->GetOwningNode());
		if (Break && Break->GetFunctionName() == TEXT("BreakHitResult") && Link == FindPin(Break, TEXT("Hit")))
		{
			if (OutBreakHit)
			{
				OutError = TEXT("LineTrace OutHit 连到多个 FHitResult Break 节点；拒绝猜测目标。");
				return false;
			}
			OutBreakHit = Break;
		}
	}
	if (!OutBreakHit || !PinsLinked(TraceHitOut, FindPin(OutBreakHit, TEXT("Hit"))))
	{
		OutError = TEXT("未找到当前 LineTrace.OutHit 使用的原 FHitResult Break 节点。");
		return false;
	}
	if (!FindPin(OutBreakHit, TEXT("ImpactPoint")) || !FindPin(OutTrace, TEXT("End")))
	{
		OutError = TEXT("原命中点或射线终点引脚缺失。");
		return false;
	}
	UEdGraphPin* TraceEnd = FindPin(OutTrace, TEXT("End"));
	if (TraceEnd->LinkedTo.Num() != 1 || TraceEnd->LinkedTo[0]->Direction != EGPD_Output)
	{
		OutError = TEXT("Trace.End 必须正好由一个原向量节点驱动，避免更改原弹道。");
		return false;
	}
	OutTraceEndSource = TraceEnd->LinkedTo[0];
	return true;
}

static bool CollectMarkedNodes(UEdGraph* Graph, FGraphNodes& Out, FString& OutError)
{
	const TPair<const TCHAR*, UEdGraphNode**> Markers[] = {
		{TEXT("Sequence"), reinterpret_cast<UEdGraphNode**>(&Out.Sequence)},
		{TEXT("AttachParent"), reinterpret_cast<UEdGraphNode**>(&Out.AttachParent)},
		{TEXT("CastPlayer"), reinterpret_cast<UEdGraphNode**>(&Out.CastPlayer)},
		{TEXT("TacticalWeapon"), reinterpret_cast<UEdGraphNode**>(&Out.TacticalWeapon)},
		{TEXT("IsWeaponValid"), reinterpret_cast<UEdGraphNode**>(&Out.IsWeaponValid)},
		{TEXT("WeaponValidBranch"), reinterpret_cast<UEdGraphNode**>(&Out.WeaponValidBranch)},
		{TEXT("MuzzleFlash"), reinterpret_cast<UEdGraphNode**>(&Out.MuzzleFlash)},
		{TEXT("IsMuzzleValid"), reinterpret_cast<UEdGraphNode**>(&Out.IsMuzzleValid)},
		{TEXT("MuzzleValidBranch"), reinterpret_cast<UEdGraphNode**>(&Out.MuzzleValidBranch)},
		{TEXT("MuzzleLocation"), reinterpret_cast<UEdGraphNode**>(&Out.MuzzleLocation)},
		{TEXT("ShotTracer"), reinterpret_cast<UEdGraphNode**>(&Out.ShotTracer)},
		{TEXT("ShotEnd"), reinterpret_cast<UEdGraphNode**>(&Out.ShotEnd)},
		{TEXT("PlayTracer"), reinterpret_cast<UEdGraphNode**>(&Out.PlayTracer)},
	};
	int32 MarkedNodeCount = 0;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeComment.StartsWith(MarkerPrefix)) ++MarkedNodeCount;
	}
	if (MarkedNodeCount == 0)
	{
		OutError = TEXT("没有已安装的曳光图节点标记。");
		return false;
	}
	for (const auto& Entry : Markers)
	{
		const FString Marker = FString::Printf(TEXT("%s.%s"), MarkerPrefix, Entry.Key);
		UEdGraphNode* Node = FindMarkerNode(Graph, *Marker);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("接入标记不完整或重复：%s。"), *Marker);
			return false;
		}
		*Entry.Value = Node;
	}
	if (MarkedNodeCount != UE_ARRAY_COUNT(Markers))
	{
		OutError = FString::Printf(TEXT("曳光标记节点数量不完整：期望 %d，实际 %d。"), UE_ARRAY_COUNT(Markers), MarkedNodeCount);
		return false;
	}
	return true;
}

static bool VerifyConnections(UBlueprint* Blueprint, const FGraphNodes& N,
	UK2Node_CallFunction* Trace, UK2Node_IfThenElse* OriginalDamageBranch,
	UK2Node_CallFunction* OriginalBreakHit, UEdGraphPin* TraceEndSource, FString& OutError)
{
	const UClass* PlayerClass = LoadObject<UClass>(nullptr, TEXT("/Game/Code/Texts/BP_Dust2_TacticalPresentationTest.BP_Dust2_TacticalPresentationTest_C"));
	const UClass* WeaponClass = LoadObject<UClass>(nullptr, PresentationWeaponClassPath);
	const UFunction* AttachFunction = AActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AActor, GetAttachParentActor));
	const UFunction* ValidFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid));
	const UFunction* LocationFunction = USceneComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_GetComponentLocation));
	const UFunction* SelectFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, SelectVector));
	const UFunction* PlayFunction = UShotTracerComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UShotTracerComponent, PlayShotTracer));
	if (!PlayerClass || !WeaponClass || !AttachFunction || !ValidFunction || !LocationFunction || !SelectFunction || !PlayFunction)
	{
		OutError = TEXT("验证所需类或 UFunction 未加载。");
		return false;
	}
	const auto CallIs = [](const UK2Node_CallFunction* Node, const UFunction* Function)
	{
		return Node && Node->GetTargetFunction() == Function;
	};
	UEdGraphPin* SequenceZero = N.Sequence->GetThenPinGivenIndex(0);
	UEdGraphPin* SequenceOne = N.Sequence->GetThenPinGivenIndex(1);
	if (!SequenceZero || !SequenceOne ||
		!PinsLinked(FindPin(Trace, TEXT("then")), FindPin(N.Sequence, TEXT("execute"))) ||
		!PinsLinked(SequenceZero, FindPin(N.CastPlayer, TEXT("execute"))) ||
		!PinsLinked(SequenceOne, FindPin(OriginalDamageBranch, TEXT("execute"))) ||
		!PinsLinked(FindPin(Trace, TEXT("OutHit")), FindPin(OriginalBreakHit, TEXT("Hit"))) ||
		!PinsLinked(FindPin(Trace, TEXT("ReturnValue")), FindPin(OriginalDamageBranch, TEXT("Condition"))) ||
		!CallIs(N.AttachParent, AttachFunction) ||
		!CallIs(N.IsWeaponValid, ValidFunction) || !CallIs(N.IsMuzzleValid, ValidFunction) ||
		!CallIs(N.MuzzleLocation, LocationFunction) || !CallIs(N.ShotEnd, SelectFunction) ||
		!CallIs(N.PlayTracer, PlayFunction) ||
		N.CastPlayer->TargetType.Get() != PlayerClass ||
		N.TacticalWeapon->VariableReference.GetMemberName() != TEXT("TacticalPresentationWeapon") ||
		N.TacticalWeapon->VariableReference.GetMemberParentClass() != PlayerClass ||
		N.MuzzleFlash->VariableReference.GetMemberName() != TEXT("MuzzleFlash") ||
		N.MuzzleFlash->VariableReference.GetMemberParentClass() != WeaponClass ||
		N.ShotTracer->VariableReference.GetMemberName() != TracerVariableName ||
		N.ShotTracer->VariableReference.IsSelfContext() == false)
	{
		OutError = TEXT("曳光执行链或原命中/伤害接线与预期不一致。");
		return false;
	}

	const bool bShotEndValid =
		PinsLinked(FindPin(OriginalBreakHit, TEXT("ImpactPoint")), FindPin(N.ShotEnd, TEXT("A"))) &&
		PinsLinked(TraceEndSource, FindPin(N.ShotEnd, TEXT("B"))) &&
		PinsLinked(FindPin(Trace, TEXT("ReturnValue")), FindPin(N.ShotEnd, TEXT("bPickA")));
	const bool bVisualFlowValid =
		PinsLinked(FindPin(N.AttachParent, TEXT("ReturnValue")), N.CastPlayer->GetCastSourcePin()) &&
		PinsLinked(N.CastPlayer->GetCastResultPin(), FindPin(N.TacticalWeapon, TEXT("self"))) &&
		PinsLinked(N.CastPlayer->GetValidCastPin(), FindPin(N.WeaponValidBranch, TEXT("execute"))) &&
		PinsLinked(FindPin(N.TacticalWeapon, TEXT("TacticalPresentationWeapon")), FindPin(N.IsWeaponValid, TEXT("Object"))) &&
		PinsLinked(FindPin(N.IsWeaponValid, TEXT("ReturnValue")), FindPin(N.WeaponValidBranch, TEXT("Condition"))) &&
		PinsLinked(FindPin(N.WeaponValidBranch, TEXT("then")), FindPin(N.MuzzleValidBranch, TEXT("execute"))) &&
		PinsLinked(FindPin(N.TacticalWeapon, TEXT("TacticalPresentationWeapon")), FindPin(N.MuzzleFlash, TEXT("self"))) &&
		PinsLinked(FindPin(N.MuzzleFlash, TEXT("MuzzleFlash")), FindPin(N.IsMuzzleValid, TEXT("Object"))) &&
		PinsLinked(FindPin(N.IsMuzzleValid, TEXT("ReturnValue")), FindPin(N.MuzzleValidBranch, TEXT("Condition"))) &&
		PinsLinked(FindPin(N.MuzzleFlash, TEXT("MuzzleFlash")), FindPin(N.MuzzleLocation, TEXT("self"))) &&
		PinsLinked(FindPin(N.MuzzleValidBranch, TEXT("then")), FindPin(N.PlayTracer, TEXT("execute"))) &&
		PinsLinked(FindPin(N.MuzzleLocation, TEXT("ReturnValue")), FindPin(N.PlayTracer, TEXT("MuzzleLocation"))) &&
		PinsLinked(FindPin(N.ShotTracer, TEXT("ShotTracer")), FindPin(N.PlayTracer, TEXT("self"))) &&
		PinsLinked(FindPin(N.ShotEnd, TEXT("ReturnValue")), FindPin(N.PlayTracer, TEXT("ShotEnd")));
	if (!bShotEndValid || !bVisualFlowValid)
	{
		OutError = TEXT("曳光枪口/落点数据连接不完整，或可选表现分支未与原伤害分支隔离。");
		return false;
	}
	return true;
}

static bool BackupBlueprintPackage(UBlueprint* Blueprint, FString& OutDirectory)
{
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	const FString Base = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TracerBackups"), Stamp);
	OutDirectory = Base;
	for (int32 Suffix = 1; IFileManager::Get().DirectoryExists(*OutDirectory); ++Suffix)
	{
		OutDirectory = FString::Printf(TEXT("%s-%02d"), *Base, Suffix);
	}
	if (!IFileManager::Get().MakeDirectory(*OutDirectory, true))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：无法创建备份目录 %s"), *OutDirectory);
		return false;
	}
	const FString PackageName = Blueprint->GetOutermost()->GetName();
	const FString SourceBase = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	const FString Relative = FPackageName::GetLongPackagePath(PackageName).RightChop(6); // Remove "/Game/".
	const FString DestinationBase = FPaths::Combine(OutDirectory, Relative, FPaths::GetCleanFilename(SourceBase));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestinationBase), true);
	const TCHAR* Extensions[] = {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")};
	bool bCopiedPrimary = false;
	for (const TCHAR* Extension : Extensions)
	{
		FString Source = FPaths::ChangeExtension(SourceBase, Extension);
		if (!IFileManager::Get().FileExists(*Source)) continue;
		const FString Destination = FPaths::ChangeExtension(DestinationBase, Extension);
		if (IFileManager::Get().Copy(*Destination, *Source, true, true) != COPY_OK)
		{
			UE_LOG(LogTemp, Error, TEXT("曳光接入：备份文件失败 %s"), *Source);
			return false;
		}
		if (FCString::Stricmp(Extension, TEXT(".uasset")) == 0) bCopiedPrimary = true;
	}
	if (!bCopiedPrimary)
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：未能备份原始 uasset，停止修改：%s"), *SourceBase);
		return false;
	}
	const FString Manifest = FString::Printf(TEXT("Blueprint=%s\nSource=%s\n"), *PackageName, *SourceBase);
	FFileHelper::SaveStringToFile(Manifest, *FPaths::Combine(OutDirectory, TEXT("backup.txt")));
	return true;
}

static bool SaveBlueprint(UBlueprint* Blueprint)
{
	const FString Filename = FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_None;
	Args.bSlowTask = false;
	return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, Args);
}

template <typename TNode>
static TNode* CreateNode(UEdGraph* Graph, const FVector2D& Position, const TCHAR* MarkerSuffix)
{
	TNode* Node = NewObject<TNode>(Graph);
	Node->CreateNewGuid();
	Node->SetFlags(RF_Transactional);
	Node->NodePosX = static_cast<int32>(Position.X);
	Node->NodePosY = static_cast<int32>(Position.Y);
	Graph->AddNode(Node, false, false);
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	StampNode(Node, MarkerSuffix);
	return Node;
}

static UK2Node_CallFunction* CreateCall(UEdGraph* Graph, UFunction* Function,
	const FVector2D& Position, const TCHAR* MarkerSuffix)
{
	UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
	Node->CreateNewGuid();
	Node->SetFlags(RF_Transactional);
	Node->NodePosX = static_cast<int32>(Position.X);
	Node->NodePosY = static_cast<int32>(Position.Y);
	Node->SetFromFunction(Function);
	Graph->AddNode(Node, false, false);
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	StampNode(Node, MarkerSuffix);
	return Node;
}

static UK2Node_VariableGet* CreateVariableGet(UEdGraph* Graph, const FName MemberName,
	UClass* OwnerClass, bool bSelfContext, const FVector2D& Position, const TCHAR* MarkerSuffix)
{
	UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
	Node->CreateNewGuid();
	Node->SetFlags(RF_Transactional);
	Node->NodePosX = static_cast<int32>(Position.X);
	Node->NodePosY = static_cast<int32>(Position.Y);
	if (bSelfContext) Node->VariableReference.SetSelfMember(MemberName);
	else Node->VariableReference.SetExternalMember(MemberName, OwnerClass);
	Graph->AddNode(Node, false, false);
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	StampNode(Node, MarkerSuffix);
	return Node;
}

static bool Connect(UEdGraph* Graph, UEdGraphPin* A, UEdGraphPin* B)
{
	const bool bOK = Graph && Graph->GetSchema() && A && B && Graph->GetSchema()->TryCreateConnection(A, B);
	if (!bOK) UE_LOG(LogTemp, Error, TEXT("Tracer connection failed: %s.%s -> %s.%s"), A ? *A->GetOwningNode()->NodeComment : TEXT("null"), A ? *A->PinName.ToString() : TEXT("null"), B ? *B->GetOwningNode()->NodeComment : TEXT("null"), B ? *B->PinName.ToString() : TEXT("null"));
	return bOK;
}

static void Install()
{
	UBlueprint* Blueprint = LoadWeaponBlueprint();
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：武器基类或 SCS 未加载。"));
		return;
	}
	UEdGraph* Graph = nullptr;
	UK2Node_CallFunction* Trace = nullptr;
	UK2Node_IfThenElse* DamageBranch = nullptr;
	UK2Node_CallFunction* BreakHit = nullptr;
	UEdGraphPin* TraceEndSource = nullptr;
	FString Error;
	if (!FindBaseNodes(Blueprint, Graph, Trace, DamageBranch, BreakHit, TraceEndSource, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：%s"), *Error);
		return;
	}

	USCS_Node* ExistingVariable = Blueprint->SimpleConstructionScript->FindSCSNode(TracerVariableName);
	USCS_Node* ExistingMarkedComponent = FindMarkedTracerNode(Blueprint);
	FGraphNodes ExistingGraphNodes;
	const bool bHasAnyMarker = [&]()
	{
		for (UEdGraphNode* Node : Graph->Nodes) if (Node && Node->NodeComment.StartsWith(MarkerPrefix)) return true;
		return false;
	}();
	if (ExistingVariable || ExistingMarkedComponent || bHasAnyMarker)
	{
		if (ExistingVariable && ExistingVariable == ExistingMarkedComponent && bHasAnyMarker &&
			CollectMarkedNodes(Graph, ExistingGraphNodes, Error) &&
			VerifyConnections(Blueprint, ExistingGraphNodes, Trace, DamageBranch, BreakHit, TraceEndSource, Error))
		{
			UE_LOG(LogTemp, Display, TEXT("曳光接入：已完整安装，未重复添加。"));
			return;
		}
		UE_LOG(LogTemp, Error, TEXT("曳光接入：发现同名 ShotTracer 组件、非本工具组件或局部图标记；拒绝重复/部分接入。%s"), *Error);
		return;
	}

	UBlueprint* PlayerBlueprint = LoadObject<UBlueprint>(nullptr, PlayerBlueprintPath);
	UBlueprint* PresentationWeaponBlueprint = LoadObject<UBlueprint>(nullptr, TEXT("/Game/KINEMATION/TacticalShooterPack/Blueprints/BP_TacticalShooterWeapon.BP_TacticalShooterWeapon"));
	UNiagaraSystem* TracerSystem = LoadObject<UNiagaraSystem>(nullptr, TracerSystemPath);
	UClass* PlayerClass = PlayerBlueprint ? PlayerBlueprint->GeneratedClass : nullptr;
	UClass* PresentationWeaponClass = PresentationWeaponBlueprint ? PresentationWeaponBlueprint->GeneratedClass : nullptr;
	FObjectPropertyBase* ActiveWeaponProperty = PlayerClass ? FindFProperty<FObjectPropertyBase>(PlayerClass, TEXT("TacticalPresentationWeapon")) : nullptr;
	FObjectPropertyBase* MuzzleProperty = PresentationWeaponClass ? FindFProperty<FObjectPropertyBase>(PresentationWeaponClass, TEXT("MuzzleFlash")) : nullptr;
	if (!PlayerClass || !PresentationWeaponClass || !TracerSystem || !ActiveWeaponProperty || !MuzzleProperty ||
		!ActiveWeaponProperty->PropertyClass->IsChildOf(PresentationWeaponClass) ||
		!MuzzleProperty->PropertyClass->IsChildOf(UNiagaraComponent::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：玩家引用、作者枪口组件或 Niagara 曳光资产缺失/类型不符；蓝图未修改。"));
		return;
	}
	UEdGraphPin* DebugPin = FindPin(Trace, TEXT("DrawDebugType"));
	UEdGraphPin* TraceExec = FindPin(Trace, TEXT("then"));
	UEdGraphPin* TraceReturn = FindPin(Trace, TEXT("ReturnValue"));
	UEdGraphPin* OldDamageExec = FindPin(DamageBranch, TEXT("execute"));
	if (!DebugPin || !TraceExec || !TraceReturn || !OldDamageExec || TraceExec->LinkedTo.Num() != 1 ||
		TraceExec->LinkedTo[0] != OldDamageExec || DebugPin->DefaultValue != TEXT("ForDuration"))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：Trace 执行口或 DrawDebugType 与预期基线不符，未修改。"));
		return;
	}
	const FString AssetObjectPath = TracerSystem->GetPathName();
	UE_LOG(LogTemp, Display, TEXT("曳光接入：将使用枪口的 NiagaraComponent 世界位置，并保留原伤害分支。系统=%s"), *AssetObjectPath);

	FString BackupDirectory;
	if (!BackupBlueprintPackage(Blueprint, BackupDirectory)) return;
	UE_LOG(LogTemp, Display, TEXT("曳光接入：修改前蓝图备份=%s"), *BackupDirectory);

	FScopedTransaction Transaction(NSLOCTEXT("Dust2ShotTracer", "Install", "安装曳光表现接线"));
	Blueprint->Modify();
	Graph->Modify();
	Blueprint->SimpleConstructionScript->Modify();
	for (UEdGraphNode* Node : Graph->Nodes) if (Node) Node->Modify();
	Trace->Modify();
	DebugPin->Modify();

	USCS_Node* ComponentNode = Blueprint->SimpleConstructionScript->CreateNode(UShotTracerComponent::StaticClass(), TracerVariableName);
	UShotTracerComponent* ComponentTemplate = ComponentNode ? Cast<UShotTracerComponent>(ComponentNode->ComponentTemplate) : nullptr;
	if (!ComponentNode || !ComponentTemplate)
	{
		Transaction.Cancel();
		UE_LOG(LogTemp, Error, TEXT("曳光接入：无法创建 SCS 组件模板；蓝图尚未保存。"));
		return;
	}
	ComponentTemplate->Modify();
	ComponentTemplate->ComponentTags.Add(FName(ComponentMarker));
	ComponentTemplate->bTracerEnabled = true;
	ComponentTemplate->TracerSystem = TracerSystem;
	ComponentTemplate->TravelSpeed = 30000.0f;
	ComponentTemplate->MaxTravelDuration = 0.15f;
	ComponentTemplate->StreakLength = 80.0f;
	ComponentTemplate->StreakWidth = 1.2f;
	ComponentTemplate->TracerColor = FLinearColor(1.0f, 0.65f, 0.25f, 1.0f);
	Blueprint->SimpleConstructionScript->AddNode(ComponentNode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UK2Node_ExecutionSequence* Sequence = CreateNode<UK2Node_ExecutionSequence>(Graph, FVector2D(300, 100), TEXT("Sequence"));
	UK2Node_CallFunction* AttachParent = CreateCall(Graph,
		AActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AActor, GetAttachParentActor)),
		FVector2D(500, -180), TEXT("AttachParent"));
	UK2Node_DynamicCast* CastPlayer = CreateNode<UK2Node_DynamicCast>(Graph, FVector2D(700, -100), TEXT("CastPlayer"));
	CastPlayer->TargetType = PlayerClass;
	CastPlayer->ReconstructNode();
	UK2Node_VariableGet* TacticalWeapon = CreateVariableGet(Graph, TEXT("TacticalPresentationWeapon"), PlayerClass, false, FVector2D(920, -220), TEXT("TacticalWeapon"));
	UK2Node_CallFunction* IsWeaponValid = CreateCall(Graph,
		UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid)),
		FVector2D(1150, -240), TEXT("IsWeaponValid"));
	UK2Node_IfThenElse* WeaponValidBranch = CreateNode<UK2Node_IfThenElse>(Graph, FVector2D(1380, -80), TEXT("WeaponValidBranch"));
	UK2Node_VariableGet* MuzzleFlash = CreateVariableGet(Graph, TEXT("MuzzleFlash"), PresentationWeaponClass, false, FVector2D(1590, -250), TEXT("MuzzleFlash"));
	UK2Node_CallFunction* IsMuzzleValid = CreateCall(Graph,
		UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid)),
		FVector2D(1810, -260), TEXT("IsMuzzleValid"));
	UK2Node_IfThenElse* MuzzleValidBranch = CreateNode<UK2Node_IfThenElse>(Graph, FVector2D(2030, -100), TEXT("MuzzleValidBranch"));
	UK2Node_CallFunction* MuzzleLocation = CreateCall(Graph,
		USceneComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_GetComponentLocation)),
		FVector2D(2240, -250), TEXT("MuzzleLocation"));
	UK2Node_VariableGet* ShotTracer = CreateVariableGet(Graph, TracerVariableName, nullptr, true, FVector2D(2240, -20), TEXT("ShotTracer"));
	UK2Node_CallFunction* ShotEnd = CreateCall(Graph,
		UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, SelectVector)),
		FVector2D(2240, 100), TEXT("ShotEnd"));
	UK2Node_CallFunction* PlayTracer = CreateCall(Graph,
		UShotTracerComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UShotTracerComponent, PlayShotTracer)),
		FVector2D(2470, -80), TEXT("PlayTracer"));

	// Preserve the original damage branch on Then 1; the new cosmetic path is isolated on Then 0.
	TraceExec->BreakLinkTo(OldDamageExec);
	bool bConnected =
		Connect(Graph, TraceExec, FindPin(Sequence, TEXT("execute"))) &&
		Connect(Graph, Sequence->GetThenPinGivenIndex(0), FindPin(CastPlayer, TEXT("execute"))) &&
		Connect(Graph, Sequence->GetThenPinGivenIndex(1), OldDamageExec) &&
		Connect(Graph, TraceReturn, FindPin(ShotEnd, TEXT("bPickA"))) &&
		Connect(Graph, FindPin(BreakHit, TEXT("ImpactPoint")), FindPin(ShotEnd, TEXT("A"))) &&
		Connect(Graph, TraceEndSource, FindPin(ShotEnd, TEXT("B"))) &&
		Connect(Graph, FindPin(AttachParent, TEXT("ReturnValue")), CastPlayer->GetCastSourcePin()) &&
		Connect(Graph, CastPlayer->GetCastResultPin(), FindPin(TacticalWeapon, TEXT("self"))) &&
		Connect(Graph, CastPlayer->GetValidCastPin(), FindPin(WeaponValidBranch, TEXT("execute"))) &&
		Connect(Graph, FindPin(TacticalWeapon, TEXT("TacticalPresentationWeapon")), FindPin(IsWeaponValid, TEXT("Object"))) &&
		Connect(Graph, FindPin(IsWeaponValid, TEXT("ReturnValue")), FindPin(WeaponValidBranch, TEXT("Condition"))) &&
		Connect(Graph, FindPin(WeaponValidBranch, TEXT("then")), FindPin(MuzzleValidBranch, TEXT("execute"))) &&
		Connect(Graph, FindPin(TacticalWeapon, TEXT("TacticalPresentationWeapon")), FindPin(MuzzleFlash, TEXT("self"))) &&
		Connect(Graph, FindPin(MuzzleFlash, TEXT("MuzzleFlash")), FindPin(IsMuzzleValid, TEXT("Object"))) &&
		Connect(Graph, FindPin(IsMuzzleValid, TEXT("ReturnValue")), FindPin(MuzzleValidBranch, TEXT("Condition"))) &&
		Connect(Graph, FindPin(MuzzleFlash, TEXT("MuzzleFlash")), FindPin(MuzzleLocation, TEXT("self"))) &&
		Connect(Graph, FindPin(MuzzleValidBranch, TEXT("then")), FindPin(PlayTracer, TEXT("execute"))) &&
		Connect(Graph, FindPin(MuzzleLocation, TEXT("ReturnValue")), FindPin(PlayTracer, TEXT("MuzzleLocation"))) &&
		Connect(Graph, FindPin(ShotTracer, TEXT("ShotTracer")), FindPin(PlayTracer, TEXT("self"))) &&
		Connect(Graph, FindPin(ShotEnd, TEXT("ReturnValue")), FindPin(PlayTracer, TEXT("ShotEnd")));
	DebugPin->DefaultValue = TEXT("None");
	Trace->PinDefaultValueChanged(DebugPin);
	FGraphNodes NewNodes;
	const bool bVerified = bConnected && CollectMarkedNodes(Graph, NewNodes, Error) &&
		VerifyConnections(Blueprint, NewNodes, Trace, DamageBranch, BreakHit, TraceEndSource, Error) &&
		ComponentTemplate->TracerSystem == TracerSystem &&
		ComponentTemplate->ComponentTags.Contains(FName(ComponentMarker)) &&
		DebugPin->DefaultValue == TEXT("None");
	if (!bVerified)
	{
		// Undo the in-memory graph mutation and remove only nodes created in this attempt.
		const TArray<UEdGraphNode*> NodesToRemove = Graph->Nodes;
		for (UEdGraphNode* Node : NodesToRemove)
		{
			if (Node && Node->NodeComment.StartsWith(MarkerPrefix)) Graph->RemoveNode(Node);
		}
		if (TraceExec && OldDamageExec) Graph->GetSchema()->TryCreateConnection(TraceExec, OldDamageExec);
		Blueprint->SimpleConstructionScript->RemoveNode(ComponentNode);
		DebugPin->DefaultValue = TEXT("ForDuration");
		Transaction.Cancel();
		UE_LOG(LogTemp, Error, TEXT("曳光接入：完整执行链检查失败，未保存。%s"), *Error);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FCompilerResultsLog CompileResults;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &CompileResults);
	if (CompileResults.NumErrors > 0 || Blueprint->Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：蓝图编译有错误（%d），未保存。已保留修改前备份：%s"), CompileResults.NumErrors, *BackupDirectory);
		return;
	}
	if (!SaveBlueprint(Blueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光接入：编译通过但保存失败；请从备份恢复。备份：%s"), *BackupDirectory);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("曳光接入：安装、完整链路验证、编译和保存通过。原 DrawDebugType=ForDuration 已设为 None。备份：%s"), *BackupDirectory);
}

static void Verify()
{
	UBlueprint* Blueprint = LoadWeaponBlueprint();
	UEdGraph* Graph = nullptr;
	UK2Node_CallFunction* Trace = nullptr;
	UK2Node_IfThenElse* DamageBranch = nullptr;
	UK2Node_CallFunction* BreakHit = nullptr;
	UEdGraphPin* TraceEndSource = nullptr;
	FString Error;
	if (!FindBaseNodes(Blueprint, Graph, Trace, DamageBranch, BreakHit, TraceEndSource, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光验证失败：%s"), *Error);
		return;
	}
	FGraphNodes Nodes;
	if (!CollectMarkedNodes(Graph, Nodes, Error) ||
		!VerifyConnections(Blueprint, Nodes, Trace, DamageBranch, BreakHit, TraceEndSource, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光验证失败：%s"), *Error);
		return;
	}
	USCS_Node* ComponentNode = Blueprint->SimpleConstructionScript ? Blueprint->SimpleConstructionScript->FindSCSNode(TracerVariableName) : nullptr;
	UShotTracerComponent* Component = ComponentNode ? Cast<UShotTracerComponent>(ComponentNode->ComponentTemplate) : nullptr;
	UEdGraphPin* DebugPin = FindPin(Trace, TEXT("DrawDebugType"));
	if (!Component || !IsMarkedComponent(ComponentNode) ||
		Component->TracerSystem != LoadObject<UNiagaraSystem>(nullptr, TracerSystemPath) ||
		!DebugPin || DebugPin->DefaultValue != TEXT("None"))
	{
		UE_LOG(LogTemp, Error, TEXT("曳光验证失败：SCS 组件标记/特效引用或 DrawDebugType=None 不匹配。"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("曳光验证通过：Trace 后 Then0 只走表现（玩家、活动枪、枪口组件各有 IsValid 门禁），Then1 保留原命中伤害分支；命中取原 ImpactPoint，未命中取原 Trace.End；空弹未执行该 Trace；DrawDebugType=None；SCS ShotTracer 使用 %s。"), *GetPathNameSafe(Component->TracerSystem));
}

static FAutoConsoleCommand InstallCommand(
	TEXT("Dust2.InstallTracerWiring"), TEXT("一次性接入并编译保存武器曳光表现，先备份资产"),
	FConsoleCommandDelegate::CreateStatic(&Install));
static FAutoConsoleCommand VerifyCommand(
	TEXT("Dust2.VerifyTracerWiring"), TEXT("只读核验武器曳光完整节点执行链及原命中链"),
	FConsoleCommandDelegate::CreateStatic(&Verify));
}

#endif // WITH_EDITOR
