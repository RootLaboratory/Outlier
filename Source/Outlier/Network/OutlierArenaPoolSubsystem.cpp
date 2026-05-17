// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/OutlierArenaPoolSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"

void UOutlierArenaPoolSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	ArenaLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/TestYS.TestYS")));

	if (ArenaLevel.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaPool] ArenaLevel is not set"));
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] OnWorldBeginPlay World=%s NetMode=%d ArenaLevel=%s MaxArenaCount=%d"),
		*InWorld.GetName(),
		static_cast<int32>(InWorld.GetNetMode()),
		*ArenaLevel.ToSoftObjectPath().ToString(),
		MaxArenaCount);

	if (InWorld.GetNetMode() != NM_Client)
	{
		PreloadArenas();
	}
}

FOutlierArenaInstance* UOutlierArenaPoolSubsystem::AcquireArena()
{
	RefreshArenaReadyStates(TEXT("AcquireArena"));

	for (FOutlierArenaInstance& Arena : Arenas)
	{
		if (!Arena.bReady && Arena.StreamingLevel)
		{
			Arena.bReady = Arena.StreamingLevel->IsLevelLoaded();
		}

		if (!Arena.bInUse && Arena.bReady)
		{
			Arena.bInUse = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaPool] AcquireArena ArenaId=%d Level=%s Transform=%s"),
				Arena.ArenaId,
				*GetNameSafe(Arena.StreamingLevel ? Arena.StreamingLevel->GetLoadedLevel() : nullptr),
				*Arena.InstanceTransform.ToHumanReadableString());
			return &Arena;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] AcquireArena failed: no ready arena. ArenaCount=%d"),
		Arenas.Num());
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
	const UWorld* World = GetWorld();

	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId == ArenaId && Arena.StreamingLevel)
		{
			ULevel* LoadedLevel = Arena.StreamingLevel->GetLoadedLevel();
			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaPool] GetArenaLoadedLevel found ArenaId=%d NetMode=%d Loaded=%d Visible=%d Streaming=%s LoadedLevel=%s Package=%s Transform=%s"),
				ArenaId,
				World ? static_cast<int32>(World->GetNetMode()) : -1,
				Arena.StreamingLevel->IsLevelLoaded() ? 1 : 0,
				Arena.StreamingLevel->IsLevelVisible() ? 1 : 0,
				*GetNameSafe(Arena.StreamingLevel),
				*GetNameSafe(LoadedLevel),
				LoadedLevel ? *LoadedLevel->GetOutermost()->GetName() : TEXT("None"),
				*Arena.InstanceTransform.ToHumanReadableString());

			return LoadedLevel;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] GetArenaLoadedLevel missing ArenaId=%d NetMode=%d ArenaCount=%d"),
		ArenaId,
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		Arenas.Num());

	return nullptr;
}

void UOutlierArenaPoolSubsystem::PreloadArenas()
{
	UWorld* World = GetWorld();
	if (!World || ArenaLevel.IsNull())
	{
		return;
	}

	Arenas.Reset();

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

		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaPool] Preloaded ArenaId=%d NetMode=%d Ready=%d Streaming=%s LoadedLevel=%s Transform=%s"),
			Arena.ArenaId,
			static_cast<int32>(World->GetNetMode()),
			Arena.bReady,
			*GetNameSafe(StreamingLevel),
			*GetNameSafe(StreamingLevel->GetLoadedLevel()),
			*Arena.InstanceTransform.ToHumanReadableString());
	}
}

ULevelStreamingDynamic* UOutlierArenaPoolSubsystem::LoadArenaLevelInstance(int32 ArenaId, const FTransform& InstanceTransform)
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
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaPool] LoadArenaLevelInstance failed ArenaId=%d NetMode=%d Level=%s Transform=%s"),
			ArenaId,
			World ? static_cast<int32>(World->GetNetMode()) : -1,
			*ArenaLevel.ToSoftObjectPath().ToString(),
			*InstanceTransform.ToHumanReadableString());
		return nullptr;
	}

	StreamingLevel->OnLevelLoaded.AddUniqueDynamic(this, &UOutlierArenaPoolSubsystem::HandleArenaLevelLoaded);
	StreamingLevel->OnLevelShown.AddUniqueDynamic(this, &UOutlierArenaPoolSubsystem::HandleArenaLevelShown);
	StreamingLevel->SetShouldBeLoaded(true);
	StreamingLevel->SetShouldBeVisible(true);

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] LoadArenaLevelInstance success ArenaId=%d NetMode=%d Streaming=%s Package=%s Transform=%s"),
		ArenaId,
		static_cast<int32>(World->GetNetMode()),
		*GetNameSafe(StreamingLevel),
		*StreamingLevel->GetWorldAssetPackageName(),
		*InstanceTransform.ToHumanReadableString());

	return StreamingLevel;
}

void UOutlierArenaPoolSubsystem::EnsureArenaLoaded(int32 ArenaId)
{
	UWorld* World = GetWorld();

	if (ArenaId == INDEX_NONE)
	{
		return;
	}

	if (!World)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] EnsureArenaLoaded requested ArenaId=%d World=%s NetMode=%d ExistingArenaCount=%d"),
		ArenaId,
		*World->GetName(),
		static_cast<int32>(World->GetNetMode()),
		Arenas.Num());

	if (World->GetNetMode() != NM_Client)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaPool] EnsureArenaLoaded skipped on non-client ArenaId=%d NetMode=%d"),
			ArenaId,
			static_cast<int32>(World->GetNetMode()));
		return;
	}

	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId == ArenaId && Arena.StreamingLevel)
		{
			Arena.StreamingLevel->SetShouldBeLoaded(true);
			Arena.StreamingLevel->SetShouldBeVisible(true);

			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaPool] EnsureArenaLoaded already exists ArenaId=%d Loaded=%d Visible=%d Streaming=%s LoadedLevel=%s"),
				ArenaId,
				Arena.StreamingLevel->IsLevelLoaded() ? 1 : 0,
				Arena.StreamingLevel->IsLevelVisible() ? 1 : 0,
				*GetNameSafe(Arena.StreamingLevel),
				*GetNameSafe(Arena.StreamingLevel->GetLoadedLevel()));
			return;
		}
	}

	const FVector InstanceLocation(0.0f, 0.0f, ArenaId * 100000.0f);
	const FTransform InstanceTransform(FRotator::ZeroRotator, InstanceLocation);

	ULevelStreamingDynamic* StreamingLevel =
		LoadArenaLevelInstance(ArenaId, InstanceTransform);

	if (!StreamingLevel)
	{
		return;
	}

	FOutlierArenaInstance Arena;
	Arena.ArenaId = ArenaId;
	Arena.StreamingLevel = StreamingLevel;
	Arena.InstanceTransform = InstanceTransform;
	Arena.bInUse = true;
	Arena.bReady = StreamingLevel->IsLevelLoaded();

	Arenas.Add(Arena);

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] EnsureArenaLoaded added ArenaId=%d NetMode=%d Ready=%d Streaming=%s LoadedLevel=%s Transform=%s"),
		Arena.ArenaId,
		static_cast<int32>(World->GetNetMode()),
		Arena.bReady,
		*GetNameSafe(StreamingLevel),
		*GetNameSafe(StreamingLevel->GetLoadedLevel()),
		*Arena.InstanceTransform.ToHumanReadableString());
}

void UOutlierArenaPoolSubsystem::RefreshArenaReadyStates(const TCHAR* Reason)
{
	const UWorld* World = GetWorld();

	for (FOutlierArenaInstance& Arena : Arenas)
	{
		if (!Arena.StreamingLevel)
		{
			continue;
		}

		const bool bWasReady = Arena.bReady;
		const bool bLoaded = Arena.StreamingLevel->IsLevelLoaded();
		const bool bVisible = Arena.StreamingLevel->IsLevelVisible();
		ULevel* LoadedLevel = Arena.StreamingLevel->GetLoadedLevel();

		Arena.bReady = bLoaded;

		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaPool] RefreshReady Reason=%s ArenaId=%d NetMode=%d Ready=%d->%d Loaded=%d Visible=%d Streaming=%s LoadedLevel=%s Package=%s"),
			Reason,
			Arena.ArenaId,
			World ? static_cast<int32>(World->GetNetMode()) : -1,
			bWasReady ? 1 : 0,
			Arena.bReady ? 1 : 0,
			bLoaded ? 1 : 0,
			bVisible ? 1 : 0,
			*GetNameSafe(Arena.StreamingLevel),
			*GetNameSafe(LoadedLevel),
			LoadedLevel ? *LoadedLevel->GetOutermost()->GetName() : TEXT("None"));
	}
}

void UOutlierArenaPoolSubsystem::HandleArenaLevelLoaded()
{
	RefreshArenaReadyStates(TEXT("OnLevelLoaded"));
}

void UOutlierArenaPoolSubsystem::HandleArenaLevelShown()
{
	RefreshArenaReadyStates(TEXT("OnLevelShown"));
}
