#pragma once

#include "CoreMinimal.h"
#include "Enemy/SelfDestructDrone.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeSelfDestructTasks.generated.h"

USTRUCT()
struct FEnemyChargeLastDirectionTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float TelegraphDuration = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeSpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MaxChargeDistance = 500.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float TargetStopDistance = 100.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	float ElapsedTime = 0.0f;

	bool bCompleted = false;
};

USTRUCT()
struct FEnemyBeginPossessedSelfDestructTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float TelegraphDuration = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeSpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MaxChargeDistance = 500.0f;
};

// 빙의 공격 입력 시 플레이어의 현재 시선 방향을 고정하고 자폭 돌진을 시작한다.
USTRUCT(meta = (DisplayName = "Enemy Begin Possessed Self Destruct", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyBeginPossessedSelfDestructTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyBeginPossessedSelfDestructTaskInstanceData;

	FEnemyBeginPossessedSelfDestructTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyUpdatePossessedSelfDestructTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeDuration = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeSpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = Output)
	float ElapsedTime = 0.0f;

	bool bCompleted = false;
};

// 고정된 방향으로 돌진을 갱신하고, 충격 방향 보정과 자폭 카운트다운을 유지한다.
USTRUCT(meta = (DisplayName = "Enemy Update Possessed Self Destruct", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyUpdatePossessedSelfDestructTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyUpdatePossessedSelfDestructTaskInstanceData;

	FEnemyUpdatePossessedSelfDestructTask();

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

// 진입 시 저장한 방향으로 2초 동안 돌진하며, 외부 충격은 가산 속도로만 반영한다.
USTRUCT(meta = (DisplayName = "Enemy Charge Last Direction", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyChargeLastDirectionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyChargeLastDirectionTaskInstanceData;

	FEnemyChargeLastDirectionTask();

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
struct FEnemyBombDetonateTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;
};

// 폭발 계산을 중복하지 않고 ASelfDestructDrone의 기존 서버 권한 사망 경로만 실행한다.
USTRUCT(meta = (DisplayName = "Enemy Bomb Detonate", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyBombDetonateTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyBombDetonateTaskInstanceData;

	FEnemyBombDetonateTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
