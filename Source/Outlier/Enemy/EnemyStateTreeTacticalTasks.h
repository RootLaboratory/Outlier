#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "EnemyEnvQuery.h"
#include "Drone/FlightPathfinding.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeTacticalTasks.generated.h"

class AEnemyAIController;
class UEnvQuery;
class URoomCombatSubsystem;
class ARoomVolume;
class UEnemyRoomSubsystem;

UENUM()
enum class EEnemyPatrolAction : uint8
{
	Session, Select, Release
};

USTRUCT()
struct FEnemyPatrolTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemyPatrolAction Action = EEnemyPatrolAction::Select;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float AcceptanceRadius = 50.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float DestinationSeparation = 200.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01"))
	float RetryInterval = 1.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float CellSize = 100.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float Clearance = 5.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float SearchPadding = 800.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1"))
	int32 MaxSearchNodes = 512;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1"))
	int32 MaxCollisionChecks = 2048;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01"))
	float MaxSearchMilliseconds = 4.0f;
	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = Output)
	TArray<FVector> SelectedWaypoints;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasDestination = false;
	UPROPERTY(EditAnywhere, Category = Output)
	EFlightPathStatus PathStatus = EFlightPathStatus::Unavailable;
	UPROPERTY(Transient)
	TWeakObjectPtr<AEnemyAIController> Controller;
	UPROPERTY(Transient)
	TObjectPtr<UEnemyRoomSubsystem> RoomSubsystem = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<URoomCombatSubsystem> CombatSubsystem = nullptr;
	UPROPERTY(Transient)
	int32 Generation = INDEX_NONE;
	UPROPERTY(Transient)
	int32 Lease = INDEX_NONE;
	// 같은 Select Task의 재진입에서는 다음 지점을 유지하고, 풀 대여/Controller 교체 시 B부터 시작한다.
	UPROPERTY(Transient)
	int32 NextPointIndex = 1;
};

USTRUCT(meta = (DisplayName = "Enemy Patrol", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemyPatrolTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyPatrolTaskInstanceData;
	FEnemyPatrolTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// 부모 순찰 수명은 자식 Select/Move 재진입과 분리한다. 이 Task는 상태 완료를 기다리게 하지 않는다.
USTRUCT(meta = (DisplayName = "Enemy Patrol Session", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemyPatrolSessionTask : public FEnemyPatrolTask
{
	GENERATED_BODY()
	FEnemyPatrolSessionTask();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

#if WITH_DEV_AUTOMATION_TESTS
namespace EnemyPatrolTesting
{
	OUTLIER_API EStateTreeRunStatus Enter(FEnemyPatrolTaskInstanceData& Data);
	OUTLIER_API EStateTreeRunStatus Tick(FEnemyPatrolTaskInstanceData& Data);
	OUTLIER_API void Exit(FEnemyPatrolTaskInstanceData& Data);
}
#endif

UENUM(BlueprintType)
enum class EEnemyEnvQueryResult : uint8
{
	None, Waiting, Running, Success, NoResults, Failed, Invalid, Stale, TimedOut, Cancelled,
	NoReachableDestination, PathBudgetExceeded, CandidateLimitReached
};

USTRUCT()
struct FEnemyRunEnvQueryTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
	UPROPERTY(EditAnywhere, Category = Parameter)
	TObjectPtr<UEnvQuery> QueryTemplate = nullptr;
	UPROPERTY(EditAnywhere, Category = Parameter)
	TArray<FAIDynamicParam> QueryConfig;
	UPROPERTY(EditAnywhere, Category = Parameter)
	TEnumAsByte<EEnvQueryRunMode::Type> RunMode = EEnvQueryRunMode::SingleResult;
	// 수색/고정 위치 질의에서는 끈다. 직접 교전 질의는 시야 상실/타깃 변경 시 폐기한다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bRequireVisibleTarget = true;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", Units = "cm"))
	float Clearance = 5.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01", Units = "s"))
	float QueryInterval = 0.5f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01", Units = "s"))
	float QueryTimeout = 2.0f;
	// 접근 질의에서 켠다. 후퇴/선회는 아래의 전용 검증 옵션을 사용한다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bValidateReachability = false;
	// 선회는 A* 지름길/긴 우회를 사용하지 않고 EQS 후보의 곡선 구간을 검증한다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bValidateOrbitPath = false;
	// 후퇴 전용 Generator와 함께 사용한다. 접근/선회 검증과 동시에 켜지 않는다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bValidateRetreatPath = false;
	// 자폭 추격 전용 후보를 초기 방 안의 A* 경로로 검증한다. 다른 검증 모드와 동시에 켜지 않는다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bValidateChasePath = false;
	FVector CapturedChaseStart = FVector::ZeroVector;
	int32 CapturedChaseLane = 0;
	FEnemyFlightHeightSnapshot HeightSnapshot;
	// None은 기존 전투 질의다. Anchor/Points는 Search Session 안에서만 실행한다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemySearchQueryMode SearchMode = EEnemySearchQueryMode::None;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0", Units = "cm"))
	float SearchPointSeparation = 200.0f;
	// 한 후보가 예산을 소진하면 간격을 두고 다음 후보부터 검증한다. 같은 후보를 무한 재검사하지 않는다.
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxSearchSelectionRetries = 2;
	TSharedPtr<FEnvQueryResult> DeferredSearchResult;
	int32 NextSearchCandidate = 0;
	int32 SearchSelectionRetries = 0;
	double NextSearchSelectionTime = 0.0;
	bool bSearchSelectionPending = false;
	bool bSearchPathBudgetExceeded = false;
	UPROPERTY(EditAnywhere, Category = Output)
	TArray<FVector> SelectedSearchPoints;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bSearchSelectionLimited = false;
	uint64 CapturedSearchSerial = 0;
	uint64 CapturedSharedRevision = 0;
	float CapturedSearchHeight = 0.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxPathCandidates = 4;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0", Units = "cm"))
	float PathCellSize = 100.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0", Units = "cm"))
	float SearchPadding = 800.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1", ClampMax = "65536"))
	int32 MaxSearchNodes = 512;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1", ClampMax = "262144"))
	int32 MaxCollisionChecks = 2048;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01"))
	float MaxSearchMilliseconds = 4.0f;
	// 0이면 기존 타깃 위치 변화 허용을 유지한다. 급이동한 타깃의 오래된 결과는 적용하지 않는다.
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", Units = "cm"))
	float TargetChangeDistance = 0.0f;
	UPROPERTY(EditAnywhere, Category = Output)
	FVector SelectedTargetLocation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AActor> SelectedTargetActor = nullptr;
	UPROPERTY(EditAnywhere, Category = Output)
	int32 CheckedPathCandidates = 0;
	UPROPERTY(EditAnywhere, Category = Output)
	EFlightPathStatus SelectionPathStatus = EFlightPathStatus::Unavailable;
	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasDestination = false;
	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyEnvQueryResult QueryResult = EEnemyEnvQueryResult::None;
	// 부모 State의 파라미터로 복사한 뒤 Fly의 PlannedWaypoints에 전달한다.
	UPROPERTY(EditAnywhere, Category = Output)
	TArray<FVector> SelectedWaypoints;
	UPROPERTY(EditAnywhere, Category = Output)
	float SelectedArcLength = 0.0f;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bSelectedOrbitLeft = false;
	// 양쪽의 모든 후보가 탈락/차단된 경우만 참이다. 예산/한도/무효 결과는 포함하지 않는다.
	UPROPERTY(EditAnywhere, Category = Output)
	bool bNoOrbitDestination = false;
	// 제한된 후보를 모두 검사한 경우만 참이다. 실패 전이에서 이 Task 출력에 직접 바인딩한다.
	UPROPERTY(EditAnywhere, Category = Output)
	bool bNoRetreatDestination = false;
	UPROPERTY(EditAnywhere, Category = Output)
	FVector RetreatOrigin = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = Output)
	FVector RetreatDirection = FVector::ZeroVector;
	FVector CapturedRetreatStart = FVector::ZeroVector;

	FVector CapturedOrbitStart = FVector::ZeroVector;
	FVector CapturedOrbitCenter = FVector::ZeroVector;
	float CapturedOrbitLength = 0.0f;
	bool bCapturedOrbitLeft = false;

	UPROPERTY(Transient)
	TWeakObjectPtr<AEnemyAIController> CachedController;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CapturedTarget;
	UPROPERTY(Transient)
	TWeakObjectPtr<ARoomVolume> CapturedRoom;
	UPROPERTY(Transient)
	TObjectPtr<URoomCombatSubsystem> CachedRooms = nullptr;
	UPROPERTY(Transient)
	FGameplayTag CapturedRoomTag;
	int32 CapturedGeneration = 0;
	int32 CapturedLease = 0;
	EEnemyCombatState CapturedCombatState = EEnemyCombatState::NonCombat;
	int32 RequestId = INDEX_NONE;
	double QueryStartedAt = 0.0;
	bool bActive = false;
};

// 목적지 선정만 수행한다. 성공 후 별도 자식 State의 Fly Task로 이동한다.
USTRUCT(meta = (DisplayName = "Enemy Run Env Query", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemyRunEnvQueryTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyRunEnvQueryTaskInstanceData;
	FEnemyRunEnvQueryTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual void PostEditInstanceDataChangeChainProperty(
		const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView) override;
#endif
};

UENUM(BlueprintType)
enum class EEnemyOrbitDirection : uint8
{
	Left,
	Right
};

USTRUCT()
struct FEnemySearchSessionTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
	UPROPERTY(EditAnywhere, Category = Output)
	FVector SearchCenter = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bSkipLossWait = false;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bShouldRestart = false;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bLocalGuard = false;
};

// Lost/Shared/LocalGuard를 모두 포함하는 부모 State에 둔다. 자식 이동 완료를 기다리지 않는다.
USTRUCT(meta = (DisplayName = "Enemy Search Session", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemySearchSessionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemySearchSessionTaskInstanceData;
	FEnemySearchSessionTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void TriggerTransitions(FStateTreeExecutionContext& Context) const override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

UENUM(BlueprintType)
enum class EEnemySearchProgress : uint8
{
	RestartWithHint,
	AnchorArrived,
	SelectCurrentPoint,
	PointChecked,
	LocalGuard
};

USTRUCT()
struct FEnemySearchProgressTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemySearchProgress Action = EEnemySearchProgress::SelectCurrentPoint;
	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = Output)
	int32 PointIndex = 0;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasPoint = false;
};

// SelectCurrentPoint 성공 -> Fly, 실패 -> LocalGuard. PointChecked는 LookAround 완료 뒤에만 실행한다.
USTRUCT(meta = (DisplayName = "Enemy Search Progress", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemySearchProgressTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemySearchProgressTaskInstanceData;
	FEnemySearchProgressTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

#if WITH_DEV_AUTOMATION_TESTS
// 테스트도 실제 Task의 판단을 호출한다. 테스트 전용 수색 로직을 별도로 만들지 않는다.
namespace EnemySearchTesting
{
	OUTLIER_API void CaptureQuery(FEnemyRunEnvQueryTaskInstanceData& Data);
	OUTLIER_API void CompleteQuery(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result);
	OUTLIER_API bool SelectDestinations(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result);
	OUTLIER_API EStateTreeRunStatus ResumeSelection(FEnemyRunEnvQueryTaskInstanceData& Data);
	OUTLIER_API EStateTreeRunStatus EnterSession(FEnemySearchSessionTaskInstanceData& Data);
	OUTLIER_API EStateTreeRunStatus TickSession(FEnemySearchSessionTaskInstanceData& Data);
	OUTLIER_API void RefreshSessionTransitions(FEnemySearchSessionTaskInstanceData& Data);
	OUTLIER_API void ExitSession(FEnemySearchSessionTaskInstanceData& Data);
	OUTLIER_API EStateTreeRunStatus Progress(FEnemySearchProgressTaskInstanceData& Data);
}
#endif


// 현재 위치를 기준으로 수평 원의 다음 후보를 계산한다.
// 1차 구현은 바닥 Trace + 고정 높이를 사용하고, 향후 3D EQS가 이 위치 선택 단계만 대체한다.
USTRUCT()
struct FEnemySelectHorizontalOrbitLocationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	FVector OrbitCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float OrbitRadius = 800.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FlightHeightOffset = 300.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float AngularStepDegrees = 45.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemyOrbitDirection OrbitDirection = EEnemyOrbitDirection::Left;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float FloorTraceHalfHeight = 10000.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector FacingLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Enemy Select Horizontal Orbit Location", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemySelectHorizontalOrbitLocationTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemySelectHorizontalOrbitLocationTaskInstanceData;

	FEnemySelectHorizontalOrbitLocationTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// 실제 플레이어 위치를 조회하지 않고 LKP 주변의 임의 좌표를 만들어 위협 사격에 사용한다.
USTRUCT()
struct FEnemySelectSuppressiveFireLocationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FVector LastKnownPlayerLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float SpreadRadius = 200.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector AttackLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Enemy Select Suppressive Fire Location", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemySelectSuppressiveFireLocationTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemySelectSuppressiveFireLocationTaskInstanceData;

	FEnemySelectSuppressiveFireLocationTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
