#include "EnemyStateTreeTransitionTasks.h"

FEnemyCommitBattleTask::FEnemyCommitBattleTask()
{
	// 매 프레임이 아니라 GameplayTag 이벤트 수신 시에만 Tick해서, CombatState가 Combat으로
	// 바뀌었는지 확인 후 Succeeded를 반환한다.
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = true;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyCommitBattleTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bCommitRequested = false;

	AEnemyBase* Enemy = InstanceData.Enemy;
	if (!Enemy || !Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Enemy->GetCombatState() == EEnemyCombatState::Combat)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (!Enemy->CommitAlertToCombat())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bCommitRequested = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyCommitBattleTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.Enemy->GetCombatState() == EEnemyCombatState::Combat)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return InstanceData.bCommitRequested
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

FEnemyCommitNonBattleTask::FEnemyCommitNonBattleTask()
{
	// 매 프레임이 아니라 GameplayTag 이벤트 수신 시에만 Tick해서, CombatState가 NonCombat으로
	// 바뀌었는지 확인 후 Succeeded를 반환한다.
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = true;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyCommitNonBattleTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bCommitRequested = false;

	AEnemyBase* Enemy = InstanceData.Enemy;
	if (!Enemy || !Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Enemy->GetCombatState() == EEnemyCombatState::NonCombat)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (!Enemy->CommitAlertToNonCombat())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bCommitRequested = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyCommitNonBattleTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.Enemy->GetCombatState() == EEnemyCombatState::NonCombat)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return InstanceData.bCommitRequested
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}
