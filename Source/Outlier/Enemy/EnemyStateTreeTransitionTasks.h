#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeTransitionTasks.generated.h"

USTRUCT()
struct FEnemyCommitBattleTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	bool bCommitRequested = false;
};

// 기존 StateTree 에셋 호환용이다. 신규 구성은 Enemy Resolve Alert Task를 사용한다.
USTRUCT(meta = (DisplayName = "Legacy Enemy Commit Battle", Category = "Enemy|Legacy"))
struct OUTLIER_API FEnemyCommitBattleTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyCommitBattleTaskInstanceData;

	FEnemyCommitBattleTask();

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
struct FEnemyCommitNonBattleTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	bool bCommitRequested = false;
};

// 기존 StateTree 에셋 호환용이다. 신규 구성은 Enemy Resolve Alert Task를 사용한다.
USTRUCT(meta = (DisplayName = "Legacy Enemy Commit Non Battle", Category = "Enemy|Legacy"))
struct OUTLIER_API FEnemyCommitNonBattleTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyCommitNonBattleTaskInstanceData;

	FEnemyCommitNonBattleTask();

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
