#include "EnemyStateTreeSyncTask.h"

#include "AIController.h"
#include "Enemy/EnemyAIController.h"

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
	InstanceData.bHasSharedTargetContact = InstanceData.Enemy->HasSharedTargetContact();
	InstanceData.SharedTargetLocation = InstanceData.Enemy->GetSharedTargetLocation();
	InstanceData.AIController = Cast<AAIController>(InstanceData.Enemy->GetController());
	const AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(InstanceData.AIController);
	InstanceData.TargetActor = EnemyAIController
		? EnemyAIController->GetPreferredVisibleTarget()
		: nullptr;
	InstanceData.LastKnownPlayerLocation = InstanceData.Enemy->GetLastKnownPlayerLocation();
	InstanceData.PatternStartPlayerLocation = InstanceData.Enemy->GetPatternStartPlayerLocation();
	InstanceData.EnemyType = InstanceData.Enemy->GetRuntimeStat().Type;
	InstanceData.NonCombatBehavior = InstanceData.Enemy->GetNonCombatBehavior();
	InstanceData.PatrolPointA = InstanceData.Enemy->GetPatrolPointA();
	InstanceData.PatrolPointB = InstanceData.Enemy->GetPatrolPointB();
}
