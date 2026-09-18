#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "RoomCombatSubsystem.generated.h"

class AEnemyBase;
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

private:
	void CompleteCurrentPhase(FGameplayTag RoomTag, bool bCancelRemainingWaves);
	void MarkRoomCleared(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime);
	void CompactAliveEnemies(FRoomCombatRuntime& Runtime);
	void HandleArenaReleased();

	TMap<FGameplayTag, FRoomCombatRuntime> RoomRuntimes;
	TMap<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>> PendingPreplacedEnemies;
	TMap<TWeakObjectPtr<AEnemyBase>, FGameplayTag> RegisteredEnemyRooms;
	FGameplayTag ActiveCombatRoomTag;
};
