#include "Enemy/EnemyStateTreeTurretTasks.h"

#include "Enemy/EnemyAIController.h"
#include "Outlier.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/LaserWeapon.h"

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
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[TurretDiag][DeployTask] EnterFailed Turret=%s Reason=InvalidTurretOrAuthority"), *GetNameSafe(Data.Turret));
		}
		return EStateTreeRunStatus::Failed;
	}
	if (Data.Turret->IsDeployed())
	{
		return EStateTreeRunStatus::Succeeded;
	}
	// 타입은 기존 StateTree 에셋 호환을 위해 유지한다. 전개 시작은 Room Wave만 소유하며,
	// StateTree는 전개 완료 뒤 시작되므로 여기서 미전개 상태라면 잘못된 진입이다.
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[TurretDiag][DeployTask] EnterFailed Turret=%s Reason=RoomWaveDeploymentIncomplete"),
			*GetNameSafe(Data.Turret));
	}
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemyDeployTurretTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Turret)
	{
		return EStateTreeRunStatus::Failed;
	}
	return Data.Turret->IsDeployed()
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Failed;
}

FEnemyRotateTurretHeadTask::FEnemyRotateTurretHeadTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyRotateTurretHeadTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const bool bValid = Data.Turret && Data.Turret->HasAuthority();
	return bValid ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
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
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[TurretDiag][RotateTask] Failed Turret=%s Reason=InvalidTarget"), *GetNameSafe(Data.Turret));
		}
		return EStateTreeRunStatus::Failed;
	}
	const bool bAligned = Data.bUseTargetActor
		? Data.Turret->UpdateTurretAimAtActor(Data.TargetActor, DeltaTime, Data.bAttackRotation)
		: Data.Turret->UpdateTurretAimAtLocation(
			Data.TargetLocation, DeltaTime, Data.bAttackRotation, Data.bUseSearchPitch);
	if (bAligned && Data.bFinishWhenAligned)
	{
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Running;
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
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[TurretDiag][Rifle] EnterFailed Turret=%s Authority=%s Visible=%s BoundTarget=%s ResolvedTarget=%s Weapon=%s"),
				*GetNameSafe(Data.Turret), Data.Turret && Data.Turret->HasAuthority() ? TEXT("true") : TEXT("false"),
				Data.Turret && Data.Turret->IsPlayerCurrentlyVisible() ? TEXT("true") : TEXT("false"),
				*GetNameSafe(Data.TargetActor), *GetNameSafe(Target),
				*GetNameSafe(Data.Turret ? Data.Turret->GetCurrentWeapon() : nullptr));
		}
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
	if (!IsValid(Data.Turret) || !Data.Turret->HasAuthority() || !IsValid(Target)
		|| Data.Turret->IsDead() || Data.Turret->GetCombatState() == EEnemyCombatState::Stun
		|| Data.Turret->IsAIControlSuppressed()
		|| !IsValid(Data.Turret->GetCurrentWeapon()))
	{
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[TurretDiag][Laser] EnterFailed Turret=%s Authority=%s Visible=%s BoundTarget=%s ResolvedTarget=%s Weapon=%s"),
				*GetNameSafe(Data.Turret), Data.Turret && Data.Turret->HasAuthority() ? TEXT("true") : TEXT("false"),
				Data.Turret && Data.Turret->IsPlayerCurrentlyVisible() ? TEXT("true") : TEXT("false"),
				*GetNameSafe(Data.TargetActor), *GetNameSafe(Target),
				*GetNameSafe(Data.Turret ? Data.Turret->GetCurrentWeapon() : nullptr));
		}
		return EStateTreeRunStatus::Failed;
	}
	Data.TargetActor = Target;
	Data.LockedAimLocation = Data.Turret->GetCombatAimPoint(Target);
	Data.ElapsedTime = 0.0f;
	Data.Phase = EEnemyTurretSniperTaskPhase::LaserTelegraphAndTracking;
	Data.bAttackStarted = false;
	Data.bLoggedAttackBlocked = false;
	Data.ActiveWeapon = Data.Turret->GetCurrentWeapon();
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		const FAutoTurretBehaviorRow& Behavior = Data.Turret->GetTurretBehavior();
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][Laser] ChargeEnter Turret=%s Weapon=%s Class=%s Laser=%d Target=%s Aim=%s Charge=%.3f Trail=%.3f Recovery=%.3f"),
			*GetNameSafe(Data.Turret), *GetNameSafe(Data.ActiveWeapon), *GetNameSafe(Data.ActiveWeapon->GetClass()),
			Cast<ALaserWeapon>(Data.ActiveWeapon) != nullptr, *GetNameSafe(Target), *Data.LockedAimLocation.ToCompactString(),
			Behavior.TelegraphDuration, Behavior.BeamPresentationDuration, Behavior.RecoveryDuration);
	}
	if (ALaserWeapon* Laser = Cast<ALaserWeapon>(Data.ActiveWeapon))
	{
		const FAutoTurretBehaviorRow& Behavior = Data.Turret->GetTurretBehavior();
		Laser->BeginCharge(Behavior.TelegraphDuration, Behavior.BeamPresentationDuration);
	}
	Data.Turret->SetAttackPhase(EEnemyAttackPhase::Telegraph);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyTurretSniperShotTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AAutoTurret* Turret = Data.Turret;
	ARangedWeaponBase* Weapon = Turret ? Turret->GetCurrentWeapon() : nullptr;
	if (!IsValid(Turret) || !Turret->HasAuthority() || !IsValid(Weapon)
		|| Weapon != Data.ActiveWeapon || Turret->IsDead()
		|| Turret->GetCombatState() == EEnemyCombatState::Stun || Turret->IsAIControlSuppressed())
	{
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[TurretDiag][Laser] Abort Turret=%s Weapon=%s ActiveWeapon=%s Phase=%d Elapsed=%.3f Authority=%d Dead=%d Stun=%d Suppressed=%d"),
				*GetNameSafe(Turret), *GetNameSafe(Weapon), *GetNameSafe(Data.ActiveWeapon), static_cast<int32>(Data.Phase), Data.ElapsedTime,
				IsValid(Turret) && Turret->HasAuthority(), IsValid(Turret) && Turret->IsDead(),
				IsValid(Turret) && Turret->GetCombatState() == EEnemyCombatState::Stun, IsValid(Turret) && Turret->IsAIControlSuppressed());
		}
		if (IsValid(Data.ActiveWeapon))
		{
			Data.ActiveWeapon->StopAttack();
		}
		return EStateTreeRunStatus::Failed;
	}
	ALaserWeapon* Laser = Cast<ALaserWeapon>(Weapon);

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
			if (!Data.bLoggedAttackBlocked && AAutoTurret::IsTurretDiagnosticsEnabled())
			{
				UE_LOG(LogOutlier, Warning, TEXT("[TurretDiag][Laser] WaitingCanAttack Turret=%s Weapon=%s Elapsed=%.3f"), *GetNameSafe(Turret), *GetNameSafe(Weapon), Data.ElapsedTime);
			}
			Data.bLoggedAttackBlocked = true;
			return EStateTreeRunStatus::Running;
		}
		if (!Turret->StartAttackLocation(Data.LockedAimLocation))
		{
			if (AAutoTurret::IsTurretDiagnosticsEnabled())
			{
				UE_LOG(LogOutlier, Warning, TEXT("[TurretDiag][Laser] FireRejected Turret=%s Weapon=%s Aim=%s"), *GetNameSafe(Turret), *GetNameSafe(Weapon), *Data.LockedAimLocation.ToCompactString());
			}
			return EStateTreeRunStatus::Failed;
		}
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][Laser] FireRequested Turret=%s Weapon=%s Target=%s Visible=%d Aim=%s"),
				*GetNameSafe(Turret), *GetNameSafe(Weapon), *GetNameSafe(Data.TargetActor), Turret->IsPlayerCurrentlyVisible(), *Data.LockedAimLocation.ToCompactString());
		}
		Turret->PlayFireMontage();
		Data.Phase = EEnemyTurretSniperTaskPhase::BeamPresentation;
		Data.ElapsedTime = 0.0f;
		Data.bAttackStarted = true;
		// 전조 Tick의 시간을 새 빔 표시 단계에 다시 더하지 않는다.
		return EStateTreeRunStatus::Running;
	}

	if (Data.Phase == EEnemyTurretSniperTaskPhase::BeamPresentation)
	{
		Data.ElapsedTime += DeltaTime;
		if (Data.ElapsedTime < Turret->GetTurretBehavior().BeamPresentationDuration)
		{
			return EStateTreeRunStatus::Running;
		}
		Data.Phase = EEnemyTurretSniperTaskPhase::Cooling;
		Data.ElapsedTime = 0.0f;
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][Laser] RecoveryEnter Turret=%s Weapon=%s Duration=%.3f"), *GetNameSafe(Turret), *GetNameSafe(Weapon), Turret->GetTurretBehavior().RecoveryDuration);
		}
		if (Laser)
		{
			Laser->EndLaserPresentation();
		}
		Turret->StopFireMontage();
		Turret->SetAttackPhase(EEnemyAttackPhase::Recover);
		return EStateTreeRunStatus::Running;
	}

	Data.ElapsedTime += DeltaTime;
	if (Laser ? Data.ElapsedTime < FMath::Max(Turret->GetTurretBehavior().RecoveryDuration, 0.0f)
		: Weapon->IsOnPostBurstCooldown())
	{
		return EStateTreeRunStatus::Running;
	}
	Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][Laser] CycleComplete Turret=%s Weapon=%s"), *GetNameSafe(Turret), *GetNameSafe(Weapon));
	}
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyTurretSniperShotTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][Laser] TaskExit Turret=%s Weapon=%s Phase=%d Elapsed=%.3f AttackStarted=%d"),
			*GetNameSafe(Data.Turret), *GetNameSafe(Data.ActiveWeapon), static_cast<int32>(Data.Phase), Data.ElapsedTime, Data.bAttackStarted);
	}
	if (IsValid(Data.ActiveWeapon))
	{
		Data.ActiveWeapon->StopAttack();
	}
	if (Data.Turret)
	{
		if (Data.Turret->GetCurrentWeapon() == Data.ActiveWeapon)
		{
			Data.Turret->StopCurrentAttack();
		}
		else
		{
			Data.Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
		}
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

	if (Data.Turret->UpdateTurretImpactRecovery(FMath::Max(DeltaTime, 0.0f)))
	{
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Running;
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
