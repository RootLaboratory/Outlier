#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeMovementTasks.generated.h"

USTRUCT()
struct FEnemyFlyToLocationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float AcceptanceRadius = 50.0f;

	// RuntimeStat.MoveSpeed를 기준으로 이 Task에서 사용할 이동 속도 비율이다.
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float SpeedMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;

};

// 지상 NavMesh에 투영하지 않고 비행 Pawn을 3D 목적지로 직접 이동시킨다.
USTRUCT(meta = (DisplayName = "Enemy Fly To Location", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyFlyToLocationTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyFlyToLocationTaskInstanceData;

	FEnemyFlyToLocationTask();

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
struct FEnemyFaceLocationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float AngleTolerance = 1.0f;
};

USTRUCT(meta = (DisplayName = "Enemy Face Location", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyFaceLocationTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyFaceLocationTaskInstanceData;

	FEnemyFaceLocationTask();

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
};

USTRUCT()
struct FEnemyMaintainFacingTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	// 직접 추적할 Actor가 있는 전투 행동에서만 바인딩한다.
	UPROPERTY(EditAnywhere, Category = Input, meta = (Optional))
	TObjectPtr<AActor> TargetActor = nullptr;

	// Actor가 없는 LKP 수색/위협 사격에서는 이 좌표만 바인딩한다.
	UPROPERTY(EditAnywhere, Category = Input, meta = (Optional))
	FVector TargetLocation = FVector::ZeroVector;

	// TargetActor가 비어 있으면 AIController가 선택한 현재 Sight 타깃을 사용한다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bUsePreferredVisibleTarget = false;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;
};

// Move To나 공격 Task와 병렬로 실행하며 TargetLocation을 계속 바라본다.
// 스스로 상태를 완료시키지 않고 다른 실행 Task의 완료/전환을 따른다.
USTRUCT(meta = (DisplayName = "Enemy Maintain Facing", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyMaintainFacingTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyMaintainFacingTaskInstanceData;

	FEnemyMaintainFacingTask();

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
};

USTRUCT()
struct FEnemyAlertHoldTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float VisibleCommitDuration = 1.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float LostCommitDuration = 5.0f;

	float VisibleElapsedTime = 0.0f;
	float LostElapsedTime = 0.0f;
};

// Alert 상태 안에서 LKP를 바라보며 감지 유지/상실 시간을 판정한다.
// 확정 시 EnemyBase가 보낸 이벤트가 StateTree를 Battle 또는 NonBattle로 전환한다.
USTRUCT(meta = (DisplayName = "Enemy Resolve Alert", Category = "Enemy|State"))
struct OUTLIER_API FEnemyAlertHoldTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyAlertHoldTaskInstanceData;

	FEnemyAlertHoldTask();

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
};

USTRUCT()
struct FEnemyRotateToAngleAndWaitTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float RelativeYawDegrees = 90.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float WaitDuration = 1.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float AngleTolerance = 1.0f;

	float TargetYaw = 0.0f;
	float ElapsedWaitTime = 0.0f;
};

USTRUCT(meta = (DisplayName = "Enemy Rotate To Angle And Wait", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyRotateToAngleAndWaitTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyRotateToAngleAndWaitTaskInstanceData;

	FEnemyRotateToAngleAndWaitTask();

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
};

USTRUCT(BlueprintType)
struct FEnemyLookAroundStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float HoldDuration = 0.2f;
};

USTRUCT()
struct FEnemyLookAroundTaskInstanceData
{
	GENERATED_BODY()

	FEnemyLookAroundTaskInstanceData();

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	// 기본 순서는 중앙 -> 좌 -> 중앙 -> 우 -> 중앙이다.
	UPROPERTY(EditAnywhere, Category = Parameter)
	TArray<FEnemyLookAroundStep> Steps;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RotationSpeed = 120.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float AngleTolerance = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bRandomizeFirstDirection = true;

	FRotator BaseControlRotation = FRotator::ZeroRotator;
	int32 CurrentStepIndex = 0;
	int32 StepDirection = 1;
	float ElapsedHoldTime = 0.0f;
	bool bOwnsTaskDrivenPitch = false;
};

// 이동을 중지하지 않고 설정된 Pitch/Yaw 지점을 한 번 순회한 뒤 성공한다.
// StateTree에서 이동 Task와 병렬 배치해 LKP 수색과 선회 연출에 공용으로 사용한다.
USTRUCT(meta = (DisplayName = "Enemy Look Around", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyLookAroundTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyLookAroundTaskInstanceData;

	FEnemyLookAroundTask();

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
