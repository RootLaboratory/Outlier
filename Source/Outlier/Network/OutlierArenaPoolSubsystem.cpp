// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/OutlierArenaPoolSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"
#include "OutlierArenaSettings.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UOutlierArenaPoolSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UOutlierArenaSettings* Settings =	GetDefault<UOutlierArenaSettings>();

	ArenaLevel = Settings->ArenaLevel;
	MaxArenaCount = Settings->MaxArenaCount;

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

	if (IsPersistentArenaWorld())
	{
		// 설정된 Arena가 이미 Persistent World이므로 다시 스트리밍하면 게임플레이 액터가 중복 생성됨.
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaPool] Using persistent world as Arena Worker ArenaId=0"));
		return;
	}

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
		Arena.bReady = IsStreamingArenaReady(Arena.StreamingLevel);

		if (!Arena.bReady && Arena.StreamingLevel)
		{
			Arena.bReady = Arena.StreamingLevel->IsLevelLoaded();
		}

		if (!Arena.bInUse && Arena.bReady)
		{
			Arena.bInUse = true;
		/*	UE_LOG(LogTemp, Warning,
				TEXT("[ArenaPool] AcquireArena ArenaId=%d Level=%s Transform=%s"),
				Arena.ArenaId,
				*GetNameSafe(Arena.StreamingLevel ? Arena.StreamingLevel->GetLoadedLevel() : nullptr),
				*Arena.InstanceTransform.ToHumanReadableString());*/
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
		OnArenaReleased.Broadcast(ArenaId);

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
		Arena.bReady = IsStreamingArenaReady(NewStreamingLevel);

		return;
	}
}

void UOutlierArenaPoolSubsystem::ReloadArena(int32 ArenaId)
{
	for (FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId != ArenaId || !Arena.StreamingLevel)
		{
			continue;
		}

		// 같은 인스턴스/이름 유지한 채 언로드→재로드. bInUse/PairId는 손대지 않으니 자동 보존.
		Arena.bReady = false;
		BeginDeferredReload(Arena.StreamingLevel);
		return;
	}
}

void UOutlierArenaPoolSubsystem::BeginDeferredReload(ULevelStreamingDynamic* StreamingLevel)
{
	if (!StreamingLevel)
	{
		return;
	}

	// 언로드 요청 (비동기). 완료되면 TickPendingReloads가 다시 로드.
	StreamingLevel->SetShouldBeVisible(false);
	StreamingLevel->SetShouldBeLoaded(false);

	PendingReloadLevels.AddUnique(StreamingLevel);

	UWorld* World = GetWorld();
	if (World && !World->GetTimerManager().IsTimerActive(ReloadPollTimer))
	{
		World->GetTimerManager().SetTimer(
			ReloadPollTimer, this, &UOutlierArenaPoolSubsystem::TickPendingReloads, 0.05f, true);
	}
}

void UOutlierArenaPoolSubsystem::TickPendingReloads()
{
	for (int32 Index = PendingReloadLevels.Num() - 1; Index >= 0; --Index)
	{
		ULevelStreamingDynamic* StreamingLevel = PendingReloadLevels[Index].Get();
		if (!StreamingLevel)
		{
			PendingReloadLevels.RemoveAt(Index);
			continue;
		}

		// 완전히 언로드될 때까지 대기
		if (StreamingLevel->IsLevelLoaded() || StreamingLevel->GetLoadedLevel() != nullptr)
		{
			continue;
		}

		// 언로드 완료 → 같은 인스턴스로 다시 로드 (이름 유지)
		StreamingLevel->SetShouldBeLoaded(true);
		StreamingLevel->SetShouldBeVisible(true);
		PendingReloadLevels.RemoveAt(Index);

		UE_LOG(LogTemp, Warning, TEXT("[ArenaPool] Deferred reload re-load requested Streaming=%s"),
			*GetNameSafe(StreamingLevel));
	}

	if (PendingReloadLevels.Num() == 0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ReloadPollTimer);
		}
	}
}

ULevel* UOutlierArenaPoolSubsystem::GetArenaLoadedLevel(int32 ArenaId) const
{
	const UWorld* World = GetWorld();
	if (ArenaId == 0 && IsPersistentArenaWorld())
	{
		// 기존 ArenaId 기반 호출을 유지하기 위해 Arena Worker의 Persistent Level을 ArenaId 0으로 취급.
		return World ? World->PersistentLevel : nullptr;
	}

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

bool UOutlierArenaPoolSubsystem::IsPersistentArenaWorld() const
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	return Settings && Settings->IsArenaWorld(GetWorld());
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

		/*UE_LOG(LogTemp, Warning,
			TEXT("[ArenaPool] Preloaded ArenaId=%d NetMode=%d Ready=%d Streaming=%s LoadedLevel=%s Transform=%s"),
			Arena.ArenaId,
			static_cast<int32>(World->GetNetMode()),
			Arena.bReady,
			*GetNameSafe(StreamingLevel),
			*GetNameSafe(StreamingLevel->GetLoadedLevel()),
			*Arena.InstanceTransform.ToHumanReadableString());*/
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
	const FString LevelNameOverride = FString::Printf(TEXT("OutlierArena_%d"), ArenaId);
	ULevelStreamingDynamic* StreamingLevel =
		ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
			World,
			ArenaLevel,
			InstanceTransform.GetLocation(),
			InstanceTransform.GetRotation().Rotator(),
			bSuccess,
			LevelNameOverride
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

	/*UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] LoadArenaLevelInstance success ArenaId=%d NetMode=%d Streaming=%s Package=%s Override=%s Transform=%s"),
		ArenaId,
		static_cast<int32>(World->GetNetMode()),
		*GetNameSafe(StreamingLevel),
		*StreamingLevel->GetWorldAssetPackageName(),
		*LevelNameOverride,
		*InstanceTransform.ToHumanReadableString());*/

	return StreamingLevel;
}

void UOutlierArenaPoolSubsystem::EnsureArenaLoaded(int32 ArenaId, bool bForceReload)
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

	if (ArenaId == 0 && IsPersistentArenaWorld())
	{
		return;
	}

	/*UE_LOG(LogTemp, Warning,
		TEXT("[ArenaPool] EnsureArenaLoaded requested ArenaId=%d World=%s NetMode=%d ExistingArenaCount=%d"),
		ArenaId,
		*World->GetName(),
		static_cast<int32>(World->GetNetMode()),
		Arenas.Num());*/

	if (World->GetNetMode() != NM_Client)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaPool] EnsureArenaLoaded skipped on non-client ArenaId=%d NetMode=%d"),
			ArenaId,
			static_cast<int32>(World->GetNetMode()));
		return;
	}

	for (FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId != ArenaId || !Arena.StreamingLevel)
		{
			continue;
		}

		if (!bForceReload)
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

		// bForceReload: 같은 인스턴스 유지한 채 언로드→재로드 (이름 유지 → 서버 리플리케이션 매칭)
		Arena.bReady = false;
		BeginDeferredReload(Arena.StreamingLevel);
		return;
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
	Arena.bReady = IsStreamingArenaReady(StreamingLevel);

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

		/*UE_LOG(LogTemp, Warning,
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
			LoadedLevel ? *LoadedLevel->GetOutermost()->GetName() : TEXT("None"));*/
	}
}

void UOutlierArenaPoolSubsystem::HandleArenaLevelLoaded()
{
	RefreshArenaReadyStates(TEXT("OnLevelLoaded"));
}

void UOutlierArenaPoolSubsystem::HandleArenaLevelShown()
{
	RefreshArenaReadyStates(TEXT("OnLevelShown"));

	const UWorld* World = GetWorld();
	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (IsStreamingArenaReady(Arena.StreamingLevel))
		{
			OnArenaShown.Broadcast(Arena.ArenaId);
		}
	}
}

bool UOutlierArenaPoolSubsystem::IsArenaReady(int32 ArenaId) const
{
	if (ArenaId == 0 && IsPersistentArenaWorld())
	{
		return true;
	}

	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId == ArenaId)
		{
			return Arena.bReady;
		}
	}
	return false;
}

 bool UOutlierArenaPoolSubsystem::IsStreamingArenaReady(const ULevelStreamingDynamic* StreamingLevel)
{
	return StreamingLevel &&
		StreamingLevel->IsLevelLoaded() &&
		StreamingLevel->IsLevelVisible() &&
		StreamingLevel->GetLoadedLevel() != nullptr;
}
