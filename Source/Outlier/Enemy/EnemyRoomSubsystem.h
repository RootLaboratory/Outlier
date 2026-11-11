#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyRoomSubsystem.generated.h"

class AEnemyBase;
class AActor;
class ULevel;
class AController;

// 경로 전체가 아니라 두 개체가 비켜 갈 여유가 없는 구간만 독점한다. 좌표는 캡슐 중심이다.
struct FEnemyCombatCorridor
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	bool bEntered = false;
};

struct FEnemyCombatReservation
{
	TWeakObjectPtr<AController> Controller;
	int32 Generation = 0;
	int32 Lease = 0;
	uint64 Episode = 0;
	uint64 Request = 0;
	FVector Destination = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	FVector ProgressLocation = FVector::ZeroVector;
	FVector ObservationCenter = FVector::ZeroVector;
	TArray<FEnemyCombatCorridor> Corridors;
	TArray<FVector> Route;
	double FurthestRouteProgress = 0.0;
	double ExpiresAt = 0.0;
};

// 같은 목표 회차에서는 재요청해도 순번을 유지한다. 성공/실패 결과의 소유권은 Request로 따로 구분한다.
struct FEnemyCombatReservationRequest
{
	FEnemyCombatReservation Demand;
	uint64 Ticket = 0;
	double WaitingSince = -1.0;
	double PriorityUntil = 0.0;
	double NextReadinessCheckAt = 0.0;
	bool bAttackReady = false;
	bool bWaiting = false;
	bool bActive = false;
};

// 이동 허가가 아니라 비켜 줄 경로를 평가하라는 요청이다. 실제 예약과 충돌 검사는 별도로 통과해야 한다.
struct FEnemyCombatYieldContext
{
	TWeakObjectPtr<AEnemyBase> Requester;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	uint64 Serial = 0;
};

struct FEnemyCombatYieldRequest
{
	FEnemyCombatYieldContext Context;
	TWeakObjectPtr<AController> RequesterController;
	TWeakObjectPtr<AController> BlockerController;
	TWeakObjectPtr<AActor> RequesterTarget;
	TWeakObjectPtr<AActor> BlockerTarget;
	int32 RequesterGeneration = 0;
	int32 RequesterLease = 0;
	int32 BlockerGeneration = 0;
	int32 BlockerLease = 0;
	uint64 RequesterEpisode = 0;
	double WaitingSince = -1.0;
	double BlockerPriorityUntil = 0.0;
	double RequesterPriorityUntil = 0.0;
	bool bApproved = false;
	double ExpiresAt = 0.0;
};

// 순찰 예약은 수색 회차와 분리한다. 같은 Actor의 새 대여나 Controller 교체도 이전 예약과 구분한다.
struct FEnemyPatrolDestination
{
	FVector Location = FVector::ZeroVector;
	FGameplayTag RoomTag;
	TWeakObjectPtr<AController> Controller;
	int32 Generation = 0;
	int32 Lease = 0;
};

// 실제 관측 시각은 직접 시야 보고만 갱신한다. 공유 수신/강제 핑과 별도 수명이다.
struct FEnemyRoomTargetObservation
{
	FVector LastObservedLocation = FVector::ZeroVector;
	double LastObservedAt = 0.0;
};

struct FEnemyRoomDirectObserver
{
	TWeakObjectPtr<AActor> TargetActor;
	TWeakObjectPtr<AController> Controller;
	int32 Generation = 0;
	int32 Lease = 0;
};

// 조회 결과는 값 복사다. 수신자가 보관하거나 다시 조회해도 유예를 연장하지 않는다.
struct FEnemyRoomTargetObservationSnapshot
{
	TWeakObjectPtr<AActor> TargetActor;
	FVector LastObservedLocation = FVector::ZeroVector;
	double LastObservedAt = 0.0;
	double TrackingExpiresAt = 0.0;
	bool bDirectlyObserved = false;
};

// 직접 시야를 가진 Enemy만 Observer로 등록한다.
// 수신한 공유 좌표만으로는 Observer가 되지 않으므로 방 안에서 위치가 재전파되지 않는다.
struct FEnemyRoomTargetContactState
{
	TWeakObjectPtr<AActor> TargetActor;
	FVector LastReportedLocation = FVector::ZeroVector; // 기존 좌표 공유/강제 핑용이며 관측 시각과 분리한다.
	// 관측자별 실제 타깃을 보관해 서로 다른 플레이어 보고가 공유 위치를 덮어쓰지 않게 한다.
	TMap<TWeakObjectPtr<AEnemyBase>, FEnemyRoomDirectObserver> DirectObservers;
	TMap<TWeakObjectPtr<AActor>, FEnemyRoomTargetObservation> TargetObservations;
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
	void GetActiveCombatEnemies(TArray<AEnemyBase*>& OutEnemies) const;

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

	// 같은 전투 Room의 대상 정보를 조회한다. 유예 길이는 수신 Enemy의 기존 스탯을 사용한다.
	// 실제 추적/공격 연결은 이 API의 소비자가 담당하며 직접 시야 상태는 변경하지 않는다.
	bool TryGetRoomTargetObservation(AEnemyBase* Receiver, AActor* TargetActor,
		FEnemyRoomTargetObservationSnapshot& OutSnapshot);
	// 현재 대상 유지 여부는 Pawn이 먼저 판단한다. 이 함수는 대체 후보만 기존 우선순위로 고른다.
	bool TrySelectRoomTarget(AEnemyBase* Receiver, FEnemyRoomTargetObservationSnapshot& OutSnapshot,
		bool bRequireLiveObservation = false);

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

	uint64 BeginCombatReservationRequest(AEnemyBase* Enemy, uint64 Episode);
	bool IsCombatReservationRequestCurrent(const AEnemyBase* Enemy, uint64 Request) const;
	bool TryReserveCombatDestination(AEnemyBase* Enemy, uint64 Request, const FVector& Destination,
		const TArray<FEnemyCombatCorridor>& Corridors, const TArray<FVector>& Route, float Clearance);
	bool IsCombatSegmentReserved(const AEnemyBase* Enemy, uint64 Request, const FVector& Start, const FVector& End) const;
	bool RefreshCombatReservation(AEnemyBase* Enemy, uint64 Request);
	bool IsCombatReservationCurrent(const AEnemyBase* Enemy, uint64 Request) const;
	// 0은 Pawn의 명시적 수명 종료에만 사용한다. Task/콜백은 반드시 자신이 받은 Request를 전달한다.
	void ReleaseCombatReservation(AEnemyBase* Enemy, uint64 Request = 0);
	bool RequestCombatYield(AEnemyBase* Requester, AEnemyBase* Blocker, const FVector& Start, const FVector& End);
	bool GetCombatYieldContext(const AEnemyBase* Enemy, FEnemyCombatYieldContext& Out) const;
	void FinishCombatYield(const AEnemyBase* Enemy, uint64 Serial);
	double GetCombatObservationCrowding(const AEnemyBase* Enemy, const FVector& Center, const FVector& Candidate) const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FEnemyF4ReservationTest;
#endif
	void BroadcastSharedTargetContact(FGameplayTag RoomTag, const FVector& TargetLocation, bool bNewInformation = true);
	void BroadcastSharedTargetLost(FGameplayTag RoomTag);
	void CompactTargetContactState(FGameplayTag RoomTag, FEnemyRoomTargetContactState& ContactState);
	void SynchronizeEnemyWithRoomState(AEnemyBase* Enemy);
	void ScheduleForcedTargetShare(FGameplayTag RoomTag);
	void HandleForcedTargetShare(FGameplayTag RoomTag);
	FGameplayTag ResolveEnemyRegistrationKey(const AEnemyBase* Enemy) const;
	FGameplayTag ResolveEnemyRoomTag(const AEnemyBase* Enemy) const;
	void CompactRegisteredEnemies(FGameplayTag RoomTag);
	void CompactAllRegisteredEnemies();
	void HandleArenaReleased();
	void CompactCombatReservations();
	bool IsCombatReservationOwnerCurrent(const AEnemyBase* Enemy, const FEnemyCombatReservation& Reservation) const;
	bool IsCombatYieldCurrent(const AEnemyBase* Blocker, const FEnemyCombatYieldRequest& Request) const;
	void RefreshCombatReadiness(AEnemyBase* Enemy, FEnemyCombatReservationRequest& Request);
	bool HasCombatContenderPriority(const AEnemyBase* AOwner, const FEnemyCombatReservationRequest& A,
		const AEnemyBase* BOwner, const FEnemyCombatReservationRequest& B) const;

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
	TMap<TWeakObjectPtr<AEnemyBase>, FEnemyCombatReservation> CombatDestinations;
	TMap<TWeakObjectPtr<AEnemyBase>, FEnemyCombatReservationRequest> CombatReservationRequests;
	TMap<TWeakObjectPtr<AEnemyBase>, FEnemyCombatYieldRequest> CombatYieldRequests;
	uint64 NextCombatYieldSerial = 0;
	uint64 NextCombatReservationRequest = 0;
	uint64 NextCombatReservationTicket = 0;
	uint64 NextSharedRevision = 0;

	// 방의 모든 직접 시야가 끊긴 뒤 이 시간이 지나면 마지막 타겟의 현재 위치를 한 번 공유한다.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Room", meta = (ClampMin = "0.0"))
	float ForcedTargetShareDelay = 180.0f;
};
