#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "RoomCombatSubsystem.generated.h"

class AEnemyBase;
class ARoomCombatSpawnPoint;
class ARoomVolume;
class URoomCombatDefinition;

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
	TSet<TWeakObjectPtr<AEnemyBase>> AlivePreplacedEnemies;
	ERoomCombatState State = ERoomCombatState::Dormant;
	int32 CurrentCombatPhaseIndex = 0;
	int32 CurrentWaveIndex = 0;
	bool bHadPreplacedEnemy = false;
	bool bEncounterIdRegistered = false;
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
	void ResetRuntimeCombatState();

	bool IsRoomRegistered(FGameplayTag RoomTag) const;
	ERoomCombatState GetRoomState(FGameplayTag RoomTag) const;
	FGameplayTag GetActiveCombatRoomTag() const { return ActiveCombatRoomTag; }
	int32 GetCurrentCombatPhaseIndex(FGameplayTag RoomTag) const;
	int32 GetCurrentWaveIndex(FGameplayTag RoomTag) const;
	int32 GetAliveEnemyCount(FGameplayTag RoomTag) const;
	int32 GetRegisteredSpawnPointCount(FGameplayTag RoomTag);

private:
	void CompleteCurrentPhase(FGameplayTag RoomTag, bool bCancelRemainingWaves);
	void MarkRoomCleared(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void CompactAliveEnemies(FRoomCombatRuntime& Runtime);
	void CompactSpawnPoints(FGameplayTag RoomTag);
	void SetRoomStreamingSourceEnabled(FGameplayTag RoomTag, bool bEnabled);
	void HandleArenaReleased();

	TMap<FGameplayTag, FRoomCombatRuntime> RoomRuntimes;
	TMap<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>> PendingPreplacedEnemies;
	TMap<TWeakObjectPtr<AEnemyBase>, FGameplayTag> RegisteredEnemyRooms;
	// RoomVolume과 SpawnPoint의 WP 로드 순서는 보장되지 않으므로 Room 등록 여부와 독립적으로 보관한다.
	TMap<FGameplayTag, TArray<FRoomCombatSpawnPointRuntime>> SpawnPointsByRoom;
	TMap<TWeakObjectPtr<ARoomCombatSpawnPoint>, FGameplayTag> RegisteredSpawnPointRooms;
	FGameplayTag ActiveCombatRoomTag;
};
