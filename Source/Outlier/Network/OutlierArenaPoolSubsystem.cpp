// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/OutlierArenaPoolSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"
#include "OutlierArenaSettings.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "WorldPartition/WorldPartitionRuntimeCellInterface.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "Engine/NetConnection.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

void UOutlierArenaPoolSubsystem::Deinitialize()
{
	for (const TWeakObjectPtr<UDataLayerManager>& ManagerPtr : BoundGameplayDataLayerManagers)
	{
		if (UDataLayerManager* Manager = ManagerPtr.Get())
		{
			Manager->OnDataLayerInstanceRuntimeStateChanged.RemoveAll(this);
	}
	}
	BoundGameplayDataLayerManagers.Reset();

	for (const TPair<TWeakObjectPtr<UWorldPartitionSubsystem>, FDelegateHandle>& Pair
		: GameplayStreamingStateHandles)
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

	for (FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.TrackedActors)
		{
			if (AActor* Actor = ActorPtr.Get())
			{
				Actor->OnEndPlay.RemoveAll(this);
			}
		}
	}
	PendingGameplayReloads.Reset();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameplayReloadTimeoutTimer);
	}

	Super::Deinitialize();

}

void UOutlierArenaPoolSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UOutlierArenaSettings* Settings =	GetDefault<UOutlierArenaSettings>();

	ArenaLevel = Settings->ArenaLevel;
	MaxArenaCount = Settings->MaxArenaCount;
	GameplayDataLayer = Settings->GameplayDataLayer;

	if (ArenaLevel.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaPool] ArenaLevel is not set"));
		return;
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[ArenaPool] OnWorldBeginPlay World=%s NetMode=%d ArenaLevel=%s MaxArenaCount=%d"),
		*InWorld.GetName(),
		static_cast<int32>(InWorld.GetNetMode()),
		*ArenaLevel.ToSoftObjectPath().ToString(),
		MaxArenaCount);

	if (Settings->ShouldUseExternalArenaHandoff(InWorld.GetNetMode())
		&& !IsPersistentArenaWorld())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaPool] Skipping Arena preload because Static Handoff uses an external Worker"));
		return;
	}

	if (IsPersistentArenaWorld())
	{
		// 설정된 Arena가 이미 Persistent World이므로 다시 스트리밍하면 게임플레이 액터가 중복 생성된다.
		// 스트리밍은 안 하되 "이 월드가 아레나 0"이라는 사실은 배열에 항목으로 남긴다.
		// 이 등록 한 곳이 Dedicated Worker(1프로세스 1아레나)와 Listen(1프로세스 N아레나)의
		// 유일한 차이 지점이고, 그 아래 순회 코드는 두 모델이 완전히 동일하게 돈다.
		Arenas.Reset();
		FOutlierArenaInstance& Arena = Arenas.AddDefaulted_GetRef();
		Arena.ArenaId = 0;
		Arena.ArenaWorld = &InWorld;
		Arena.StreamingLevel = nullptr;   // 스트리밍할 대상이 없다
		Arena.InstanceTransform = FTransform::Identity;
		Arena.bReady = true;              // 월드 자체라 늘 떠 있다 — 이 선언은 여기 한 곳에만 있다

		UE_LOG(LogTemp, Display,
			TEXT("[ArenaPool] Registered persistent world as Arena ArenaId=0 World=%s"),
			*InWorld.GetName());

		// Dedicated Worker(Persistent Arena World)의 최초 로드 경로.
		// 아래 PreloadArenas 경로는 레벨이 Shown 될 때 HandleArenaLevelShown에서 같은 호출을 한다.
		EnsureArenaGameplayDataActivated(Arena.ArenaId);
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
		// StreamingLevel이 없는 항목(Dedicated Worker의 Persistent Arena)은 스트리밍 상태로
		// 판정할 대상이 아니다. 등록 시점에 확정된 bReady를 그대로 둔다.
		if (Arena.StreamingLevel)
		{
			Arena.bReady = IsStreamingArenaReady(Arena.StreamingLevel);

			if (!Arena.bReady)
			{
				Arena.bReady = Arena.StreamingLevel->IsLevelLoaded();
			}
		}

		if (!Arena.bInUse && Arena.bReady)
		{
			Arena.bInUse = true;
		/*	UE_LOG(LogTemp, Verbose,
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
		OnArenaReleased.Broadcast(ArenaId);

		// Persistent Arena(Dedicated Worker)는 버리고 다시 만들 대상이 아니다 —
		// 그 월드가 곧 프로세스이고, 재사용은 프로세스 재실행으로 한다.
		// 페어링만 풀고 준비 상태는 유지한다.
		if (!Arena.StreamingLevel)
		{
			return;
		}

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

		// Interactable/Hackable/Drone/RoomVolume 등 상태를 갖는 액터는 전부 WP_Test 쪽(OFPA)에
		// 직접 배치하는 걸로 컨벤션을 정했다 — 거기 있으면 이 언로드→재로드만으로 확실히
		// Destroy→재생성된다.
		//
		// ALevelInstance는 이 아레나 레벨 안의 평범한 액터라서, 외곽 레벨을 내리면 같이 Destroy되고
		// LevelInstanceSubsystem이 딸린 서브레벨도 언로드한다 — "아트는 안 내린다" 같은 선택지는 없고,
		// 리로드마다 아트 콘텐츠도 다시 스트리밍된다(패키지가 메모리에 남아 콜드 로드보단 쌀 뿐).
		// 다만 서브레벨 언로드는 자기 스트리밍 상태머신을 타는 별도 비동기 작업이라, TickPendingReloads가
		// "외곽 레벨 언로드 완료"를 보고 도는 ForceGarbageCollection이 그걸 purge하지 못할 수 있다.
		// 그러면 OFPA에서 겪은 것과 같은 "기존 오브젝트 재사용 → 런타임 상태가 리로드를 넘어 생존"이
		// LevelInstance 콘텐츠에서 재현된다. 즉 LevelInstance는 "상태가 없어서 안전"한 게 아니라
		// "상태를 두면 초기화가 보장되지 않는" 자리다 — 상태 있는 액터는 WP_Test 직접 배치를 유지할 것.

		// 같은 인스턴스/이름 유지한 채 언로드→재로드. bInUse/PairId는 손대지 않으니 자동 보존.
		Arena.bReady = false;
		BeginDeferredReload(Arena.StreamingLevel);
		return;
	}
}

const UWorld* UOutlierArenaPoolSubsystem::ResolveDataLayerWorld(int32 ArenaId) const
{
	if (const UWorld* ArenaWorld = ResolveArenaWorld(ArenaId))
	{
		return ArenaWorld;
	}
	return GetWorld();
}

const UDataLayerInstance* UOutlierArenaPoolSubsystem::ResolveGameplayDataLayer(int32 ArenaId) const
{
	if (GameplayDataLayer.IsNull() || !GetWorld())
	{
		return nullptr;
	}

	const UWorld* DataLayerWorld = ResolveDataLayerWorld(ArenaId);

	UDataLayerManager* DataLayerManager = UDataLayerManager::GetDataLayerManager(DataLayerWorld);
	UDataLayerAsset* DataLayerAsset = GameplayDataLayer.LoadSynchronous();
	const UDataLayerInstance* DataLayerInstance = DataLayerManager && DataLayerAsset
		? DataLayerManager->GetDataLayerInstance(DataLayerAsset)
		: nullptr;

	return DataLayerInstance;
}

bool UOutlierArenaPoolSubsystem::SetGameplayDataLayerState(int32 ArenaId, EDataLayerRuntimeState State) const
{
	const UDataLayerInstance* DataLayerInstance = ResolveGameplayDataLayer(ArenaId);
	const UWorld* DataLayerWorld = ResolveDataLayerWorld(ArenaId);
	UDataLayerManager* DataLayerManager = DataLayerWorld
		? UDataLayerManager::GetDataLayerManager(DataLayerWorld)
		: nullptr;
	if (!DataLayerManager || !DataLayerInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaPool][DataLayer] Cannot set state=%s asset=%s"),
			GetDataLayerRuntimeStateName(State), *GameplayDataLayer.ToSoftObjectPath().ToString());
		return false;
	}

	const bool bResult = DataLayerManager->SetDataLayerInstanceRuntimeState(
		DataLayerInstance, State, false);
	return bResult;
}

bool UOutlierArenaPoolSubsystem::IsGameplayDataLayerAvailable(int32 ArenaId) const
{
	return ResolveGameplayDataLayer(ArenaId) != nullptr;
}

bool UOutlierArenaPoolSubsystem::IsGameplayDataLayerState(int32 ArenaId, EDataLayerRuntimeState State) const
{
	const UDataLayerInstance* DataLayerInstance = ResolveGameplayDataLayer(ArenaId);
	const UWorld* DataLayerWorld = ResolveDataLayerWorld(ArenaId);
	UDataLayerManager* DataLayerManager = DataLayerWorld
		? UDataLayerManager::GetDataLayerManager(DataLayerWorld)
		: nullptr;
	if (!DataLayerManager || !DataLayerInstance)
	{
		return false;
	}

	const EDataLayerRuntimeState EffectiveState =
		DataLayerManager->GetDataLayerInstanceEffectiveRuntimeState(DataLayerInstance);
	if (EffectiveState != State)
	{
		return false;
	}

	// EffectiveRuntimeState는 Data Layer의 목표 상태만 나타낸다. 특히 Unloaded로 바꾼 직후에도
	// 해당 WP 셀/액터의 비동기 언로드는 아직 진행 중일 수 있다. 그 시점에 GC 후 곧바로
	// Activated로 되돌리면 기존 OFPA UObject가 살아 있는 채 재사용되어 런타임 태그
	// (예: State.HackedOnce)가 리로드를 넘어 남는다.
	//
	// WorldPartitionSubsystem은 반드시 "호스트 월드"에서 가져온다. 아레나가 스트리밍 인스턴스일 때
	// (Listen) 그 인스턴스 월드는 게임 월드로 초기화되지 않아 UWorldSubsystem이 없다. 예전에는
	// DataLayerWorld에서 가져오다가 null이 나와 이 함수가 영원히 false를 돌려줬고, 그 결과
	// TryCompleteGameplayReloadActivation이 완료되지 못해 리로드가 로딩에서 멈췄다.
	// 호스트 월드의 서브시스템은 등록된 모든 WP(인스턴스 포함)를 검사한다.
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
	QuerySource.DataLayers.Add(DataLayerInstance->GetDataLayerFName());

	EWorldPartitionRuntimeCellState CellState = EWorldPartitionRuntimeCellState::Unloaded;
	switch (State)
	{
	case EDataLayerRuntimeState::Loaded:
		CellState = EWorldPartitionRuntimeCellState::Loaded;
		break;
	case EDataLayerRuntimeState::Activated:
		CellState = EWorldPartitionRuntimeCellState::Activated;
		break;
	case EDataLayerRuntimeState::Unloaded:
	default:
		break;
	}

	return WorldPartitionSubsystem->IsStreamingCompleted(
		CellState, { QuerySource }, /*bExactState=*/true);
}

void UOutlierArenaPoolSubsystem::ReloadArenaGameplayData(int32 ArenaId, bool bDeferActivation)
{
	if (ArenaId == INDEX_NONE || GetWorld()->GetNetMode() == NM_Client)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArenaPool][DataLayer] Reload rejected ArenaId=%d NetMode=%d"),
			ArenaId, GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1);
		return;
	}

	if (!IsGameplayDataLayerAvailable(ArenaId))
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaPool][DataLayer] Reload rejected: configured Data Layer is unavailable"));
		return;
	}

	for (const FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		if (Pending.ArenaId == ArenaId)
		{
			return;
		}
	}

	AddPendingGameplayReload(ArenaId, !bDeferActivation);
	if (!SetGameplayDataLayerState(ArenaId, EDataLayerRuntimeState::Unloaded))
	{
		PendingGameplayReloads.RemoveAt(PendingGameplayReloads.Num() - 1);
		return;
	}

	TryRequestGameplayReloadGC();
}

void UOutlierArenaPoolSubsystem::WaitForArenaGameplayDataReady(int32 ArenaId)
{
	if (ArenaId == INDEX_NONE || !GetWorld())
	{
		return;
	}

	for (const FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		if (Pending.ArenaId == ArenaId)
		{
			return;
		}
	}

	// 서버는 모든 원격 클라이언트의 GC 완료 응답을 받기 전까지 Data Layer를
	// Unloaded로 유지한다. 클라이언트도 로컬 Unloaded를 확인하고 같은 GC 단계를 탄다.
	AddPendingGameplayReload(ArenaId, false);
	TryRequestGameplayReloadGC();
}

void UOutlierArenaPoolSubsystem::ActivateArenaGameplayData(int32 ArenaId)
{
	if (ArenaId == INDEX_NONE || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	bool bFoundPending = false;
	bool bCanActivateNow = false;
	for (FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		if (Pending.ArenaId != ArenaId)
		{
			continue;
		}

		// 클라이언트 응답이 서버 자신의 Unload/GC 완료보다 먼저 도착할 수 있다.
		// 이 경우 권한만 열어두고 서버 GC 단계가 끝나는 틱에서 활성화한다.
		Pending.bCanChangeState = true;
		bFoundPending = true;
		bCanActivateNow = Pending.bLoadRequested;
		break;
	}

	if (bFoundPending)
	{
		if (bCanActivateNow)
		{
			ActivatePendingGameplayReload(ArenaId);
		}
		return;
	}

	SetGameplayDataLayerState(ArenaId, EDataLayerRuntimeState::Activated);
}

FString UOutlierArenaPoolSubsystem::DescribeActorLevelPackage(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("<null>");
	}

	const ULevel* Level = Actor->GetLevel();
	if (!Level)
	{
		return TEXT("<no level>");
	}

	// WP 셀에 사는 액터만 Data Layer 상태 변화로 언로드된다. PersistentLevel로 승격된 액터는
	// (bIsSpatiallyLoaded=false + DL 미해석 등) DL을 내려도 그대로 살아남아 EndPlay가 오지 않는다.
	const bool bIsCell = Level->GetWorldPartitionRuntimeCell() != nullptr;
	return FString::Printf(
		TEXT("%s(Package=%s, IsWPCell=%d)"),
		*GetNameSafe(Actor),
		*GetNameSafe(Level->GetOutermost()),
		bIsCell ? 1 : 0);
}

void UOutlierArenaPoolSubsystem::AddPendingGameplayReload(int32 ArenaId, bool bCanChangeState)

{
	const UDataLayerInstance* DataLayerInstance = ResolveGameplayDataLayer(ArenaId);
	const UWorld* ArenaWorld = ResolveArenaWorld(ArenaId);
	if (!DataLayerInstance || !ArenaWorld)
	{
		return;
	}

	UWorld* HostWorld = GetWorld();
	if (!HostWorld)
	{
		return;
	}

	FPendingGameplayReload& Pending = PendingGameplayReloads.AddDefaulted_GetRef();
	Pending.ArenaId = ArenaId;
	Pending.DataLayerInstance = const_cast<UDataLayerInstance*>(DataLayerInstance);
	// 호스트 월드 기준. 인스턴스 월드(Listen)에는 UWorldSubsystem이 없다.
	Pending.WorldPartitionSubsystem = HostWorld->GetSubsystem<UWorldPartitionSubsystem>();
	Pending.bCanChangeState = bCanChangeState;
	Pending.StartTime = HostWorld->GetTimeSeconds();


	// 추적 대상 액터는 아레나 월드를 TActorIterator로 훑어서는 찾을 수 없다.
	// WP 셀 레벨은 각자 자기 패키지(/Memory/..._<GUID>) 안에 UWorld를 갖고, 그 레벨은 아레나
	// 월드가 아니라 "호스트 월드"의 Levels에 달린다. 아레나 월드를 순회하면 셀 안의 액터가
	// 통째로 누락되고(Listen에서 Actors=0), 그러면 EndPlay/GC 검증이 빈 집합에 대해 자명하게
	// 통과해서 아무것도 보장하지 못한다 — 실제로 같은 UObject가 재사용되어 State.HackedOnce가
	// 리로드를 넘어 살아남았다.
	// 호스트 월드의 모든 레벨을 훑되 소속 아레나로 필터하는 이 패턴은
	// AreArenaLevelInstancesLoaded가 이미 같은 이유로 쓰고 있다.
	// Data Layer는 갖고 있지만 추적 대상이 아니라 제외한 액터 수.
	// WorldSettings: 셀 레벨마다 딸려오는 엔진 소유 액터 — 우리 콘텐츠가 아니다.
	// NotBegunPlay: OnEndPlay가 구조적으로 올 수 없는 액터 — 기다리면 영구 정지한다.
	int32 SkippedWorldSettings = 0;
	int32 SkippedNotBegunPlay = 0;

	UDataLayerAsset* DataLayerAsset = GameplayDataLayer.LoadSynchronous();
	if (DataLayerAsset)
	{
		for (ULevel* Level : HostWorld->GetLevels())

		{
			if (!Level || GetOwningArenaWorld(Level) != ArenaWorld)
			{
				continue;
			}

			for (AActor* Actor : Level->Actors)
			{
				if (!Actor || !Actor->ContainsDataLayer(DataLayerAsset))
				{
					continue;
				}

				// 셀 레벨마다 엔진이 자동으로 넣는 AWorldSettings는 우리가 배치한 콘텐츠가 아니고
				// 리셋을 검증할 대상도 아니다. 셀 안에 있다는 이유로 Data Layer를 물려받아 여기까지 온다.
				//
				// 엔진의 TActorIterator는 이걸 스스로 걸러낸다("ignore non-persistent world settings",
				// EngineUtils.h:356). 09-14에 리슨의 Actors=0을 고치려고 TActorIterator를 직접 루프로
				// 바꾸면서 그 필터를 같이 잃었고, 그래서 dedi 리로드가 멈췄다. 잃은 한 줄을 여기서 복구한다.
				if (Level != HostWorld->PersistentLevel && Actor->IsA<AWorldSettings>())
				{
					++SkippedWorldSettings;
					continue;
				}

				// WP 셀 레벨에 있는 액터는 그 셀의 Data Layer를 전부 물려받는다 — 셀 레벨마다 기본으로
				// 들어 있는 AWorldSettings까지 포함이다. 그런데 서브레벨의 WorldSettings는 BeginPlay를
				// 타지 않고, AActor::RouteEndPlay는 BegunPlay인 액터에만 EndPlay를 돌린다(Actor.cpp:3198).
				// 즉 이 액터들의 OnEndPlay는 영원히 오지 않는다.
				//
				// 실측(2026-09-15 dedi): DL 셀 2개짜리 Level_Outlier에서 추적 10개 중 2개가 각 셀의
				// WorldSettings였고, 게임플레이 액터 8개는 EndPlay를 마쳤는데 이 2개 때문에
				// ActorsAwaitingEndPlay가 0이 되지 않아 리로드가 영구 정지했다.
				// DL 셀이 1개였던 예전 맵에서는 추적 집계 방식이 달라 안 걸렸을 뿐, 구조적인 버그다.
				//
				// 레벨과 함께 파괴되는 건 맞으므로 "리셋을 검증할 대상"에서 빼는 것으로 충분하다.
				if (!Actor->HasActorBegunPlay())
				{
					++SkippedNotBegunPlay;
					continue;
				}

				Pending.TrackedActors.Add(Actor);

				Pending.ActorsAwaitingEndPlay.Add(Actor);
				Actor->OnEndPlay.AddUniqueDynamic(
					this, &UOutlierArenaPoolSubsystem::HandleGameplayReloadActorEndPlay);
			}
		}
	}

	BindGameplayReloadEvents(ArenaId);
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaPool][DataLayer] Tracking unload ArenaId=%d Actors=%d ArenaWorld=%s HostWorld=%s"),
		ArenaId,
		Pending.TrackedActors.Num(),
		*GetNameSafe(ArenaWorld),
		*GetNameSafe(HostWorld));

	// 추적 대상이 0개면 이후 EndPlay/GC 검증이 전부 자명하게 통과한다. 리로드가 "성공"으로
	// 찍히면서 실제로는 아무것도 리셋되지 않으므로, 조용히 넘어가면 안 된다.
	if (Pending.TrackedActors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaPool][DataLayer] No actors found in the Gameplay Data Layer for ArenaId=%d — ")
			TEXT("reload verification will be vacuous. Check the Data Layer assignment and arena ownership."),
			ArenaId);
	}

	// 추적 액터가 어느 레벨 패키지에 사는지 미리 남긴다. 리로드가 EndPlay 대기에서 멈췄을 때
	// "Data Layer가 안 내려간 것"과 "애초에 언로드 대상이 아닌 액터를 추적한 것"을 가르는 유일한 근거다.
	// WP 셀이 아닌 레벨(PersistentLevel 등)에 있는 액터는 DL을 Unloaded로 내려도 파괴되지 않는다.
	{
		int32 CellActors = 0;
		for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.TrackedActors)
		{
			const AActor* Actor = ActorPtr.Get();
			const ULevel* Level = Actor ? Actor->GetLevel() : nullptr;
			if (Level && Level->GetWorldPartitionRuntimeCell())
			{
				++CellActors;
			}
		}

		UE_LOG(LogTemp, Display,
			TEXT("[ArenaPool][DataLayer] Tracked actor placement ArenaId=%d InWPCell=%d/%d ")
		TEXT("SkippedWorldSettings=%d SkippedNotBegunPlay=%d"),
			ArenaId, CellActors, Pending.TrackedActors.Num(),
			SkippedWorldSettings, SkippedNotBegunPlay);



		for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.TrackedActors)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[ArenaPool][DataLayer]   tracked %s"),
				*DescribeActorLevelPackage(ActorPtr.Get()));
		}

		// 셀 밖 액터가 하나라도 있으면 그 액터의 EndPlay는 절대 오지 않는다 — 타임아웃을 기다릴 필요 없이
		// 지금 경고한다(타임아웃 경로는 이미 15초를 날린 뒤다).
		if (CellActors < Pending.TrackedActors.Num())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ArenaPool][DataLayer] %d actor(s) tracked for reload are NOT in a WP cell ArenaId=%d — ")
				TEXT("Data Layer를 내려도 파괴되지 않으므로 EndPlay 대기가 끝나지 않는다. ")
				TEXT("해당 액터의 Is Spatially Loaded / Data Layer 할당을 확인할 것"),
				Pending.TrackedActors.Num() - CellActors, ArenaId);
		}
	}

	if (!HostWorld->GetTimerManager().IsTimerActive(GameplayReloadTimeoutTimer))
	{
		HostWorld->GetTimerManager().SetTimer(
			GameplayReloadTimeoutTimer, this,
			&UOutlierArenaPoolSubsystem::TickPendingGameplayReloadTimeouts, 0.5f, true);
	}
}


void UOutlierArenaPoolSubsystem::BindGameplayReloadEvents(int32 ArenaId)
{
	const UWorld* ArenaWorld = ResolveArenaWorld(ArenaId);
	UDataLayerManager* Manager = ArenaWorld
		? UDataLayerManager::GetDataLayerManager(ArenaWorld)
		: nullptr;
	if (Manager && !BoundGameplayDataLayerManagers.Contains(Manager))
	{
		Manager->OnDataLayerInstanceRuntimeStateChanged.AddUniqueDynamic(
			this, &UOutlierArenaPoolSubsystem::HandleGameplayDataLayerStateChanged);
		BoundGameplayDataLayerManagers.Add(Manager);
	}

	// 스트리밍 상태 변경 알림도 호스트 월드에서 받는다. 인스턴스 월드에는 서브시스템이 없어서
	// 예전에는 여기서 아무것도 안 걸렸고, 그러면 TryCompleteGameplayReloadActivation을 깨울
	// 이벤트 자체가 사라져 리로드가 완료 판정을 못 받는다.
	UWorldPartitionSubsystem* StreamingSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UWorldPartitionSubsystem>()
		: nullptr;
	if (StreamingSubsystem && !GameplayStreamingStateHandles.Contains(StreamingSubsystem))
	{
		const FDelegateHandle Handle = StreamingSubsystem->OnStreamingStateUpdated().AddUObject(
			this, &UOutlierArenaPoolSubsystem::HandleGameplayStreamingStateUpdated);
		GameplayStreamingStateHandles.Add(StreamingSubsystem, Handle);
	}

	if (!GameplayGarbageCollectCompleteHandle.IsValid())
	{
		GameplayGarbageCollectCompleteHandle =
			FCoreUObjectDelegates::GarbageCollectComplete.AddUObject(
				this, &UOutlierArenaPoolSubsystem::HandleGameplayGarbageCollectComplete);
	}
}

void UOutlierArenaPoolSubsystem::HandleGameplayReloadActorEndPlay(
	AActor* Actor,
	EEndPlayReason::Type EndPlayReason)
{
	(void)EndPlayReason;
	if (!Actor)
	{
		return;
	}

	for (FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		Pending.ActorsAwaitingEndPlay.RemoveAll(
			[Actor](const TWeakObjectPtr<AActor>& ActorPtr)
			{
				return ActorPtr.Get(true) == Actor;
			});
	}

	TryRequestGameplayReloadGC();
}

void UOutlierArenaPoolSubsystem::HandleGameplayDataLayerStateChanged(
	const UDataLayerInstance* DataLayer,
	EDataLayerRuntimeState State)
{
	if (!DataLayer)
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

void UOutlierArenaPoolSubsystem::TryRequestGameplayReloadGC()
{
	bool bShouldRequestGC = false;
	for (FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		if (Pending.bGCRequested || Pending.bLoadRequested
			|| Pending.ActorsAwaitingEndPlay.Num() > 0)
		{
			continue;
		}

		UDataLayerInstance* DataLayerInstance = Pending.DataLayerInstance.Get();
		const UWorld* ArenaWorld = ResolveArenaWorld(Pending.ArenaId);
		UDataLayerManager* Manager = ArenaWorld
			? UDataLayerManager::GetDataLayerManager(ArenaWorld)
			: nullptr;
		if (!Manager || !DataLayerInstance
			|| Manager->GetDataLayerInstanceEffectiveRuntimeState(DataLayerInstance)
				!= EDataLayerRuntimeState::Unloaded)
		{
			continue;
		}

		Pending.bGCRequested = true;
		bShouldRequestGC = true;
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaPool][DataLayer] All tracked actors reached EndPlay ArenaId=%d Actors=%d; requesting purge GC"),
			Pending.ArenaId,
			Pending.TrackedActors.Num());
	}

	if (bShouldRequestGC && GEngine)
	{
		GEngine->ForceGarbageCollection(/*bForcePurge=*/true);
	}
}

void UOutlierArenaPoolSubsystem::HandleGameplayGarbageCollectComplete()
{
	TArray<int32> VerifiedArenaIds;
	for (FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		if (!Pending.bGCRequested || Pending.bLoadRequested)
		{
			continue;
		}

		int32 LiveActorCount = 0;
		for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.TrackedActors)
		{
			// bEvenIfGarbage=true: 단순 PendingKill이 아니라 기존 UObject가 실제로
			// purge되어 weak object table에서도 사라졌는지를 검사한다.
			LiveActorCount += ActorPtr.IsValid(/*bEvenIfGarbage=*/true) ? 1 : 0;
		}

		if (LiveActorCount > 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ArenaPool][DataLayer] GC completed but old actors are still alive ArenaId=%d Live=%d/%d; keeping layer Unloaded"),
				Pending.ArenaId,
				LiveActorCount,
				Pending.TrackedActors.Num());
			continue;
		}

		Pending.bGCRequested = false;
		Pending.bLoadRequested = true;
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaPool][DataLayer] EndPlay and weak-pointer purge verified ArenaId=%d Actors=%d"),
			Pending.ArenaId,
			Pending.TrackedActors.Num());
		VerifiedArenaIds.Add(Pending.ArenaId);
	}

	// Delegate나 SetDataLayerState가 동기적으로 pending 배열을 변경할 수 있으므로,
	// 검증 루프가 끝난 뒤 ArenaId만 사용해 후속 이벤트를 보낸다.
	for (const int32 ArenaId : VerifiedArenaIds)
	{
		OnArenaGameplayGCReady.Broadcast(ArenaId);

		const FPendingGameplayReload* Pending = PendingGameplayReloads.FindByPredicate(
			[ArenaId](const FPendingGameplayReload& Item)
			{
				return Item.ArenaId == ArenaId;
			});
		if (Pending && Pending->bCanChangeState)
		{
			ActivatePendingGameplayReload(ArenaId);
		}
	}
}

void UOutlierArenaPoolSubsystem::ActivatePendingGameplayReload(int32 ArenaId)
{
	if (SetGameplayDataLayerState(ArenaId, EDataLayerRuntimeState::Activated))
	{
		TryCompleteGameplayReloadActivation();
	}
}

void UOutlierArenaPoolSubsystem::EnsureArenaGameplayDataActivated(int32 ArenaId)
{
	UWorld* World = GetWorld();
	if (!World || ArenaId == INDEX_NONE || GameplayDataLayer.IsNull())
	{
		return;
	}

	// LoadFilter=None인 레이어의 런타임 상태는 서버의 AWorldDataLayers가 복제한다.
	// 클라가 직접 올리면 엔진이 AuthoritativeFromClient로 무시하고(WorldDataLayers.cpp:240),
	// 복제본과 로컬 판단이 어긋나는 경로만 하나 더 생긴다. 클라는 WaitForArenaGameplayDataReady로 기다린다.
	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	for (const FPendingInitialActivation& Existing : PendingInitialActivations)
	{
		if (Existing.ArenaId == ArenaId)
		{
			return;
		}
	}

	FPendingInitialActivation& Pending = PendingInitialActivations.AddDefaulted_GetRef();
	Pending.ArenaId = ArenaId;
	Pending.StartTime = World->GetTimeSeconds();

	// 첫 시도는 즉시 한다 — DataLayerManager가 이미 올라와 있으면(Dedicated의 Persistent World가
	// 보통 그렇다) 타이머를 걸지 않고 여기서 끝난다.
	TickPendingInitialActivations();

	if (PendingInitialActivations.Num() > 0
		&& !World->GetTimerManager().IsTimerActive(InitialActivationPollTimer))
	{
		World->GetTimerManager().SetTimer(
			InitialActivationPollTimer, this,
			&UOutlierArenaPoolSubsystem::TickPendingInitialActivations, 0.05f, true);
	}
}

void UOutlierArenaPoolSubsystem::TickPendingInitialActivations()
{
	// 영원히 못 잡으면 조용히 넘어가지 말고 에러를 남긴다 — 이 실패의 증상은 "DL 액터가 통째로 없는
	// 월드"인데, 그걸 막는 게이트가 없어서(IsArenaContentReady는 Data Layer를 보지 않는다)
	// 겉보기에는 정상 플레이와 구분되지 않는다.
	constexpr double TimeoutSeconds = 10.0;

	UWorld* World = GetWorld();

	for (int32 Index = PendingInitialActivations.Num() - 1; Index >= 0; --Index)
	{
		const FPendingInitialActivation Pending = PendingInitialActivations[Index];

		// 리로드 상태머신이 같은 아레나의 Data Layer를 들고 있으면 손대지 않는다.
		// Unloaded -> GC -> Activated 사이에 Activated를 끼워 넣으면 그 사이클이 깨진다.
		bool bOwnedByReload = false;
		for (const FPendingGameplayReload& Reload : PendingGameplayReloads)
		{
			if (Reload.ArenaId == Pending.ArenaId)
			{
				bOwnedByReload = true;
				break;
			}
		}

		if (bOwnedByReload)
		{
			PendingInitialActivations.RemoveAt(Index);
			continue;
		}

		// Available이 되기 전에는 SetGameplayDataLayerState를 부르지 않는다 —
		// 그쪽은 실패할 때마다 Error를 찍기 때문에 폴링으로 부르면 로그가 도배된다.
		if (IsGameplayDataLayerAvailable(Pending.ArenaId))
		{
			// 이미 Activated여도 true를 돌려준다(같은 상태면 엔진이 no-op, WorldDataLayers.cpp:250).
			if (SetGameplayDataLayerState(Pending.ArenaId, EDataLayerRuntimeState::Activated))
			{
				UE_LOG(LogTemp, Display,
					TEXT("[ArenaPool][DataLayer] Initial activation ArenaId=%d DataLayerWorld=%s Asset=%s"),
					Pending.ArenaId,
					*GetNameSafe(ResolveDataLayerWorld(Pending.ArenaId)),
					*GameplayDataLayer.ToSoftObjectPath().ToString());
				PendingInitialActivations.RemoveAt(Index);
				continue;
			}
		}

		if (World && (World->GetTimeSeconds() - Pending.StartTime > TimeoutSeconds))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ArenaPool][DataLayer] Initial activation timed out ArenaId=%d Asset=%s — ")
				TEXT("Data Layer 소속 액터 없이 플레이가 시작된다. 이 World에 해당 Data Layer Instance가 있는지 확인할 것"),
				Pending.ArenaId,
				*GameplayDataLayer.ToSoftObjectPath().ToString());
			PendingInitialActivations.RemoveAt(Index);
		}
	}

	if (PendingInitialActivations.Num() == 0 && World)
	{
		World->GetTimerManager().ClearTimer(InitialActivationPollTimer);
	}
}

void UOutlierArenaPoolSubsystem::HandleGameplayStreamingStateUpdated()
{
	TryCompleteGameplayReloadActivation();
}

void UOutlierArenaPoolSubsystem::TryCompleteGameplayReloadActivation()
{
	for (int32 Index = PendingGameplayReloads.Num() - 1; Index >= 0; --Index)
	{
		FPendingGameplayReload& Pending = PendingGameplayReloads[Index];
		if (!Pending.bLoadRequested
			|| !IsGameplayDataLayerState(Pending.ArenaId, EDataLayerRuntimeState::Activated))
		{
			continue;
		}

		for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.TrackedActors)
		{
			if (AActor* Actor = ActorPtr.Get())
			{
				Actor->OnEndPlay.RemoveAll(this);
			}
		}

		const int32 ReadyArenaId = Pending.ArenaId;
		PendingGameplayReloads.RemoveAt(Index);
		OnArenaGameplayReady.Broadcast(ReadyArenaId);
	}

	if (PendingGameplayReloads.Num() == 0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(GameplayReloadTimeoutTimer);
		}
	}
}

void UOutlierArenaPoolSubsystem::TickPendingGameplayReloadTimeouts()
{
	// 정상 리로드는 실측 65ms ~ 900ms에 끝난다(09-13 dedi / 09-15 listen 로그). 15초는 그보다
	// 한 자릿수 이상 여유를 둔 값이라, 여기 걸렸다면 느린 게 아니라 이벤트가 안 오는 것이다.
	constexpr double StallTimeoutSeconds = 15.0;
	constexpr int32 MaxDumpedActors = 10;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<int32> StalledArenaIds;
	for (FPendingGameplayReload& Pending : PendingGameplayReloads)
	{
		const double Elapsed = World->GetTimeSeconds() - Pending.StartTime;
		if (Pending.bStallReported || Elapsed < StallTimeoutSeconds)
		{
			continue;
		}
		Pending.bStallReported = true;

		// 어느 단계에서 멈췄는지가 곧 원인 분류다. EndPlay 대기면 Data Layer/셀 언로드 문제,
		// GC 대기면 참조가 남은 것, 클라 ACK 대기면 네트워크/Logout 정리 누락, 스트리밍 대기면 재활성 실패.
		const TCHAR* Phase = TEXT("Unknown");
		if (Pending.ActorsAwaitingEndPlay.Num() > 0)
		{
			Phase = TEXT("WaitingForActorEndPlay");
		}
		else if (Pending.bGCRequested)
		{
			Phase = TEXT("WaitingForGCPurge");
		}
		else if (Pending.bLoadRequested && !Pending.bCanChangeState)
		{
			Phase = TEXT("WaitingForRemoteClientAck");
		}
		else if (Pending.bLoadRequested)
		{
			Phase = TEXT("WaitingForActivationStreaming");
		}

		const UDataLayerInstance* DataLayerInstance = ResolveGameplayDataLayer(Pending.ArenaId);
		const UWorld* DataLayerWorld = ResolveDataLayerWorld(Pending.ArenaId);
		UDataLayerManager* Manager = DataLayerWorld
			? UDataLayerManager::GetDataLayerManager(DataLayerWorld)
			: nullptr;
		const EDataLayerRuntimeState EffectiveState = (Manager && DataLayerInstance)
			? Manager->GetDataLayerInstanceEffectiveRuntimeState(DataLayerInstance)
			: EDataLayerRuntimeState::Unloaded;

		int32 LiveTrackedActors = 0;
		for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.TrackedActors)
		{
			LiveTrackedActors += ActorPtr.IsValid(/*bEvenIfGarbage=*/true) ? 1 : 0;
		}

		UE_LOG(LogTemp, Error,
			TEXT("[ArenaPool][DataLayer] Reload STALLED ArenaId=%d Phase=%s Elapsed=%.1fs NetMode=%d ")
			TEXT("Actors=%d AwaitingEndPlay=%d LiveTracked=%d GCRequested=%d LoadRequested=%d CanChangeState=%d ")
			TEXT("DataLayer=%s EffectiveState=%s StreamingUnloaded=%d StreamingActivated=%d ArenaWorld=%s HostWorld=%s"),
			Pending.ArenaId,
			Phase,
			Elapsed,
			static_cast<int32>(World->GetNetMode()),
			Pending.TrackedActors.Num(),
			Pending.ActorsAwaitingEndPlay.Num(),
			LiveTrackedActors,
			Pending.bGCRequested ? 1 : 0,
			Pending.bLoadRequested ? 1 : 0,
			Pending.bCanChangeState ? 1 : 0,
			DataLayerInstance ? *DataLayerInstance->GetDataLayerFName().ToString() : TEXT("<none>"),
			GetDataLayerRuntimeStateName(EffectiveState),
			IsGameplayDataLayerState(Pending.ArenaId, EDataLayerRuntimeState::Unloaded) ? 1 : 0,
			IsGameplayDataLayerState(Pending.ArenaId, EDataLayerRuntimeState::Activated) ? 1 : 0,
			*GetNameSafe(ResolveArenaWorld(Pending.ArenaId)),
			*GetNameSafe(World));

		// EffectiveState=Unloaded 인데 액터가 살아 있으면 "DL은 내려갔는데 셀이 안 내려갔다"는 뜻이다.
		// 그 액터가 어느 패키지에 사는지가 다음 조사 지점이 된다.
		int32 Dumped = 0;
		for (const TWeakObjectPtr<AActor>& ActorPtr : Pending.ActorsAwaitingEndPlay)
		{
			if (Dumped++ >= MaxDumpedActors)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[ArenaPool][DataLayer]   ... and %d more awaiting EndPlay"),
					Pending.ActorsAwaitingEndPlay.Num() - MaxDumpedActors);
				break;
			}

			UE_LOG(LogTemp, Error,
				TEXT("[ArenaPool][DataLayer]   awaiting EndPlay %s"),
				*DescribeActorLevelPackage(ActorPtr.Get(/*bEvenIfGarbage=*/true)));
		}

		StalledArenaIds.Add(Pending.ArenaId);
	}

	// 배열을 순회하는 중에 포기 처리를 하면 broadcast 수신자가 같은 배열을 건드릴 수 있다.
	// ArenaId만 모아뒀다가 루프 밖에서 처리한다 — HandleGameplayGarbageCollectComplete와 같은 이유.
	for (const int32 ArenaId : StalledArenaIds)
	{
		AbandonStalledGameplayReload(ArenaId);
	}

	if (PendingGameplayReloads.Num() == 0)
	{
		World->GetTimerManager().ClearTimer(GameplayReloadTimeoutTimer);
	}
}

void UOutlierArenaPoolSubsystem::AbandonStalledGameplayReload(int32 ArenaId)
{
	const int32 Index = PendingGameplayReloads.IndexOfByPredicate(
		[ArenaId](const FPendingGameplayReload& Item) { return Item.ArenaId == ArenaId; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& ActorPtr : PendingGameplayReloads[Index].TrackedActors)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actor->OnEndPlay.RemoveAll(this);
		}
	}

	// GC 단계를 이미 통과했다면 OnArenaGameplayGCReady는 그때 나갔다. 두 번 쏘면
	// 클라가 서버에 GC 완료를 두 번 통보하게 된다.
	const bool bAlreadyBroadcastGCReady = PendingGameplayReloads[Index].bLoadRequested;
	PendingGameplayReloads.RemoveAt(Index);

	UWorld* World = GetWorld();
	const bool bIsClient = World && World->GetNetMode() == NM_Client;

	// Data Layer 상태는 서버만 건드린다. 클라가 올리면 엔진이 AuthoritativeFromClient로 무시하고
	// (WorldDataLayers.cpp:240) 복제본과 로컬 판단이 어긋나는 경로만 하나 더 생긴다.
	if (!bIsClient)
	{
		SetGameplayDataLayerState(ArenaId, EDataLayerRuntimeState::Activated);
	}

	UE_LOG(LogTemp, Error,
		TEXT("[ArenaPool][DataLayer] Reload abandoned ArenaId=%d IsClient=%d — Data Layer를 Activated로 되돌리고 ")
		TEXT("대기를 푼다. 이번 사이클의 액터 리셋은 보장되지 않는다(런타임 상태가 리로드를 넘어 살아남았을 수 있음)"),
		ArenaId,
		bIsClient ? 1 : 0);

	// 기다리던 쪽을 전부 풀어준다. 서버는 possess(HandleServerArenaReloaded)가, 클라는 GC 완료 통보와
	// 로딩 해제가 이 이벤트에 걸려 있어서, 여기서 안 쏘면 타임아웃을 넣은 의미가 없다.
	if (!bAlreadyBroadcastGCReady)
	{
		OnArenaGameplayGCReady.Broadcast(ArenaId);
	}
	OnArenaGameplayReady.Broadcast(ArenaId);
}


void UOutlierArenaPoolSubsystem::SuspendArenaVisibilityForConnection(int32 ArenaId, APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return;
	}

	// 서버에서만 의미가 있다 — ClientVisibleLevelNames는 서버가 커넥션별로 들고 있는 상태다.
	UNetConnection* Connection = PlayerController->GetNetConnection();
	if (!Connection)
	{
		return;
	}

	const FOutlierArenaInstance* Arena = Arenas.FindByPredicate(
		[ArenaId](const FOutlierArenaInstance& Candidate) { return Candidate.ArenaId == ArenaId; });

	if (!Arena || !Arena->StreamingLevel)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaVisibility] SuspendArenaVisibilityForConnection bail ArenaId=%d HasArena=%d"),
			ArenaId, Arena != nullptr);
		return;
	}

	// 외곽 LevelInstance는 "/Game/.../UEDPIE_0_OutlierArena_0", 그 안의 WP 셀은
	// "/Memory/UEDPIE_0_OutlierArena_0_<셀해시>" — 경로 루트가 달라서 전체 경로로 접두사 비교하면
	// 셀이 하나도 안 걸린다(실측: VisibleTotal=5인데 Matched=1). 짧은 이름으로 비교해야 한다.
	// ArenaId 1이 10을 잡지 않도록 정확히 같거나 "<짧은이름>_" 접두사인 것만 고른다.
	const FString BasePackageName = Arena->StreamingLevel->GetWorldAssetPackageName();
	if (BasePackageName.IsEmpty())
	{
		return;
	}
	const FString BaseShortName = FPackageName::GetShortName(BasePackageName);
	const FString CellPrefix = BaseShortName + TEXT("_");

	TArray<FName> NamesToHide;
	for (const FName& VisibleLevelName : Connection->ClientVisibleLevelNames)
	{
		const FString VisibleShortName = FPackageName::GetShortName(VisibleLevelName.ToString());
		if (VisibleShortName == BaseShortName
			|| VisibleShortName.StartsWith(CellPrefix, ESearchCase::CaseSensitive))
		{
			NamesToHide.Add(VisibleLevelName);
		}
	}

	// 하나도 못 잡았다면 이 안전장치가 조용히 무력화된 것이다(패키지 이름 규칙이 바뀌었거나
	// 아직 클라가 이 아레나를 visible로 보고하지 않은 상태). 조용히 넘어가면 리로드 중
	// 액터 채널이 깨져도 원인을 못 찾으므로 반드시 남긴다.
	if (NamesToHide.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaVisibility] Suspend matched nothing ArenaId=%d PC=%s BaseShort=%s VisibleTotal=%d"),
			ArenaId, *GetNameSafe(PlayerController), *BaseShortName,
			Connection->ClientVisibleLevelNames.Num());
	}

	for (const FName& NameToHide : NamesToHide)
	{
		// bIsVisible=false 경로는 PackageName만 보고 ClientVisibleLevelNames/ClientMakingVisibleLevelNames에서
		// 제거만 한다 — 레벨 존재 검증이나 커넥션 종료가 없어 서버에서 직접 불러도 안전하다.
		FUpdateLevelVisibilityLevelInfo LevelVisibility;
		LevelVisibility.PackageName = NameToHide;
		LevelVisibility.bIsVisible = false;
		LevelVisibility.bSkipCloseOnError = true;
		Connection->UpdateLevelVisibility(LevelVisibility);
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

		// 언로드 완료. 여기서 곧장 다시 로드하면 액터의 OFPA 패키지가 아직 메모리에 살아 있어서
		// WP가 디스크에서 새로 만들지 않고 기존 오브젝트를 그대로 재사용한다 — 그러면 리로드가
		// 상태를 초기화하지 못한다(실측: 해킹해둔 StatMachine이 리로드 후에도 BeginPlay 시점에
		// 이미 State.HackedOnce를 들고 있었음, 서버/클라 양쪽). GC로 비우고 다음 폴링에 로드한다.
		PendingReloadLevels.RemoveAt(Index);
		PendingGCLevels.AddUnique(StreamingLevel);
		bReloadGCRequested = true;
	}

	if (bReloadGCRequested)
	{
		bReloadGCRequested = false;
		if (GEngine)
		{
			GEngine->ForceGarbageCollection(/*bForcePurge=*/true);
		}

		// 이번 틱엔 로드하지 않는다 — 다음 폴링(0.05s 뒤)이면 퍼지가 끝나 있다.
		return;
	}

	for (const TWeakObjectPtr<ULevelStreamingDynamic>& PendingGCLevel : PendingGCLevels)
	{
		if (ULevelStreamingDynamic* StreamingLevel = PendingGCLevel.Get())
		{
			// 같은 인스턴스로 다시 로드 (이름 유지 → 서버/클라 리플리케이션 매칭 보존)
			StreamingLevel->SetShouldBeLoaded(true);
			StreamingLevel->SetShouldBeVisible(true);
		}
	}
	PendingGCLevels.Reset();

	if (PendingReloadLevels.Num() == 0 && PendingGCLevels.Num() == 0)
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

	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId != ArenaId)
		{
			continue;
		}

		// StreamingLevel이 없는 항목(Persistent Arena)은 그 World의 PersistentLevel이 곧 아레나 레벨이다.
		if (!Arena.StreamingLevel)
		{
			UWorld* ArenaWorld = Arena.ArenaWorld.Get();
			return ArenaWorld ? ArenaWorld->PersistentLevel : nullptr;
		}

		{
			ULevel* LoadedLevel = Arena.StreamingLevel->GetLoadedLevel();
			UE_LOG(LogTemp, Verbose,
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

const UWorld* UOutlierArenaPoolSubsystem::GetOwningArenaWorld(const ULevel* Level)
{
	if (!Level)
	{
		return nullptr;
	}

	// WP는 액터를 PersistentLevel이 아니라 셀 레벨(/Memory/..._<GUID>)로 옮긴다.
	// 셀은 자신을 만든 WP의 월드를 알고 있으므로 그걸로 아레나를 식별한다.
	if (const IWorldPartitionCell* Cell = Level->GetWorldPartitionRuntimeCell())
	{
		return Cell->GetOuterWorld();
	}

	// WP가 아닌 아레나 레벨은 종전대로 레벨의 아우터 월드가 곧 아레나 월드다.
	return Level->GetTypedOuter<UWorld>();
}

const UWorld* UOutlierArenaPoolSubsystem::GetArenaWorld(const FOutlierArenaInstance& Arena)
{
	if (Arena.StreamingLevel)
	{
		return GetOwningArenaWorld(Arena.StreamingLevel->GetLoadedLevel());
	}

	// StreamingLevel이 없는 항목 = Dedicated Worker의 Persistent Arena. 등록 시점 값이 곧 정답이다.
	return Arena.ArenaWorld.Get();
}

const UWorld* UOutlierArenaPoolSubsystem::ResolveArenaWorld(int32 ArenaId) const
{
	if (ArenaId == INDEX_NONE)
	{
		return nullptr;
	}

	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId == ArenaId)
		{
			// 아직 로드 전이거나 언로드 중이면 null. "이 아레나의 World가 지금 없다"는 정확한 답이다.
			return GetArenaWorld(Arena);
		}
	}

	return nullptr;
}

int32 UOutlierArenaPoolSubsystem::FindArenaIdForActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return INDEX_NONE;
	}

	const UWorld* ActorArenaWorld = GetOwningArenaWorld(Actor->GetLevel());
	if (!ActorArenaWorld)
	{
		return INDEX_NONE;
	}

	// Persistent Arena든 스트리밍 인스턴스든 "소유 World가 같은 항목"을 찾는 문제로 같아진다.
	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (GetArenaWorld(Arena) == ActorArenaWorld)
		{
			return Arena.ArenaId;
		}
	}

	return INDEX_NONE;
}

FTransform UOutlierArenaPoolSubsystem::GetArenaInstanceTransform(int32 ArenaId)
{
	// WP 런타임 그리드는 2D(XY)로만 셀을 분할한다 (FSquare2DGridHelper::GetCellCoords가 X,Y만 본다).
	// Z축 오프셋은 그리드가 무시하므로 Z로 쌓으면 아레나가 전부 같은 XY 셀 위에 겹쳐 서로의 셀을 켠다.
	// 실질적으로 격리되려면 XY로 벌려야 한다.
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const double Spacing = Settings ? Settings->ArenaInstanceSpacing : 100000.0;

	const FVector InstanceLocation(ArenaId * Spacing, 0.0, 0.0);
	return FTransform(FRotator::ZeroRotator, InstanceLocation);
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
		const FTransform InstanceTransform = GetArenaInstanceTransform(Index);

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

		UE_LOG(LogTemp, Verbose,
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

	UE_LOG(LogTemp, Verbose,
		TEXT("[ArenaPool] LoadArenaLevelInstance success ArenaId=%d NetMode=%d Streaming=%s Package=%s Override=%s Transform=%s"),
		ArenaId,
		static_cast<int32>(World->GetNetMode()),
		*GetNameSafe(StreamingLevel),
		*StreamingLevel->GetWorldAssetPackageName(),
		*LevelNameOverride,
		*InstanceTransform.ToHumanReadableString());

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

	UE_LOG(LogTemp, Verbose,
		TEXT("[ArenaPool] EnsureArenaLoaded requested ArenaId=%d World=%s NetMode=%d ExistingArenaCount=%d"),
		ArenaId,
		*World->GetName(),
		static_cast<int32>(World->GetNetMode()),
		Arenas.Num());

	if (World->GetNetMode() != NM_Client)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[ArenaPool] EnsureArenaLoaded skipped on non-client ArenaId=%d NetMode=%d"),
			ArenaId,
			static_cast<int32>(World->GetNetMode()));
		return;
	}

	for (FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId != ArenaId)
		{
			continue;
		}

		// 항목은 있는데 StreamingLevel이 없다 = 이 월드 자체가 아레나(Dedicated Worker).
		// 스트리밍할 대상이 없으므로 할 일이 없다. 여기서 안 막으면 아래로 떨어져서
		// 같은 아레나를 한 벌 더 스트리밍하게 된다.
		if (!Arena.StreamingLevel)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[ArenaPool] EnsureArenaLoaded skipped: ArenaId=%d is this world itself"),
				ArenaId);
			return;
		}

		if (!bForceReload)
		{
			Arena.StreamingLevel->SetShouldBeLoaded(true);
			Arena.StreamingLevel->SetShouldBeVisible(true);

			UE_LOG(LogTemp, Verbose,
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

	const FTransform InstanceTransform = GetArenaInstanceTransform(ArenaId);

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

	UE_LOG(LogTemp, Verbose,
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

		UE_LOG(LogTemp, Verbose,
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

	const UWorld* World = GetWorld();
	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (IsStreamingArenaReady(Arena.StreamingLevel))
		{
			// 최초 로드 경로. Data Layer Instance의 Initial State는 Unloaded가 기본값이라,
			// 여기서 올려주지 않으면 DL 소속 액터는 첫 리로드 사이클 전까지 월드에 없다.
			EnsureArenaGameplayDataActivated(Arena.ArenaId);
			OnArenaShown.Broadcast(Arena.ArenaId);
		}
	}
}

bool UOutlierArenaPoolSubsystem::IsArenaReady(int32 ArenaId) const
{
	// Persistent Arena의 "항상 준비됨"은 등록 시점(OnWorldBeginPlay)에 bReady=true로 박아둔다.
	// 여기서 다시 판정하지 않으므로 bReady는 두 모델에서 같은 의미를 갖는다.
	for (const FOutlierArenaInstance& Arena : Arenas)
	{
		if (Arena.ArenaId == ArenaId)
		{
			return Arena.bReady;
		}
	}
	return false;
}

bool UOutlierArenaPoolSubsystem::IsArenaContentReady(int32 ArenaId) const
{
	if (!GetWorld() || ArenaId == INDEX_NONE)
	{
		return false;
	}

	// 예전에는 Dedicated 경로만 따로 떼어 "PresetId=Start 액터가 월드에 있는가"로 판정했다. 그 지표는
	// 두 가지 이유로 버린다.
	//
	// ① 게이트로 동작하지 않는다. Start는 APlayerStart 상속이라 bIsSpatiallyLoaded=false이고
	//    Data Layer도 없어서 always-loaded 셀 → 런타임에 PersistentLevel로 승격된다
	//    (WorldPartitionRuntimeHashSetStreamingGeneration.cpp:183). 월드 초기화부터 상주하므로
	//    이 스캔은 첫 틱부터 참이다. 레벨 배치를 바꾸면 게이트의 유무가 조용히 뒤바뀌는 구조이기도 하다.
	// ② 클라가 Start 액터를 알아야 할 이유가 사라졌다. 스폰 위치는 서버가 ClientArenaLoad /
	//    ClientArenaReload에 FVector로 실어 보낸다 — 클라는 좌표만 있으면 되고 앵커 액터는 필요 없다.
	//
	// 리로드 완료 판정은 이 함수가 아니라 Gameplay Data Layer 상태를 직접 보는
	// TryCompleteGameplayReloadActivation → OnArenaGameplayReady가 이미 정확하게 하고 있다.
	// 여기는 "이 아레나의 컨테이너와 중첩 LevelInstance가 올라왔는가"만 본다.
	//
	// Dedicated(Persistent Arena World)에서는 IsArenaReady(0)이 항상 true라, 아래 한 줄이
	// 예전 두 갈래를 그대로 대체한다.
	return IsArenaReady(ArenaId) && AreArenaLevelInstancesLoaded(ArenaId);
}

 bool UOutlierArenaPoolSubsystem::IsStreamingArenaReady(const ULevelStreamingDynamic* StreamingLevel)
{
	return StreamingLevel &&
		StreamingLevel->IsLevelLoaded() &&
		StreamingLevel->IsLevelVisible() &&
		StreamingLevel->GetLoadedLevel() != nullptr;
}

void UOutlierArenaPoolSubsystem::HoldCharacterUntilArenaCellReady(ACharacter* Character, int32 ArenaId)
{
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	// PersistentLevel(=아레나 인스턴스) 로드와 무관하게, 스폰 지점 아래 바닥 지오메트리는
	// 여전히 거리 기반 일반 셀 소속이라 근처에 스트리밍 소스(=이 캐릭터 자신)가 생기기 전엔 로드가 안 된다.
	// 폰이 스트리밍 소스로 잡히는 건 스폰 다음 틱이라, 그 사이 중력을 받으면 바닥 없이 낙하한다.
	// → 중력/무브먼트를 잠가두고 셀이 Activated 되면 풀어준다.
	//
	// 스폰 시점 MovementMode를 스냅샷해뒀다 그대로 복원하지 않는다: 이 시점은 아직 Possess 전이라
	// 대개 MOVE_None(미확정)이고, Possess()→ACharacter::Restart()→SetDefaultMovementMode()가
	// 우리보다 늦게 실행되며 그 시점 바닥 유무만 보고 모드를 무조건 다시 정한다(바닥 없으면 MOVE_Falling
	// + 실제 중력). 그래서 폴링 중엔 매번 MOVE_None을 재적용해 Possess의 재설정을 덮어쓰고,
	// 준비되면 캡처값 복원 대신 SetDefaultMovementMode()를 다시 불러 그 시점 기준으로 스스로 판정시킨다.
	FPendingSpawnHold Hold;
	Hold.Character = Character;
	Hold.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Hold.ArenaId = ArenaId;

	Movement->SetMovementMode(MOVE_None);
	PendingSpawnHolds.Add(Hold);

	UWorld* World = GetWorld();
	if (World && !World->GetTimerManager().IsTimerActive(SpawnHoldPollTimer))
	{
		World->GetTimerManager().SetTimer(
			SpawnHoldPollTimer, this, &UOutlierArenaPoolSubsystem::TickPendingSpawnHolds, 0.05f, true);
	}
}

void UOutlierArenaPoolSubsystem::TickPendingSpawnHolds()
{
	// 셀이 영영 안 뜨는 이상 상황(콘텐츠 누락 등)에 캐릭터가 영구히 묶이지 않도록 폴백 타임아웃을 둔다.
	constexpr double TimeoutSeconds = 5.0;

	UWorld* World = GetWorld();
	UWorldPartitionSubsystem* WorldPartitionSubsystem = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;

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
			// 월드 스페이스 좌표를 넣으면 등록된 모든 WP(인스턴스 포함)에 대해 알아서 로컬로 변환해서 검사해준다
			// (UWorldPartitionSubsystem::IsStreamingCompleted 내부에서 GetInstanceTransform().Inverse() 적용).
			const FWorldPartitionStreamingQuerySource QuerySource(Character->GetActorLocation());
			bCellReady = WorldPartitionSubsystem->IsStreamingCompleted(
				EWorldPartitionRuntimeCellState::Activated,
				{ QuerySource },
				/*bExactState=*/false);
		}

		// WP 파티션 체크는 룸을 통째로 끌어온 ALevelInstance(WP 셀이 아님) 내부 로딩은 보지 못한다.
		// 그 좌표에 WP가 볼 게 아예 없으면 즉시 Activated로 잡혀버려 바닥(LevelInstance 콘텐츠)이
		// 아직 로딩 중인데 락이 풀리는 문제가 생기므로, 같은 아레나 안의 LevelInstance들도 별도로 확인한다.
		if (bCellReady)
		{
			bCellReady = AreArenaLevelInstancesLoaded(Hold.ArenaId);
		}

		const bool bTimedOut = World && (World->GetTimeSeconds() - Hold.StartTime > TimeoutSeconds);
		if (!bCellReady && !bTimedOut)
		{
			// 그 사이 Possess()가 실행돼 모드를 Walking/Falling 등으로 바꿔놨을 수 있으니 매번 재잠금.
			// SetMovementMode는 값이 같으면 아무 일도 안 하니(엔진 내부 가드) 매 틱 호출해도 저렴하다.
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->SetMovementMode(MOVE_None);
			}
			continue;
		}

		if (!bCellReady && bTimedOut)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaPool] Spawn hold timed out (cell never reported Activated), releasing anyway. Character=%s Location=%s"),
				*GetNameSafe(Character),
				*Character->GetActorLocation().ToString());
		}

		// 캡처해둔 값으로 복원하지 않고, 지금 이 순간(바닥이 실제로 존재하는 상태)을 기준으로
		// Possess()가 쓰는 것과 동일한 함수로 다시 판정시킨다 — 바닥이 있으면 Walking, 없으면 Falling.
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->SetDefaultMovementMode();
		}
		PendingSpawnHolds.RemoveAt(Index);
	}

	if (PendingSpawnHolds.Num() == 0 && World)
	{
		World->GetTimerManager().ClearTimer(SpawnHoldPollTimer);
	}
}

bool UOutlierArenaPoolSubsystem::AreArenaLevelInstancesLoaded(int32 ArenaId) const
{
	if (ArenaId == INDEX_NONE)
	{
		// 어느 아레나인지 모르면(호출자가 안 넘겼거나 초기 스폰) 예전 동작대로 그냥 통과시킨다.
		return true;
	}

	const UWorld* HostWorld = GetWorld();
	const UWorld* ArenaWorld = ResolveArenaWorld(ArenaId);
	if (!HostWorld || !ArenaWorld)
	{
		return false;
	}

	// PersistentLevel 하나만 보면 WP runtime cell에 배치된 LevelInstance actor를 놓친다.
	// Host World에 현재 올라온 모든 Level을 훑되, 동일한 Arena World가 소유한 Level만 검사한다.
	// 이 기준은 Listen의 동적 WP 인스턴스와 Dedicated Worker의 Persistent WP World에 동일하다.
	for (ULevel* Level : HostWorld->GetLevels())
	{
		if (!Level || GetOwningArenaWorld(Level) != ArenaWorld)
		{
			continue;
		}

		// 리로드로 LevelInstance actor가 destroy/재생성될 수 있으므로 매 폴링마다 현재 목록을 순회한다.
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
