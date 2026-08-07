#include "Enemy/EnemyStateTreeSelfDestructTasks.h"

#include "Components/ActorComponent.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/Controller.h"
#include "Outlier.h"

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

FEnemyBeginPossessedSelfDestructTask::FEnemyBeginPossessedSelfDestructTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyBeginPossessedSelfDestructTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructEnter Owner=%s BoundEnemy=%s ResolvedDrone=%s "
				"Authority=%s Possessed=%s HasRequest=%s Committed=%s"),
			*GetNameSafe(Context.GetOwner()),
			*GetNameSafe(InstanceData.Drone),
			*GetNameSafe(Drone),
			Drone && Drone->HasAuthority() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsEnemyPossessed() ? TEXT("true") : TEXT("false"),
			Drone && Drone->HasPossessedAttackRequest() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsSelfDestructCommitted() ? TEXT("true") : TEXT("false"));
	}
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsEnemyPossessed()
		|| !Drone->HasPossessedAttackRequest()
		|| Drone->IsSelfDestructCommitted())
	{
		return EStateTreeRunStatus::Failed;
	}

	AController* ActiveController = Drone->GetController();
	if (!ActiveController)
	{
		if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructFailed Enemy=%s Reason=NoController"), *GetNameSafe(Drone));
		}
		return EStateTreeRunStatus::Failed;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	ActiveController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const float ChargeSpeed =
		FMath::Max(Drone->GetRuntimeStat().MoveSpeed, 0.0f)
		* FMath::Max(InstanceData.ChargeSpeedMultiplier, 0.0f);
	if (!Drone->BeginCommittedSelfDestructDirection(
		ViewRotation.Vector(),
		InstanceData.TelegraphDuration,
		ChargeSpeed,
		InstanceData.MaxChargeDistance))
	{
		if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructFailed Enemy=%s Reason=CommitRejected"), *GetNameSafe(Drone));
		}
		return EStateTreeRunStatus::Failed;
	}

	// 입력 소비까지 성공해야 같은 입력이 다음 공격 사이클로 중복 전달되지 않는다.
	if (!Drone->ConsumePossessedAttackRequest())
	{
		if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructFailed Enemy=%s Reason=ConsumeRequestFailed"), *GetNameSafe(Drone));
		}
		Drone->CancelCommittedSelfDestruct();
		return EStateTreeRunStatus::Failed;
	}

	Drone->SetAttackPhase(EEnemyAttackPhase::Telegraph);
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructSucceeded Enemy=%s"), *GetNameSafe(Drone));
	}
	return EStateTreeRunStatus::Succeeded;
}

FEnemyUpdatePossessedSelfDestructTask::FEnemyUpdatePossessedSelfDestructTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyUpdatePossessedSelfDestructTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.bCompleted = false;
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] UpdateSelfDestructEnter Enemy=%s Authority=%s Possessed=%s Committed=%s"),
			*GetNameSafe(Drone),
			Drone && Drone->HasAuthority() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsEnemyPossessed() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsSelfDestructCommitted() ? TEXT("true") : TEXT("false"));
	}
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsEnemyPossessed()
		|| !Drone->IsSelfDestructCommitted())
	{
		return EStateTreeRunStatus::Failed;
	}

	Drone->SetAttackPhase(EEnemyAttackPhase::Firing);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyUpdatePossessedSelfDestructTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsEnemyPossessed()
		|| !Drone->IsSelfDestructCommitted())
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

	if (InstanceData.ElapsedTime < FMath::Max(InstanceData.ChargeDuration, 0.0f))
	{
		return EStateTreeRunStatus::Running;
	}

	// 성공 전환 뒤 Detonate Task가 실행될 때까지 Commit을 유지한다.
	InstanceData.bCompleted = true;
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyUpdatePossessedSelfDestructTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!InstanceData.bCompleted && IsValid(Drone) && Drone->HasAuthority())
	{
		Drone->CancelCommittedSelfDestruct();
		Drone->SetAttackPhase(EEnemyAttackPhase::Idle);
	}
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
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] DetonateEnter Enemy=%s Authority=%s Committed=%s"),
			*GetNameSafe(Drone),
			Drone && Drone->HasAuthority() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsSelfDestructCommitted() ? TEXT("true") : TEXT("false"));
	}
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
