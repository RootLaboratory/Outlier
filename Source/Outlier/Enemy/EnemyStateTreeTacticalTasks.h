#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
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

// RoomSubsystem에서 LKP 원형 슬롯을 요청하고 Move To/Maintain Facing에 바인딩할 좌표를 출력한다.
USTRUCT()
struct FEnemyRequestSearchRingSlotTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	FVector SearchCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RingRadius = 1200.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FlightHeightOffset = 300.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ReassignmentDistance = 200.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float FloorTraceHalfHeight = 10000.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector FacingLocation = FVector::ZeroVector;
};

// 슬롯을 보유하는 동안 Running을 유지한다.
// 재감지 등으로 State를 이탈하면 ExitState에서 슬롯을 자동 반환한다.
USTRUCT(meta = (DisplayName = "Enemy Request Search Ring Slot", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemyRequestSearchRingSlotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyRequestSearchRingSlotTaskInstanceData;

	FEnemyRequestSearchRingSlotTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyReleaseSearchRingSlotTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
};

USTRUCT(meta = (DisplayName = "Enemy Release Search Ring Slot", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemyReleaseSearchRingSlotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyReleaseSearchRingSlotTaskInstanceData;

	FEnemyReleaseSearchRingSlotTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

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
