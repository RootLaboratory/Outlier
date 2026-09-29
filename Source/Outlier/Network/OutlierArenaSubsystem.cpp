#include "Network/OutlierArenaSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "OutlierArenaSettings.h"
#include "Engine/NetConnection.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	const TCHAR* GetArenaReloadRole(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("NoWorld");
		}
		switch (World->GetNetMode())
		{
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Standalone");
		}
	}

	int32 CountGameplayActors(const ULevel* Level)
	{
		int32 Count = 0;
		if (Level)
		{
			for (const AActor* Actor : Level->Actors)
			{
				Count += Actor && !Actor->IsA<AWorldSettings>() ? 1 : 0;
			}
		}
		return Count;
	}

	bool IsGameplayLevelUnloaded(const ULevelStreaming* Level)
	{
		return Level && !Level->IsLevelLoaded() && !Level->IsLevelVisible()
			&& !Level->GetLoadedLevel();
	}

	bool IsGameplayLevelShown(const ULevelStreaming* Level)
	{
		return Level && Level->IsLevelLoaded() && Level->IsLevelVisible()
			&& Level->GetLoadedLevel();
	}
}

void UOutlierArenaSubsystem::Deinitialize()
{
	if (ArenaPollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ArenaPollTickerHandle);
		ArenaPollTickerHandle.Reset();
	}
	PendingGameplayReload.Reset();
	GameplayStreamingLevels.Reset();
	ReleasingLevels.Reset();
	Super::Deinitialize();
}

void UOutlierArenaSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	ArenaLevel = Settings->ArenaLevel;
	InitialLoadStartTime = FPlatformTime::Seconds();
	if (ArenaLevel.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Configure ArenaLevel in Outlier Arena settings"));
		return;
	}

	if (Settings->ShouldUseExternalArenaHandoff(InWorld.GetNetMode()) && !IsPersistentArenaWorld())
	{
		// Dedicated Lobby는 연결만 넘긴다. 실제 Arena 월드는 별도 워커 프로세스가 소유한다.
		return;
	}

	if (IsPersistentArenaWorld())
	{
		// 전용 워커는 Arena 월드에 등록된 Blueprint 스트리밍 레벨을 그대로 사용한다.
		Arena.ArenaWorld = &InWorld;
		Arena.InstanceTransform = FTransform::Identity;
		if (ResolvePersistentGameplayLevels())
		{
			RequestGameplayLevelsLoaded(true);
			bInitialGameplayLevelsRequested = true;
			EnsureArenaPollTicker();
		}
		return;
	}

	if (InWorld.GetNetMode() == NM_ListenServer)
	{
		// Listen의 실제 Persistent Level은 Title이다. Arena에 등록된 서브레벨은
		// Title에 자동 등록되지 않으므로 Arena 맵의 Levels 목록을 읽어 형제로 로드한다.
		PreloadArena();
	}
}

bool UOutlierArenaSubsystem::IsGameplayLevelsConfigured() const
{
	if (!bGameplayLevelsResolved || GameplaySublevels.Num() > 4)
	{
		return false;
	}
	TSet<FString> Packages;
	for (const TSoftObjectPtr<UWorld>& Map : GameplaySublevels)
	{
		const FString Package = Map.ToSoftObjectPath().GetLongPackageName();
		if (Package.IsEmpty() || Package == ArenaLevel.ToSoftObjectPath().GetLongPackageName()
			|| Packages.Contains(Package))
		{
			return false;
		}
		Packages.Add(Package);
	}
	return true;
}

bool UOutlierArenaSubsystem::ResolveGameplaySublevels(const UWorld* ArenaWorld)
{
	GameplaySublevels.Reset();
	bGameplayLevelsResolved = false;
	if (!ArenaWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Arena map is unavailable for sublevel discovery"));
		return false;
	}
	for (const ULevelStreaming* StreamingLevel : ArenaWorld->GetStreamingLevels())
	{
		if (!StreamingLevel || StreamingLevel->IsA<ULevelStreamingAlwaysLoaded>())
		{
			continue;
		}
		if (!StreamingLevel->IsA<ULevelStreamingDynamic>())
		{
			UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Unsupported gameplay streaming type for %s"),
				*StreamingLevel->GetWorldAssetPackageName());
			GameplaySublevels.Reset();
			return false;
		}
		const FString Package = UWorld::RemovePIEPrefix(StreamingLevel->GetWorldAssetPackageName());
		if (Package.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Arena contains a streaming level without a map"));
			GameplaySublevels.Reset();
			return false;
		}
		const FSoftObjectPath MapPath(Package + TEXT(".") + FPackageName::GetShortName(Package));
		GameplaySublevels.Add(TSoftObjectPtr<UWorld>(MapPath));
	}
	bGameplayLevelsResolved = true;
	if (!IsGameplayLevelsConfigured())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaLevels] Arena map must contain at most 4 unique Blueprint-streamed gameplay sublevels"));
		GameplaySublevels.Reset();
		bGameplayLevelsResolved = false;
		return false;
	}
	if (GameplaySublevels.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaLevels] Arena has no gameplay sublevels; keeping the current Arena map and using a no-op level reload"));
	}
	return true;
}

bool UOutlierArenaSubsystem::ResolvePersistentGameplayLevels()
{
	UWorld* World = GetWorld();
	if (!World || !IsPersistentArenaWorld() || !ResolveGameplaySublevels(World))
	{
		return false;
	}
	GameplayStreamingLevels.Reset();
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel || StreamingLevel->IsA<ULevelStreamingAlwaysLoaded>())
		{
			continue;
		}
		GameplayStreamingLevels.Add(StreamingLevel);
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaLoad][%s] Registered gameplay sublevel Map=%s Instance=%s"),
			GetArenaReloadRole(World),
			*UWorld::RemovePIEPrefix(StreamingLevel->GetWorldAssetPackageName()),
			*StreamingLevel->GetWorldAssetPackageName());
	}
	return GameplayStreamingLevels.Num() == GameplaySublevels.Num();
}

ULevelStreamingDynamic* UOutlierArenaSubsystem::LoadLevelInstance(
	const TSoftObjectPtr<UWorld>& Map, const FString& InstanceName)
{
	UWorld* World = GetWorld();
	if (!World || Map.IsNull())
	{
		return nullptr;
	}
	bool bSuccess = false;
	ULevelStreamingDynamic* Level = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
		World, Map, Arena.InstanceTransform.GetLocation(),
		Arena.InstanceTransform.GetRotation().Rotator(), bSuccess, InstanceName);
	if (!bSuccess || !Level)
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Failed to create %s from %s"),
			*InstanceName, *Map.ToSoftObjectPath().ToString());
		return nullptr;
	}
	Level->SetShouldBeLoaded(true);
	Level->SetShouldBeVisible(true);
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaLoad][%s] Requested instance=%s Map=%s"),
		GetArenaReloadRole(World), *Level->GetWorldAssetPackageName(),
		*Map.ToSoftObjectPath().GetLongPackageName());
	return Level;
}

void UOutlierArenaSubsystem::PreloadArena()
{
	if (!GetWorld() || ArenaLevel.IsNull() || Arena.StreamingLevel || bPreloadAfterRelease)
	{
		return;
	}
	Arena = FOutlierArenaInstance();
	GameplaySublevels.Reset();
	bGameplayLevelsResolved = false;
	Arena.InstanceTransform = FTransform::Identity;
	Arena.StreamingLevel = LoadLevelInstance(ArenaLevel, TEXT("OutlierArena"));
	bArenaShownBroadcast = false;
	bInitialGameplayLevelsRequested = false;
	bInitialLoadStallLogged = false;
	InitialLoadStartTime = FPlatformTime::Seconds();
	EnsureArenaPollTicker();
}

bool UOutlierArenaSubsystem::CreateListenGameplayLevels()
{
	if (!GetWorld() || !Arena.StreamingLevel || GameplayStreamingLevels.Num() != 0)
	{
		return false;
	}
	// 로드된 Arena와 같은 맵 에셋의 Levels 목록을 사용한다. 동적 인스턴스의
	// 서브레벨은 Title 월드에 자동 등록되지 않아 별도 인스턴스로 생성한다.
	const UWorld* ArenaMap = GetArenaWorld();
	if (!ArenaMap || ArenaMap->GetStreamingLevels().IsEmpty())
	{
		ArenaMap = ArenaLevel.LoadSynchronous();
	}
	if (!ResolveGameplaySublevels(ArenaMap))
	{
		bInitialGameplayLevelsRequested = true;
		return false;
	}
	for (int32 Index = 0; Index < GameplaySublevels.Num(); ++Index)
	{
		const FString Name = FString::Printf(TEXT("OutlierGameplay%02d"), Index + 1);
		ULevelStreamingDynamic* Level = LoadLevelInstance(GameplaySublevels[Index], Name);
		if (!Level)
		{
			for (ULevelStreaming* Created : GameplayStreamingLevels)
			{
				Created->SetShouldBeVisible(false);
				Created->SetShouldBeLoaded(false);
				Created->SetIsRequestingUnloadAndRemoval(true);
			}
			GameplayStreamingLevels.Reset();
			bInitialGameplayLevelsRequested = true; // 틱마다 같은 이름의 인스턴스를 중복 생성하지 않는다.
			return false;
		}
		GameplayStreamingLevels.Add(Level);
	}
	bInitialGameplayLevelsRequested = true;
	return true;
}

void UOutlierArenaSubsystem::EnsureArenaLoaded()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Client || ArenaLevel.IsNull())
	{
		return;
	}
	if (IsPersistentArenaWorld())
	{
		if (GameplayStreamingLevels.IsEmpty() && ResolvePersistentGameplayLevels())
		{
			RequestGameplayLevelsLoaded(true);
			bInitialGameplayLevelsRequested = true;
		}
		EnsureArenaPollTicker();
		return;
	}
	if (!Arena.StreamingLevel)
	{
		PreloadArena();
	}
	else
	{
		Arena.StreamingLevel->SetShouldBeLoaded(true);
		Arena.StreamingLevel->SetShouldBeVisible(true);
		EnsureArenaPollTicker();
	}
}

FOutlierArenaInstance* UOutlierArenaSubsystem::AcquireArena()
{
	RefreshArenaReadyState();
	if (Arena.bInUse || !Arena.bReady)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArenaLevels] AcquireArena rejected: content not ready or busy"));
		return nullptr;
	}
	Arena.bInUse = true;
	return &Arena;
}

void UOutlierArenaSubsystem::ReleaseArena()
{
	Arena.bInUse = false;
	Arena.PairId = INDEX_NONE;
	OnArenaReleased.Broadcast();
	if (!Arena.StreamingLevel)
	{
		return; // Worker leaves its real Persistent map when the process exits.
	}
	Arena.bReady = false;
	bArenaShownBroadcast = false;
	PendingGameplayReload.Reset();
	bPreloadAfterRelease = true;
	for (ULevelStreaming* Level : GameplayStreamingLevels)
	{
		if (Level)
		{
			Level->SetShouldBeVisible(false);
			Level->SetShouldBeLoaded(false);
			Level->SetIsRequestingUnloadAndRemoval(true);
			ReleasingLevels.Add(Level);
		}
	}
	GameplayStreamingLevels.Reset();
	Arena.StreamingLevel->SetShouldBeVisible(false);
	Arena.StreamingLevel->SetShouldBeLoaded(false);
	Arena.StreamingLevel->SetIsRequestingUnloadAndRemoval(true);
	ReleasingLevels.Add(Arena.StreamingLevel);
	Arena.StreamingLevel = nullptr;
	EnsureArenaPollTicker();
}

void UOutlierArenaSubsystem::RequestGameplayLevelsLoaded(bool bLoaded)
{
	for (ULevelStreaming* Level : GameplayStreamingLevels)
	{
		if (!Level)
		{
			continue;
		}
		if (bLoaded)
		{
			Level->SetShouldBeLoaded(true);
			Level->SetShouldBeVisible(true);
		}
		else
		{
			Level->SetShouldBeVisible(false);
			Level->SetShouldBeLoaded(false);
		}
	}
}

bool UOutlierArenaSubsystem::AreGameplayLevelsUnloaded() const
{
	if (!IsGameplayLevelsConfigured()
		|| GameplayStreamingLevels.Num() != GameplaySublevels.Num())
	{
		return false;
	}
	for (const ULevelStreaming* Level : GameplayStreamingLevels)
	{
		if (!IsGameplayLevelUnloaded(Level))
		{
			return false;
		}
	}
	return true;
}

bool UOutlierArenaSubsystem::AreGameplayLevelsShown() const
{
	if (!IsGameplayLevelsConfigured()
		|| GameplayStreamingLevels.Num() != GameplaySublevels.Num())
	{
		return false;
	}
	for (const ULevelStreaming* Level : GameplayStreamingLevels)
	{
		if (!IsGameplayLevelShown(Level))
		{
			return false;
		}
	}
	return true;
}

bool UOutlierArenaSubsystem::IsGameplayLevelsReady() const
{
	return !PendingGameplayReload.IsSet() && AreGameplayLevelsShown();
}

bool UOutlierArenaSubsystem::IsArenaContentReady() const
{
	return Arena.bReady && IsGameplayLevelsReady();
}

bool UOutlierArenaSubsystem::IsArenaReady() const
{
	return Arena.bReady;
}

void UOutlierArenaSubsystem::RefreshArenaReadyState()
{
	if (bPreloadAfterRelease || PendingGameplayReload.IsSet())
	{
		return;
	}
	const bool bStaticReady = IsPersistentArenaWorld()
		? GetWorld() && GetWorld()->PersistentLevel
		: Arena.StreamingLevel && Arena.StreamingLevel->IsLevelLoaded()
			&& Arena.StreamingLevel->IsLevelVisible() && Arena.StreamingLevel->GetLoadedLevel();
	Arena.bReady = bStaticReady && AreGameplayLevelsShown();
	if (Arena.bReady && !bArenaShownBroadcast)
	{
		bArenaShownBroadcast = true;
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaLoad][%s] Initial content ready Persistent=%s PersistentActors=%d GameplayLevels=%d"),
			GetArenaReloadRole(GetWorld()), *GetNameSafe(GetArenaLoadedLevel()),
			CountGameplayActors(GetArenaLoadedLevel()), GameplayStreamingLevels.Num());
		// LogGameplayLevelState(0, TEXT("InitialReady"));
		OnArenaShown.Broadcast();
	}
}

uint32 UOutlierArenaSubsystem::ReserveGameplayGeneration()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || PendingGameplayReload.IsSet())
	{
		return 0;
	}
	if (++GameplayGeneration == 0)
	{
		++GameplayGeneration;
	}
	return GameplayGeneration;
}

void UOutlierArenaSubsystem::CaptureGameplayReloadActors(FPendingGameplayReload& Pending)
{
	Pending.StableArenaLevel = GetArenaLoadedLevel();
	// Pending.OldActorCounts.SetNumZeroed(GameplayStreamingLevels.Num());
	// Pending.OldActorsByLevel.SetNum(GameplayStreamingLevels.Num());
	// Pending.UnloadedLevelLogged.Init(0, GameplayStreamingLevels.Num());
	// Pending.ShownLevelLogged.Init(0, GameplayStreamingLevels.Num());
	for (int32 Index = 0; Index < GameplayStreamingLevels.Num(); ++Index)
	{
		const ULevel* Loaded = GameplayStreamingLevels[Index]
			? GameplayStreamingLevels[Index]->GetLoadedLevel() : nullptr;
		if (!Loaded)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaReload][%s] Gen=%u Capture missing loaded level Index=%d Map=%s"),
				GetArenaReloadRole(GetWorld()), Pending.Generation, Index,
				*GameplaySublevels[Index].ToSoftObjectPath().GetLongPackageName());
			continue;
		}
		for (AActor* Actor : Loaded->Actors)
		{
			if (Actor && !Actor->IsA<AWorldSettings>())
			{
				Pending.OldActors.Add(Actor);
				// Pending.OldActorsByLevel[Index].Add(Actor);
				// ++Pending.OldActorCounts[Index];
			}
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReload][%s] Gen=%u Begin Levels=%d OldActors=%d Persistent=%s PersistentActors=%d"),
		GetArenaReloadRole(GetWorld()), Pending.Generation, GameplayStreamingLevels.Num(),
		Pending.OldActors.Num(), *GetNameSafe(Pending.StableArenaLevel.Get()),
		CountGameplayActors(Pending.StableArenaLevel.Get()));
	// LogGameplayLevelState(Pending.Generation, TEXT("BeforeUnload"));
}

/*
void UOutlierArenaSubsystem::LogGameplayLevelState(uint32 Generation, const TCHAR* Event) const
{
	const FPendingGameplayReload* Pending = PendingGameplayReload.IsSet()
		? &PendingGameplayReload.GetValue() : nullptr;
	const double ElapsedMs = Pending
		? (FPlatformTime::Seconds() - Pending->ReloadStartTime) * 1000.0 : 0.0;
	for (int32 Index = 0; Index < GameplaySublevels.Num(); ++Index)
	{
		const ULevelStreaming* Level = GameplayStreamingLevels.IsValidIndex(Index)
			? GameplayStreamingLevels[Index].Get() : nullptr;
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReload][%s] Gen=%u Event=%s Index=%d Map=%s Instance=%s Loaded=%d Visible=%d Actors=%d OldActors=%d ElapsedMs=%.1f"),
			GetArenaReloadRole(GetWorld()), Generation, Event, Index,
			*GameplaySublevels[Index].ToSoftObjectPath().GetLongPackageName(),
			Level ? *Level->GetWorldAssetPackageName() : TEXT("null"),
			Level && Level->IsLevelLoaded() ? 1 : 0,
			Level && Level->IsLevelVisible() ? 1 : 0,
			CountGameplayActors(Level ? Level->GetLoadedLevel() : nullptr),
			Pending && Pending->OldActorCounts.IsValidIndex(Index)
				? Pending->OldActorCounts[Index] : 0, ElapsedMs);
	}
}
*/

bool UOutlierArenaSubsystem::ReloadGameplayLevels(uint32 Generation, bool bWaitForClientAcks)
{
	// 전용 워커는 실제 Arena Persistent 아래의 등록된 스트리밍 레벨을 전환한다.
	// Listen은 Title에 동적으로 만든 Arena 지형과 게임플레이 레벨 중 뒤의 목록만 전환한다.
	// 서브레벨이 없는 맵은 양쪽 모두 Arena를 유지하며 ACK/재시작 흐름만 진행한다.
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || Generation == 0
		|| Generation != GameplayGeneration || PendingGameplayReload.IsSet()
		|| !AreGameplayLevelsShown() || !GetArenaLoadedLevel())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaReload][%s] Reject server reload Gen=%u CurrentGen=%u Phase=%s LevelsReady=%d Persistent=%s"),
			GetArenaReloadRole(GetWorld()), Generation, GameplayGeneration,
			*UEnum::GetValueAsString(GetGameplayReloadPhase()),
			AreGameplayLevelsShown() ? 1 : 0, *GetNameSafe(GetArenaLoadedLevel()));
		return false;
	}
	if (GameplaySublevels.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReload][%s] Gen=%u No gameplay sublevels; Arena map and placed actors remain loaded"),
			GetArenaReloadRole(GetWorld()), Generation);
	}
	PendingGameplayReload.Emplace();
	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	Pending.Generation = Generation;
	Pending.Phase = EOutlierGameplayReloadPhase::WaitingForUnload;
	Pending.PhaseStartTime = FPlatformTime::Seconds();
	Pending.ReloadStartTime = Pending.PhaseStartTime;
	Pending.bCanLoad = !bWaitForClientAcks;
	CaptureGameplayReloadActors(Pending);
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReload][%s] Gen=%u UnloadRequested WaitClientAcks=%d"),
		GetArenaReloadRole(GetWorld()), Generation, bWaitForClientAcks ? 1 : 0);
	Arena.bReady = false;
	OnArenaGameplayReloadStarted.Broadcast(Generation);
	RequestGameplayLevelsLoaded(false);
	EnsureArenaPollTicker();
	return true;
}

bool UOutlierArenaSubsystem::BeginClientGameplayReload(uint32 Generation)
{
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Client
		|| !IsGameplayGenerationNewer(Generation, GameplayGeneration)
		|| PendingGameplayReload.IsSet() || !IsGameplayLevelsConfigured()
		|| GameplayStreamingLevels.Num() != GameplaySublevels.Num()
		|| !GetArenaLoadedLevel())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaReload][Client] Reject client reload Gen=%u CurrentGen=%u Phase=%s Levels=%d/%d Persistent=%s"),
			Generation, GameplayGeneration,
			*UEnum::GetValueAsString(GetGameplayReloadPhase()),
			GameplayStreamingLevels.Num(), GameplaySublevels.Num(),
			*GetNameSafe(GetArenaLoadedLevel()));
		return false;
	}
	if (GameplaySublevels.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReload][Client] Gen=%u No gameplay sublevels; Arena map and placed actors remain loaded"),
			Generation);
	}
	GameplayGeneration = Generation;
	PendingGameplayReload.Emplace();
	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	Pending.Generation = Generation;
	Pending.Phase = EOutlierGameplayReloadPhase::WaitingForUnload;
	Pending.PhaseStartTime = FPlatformTime::Seconds();
	Pending.ReloadStartTime = Pending.PhaseStartTime;
	CaptureGameplayReloadActors(Pending);
	UE_LOG(LogTemp, Display, TEXT("[ArenaReload][Client] Gen=%u UnloadRequested"), Generation);
	Arena.bReady = false;
	OnArenaGameplayReloadStarted.Broadcast(Generation);
	RequestGameplayLevelsLoaded(false);
	EnsureArenaPollTicker();
	return true;
}

void UOutlierArenaSubsystem::AllowGameplayLevelLoad(uint32 Generation)
{
	if (PendingGameplayReload.IsSet() && PendingGameplayReload->Generation == Generation)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReload][%s] Gen=%u LoadAuthorized Phase=%s LocalUnloaded=%d ElapsedMs=%.1f"),
			GetArenaReloadRole(GetWorld()), Generation,
			*UEnum::GetValueAsString(PendingGameplayReload->Phase),
			PendingGameplayReload->bUnloaded ? 1 : 0,
			(FPlatformTime::Seconds() - PendingGameplayReload->ReloadStartTime) * 1000.0);
		PendingGameplayReload->bCanLoad = true;
		EnsureArenaPollTicker();
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReload][%s] Reject load authorization Gen=%u PendingGen=%u Phase=%s"),
			GetArenaReloadRole(GetWorld()), Generation,
			PendingGameplayReload.IsSet() ? PendingGameplayReload->Generation : 0,
			*UEnum::GetValueAsString(GetGameplayReloadPhase()));
	}
}

bool UOutlierArenaSubsystem::IsGameplayReloadUnloaded(uint32 Generation) const
{
	return PendingGameplayReload.IsSet() && PendingGameplayReload->Generation == Generation
		&& PendingGameplayReload->bUnloaded;
}

void UOutlierArenaSubsystem::SuspendGameplayVisibilityForConnection(
	APlayerController* PlayerController) const
{
	UNetConnection* Connection = PlayerController ? PlayerController->GetNetConnection() : nullptr;
	if (!Connection)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReload][%s] No connection while suspending gameplay visibility PC=%s"),
			GetArenaReloadRole(GetWorld()), *GetNameSafe(PlayerController));
		return;
	}
	// Close the old actor channels before the client begins unloading. This matters
	// for placed replicated actors: a late SerializeNewActor against the unloaded
	// level can permanently close its channel for this connection.
	for (const ULevelStreaming* GameplayLevel : GameplayStreamingLevels)
	{
		if (!GameplayLevel)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ArenaReload][%s] Gen=%u Null gameplay streaming level while hiding client visibility"),
				GetArenaReloadRole(GetWorld()), GameplayGeneration);
			continue;
		}
		const FString Expected = UWorld::RemovePIEPrefix(
			GameplayLevel->GetWorldAssetPackageName());
		FName VisiblePackage = NAME_None;
		for (const FName& VisibleName : Connection->ClientVisibleLevelNames)
		{
			if (UWorld::RemovePIEPrefix(VisibleName.ToString()) == Expected)
			{
				VisiblePackage = VisibleName;
				break;
			}
		}
		if (VisiblePackage.IsNone())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaReload][%s] Client level not visible before reload PC=%s Gen=%u Instance=%s"),
				GetArenaReloadRole(GetWorld()), *GetNameSafe(PlayerController),
				GameplayGeneration, *Expected);
			continue;
		}
		FUpdateLevelVisibilityLevelInfo Visibility;
		Visibility.PackageName = VisiblePackage;
		Visibility.bIsVisible = false;
		Visibility.bSkipCloseOnError = true;
		Connection->UpdateLevelVisibility(Visibility);
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReload][%s] SuspendedClientLevel PC=%s Gen=%u Package=%s"),
			GetArenaReloadRole(GetWorld()), *GetNameSafe(PlayerController),
			GameplayGeneration, *VisiblePackage.ToString());
	}
}

EOutlierGameplayReloadPhase UOutlierArenaSubsystem::GetGameplayReloadPhase() const
{
	return PendingGameplayReload.IsSet()
		? PendingGameplayReload->Phase : EOutlierGameplayReloadPhase::Ready;
}

bool UOutlierArenaSubsystem::IsGameplayReloadStalled(uint32 Generation) const
{
	return PendingGameplayReload.IsSet() && PendingGameplayReload->Generation == Generation
		&& PendingGameplayReload->bIsStalled;
}

bool UOutlierArenaSubsystem::IsGameplayGenerationNewer(uint32 Candidate, uint32 Reference)
{
	return Candidate != 0 && static_cast<int32>(Candidate - Reference) > 0;
}

bool UOutlierArenaSubsystem::HasGameplayReloadTimedOut(double ElapsedSeconds, double TimeoutSeconds)
{
	return TimeoutSeconds > 0.0 && ElapsedSeconds >= TimeoutSeconds;
}

bool UOutlierArenaSubsystem::CanCompleteGameplayReload(
	const TArray<TWeakObjectPtr<AActor>>& OldActors,
	bool bUnloaded,
	bool bCanLoad,
	bool bLevelsShown)
{
	if (!bUnloaded || !bCanLoad || !bLevelsShown)
	{
		return false;
	}
	for (const TWeakObjectPtr<AActor>& Actor : OldActors)
	{
		if (Actor.IsValid(true))
		{
			return false;
		}
	}
	return true;
}

void UOutlierArenaSubsystem::SetGameplayReloadPhase(EOutlierGameplayReloadPhase Phase)
{
	if (!PendingGameplayReload.IsSet() || PendingGameplayReload->Phase == Phase)
	{
		return;
	}
	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	const bool bWasStalled = Pending.bIsStalled;
	const EOutlierGameplayReloadPhase PreviousPhase = Pending.Phase;
	const double Now = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReload][%s] Gen=%u Phase=%s->%s PhaseMs=%.1f TotalMs=%.1f"),
		GetArenaReloadRole(GetWorld()), Pending.Generation,
		*UEnum::GetValueAsString(PreviousPhase), *UEnum::GetValueAsString(Phase),
		(Now - Pending.PhaseStartTime) * 1000.0,
		(Now - Pending.ReloadStartTime) * 1000.0);
	Pending.Phase = Phase;
	Pending.PhaseStartTime = Now;
	Pending.bIsStalled = false;
	if (bWasStalled)
	{
		OnArenaGameplayReloadResumed.Broadcast(Pending.Generation);
	}
}

FString UOutlierArenaSubsystem::BuildGameplayReloadDiagnostic() const
{
	if (!PendingGameplayReload.IsSet())
	{
		FString Levels;
		for (const ULevelStreaming* Level : GameplayStreamingLevels)
		{
			Levels += FString::Printf(TEXT(" [%s L=%d V=%d]"),
				Level ? *Level->GetWorldAssetPackageName() : TEXT("null"),
				Level && Level->IsLevelLoaded() ? 1 : 0,
				Level && Level->IsLevelVisible() ? 1 : 0);
		}
		return FString::Printf(TEXT("No pending reload; gameplay levels=%d ready=%d%s"),
			GameplayStreamingLevels.Num(), AreGameplayLevelsShown() ? 1 : 0, *Levels);
	}
	const FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	FString Levels;
	for (const ULevelStreaming* Level : GameplayStreamingLevels)
	{
		Levels += FString::Printf(TEXT(" [%s L=%d V=%d]"),
			Level ? *Level->GetWorldAssetPackageName() : TEXT("null"),
			Level && Level->IsLevelLoaded() ? 1 : 0,
			Level && Level->IsLevelVisible() ? 1 : 0);
	}
	return FString::Printf(
		TEXT("Generation=%u Phase=%s Elapsed=%.2f Unloaded=%d CanLoad=%d GCRequested=%d OldActors=%d%s"),
		Pending.Generation, *UEnum::GetValueAsString(Pending.Phase),
		FPlatformTime::Seconds() - Pending.PhaseStartTime,
		Pending.bUnloaded ? 1 : 0, Pending.bCanLoad ? 1 : 0,
		Pending.bGCRequested ? 1 : 0, Pending.OldActors.Num(), *Levels);
}

/*
void UOutlierArenaSubsystem::DumpGameplayReloadState() const
{
	UE_LOG(LogTemp, Display, TEXT("[ArenaLevels] %s"), *BuildGameplayReloadDiagnostic());
	LogGameplayLevelState(PendingGameplayReload.IsSet()
		? PendingGameplayReload->Generation : GameplayGeneration, TEXT("ManualDump"));
	if (PendingGameplayReload.IsSet())
	{
		int32 TotalSurvivors = 0;
		for (int32 Index = 0; Index < PendingGameplayReload->OldActorsByLevel.Num(); ++Index)
		{
			int32 LevelSurvivors = 0;
			for (const TWeakObjectPtr<AActor>& OldActor : PendingGameplayReload->OldActorsByLevel[Index])
			{
				if (!OldActor.IsValid(true))
				{
					continue;
				}
				if (LevelSurvivors++ < 50)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[ArenaReload][%s] Gen=%u Map=%s OldActorStillValid=%s"),
						GetArenaReloadRole(GetWorld()), PendingGameplayReload->Generation,
						*GameplaySublevels[Index].ToSoftObjectPath().GetLongPackageName(),
						*GetPathNameSafe(OldActor.Get(true)));
				}
			}
			TotalSurvivors += LevelSurvivors;
			UE_LOG(LogTemp, Display,
				TEXT("[ArenaReload][%s] Gen=%u Map=%s OldActorSurvivors=%d%s"),
				GetArenaReloadRole(GetWorld()), PendingGameplayReload->Generation,
				*GameplaySublevels[Index].ToSoftObjectPath().GetLongPackageName(),
				LevelSurvivors,
				LevelSurvivors > 50 ? TEXT(" (first 50 shown)") : TEXT(""));
		}
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReload][%s] Gen=%u OldActorSurvivorsTotal=%d"),
			GetArenaReloadRole(GetWorld()), PendingGameplayReload->Generation,
			TotalSurvivors);
	}
}

void UOutlierArenaSubsystem::DumpGameplayActorState() const
{
	const auto DumpLevel = [this](const ULevel* Level, const FString& Label)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaActors][%s] Level=%s Loaded=%d Actors=%d"),
			GetArenaReloadRole(GetWorld()), *Label, Level ? 1 : 0,
			CountGameplayActors(Level));
		if (!Level)
		{
			return;
		}
		int32 Listed = 0;
		for (const AActor* Actor : Level->Actors)
		{
			if (!Actor || Actor->IsA<AWorldSettings>())
			{
				continue;
			}
			if (Listed++ < 50)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[ArenaActors][%s] Level=%s Actor=%s Class=%s Valid=%d"),
					GetArenaReloadRole(GetWorld()), *Label, *GetPathNameSafe(Actor),
					*GetNameSafe(Actor->GetClass()), IsValid(Actor) ? 1 : 0);
			}
		}
		if (Listed > 50)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[ArenaActors][%s] Level=%s OmittedActors=%d"),
				GetArenaReloadRole(GetWorld()), *Label, Listed - 50);
		}
	};
	DumpLevel(GetArenaLoadedLevel(), TEXT("PersistentArena"));
	for (int32 Index = 0; Index < GameplaySublevels.Num(); ++Index)
	{
		const ULevelStreaming* Streaming = GameplayStreamingLevels.IsValidIndex(Index)
			? GameplayStreamingLevels[Index].Get() : nullptr;
		DumpLevel(Streaming ? Streaming->GetLoadedLevel() : nullptr,
			GameplaySublevels[Index].ToSoftObjectPath().GetLongPackageName());
	}
}
*/

void UOutlierArenaSubsystem::FailGameplayReload(
	uint32 Generation, EOutlierGameplayReloadFailure Failure)
{
	if (!PendingGameplayReload.IsSet())
	{
		if (Generation == 0 || Generation != GameplayGeneration)
		{
			return;
		}
		PendingGameplayReload.Emplace();
		PendingGameplayReload->Generation = Generation;
	}
	if (PendingGameplayReload->Generation != Generation
		|| PendingGameplayReload->Phase == EOutlierGameplayReloadPhase::Failed)
	{
		return;
	}
	PendingGameplayReload->Phase = EOutlierGameplayReloadPhase::Failed;
	PendingGameplayReload->Failure = Failure;
	PendingGameplayReload->bIsStalled = false;
	UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Reload failed: %s"),
		*BuildGameplayReloadDiagnostic());
	OnArenaGameplayReloadFailed.Broadcast(Generation, Failure);
}

bool UOutlierArenaSubsystem::RetryStalledGameplayReload(uint32 Generation)
{
	if (!IsGameplayReloadStalled(Generation))
	{
		return false;
	}
	PendingGameplayReload->bIsStalled = false;
	PendingGameplayReload->PhaseStartTime = FPlatformTime::Seconds();
	OnArenaGameplayReloadResumed.Broadcast(Generation);
	return true;
}

void UOutlierArenaSubsystem::EnsureArenaPollTicker()
{
	if (!ArenaPollTickerHandle.IsValid())
	{
		ArenaPollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UOutlierArenaSubsystem::TickArenaLevels), 0.05f);
	}
}

bool UOutlierArenaSubsystem::TickArenaLevels(float DeltaTime)
{
	(void)DeltaTime;
	if (!GetWorld())
	{
		ArenaPollTickerHandle.Reset();
		return false;
	}

	if (bPreloadAfterRelease)
	{
		for (const TWeakObjectPtr<ULevelStreaming>& LevelPtr : ReleasingLevels)
		{
			if (ULevelStreaming* Level = LevelPtr.Get();
				Level && (Level->IsLevelLoaded() || Level->GetLoadedLevel()
					|| GetWorld()->GetStreamingLevels().Contains(Level)))
			{
				return true;
			}
		}
		ReleasingLevels.Reset();
		bPreloadAfterRelease = false;
		PreloadArena();
	}

	if (!IsPersistentArenaWorld() && Arena.StreamingLevel
		&& Arena.StreamingLevel->IsLevelVisible() && !bInitialGameplayLevelsRequested)
	{
		CreateListenGameplayLevels();
	}

	if (PendingGameplayReload.IsSet()
		&& PendingGameplayReload->Phase != EOutlierGameplayReloadPhase::Failed)
	{
		FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
		/* if (Pending.Phase == EOutlierGameplayReloadPhase::WaitingForUnload)
		{
			for (int32 Index = 0; Index < GameplayStreamingLevels.Num(); ++Index)
			{
				if (!Pending.UnloadedLevelLogged[Index]
					&& IsGameplayLevelUnloaded(GameplayStreamingLevels[Index]))
				{
					Pending.UnloadedLevelLogged[Index] = 1;
					UE_LOG(LogTemp, Display,
						TEXT("[ArenaReload][%s] Gen=%u LevelUnloaded Index=%d Map=%s OldActors=%d ElapsedMs=%.1f"),
						GetArenaReloadRole(GetWorld()), Pending.Generation, Index,
						*GameplaySublevels[Index].ToSoftObjectPath().GetLongPackageName(),
						Pending.OldActorCounts[Index],
						(FPlatformTime::Seconds() - Pending.ReloadStartTime) * 1000.0);
				}
			}
		}
		else if (Pending.Phase == EOutlierGameplayReloadPhase::WaitingForStreaming)
		{
			for (int32 Index = 0; Index < GameplayStreamingLevels.Num(); ++Index)
			{
				if (!Pending.ShownLevelLogged[Index]
					&& IsGameplayLevelShown(GameplayStreamingLevels[Index]))
				{
					Pending.ShownLevelLogged[Index] = 1;
					UE_LOG(LogTemp, Display,
						TEXT("[ArenaReload][%s] Gen=%u LevelShown Index=%d Map=%s NewActors=%d LoadMs=%.1f TotalMs=%.1f"),
						GetArenaReloadRole(GetWorld()), Pending.Generation, Index,
						*GameplaySublevels[Index].ToSoftObjectPath().GetLongPackageName(),
						CountGameplayActors(GameplayStreamingLevels[Index]->GetLoadedLevel()),
						(FPlatformTime::Seconds() - Pending.LoadRequestTime) * 1000.0,
						(FPlatformTime::Seconds() - Pending.ReloadStartTime) * 1000.0);
				}
			}
		} */
		if (Pending.Phase == EOutlierGameplayReloadPhase::WaitingForUnload
			&& AreGameplayLevelsUnloaded())
		{
			// LogGameplayLevelState(Pending.Generation, TEXT("AllLevelsUnloaded"));
			SetGameplayReloadPhase(EOutlierGameplayReloadPhase::WaitingForGCPurge);
			Pending.bGCRequested = true;
			if (GEngine)
			{
				GEngine->ForceGarbageCollection(true);
			}
		}
		else if (Pending.Phase == EOutlierGameplayReloadPhase::WaitingForGCPurge)
		{
			Pending.OldActors.RemoveAll([](const TWeakObjectPtr<AActor>& Actor)
			{
				return !Actor.IsValid(true);
			});
			if (Pending.OldActors.IsEmpty())
			{
				UE_LOG(LogTemp, Display,
					TEXT("[ArenaReload][%s] Gen=%u OldActorGCComplete ElapsedMs=%.1f"),
					GetArenaReloadRole(GetWorld()), Pending.Generation,
					(FPlatformTime::Seconds() - Pending.ReloadStartTime) * 1000.0);
				Pending.bUnloaded = true;
				SetGameplayReloadPhase(EOutlierGameplayReloadPhase::WaitingForClientAcks);
				OnArenaGameplayUnloaded.Broadcast(Pending.Generation);
				OnArenaGameplayGCReady.Broadcast(Pending.Generation);
			}
		}
		else if (Pending.Phase == EOutlierGameplayReloadPhase::WaitingForClientAcks
			&& Pending.bCanLoad)
		{
			Pending.LoadRequestTime = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Display,
				TEXT("[ArenaReload][%s] Gen=%u LevelLoadRequested ElapsedMs=%.1f"),
				GetArenaReloadRole(GetWorld()), Pending.Generation,
				(Pending.LoadRequestTime - Pending.ReloadStartTime) * 1000.0);
			RequestGameplayLevelsLoaded(true);
			SetGameplayReloadPhase(EOutlierGameplayReloadPhase::WaitingForStreaming);
		}
		else if (Pending.Phase == EOutlierGameplayReloadPhase::WaitingForStreaming
			&& AreGameplayLevelsShown())
		{
			// Phase 순서가 어긋나도 옛 배치 Actor와 Client ACK가 남은 채 Ready를 발행하지 않는다.
			if (!CanCompleteGameplayReload(Pending.OldActors, Pending.bUnloaded,
				Pending.bCanLoad, true))
			{
				UE_LOG(LogTemp, Error,
					TEXT("[ArenaReload][%s] Gen=%u RejectReady Unloaded=%d CanLoad=%d OldActors=%d"),
					GetArenaReloadRole(GetWorld()), Pending.Generation,
					Pending.bUnloaded ? 1 : 0, Pending.bCanLoad ? 1 : 0,
					Pending.OldActors.Num());
				FailGameplayReload(Pending.Generation, EOutlierGameplayReloadFailure::InvalidRuntime);
				return true;
			}
			const ULevel* CurrentArenaLevel = GetArenaLoadedLevel();
			if (CurrentArenaLevel != Pending.StableArenaLevel.Get())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[ArenaReload][%s] Gen=%u PersistentArenaChanged Old=%s New=%s"),
					GetArenaReloadRole(GetWorld()), Pending.Generation,
					*GetNameSafe(Pending.StableArenaLevel.Get()), *GetNameSafe(CurrentArenaLevel));
			}
			// LogGameplayLevelState(Pending.Generation, TEXT("AllLevelsShown"));
			UE_LOG(LogTemp, Display,
				TEXT("[ArenaReload][%s] Gen=%u Complete TotalMs=%.1f PersistentActors=%d"),
				GetArenaReloadRole(GetWorld()), Pending.Generation,
				(FPlatformTime::Seconds() - Pending.ReloadStartTime) * 1000.0,
				CountGameplayActors(CurrentArenaLevel));
			const uint32 CompletedGeneration = Pending.Generation;
			PendingGameplayReload.Reset();
			Arena.bReady = true;
			OnArenaGameplayReady.Broadcast(CompletedGeneration);
		}

		if (PendingGameplayReload.IsSet())
		{
			const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
			const double StallSeconds = Settings
				? FMath::Max(static_cast<double>(Settings->ArenaGameplayReloadStallSeconds), 1.0)
				: 15.0;
			FPendingGameplayReload& Current = PendingGameplayReload.GetValue();
			if (!Current.bIsStalled && HasGameplayReloadTimedOut(
				FPlatformTime::Seconds() - Current.PhaseStartTime, StallSeconds))
			{
				Current.bIsStalled = true;
					const FString Diagnostic = BuildGameplayReloadDiagnostic();
					UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Reload stalled: %s"), *Diagnostic);
					// DumpGameplayReloadState();
					OnArenaGameplayReloadStalled.Broadcast(
					Current.Generation, Current.Phase, Diagnostic);
			}
		}
	}

	RefreshArenaReadyState();
	if (!Arena.bReady && !PendingGameplayReload.IsSet() && !bPreloadAfterRelease
		&& !bInitialLoadStallLogged && FPlatformTime::Seconds() - InitialLoadStartTime >= 30.0)
	{
		bInitialLoadStallLogged = true;
		UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] Initial load stalled: %s"),
			*BuildGameplayReloadDiagnostic());
		// LogGameplayLevelState(0, TEXT("InitialLoadStalled"));
	}
	return true;
}

ULevel* UOutlierArenaSubsystem::GetArenaLoadedLevel() const
{
	if (IsPersistentArenaWorld())
	{
		return GetWorld() ? GetWorld()->PersistentLevel : nullptr;
	}
	return Arena.StreamingLevel ? Arena.StreamingLevel->GetLoadedLevel() : nullptr;
}

bool UOutlierArenaSubsystem::IsPersistentArenaWorld() const
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	return Settings && Settings->IsArenaWorld(GetWorld());
}

const UWorld* UOutlierArenaSubsystem::GetArenaWorld() const
{
	if (IsPersistentArenaWorld())
	{
		return GetWorld();
	}
	const ULevel* Level = Arena.StreamingLevel ? Arena.StreamingLevel->GetLoadedLevel() : nullptr;
	return Level ? Level->GetTypedOuter<UWorld>() : nullptr;
}

bool UOutlierArenaSubsystem::IsActorOwnedByArena(const AActor* Actor) const
{
	if (!Actor || !Actor->GetLevel())
	{
		return false;
	}
	if (Actor->GetLevel() == GetArenaLoadedLevel())
	{
		return true;
	}
	for (const ULevelStreaming* Level : GameplayStreamingLevels)
	{
		if (Level && Actor->GetLevel() == Level->GetLoadedLevel())
		{
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR
bool UOutlierArenaSubsystem::IsGameplaySublevelPackage(
	const UWorld* ArenaMap, const FString& LevelPackageName)
{
	if (!ArenaMap || LevelPackageName.IsEmpty())
	{
		return false;
	}

	const FString ActorPackage = UWorld::RemovePIEPrefix(LevelPackageName);
	for (const ULevelStreaming* StreamingLevel : ArenaMap->GetStreamingLevels())
	{
		if (StreamingLevel && StreamingLevel->IsA<ULevelStreamingDynamic>()
			&& !StreamingLevel->IsA<ULevelStreamingAlwaysLoaded>()
			&& ActorPackage == UWorld::RemovePIEPrefix(
				StreamingLevel->GetWorldAssetPackageName()))
		{
			return true;
		}
	}
	return false;
}

bool UOutlierArenaSubsystem::ValidateGameplayActorPlacement(
	const AActor* Actor, FDataValidationContext& Context)
{
	if (!Actor || Actor->IsTemplate())
	{
		return true;
	}
	const UWorld* World = Actor->GetWorld();
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	if (!World || !Settings
		|| !Settings->MatchesArenaPackageName(World->GetOutermost()->GetName()))
	{
		// 일반 테스트 맵과 BP 기본 객체에는 Arena 배치 계약을 적용하지 않는다.
		return true;
	}

	const ULevel* ActorLevel = Actor->GetLevel();
	const FString ActorLevelPackage = ActorLevel && ActorLevel->GetOutermost()
		? ActorLevel->GetOutermost()->GetName() : FString();
	if (IsGameplaySublevelPackage(World, ActorLevelPackage))
	{
		return true;
	}

	Context.AddError(FText::FromString(FString::Printf(
		TEXT("%s is in %s, which is not a reloadable Gameplay sublevel of %s. Move the placed actor to a Blueprint-streamed Gameplay level."),
		*GetNameSafe(Actor), *ActorLevelPackage, *World->GetOutermost()->GetName())));
	return false;
}
#endif
