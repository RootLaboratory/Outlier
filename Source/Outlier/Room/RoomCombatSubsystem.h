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

struct FRoomCombatPreparationContext // Room 재등록/Arena 리로드 뒤 이전 합류 완료 요청을 버리는 토큰.
{
	FGameplayTag RoomTag;
	TWeakObjectPtr<AActor> AnchorPlayer;
	FGuid RoomRegistrationId;
	int32 GameplayGeneration = INDEX_NONE;
};

struct FRoomCombatReconnectContext // 재접속 로드 완료 전에 Room 수명이 바뀌었는지 확인한다.
{
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
	TWeakObjectPtr<AActor> PreparingAnchorPlayer; // 중복 발각으로 바뀌지 않으며 준비/차수 종료 때 비운다.
	FGameplayTag ActiveActivationGroupTag;
	bool bTriggeredSequenceActive = false;
	// 현재 차수 진행 여부와 별개다. 한 번 막힌 출입구는 중간 대기에도 유지하고 Clear/Reset에서 연다.
	bool bExitBlockActive = false;
	// 시작/차수 완료 이벤트에서 차단 상태를 적용한 뒤 실제 Pool 대여를 실행한다.
	bool bDeferSpawnExecution = false;
	double LastSpawnRetryLogSeconds = -1000000.0;
	int32 SpawnRetryAttempts = 0;
	bool bHadPreplacedEnemy = false;
	bool bCurrentWaveSpawnStarted = false;
	bool bEncounterIdRegistered = false;
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
	// SpawnPoint는 WP 셀 수명을 따르므로 Subsystem이 Actor 수명을 소유하지 않는다.
	TWeakObjectPtr<ARoomCombatSpawnPoint> SpawnPoint;
	FGameplayTagContainer SpawnPointTags;
	FGameplayTag ActivationGroupTag;
};

struct FRoomCombatWaveTurretRuntime
{
	// 배치 터렛은 WP Actor이므로 Subsystem이 수명을 연장하지 않는다.
	TWeakObjectPtr<AAutoTurret> Turret;
	int32 CombatPhaseIndex = INDEX_NONE;
	int32 WaveIndex = INDEX_NONE;
};

UCLASS()
class OUTLIER_API URoomCombatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// ExternalTrigger에서는 ActivationGroupTag를 비워도 된다. 지연 콜백은 이 문맥을 보관한다.
	UFUNCTION(BlueprintCallable, BlueprintPure = false, BlueprintAuthorityOnly, Category = "Room Combat")
	bool CreateTriggerContext(AActor* Requester, FGameplayTag RoomTag,
		FGameplayTag ActivationGroupTag, FRoomCombatTriggerContext& OutContext) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Room Combat")
	bool StartTriggeredSequence(AActor* Requester, const FRoomCombatTriggerContext& Context);

	// 서버 상태 조회다. 차단 Actor는 이 값으로 충돌을 적용하고 자신의 상태를 Client에 복제한다.
	UFUNCTION(BlueprintPure, Category = "Room Combat")
	bool IsExitBlocked(FGameplayTag RoomTag) const;

	// 델리게이트는 서버 로컬 알림이다. 늦게 로드된 오브젝트는 구독 후 IsExitBlocked도 조회한다.
	UPROPERTY(BlueprintAssignable, Category = "Room Combat")
	FOnRoomCombatEvent OnCombatEvent;

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
	bool TryStartInitialDetectionForPlayers(FGameplayTag RoomTag);
	bool GetReconnectContext(FRoomCombatReconnectContext& OutContext) const;
	bool IsReconnectContextCurrent(const FRoomCombatReconnectContext& Context) const;
	bool TryPlaceReconnectingPlayer(AFirstPersonCharacter* Player,
		AFirstPersonCharacter* Anchor, const FRoomCombatReconnectContext& Context);
	bool IsEnemyAttackBlocked(AEnemyBase* Enemy) const;
	bool NotifyRoomCombatStarted(FGameplayTag RoomTag);
	bool StartWaveSpawning(FGameplayTag RoomTag, int32 CombatPhaseIndex, int32 WaveIndex);
	void ResetRuntimeCombatState();

	bool IsRoomRegistered(FGameplayTag RoomTag) const;
	ERoomCombatState GetRoomState(FGameplayTag RoomTag) const;
	FGameplayTag GetActiveCombatRoomTag() const { return ActiveCombatRoomTag; }
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
#endif

private:
	bool IsPlayerInsideRoom(const AFirstPersonCharacter* Player, const ARoomVolume* Room) const;
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
		int32 PhaseIndex, int32 Generation);
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
	void StartAutomaticPhase(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime,
		const FRoomCombatRoomDefinition& Definition);
	const FRoomCombatRoomDefinition* FindRoomDefinition(FGameplayTag RoomTag) const;
	void MarkRoomCleared(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void CompactAliveEnemies(FRoomCombatRuntime& Runtime);
	void CompactSpawnPoints(FGameplayTag RoomTag);
	void ApplyRoomClearedToSpawnPoints(FGameplayTag RoomTag, bool bCleared);
	void CompactWaveTurrets(FGameplayTag RoomTag);
	void ResetWaveTurretsForRoom(FGameplayTag RoomTag, const TCHAR* ResetReason);
	void SetRoomStreamingSourceEnabled(FGameplayTag RoomTag, bool bEnabled);
	void HandleArenaGameplayReloadStarted(uint32 GameplayGeneration);
	void HandleArenaReleased();

	TMap<FGameplayTag, FRoomCombatRuntime> RoomRuntimes;
	TMap<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>> PendingPreplacedEnemies;
	TMap<TWeakObjectPtr<AEnemyBase>, FRoomCombatEnemyRegistration> RegisteredEnemies;
	TArray<FRoomCombatPendingSpawn> PendingSpawnRequests;
	// RoomVolume과 SpawnPoint의 WP 로드 순서는 보장되지 않으므로 Room 등록 여부와 독립적으로 보관한다.
	TMap<FGameplayTag, TArray<FRoomCombatSpawnPointRuntime>> SpawnPointsByRoom;
	TMap<TWeakObjectPtr<ARoomCombatSpawnPoint>, FGameplayTag> RegisteredSpawnPointRooms;
	// 배치 터렛은 Pool SpawnPoint와 선택 방식이 다르므로 별도 인덱스로 관리한다.
	TMap<FGameplayTag, TArray<FRoomCombatWaveTurretRuntime>> WaveTurretsByRoom;
	TMap<TWeakObjectPtr<AAutoTurret>, FGameplayTag> RegisteredWaveTurretRooms;
	// 프로젝트 설정의 통합 DA를 한 번 로드해 WP RoomVolume 재등록 동안 같은 원본을 유지한다.
	UPROPERTY()
	TObjectPtr<URoomCombatDefinition> CombatDefinition;
	FGameplayTag ActiveCombatRoomTag;
	FTimerHandle SpawnRetryTimer;
	bool bResettingRuntime = false;
};
