#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "EnemyStateTreeTacticalTasks.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeCombatMovementTasks.generated.h"

UENUM(BlueprintType)
enum class EEnemyTacticalDistanceMode : uint8
{
	Horizontal2D,
	Spatial3D
};

USTRUCT()
struct FEnemyApproachTargetTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemyTacticalDistanceMode DistanceMode = EEnemyTacticalDistanceMode::Horizontal2D;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float OrbitRadius = 1000.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float OrbitEnterDistance = 1000.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float OrbitEntryPlanningDistance = 1400.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FlightHeightOffset = 300.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float EntryLookAheadDegrees = 25.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01"))
	float TargetRefreshInterval = 0.2f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MinRetargetDistance = 100.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float AcceptanceRadius = 100.0f;

	// RuntimeStat.MoveSpeed를 기준으로 이 Task에서 사용할 이동 속도 비율이다.
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float SpeedMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float FloorTraceHalfHeight = 10000.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	float TargetDistance = 0.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector MoveDestination = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bShouldEnterOrbit = false;

	float RefreshElapsedTime = 0.0f;
	bool bHasMoveDestination = false;
};

// 먼 거리에서는 직접 접근하고, 선회 반경 근처에서는 접선 방향 진입점으로 이동한다.
// 거리 계산 모드와 후보 선택 단계를 분리해 이후 3D EQS 후보로 교체할 수 있게 둔다.
USTRUCT(meta = (DisplayName = "Enemy Approach Target", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyApproachTargetTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyApproachTargetTaskInstanceData;

	FEnemyApproachTargetTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		float DeltaTime) const override;
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyOrbitTargetTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemyTacticalDistanceMode DistanceMode = EEnemyTacticalDistanceMode::Horizontal2D;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float OrbitRadius = 1000.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float OrbitExitDistance = 1200.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FlightHeightOffset = 300.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float LookAheadDegrees = 25.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float CandidateAngularStep = 15.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxCandidateAttempts = 3;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01"))
	float TargetRefreshInterval = 0.2f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MinRetargetDistance = 100.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float AcceptanceRadius = 100.0f;

	// RuntimeStat.MoveSpeed를 기준으로 이 Task에서 사용할 이동 속도 비율이다.
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float SpeedMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float SeparationRadius = 200.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RetryCooldown = 0.5f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float FloorTraceHalfHeight = 10000.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	float TargetDistance = 0.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector MoveDestination = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyOrbitDirection ActiveOrbitDirection = EEnemyOrbitDirection::Left;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bShouldFallbackToApproach = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bShouldExitOrbit = false;

	float RefreshElapsedTime = 0.0f;
	float RetryElapsedTime = 0.0f;
	float PhaseOffsetDegrees = 0.0f;
	bool bHasMoveDestination = false;
};

// 진입 시 선택한 좌우 방향을 상태가 끝날 때까지 유지한다.
// 최대 세 후보를 순서대로 검사하고 모두 막히면 접근 폴백 신호를 출력한다.
USTRUCT(meta = (DisplayName = "Enemy Orbit Target", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyOrbitTargetTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyOrbitTargetTaskInstanceData;

	FEnemyOrbitTargetTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		float DeltaTime) const override;
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
