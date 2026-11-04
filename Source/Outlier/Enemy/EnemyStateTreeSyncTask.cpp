#include "EnemyStateTreeSyncTask.h"

#include "AIController.h"
#include "Enemy/EnemyAIController.h"
#include "Outlier.h"

FEnemyStateTreeSyncTask::FEnemyStateTreeSyncTask()
{
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = true;
	bShouldStateChangeOnReselect = false;

#if WITH_EDITORONLY_DATA
	// Keep a linked asset's global frame running while synchronization is active.
	bConsideredForCompletion = true;
	bCanEditConsideredForCompletion = false;
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
	InstanceData.bPossessionInProgress = InstanceData.Enemy->IsPossessionInProgress();
	InstanceData.bPossessedAttackHeld = InstanceData.Enemy->IsPossessedAttackHeld();
	InstanceData.bPossessedAttackQueued = InstanceData.Enemy->HasPossessedAttackQueued();
	InstanceData.bHasPossessedAttackRequest = InstanceData.Enemy->HasPossessedAttackRequest();
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled()
		&& InstanceData.bIsPossessed
		&& InstanceData.bHasPossessedAttackRequest)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] StateTreeSync Enemy=%s Held=%s Queued=%s HasRequest=%s"),
			*GetNameSafe(InstanceData.Enemy),
			InstanceData.bPossessedAttackHeld ? TEXT("true") : TEXT("false"),
			InstanceData.bPossessedAttackQueued ? TEXT("true") : TEXT("false"),
			InstanceData.bHasPossessedAttackRequest ? TEXT("true") : TEXT("false"));
	}
	InstanceData.bHasSharedTargetContact = InstanceData.Enemy->HasSharedTargetContact();
	InstanceData.bHasFreshSharedTargetLocation = InstanceData.Enemy->HasFreshSharedTargetLocation();
	InstanceData.SharedTargetLocation = InstanceData.Enemy->GetSharedTargetLocation();
	InstanceData.AIController = Cast<AAIController>(InstanceData.Enemy->GetController());
	InstanceData.TargetActor = InstanceData.Enemy->GetTrackedTarget();
	InstanceData.bHasTrackableTarget = IsValid(InstanceData.TargetActor);
	// 다른 플레이어가 보여도 현재 전투 대상이 공유로만 추적된다면 직접 시야로 표시하지 않는다.
	const AEnemyAIController* PerceptionController = Cast<AEnemyAIController>(InstanceData.AIController);
	InstanceData.bPlayerCurrentlyVisible = InstanceData.Enemy->IsPlayerCurrentlyVisible()
		&& PerceptionController && PerceptionController->IsTargetDirectlyVisible(InstanceData.TargetActor);
	InstanceData.LastKnownPlayerLocation = InstanceData.Enemy->GetLastKnownPlayerLocation();
	InstanceData.PatternStartPlayerLocation = InstanceData.Enemy->GetPatternStartPlayerLocation();
	InstanceData.EnemyType = InstanceData.Enemy->GetRuntimeStat().Type;
	InstanceData.NonCombatBehavior = InstanceData.Enemy->GetNonCombatBehavior();
	InstanceData.PatrolPointA = InstanceData.Enemy->GetPatrolPointA();
	InstanceData.PatrolPointB = InstanceData.Enemy->GetPatrolPointB();
}
