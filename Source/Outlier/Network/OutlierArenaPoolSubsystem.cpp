// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/OutlierArenaPoolSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"

void UOutlierArenaPoolSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_DedicatedServer ||
		InWorld.GetNetMode() == NM_ListenServer)
	{
		ArenaLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/TesttYS.TesttYS")));

		if (!ArenaLevel)
		{
			UE_LOG(LogTemp, Error,TEXT("ArenaLevle Faileed"))
			return;
		}

		PreloadArenas();
	}
}

FOutlierArenaInstance* UOutlierArenaPoolSubsystem::AcquireArena()
{
	for (FOutlierArenaInstance& Arena : Arenas)
	{
		if (!Arena.bReady && Arena.StreamingLevel)
		{
			Arena.bReady = Arena.StreamingLevel->IsLevelLoaded();
		}

		if (!Arena.bInUse && Arena.bReady)
		{
			Arena.bInUse = true;
			return &Arena;
		}
	}

	return nullptr;
}

void UOutlierArenaPoolSubsystem::ReleaseArena(int32 ArenaId)
{
	for (FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId != ArenaId)
		{
			continue;
		}

		Arena.bInUse = false;
		Arena.PairId = INDEX_NONE;
		Arena.bReady = false;

		ULevelStreamingDynamic* OldStreamingLevel = Arena.StreamingLevel;
		Arena.StreamingLevel = nullptr;

		if (OldStreamingLevel)
		{
			OldStreamingLevel->SetShouldBeLoaded(false);
			OldStreamingLevel->SetShouldBeVisible(false);
			OldStreamingLevel->SetIsRequestingUnloadAndRemoval(true);
		}

		ULevelStreamingDynamic* NewStreamingLevel = LoadArenaLevelInstance(Arena.ArenaId, Arena.InstanceTransform);

		if (!NewStreamingLevel)
		{
			continue;
		}

		Arena.StreamingLevel = NewStreamingLevel;
		Arena.bReady = NewStreamingLevel->IsLevelLoaded();
		
		return;
	}
}

ULevel* UOutlierArenaPoolSubsystem::GetArenaLoadedLevel(int32 ArenaId) const
{
	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId == ArenaId && Arena.StreamingLevel)
		{
			return Arena.StreamingLevel->GetLoadedLevel();
		}
	}

	return nullptr;
}

void UOutlierArenaPoolSubsystem::PreloadArenas()
{
	UWorld* World = GetWorld();
	if (!World || ArenaLevel.IsNull())
	{
		return;
	}

	for (int32 Index = 0; Index < MaxArenaCount; ++Index)
	{
		const FVector InstanceLocation(0.0f, 0.0f, Index * 100000.0f);
		const FRotator InstanceRotation = FRotator::ZeroRotator;
		const FTransform InstanceTransform(InstanceRotation, InstanceLocation);

		ULevelStreamingDynamic* StreamingLevel = LoadArenaLevelInstance(Index, InstanceTransform);

		if (!StreamingLevel)
		{
			continue;
		}

		FOutlierArenaInstance Arena;
		Arena.ArenaId = Index;
		Arena.StreamingLevel = StreamingLevel;
		Arena.InstanceTransform = InstanceTransform;
		Arena.bInUse = false;
		Arena.bReady = StreamingLevel->IsLevelLoaded();

		Arenas.Add(Arena);
	}
}

ULevelStreamingDynamic* UOutlierArenaPoolSubsystem::LoadArenaLevelInstance(int32, const FTransform& InstanceTransform)
{
	UWorld* World = GetWorld();
	if (!World || ArenaLevel.IsNull())
	{
		return nullptr;
	}

	bool bSuccess = false;
	ULevelStreamingDynamic* StreamingLevel =
		ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
			World,
			ArenaLevel,
			InstanceTransform.GetLocation(),
			InstanceTransform.GetRotation().Rotator(),
			bSuccess
		);

	if (!bSuccess || !StreamingLevel)
	{
		return nullptr;
	}

	StreamingLevel->SetShouldBeLoaded(true);
	StreamingLevel->SetShouldBeVisible(true);

	return StreamingLevel;
}
