#include "Enemy/EnemyStateTreeAttackTasks.h"

#include "Enemy/EnemyAIController.h"
#include "Weapon/RangedWeaponBase.h"
#include "GameFramework/Controller.h"

namespace
{
constexpr float CombatDecisionRefreshInterval = 0.25f;

void LogCloseAttackAim(const AEnemyBase& Enemy, const TCHAR* Task, const FVector& Input, float PreviousPitch)
{
	if (Enemy.IsEnemyPossessed() || Enemy.GetRuntimeStat().Type != EEnemyType::Gun)
	{
		return;
	}
	const FVector Origin = Enemy.GetCombatShotOrigin();
	if (FVector::DistSquared2D(Origin, Input) > FMath::Square(500.0))
	{
		return;
	}
	const AController* Controller = Enemy.GetController();
	const AActor* Tracked = Enemy.GetTrackedTarget();
	const FVector TrackedPoint = IsValid(Tracked) ? Enemy.GetCombatAimPoint(Tracked) : FVector::ZeroVector;
	// 근거리 버스트를 실제로 시작한 순간만 기록한다. 매 Tick 로그 없이 Task 입력과 시선 덮어쓰기를 구분한다.
	UE_LOG(LogTemp, Display, TEXT("[EnemyAimTask] Task=%s Enemy=%s Origin=%s Input=%s Tracked=%s TrackedPoint=%s Distance2D=%.2f HeightDelta=%.2f InputPitch=%.3f PreviousPitch=%.3f AppliedPitch=%.3f Phase=%s"),
		Task, *Enemy.GetName(), *Origin.ToCompactString(), *Input.ToCompactString(), *GetNameSafe(Tracked),
		*TrackedPoint.ToCompactString(), FVector::Dist2D(Origin, Input), Input.Z - Origin.Z,
		(Input - Origin).Rotation().Pitch, PreviousPitch, Controller ? Controller->GetControlRotation().Pitch : 0.0f,
		*UEnum::GetValueAsString(Enemy.GetAttackPhase()));
}

float ReadControlPitch(const AEnemyBase& Enemy)
{
	const AController* Controller = Enemy.GetController();
	return Controller ? Controller->GetControlRotation().Pitch : 0.0f;
}

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
	InstanceData.CycleTarget.Reset();
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
	InstanceData.CycleTarget = TargetActor;

	// 시도 자격은 Pawn이 보관한다. Task 재진입은 소진 기록이나 남은 버스트를 새로 만들지 않는다.
	InstanceData.Enemy->PrepareTargetAttackAttempt(InstanceData.SelectedAttackDuration);
	if (!InstanceData.Enemy->CanAttackTrackedTarget(TargetActor)
		|| !InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
	{
		return EStateTreeRunStatus::Running;
	}

	const float PreviousPitch = ReadControlPitch(*InstanceData.Enemy);
	if (!InstanceData.Enemy->StartAttackTarget(TargetActor))
	{
		return EStateTreeRunStatus::Running;
	}

	InstanceData.bAttackStarted = true;
	LogCloseAttackAim(*InstanceData.Enemy, TEXT("AttackTarget.Enter"), InstanceData.Enemy->GetCombatAimPoint(TargetActor), PreviousPitch);
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
	// 추적이 먼저 만료돼도 시작한 시도의 절대 기한은 회복으로 인계한다.
	if (InstanceData.Enemy->HasTargetAttackAttemptExpired())
	{
		InstanceData.Enemy->FinishTargetAttackAttempt();
		return EStateTreeRunStatus::Succeeded;
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
	if (InstanceData.CycleTarget.Get() != TargetActor)
	{
		// 비사격 대기 중 채택한 새 대상은 이전 버스트의 소비 시간을 물려받지 않는다.
		// 이전 대상의 무기 정리는 Pawn의 대상 교체 경계에서 이미 끝난 상태다.
		InstanceData.CycleTarget = TargetActor;
		InstanceData.ElapsedTime = 0.0f;
		InstanceData.bAttackStarted = false;
	}

	if (InstanceData.Enemy->UsesTargetAttackPreparation())
	{
		// 같은 공격 State에서 대상만 바뀌어도 새 대상의 전조는 필요하다.
		// 최초 Telegraph가 정한 시간으로 준비를 이어가며, 이미 완료된 같은 대상에는 시간을 더하지 않는다.
		InstanceData.Enemy->AdvanceTargetAttackPreparation(DeltaTime);
	}

	InstanceData.Enemy->PrepareTargetAttackAttempt(
		FMath::Max(0.0f, InstanceData.SelectedAttackDuration - InstanceData.ElapsedTime));

	// 이동 완료와 무관하게 사거리·사선·전조·조준각을 검사한다.
	// 보류 중에는 무기만 멈추고 버스트 잔여시간을 보존한다. 회전은 Controller의 기존 갱신이 이어간다.
	if (!InstanceData.Enemy->CanAttackTrackedTarget(TargetActor))
	{
		if (InstanceData.bAttackStarted)
		{
			InstanceData.Enemy->PauseTargetAttack();
		}
		InstanceData.bAttackStarted = false;
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.bAttackStarted)
	{
		if (!InstanceData.Enemy->GetCurrentWeapon()->CanAttack())
		{
			return EStateTreeRunStatus::Running;
		}

		const float PreviousPitch = ReadControlPitch(*InstanceData.Enemy);
		if (!InstanceData.Enemy->StartAttackTarget(TargetActor))
		{
			return EStateTreeRunStatus::Running;
		}

		InstanceData.bAttackStarted = true;
		LogCloseAttackAim(*InstanceData.Enemy, TEXT("AttackTarget.TickStart"), InstanceData.Enemy->GetCombatAimPoint(TargetActor), PreviousPitch);
		return EStateTreeRunStatus::Running;
	}

	// 공격 시작과 같은 선택 부위를 조회해 조준과 실제 탄도의 목표점을 일치시킨다.
	const FVector TargetLocation = InstanceData.Enemy->GetCombatAimPoint(TargetActor);
	if (!InstanceData.Enemy->UpdateAttackLocation(TargetLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	// 정상 연사 간격도 활성 버스트 시간이다. CanAttack()의 매 탄 쿨다운으로 이 시계를 멈추지 않는다.
	InstanceData.ElapsedTime += FMath::Max(0.0f, DeltaTime);
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
	if (!InstanceData.Enemy)
	{
		return;
	}
	if (InstanceData.CycleTarget.Get() == InstanceData.Enemy->GetTrackedTarget())
	{
		// 같은 대상의 Task만 예외 구간을 닫는다. 사용 기록은 Pawn에 남아 다음 진입도 보호한다.
		InstanceData.Enemy->FinishTargetAttackAttempt();
	}
	if (InstanceData.bAttackStarted && InstanceData.CycleTarget.Get() == InstanceData.Enemy->GetTrackedTarget())
	{
		// 조사 상태 진입보다 이전에 Exit가 호출되어도 남은 발사 간격을 보존한다.
		if (InstanceData.Enemy->IsLocationInvestigationActive()
			|| InstanceData.Enemy->GetPerceptionGoal().Type == EEnemyPerceptionGoalType::InvestigateNoise)
		{
			InstanceData.Enemy->PauseTargetAttack();
		}
		else
		{
			InstanceData.Enemy->StopCurrentWeaponAttack();
		}
	}
	// 늦은 이전 Task의 Exit는 새 대상의 공격/보호를 정리하지 않는다.
	// 다음 Recover 진입이 원래 회복 기한을 먼저 기록한 뒤 선택 경계를 연다.
	InstanceData.Enemy->EndTargetSelectionBurst(InstanceData.CycleTarget.Get());
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

	const float PreviousPitch = ReadControlPitch(*InstanceData.Enemy);
	if (!InstanceData.Enemy->StartAttackLocation(InstanceData.TargetLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bAttackStarted = true;
	LogCloseAttackAim(*InstanceData.Enemy, TEXT("AttackLocation.Enter"), InstanceData.TargetLocation, PreviousPitch);
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

		const float PreviousPitch = ReadControlPitch(*InstanceData.Enemy);
		if (!InstanceData.Enemy->StartAttackLocation(InstanceData.TargetLocation))
		{
			return EStateTreeRunStatus::Failed;
		}

		InstanceData.bAttackStarted = true;
		LogCloseAttackAim(*InstanceData.Enemy, TEXT("AttackLocation.TickStart"), InstanceData.TargetLocation, PreviousPitch);
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
	{
		return EStateTreeRunStatus::Running;
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
	EEnemyAttackPhase PresentationPhase = InstanceData.Phase;
	if (InstanceData.Enemy->UsesTargetAttackPreparation())
	{
		if (InstanceData.Phase == EEnemyAttackPhase::Telegraph)
		{
			// Task 재진입은 남은 전조를 재선택하지 않는다. 대상별 준비와 무기 공통 회복은 Enemy가 보관한다.
			InstanceData.Enemy->BeginTargetAttackPreparation(InstanceData.SelectedDuration);
			if (InstanceData.Enemy->IsTargetAttackPreparationComplete())
			{
				PresentationPhase = EEnemyAttackPhase::Recover;
			}
		}
		else if (InstanceData.Phase == EEnemyAttackPhase::Recover)
		{
			InstanceData.Enemy->RememberTargetAttackRecovery(InstanceData.SelectedDuration);
			// 대상 교체가 회복을 생략하지 않도록 종료 시각 확정 뒤 재평가한다.
			InstanceData.Enemy->RefreshTargetTrackingAtSelectionBoundary();
		}
	}
	else if (!InstanceData.Enemy->IsEnemyPossessed())
	{
		if (InstanceData.Phase == EEnemyAttackPhase::Telegraph)
		{
			// 수색 중 같은 대상을 다시 찾으면 새 전조 대신 남아 있던 회복 시간만 기다린다.
			if (InstanceData.Enemy->ConsumeTargetAttackResume())
			{
				InstanceData.SelectedDuration = InstanceData.Enemy->GetTargetAttackRecoveryRemaining();
				PresentationPhase = EEnemyAttackPhase::Recover;
				if (InstanceData.SelectedDuration <= 0.0f)
				{
					return EStateTreeRunStatus::Succeeded;
				}
			}
			InstanceData.Enemy->RememberTargetAttackPreparation();
		}
		else if (InstanceData.Phase == EEnemyAttackPhase::Recover)
		{
			InstanceData.Enemy->RememberTargetAttackRecovery(InstanceData.SelectedDuration);
		}
	}
	// 서버가 단계만 확정하고 복제하며, 실제 전조·회복 연출은 Enemy BP가 선택한다.
	InstanceData.Enemy->SetAttackPhase(PresentationPhase);
	if (InstanceData.Enemy->UsesTargetAttackPreparation())
	{
		if (InstanceData.Phase == EEnemyAttackPhase::Telegraph)
		{
			return InstanceData.Enemy->IsTargetAttackPreparationComplete()
				&& InstanceData.Enemy->GetTargetAttackRecoveryRemaining() <= 0.0f
				&& InstanceData.Enemy->IsTargetAttackAimReady()
				? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
		}
		if (InstanceData.Phase == EEnemyAttackPhase::Recover)
		{
			return InstanceData.Enemy->GetTargetAttackRecoveryRemaining() <= 0.0f
				? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
		}
	}
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

	if (InstanceData.Enemy->UsesTargetAttackPreparation())
	{
		if (InstanceData.Phase == EEnemyAttackPhase::Telegraph)
		{
			// 전조는 직접 관측 중에만 진행한다. 회전과 회복 기한은 동시에 진행하며 어느 쪽도 다시 시작하지 않는다.
			// 같은 State를 유지한 채 대상이 교체된 경우에도 새 대상의 준비 소유권을 먼저 확정한다.
			InstanceData.Enemy->BeginTargetAttackPreparation(InstanceData.SelectedDuration);
			const bool bPrepared = InstanceData.Enemy->AdvanceTargetAttackPreparation(DeltaTime);
			return bPrepared && InstanceData.Enemy->GetTargetAttackRecoveryRemaining() <= 0.0f
				&& InstanceData.Enemy->IsTargetAttackAimReady()
				? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
		}
		if (InstanceData.Phase == EEnemyAttackPhase::Recover)
		{
			// 회복 State 안에서 대상이 바뀌어도 새 전조는 회복과 병렬로 진행한다.
			InstanceData.Enemy->AdvanceTargetAttackPreparation(DeltaTime);
			// 대상 교체/재진입과 무관하게 처음 정한 서버 종료 시각을 기다린다.
			if (InstanceData.Enemy->GetTargetAttackRecoveryRemaining() <= 0.0f)
			{
				InstanceData.Enemy->RefreshTargetTrackingAtSelectionBoundary();
				return EStateTreeRunStatus::Succeeded;
			}
			return EStateTreeRunStatus::Running;
		}
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
		&& Transition.ChangeType == EStateTreeStateChangeType::Sustained)
	{
		return;
	}
	if (InstanceData.Enemy
		&& InstanceData.Phase == EEnemyAttackPhase::Recover
		&& InstanceData.Enemy->GetAttackPhase() == EEnemyAttackPhase::Recover
		&& !InstanceData.Enemy->HasPossessedAttackRequest())
	{
		InstanceData.Enemy->SetAttackPhase(EEnemyAttackPhase::Idle);
	}
}
