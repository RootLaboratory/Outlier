#include "EnemyStateTreeReturnTasks.h"

#include "EnemyBase.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
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
	Data.bGuarding = false;
	if (!CanReturnHome(Data.Enemy))
	{
		return EStateTreeRunStatus::Failed;
	}
	// 최초 선택은 즉시 시도하고, 실패 이후의 대기는 Pawn의 동일 복구 회차가 관리한다.
	const bool bSelected = Data.Enemy->TrySelectReturnHomeDestination(Data.Destination);
	Data.bGuarding = Data.Enemy->IsReturnHomeGuarding();
	return bSelected ? EStateTreeRunStatus::Succeeded
		: Data.bGuarding ? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemySelectReturnHomeTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!CanReturnHome(Data.Enemy))
	{
		return EStateTreeRunStatus::Failed;
	}
	const bool bSelected = Data.Enemy->TrySelectReturnHomeDestination(Data.Destination);
	Data.bGuarding = Data.Enemy->IsReturnHomeGuarding();
	return bSelected ? EStateTreeRunStatus::Succeeded
		: Data.bGuarding ? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Running;
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

FEnemyReturnHomeGuardTask::FEnemyReturnHomeGuardTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FEnemyReturnHomeGuardTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (CanReturnHome(Data.Enemy) && Data.Enemy->IsReturnHomeGuarding())
	{
		// 기절 복원 뒤 복구 이벤트 구독을 다시 연결하되, 소진 예산은 돌려주지 않는다.
		FVector Unused;
		Data.Enemy->TrySelectReturnHomeDestination(Unused);
		if (AAIController* Controller = Cast<AAIController>(Data.Enemy->GetController()))
		{
			Controller->StopMovement();
		}
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}
	return Tick(Context, 0.0f);
}

EStateTreeRunStatus FEnemyReturnHomeGuardTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!CanReturnHome(Data.Enemy))
	{
		return EStateTreeRunStatus::Failed;
	}
	return Data.Enemy->IsReturnHomeGuarding() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}
