#include "Enemy/EnemyStateTreeImpactTasks.h"

FEnemyStopForImpactTask::FEnemyStopForImpactTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyStopForImpactTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Enemy->BeginImpactReaction())
	{
		return EStateTreeRunStatus::Failed;
	}

	// 같은 State의 Recover Task가 회복을 완료할 때까지 State를 유지한다.
	// 여기서 즉시 성공하면 Tasks 완료 정책이 Any일 때 Recover Task가 Tick되기 전에 State가 종료된다.
	return EStateTreeRunStatus::Running;
}

FEnemyRecoverFromKnockbackTask::FEnemyRecoverFromKnockbackTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyRecoverFromKnockbackTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Stop Task 없이 단독 배치된 경우에도 동일한 진입 계약을 보장한다.
	if (!InstanceData.Enemy->IsImpactReactionActive()
		&& !InstanceData.Enemy->BeginImpactReaction())
	{
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyRecoverFromKnockbackTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += FMath::Max(DeltaTime, 0.0f);
	if (!InstanceData.Enemy->UpdateImpactRecovery(
		DeltaTime,
		InstanceData.ElapsedTime))
	{
		return EStateTreeRunStatus::Running;
	}

	InstanceData.Enemy->EndImpactReaction();
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyRecoverFromKnockbackTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (IsValid(InstanceData.Enemy) && InstanceData.Enemy->HasAuthority())
	{
		// Stun, Possession, Death 전환으로 중단돼도 Task가 시작한 반동 상태를 남기지 않는다.
		InstanceData.Enemy->EndImpactReaction();
	}
}
