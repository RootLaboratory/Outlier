#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeImpactTasks.generated.h"

USTRUCT()
struct FEnemyStopForImpactTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
};

// 현재 행동을 정리하고 Perception을 멈춘 뒤 공용 반동 회복 State에 제어를 넘긴다.
USTRUCT(meta = (DisplayName = "Enemy Stop For Impact", Category = "Enemy|Impact"))
struct OUTLIER_API FEnemyStopForImpactTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyStopForImpactTaskInstanceData;

	FEnemyStopForImpactTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyRecoverFromKnockbackTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	float ElapsedTime = 0.0f;
};

// State가 활성화된 동안에만 3D 반동 속도를 감속하고 완료 시 AI 제어를 복구한다.
USTRUCT(meta = (DisplayName = "Enemy Recover From Knockback", Category = "Enemy|Impact"))
struct OUTLIER_API FEnemyRecoverFromKnockbackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyRecoverFromKnockbackTaskInstanceData;

	FEnemyRecoverFromKnockbackTask();

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
