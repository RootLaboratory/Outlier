#include "Enemy/EnemyStateTreeTurretTasks.h"

#include "Enemy/EnemyAIController.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
	AActor* ResolveVisibleTarget(AAutoTurret* Turret, AActor* BoundTarget)
	{
		if (!Turret || !Turret->IsPlayerCurrentlyVisible())
		{
			return nullptr;
		}
		const AEnemyAIController* Controller = Cast<AEnemyAIController>(Turret->GetController());
		AActor* PreferredTarget = Controller ? Controller->GetPreferredVisibleTarget() : nullptr;
		if (!IsValid(BoundTarget))
		{
			return PreferredTarget;
		}

		// StateTree에 남아 있는 타겟이 아니라 현재 Perception이 직접 보고 있는 타겟만 유효하다.
		return PreferredTarget == BoundTarget ? BoundTarget : nullptr;
	}
}

FEnemyDeployTurretTask::FEnemyDeployTurretTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyDeployTurretTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret || !Data.Turret->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Data.Turret->IsDeployed())
	{
		return EStateTreeRunStatus::Succeeded;
	}
	return Data.Turret->BeginTurretDeployment()
		? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemyDeployTurretTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret)
	{
		return EStateTreeRunStatus::Failed;
	}
	return Data.Turret->IsDeployed() ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

FEnemyRotateTurretHeadTask::FEnemyRotateTurretHeadTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyRotateTurretHeadTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	return Data.Turret && Data.Turret->HasAuthority()
		? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemyRotateTurretHeadTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bUseTargetActor && !IsValid(Data.TargetActor))
	{
		return EStateTreeRunStatus::Failed;
	}
	const bool bAligned = Data.bUseTargetActor
		? Data.Turret->UpdateTurretAimAtActor(Data.TargetActor, DeltaTime, Data.bAttackRotation)
		: Data.Turret->UpdateTurretAimAtLocation(
			Data.TargetLocation, DeltaTime, Data.bAttackRotation, Data.bUseSearchPitch);
	return bAligned && Data.bFinishWhenAligned
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

FEnemyTurretSearchTask::FEnemyTurretSearchTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyTurretSearchTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret || !Data.Turret->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.ElapsedTime = 0.0f;
	const FVector Center = Data.SearchCenter.IsNearlyZero()
		? Data.Turret->GetCurrentTurretAimLocation() : Data.SearchCenter;
	const FVector Direction = Center - Data.Turret->GetPawnViewLocation();
	Data.CenterRotation = Direction.IsNearlyZero() ? Data.Turret->GetViewRotation() : Direction.Rotation();
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyTurretSearchTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret)
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.ElapsedTime += FMath::Max(DeltaTime, 0.0f);
	const FAutoTurretBehaviorRow& Behavior = Data.Turret->GetTurretBehavior();
	const float HalfCycle = FMath::Max(Behavior.SearchHalfCycleDuration, KINDA_SMALL_NUMBER);
	FRotator SearchRotation = Data.CenterRotation;
	SearchRotation.Yaw += FMath::Sin(Data.ElapsedTime * PI / HalfCycle) * Behavior.SearchYawDegrees;
	const FVector SearchLocation = Data.Turret->GetPawnViewLocation() + SearchRotation.Vector() * 10000.0f;
	Data.Turret->UpdateTurretAimAtLocation(SearchLocation, DeltaTime, false, true);
	return EStateTreeRunStatus::Running;
}

FEnemyTurretBurstFireTask::FEnemyTurretBurstFireTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyTurretBurstFireTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AActor* Target = ResolveVisibleTarget(Data.Turret, Data.TargetActor);
	if (!Data.Turret || !Data.Turret->HasAuthority() || !IsValid(Target)
		|| !IsValid(Data.Turret->GetCurrentWeapon()))
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.TargetActor = Target;
	Data.LastValidAimLocation = Data.Turret->GetCombatAimPoint(Target);
	Data.ElapsedTime = 0.0f;
	Data.Phase = EEnemyTurretBurstTaskPhase::TelegraphAndTracking;
	Data.bAttackStarted = false;
	Data.Turret->SetAttackPhase(EEnemyAttackPhase::Telegraph);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyTurretBurstFireTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AAutoTurret* Turret = Data.Turret;
	ARangedWeaponBase* Weapon = Turret ? Turret->GetCurrentWeapon() : nullptr;
	if (!Turret || !Turret->HasAuthority() || !Weapon)
	{
		return EStateTreeRunStatus::Failed;
	}

	const bool bCanUpdateTrackedAim =
		Data.Phase == EEnemyTurretBurstTaskPhase::TelegraphAndTracking
		|| Data.Phase == EEnemyTurretBurstTaskPhase::BurstFiring;
	if (bCanUpdateTrackedAim
		&& ResolveVisibleTarget(Turret, Data.TargetActor) == Data.TargetActor)
	{
		Data.LastValidAimLocation = Turret->GetCombatAimPoint(Data.TargetActor);
	}
	Turret->UpdateTurretAimAtLocation(Data.LastValidAimLocation, DeltaTime, true);

	if (Data.Phase == EEnemyTurretBurstTaskPhase::TelegraphAndTracking)
	{
		Data.ElapsedTime += DeltaTime;
		if (Data.ElapsedTime < Turret->GetTurretBehavior().TelegraphDuration)
		{
			return EStateTreeRunStatus::Running;
		}
		if (!Weapon->CanAttack())
		{
			return EStateTreeRunStatus::Running;
		}
		// Fire 몽타주와 총구 그룹 모두 첫 그룹에서 같은 사이클을 시작하도록 애니메이션을 먼저 재생한다.
		Turret->PlayFireMontage();
		if (!Turret->StartAttackLocation(Data.LastValidAimLocation))
		{
			Turret->StopFireMontage();
			return EStateTreeRunStatus::Failed;
		}
		Data.Phase = EEnemyTurretBurstTaskPhase::BurstFiring;
		Data.bAttackStarted = true;
		return EStateTreeRunStatus::Running;
	}

	if (Data.Phase == EEnemyTurretBurstTaskPhase::BurstFiring)
	{
		Turret->UpdateAttackLocation(Data.LastValidAimLocation);
		if (Weapon->IsAttacking())
		{
			return EStateTreeRunStatus::Running;
		}
		Data.Phase = EEnemyTurretBurstTaskPhase::Cooling;
		Turret->StopFireMontage();
		Turret->SetAttackPhase(EEnemyAttackPhase::Recover);
	}

	if (Weapon->IsOnPostBurstCooldown())
	{
		return EStateTreeRunStatus::Running;
	}
	Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyTurretBurstFireTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret)
	{
		return;
	}
	ARangedWeaponBase* Weapon = Data.Turret->GetCurrentWeapon();
	if (Data.bAttackStarted && Weapon && Weapon->IsAttacking()
		&& Weapon->GetCurrentBurstShotCount() > 0)
	{
		Weapon->ForcePostBurstCooldown();
	}
	Data.Turret->StopCurrentAttack();
	Data.Turret->StopFireMontage();
}

FEnemyTurretSniperShotTask::FEnemyTurretSniperShotTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyTurretSniperShotTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AActor* Target = ResolveVisibleTarget(Data.Turret, Data.TargetActor);
	if (!Data.Turret || !Data.Turret->HasAuthority() || !IsValid(Target)
		|| !IsValid(Data.Turret->GetCurrentWeapon()))
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.TargetActor = Target;
	Data.LockedAimLocation = Data.Turret->GetCombatAimPoint(Target);
	Data.ElapsedTime = 0.0f;
	Data.Phase = EEnemyTurretSniperTaskPhase::LaserTelegraphAndTracking;
	Data.bAttackStarted = false;
	Data.Turret->SetAttackPhase(EEnemyAttackPhase::Telegraph);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyTurretSniperShotTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AAutoTurret* Turret = Data.Turret;
	ARangedWeaponBase* Weapon = Turret ? Turret->GetCurrentWeapon() : nullptr;
	if (!Turret || !Turret->HasAuthority() || !Weapon)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.Phase == EEnemyTurretSniperTaskPhase::LaserTelegraphAndTracking)
	{
		if (ResolveVisibleTarget(Turret, Data.TargetActor) == Data.TargetActor)
		{
			Data.LockedAimLocation = Turret->GetCombatAimPoint(Data.TargetActor);
		}
		Turret->UpdateTurretAimAtLocation(Data.LockedAimLocation, DeltaTime, false);
		Data.ElapsedTime += DeltaTime;
		if (Data.ElapsedTime < Turret->GetTurretBehavior().TelegraphDuration)
		{
			return EStateTreeRunStatus::Running;
		}
		if (!Weapon->CanAttack())
		{
			return EStateTreeRunStatus::Running;
		}
		if (!Turret->StartAttackLocation(Data.LockedAimLocation))
		{
			return EStateTreeRunStatus::Failed;
		}
		Turret->PlayFireMontage();
		Data.Phase = EEnemyTurretSniperTaskPhase::BeamPresentation;
		Data.ElapsedTime = 0.0f;
		Data.bAttackStarted = true;
	}

	if (Data.Phase == EEnemyTurretSniperTaskPhase::BeamPresentation)
	{
		Data.ElapsedTime += DeltaTime;
		if (Data.ElapsedTime < Turret->GetTurretBehavior().BeamPresentationDuration)
		{
			return EStateTreeRunStatus::Running;
		}
		Data.Phase = EEnemyTurretSniperTaskPhase::Cooling;
		Turret->StopFireMontage();
		Turret->SetAttackPhase(EEnemyAttackPhase::Recover);
	}

	if (Weapon->IsOnPostBurstCooldown())
	{
		return EStateTreeRunStatus::Running;
	}
	Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyTurretSniperShotTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (Data.Turret)
	{
		Data.Turret->StopCurrentAttack();
		Data.Turret->StopFireMontage();
	}
}

FEnemyRecoverTurretImpactOffsetTask::FEnemyRecoverTurretImpactOffsetTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyRecoverTurretImpactOffsetTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret || !Data.Turret->HasAuthority()
		|| Data.Turret->ImpactReactionMode != EAutoTurretImpactReactionMode::StateTreeInterrupt)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.Turret->StopImpactRecovery();
	return Data.Turret->ImpactRotationOffset.IsNearlyZero(0.01f)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyRecoverTurretImpactOffsetTask::Tick(
	FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret || !Data.Turret->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	return Data.Turret->UpdateTurretImpactRecovery(FMath::Max(DeltaTime, 0.0f))
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

void FEnemyRecoverTurretImpactOffsetTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (IsValid(Data.Turret) && Data.Turret->HasAuthority())
	{
		// Death나 Stun으로 중단돼도 머리 Offset이 남아 있지 않도록 종료 시 정리한다.
		Data.Turret->StopImpactRecovery();
		Data.Turret->ResetTurretImpactRecovery();
	}
}
