#include "EnemyStateTreeSyncTask.h"

#include "AIController.h"
#include "Enemy/AutoTurret.h"
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
	const AAutoTurret* Turret = Cast<AAutoTurret>(InstanceData.Enemy);
	InstanceData.bHackingInProgress = Turret && Turret->IsHackingInProgress();
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
	const FEnemyPerceptionGoal& Goal = InstanceData.Enemy->GetPerceptionGoal();
	InstanceData.PerceptionGoalType = Goal.Type;
	// 저장된 트리의 위치 조사 분기/바인딩을 공유한다. 런타임 근거는 InvestigateHit로 남고 청각 사건은 만들지 않는다.
	if (Goal.Type == EEnemyPerceptionGoalType::InvestigateHit)
		InstanceData.PerceptionGoalType = InstanceData.Enemy->CanRunHitInvestigationMovement()
			? EEnemyPerceptionGoalType::InvestigateNoise : EEnemyPerceptionGoalType::None;
	InstanceData.PerceptionGoalLocation = Goal.Location;
	InstanceData.PerceptionGoalTarget = Goal.Target.Get();
	// 채택 전에는 false여야 저장된 트리의 '새 조사 진입' 전이를 통과한다. 회전 소유권과 이동 채택은 별개다.
	InstanceData.bNoiseInvestigationActive = InstanceData.Enemy->IsLocationInvestigationAdopted();
	InstanceData.bNoiseSearchActive = InstanceData.Enemy->IsLocationSearchActive();
	if (InstanceData.Enemy->HasHitResponse() && InstanceData.CombatState != EEnemyCombatState::Returning)
	{
		// 이전 추적 정보는 Pawn에 유지하되, 조사 중 트리의 공격/공유 분기를 선택할 권한은 주지 않는다.
		InstanceData.TargetActor = nullptr;
		InstanceData.bHasTrackableTarget = false;
		InstanceData.bHasSharedTargetContact = InstanceData.bHasFreshSharedTargetLocation = false;
	}
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
