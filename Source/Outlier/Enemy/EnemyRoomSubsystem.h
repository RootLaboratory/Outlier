#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyRoomSubsystem.generated.h"

class AEnemyBase;
class AActor;
class ULevel;

// 수색 슬롯은 Arena와 RoomTag가 모두 같은 적끼리만 공유한다.
struct FEnemyRoomSearchKey
{
	int32 ArenaId = INDEX_NONE;
	FGameplayTag RoomTag;

	bool operator==(const FEnemyRoomSearchKey& Other) const
	{
		return ArenaId == Other.ArenaId && RoomTag == Other.RoomTag;
	}

	friend uint32 GetTypeHash(const FEnemyRoomSearchKey& Key)
	{
		return HashCombine(GetTypeHash(Key.ArenaId), GetTypeHash(Key.RoomTag));
	}
};

// LKP 원형 수색의 현재 배치 결과.
// Enemy는 약한 참조로 보관해 파괴된 Actor가 슬롯 상태를 붙잡지 않도록 한다.
struct FEnemyRoomSearchState
{
	FVector Center = FVector::ZeroVector;
	float Radius = 0.0f;
	float FlightHeightOffset = 0.0f;
	float FloorTraceHalfHeight = 0.0f;
	TMap<TWeakObjectPtr<AEnemyBase>, FVector> Assignments;
};

// 직접 시야를 가진 Enemy만 Observer로 등록한다.
// 수신한 공유 좌표만으로는 Observer가 되지 않으므로 방 안에서 위치가 재전파되지 않는다.
struct FEnemyRoomTargetContactState
{
	TWeakObjectPtr<AActor> TargetActor;
	FVector LastReportedLocation = FVector::ZeroVector;
	TSet<TWeakObjectPtr<AEnemyBase>> DirectObservers;
};

UCLASS()
class OUTLIER_API UEnemyRoomSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterEnemy(AEnemyBase* Enemy);
	void UnregisterEnemy(AEnemyBase* Enemy);
	void NotifyRoomCombat(int32 ArenaId, FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy);
	bool IsRoomInCombat(int32 ArenaId, FGameplayTag RoomTag) const;

	// Sight로 직접 대상을 관측한 Enemy만 호출한다.
	void ReportRoomTargetContact(
		AEnemyBase* Observer,
		AActor* TargetActor,
		int32 ArenaId,
		const FVector& TargetLocation);

	// Observer가 시야를 잃거나 전투 행동에서 이탈하면 직접 관측자 집합에서 제거한다.
	void RemoveRoomTargetObserver(AEnemyBase* Observer);

	// 전투 중이며 플레이어를 놓친 이동 가능 Enemy에게 LKP 원형 슬롯을 배정한다.
	// 중심/반경/높이 설정 또는 참여 개체가 달라지면 같은 방의 슬롯을 다시 계산한다.
	bool RequestSearchRingSlot(
		AEnemyBase* Enemy,
		const FVector& Center,
		float Radius,
		float FlightHeightOffset,
		float ReassignmentDistance,
		float FloorTraceHalfHeight,
		FVector& OutSlotLocation);

	// Enemy가 수색 행동을 끝내거나 유효하지 않은 상태가 되면 배정만 제거한다.
	void ReleaseSearchRingSlot(AEnemyBase* Enemy);

private:
	// 같은 방의 유효 Enemy를 모아 수평 원 슬롯을 만들고 가까운 슬롯부터 배정한다.
	bool RebuildSearchRingAssignments(
		const FEnemyRoomSearchKey& Key,
		const FVector& Center,
		float Radius,
		float FlightHeightOffset,
		float FloorTraceHalfHeight);
	void CompactSearchState(FEnemyRoomSearchState& SearchState);
	void BroadcastSharedTargetContact(const FEnemyRoomSearchKey& Key, const FVector& TargetLocation);
	void BroadcastSharedTargetLost(const FEnemyRoomSearchKey& Key);
	void CompactTargetContactState(FEnemyRoomTargetContactState& ContactState);
	FGameplayTag ResolveEnemyRoomTag(const AEnemyBase* Enemy) const;
	bool IsEnemyInArena(const AEnemyBase* Enemy, int32 ArenaId, const ULevel* ArenaLevel) const;
	void CompactRegisteredEnemies();
	void HandleArenaReleased(int32 ArenaId);

	TMap<int32, TSet<FGameplayTag>> CombatRoomsByArena;
	TSet<TWeakObjectPtr<AEnemyBase>> RegisteredEnemies;

	// Arena 해제 시 HandleArenaReleased에서 함께 제거한다.
	TMap<FEnemyRoomSearchKey, FEnemyRoomSearchState> SearchStates;
	TMap<FEnemyRoomSearchKey, FEnemyRoomTargetContactState> TargetContactStates;
};
