// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "OutlierArenaInstance.h"
#include "OutlierArenaPoolSubsystem.generated.h"

class ULevelStreamingDynamic;
class ULevel;
class AActor;
class ACharacter;
class APlayerController;
class UDataLayerAsset;
class UDataLayerInstance;
class UDataLayerManager;
class UWorldPartitionSubsystem;
enum class EDataLayerRuntimeState : uint8;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnArenaShown, int32 /*ArenaId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArenaReleased, int32 /*ArenaId*/);

/**
 * 
 */
UCLASS()
class OUTLIER_API UOutlierArenaPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	FOutlierArenaInstance* AcquireArena();
	void ReleaseArena(int32 ArenaId);
	void ReloadArena(int32 ArenaId); // 디버그: 페어링(bInUse/PairId) 보존한 채 제자리 재스트리밍
	// Arena LevelInstance는 유지하고 설정된 Gameplay Runtime Data Layer만 재스트리밍한다.
	void ReloadArenaGameplayData(int32 ArenaId, bool bDeferActivation = false);
	// 클라이언트 GC 완료 응답을 기다리는 서버 reload의 Data Layer를 다시 활성화한다.
	void ActivateArenaGameplayData(int32 ArenaId);
	bool IsGameplayDataLayerAvailable(int32 ArenaId) const;
	// 서버가 복제한 Data Layer가 이 로컬 월드에서 Activated 될 때까지 대기한다.
	void WaitForArenaGameplayDataReady(int32 ArenaId);

	// [서버 전용] 아레나 리로드 직전에, 이 클라이언트 커넥션이 "보고 있다"고 서버가 믿고 있는
	// 아레나 소속 레벨 패키지(외곽 LevelInstance + 그 안의 WP 셀)를 미리 비가시 처리한다.
	// 클라이언트가 언로드한 사실을 서버가 RPC로 전해 듣기 전까지 서버는 계속 그 레벨 액터들을
	// 리플리케이트하는데, 그 사이 클라이언트에는 레벨이 없어서 SerializeNewActor가 실패하고
	// 액터 채널이 영구히 닫힌다(정적/레벨배치 액터는 자가 복구가 안 됨).
	// ClientArenaReload를 보내기 "직전"에 호출하면 순서가 보장된다.
	void SuspendArenaVisibilityForConnection(int32 ArenaId, APlayerController* PlayerController);
	ULevel* GetArenaLoadedLevel(int32 ArenaId) const;
	bool IsPersistentArenaWorld() const;
	// WP 인스턴싱 시 액터는 아레나 PersistentLevel이 아니라 WP 셀 레벨에 들어간다.
	// 레벨 포인터 비교가 통하지 않으므로, 셀이 알고 있는 소유 월드로 거슬러 올라가 ArenaId를 찾는다.
	int32 FindArenaIdForActor(const AActor* Actor) const;
	void EnsureArenaLoaded(int32 ArenaId, bool bForceReload = false); // Client는 id 에 해당하는 arena만 load
	bool IsArenaReady(int32 ArenaId) const;
	// Arena 컨테이너와 중첩 Level Instance가 준비됐는지 확인한다.
	// WP 셀 범위 완료는 호출자가 자신의 StreamingSource::IsStreamingCompleted()로 별도 확인한다.
	bool IsArenaContentReady(int32 ArenaId) const;
	bool IsStreamingArenaReady(const ULevelStreamingDynamic* StreamingLevel);
	FOnArenaShown OnArenaShown;
	// 로컬 프로세스에서 대상 Actor EndPlay와 GC 후 weak-pointer 무효화까지 검증된 시점.
	FOnArenaShown OnArenaGameplayGCReady;
	FOnArenaShown OnArenaGameplayReady;
	FOnArenaReleased OnArenaReleased;

	// 스폰 시점에 캐릭터 위치의 WP 셀이 아직 로드 전이면(특히 원점이 아닌 아레나) 바닥 없이 낙하한다.
	// 스폰 직후 중력을 잠그고, 셀이 Activated 되면 원래 무브먼트 모드로 풀어준다.
	// (PlayerStart 자체는 Is Spatially Loaded=false라 무죄 — 문제는 그 아래 바닥 지오메트리가 거리 기반 셀이라는 것)
	// ArenaId를 넘기면 그 아레나 인스턴스 안의 ALevelInstance(룸을 통째로 끌어온 서브레벨)들이
	// 로딩 완료됐는지도 같이 확인한다 — WP 파티션 체크만으론 LevelInstance 내부 로딩을 못 보기 때문.
	void HoldCharacterUntilArenaCellReady(ACharacter* Character, int32 ArenaId = INDEX_NONE);

private:
	struct FPendingGameplayReload;

	void PreloadArenas();
	// ArenaId -> 인스턴스 배치 트랜스폼. 서버(PreloadArenas)와 클라(EnsureArenaLoaded)가 각자
	// 계산하던 공식을 한 곳으로 모은 것이다. 한쪽만 고치면 서버/클라가 같은 이름의 레벨을 서로 다른
	// 좌표에 띄우게 되는데, 매칭은 이름으로 되기 때문에 에러 없이 위치만 조용히 어긋난다.
	// 설정을 캐시하지 않고 매번 읽는다 — 클라의 EnsureArenaLoaded는 RPC 경로라 캐시가 채워졌는지에
	// 대한 순서 가정을 만들고 싶지 않다.
	static FTransform GetArenaInstanceTransform(int32 ArenaId);
	// 레벨이 WP 셀이면 그 셀을 만든 아레나 월드를, 아니면 레벨 자신의 아우터 월드를 돌려준다.
	static const UWorld* GetOwningArenaWorld(const ULevel* Level);
	// 항목 하나의 소유 World. 스트리밍 인스턴스는 매번 현재 로드 상태에서 계산하고(언로드되면 즉시 null),
	// StreamingLevel이 없는 Persistent Arena만 등록 시점에 박아둔 ArenaWorld를 쓴다.
	// 캐시해두면 언로드를 알려주는 경로가 없어서(RefreshArenaReadyStates는 로드/가시화 때만 불린다)
	// 죽어가는 World 포인터를 들고 있게 된다.
	static const UWorld* GetArenaWorld(const FOutlierArenaInstance& Arena);
	// 논리 ArenaId를 실제 콘텐츠를 소유한 Arena World로 변환한다.
	// Listen은 동적 WP 인스턴스 World, Dedicated Worker는 현재 Persistent World를 반환한다.
	const UWorld* ResolveArenaWorld(int32 ArenaId) const;
	ULevelStreamingDynamic* LoadArenaLevelInstance(int32, const FTransform& InstanceTransform);
	void RefreshArenaReadyStates(const TCHAR* Reason);

	// 지연 재로드: 같은 인스턴스/이름 유지한 채 언로드 완료를 기다렸다 다시 로드 (리플리케이션 매칭 유지)
	void BeginDeferredReload(ULevelStreamingDynamic* StreamingLevel);
	void TickPendingReloads();
	// Data Layer를 조회/변경할 World. ArenaId가 가리키는 아레나 World가 있으면 그것, 없으면 이 월드.
	// 예전에는 이 "아레나 -> World" 변환을 Data Layer 함수 3개가 각자 재구현하고 있었다.
	const UWorld* ResolveDataLayerWorld(int32 ArenaId) const;
	const UDataLayerInstance* ResolveGameplayDataLayer(int32 ArenaId) const;
	bool SetGameplayDataLayerState(int32 ArenaId, EDataLayerRuntimeState State) const;
	bool IsGameplayDataLayerState(int32 ArenaId, EDataLayerRuntimeState State) const;
	void AddPendingGameplayReload(int32 ArenaId, bool bCanChangeState);
	void BindGameplayReloadEvents(int32 ArenaId);
	void TryRequestGameplayReloadGC();
	void TryCompleteGameplayReloadActivation();
	// 리로드 사이클이 진행을 멈췄는지 감시한다. 이 상태머신은 EndPlay -> GC purge -> 스트리밍 완료를
	// 전부 "이벤트가 오면 다음 단계"로 엮어놨다. 한 단계라도 이벤트가 안 오면 아무도 깨워주지 않고
	// 영원히 매달린다 — 그 사이 Data Layer는 Unloaded로 남아서 그 세션의 게임플레이 액터가 통째로 사라진다.
	// 로그도 마지막 "성공" 한 줄에서 끊기기 때문에 겉보기엔 정상 진행과 구분되지 않는다.
	void TickPendingGameplayReloadTimeouts();
	// 멈춘 사이클을 포기한다. 리셋은 보장 못 하지만 Data Layer를 Activated로 되돌리고
	// 대기 중인 쪽(possess / 클라 로딩)을 풀어준다 — "액터 없는 아레나에 영구 정지"보다는 낫다.
	void AbandonStalledGameplayReload(int32 ArenaId);
	// 진단용. 액터가 WP 셀 레벨에 사는지 PersistentLevel에 사는지 구분해서 찍는다.
	// 셀에 없는 액터는 Data Layer를 내려도 언로드 대상이 아니므로 EndPlay가 영원히 안 온다.
	static FString DescribeActorLevelPackage(const AActor* Actor);

	void ActivatePendingGameplayReload(int32 ArenaId);
	// 최초 로드 시 Gameplay Data Layer를 Activated로 올린다.
	// Data Layer Instance의 Initial State 기본값이 Unloaded(DataLayerInstance.cpp:34)라서,
	// 이걸 아무도 안 불러주면 DL 소속 액터는 첫 리로드 사이클이 돌기 전까지 월드에 아예 없다
	// (실측: 세션 첫 리로드에서 Tracking unload Actors=0, 그 다음 리로드부터 정상 집계).
	void EnsureArenaGameplayDataActivated(int32 ArenaId);
	void TickPendingInitialActivations();
	void HandleGameplayGarbageCollectComplete();
	void HandleGameplayStreamingStateUpdated();

	UFUNCTION()
	void HandleGameplayDataLayerStateChanged(
		const UDataLayerInstance* DataLayer,
		EDataLayerRuntimeState State);

	UFUNCTION()
	void HandleGameplayReloadActorEndPlay(
		AActor* Actor,
		EEndPlayReason::Type EndPlayReason);

	TArray<TWeakObjectPtr<ULevelStreamingDynamic>> PendingReloadLevels;

	// 언로드는 끝났지만 아직 GC를 기다리는 레벨. 바로 다시 로드하면 OFPA 액터 패키지가
	// 메모리에 남아 있어서 "디스크에서 재생성"이 아니라 "기존 오브젝트 재사용"이 되고,
	// 런타임 상태(예: State.HackedOnce)가 리로드를 넘어 그대로 살아남는다.
	TArray<TWeakObjectPtr<ULevelStreamingDynamic>> PendingGCLevels;
	bool bReloadGCRequested = false;

	FTimerHandle ReloadPollTimer;

	struct FPendingGameplayReload
	{
		int32 ArenaId = INDEX_NONE;
		TWeakObjectPtr<UDataLayerInstance> DataLayerInstance;
		TWeakObjectPtr<UWorldPartitionSubsystem> WorldPartitionSubsystem;
		TArray<TWeakObjectPtr<AActor>> TrackedActors;
		TArray<TWeakObjectPtr<AActor>> ActorsAwaitingEndPlay;
		bool bLoadRequested = false;
		bool bGCRequested = false;
		bool bCanChangeState = false;
		// 이 사이클이 시작된 World time. 얼마나 매달려 있는지 재는 유일한 기준.
		double StartTime = 0.0;
		// 정지 진단을 이미 찍었는지. 폴링마다 같은 덤프를 반복하지 않는다.
		bool bStallReported = false;
	};
	TArray<FPendingGameplayReload> PendingGameplayReloads;
	// PendingGameplayReloads가 비어 있지 않은 동안에만 도는 폴링 타이머.
	FTimerHandle GameplayReloadTimeoutTimer;


	// DataLayerManager가 잡힐 때까지 최초 활성화를 재시도할 아레나들.
	// 스트리밍으로 얹은 아레나(Listen)는 레벨이 Shown 된 뒤에도 그 인스턴스 World의
	// DataLayerManager가 한 틱 늦게 잡힐 수 있어서 한 번의 시도로는 부족하다.
	struct FPendingInitialActivation
	{
		int32 ArenaId = INDEX_NONE;
		double StartTime = 0.0;
	};
	TArray<FPendingInitialActivation> PendingInitialActivations;
	FTimerHandle InitialActivationPollTimer;
	TArray<TWeakObjectPtr<UDataLayerManager>> BoundGameplayDataLayerManagers;
	TMap<TWeakObjectPtr<UWorldPartitionSubsystem>, FDelegateHandle> GameplayStreamingStateHandles;
	FDelegateHandle GameplayGarbageCollectCompleteHandle;

	UPROPERTY()
	TSoftObjectPtr<UDataLayerAsset> GameplayDataLayer;

	// HoldCharacterUntilArenaCellReady가 잠근 캐릭터들을 매 틱 폴링해서 셀 준비되면 풀어준다.
	// 스냅샷을 저장했다 복원하지 않는다 — Possess()가 우리보다 늦게 실행되며 Restart()에서
	// SetDefaultMovementMode()로 모드를 무조건 재확정하므로, 스폰 시점에 캡처한 값은 신뢰할 수 없다.
	// 그래서 준비 안 됐으면 매 폴링마다 MOVE_None을 재적용(Possess가 끼어들어도 다시 잠금)하고,
	// 준비되면 그 시점 기준으로 SetDefaultMovementMode()를 다시 호출시켜 스스로 판정하게 한다.
	struct FPendingSpawnHold
	{
		TWeakObjectPtr<ACharacter> Character;
		double StartTime = 0.0;
		int32 ArenaId = INDEX_NONE;
	};
	TArray<FPendingSpawnHold> PendingSpawnHolds;
	FTimerHandle SpawnHoldPollTimer;
	void TickPendingSpawnHolds();

	// ArenaId 소속으로 현재 로드된 레벨 안의 ALevelInstance(아트 전용 서브레벨 — USD/PPVolume/Light)가
	// 전부 로딩 완료됐는지. Interactable/Hackable/Drone/RoomVolume 등 상태를 갖는 액터는 컨벤션상
	// WP_Test에 직접 배치하므로 여기 안 걸리고, 이 체크는 순수 아트 콘텐츠(특히 바닥 콜리전) 스트리밍
	// 완료 대기용이다. ArenaId를 못 찾거나 INDEX_NONE이면 그냥 통과시킨다(예전 동작 유지).
	bool AreArenaLevelInstancesLoaded(int32 ArenaId) const;

	UFUNCTION()
	void HandleArenaLevelLoaded();

	UFUNCTION()
	void HandleArenaLevelShown();


	UPROPERTY()
	TArray<FOutlierArenaInstance> Arenas;

	UPROPERTY(EditDefaultsOnly, Category = "Arena")
	TSoftObjectPtr<UWorld> ArenaLevel;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena")
	int32 MaxArenaCount = 1;
};
