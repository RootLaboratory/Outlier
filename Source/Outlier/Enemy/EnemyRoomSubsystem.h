#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyRoomSubsystem.generated.h"

class AEnemyBase;
class AActor;
class ULevel;

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
	FTimerHandle ForcedShareTimerHandle;
	bool bSharedContactActive = false;
};

UCLASS()
class OUTLIER_API UEnemyRoomSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Gameplay Data Layer만 다시 읽을 때는 ArenaReleased가 오지 않으므로
	// 체크포인트 리로드가 이전 Generation의 전투/탐색 상태를 명시적으로 비운다.
	void ResetRuntimeCombatState();

	void RegisterEnemy(AEnemyBase* Enemy);
	void UnregisterEnemy(AEnemyBase* Enemy);
	void RefreshEnemyRegistration(AEnemyBase* Enemy);
	bool NotifyRoomCombat(FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy);
	void NotifyRoomCombatEnded(FGameplayTag RoomTag);
	bool IsRoomInCombat(FGameplayTag RoomTag) const;
	bool HasActiveCombat() const;

#if WITH_DEV_AUTOMATION_TESTS
	void SetActiveRoomTargetForTesting(FGameplayTag RoomTag, const FVector& TargetLocation);
#endif

	// Sight로 직접 대상을 관측한 Enemy만 호출한다.
	void ReportRoomTargetContact(
		AEnemyBase* Observer,
		AActor* TargetActor,
		const FVector& TargetLocation);

	// Observer가 시야를 잃거나 전투 행동에서 이탈하면 직접 관측자 집합에서 제거한다.
	void RemoveRoomTargetObserver(AEnemyBase* Observer);
	// 사망 또는 리스폰으로 교체되는 타겟을 방 공유 접촉과 Enemy Perception에서 제거한다.
	void NotifyTargetActorRemoved(AActor* TargetActor);
	// 스텔스 해제처럼 Actor는 유지되지만 감지 조건이 바뀐 경우 등록된 Enemy의 Sight를 다시 평가한다.
	void RefreshDetectionTarget(AActor* TargetActor);

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
		FGameplayTag RoomTag,
		const FVector& Center,
		float Radius,
		float FlightHeightOffset,
		float FloorTraceHalfHeight);
	void CompactSearchState(FEnemyRoomSearchState& SearchState);
	void BroadcastSharedTargetContact(FGameplayTag RoomTag, const FVector& TargetLocation);
	void BroadcastSharedTargetLost(FGameplayTag RoomTag);
	void CompactTargetContactState(FEnemyRoomTargetContactState& ContactState);
	void SynchronizeEnemyWithRoomState(AEnemyBase* Enemy);
	void ScheduleForcedTargetShare(FGameplayTag RoomTag);
	void HandleForcedTargetShare(FGameplayTag RoomTag);
	FGameplayTag ResolveEnemyRegistrationKey(const AEnemyBase* Enemy) const;
	FGameplayTag ResolveEnemyRoomTag(const AEnemyBase* Enemy) const;
	void CompactRegisteredEnemies(FGameplayTag RoomTag);
	void CompactAllRegisteredEnemies();
	void HandleArenaReleased();

	TSet<FGameplayTag> CombatRooms;
	TMap<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>> RegisteredEnemiesByRoom;
	TMap<TWeakObjectPtr<AEnemyBase>, FGameplayTag> RegisteredEnemyKeys;

	// Arena 해제 시 HandleArenaReleased에서 함께 제거한다.
	TMap<FGameplayTag, FEnemyRoomSearchState> SearchStates;
	TMap<FGameplayTag, FEnemyRoomTargetContactState> TargetContactStates;

	// 방의 모든 직접 시야가 끊긴 뒤 이 시간이 지나면 마지막 타겟의 현재 위치를 한 번 공유한다.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Room", meta = (ClampMin = "0.0"))
	float ForcedTargetShareDelay = 8.0f;
};
