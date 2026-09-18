#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "RoomCombatSubsystem.generated.h"

class AEnemyBase;
class ARoomCombatSpawnPoint;
class ARoomVolume;
class URoomCombatDefinition;

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
	Cleared
};

struct FRoomCombatRuntime
{
	TWeakObjectPtr<ARoomVolume> RoomVolume;
	TWeakObjectPtr<URoomCombatDefinition> Definition;
	TSet<TWeakObjectPtr<AEnemyBase>> TrackedAliveEnemies;
	TMap<TWeakObjectPtr<ARoomCombatSpawnPoint>, int32> SpawnAssignments;
	ERoomCombatState State = ERoomCombatState::Dormant;
	int32 CurrentCombatPhaseIndex = 0;
	int32 CurrentWaveIndex = 0;
	int32 WaveBaselineEnemyCount = INDEX_NONE;
	int32 GameplayGeneration = 0;
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
};

struct FRoomCombatPendingSpawn
{
	TSubclassOf<AEnemyBase> EnemyClass;
	TWeakObjectPtr<ARoomCombatSpawnPoint> AssignedSpawnPoint;
	FGameplayTag RoomTag;
	FGameplayTagQuery SpawnPointQuery;
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

UCLASS()
class OUTLIER_API URoomCombatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool RegisterRoom(
		ARoomVolume* RoomVolume,
		FGameplayTag RoomTag,
		URoomCombatDefinition* Definition);
	void UnregisterRoom(ARoomVolume* RoomVolume);
	bool RegisterSpawnPoint(
		ARoomCombatSpawnPoint* SpawnPoint,
		FGameplayTag RoomTag,
		const FGameplayTagContainer& SpawnPointTags,
		FGameplayTag ActivationGroupTag);
	void UnregisterSpawnPoint(ARoomCombatSpawnPoint* SpawnPoint);
	void SetActivationGroupActive(
		FGameplayTag RoomTag,
		FGameplayTag ActivationGroupTag,
		bool bActive);
	void GetEligibleSpawnPoints(
		FGameplayTag RoomTag,
		const FGameplayTagQuery& SpawnPointQuery,
		TArray<ARoomCombatSpawnPoint*>& OutSpawnPoints);

	void RegisterPreplacedEnemy(AEnemyBase* Enemy);
	void UnregisterEnemy(AEnemyBase* Enemy);
	void NotifyEnemyDefeated(AEnemyBase* Enemy);
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
	int32 GetAssignedSpawnCount(
		FGameplayTag RoomTag,
		const ARoomCombatSpawnPoint* SpawnPoint) const;
	int32 GetRegisteredSpawnPointCount(FGameplayTag RoomTag);

#if WITH_DEV_AUTOMATION_TESTS
	void RetryPendingSpawnsForTesting() { RetryPendingSpawns(); }
#endif

private:
	bool RegisterSpawnedEnemy(
		AEnemyBase* Enemy,
		const FRoomCombatPendingSpawn& SpawnRequest);
	void TrySpawnPendingRequests(FGameplayTag RoomTag);
	void RetryPendingSpawns();
	void ScheduleSpawnRetry();
	void CancelSpawnRetry();
	void FinalizeCurrentWaveSpawn(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void EvaluateWaveProgress(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	ARoomCombatSpawnPoint* ResolveSpawnPoint(
		FRoomCombatRuntime& Runtime,
		FRoomCombatPendingSpawn& SpawnRequest,
		const TArray<ARoomCombatSpawnPoint*>& EligibleSpawnPoints);
	void CompleteCurrentPhase(FGameplayTag RoomTag, bool bCancelRemainingWaves);
	void MarkRoomCleared(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void CompactAliveEnemies(FRoomCombatRuntime& Runtime);
	void CompactSpawnPoints(FGameplayTag RoomTag);
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
	FGameplayTag ActiveCombatRoomTag;
	FTimerHandle SpawnRetryTimer;
};
