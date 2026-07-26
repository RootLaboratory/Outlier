#include "Enemy/EnemyStateTreeAttackTasks.h"

#include "Enemy/EnemyAIController.h"
#include "Weapon/RangedWeaponBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyBattleTask, Log, All);

namespace
{
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
	InstanceData.bAttackStarted = false;
	InstanceData.SelectedAttackDuration = SelectDuration(
		InstanceData.MinAttackDuration,
		InstanceData.MaxAttackDuration);
	AActor* TargetActor = ResolveAttackTarget(InstanceData);
	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !IsValid(TargetActor)
		|| (InstanceData.bRequireVisibleTarget
			&& !InstanceData.Enemy->IsPlayerCurrentlyVisible())
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon()))
	{
		UE_LOG(
			LogEnemyBattleTask,
			Warning,
			TEXT("[Battle][AttackTarget] Enter failed. Enemy=%s Authority=%d Target=%s Visible=%d RequireVisible=%d"),
			*GetNameSafe(InstanceData.Enemy),
			InstanceData.Enemy && InstanceData.Enemy->HasAuthority(),
			*GetNameSafe(TargetActor),
			InstanceData.Enemy && InstanceData.Enemy->IsPlayerCurrentlyVisible(),
			InstanceData.bRequireVisibleTarget);
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
	{
		UE_LOG(
			LogEnemyBattleTask,
			Verbose,
			TEXT("[Battle][AttackTarget] Waiting for weapon. Enemy=%s Target=%s"),
			*GetNameSafe(InstanceData.Enemy),
			*GetNameSafe(TargetActor));
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->StartAttackTarget(TargetActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bAttackStarted = true;
	UE_LOG(
		LogEnemyBattleTask,
		Display,
		TEXT("[Battle][AttackTarget] Enter. Enemy=%s Target=%s Duration=%.2f"),
		*GetNameSafe(InstanceData.Enemy),
		*GetNameSafe(TargetActor),
		InstanceData.SelectedAttackDuration);
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
		|| !IsValid(TargetActor)
		|| (InstanceData.bRequireVisibleTarget
			&& !InstanceData.Enemy->IsPlayerCurrentlyVisible())
		|| !IsValid(InstanceData.Enemy->GetCurrentWeapon()))
	{
		UE_LOG(
			LogEnemyBattleTask,
			Warning,
			TEXT("[Battle][AttackTarget] Tick failed. Enemy=%s Target=%s Visible=%d"),
			*GetNameSafe(InstanceData.Enemy),
			*GetNameSafe(TargetActor),
			InstanceData.Enemy && InstanceData.Enemy->IsPlayerCurrentlyVisible());
		return EStateTreeRunStatus::Failed;
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
		UE_LOG(
			LogEnemyBattleTask,
			Display,
			TEXT("[Battle][AttackTarget] Started after wait. Enemy=%s Target=%s Duration=%.2f"),
			*GetNameSafe(InstanceData.Enemy),
			*GetNameSafe(TargetActor),
			InstanceData.SelectedAttackDuration);
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->UpdateAttackLocation(TargetActor->GetActorLocation()))
	{
		UE_LOG(
			LogEnemyBattleTask,
			Warning,
			TEXT("[Battle][AttackTarget] Tick failed. Enemy=%s Target=%s Visible=%d"),
			*GetNameSafe(InstanceData.Enemy),
			*GetNameSafe(TargetActor),
			InstanceData.Enemy->IsPlayerCurrentlyVisible());
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;
	if (InstanceData.ElapsedTime >= InstanceData.SelectedAttackDuration)
	{
		UE_LOG(
			LogEnemyBattleTask,
			Display,
			TEXT("[Battle][AttackTarget] Completed. Enemy=%s Target=%s Elapsed=%.2f"),
			*GetNameSafe(InstanceData.Enemy),
			*GetNameSafe(TargetActor),
			InstanceData.ElapsedTime);
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
		InstanceData.Enemy->StopCurrentAttack();
		UE_LOG(
			LogEnemyBattleTask,
			Display,
			TEXT("[Battle][AttackTarget] Exit. Enemy=%s"),
			*GetNameSafe(InstanceData.Enemy));
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
		UE_LOG(
			LogEnemyBattleTask,
			Warning,
			TEXT("[Battle][AttackLocation] Enter failed. Enemy=%s Authority=%d Location=%s"),
			*GetNameSafe(InstanceData.Enemy),
			InstanceData.Enemy && InstanceData.Enemy->HasAuthority(),
			*InstanceData.TargetLocation.ToCompactString());
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
	{
		UE_LOG(
			LogEnemyBattleTask,
			Verbose,
			TEXT("[Battle][AttackLocation] Waiting for weapon. Enemy=%s Location=%s"),
			*GetNameSafe(InstanceData.Enemy),
			*InstanceData.TargetLocation.ToCompactString());
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->StartAttackLocation(InstanceData.TargetLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bAttackStarted = true;
	UE_LOG(
		LogEnemyBattleTask,
		Display,
		TEXT("[Battle][AttackLocation] Enter. Enemy=%s Location=%s Duration=%.2f"),
		*GetNameSafe(InstanceData.Enemy),
		*InstanceData.TargetLocation.ToCompactString(),
		InstanceData.SelectedAttackDuration);
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
		UE_LOG(
			LogEnemyBattleTask,
			Warning,
			TEXT("[Battle][AttackLocation] Tick failed. Enemy=%s Location=%s"),
			*GetNameSafe(InstanceData.Enemy),
			*InstanceData.TargetLocation.ToCompactString());
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
		UE_LOG(
			LogEnemyBattleTask,
			Display,
			TEXT("[Battle][AttackLocation] Started after wait. Enemy=%s Location=%s Duration=%.2f"),
			*GetNameSafe(InstanceData.Enemy),
			*InstanceData.TargetLocation.ToCompactString(),
			InstanceData.SelectedAttackDuration);
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Enemy->UpdateAttackLocation(InstanceData.TargetLocation))
	{
		UE_LOG(
			LogEnemyBattleTask,
			Warning,
			TEXT("[Battle][AttackLocation] Tick failed. Enemy=%s Location=%s"),
			*GetNameSafe(InstanceData.Enemy),
			*InstanceData.TargetLocation.ToCompactString());
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;
	if (InstanceData.ElapsedTime >= InstanceData.SelectedAttackDuration)
	{
		UE_LOG(
			LogEnemyBattleTask,
			Display,
			TEXT("[Battle][AttackLocation] Completed. Enemy=%s Location=%s Elapsed=%.2f"),
			*GetNameSafe(InstanceData.Enemy),
			*InstanceData.TargetLocation.ToCompactString(),
			InstanceData.ElapsedTime);
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
		InstanceData.Enemy->StopCurrentAttack();
		UE_LOG(
			LogEnemyBattleTask,
			Display,
			TEXT("[Battle][AttackLocation] Exit. Enemy=%s"),
			*GetNameSafe(InstanceData.Enemy));
	}
}

FEnemyAttackPhaseWaitTask::FEnemyAttackPhaseWaitTask()
{
	bShouldStateChangeOnReselect = false;
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

	InstanceData.ElapsedTime = 0.0f;
	InstanceData.SelectedDuration = SelectDuration(
		InstanceData.MinDuration,
		InstanceData.MaxDuration);
	// 서버가 단계만 확정하고 복제하며, 실제 전조·회복 연출은 Enemy BP가 선택한다.
	InstanceData.Enemy->SetAttackPhase(InstanceData.Phase);
	UE_LOG(
		LogEnemyBattleTask,
		Display,
		TEXT("[Battle][AttackPhaseWait] Enter. Enemy=%s Phase=%s Duration=%.2f"),
		*GetNameSafe(InstanceData.Enemy),
		*UEnum::GetValueAsString(InstanceData.Phase),
		InstanceData.SelectedDuration);
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
		UE_LOG(
			LogEnemyBattleTask,
			Display,
			TEXT("[Battle][AttackPhaseWait] Completed. Enemy=%s Phase=%s Elapsed=%.2f"),
			*GetNameSafe(InstanceData.Enemy),
			*UEnum::GetValueAsString(InstanceData.Phase),
			InstanceData.ElapsedTime);
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
		&& InstanceData.Enemy->GetAttackPhase() == InstanceData.Phase)
	{
		InstanceData.Enemy->SetAttackPhase(EEnemyAttackPhase::Idle);
	}
}
