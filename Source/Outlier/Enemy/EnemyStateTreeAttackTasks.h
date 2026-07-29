#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeAttackTasks.generated.h"

// TargetActor가 비어 있으면 EnemyAIController가 현재 Sight 대상 중 가장 가까운 플레이어를 선택한다.
USTRUCT()
struct FEnemyAttackTargetTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MinAttackDuration = 0.7f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MaxAttackDuration = 1.2f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bRequireVisibleTarget = true;

	float ElapsedTime = 0.0f;
	float SelectedAttackDuration = 0.0f;
	float CombatDecisionRefreshElapsed = 0.0f;
	bool bAttackStarted = false;
	bool bWaitingForCombatDecision = false;
};

// Actor를 추적해 조준 좌표를 매 Tick 갱신하고 지정 시간 동안 CurrentWeapon을 실행한다.
// State가 중단되거나 완료되면 ExitState에서 반복 사격을 정리한다.
USTRUCT(meta = (DisplayName = "Enemy Attack Target", Category = "Enemy|Attack"))
struct OUTLIER_API FEnemyAttackTargetTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyAttackTargetTaskInstanceData;

	FEnemyAttackTargetTask();

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

// Actor를 추적하지 않는 좌표 기반 공격 데이터. LKP 위협 사격에 사용한다.
USTRUCT()
struct FEnemyAttackLocationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MinAttackDuration = 0.7f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MaxAttackDuration = 1.2f;

	float ElapsedTime = 0.0f;
	float SelectedAttackDuration = 0.0f;
	bool bAttackStarted = false;
};

// TargetLocation을 유지한 채 지정 시간 동안 CurrentWeapon을 실행한다.
// 시야가 없는 상태에서도 사용할 수 있으며 실제 플레이어 현재 위치는 참조하지 않는다.
USTRUCT(meta = (DisplayName = "Enemy Attack Location", Category = "Enemy|Attack"))
struct OUTLIER_API FEnemyAttackLocationTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyAttackLocationTaskInstanceData;

	FEnemyAttackLocationTask();

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
struct FEnemyPossessedBurstAttackTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	bool bAttackStarted = false;
};

// Possessed VEC fire uses the PlayerController viewpoint resolved by the weapon
// and completes only after the configured fixed burst has stopped.
USTRUCT(meta = (DisplayName = "Enemy Possessed Burst Attack", Category = "Enemy|Attack"))
struct OUTLIER_API FEnemyPossessedBurstAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyPossessedBurstAttackTaskInstanceData;

	FEnemyPossessedBurstAttackTask();

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
struct FEnemyAttackPhaseWaitTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemyAttackPhase Phase = EEnemyAttackPhase::Telegraph;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MinDuration = 0.5f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MaxDuration = 1.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bConsumePossessedAttackRequest = false;

	float ElapsedTime = 0.0f;
	float SelectedDuration = 0.0f;
};

// 실제 연출 에셋은 BP의 OnAttackPhaseChanged에서 선택한다.
// 같은 Task를 Telegraph와 Recover State에 각각 배치해 공격 리듬을 구성한다.
USTRUCT(meta = (DisplayName = "Enemy Attack Phase Wait", Category = "Enemy|Attack"))
struct OUTLIER_API FEnemyAttackPhaseWaitTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyAttackPhaseWaitTaskInstanceData;

	FEnemyAttackPhaseWaitTask();

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
