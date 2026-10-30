#include "EnemyStateTreeReturnTasks.h"

#include "EnemyBase.h"
#include "StateTreeExecutionContext.h"

namespace
{
bool CanReturnHome(const AEnemyBase* Enemy)
{
	return IsValid(Enemy) && Enemy->HasAuthority() && !Enemy->IsDead()
		&& !Enemy->IsEnemyPossessed() && !Enemy->IsPossessionInProgress()
		&& Enemy->GetCombatState() == EEnemyCombatState::Returning;
}
}

FEnemySelectReturnHomeTask::FEnemySelectReturnHomeTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FEnemySelectReturnHomeTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Destination = FVector::ZeroVector;
	Data.RetryElapsed = 0.0f;
	return CanReturnHome(Data.Enemy) ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemySelectReturnHomeTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!CanReturnHome(Data.Enemy))
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.RetryElapsed += FMath::Max(DeltaTime, 0.0f);
	if (Data.RetryElapsed < 1.0f)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.RetryElapsed = 0.0f;
	return Data.Enemy->GetReturnHomeDestination(Data.Destination)
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

FEnemyCompleteReturnHomeTask::FEnemyCompleteReturnHomeTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FEnemyCompleteReturnHomeTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	return CanReturnHome(Data.Enemy) && Data.Enemy->CompleteReturnHome()
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}
