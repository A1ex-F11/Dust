#include "CoreMinimal.h"
#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
namespace ShotTracerInspection
{
static void Inspect()
{
 const TCHAR* Paths[] = {TEXT("/Game/KINEMATION/TacticalShooterPack/Blueprints/BP_TacticalShooterWeapon.BP_TacticalShooterWeapon"), TEXT("/Game/KINEMATION/TacticalShooterPack/Blueprints/Weapons/BP_AK105.BP_AK105"),TEXT("/Game/Code/Player/Player/Bp_WeaponBase.Bp_WeaponBase"), TEXT("/Game/Code/Texts/BP_Dust2_TacticalWeaponAdapter.BP_Dust2_TacticalWeaponAdapter"),TEXT("/Game/Code/Texts/BP_Dust2_TacticalPresentationTest.BP_Dust2_TacticalPresentationTest")};
 for (const TCHAR* Path : Paths)
 {
  UBlueprint* BP=LoadObject<UBlueprint>(nullptr,Path);
  if(!BP){UE_LOG(LogTemp,Warning,TEXT("TRACER_INSPECT missing %s"),Path);continue;}
  UE_LOG(LogTemp,Display,TEXT("TRACER_INSPECT BP %s"),Path);
  if(BP->SimpleConstructionScript) for(USCS_Node* Node:BP->SimpleConstructionScript->GetAllNodes())
  {
   UObject* Template=Node->ComponentTemplate;
   UE_LOG(LogTemp,Display,TEXT("TRACER_INSPECT COMPONENT %s %s"),*Node->GetVariableName().ToString(),*GetNameSafe(Template?Template->GetClass():nullptr));
   if(Template) for(TFieldIterator<FProperty> It(Template->GetClass());It;++It)
   {
    FString Name=It->GetName();
    if(Name.Contains(TEXT("Mesh"))||Name.Contains(TEXT("Socket"))||Name.Contains(TEXT("ChildActorClass")))
    {FString Value;It->ExportText_InContainer(0,Value,Template,Template,Template,PPF_None);UE_LOG(LogTemp,Display,TEXT("TRACER_INSPECT PROP %s=%s"),*Name,*Value);}
   }
  }
  TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
  for(UEdGraph* Graph:Graphs)
  {
   if(BP->GetName()==TEXT("Bp_WeaponBase") && Graph->GetName()!=TEXT("RequestFire"))continue;
   UE_LOG(LogTemp,Display,TEXT("TRACER_INSPECT GRAPH %s"),*Graph->GetName());
   for(UEdGraphNode* Node:Graph->Nodes)
   {
    UE_LOG(LogTemp,Display,TEXT("TRACER_INSPECT NODE %s %s %s"),*Node->NodeGuid.ToString(),*Node->GetClass()->GetName(),*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"),TEXT(" ")));
    for(UEdGraphPin* Pin:Node->Pins)
    {
     FString Links;for(UEdGraphPin* Link:Pin->LinkedTo)Links+=Link->GetOwningNode()->NodeGuid.ToString()+TEXT(":")+Link->PinName.ToString()+TEXT(" ");
     UE_LOG(LogTemp,Display,TEXT("TRACER_INSPECT PIN %s %s default=%s object=%s links=%s"),Pin->Direction==EGPD_Input?TEXT("IN"):TEXT("OUT"),*Pin->PinName.ToString(),*Pin->DefaultValue,*GetPathNameSafe(Pin->DefaultObject),*Links);
    }
   }
  }
 }
}
static FAutoConsoleCommand Command(TEXT("Dust2.InspectTracerWiring"),TEXT("Read-only weapon trace/muzzle wiring inventory"),FConsoleCommandDelegate::CreateStatic(&Inspect));
}
#endif