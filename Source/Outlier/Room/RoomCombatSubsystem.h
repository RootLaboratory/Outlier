#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "RoomCombatSubsystem.generated.h"

class AEnemyBase;
class AActor;
class AAutoTurret;
class ARoomCombatSpawnPoint;
class ARoomCombatBarrier;
class ARoomVolume;
class AFirstPersonCharacter;
class URoomCombatDefinition;
struct FOutlierRoomPhaseProgress;
// 요청 동안 확보하는 약한 공간 문맥이며 Room 등록 변경 시 다시 만든다.
struct OUTLIER_API FEnemyMovementRoomContext
{
	FGameplayTag HomeRoomTag;
	bool bAllowGroup = false;
	uint64 Revision = 0;
	TArray<TWeakObjectPtr<ARoomVolume>> Rooms;
};

struct FRoomCombatRoomDefinition;
struct FRoomCombatWaveDefinition;

namespace RoomCombat
{
	OUTLIER_API int32 SelectWeightedSpawnPoint(
		const TArray<float>& Weights,
		const TArray<int32>& AssignedCounts,
		int32 TieBreakOffset);
}

UENUM(BlueprintType)
enum class ERoomCombatState : uint8
{
	Dormant,
	Combat,
	WaitingForTrigger,
	Cleared,
	Preparing
};

UENUM(BlueprintType)
enum class ERoomCombatEvent : uint8
{
	SequenceStarted,
	PhaseCompleted,
	RoomCleared,
	Cancelled
};

	// 해킹/외부 이벤트 시작 때 확보한다. 이 문맥을 만들었다고 전투가 예약되지는 않는다.
USTRUCT(BlueprintType)
struct OUTLIER_API FRoomCombatTriggerContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Room Combat")
	FGameplayTag RoomTag;

	UPROPERTY(BlueprintReadOnly, Category = "Room Combat")
	FGameplayTag ActivationGroupTag;

	UPROPERTY(BlueprintReadOnly, Category = "Room Combat")
	int32 CombatPhaseIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Room Combat")
	int32 GameplayGeneration = INDEX_NONE;

	UPROPERTY()
	FGuid RoomRegistrationId;

	UPROPERTY()
	TWeakObjectPtr<AActor> Requester;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnRoomCombatEvent,
	FGameplayTag, RoomTag, ERoomCombatEvent, Event, int32, CombatPhaseIndex, int32, GameplayGeneration);

// BP 알림과 별개로, 등록 해제 뒤에도 취소된 운행의 수명을 보존하는 서버 문맥이다.
struct FRoomCombatLifecycleEvent
{
	FGameplayTag RoomTag;
	ERoomCombatEvent Event = ERoomCombatEvent::Cancelled;
	int32 CombatPhaseIndex = INDEX_NONE;
	int32 GameplayGeneration = INDEX_NONE;
	FGuid RoomRegistrationId;
	FGuid ExecutionId;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FOnRoomCombatLifecycleEvent, const FRoomCombatLifecycleEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnRoomStartReadinessChanged, FGameplayTag /*RoomTag*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnRoomReinforcementStarted,
	FGameplayTag /*RoomTag*/, int32 /*CombatPhaseIndex*/, int32 /*WaveIndex*/, int32 /*GameplayGeneration*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FOnRoomWaveFirstSpawned,
	FGameplayTag /*RoomTag*/, int32 /*CombatPhaseIndex*/, int32 /*WaveIndex*/,
	int32 /*GameplayGeneration*/, FGuid /*RoomRegistrationId*/);

struct FRoomCombatPreparationContext // Room 재등록/Arena 리로드 뒤 이전 합류 완료 요청을 버리는 토큰.
{
	FGameplayTag RoomTag;
	TWeakObjectPtr<AActor> AnchorPlayer;
	FGuid RoomRegistrationId;
	FGuid PreparationId;
	int32 GameplayGeneration = INDEX_NONE;
};

struct FRoomCombatReconnectContext // 재접속 로드 완료 전에 Room 수명이 바뀌었는지 확인한다.
{
	FGuid CombatGroupId;
	// 그룹 대표 방과 실제 합류 방은 다를 수 있다. ACK는 목적지 등록 수명까지 비교한다.
	FGameplayTag DestinationRoomTag;
	FGuid DestinationRoomRegistrationId;
	FGameplayTag RoomTag;
	FGuid RoomRegistrationId;
	int32 GameplayGeneration = INDEX_NONE;
};

struct FRoomCombatRuntime
{
	TWeakObjectPtr<ARoomVolume> RoomVolume;
	TSet<TWeakObjectPtr<AEnemyBase>> TrackedAliveEnemies;
	TSet<TWeakObjectPtr<AAutoTurret>> PendingWaveTurretActivations;
	TMap<TWeakObjectPtr<ARoomCombatSpawnPoint>, int32> SpawnAssignments;
	ERoomCombatState State = ERoomCombatState::Dormant;
	int32 CurrentCombatPhaseIndex = 0;
	int32 CurrentWaveIndex = 0;
	int32 WaveBaselineEnemyCount = INDEX_NONE;
	int32 PendingActivationCount = 0;
	int32 GameplayGeneration = 0;
	FGuid RegistrationId;
	// 엘리베이터 운행 동안만 중간 체크포인트 확정을 보류한다. Room 재등록 시 함께 폐기된다.
	FGuid CheckpointHoldExecutionId;
	TWeakObjectPtr<AActor> CheckpointHoldOwner;
	TWeakObjectPtr<AActor> PreparingAnchorPlayer; // 중복 발각으로 바뀌지 않으며 준비/차수 종료 때 비운다.
	FGuid PreparationId;
	double NextPreparationAllowedTimeSeconds = 0.0;
	bool bInitialDetectionPendingJoin = false;
	bool bPreparationOwnsReservation = false;
	bool bPreparationExitBlockWasActive = false;
	FGameplayTag ActiveActivationGroupTag;
	bool bTriggeredSequenceActive = false;
	// 현재 차수 진행 여부와 별개다. 한 번 막힌 출입구는 중간 대기에도 유지하고 Clear/Reset에서 연다.
	bool bExitBlockActive = false;
	// 시작/차수 완료 이벤트에서 차단 상태를 적용한 뒤 실제 Pool 대여를 실행한다.
	bool bDeferSpawnExecution = false;
	FTimerHandle InitialSpawnDelayTimer;
	// 차수 경계에서는 다음 인덱스만 확정한다. 이 대기가 끝나기 전에는 Wave 요청도 만들지 않는다.
	bool bPhaseDelayPending = false;
	FTimerHandle PhaseDelayTimer;
	double LastSpawnRetryLogSeconds = -1000000.0;
	int32 SpawnRetryAttempts = 0;
	bool bHadPreplacedEnemy = false;
	bool bCurrentWaveSpawnStarted = false;
	bool bCurrentWaveHasSpawned = false;
	bool bEncounterIdRegistered = false;
	bool bSavedAutomaticPhasePending = false;
	// 완료 경계의 다음 차수 대기와 구분한다. 준비 완료 후 현재 차수의 첫 Wave를 재구성한다.
	bool bSavedCurrentPhasePending = false;
};

struct FRoomCombatEnemyRegistration
{
	FGameplayTag RoomTag;
	int32 CombatPhaseIndex = 0;
	int32 WaveIndex = 0;
	int32 GameplayGeneration = 0;
	bool bPreplaced = false;
	bool bPoolManaged = false;
};

struct FRoomCombatPendingSpawn
{
	TSubclassOf<AEnemyBase> EnemyClass;
	TWeakObjectPtr<ARoomCombatSpawnPoint> AssignedSpawnPoint;
	FGameplayTag RoomTag;
	FGameplayTag RequiredSpawnPointTag;
	int32 CombatPhaseIndex = INDEX_NONE;
	int32 WaveIndex = INDEX_NONE;
	int32 GameplayGeneration = 0;
	int32 SearchSerial = 0;
};

struct FRoomCombatSpawnPointRuntime
{
	// SpawnPoint는 Gameplay 서브레벨 수명을 따르므로 Subsystem이 Actor 수명을 소유하지 않는다.
	TWeakObjectPtr<ARoomCombatSpawnPoint> SpawnPoint;
	FGameplayTagContainer SpawnPointTags;
	FGameplayTag ActivationGroupTag;
};

struct FRoomCombatWaveTurretRuntime
{
	// 배치 터렛은 Gameplay 서브레벨 Actor이므로 Subsystem이 수명을 연장하지 않는다.
	TWeakObjectPtr<AAutoTurret> Turret;
	int32 CombatPhaseIndex = INDEX_NONE;
	int32 WaveIndex = INDEX_NONE;
};

UCLASS()
class OUTLIER_API URoomCombatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool IsCombatMusicActive() const;
	void PublishCombatMusicState(bool bPlayEnd);
	void SendCombatMusicToPlayer(class APlayerState* PlayerState, bool bActive, bool bPlayEnd);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// ExternalTrigger에서는 ActivationGroupTag를 비워도 된다. 지연 콜백은 이 문맥을 보관한다.
	UFUNCTION(BlueprintCallable, BlueprintPure = false, BlueprintAuthorityOnly, Category = "Room Combat")
	bool CreateTriggerContext(AActor* Requester, FGameplayTag RoomTag,
		FGameplayTag ActivationGroupTag, FRoomCombatTriggerContext& OutContext) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Room Combat")
	bool StartTriggeredSequence(AActor* Requester, const FRoomCombatTriggerContext& Context);
	bool StartTriggeredSequenceWithSpawnDelay(AActor* Requester,
		const FRoomCombatTriggerContext& Context, float SpawnDelaySeconds);

	// 암전 이동부터 착층까지 해당 Room의 새 전투 시작만 보류한다. 미등록 도착 방도 예약할 수 있다.
	bool AcquireRoomStartHold(FGameplayTag RoomTag, FGuid ExecutionId, UObject* Owner);
	void ReleaseRoomStartHold(FGuid ExecutionId);

	// 해당 방이 직접 유지 중인 차단 원인이다. 외곽 Actor는 아래 그룹 조회를 사용한다.
	UFUNCTION(BlueprintPure, Category = "Room Combat")
	bool IsExitBlocked(FGameplayTag RoomTag) const;

	// 외곽 차단용 조회. 실제 참여 방의 차단 원인이 모두 끝날 때 해제한다.
	UFUNCTION(BlueprintPure, Category = "Room Combat")
	bool IsGroupExitBlocked(FGameplayTag RoomTag) const;

	// 서버 조회용이다. 클라이언트 UI에 필요하면 호출자가 별도로 복제한다.
	UFUNCTION(BlueprintPure, Category = "Room Combat")
	bool IsPhaseDelayPending(FGameplayTag RoomTag) const;

	UFUNCTION(BlueprintPure, Category = "Room Combat")
	float GetPhaseDelayRemainingSeconds(FGameplayTag RoomTag) const;

	UFUNCTION(BlueprintPure, Category = "Room Combat")
	int32 GetPendingCombatPhaseIndex(FGameplayTag RoomTag) const;

	// 델리게이트는 서버 로컬 알림이다. 늦게 로드된 오브젝트는 구독 후 IsExitBlocked도 조회한다.
	UPROPERTY(BlueprintAssignable, Category = "Room Combat")
	FOnRoomCombatEvent OnCombatEvent;
	FOnRoomCombatLifecycleEvent OnCombatLifecycleEvent;
	FOnRoomStartReadinessChanged OnRoomStartReadinessChanged;
	// 남은 적 비율이 기준 이하가 되어 후속 Wave(1 이상) 시작에 성공했을 때만 보낸다. 각 phase의 Wave 0 시작은 포함하지 않는다.
	FOnRoomReinforcementStarted OnReinforcementStarted;
	// Wave 0도 포함한다. 요청 생성이 아닌 첫 Pool 등록/터렛 활성화 성공 뒤 서버에서 알린다.
	FOnRoomWaveFirstSpawned OnWaveFirstSpawned;
	bool HasCurrentWaveSpawned(const FRoomCombatTriggerContext& Context) const;
	bool IsSingleExternalTriggerEncounter(FGameplayTag RoomTag) const;
	bool IsElevatorEncounter(FGameplayTag RoomTag) const;
	bool AcquireEncounterCheckpointHold(const FRoomCombatTriggerContext& Context, FGuid ExecutionId);
	void ReleaseEncounterCheckpointHold(FGameplayTag RoomTag, FGuid ExecutionId);
	bool IsEncounterContextCurrent(const FRoomCombatTriggerContext& Context, FGuid ExecutionId) const;
	bool IsCombatPhaseCheckpointDeferred(FGameplayTag RoomTag, int32 Generation) const;
	// ExternalTrigger의 첫 Wave가 등록된 Room과 설정한 수의 활성 SpawnPoint를 확보했는지 확인한다.
	bool HasReadyExternalTriggerRoster(const ARoomVolume* RoomVolume, FGameplayTag RoomTag) const;

	bool RegisterRoom(
		ARoomVolume* RoomVolume,
		FGameplayTag RoomTag);
	void UnregisterRoom(ARoomVolume* RoomVolume);
	bool RegisterSpawnPoint(
		ARoomCombatSpawnPoint* SpawnPoint,
		FGameplayTag RoomTag,
		const FGameplayTagContainer& SpawnPointTags,
		FGameplayTag ActivationGroupTag);
	void UnregisterSpawnPoint(ARoomCombatSpawnPoint* SpawnPoint);
	void RegisterBarrier(ARoomCombatBarrier* Barrier);
	void UnregisterBarrier(ARoomCombatBarrier* Barrier);
	bool RegisterWaveTurret(
		AAutoTurret* Turret,
		FGameplayTag RoomTag,
		int32 CombatPhaseIndex,
		int32 WaveIndex);
	void UnregisterWaveTurret(AAutoTurret* Turret);
	bool NotifyWaveTurretDeploymentFinished(
		AAutoTurret* Turret,
		int32 GameplayGeneration,
		int32 ActivationSerial);
	void GetRegisteredWaveTurrets(
		FGameplayTag RoomTag,
		int32 CombatPhaseIndex,
		int32 WaveIndex,
		TArray<AAutoTurret*>& OutTurrets);
	void SetActivationGroupActive(
		FGameplayTag RoomTag,
		FGameplayTag ActivationGroupTag,
		bool bActive);
	void GetEligibleSpawnPoints(
		FGameplayTag RoomTag,
		FGameplayTag RequiredSpawnPointTag,
		TArray<ARoomCombatSpawnPoint*>& OutSpawnPoints);

	void RegisterPreplacedEnemy(AEnemyBase* Enemy);
	void UnregisterEnemy(AEnemyBase* Enemy);
	void NotifyEnemyDefeated(AEnemyBase* Enemy);
	// 감지 진입과 재접속은 서버의 현재 Room 수명으로 검증한 뒤에만 Combat/합류를 확정한다.
	bool BeginInitialDetectionPreparation(FGameplayTag RoomTag, AActor* AnchorPlayer,
		FRoomCombatPreparationContext& OutContext);
	bool CompleteInitialDetectionPreparation(const FRoomCombatPreparationContext& Context);
	bool CancelInitialDetectionPreparation(const FRoomCombatPreparationContext& Context);
	bool TryStartInitialDetectionForPlayers(FGameplayTag RoomTag);
	// 추적 적의 현재 위치만 검사한다. 전투 소속과 Wave 명단은 원래 방에 유지한다.
	bool TryStartCombatFromPursuit(AEnemyBase* Enemy, FGameplayTag EnteredRoomTag);
	bool GetReconnectContext(FRoomCombatReconnectContext& OutContext) const;
	void CaptureRoomPhaseProgressForCheckpoint(TMap<FGameplayTag, FOutlierRoomPhaseProgress>& OutProgress) const;
	bool IsReconnectContextCurrent(const FRoomCombatReconnectContext& Context) const;
	// 그룹 전투가 끝난 뒤에도 ACK가 가리키는 실제 목적지의 등록 수명을 검사한다.
	bool IsReconnectDestinationCurrent(const FRoomCombatReconnectContext& Context) const;
	bool TryPlaceReconnectingPlayer(AFirstPersonCharacter* Player,
		AFirstPersonCharacter* Anchor, const FRoomCombatReconnectContext& Context);
	bool IsEnemyAttackBlocked(AEnemyBase* Enemy) const;
	bool NotifyRoomCombatStarted(FGameplayTag RoomTag);
	bool StartWaveSpawning(FGameplayTag RoomTag, int32 CombatPhaseIndex, int32 WaveIndex);
	void ResetRuntimeCombatState();
	void ResumeRestoredAutomaticPhases();

	bool BuildEnemyMovementRoomContext(const AEnemyBase* Enemy, FEnemyMovementRoomContext& Out) const;
	bool IsEnemyMovementRoomContextCurrent(const AEnemyBase* Enemy, const FEnemyMovementRoomContext& Context) const;
	bool IsRoomRegistered(FGameplayTag RoomTag) const;
	ARoomVolume* GetRegisteredRoomVolume(FGameplayTag RoomTag) const;
	ERoomCombatState GetRoomState(FGameplayTag RoomTag) const;
	// 기존 단일 방 조회/진단용 대표다. 전투 허용 여부는 방별 예약과 상태로 판정한다.
	FGameplayTag GetActiveCombatRoomTag() const { return ActiveCombatRoomTag; }
	FGameplayTag GetActiveCombatGroupTag() const { return ActiveCombatGroupTag; }
	bool HasActiveCombatReservation() const { return !ReservedCombatRooms.IsEmpty(); }
	bool IsRoomCombatReserved(FGameplayTag RoomTag) const { return ReservedCombatRooms.Contains(RoomTag); }
	int32 GetCurrentCombatPhaseIndex(FGameplayTag RoomTag) const;
	int32 GetCurrentWaveIndex(FGameplayTag RoomTag) const;
	int32 GetWaveBaselineEnemyCount(FGameplayTag RoomTag) const;
	int32 GetAliveEnemyCount(FGameplayTag RoomTag) const;
	int32 GetPendingSpawnCount(FGameplayTag RoomTag) const;
	int32 GetPendingActivationCount(FGameplayTag RoomTag) const;
	int32 GetAssignedSpawnCount(
		FGameplayTag RoomTag,
		const ARoomCombatSpawnPoint* SpawnPoint) const;
	int32 GetRegisteredSpawnPointCount(FGameplayTag RoomTag);
	int32 GetRegisteredWaveTurretCount(
		FGameplayTag RoomTag,
		int32 CombatPhaseIndex,
		int32 WaveIndex);

#if WITH_DEV_AUTOMATION_TESTS
	void RetryPendingSpawnsForTesting() { RetryPendingSpawns(); }
	void SetCombatDefinitionForTesting(URoomCombatDefinition* Definition) { CombatDefinition = Definition; }
	TFunction<void(FGameplayTag, ERoomCombatEvent, int32)> CombatEventObserverForTesting;
	TFunction<void()> JoinTeleportObserverForTesting;
#endif

private:
	struct FRoomStartHold
	{
		FGuid ExecutionId;
		FGuid RoomRegistrationId;
		TWeakObjectPtr<UObject> Owner;
	};
	bool IsRoomStartHeld(FGameplayTag RoomTag) const;
	bool IsPreparationContextCurrent(const FRoomCombatPreparationContext& Context) const;
	bool CanReserveCombatRoom(FGameplayTag RoomTag) const;
	void ReserveCombatRoom(FGameplayTag RoomTag);
	void ReleaseCombatRoom(FGameplayTag RoomTag);
	void ResumeRoomAfterStartHold(FGameplayTag RoomTag, FGuid RegistrationId);
	TMap<FGameplayTag, FRoomStartHold> RoomStartHolds;
	bool IsPlayerInsideRoom(const AFirstPersonCharacter* Player, const ARoomVolume* Room) const;
	ARoomVolume* FindPlayerRoomInCombatGroup(const AFirstPersonCharacter* Player, FGameplayTag CombatRoomTag) const;
	bool CompleteInitialDetectionPreparation(const FRoomCombatPreparationContext& Context,
		TFunctionRef<bool()> IsJoinCurrent);
	bool TryStartInitialDetectionForPlayers(FGameplayTag RoomTag, TFunctionRef<bool()> IsRequestCurrent);
	bool HasAlivePreplacedEnemy(FGameplayTag RoomTag, const FRoomCombatRuntime& Runtime) const;
	bool IsSafeJoinDestination(const AFirstPersonCharacter* MovingPlayer,
		const AFirstPersonCharacter* Anchor, const ARoomVolume* Room,
		const FVector& Location) const;
	bool FindJoinDestination(AFirstPersonCharacter* MovingPlayer,
		AFirstPersonCharacter* Anchor, ARoomVolume* Room, bool bFallbackOnly,
		FVector& OutLocation) const;
	TArray<TWeakObjectPtr<ARoomCombatBarrier>> RegisteredBarriers;
	bool CanRunServerGameplay() const;
	bool HasPendingSpawns(FGameplayTag RoomTag) const;
	bool HasPendingWaveWork(FGameplayTag RoomTag, const FRoomCombatRuntime& Runtime) const;
	bool IsActiveCombatRuntime(FGameplayTag RoomTag, const FRoomCombatRuntime& Runtime) const;
	bool IsInitialDetectionPhase(FGameplayTag RoomTag, const FRoomCombatRuntime& Runtime) const;
	void StartInitialDetectionCombat(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void BroadcastCombatEvent(FGameplayTag RoomTag, ERoomCombatEvent Event,
		int32 PhaseIndex, int32 Generation, FGuid RegistrationId = FGuid(), FGuid ExecutionId = FGuid());
	void ResumeDeferredSpawning(FGameplayTag RoomTag, const FGuid& RegistrationId);
	// 소환 시작: 조건 검사 -> 고정 명단 구성 -> 지점 분배 -> Pending 실행.
	const FRoomCombatWaveDefinition* FindSpawnableWave(
		FGameplayTag RoomTag, int32 CombatPhaseIndex, int32 WaveIndex) const;
	void QueueWaveSpawnRequests(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime,
		const FRoomCombatWaveDefinition& Wave, const TArray<TSubclassOf<AEnemyBase>>& EnemyRoster);
	void QueueWaveTurretActivations(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime,
		const FRoomCombatWaveDefinition& Wave);
	void TryStartPendingWaveTurretActivations(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	bool RegisterActivatedWaveTurret(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime,
		AAutoTurret* Turret);
	bool RegisterSpawnedEnemy(
		AEnemyBase* Enemy,
		const FRoomCombatPendingSpawn& SpawnRequest);
	void TrySpawnPendingRequests(FGameplayTag RoomTag);
	void NotifyFirstWaveSpawn(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void RetryPendingSpawns();
	void ScheduleSpawnRetry();
	void CancelSpawnRetry();
	// Pending이 모두 성공한 뒤에만 기준값을 확정하고 사망 이벤트마다 진행 조건을 평가한다.
	void FinalizeCurrentWaveSpawn(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void EvaluateWaveProgress(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	ARoomCombatSpawnPoint* ResolveSpawnPoint(
		FRoomCombatRuntime& Runtime,
		FRoomCombatPendingSpawn& SpawnRequest,
		const TArray<ARoomCombatSpawnPoint*>& EligibleSpawnPoints);
	void CompleteCurrentPhase(FGameplayTag RoomTag, bool bCancelRemainingWaves);
	void BeginPhaseDelay(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void CompletePhaseDelay(FGameplayTag RoomTag, FGuid RegistrationId, int32 Generation, int32 PhaseIndex);
	void StartAutomaticPhase(FGameplayTag RoomTag);
	void RecordPhaseBoundary(FGameplayTag RoomTag, const FRoomCombatRuntime& Runtime);
	void TryStartRestoredAutomaticPhase(FGameplayTag RoomTag);
	const FRoomCombatRoomDefinition* FindRoomDefinition(FGameplayTag RoomTag) const;
	void MarkRoomCleared(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void CompactAliveEnemies(FRoomCombatRuntime& Runtime);
	void CompactSpawnPoints(FGameplayTag RoomTag);
	void ApplyRoomClearedToSpawnPoints(FGameplayTag RoomTag, bool bCleared);
	// 배정된 Pending 소환이 남은 지점만 작동 상태로 둔다. 리셋 정리는 bPlayTransitionAudio=false.
	void RefreshSpawnPointWorkStates(FGameplayTag RoomTag, bool bPlayTransitionAudio);
	void CompactWaveTurrets(FGameplayTag RoomTag);
	void ResetWaveTurretsForRoom(FGameplayTag RoomTag, const TCHAR* ResetReason);
	void HandleArenaGameplayReloadStarted(uint32 GameplayGeneration);
	void HandleArenaReleased();

	uint64 MovementRoomRevision = 1;
	TMap<FGameplayTag, FRoomCombatRuntime> RoomRuntimes;
	TMap<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>> PendingPreplacedEnemies;
	TMap<TWeakObjectPtr<AEnemyBase>, FRoomCombatEnemyRegistration> RegisteredEnemies;
	TArray<FRoomCombatPendingSpawn> PendingSpawnRequests;
	// RoomVolume과 SpawnPoint의 등록 순서는 보장되지 않으므로 Room 등록 여부와 독립적으로 보관한다.
	TMap<FGameplayTag, TArray<FRoomCombatSpawnPointRuntime>> SpawnPointsByRoom;
	TMap<TWeakObjectPtr<ARoomCombatSpawnPoint>, FGameplayTag> RegisteredSpawnPointRooms;
	// 배치 터렛은 Pool SpawnPoint와 선택 방식이 다르므로 별도 인덱스로 관리한다.
	TMap<FGameplayTag, TArray<FRoomCombatWaveTurretRuntime>> WaveTurretsByRoom;
	TMap<TWeakObjectPtr<AAutoTurret>, FGameplayTag> RegisteredWaveTurretRooms;
	// 프로젝트 설정의 통합 DA를 한 번 로드해 RoomVolume 재등록 동안 같은 원본을 유지한다.
	UPROPERTY()
	TObjectPtr<URoomCombatDefinition> CombatDefinition;
	FGameplayTag ActiveCombatRoomTag;
	FGameplayTag ActiveCombatGroupTag;
	// 대표 자식 방이 완료/언로드되어도 유지하고, 그룹 점유가 끝나면 폐기한다.
	FGuid ActiveCombatGroupId;
	// Preparing도 그룹을 예약한다. 완료/실패한 방만 제거하며 나머지 방의 점유는 유지한다.
	TSet<FGameplayTag> ReservedCombatRooms;
	FTimerHandle SpawnRetryTimer;
	bool bResettingRuntime = false;
	bool bRestoredAutomaticStartAllowed = false;
};
