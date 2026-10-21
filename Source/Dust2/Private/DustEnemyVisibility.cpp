#include "DustEnemyVisibility.h"
#include "AIController.h"
#include "DustEnemyCombatProfile.h"
#include "DustEnemyWeaponComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "WeaponDefinitionData.h"
#include "WeaponBallisticsData.h"

APawn* DustEnemyVisibility::ResolvePawn(const AActor* Actor)
{
	for (int32 Depth = 0; IsValid(Actor) && Depth < 8; ++Depth)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor)) return const_cast<APawn*>(Pawn);
		if (const AController* Controller = Cast<AController>(Actor)) return Controller->GetPawn();
		const AActor* Next = Actor->GetOwner();
		if (!Next) Next = Actor->GetAttachParentActor();
		if (Next == Actor) break;
		Actor = Next;
	}
	return nullptr;
}

bool DustEnemyVisibility::IsFriendlyBlocker(const AActor* Observer, const AActor* Blocker)
{
	const APawn* Source = ResolvePawn(Observer);
	const APawn* Other = ResolvePawn(Blocker);
	if (!IsValid(Source) || !IsValid(Other)
		|| !Source->FindComponentByClass<UDustEnemyWeaponComponent>()
		|| !Other->FindComponentByClass<UDustEnemyWeaponComponent>()) return false;
	if (Source == Other) return true;
	const FGenericTeamId SourceTeam = FGenericTeamId::GetTeamIdentifier(Source->GetController());
	const FGenericTeamId OtherTeam = FGenericTeamId::GetTeamIdentifier(Other->GetController());
	if (SourceTeam != FGenericTeamId::NoTeam || OtherTeam != FGenericTeamId::NoTeam)
		return SourceTeam != FGenericTeamId::NoTeam && SourceTeam == OtherTeam;
	// Current Dust2 enemies share one faction. Explicit different team ids above are not ignored.
	return true;
}

FVector DustEnemyVisibility::GetAimPoint(const AActor* Target, const UDustEnemyCombatProfile* Profile)
{
	if (!IsValid(Target)) return FVector::ZeroVector;
	const float Offset = Profile && FMath::IsFinite(Profile->AimHeightOffset) ? Profile->AimHeightOffset : 50.0f;
	return Target->GetActorLocation() + FVector(0.0f, 0.0f, Offset);
}

bool DustEnemyVisibility::IsInRange(const AActor* Observer, const AActor* Target,
	const UDustEnemyCombatProfile* Profile, const UWeaponDefinitionData* Definition)
{
	if (!IsValid(Observer) || !IsValid(Target) || !IsValid(Profile)
		|| !IsValid(Definition) || !IsValid(Definition->BallisticsData)) return false;
	const float Range = FMath::Min(FMath::IsFinite(Profile->EngagementRange) ? FMath::Max(0.0f, Profile->EngagementRange) : 0.0f,
		FMath::IsFinite(Definition->BallisticsData->TraceDistance) ? FMath::Max(0.0f, Definition->BallisticsData->TraceDistance) : 0.0f);
	const FVector Delta = GetAimPoint(Target, Profile) - Observer->GetActorLocation();
	return !Delta.ContainsNaN() && Delta.SizeSquared() <= FMath::Square(Range);
}

bool DustEnemyVisibility::HasSight(const AActor* Observer, const AActor* Target,
	const FVector& Eye, const FVector& End, int32* Checks)
{
	UWorld* World = IsValid(Observer) ? Observer->GetWorld() : nullptr;
	if (!World || Eye.ContainsNaN() || End.ContainsNaN()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DustEnemyKnowledgeSight), true, Observer);
	if (const APawn* Pawn = ResolvePawn(Observer)) Params.AddIgnoredActor(Pawn);
	for (int32 Index = 0; Index < 64; ++Index)
	{
		FHitResult Hit;
		if (Checks) ++*Checks;
		if (!World->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Params)) return true;
		if (IsValid(Target) && Hit.GetActor() == Target) return true;
		if (!IsFriendlyBlocker(Observer, Hit.GetActor())) return false;
		Params.AddIgnoredActor(Hit.GetActor());
		if (APawn* Friendly = ResolvePawn(Hit.GetActor())) Params.AddIgnoredActor(Friendly);
	}
	return false;
}
