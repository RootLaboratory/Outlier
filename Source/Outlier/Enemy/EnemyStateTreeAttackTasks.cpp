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
	const AEnemyBase* Enemy = InstanceData.Enemy;
	AActor* TrackedTarget = Enemy ? Enemy->GetTrackedTarget() : nullptr;
	// 오래된 바인딩이 다른 대상을 가리키면 현재 추적 대상을 우회하지 않는다.
	return !InstanceData.TargetActor || InstanceData.TargetActor == TrackedTarget ? TrackedTarget : nullptr;
}

bool ShouldWaitForCombatDecision(
	const FEnemyAttackTargetTaskInstanceData& InstanceData)
{
	return InstanceData.Enemy
		&& InstanceData.Enemy->HasAuthority()
		&& InstanceData.Enemy->GetCombatState() == EEnemyCombatState::Combat
		&& InstanceData.bRequireVisibleTarget
		&& !InstanceData.Enemy->HasTrackableTarget();
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

	if (!InstanceData.Enemy->CanAttackTrackedTarget(TargetActor)
		|| !InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
	{
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->StartAttackTarget(TargetActor))
	{
		return EStateTreeRunStatus::Running;
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

	// 이동 완료와 무관하게 사거리/사선을 검사한다. 차단 중에는 버스트 진행과 전조를 초기화하지 않는다.
	if (!InstanceData.Enemy->CanAttackTrackedTarget(TargetActor))
	{
		if (InstanceData.bAttackStarted) InstanceData.Enemy->PauseTargetAttack();
		InstanceData.bAttackStarted = false;
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.bAttackStarted)
	{
		if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
		{
			return EStateTreeRunStatus::Running;
		}

		if (!InstanceData.Enemy->StartAttackTarget(TargetActor))
		{
			return EStateTreeRunStatus::Running;
		}

		InstanceData.bAttackStarted = true;
		return EStateTreeRunStatus::Running;
	}

	// 공격 시작과 같은 조준점을 매 Tick 조회해 추적 중에도 캡슐 중심을 유지한다.
	const FVector TargetLocation = InstanceData.Enemy->GetCombatAimPoint(TargetActor);
	if (!InstanceData.Enemy->UpdateAttackLocation(TargetLocation))
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

	// 감지 출력만 바뀐 AI 재선택은 진행 중인 전조/회복을 이어간다. 빙의 입력 소비는 기존 흐름을 유지한다.
	if (!InstanceData.Enemy->IsEnemyPossessed() && Transition.ChangeType == EStateTreeStateChangeType::Sustained)
		return EStateTreeRunStatus::Running;

	if (InstanceData.bConsumePossessedAttackRequest
		&& !InstanceData.Enemy->ConsumePossessedAttackRequest())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime = 0.0f;
	InstanceData.SelectedDuration = SelectDuration(
		InstanceData.MinDuration,
		InstanceData.MaxDuration);
	EEnemyAttackPhase PresentationPhase = InstanceData.Phase;
	if (!InstanceData.Enemy->IsEnemyPossessed())
	{
		if (InstanceData.Phase == EEnemyAttackPhase::Telegraph)
		{
			// 수색 중 같은 대상을 다시 찾으면 새 전조 대신 남아 있던 회복 시간만 기다린다.
			if (InstanceData.Enemy->ConsumeTargetAttackResume())
			{
				InstanceData.SelectedDuration = InstanceData.Enemy->GetTargetAttackRecoveryRemaining();
				PresentationPhase = EEnemyAttackPhase::Recover;
				if (InstanceData.SelectedDuration <= 0.0f) return EStateTreeRunStatus::Succeeded;
			}
			InstanceData.Enemy->RememberTargetAttackPreparation();
		}
		else if (InstanceData.Phase == EEnemyAttackPhase::Recover)
			InstanceData.Enemy->RememberTargetAttackRecovery(InstanceData.SelectedDuration);
	}
	// 서버가 단계만 확정하고 복제하며, 실제 전조·회복 연출은 Enemy BP가 선택한다.
	InstanceData.Enemy->SetAttackPhase(PresentationPhase);
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
	if (InstanceData.Enemy && !InstanceData.Enemy->IsEnemyPossessed()
		&& Transition.ChangeType == EStateTreeStateChangeType::Sustained) return;
	if (InstanceData.Enemy
		&& InstanceData.Phase == EEnemyAttackPhase::Recover
		&& InstanceData.Enemy->GetAttackPhase() == EEnemyAttackPhase::Recover
		&& !InstanceData.Enemy->HasPossessedAttackRequest())
	{
		InstanceData.Enemy->SetAttackPhase(EEnemyAttackPhase::Idle);
	}
}
