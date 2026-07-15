#include "EnemyStateTreeSyncTask.h"

FEnemyStateTreeSyncTask::FEnemyStateTreeSyncTask()
{
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = true;
	bShouldStateChangeOnReselect = false;

#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif // WITH_EDITORONLY_DATA

}

EStateTreeRunStatus FEnemyStateTreeSyncTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	SyncFromEnemy(InstanceData);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyStateTreeSyncTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	SyncFromEnemy(InstanceData);
	return EStateTreeRunStatus::Running;
}

void FEnemyStateTreeSyncTask::SyncFromEnemy(FInstanceDataType& InstanceData) const
{
	if (!InstanceData.Enemy)
	{
		return;
	}

	InstanceData.CombatState = InstanceData.Enemy->GetCombatState();
	InstanceData.bIsPossessed = InstanceData.Enemy->IsEnemyPossessed();
	InstanceData.bPlayerCurrentlyVisible = InstanceData.Enemy->IsPlayerCurrentlyVisible();
}
