// Fill out your copyright notice in the Description page of Project Settings.

#include "Network/OutlierArenaSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/NetConnection.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "GameFramework/WorldSettings.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "Misc/PackageName.h"
#include "OutlierArenaSettings.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeCellInterface.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

void UOutlierArenaSubsystem::Deinitialize()
{
	for (const TWeakObjectPtr<UDataLayerManager>& ManagerPtr : BoundGameplayDataLayerManagers)
	{
		if (UDataLayerManager* Manager = ManagerPtr.Get())
		{
			Manager->OnDataLayerInstanceRuntimeStateChanged.RemoveAll(this);
		}
	}
	BoundGameplayDataLayerManagers.Reset();

	for (const TPair<TWeakObjectPtr<UWorldPartitionSubsystem>, FDelegateHandle>& Pair : GameplayStreamingStateHandles)
	{
		if (UWorldPartitionSubsystem* Subsystem = Pair.Key.Get())
		{
			Subsystem->OnStreamingStateUpdated().Remove(Pair.Value);
		}
	}
	GameplayStreamingStateHandles.Reset();

	if (GameplayGarbageCollectCompleteHandle.IsValid())
	{
		FCoreUObjectDelegates::GarbageCollectComplete.Remove(GameplayGarbageCollectCompleteHandle);
		GameplayGarbageCollectCompleteHandle.Reset();
	}

	if (PendingGameplayReload.IsSet())
	{
		for (const TWeakObjectPtr<AActor>& ActorPtr : PendingGameplayReload->TrackedActors)
		{
			if (AActor* Actor = ActorPtr.Get())
			{
				Actor->OnEndPlay.RemoveAll(this);
			}
		}
		PendingGameplayReload.Reset();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	Super::Deinitialize();
}

void UOutlierArenaSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	ArenaLevel = Settings->ArenaLevel;
	GameplayDataLayer = Settings->GameplayDataLayer;

	if (ArenaLevel.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaSubsystem] ArenaLevel is not set"));
		return;
	}

	if (Settings->ShouldUseExternalArenaHandoff(InWorld.GetNetMode()) && !IsPersistentArenaWorld())
	{
		UE_LOG(LogTemp, Display, TEXT("[ArenaSubsystem] Skipping Arena preload because Static Handoff uses an external Worker"));
		return;
	}

	if (IsPersistentArenaWorld())
	{
		Arena = FOutlierArenaInstance();
		Arena.ArenaWorld = &InWorld;
		Arena.InstanceTransform = FTransform::Identity;
		Arena.bReady = true;
		EnsureArenaGameplayDataActivated();
		return;
	}

	if (InWorld.GetNetMode() == NM_ListenServer)
	{
		PreloadArena();
	}
}

FOutlierArenaInstance* UOutlierArenaSubsystem::AcquireArena()
{
	RefreshArenaReadyState(TEXT("AcquireArena"));
	if (Arena.StreamingLevel)
	{
		Arena.bReady = IsStreamingArenaReady(Arena.StreamingLevel)
			|| Arena.StreamingLevel->IsLevelLoaded();
	}

	if (Arena.bInUse || !Arena.bReady)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArenaSubsystem] AcquireArena failed: single Arena is busy or not ready"));
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
		return;
	}

	Arena.bReady = false;
	ULevelStreamingDynamic* OldStreamingLevel = Arena.StreamingLevel;
	Arena.StreamingLevel = nullptr;
	OldStreamingLevel->SetShouldBeLoaded(false);
	OldStreamingLevel->SetShouldBeVisible(false);
	OldStreamingLevel->SetIsRequestingUnloadAndRemoval(true);
	Arena.StreamingLevel = LoadArenaLevelInstance(Arena.InstanceTransform);
	Arena.bReady = IsStreamingArenaReady(Arena.StreamingLevel);
}

void UOutlierArenaSubsystem::ReloadArena()
{
	if (!Arena.StreamingLevel)
	{
		return;
	}

	Arena.bReady = false;
	BeginDeferredReload(Arena.StreamingLevel);
}

const UWorld* UOutlierArenaSubsystem::ResolveDataLayerWorld() const
{
	if (const UWorld* ArenaWorld = GetArenaWorld())
	{
		return ArenaWorld;
	}
	return GetWorld();
}

const UDataLayerInstance* UOutlierArenaSubsystem::ResolveGameplayDataLayer() const
{
	if (GameplayDataLayer.IsNull() || !GetWorld())
	{
		return nullptr;
	}

	const UWorld* DataLayerWorld = ResolveDataLayerWorld();
	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(DataLayerWorld);
	UDataLayerAsset* Asset = GameplayDataLayer.LoadSynchronous();
	return Manager && Asset ? Manager->GetDataLayerInstance(Asset) : nullptr;
}

bool UOutlierArenaSubsystem::SetGameplayDataLayerState(EDataLayerRuntimeState State) const
{
	const UDataLayerInstance* Instance = ResolveGameplayDataLayer();
	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(ResolveDataLayerWorld());
	if (!Manager || !Instance)
	{
		return false;
	}
	return Manager->SetDataLayerInstanceRuntimeState(Instance, State, false);
}

bool UOutlierArenaSubsystem::IsGameplayDataLayerAvailable() const
{
	return ResolveGameplayDataLayer() != nullptr;
}

bool UOutlierArenaSubsystem::IsGameplayDataLayerState(EDataLayerRuntimeState State) const
{
	const UDataLayerInstance* Instance = ResolveGameplayDataLayer();
	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(ResolveDataLayerWorld());
	if (!Manager || !Instance
		|| Manager->GetDataLayerInstanceEffectiveRuntimeState(Instance) != State)
	{
		return false;
	}

	UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UWorldPartitionSubsystem>()
		: nullptr;
	if (!WorldPartitionSubsystem)
	{
		return false;
	}

	FWorldPartitionStreamingQuerySource QuerySource;
	QuerySource.bDataLayersOnly = true;
	QuerySource.bSpatialQuery = false;
	QuerySource.DataLayers.Add(Instance->GetDataLayerFName());

	EWorldPartitionRuntimeCellState CellState = EWorldPartitionRuntimeCellState::Unloaded;
	if (State == EDataLayerRuntimeState::Loaded)
	{
		CellState = EWorldPartitionRuntimeCellState::Loaded;
	}
	else if (State == EDataLayerRuntimeState::Activated)
	{
		CellState = EWorldPartitionRuntimeCellState::Activated;
	}

	return WorldPartitionSubsystem->IsStreamingCompleted(CellState, { QuerySource }, true);
}

uint32 UOutlierArenaSubsystem::ReserveGameplayGeneration()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || PendingGameplayReload.IsSet())
	{
		return 0;
	}

	++GameplayGeneration;
	if (GameplayGeneration == 0)
	{
		++GameplayGeneration;
	}
	return GameplayGeneration;
}

EOutlierGameplayReloadPhase UOutlierArenaSubsystem::GetGameplayReloadPhase() const
{
	return PendingGameplayReload.IsSet()
		? PendingGameplayReload->Phase
		: EOutlierGameplayReloadPhase::Ready;
}

bool UOutlierArenaSubsystem::IsGameplayReloadStalled(uint32 InGameplayGeneration) const
{
	return PendingGameplayReload.IsSet()
		&& PendingGameplayReload->Generation == InGameplayGeneration
		&& PendingGameplayReload->bIsStalled;
}

bool UOutlierArenaSubsystem::IsGameplayReloadGCVerified(uint32 InGameplayGeneration) const
{
	return PendingGameplayReload.IsSet()
		&& PendingGameplayReload->Generation == InGameplayGeneration
		&& PendingGameplayReload->bGCVerified;
}

bool UOutlierArenaSubsystem::IsGameplayGenerationNewer(uint32 Candidate, uint32 Reference)
{
	return Candidate != 0 && static_cast<int32>(Candidate - Reference) > 0;
}

bool UOutlierArenaSubsystem::HasGameplayReloadTimedOut(double ElapsedSeconds, double TimeoutSeconds)
{
	return TimeoutSeconds > 0.0 && ElapsedSeconds >= TimeoutSeconds;
}

bool UOutlierArenaSubsystem::ReloadGameplayData(uint32 InGameplayGeneration, bool bDeferActivation)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client
		|| InGameplayGeneration == 0 || InGameplayGeneration != GameplayGeneration
		|| PendingGameplayReload.IsSet())
	{
		return false;
	}
	if (!IsGameplayDataLayerAvailable())
	{
		FailGameplayReload(InGameplayGeneration, EOutlierGameplayReloadFailure::DataLayerUnavailable);
		return false;
	}

	if (!AddPendingGameplayReload(InGameplayGeneration, !bDeferActivation))
	{
		FailGameplayReload(InGameplayGeneration, EOutlierGameplayReloadFailure::InvalidRuntime);
		return false;
	}

	// Gameplay Data Layer 밖의 런타임 소유자는 셀이 내려가기 전에 현재 세대 객체를 폐기한다.
	OnArenaGameplayReloadStarted.Broadcast(InGameplayGeneration);

	if (!SetGameplayDataLayerState(EDataLayerRuntimeState::Unloaded))
	{
		FailGameplayReload(InGameplayGeneration, EOutlierGameplayReloadFailure::DataLayerStateChangeRejected);
		return false;
	}

	TryRequestGameplayReloadGC();
	return true;
}

void UOutlierArenaSubsystem::WaitForGameplayDataReady(uint32 InGameplayGeneration)
{
	if (!GetWorld() || !IsGameplayGenerationNewer(InGameplayGeneration, GameplayGeneration))
	{
		return;
	}

	GameplayGeneration = InGameplayGeneration;
	if (PendingGameplayReload.IsSet())
	{
		return;
	}

	if (AddPendingGameplayReload(InGameplayGeneration, false))
	{
		TryRequestGameplayReloadGC();
	}
}

void UOutlierArenaSubsystem::ActivateGameplayData(uint32 InGameplayGeneration)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client
		|| InGameplayGeneration == 0 || InGameplayGeneration != GameplayGeneration)
	{
		return;
	}

	if (!PendingGameplayReload.IsSet())
	{
		SetGameplayDataLayerState(EDataLayerRuntimeState::Activated);
		return;
	}

	if (PendingGameplayReload->Generation != InGameplayGeneration)
	{
		return;
	}

	PendingGameplayReload->bCanChangeState = true;
	if (PendingGameplayReload->bLoadRequested)
	{
		ActivatePendingGameplayReload(InGameplayGeneration);
	}
}

FString UOutlierArenaSubsystem::DescribeActorLevelPackage(const AActor* Actor)
{
	const ULevel* Level = Actor ? Actor->GetLevel() : nullptr;
	if (!Level)
	{
		return TEXT("<no level>");
	}
	return FString::Printf(TEXT("%s(Package=%s, IsWPCell=%d)"),
		*GetNameSafe(Actor), *GetNameSafe(Level->GetOutermost()),
		Level->GetWorldPartitionRuntimeCell() ? 1 : 0);
}

bool UOutlierArenaSubsystem::AddPendingGameplayReload(uint32 Generation, bool bCanChangeState)
{
	// Unload 요청 전에 대상 Actor와 이벤트를 확보한다. 이후 EndPlay -> GC 검증 -> 활성화 순으로 진행한다.
	const UDataLayerInstance* Instance = ResolveGameplayDataLayer();
	const UWorld* ArenaWorld = GetArenaWorld();
	UWorld* HostWorld = GetWorld();
	if (!Instance || !ArenaWorld || !HostWorld || PendingGameplayReload.IsSet())
	{
		return false;
	}

	PendingGameplayReload.Emplace();
	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	Pending.Generation = Generation;
	Pending.DataLayerInstance = const_cast<UDataLayerInstance*>(Instance);
	Pending.WorldPartitionSubsystem = HostWorld->GetSubsystem<UWorldPartitionSubsystem>();
	Pending.bCanChangeState = bCanChangeState;
	Pending.Phase = EOutlierGameplayReloadPhase::WaitingForActorEndPlay;
	Pending.PhaseStartTime = HostWorld->GetTimeSeconds();

	UDataLayerAsset* Asset = GameplayDataLayer.LoadSynchronous();
	if (Asset)
	{
		for (ULevel* Level : HostWorld->GetLevels())
		{
			if (!Level || GetOwningArenaWorld(Level) != ArenaWorld)
			{
				continue;
			}

			for (AActor* Actor : Level->Actors)
			{
				if (!Actor || !Actor->ContainsDataLayer(Asset)
					|| (Level != HostWorld->PersistentLevel && Actor->IsA<AWorldSettings>())
					|| !Actor->HasActorBegunPlay())
				{
					continue;
				}

				Pending.TrackedActors.Add(Actor);
				Pending.ActorsAwaitingEndPlay.Add(Actor);
				Actor->OnEndPlay.AddUniqueDynamic(this, &UOutlierArenaSubsystem::HandleGameplayReloadActorEndPlay);
			}
		}
	}

	BindGameplayReloadEvents();
	UE_LOG(LogTemp, Display, TEXT("[ArenaSubsystem][DataLayer] Tracking reload Generation=%u Actors=%d"),
		Generation, Pending.TrackedActors.Num());

	if (!HostWorld->GetTimerManager().IsTimerActive(GameplayReloadTimeoutTimer))
	{
		HostWorld->GetTimerManager().SetTimer(GameplayReloadTimeoutTimer, this,
			&UOutlierArenaSubsystem::TickPendingGameplayReloadTimeouts, 0.5f, true);
	}
	return true;
}

void UOutlierArenaSubsystem::BindGameplayReloadEvents()
{
	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(ResolveDataLayerWorld());
	if (Manager && !BoundGameplayDataLayerManagers.Contains(Manager))
	{
		Manager->OnDataLayerInstanceRuntimeStateChanged.AddUniqueDynamic(
			this, &UOutlierArenaSubsystem::HandleGameplayDataLayerStateChanged);
		BoundGameplayDataLayerManagers.Add(Manager);
	}

	UWorldPartitionSubsystem* StreamingSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UWorldPartitionSubsystem>()
		: nullptr;
	if (StreamingSubsystem && !GameplayStreamingStateHandles.Contains(StreamingSubsystem))
	{
		GameplayStreamingStateHandles.Add(StreamingSubsystem,
			StreamingSubsystem->OnStreamingStateUpdated().AddUObject(
				this, &UOutlierArenaSubsystem::HandleGameplayStreamingStateUpdated));
	}

	if (!GameplayGarbageCollectCompleteHandle.IsValid())
	{
		GameplayGarbageCollectCompleteHandle = FCoreUObjectDelegates::GarbageCollectComplete.AddUObject(
			this, &UOutlierArenaSubsystem::HandleGameplayGarbageCollectComplete);
	}
}

void UOutlierArenaSubsystem::HandleGameplayReloadActorEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	(void)EndPlayReason;
	if (Actor && PendingGameplayReload.IsSet())
	{
		PendingGameplayReload->ActorsAwaitingEndPlay.RemoveAll(
			[Actor](const TWeakObjectPtr<AActor>& ActorPtr) { return ActorPtr.Get(true) == Actor; });
		TryRequestGameplayReloadGC();
	}
}

void UOutlierArenaSubsystem::HandleGameplayDataLayerStateChanged(
	const UDataLayerInstance* DataLayer, EDataLayerRuntimeState State)
{
	if (!DataLayer || !PendingGameplayReload.IsSet()
		|| DataLayer != PendingGameplayReload->DataLayerInstance.Get())
	{
		return;
	}

	if (State == EDataLayerRuntimeState::Unloaded)
	{
		TryRequestGameplayReloadGC();
	}
	else if (State == EDataLayerRuntimeState::Activated)
	{
		TryCompleteGameplayReloadActivation();
	}
}

void UOutlierArenaSubsystem::TryRequestGameplayReloadGC()
{
	// Data Layer가 내려갔다는 통보만으로는 부족하다. 추적한 Actor의 EndPlay도 모두 끝나야 GC를 요청한다.
	if (!PendingGameplayReload.IsSet())
	{
		return;
	}

	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	if (Pending.bGCRequested || Pending.bLoadRequested || Pending.ActorsAwaitingEndPlay.Num() > 0)
	{
		return;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(ResolveDataLayerWorld());
	if (!Manager || !Pending.DataLayerInstance.IsValid()
		|| Manager->GetDataLayerInstanceEffectiveRuntimeState(Pending.DataLayerInstance.Get())
			!= EDataLayerRuntimeState::Unloaded)
	{
		return;
	}

	Pending.bGCRequested = true;
	SetGameplayReloadPhase(EOutlierGameplayReloadPhase::WaitingForGCPurge);
	if (GEngine)
	{
		GEngine->ForceGarbageCollection(true);
	}
}

void UOutlierArenaSubsystem::HandleGameplayGarbageCollectComplete()
{
	// 전역 GC 완료 이벤트는 재로드 완료가 아니다. 이번 세대가 추적한 이전 Actor의 소멸을 따로 확인한다.
	if (!PendingGameplayReload.IsSet())
	{
		return;
	}

	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	if (!Pending.bGCRequested || Pending.bLoadRequested)
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.TrackedActors)
	{
		if (ActorPtr.IsValid(true))
		{
			UE_LOG(LogTemp, Error, TEXT("[ArenaSubsystem][DataLayer] Generation=%u old actor still alive: %s"),
				Pending.Generation, *DescribeActorLevelPackage(ActorPtr.Get(true)));
			return;
		}
	}

	Pending.bGCVerified = true;
	Pending.bLoadRequested = true;
	SetGameplayReloadPhase(Pending.bCanChangeState
		? EOutlierGameplayReloadPhase::ActivatingGameplayData
		: EOutlierGameplayReloadPhase::WaitingForClientAcks);
	// 로컬 GC 검증을 알린 뒤 외부 조정자가 활성화를 허용한다. 콜백 중 Reset될 수 있어 Pending을 다시 찾는다.
	const uint32 VerifiedGeneration = Pending.Generation;
	OnArenaGameplayGCReady.Broadcast(VerifiedGeneration);
	if (PendingGameplayReload.IsSet()
		&& PendingGameplayReload->Generation == VerifiedGeneration
		&& PendingGameplayReload->bCanChangeState)
	{
		ActivatePendingGameplayReload(VerifiedGeneration);
	}
}

void UOutlierArenaSubsystem::ActivatePendingGameplayReload(uint32 Generation)
{
	if (!PendingGameplayReload.IsSet() || PendingGameplayReload->Generation != Generation
		|| !PendingGameplayReload->bLoadRequested || !PendingGameplayReload->bCanChangeState)
	{
		return;
	}
	SetGameplayReloadPhase(EOutlierGameplayReloadPhase::ActivatingGameplayData);
	if (!SetGameplayDataLayerState(EDataLayerRuntimeState::Activated))
	{
		FailGameplayReload(Generation, EOutlierGameplayReloadFailure::DataLayerStateChangeRejected);
		return;
	}
	SetGameplayReloadPhase(EOutlierGameplayReloadPhase::WaitingForStreaming);
	TryCompleteGameplayReloadActivation();
}

void UOutlierArenaSubsystem::HandleGameplayStreamingStateUpdated()
{
	TryRequestGameplayReloadGC();
	TryCompleteGameplayReloadActivation();
}

void UOutlierArenaSubsystem::TryCompleteGameplayReloadActivation()
{
	// Activated 요청과 실제 준비 완료를 구분한다. 아래 상태 조회에서 Data Layer와 Streaming 완료를 함께 검사한다.
	UWorld* World = GetWorld();
	const bool bRequiresActivationPermission = World && World->GetNetMode() != NM_Client;
	if (!PendingGameplayReload.IsSet() || !PendingGameplayReload->bLoadRequested
		|| (bRequiresActivationPermission && !PendingGameplayReload->bCanChangeState)
		|| !IsGameplayDataLayerState(EDataLayerRuntimeState::Activated))
	{
		return;
	}

	const uint32 CompletedGeneration = PendingGameplayReload->Generation;
	ClearGameplayReloadActorBindings();
	PendingGameplayReload.Reset();

	if (World)
	{
		World->GetTimerManager().ClearTimer(GameplayReloadTimeoutTimer);
	}
	OnArenaGameplayReady.Broadcast(CompletedGeneration);
}

void UOutlierArenaSubsystem::TickPendingGameplayReloadTimeouts()
{
	// 이 타이머는 정체 진단만 담당한다. 시간 초과를 성공으로 간주하거나 다음 단계로 강제 전환하지 않는다.
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const double TimeoutSeconds = Settings
		? FMath::Max(static_cast<double>(Settings->ArenaGameplayReloadStallSeconds), 1.0)
		: 15.0;
	UWorld* World = GetWorld();
	if (!World || !PendingGameplayReload.IsSet())
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(GameplayReloadTimeoutTimer);
		}
		return;
	}

	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	if (!Pending.bStallReported
		&& HasGameplayReloadTimedOut(World->GetTimeSeconds() - Pending.PhaseStartTime, TimeoutSeconds))
	{
		ReportStalledGameplayReload();
	}
}

void UOutlierArenaSubsystem::SetGameplayReloadPhase(EOutlierGameplayReloadPhase NewPhase)
{
	if (!PendingGameplayReload.IsSet() || PendingGameplayReload->Phase == NewPhase)
	{
		return;
	}

	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	const bool bWasStalled = Pending.bIsStalled;
	// 전체 리로드 시간이 아니라 각 단계가 실제로 멈춘 시간을 감시한다. 정상적으로 다음 단계로
	// 넘어간 작업이 앞 단계에서 사용한 시간을 이어받아 곧바로 Stalled 되는 것을 막는다.
	Pending.Phase = NewPhase;
	Pending.PhaseStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Pending.bStallReported = false;
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
		return TEXT("No pending gameplay reload");
	}

	const FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	const double Elapsed = GetWorld()
		? GetWorld()->GetTimeSeconds() - Pending.PhaseStartTime
		: 0.0;
	return FString::Printf(
		TEXT("Generation=%u Phase=%s PhaseElapsed=%.2f AwaitingEndPlay=%d TrackedActors=%d GCRequested=%d GCVerified=%d LoadRequested=%d CanActivate=%d DataLayerActivated=%d"),
		Pending.Generation,
		*UEnum::GetValueAsString(Pending.Phase),
		Elapsed,
		Pending.ActorsAwaitingEndPlay.Num(),
		Pending.TrackedActors.Num(),
		Pending.bGCRequested ? 1 : 0,
		Pending.bGCVerified ? 1 : 0,
		Pending.bLoadRequested ? 1 : 0,
		Pending.bCanChangeState ? 1 : 0,
		IsGameplayDataLayerState(EDataLayerRuntimeState::Activated) ? 1 : 0);
}

void UOutlierArenaSubsystem::ReportStalledGameplayReload()
{
	if (!PendingGameplayReload.IsSet() || PendingGameplayReload->bStallReported)
	{
		return;
	}

	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	Pending.bStallReported = true;
	Pending.bIsStalled = true;
	const FString Diagnostic = BuildGameplayReloadDiagnostic();
	UE_LOG(LogTemp, Error, TEXT("[ArenaSubsystem][DataLayer] Reload STALLED %s"), *Diagnostic);
	OnArenaGameplayReloadStalled.Broadcast(Pending.Generation, Pending.Phase, Diagnostic);
}

void UOutlierArenaSubsystem::ClearGameplayReloadActorBindings()
{
	if (!PendingGameplayReload.IsSet())
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& ActorPtr : PendingGameplayReload->TrackedActors)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actor->OnEndPlay.RemoveAll(this);
		}
	}
}

void UOutlierArenaSubsystem::FailGameplayReload(
	uint32 InGameplayGeneration,
	EOutlierGameplayReloadFailure Failure)
{
	if (!PendingGameplayReload.IsSet())
	{
		if (InGameplayGeneration == 0 || InGameplayGeneration != GameplayGeneration)
		{
			return;
		}
		// Data Layer 조회처럼 Pending 생성 전 실패도 명시적인 Failed 상태로 남긴다.
		// 상태가 비어 있으면 호출자가 Ready로 오인해 Pawn을 다시 Possess할 수 있다.
		PendingGameplayReload.Emplace();
		PendingGameplayReload->Generation = InGameplayGeneration;
	}

	if (PendingGameplayReload->Generation != InGameplayGeneration
		|| PendingGameplayReload->Phase == EOutlierGameplayReloadPhase::Failed)
	{
		return;
	}

	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	Pending.Phase = EOutlierGameplayReloadPhase::Failed;
	Pending.Failure = Failure;
	Pending.bIsStalled = false;
	ClearGameplayReloadActorBindings();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameplayReloadTimeoutTimer);
	}
	UE_LOG(LogTemp, Error,
		TEXT("[ArenaSubsystem][DataLayer] Reload FAILED Generation=%u Failure=%s"),
		InGameplayGeneration,
		*UEnum::GetValueAsString(Failure));
	OnArenaGameplayReloadFailed.Broadcast(InGameplayGeneration, Failure);
}

bool UOutlierArenaSubsystem::RetryStalledGameplayReload(uint32 InGameplayGeneration)
{
	if (!IsGameplayReloadStalled(InGameplayGeneration)
		|| PendingGameplayReload->Phase == EOutlierGameplayReloadPhase::Failed)
	{
		return false;
	}

	FPendingGameplayReload& Pending = PendingGameplayReload.GetValue();
	// 재시도는 현재 Phase의 검사를 다시 수행할 뿐 다음 Phase를 강제로 선택하지 않는다.
	// 실제 EndPlay/GC/Streaming 완료 이벤트가 와야만 기존 상태 전이가 계속된다.
	Pending.bIsStalled = false;
	Pending.bStallReported = false;
	Pending.PhaseStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	OnArenaGameplayReloadResumed.Broadcast(InGameplayGeneration);

	switch (Pending.Phase)
	{
	case EOutlierGameplayReloadPhase::WaitingForActorEndPlay:
		Pending.ActorsAwaitingEndPlay.RemoveAll([](const TWeakObjectPtr<AActor>& Actor)
		{
			return !Actor.IsValid(true);
		});
		TryRequestGameplayReloadGC();
		break;
	case EOutlierGameplayReloadPhase::WaitingForGCPurge:
		Pending.bGCRequested = false;
		TryRequestGameplayReloadGC();
		break;
	case EOutlierGameplayReloadPhase::ActivatingGameplayData:
		ActivatePendingGameplayReload(InGameplayGeneration);
		break;
	case EOutlierGameplayReloadPhase::WaitingForStreaming:
		TryCompleteGameplayReloadActivation();
		break;
	case EOutlierGameplayReloadPhase::WaitingForClientAcks:
	case EOutlierGameplayReloadPhase::Ready:
	case EOutlierGameplayReloadPhase::Failed:
	default:
		break;
	}
	return true;
}

void UOutlierArenaSubsystem::DumpGameplayReloadState() const
{
	UE_LOG(LogTemp, Display, TEXT("[ArenaSubsystem][DataLayer] %s"), *BuildGameplayReloadDiagnostic());
}

void UOutlierArenaSubsystem::EnsureArenaGameplayDataActivated()
{
	if (GameplayDataLayer.IsNull())
	{
		return;
	}

	if (ResolveGameplayDataLayer())
	{
		SetGameplayDataLayerState(EDataLayerRuntimeState::Activated);
		bInitialActivationPending = false;
		return;
	}

	UWorld* World = GetWorld();
	if (!World || bInitialActivationPending)
	{
		return;
	}
	bInitialActivationPending = true;
	InitialActivationStartTime = World->GetTimeSeconds();
	World->GetTimerManager().SetTimer(InitialActivationPollTimer, this,
		&UOutlierArenaSubsystem::TickPendingInitialActivation, 0.05f, true);
}

void UOutlierArenaSubsystem::TickPendingInitialActivation()
{
	UWorld* World = GetWorld();
	if (!World || !bInitialActivationPending)
	{
		return;
	}

	if (ResolveGameplayDataLayer())
	{
		SetGameplayDataLayerState(EDataLayerRuntimeState::Activated);
		bInitialActivationPending = false;
		World->GetTimerManager().ClearTimer(InitialActivationPollTimer);
	}
	else if (World->GetTimeSeconds() - InitialActivationStartTime > 10.0)
	{
		bInitialActivationPending = false;
		World->GetTimerManager().ClearTimer(InitialActivationPollTimer);
		UE_LOG(LogTemp, Error, TEXT("[ArenaSubsystem][DataLayer] Initial activation timed out"));
	}
}

void UOutlierArenaSubsystem::SuspendArenaVisibilityForConnection(APlayerController* PlayerController)
{
	UNetConnection* Connection = PlayerController ? PlayerController->GetNetConnection() : nullptr;
	if (!Connection || !Arena.StreamingLevel)
	{
		return;
	}

	const FString BasePackageName = Arena.StreamingLevel->GetWorldAssetPackageName();
	const FString BaseShortName = FPackageName::GetShortName(BasePackageName);
	const FString CellPrefix = BaseShortName + TEXT("_");
	TArray<FName> NamesToHide;
	for (const FName& VisibleLevelName : Connection->ClientVisibleLevelNames)
	{
		const FString VisibleShortName = FPackageName::GetShortName(VisibleLevelName.ToString());
		if (VisibleShortName == BaseShortName || VisibleShortName.StartsWith(CellPrefix, ESearchCase::CaseSensitive))
		{
			NamesToHide.Add(VisibleLevelName);
		}
	}

	for (const FName& NameToHide : NamesToHide)
	{
		FUpdateLevelVisibilityLevelInfo Visibility;
		Visibility.PackageName = NameToHide;
		Visibility.bIsVisible = false;
		Visibility.bSkipCloseOnError = true;
		Connection->UpdateLevelVisibility(Visibility);
	}
}

void UOutlierArenaSubsystem::BeginDeferredReload(ULevelStreamingDynamic* StreamingLevel)
{
	if (!StreamingLevel)
	{
		return;
	}
	StreamingLevel->SetShouldBeVisible(false);
	StreamingLevel->SetShouldBeLoaded(false);
	PendingReloadLevels.AddUnique(StreamingLevel);
	if (UWorld* World = GetWorld(); World && !World->GetTimerManager().IsTimerActive(ReloadPollTimer))
	{
		World->GetTimerManager().SetTimer(ReloadPollTimer, this,
			&UOutlierArenaSubsystem::TickPendingReloads, 0.05f, true);
	}
}

void UOutlierArenaSubsystem::TickPendingReloads()
{
	for (int32 Index = PendingReloadLevels.Num() - 1; Index >= 0; --Index)
	{
		ULevelStreamingDynamic* StreamingLevel = PendingReloadLevels[Index].Get();
		if (!StreamingLevel)
		{
			PendingReloadLevels.RemoveAt(Index);
		}
		else if (!StreamingLevel->IsLevelLoaded() && !StreamingLevel->GetLoadedLevel())
		{
			PendingReloadLevels.RemoveAt(Index);
			PendingGCLevels.AddUnique(StreamingLevel);
			bReloadGCRequested = true;
		}
	}

	if (bReloadGCRequested)
	{
		bReloadGCRequested = false;
		if (GEngine)
		{
			GEngine->ForceGarbageCollection(true);
		}
		return;
	}

	for (const TWeakObjectPtr<ULevelStreamingDynamic>& PendingGCLevel : PendingGCLevels)
	{
		if (ULevelStreamingDynamic* StreamingLevel = PendingGCLevel.Get())
		{
			StreamingLevel->SetShouldBeLoaded(true);
			StreamingLevel->SetShouldBeVisible(true);
		}
	}
	PendingGCLevels.Reset();

	if (PendingReloadLevels.IsEmpty())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ReloadPollTimer);
		}
	}
}

ULevel* UOutlierArenaSubsystem::GetArenaLoadedLevel() const
{
	if (!Arena.StreamingLevel)
	{
		UWorld* ArenaWorld = Arena.ArenaWorld.Get();
		return ArenaWorld ? ArenaWorld->PersistentLevel : nullptr;
	}
	return Arena.StreamingLevel->GetLoadedLevel();
}

bool UOutlierArenaSubsystem::IsPersistentArenaWorld() const
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	return Settings && Settings->IsArenaWorld(GetWorld());
}

const UWorld* UOutlierArenaSubsystem::GetOwningArenaWorld(const ULevel* Level)
{
	if (!Level)
	{
		return nullptr;
	}
	if (const IWorldPartitionCell* Cell = Level->GetWorldPartitionRuntimeCell())
	{
		return Cell->GetOuterWorld();
	}
	return Level->GetTypedOuter<UWorld>();
}

const UWorld* UOutlierArenaSubsystem::ResolveArenaWorld(const FOutlierArenaInstance& InArena)
{
	return InArena.StreamingLevel
		? GetOwningArenaWorld(InArena.StreamingLevel->GetLoadedLevel())
		: InArena.ArenaWorld.Get();
}

const UWorld* UOutlierArenaSubsystem::GetArenaWorld() const
{
	return ResolveArenaWorld(Arena);
}

bool UOutlierArenaSubsystem::IsActorOwnedByArena(const AActor* Actor) const
{
	return Actor && GetOwningArenaWorld(Actor->GetLevel()) == GetArenaWorld();
}

void UOutlierArenaSubsystem::PreloadArena()
{
	if (!GetWorld() || ArenaLevel.IsNull())
	{
		return;
	}
	Arena = FOutlierArenaInstance();
	Arena.InstanceTransform = FTransform::Identity;
	Arena.StreamingLevel = LoadArenaLevelInstance(Arena.InstanceTransform);
	Arena.bReady = Arena.StreamingLevel && Arena.StreamingLevel->IsLevelLoaded();
}

ULevelStreamingDynamic* UOutlierArenaSubsystem::LoadArenaLevelInstance(const FTransform& InstanceTransform)
{
	UWorld* World = GetWorld();
	if (!World || ArenaLevel.IsNull())
	{
		return nullptr;
	}

	bool bSuccess = false;
	ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
		World, ArenaLevel, InstanceTransform.GetLocation(), InstanceTransform.GetRotation().Rotator(),
		bSuccess, TEXT("OutlierArena"));
	if (!bSuccess || !StreamingLevel)
	{
		return nullptr;
	}

	StreamingLevel->OnLevelLoaded.AddUniqueDynamic(this, &UOutlierArenaSubsystem::HandleArenaLevelLoaded);
	StreamingLevel->OnLevelShown.AddUniqueDynamic(this, &UOutlierArenaSubsystem::HandleArenaLevelShown);
	StreamingLevel->SetShouldBeLoaded(true);
	StreamingLevel->SetShouldBeVisible(true);
	return StreamingLevel;
}

void UOutlierArenaSubsystem::EnsureArenaLoaded(bool bForceReload)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Client)
	{
		return;
	}

	if (Arena.StreamingLevel)
	{
		if (bForceReload)
		{
			Arena.bReady = false;
			BeginDeferredReload(Arena.StreamingLevel);
		}
		else
		{
			Arena.StreamingLevel->SetShouldBeLoaded(true);
			Arena.StreamingLevel->SetShouldBeVisible(true);
		}
		return;
	}

	Arena.InstanceTransform = FTransform::Identity;
	Arena.StreamingLevel = LoadArenaLevelInstance(Arena.InstanceTransform);
	Arena.bInUse = true;
	Arena.bReady = IsStreamingArenaReady(Arena.StreamingLevel);
}

void UOutlierArenaSubsystem::RefreshArenaReadyState(const TCHAR* Reason)
{
	(void)Reason;
	if (Arena.StreamingLevel)
	{
		Arena.bReady = Arena.StreamingLevel->IsLevelLoaded();
	}
}

void UOutlierArenaSubsystem::HandleArenaLevelLoaded()
{
	RefreshArenaReadyState(TEXT("OnLevelLoaded"));
}

void UOutlierArenaSubsystem::HandleArenaLevelShown()
{
	RefreshArenaReadyState(TEXT("OnLevelShown"));
	if (IsStreamingArenaReady(Arena.StreamingLevel))
	{
		EnsureArenaGameplayDataActivated();
		OnArenaShown.Broadcast();
	}
}

bool UOutlierArenaSubsystem::IsArenaReady() const
{
	return Arena.bReady;
}

bool UOutlierArenaSubsystem::IsArenaContentReady() const
{
	return GetWorld() && IsArenaReady() && AreArenaLevelInstancesLoaded();
}

bool UOutlierArenaSubsystem::IsStreamingArenaReady(const ULevelStreamingDynamic* StreamingLevel)
{
	return StreamingLevel && StreamingLevel->IsLevelLoaded()
		&& StreamingLevel->IsLevelVisible() && StreamingLevel->GetLoadedLevel();
}

void UOutlierArenaSubsystem::HoldCharacterUntilArenaCellReady(ACharacter* Character)
{
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	FPendingSpawnHold& Hold = PendingSpawnHolds.AddDefaulted_GetRef();
	Hold.Character = Character;
	Hold.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Movement->SetMovementMode(MOVE_None);

	if (UWorld* World = GetWorld(); World && !World->GetTimerManager().IsTimerActive(SpawnHoldPollTimer))
	{
		World->GetTimerManager().SetTimer(SpawnHoldPollTimer, this,
			&UOutlierArenaSubsystem::TickPendingSpawnHolds, 0.05f, true);
	}
}

void UOutlierArenaSubsystem::TickPendingSpawnHolds()
{
	constexpr double TimeoutSeconds = 5.0;
	UWorld* World = GetWorld();
	UWorldPartitionSubsystem* WorldPartitionSubsystem = World
		? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;

	for (int32 Index = PendingSpawnHolds.Num() - 1; Index >= 0; --Index)
	{
		FPendingSpawnHold& Hold = PendingSpawnHolds[Index];
		ACharacter* Character = Hold.Character.Get();
		if (!Character)
		{
			PendingSpawnHolds.RemoveAt(Index);
			continue;
		}

		bool bCellReady = true;
		if (WorldPartitionSubsystem)
		{
			const FWorldPartitionStreamingQuerySource QuerySource(Character->GetActorLocation());
			bCellReady = WorldPartitionSubsystem->IsStreamingCompleted(
				EWorldPartitionRuntimeCellState::Activated, { QuerySource }, false);
		}
		bCellReady = bCellReady && AreArenaLevelInstancesLoaded();
		const bool bTimedOut = World && World->GetTimeSeconds() - Hold.StartTime > TimeoutSeconds;
		if (!bCellReady && !bTimedOut)
		{
			Character->GetCharacterMovement()->SetMovementMode(MOVE_None);
			continue;
		}

		Character->GetCharacterMovement()->SetDefaultMovementMode();
		PendingSpawnHolds.RemoveAt(Index);
	}

	if (PendingSpawnHolds.IsEmpty() && World)
	{
		World->GetTimerManager().ClearTimer(SpawnHoldPollTimer);
	}
}

bool UOutlierArenaSubsystem::AreArenaLevelInstancesLoaded() const
{
	const UWorld* HostWorld = GetWorld();
	const UWorld* ArenaWorld = GetArenaWorld();
	if (!HostWorld || !ArenaWorld)
	{
		return false;
	}

	for (ULevel* Level : HostWorld->GetLevels())
	{
		if (!Level || GetOwningArenaWorld(Level) != ArenaWorld)
		{
			continue;
		}
		for (AActor* Actor : Level->Actors)
		{
			const ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(Actor);
			if (LevelInstance && !LevelInstance->IsLoaded())
			{
				return false;
			}
		}
	}
	return true;
}
