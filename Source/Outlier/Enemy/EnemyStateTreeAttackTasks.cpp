#include "Enemy/EnemyStateTreeAttackTasks.h"

#include "Enemy/EnemyAIController.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
constexpr float CombatDecisionRefreshInterval = 0.25f;

float SelectDuration(float MinDuration, float MaxDuration)
{
	// 발사 횟수는 Weapon의 연사 간격이 결정하고, Task는 자연스러운 버스트 지속시간만 선택한다.
	const float SafeMin = FMath::Max(MinDuration, 0.0f);
	const float SafeMax = FMath::Max(MaxDuration, SafeMin);
	return FMath::FRandRange(SafeMin, SafeMax);
}

AActor* ResolveAttackTarget(const FEnemyAttackTargetTaskInstanceData& InstanceData)
{
	// StateTree에서 명시적으로 바인딩한 Actor가 있으면 그 대상을 우선한다.
	if (IsValid(InstanceData.TargetActor))
	{
		return InstanceData.TargetActor;
	}

	// 바인딩이 없으면 현재 Sight 결과를 직접 조회해 Global Task의 이벤트 갱신 시점에 의존하지 않는다.
	const AEnemyBase* Enemy = InstanceData.Enemy;
	const AEnemyAIController* EnemyController = Enemy
		? Cast<AEnemyAIController>(Enemy->GetController())
		: nullptr;
	return EnemyController ? EnemyController->GetPreferredVisibleTarget() : nullptr;
}

bool ShouldWaitForCombatDecision(
	const FEnemyAttackTargetTaskInstanceData& InstanceData)
{
	return InstanceData.Enemy
		&& InstanceData.Enemy->HasAuthority()
		&& InstanceData.Enemy->GetCombatState() == EEnemyCombatState::Combat
		&& InstanceData.bRequireVisibleTarget
		&& !InstanceData.Enemy->IsPlayerCurrentlyVisible();
}

void BeginCombatDecisionWait(FEnemyAttackTargetTaskInstanceData& InstanceData)
{
	if (InstanceData.bAttackStarted)
	{
		InstanceData.Enemy->StopCurrentAttack();
		InstanceData.bAttackStarted = false;
	}

	InstanceData.bWaitingForCombatDecision = true;
	InstanceData.CombatDecisionRefreshElapsed = 0.0f;
	InstanceData.Enemy->RequestCombatDecisionRefresh();
}
}

FEnemyAttackTargetTask::FEnemyAttackTargetTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyAttackTargetTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.CombatDecisionRefreshElapsed = 0.0f;
	InstanceData.bAttackStarted = false;
	InstanceData.bWaitingForCombatDecision = false;
	InstanceData.SelectedAttackDuration = SelectDuration(
		InstanceData.MinAttackDuration,
		InstanceData.MaxAttackDuration);
	AActor* TargetActor = ResolveAttackTarget(InstanceData);
	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon()))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (ShouldWaitForCombatDecision(InstanceData))
	{
		BeginCombatDecisionWait(InstanceData);
		return EStateTreeRunStatus::Running;
	}

	if (!IsValid(TargetActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
	{
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->StartAttackTarget(TargetActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bAttackStarted = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyAttackTargetTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AActor* TargetActor = ResolveAttackTarget(InstanceData);
	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon()))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (ShouldWaitForCombatDecision(InstanceData))
	{
		if (!InstanceData.bWaitingForCombatDecision)
		{
			BeginCombatDecisionWait(InstanceData);
		}
		else
		{
			InstanceData.CombatDecisionRefreshElapsed += DeltaTime;
			if (InstanceData.CombatDecisionRefreshElapsed
				>= CombatDecisionRefreshInterval)
			{
				InstanceData.CombatDecisionRefreshElapsed = 0.0f;
				InstanceData.Enemy->RequestCombatDecisionRefresh();
			}
		}

		return EStateTreeRunStatus::Running;
	}

	if (!IsValid(TargetActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.bWaitingForCombatDecision)
	{
		InstanceData.bWaitingForCombatDecision = false;
		InstanceData.CombatDecisionRefreshElapsed = 0.0f;
	}

	if (!InstanceData.bAttackStarted)
	{
		if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
		{
			return EStateTreeRunStatus::Running;
		}

		if (!InstanceData.Enemy->StartAttackTarget(TargetActor))
		{
			return EStateTreeRunStatus::Failed;
		}

		InstanceData.bAttackStarted = true;
		InstanceData.ElapsedTime = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->UpdateAttackLocation(TargetActor->GetActorLocation()))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;
	if (InstanceData.ElapsedTime >= InstanceData.SelectedAttackDuration)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FEnemyAttackTargetTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.Enemy && InstanceData.bAttackStarted)
	{
		InstanceData.Enemy->StopCurrentWeaponAttack();
	}
}

FEnemyAttackLocationTask::FEnemyAttackLocationTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyAttackLocationTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.bAttackStarted = false;
	InstanceData.SelectedAttackDuration = SelectDuration(
		InstanceData.MinAttackDuration,
		InstanceData.MaxAttackDuration);
	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon()))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
	{
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->StartAttackLocation(InstanceData.TargetLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bAttackStarted = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyAttackLocationTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon()))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.bAttackStarted)
	{
		if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
		{
			return EStateTreeRunStatus::Running;
		}

		if (!InstanceData.Enemy->StartAttackLocation(InstanceData.TargetLocation))
		{
			return EStateTreeRunStatus::Failed;
		}

		InstanceData.bAttackStarted = true;
		InstanceData.ElapsedTime = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->UpdateAttackLocation(InstanceData.TargetLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;
	if (InstanceData.ElapsedTime >= InstanceData.SelectedAttackDuration)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FEnemyAttackLocationTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.Enemy && InstanceData.bAttackStarted)
	{
		InstanceData.Enemy->StopCurrentWeaponAttack();
	}
}

FEnemyPossessedBurstAttackTask::FEnemyPossessedBurstAttackTask()
{
	bShouldStateChangeOnReselect = true;
}

EStateTreeRunStatus FEnemyPossessedBurstAttackTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bAttackStarted = false;

	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !InstanceData.Enemy->IsEnemyPossessed()
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon())
		|| !InstanceData.Enemy->GetCurrentWeapon()->HasFixedBurst())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
	{
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->StartPossessedAttackBurst())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bAttackStarted = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyPossessedBurstAttackTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	(void)DeltaTime;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !InstanceData.Enemy->IsEnemyPossessed()
		|| InstanceData.Enemy->GetCombatState() == EEnemyCombatState::Stun
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon())
		|| !InstanceData.Enemy->GetCurrentWeapon()->HasFixedBurst())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.bAttackStarted)
	{
		if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
		{
			return EStateTreeRunStatus::Running;
		}

		if (!InstanceData.Enemy->StartPossessedAttackBurst())
		{
			return EStateTreeRunStatus::Failed;
		}

		InstanceData.bAttackStarted = true;
		return EStateTreeRunStatus::Running;
	}

	return InstanceData.Enemy->GetCurrentWeapon()->IsAttacking()
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Succeeded;
}

void FEnemyPossessedBurstAttackTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.Enemy
		&& InstanceData.bAttackStarted
		&& IsValid(InstanceData.Enemy->GetCurrentWeapon())
		&& InstanceData.Enemy->GetCurrentWeapon()->IsAttacking())
	{
		InstanceData.Enemy->StopCurrentWeaponAttack();
	}
}

FEnemyAttackPhaseWaitTask::FEnemyAttackPhaseWaitTask()
{
	bShouldStateChangeOnReselect = true;
}

EStateTreeRunStatus FEnemyAttackPhaseWaitTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.bConsumePossessedAttackRequest
		&& !InstanceData.Enemy->ConsumePossessedAttackRequest())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime = 0.0f;
	InstanceData.SelectedDuration = SelectDuration(
		InstanceData.MinDuration,
		InstanceData.MaxDuration);
	// 서버가 단계만 확정하고 복제하며, 실제 전조·회복 연출은 Enemy BP가 선택한다.
	InstanceData.Enemy->SetAttackPhase(InstanceData.Phase);
	return InstanceData.SelectedDuration <= 0.0f
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyAttackPhaseWaitTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;
	if (InstanceData.ElapsedTime >= InstanceData.SelectedDuration)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FEnemyAttackPhaseWaitTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.Enemy
		&& InstanceData.Phase == EEnemyAttackPhase::Recover
		&& InstanceData.Enemy->GetAttackPhase() == EEnemyAttackPhase::Recover
		&& !InstanceData.Enemy->HasPossessedAttackRequest())
	{
		InstanceData.Enemy->SetAttackPhase(EEnemyAttackPhase::Idle);
	}
}
