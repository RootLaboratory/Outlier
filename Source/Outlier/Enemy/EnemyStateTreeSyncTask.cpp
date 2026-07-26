#include "EnemyStateTreeSyncTask.h"

#include "AIController.h"
#include "Enemy/EnemyAIController.h"
#include "StateTree.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyStateTreeSync, Log, All);

namespace
{
const UStateTree* ResolveActiveStateTree(const FStateTreeExecutionContext& Context)
{
	if (const FStateTreeExecutionFrame* Frame = Context.GetCurrentlyProcessedFrame())
	{
		return Frame->StateTree;
	}

	return Context.GetStateTree();
}
}

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
	if (InstanceData.CombatState == EEnemyCombatState::Combat)
	{
		UE_LOG(
			LogEnemyStateTreeSync,
			Display,
			TEXT("[Battle][StateSync] Enter. Tree=%s Enemy=%s Visible=%d Target=%s"),
			*GetNameSafe(ResolveActiveStateTree(Context)),
			*GetNameSafe(InstanceData.Enemy),
			InstanceData.bPlayerCurrentlyVisible,
			*GetNameSafe(InstanceData.TargetActor));
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyStateTreeSyncTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	SyncFromEnemy(InstanceData);
	if (InstanceData.CombatState == EEnemyCombatState::Combat)
	{
		UE_LOG(
			LogEnemyStateTreeSync,
			Display,
			TEXT("[Battle][StateSync] Event. Tree=%s Enemy=%s Current=%s Visible=%d Target=%s SharedContact=%d SharedLocation=%s LastKnown=%s"),
			*GetNameSafe(ResolveActiveStateTree(Context)),
			*GetNameSafe(InstanceData.Enemy),
			InstanceData.Enemy
				? *InstanceData.Enemy->GetActorLocation().ToCompactString()
				: TEXT("None"),
			InstanceData.bPlayerCurrentlyVisible,
			*GetNameSafe(InstanceData.TargetActor),
			InstanceData.bHasSharedTargetContact,
			*InstanceData.SharedTargetLocation.ToCompactString(),
			*InstanceData.LastKnownPlayerLocation.ToCompactString());

		if (!InstanceData.bPlayerCurrentlyVisible)
		{
			UE_LOG(
				LogEnemyStateTreeSync,
				Display,
				TEXT("[TargetLostMove][StateSync] Enemy=%s Current=%s Target=%s SharedContact=%d SharedLocation=%s LastKnown=%s"),
				*GetNameSafe(InstanceData.Enemy),
				InstanceData.Enemy
					? *InstanceData.Enemy->GetActorLocation().ToCompactString()
					: TEXT("None"),
				*GetNameSafe(InstanceData.TargetActor),
				InstanceData.bHasSharedTargetContact,
				*InstanceData.SharedTargetLocation.ToCompactString(),
				*InstanceData.LastKnownPlayerLocation.ToCompactString());
		}
	}
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
