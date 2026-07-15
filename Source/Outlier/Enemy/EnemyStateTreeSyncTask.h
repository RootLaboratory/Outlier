#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeSyncTask.generated.h"

USTRUCT()
struct FEnemyStateTreeSyncTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyCombatState CombatState = EEnemyCombatState::NonCombat;

	// StateTree 바인딩 호환성을 위해 bool 
	UPROPERTY(EditAnywhere, Category = Output)
	bool bIsPossessed = false;

	// StateTree 바인딩 호환성을 위해 bool 
	UPROPERTY(EditAnywhere, Category = Output)
	bool bPlayerCurrentlyVisible = false;
};

USTRUCT(meta = (
	DisplayName = "Enemy State Sync",
	Category = "Enemy"
	))
struct OUTLIER_API FEnemyStateTreeSyncTask
	: public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyStateTreeSyncTaskInstanceData;

	FEnemyStateTreeSyncTask();

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

private:
	void SyncFromEnemy(FInstanceDataType& InstanceData) const;
};
