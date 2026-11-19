#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

struct FWeaponKitTestWorld
{
	UWorld* World = nullptr;

	FWeaponKitTestWorld()
	{
		UWorld::InitializationValues Values;
		Values.AllowAudioPlayback(false)
			.RequiresHitProxies(false)
			.CreatePhysicsScene(false)
			.CreateNavigation(false)
			.CreateAISystem(false)
			.ShouldSimulatePhysics(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Values);
		if (World && GEngine)
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		if (World)
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
	}

	~FWeaponKitTestWorld()
	{
		if (World)
		{
			World->DestroyWorld(false);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World = nullptr;
		}
	}

	UWorld* Get() const { return World; }
};
