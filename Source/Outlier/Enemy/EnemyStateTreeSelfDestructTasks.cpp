#include "Enemy/EnemyStateTreeSelfDestructTasks.h"

#include "Components/ActorComponent.h"
#include "Enemy/EnemyAIController.h"

namespace
{
	ASelfDestructDrone* ResolveSelfDestructDrone(
		FStateTreeExecutionContext& Context,
		AEnemyBase* BoundEnemy)
	{
		if (ASelfDestructDrone* BoundDrone = Cast<ASelfDestructDrone>(BoundEnemy))
		{
			return BoundDrone;
		}

		if (ASelfDestructDrone* OwnerDrone = Cast<ASelfDestructDrone>(Context.GetOwner()))
		{
			return OwnerDrone;
		}

		if (const UActorComponent* OwnerComponent = Cast<UActorComponent>(Context.GetOwner()))
		{
			return Cast<ASelfDestructDrone>(OwnerComponent->GetOwner());
		}

		return nullptr;
	}
}

FEnemyChargeLastDirectionTask::FEnemyChargeLastDirectionTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyChargeLastDirectionTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.bCompleted = false;
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!Drone || !Drone->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* Target = InstanceData.TargetActor.Get();
	if (!IsValid(Target))
	{
		if (const AEnemyAIController* AIController =
			Cast<AEnemyAIController>(Drone->GetController()))
		{
			Target = AIController->GetPreferredVisibleTarget();
		}
	}

	const float ChargeSpeed =
		FMath::Max(Drone->GetRuntimeStat().MoveSpeed, 0.0f)
		* FMath::Max(InstanceData.ChargeSpeedMultiplier, 0.0f);
	return Drone->BeginCommittedSelfDestruct(
		Target,
		InstanceData.TelegraphDuration,
		ChargeSpeed,
		InstanceData.MaxChargeDistance,
		InstanceData.TargetStopDistance)
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemyChargeLastDirectionTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!Drone || !Drone->HasAuthority() || !Drone->IsSelfDestructCommitted())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += FMath::Max(DeltaTime, 0.0f);
	const float ChargeSpeed =
		FMath::Max(Drone->GetRuntimeStat().MoveSpeed, 0.0f)
		* FMath::Max(InstanceData.ChargeSpeedMultiplier, 0.0f);
	if (!Drone->UpdateCommittedSelfDestructMovement(DeltaTime, ChargeSpeed))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.ElapsedTime < FMath::Max(InstanceData.TelegraphDuration, 0.0f))
	{
		return EStateTreeRunStatus::Running;
	}

	// 정상 완료 시 다음 Detonate Task까지 Commit을 유지한다.
	InstanceData.bCompleted = true;
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyChargeLastDirectionTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!InstanceData.bCompleted
		&& IsValid(Drone)
		&& Drone->HasAuthority())
	{
		// Stun, Possession, Death 또는 실패 전환은 살아 있는 드론의 전조를 취소한다.
		Drone->CancelCommittedSelfDestruct();
	}
}

FEnemyBombDetonateTask::FEnemyBombDetonateTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyBombDetonateTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsSelfDestructCommitted())
	{
		if (IsValid(Drone) && Drone->HasAuthority())
		{
			Drone->CancelCommittedSelfDestruct();
		}
		return EStateTreeRunStatus::Failed;
	}

	Drone->TriggerSelfDestruct();
	return EStateTreeRunStatus::Succeeded;
}
