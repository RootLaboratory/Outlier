#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyRoomSubsystem.generated.h"

class AEnemyBase;
class AActor;
class ULevel;
class AController;

// 순찰 예약은 수색 회차와 분리한다. 같은 Actor의 새 대여나 Controller 교체도 이전 예약과 구분한다.
struct FEnemyPatrolDestination
{
	FVector Location = FVector::ZeroVector;
	FGameplayTag RoomTag;
	TWeakObjectPtr<AController> Controller;
	int32 Generation = 0;
	int32 Lease = 0;
};

// 직접 시야를 가진 Enemy만 Observer로 등록한다.
// 수신한 공유 좌표만으로는 Observer가 되지 않으므로 방 안에서 위치가 재전파되지 않는다.
struct FEnemyRoomTargetContactState
{
	TWeakObjectPtr<AActor> TargetActor;
	FVector LastReportedLocation = FVector::ZeroVector;
	// 관측자별 실제 타깃을 보관해 서로 다른 플레이어 보고가 공유 위치를 덮어쓰지 않게 한다.
	TMap<TWeakObjectPtr<AEnemyBase>, TWeakObjectPtr<AActor>> DirectObservers;
	FTimerHandle ForcedShareTimerHandle;
	bool bSharedContactActive = false;
	bool bForcedShareConsumed = false;
	uint64 SharedRevision = 0;
};

UCLASS()
class OUTLIER_API UEnemyRoomSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Gameplay 서브레벨만 다시 읽을 때는 ArenaReleased가 오지 않으므로
	// 체크포인트 리로드가 이전 Generation의 전투/탐색 상태를 명시적으로 비운다.
	void ResetRuntimeCombatState();

	void RegisterEnemy(AEnemyBase* Enemy);
	void UnregisterEnemy(AEnemyBase* Enemy);
	void RefreshEnemyRegistration(AEnemyBase* Enemy);
	// 기존 등록 수명주기를 재사용한다. 발사 시점에만 호출하며 매 프레임 월드를 검색하지 않는다.
	void GetAutoAimCandidates(TArray<AEnemyBase*>& OutEnemies) const;
	bool NotifyRoomCombat(FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy);
	void NotifyRoomCombatEnded(FGameplayTag RoomTag);
	bool IsRoomInCombat(FGameplayTag RoomTag) const;
	bool HasActiveCombat() const;

#if WITH_DEV_AUTOMATION_TESTS
	void SetActiveRoomTargetForTesting(FGameplayTag RoomTag, const FVector& TargetLocation,
		bool bSharedContactActive = true);
	uint64 GetSharedRevisionForTesting(FGameplayTag RoomTag) const;
	bool IsForcedShareConsumedForTesting(FGameplayTag RoomTag) const;
	float GetForcedShareDelayForTesting() const { return ForcedTargetShareDelay; }
	void TriggerForcedShareForTesting(FGameplayTag RoomTag) { HandleForcedTargetShare(RoomTag); }
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

	bool IsSearchDestinationReserved(const AEnemyBase* Enemy, const FVector& Location, float Separation) const;
	bool ReserveSearchDestinations(AEnemyBase* Enemy, const TArray<FVector>& Locations, float Separation);
	void ReleaseSearchDestinations(AEnemyBase* Enemy);
	bool IsPatrolDestinationUsed(const AEnemyBase* Enemy, const FVector& Location, float Separation) const;
	bool ReservePatrolDestination(AEnemyBase* Enemy, const FVector& Location);
	void ReleasePatrolDestination(AEnemyBase* Enemy);
	// 이전 부모 Task의 Exit가 새 풀 대여의 예약을 지우지 않도록 수명을 확인한다.
	void ReleasePatrolDestination(AEnemyBase* Enemy, int32 Generation, int32 Lease, AController* Controller);

private:
	void BroadcastSharedTargetContact(FGameplayTag RoomTag, const FVector& TargetLocation, bool bNewInformation = true);
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
	// 직접 시야가 끊겨도 새 증원이 Combat에 합류할 수 있도록 마지막 전투 위치를 보존한다.
	TMap<FGameplayTag, FVector> CombatRoomLastKnownLocations;
	TMap<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>> RegisteredEnemiesByRoom;
	TMap<TWeakObjectPtr<AEnemyBase>, FGameplayTag> RegisteredEnemyKeys;

	// Arena 해제 시 HandleArenaReleased에서 함께 제거한다.
	TMap<FGameplayTag, FEnemyRoomTargetContactState> TargetContactStates;
	// 한 회차에서 사전 선정한 두 지점만 보관한다. 풀 반환/수색 종료 시 해제한다.
	TMap<TWeakObjectPtr<AEnemyBase>, TArray<FVector>> SearchDestinations;
	TMap<TWeakObjectPtr<AEnemyBase>, FEnemyPatrolDestination> PatrolDestinations;
	uint64 NextSharedRevision = 0;

	// 방의 모든 직접 시야가 끊긴 뒤 이 시간이 지나면 마지막 타겟의 현재 위치를 한 번 공유한다.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Room", meta = (ClampMin = "0.0"))
	float ForcedTargetShareDelay = 180.0f;
};
