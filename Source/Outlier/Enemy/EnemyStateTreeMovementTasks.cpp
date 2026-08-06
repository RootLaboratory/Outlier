#include "EnemyStateTreeMovementTasks.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
AAIController* ResolveAIController(AEnemyBase* Enemy)
{
	return Enemy ? Cast<AAIController>(Enemy->GetController()) : nullptr;
}

AEnemyBase* ResolveEnemy(
	FStateTreeExecutionContext& Context,
	AEnemyBase* BoundEnemy)
{
	if (IsValid(BoundEnemy))
	{
		return BoundEnemy;
	}

	if (AEnemyBase* OwnerEnemy = Cast<AEnemyBase>(Context.GetOwner()))
	{
		return OwnerEnemy;
	}

	if (const UActorComponent* OwnerComponent = Cast<UActorComponent>(Context.GetOwner()))
	{
		return Cast<AEnemyBase>(OwnerComponent->GetOwner());
	}

	return nullptr;
}

FVector ResolveFacingLocation(
	const FEnemyMaintainFacingTaskInstanceData& InstanceData,
	AAIController& AIController)
{
	// 명시적 바인딩을 우선하고, 없을 때만 AIController의 현재 우선 타깃을 조회한다.
	if (IsValid(InstanceData.TargetActor))
	{
		return InstanceData.TargetActor->GetActorLocation();
	}

	if (InstanceData.bUsePreferredVisibleTarget)
	{
		if (const AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(&AIController))
		{
			if (const AActor* Target = EnemyAIController->GetPreferredVisibleTarget())
			{
				return Target->GetActorLocation();
			}
		}
	}

	return InstanceData.TargetLocation;
}

// Capsule(Actor 본체)은 Yaw만 돌리고, AIController의 ControlRotation에는 Pitch까지 반영한다.
// VECDroneMovementComponent::UpdateAIFacingPitch()가 이 Pitch를 읽어 AIFacingPitchRoot(메시 쪽
// Pitch 전용 회전축)에 적용하므로, 몸체 이동/충돌은 Yaw만으로 처리하고 시각적인 "바라보는 방향"
// 연출만 별도로 분리할 수 있다.
bool RotateTowardLocation(
	AEnemyBase& Enemy,
	AAIController& AIController,
	const FVector& TargetLocation,
	float RotationSpeed,
	float AngleTolerance,
	float DeltaTime)
{
	const FVector Direction = TargetLocation - Enemy.GetActorLocation();
	if (Direction.IsNearlyZero())
	{
		return true;
	}

	FRotator DesiredControlRotation = Direction.Rotation();
	DesiredControlRotation.Roll = 0.0f;

	const FRotator NewControlRotation = FMath::RInterpConstantTo(
		AIController.GetControlRotation(),
		DesiredControlRotation,
		DeltaTime,
		FMath::Max(RotationSpeed, 0.0f)
	).GetNormalized();
	AIController.SetControlRotation(NewControlRotation);

	Enemy.SetActorRotation(FRotator(0.0f, NewControlRotation.Yaw, 0.0f));

	const float YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		NewControlRotation.Yaw,
		DesiredControlRotation.Yaw
	));
	const float PitchError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		NewControlRotation.Pitch,
		DesiredControlRotation.Pitch
	));
	return YawError <= AngleTolerance && PitchError <= AngleTolerance;
}

void ApplyFlyTaskSpeedMultiplier(
	AEnemyBase& Enemy,
	float SpeedMultiplier)
{
	if (UCharacterMovementComponent* Movement = Enemy.GetCharacterMovement())
	{
		const float BaseMoveSpeed = FMath::Max(Enemy.GetRuntimeStat().MoveSpeed, 0.0f);
		Movement->MaxFlySpeed =
			BaseMoveSpeed * FMath::Max(SpeedMultiplier, 0.0f);
	}
}

void RestoreFlyTaskBaseSpeed(AEnemyBase& Enemy)
{
	if (UCharacterMovementComponent* Movement = Enemy.GetCharacterMovement())
	{
		Movement->MaxFlySpeed = FMath::Max(
			Enemy.GetRuntimeStat().MoveSpeed,
			0.0f);
	}
}
}

FEnemyFlyToLocationTask::FEnemyFlyToLocationTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyFlyToLocationTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = ResolveEnemy(Context, InstanceData.Enemy);
	InstanceData.Enemy = Enemy;
	AAIController* AIController = ResolveAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 이전 NavMesh 이동 요청이 비행 입력과 경쟁하지 않도록 진입 시 정리한다.
	AIController->StopMovement();

	const float AcceptanceRadius = FMath::Max(InstanceData.AcceptanceRadius, 0.0f);
	const bool bAlreadyAtDestination =
		FVector::DistSquared(Enemy->GetActorLocation(), InstanceData.Destination)
		<= FMath::Square(AcceptanceRadius);
	if (!bAlreadyAtDestination)
	{
		ApplyFlyTaskSpeedMultiplier(*Enemy, InstanceData.SpeedMultiplier);
	}
	return bAlreadyAtDestination
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyFlyToLocationTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = ResolveEnemy(Context, InstanceData.Enemy);
	InstanceData.Enemy = Enemy;
	AAIController* AIController = ResolveAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	ApplyFlyTaskSpeedMultiplier(*Enemy, InstanceData.SpeedMultiplier);
	const FVector ToDestination = InstanceData.Destination - Enemy->GetActorLocation();
	const float AcceptanceRadius = FMath::Max(InstanceData.AcceptanceRadius, 0.0f);

	if (ToDestination.SizeSquared() <= FMath::Square(AcceptanceRadius))
	{
		if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		return EStateTreeRunStatus::Succeeded;
	}

	RotateTowardLocation(
		*Enemy,
		*AIController,
		InstanceData.Destination,
		InstanceData.RotationSpeed,
		0.0f,
		DeltaTime);

	// XY와 Z를 함께 입력해 PatrolPoint의 비행 고도를 그대로 유지한다.
	Enemy->AddMovementInput(ToDestination.GetSafeNormal());
	return EStateTreeRunStatus::Running;
}

void FEnemyFlyToLocationTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (IsValid(InstanceData.Enemy))
	{
		if (UCharacterMovementComponent* Movement = InstanceData.Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		RestoreFlyTaskBaseSpeed(*InstanceData.Enemy);
	}
}

FEnemyFaceLocationTask::FEnemyFaceLocationTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyFaceLocationTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AAIController* AIController = ResolveAIController(InstanceData.Enemy);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	AIController->StopMovement();
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyFaceLocationTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AAIController* AIController = ResolveAIController(InstanceData.Enemy);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	return RotateTowardLocation(
		*InstanceData.Enemy,
		*AIController,
		InstanceData.TargetLocation,
		InstanceData.RotationSpeed,
		InstanceData.AngleTolerance,
		DeltaTime
	)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

FEnemyMaintainFacingTask::FEnemyMaintainFacingTask()
{
	bShouldStateChangeOnReselect = false;
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif
}

EStateTreeRunStatus FEnemyMaintainFacingTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	return InstanceData.Enemy
		&& InstanceData.Enemy->HasAuthority()
		&& ResolveAIController(InstanceData.Enemy)
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemyMaintainFacingTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AAIController* AIController = ResolveAIController(InstanceData.Enemy);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	RotateTowardLocation(
		*InstanceData.Enemy,
		*AIController,
		ResolveFacingLocation(InstanceData, *AIController),
		InstanceData.RotationSpeed,
		0.0f,
		DeltaTime);
	return EStateTreeRunStatus::Running;
}

FEnemyAlertHoldTask::FEnemyAlertHoldTask()
{
	bShouldStateChangeOnReselect = false;

	// Alert 상태는 Task 완료가 아니라 Combat 이벤트 전환으로 빠져나간다.
	// bConsideredForCompletion은 이 상시 Running 동작에 대한 에디터 경고만 막는다.
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif
}

EStateTreeRunStatus FEnemyAlertHoldTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = ResolveEnemy(Context, InstanceData.Enemy);
	InstanceData.Enemy = Enemy;
	AAIController* AIController = ResolveAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	AIController->StopMovement();
	InstanceData.VisibleElapsedTime = 0.0f;
	InstanceData.LostElapsedTime = 0.0f;
	if (!InstanceData.bOwnsTaskDrivenPitch)
	{
		if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(AIController))
		{
			// Alert 중 계산한 Pitch가 AIController의 기본 회전 갱신으로 초기화되지 않게 유지한다.
			EnemyAIController->BeginTaskDrivenControlPitch();
			InstanceData.bOwnsTaskDrivenPitch = true;
		}
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyAlertHoldTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = ResolveEnemy(Context, InstanceData.Enemy);
	InstanceData.Enemy = Enemy;
	AAIController* AIController = ResolveAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Commit 직후 이벤트 전환이 다음 처리 단계에서 적용되더라도 중복 요청하지 않는다.
	if (Enemy->GetCombatState() != EEnemyCombatState::Alert)
	{
		return EStateTreeRunStatus::Running;
	}

	const bool bPlayerVisible = Enemy->IsPlayerCurrentlyVisible();
	AActor* VisibleTarget = nullptr;
	FVector FacingLocation = Enemy->GetLastKnownPlayerLocation();
	if (bPlayerVisible)
	{
		if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(AIController))
		{
			VisibleTarget = EnemyAIController->GetPreferredVisibleTarget();
		}

		if (IsValid(VisibleTarget))
		{
			FacingLocation = VisibleTarget->GetActorLocation();
			// 보이는 동안은 실제 타겟을 따라가며, 시야가 끊긴 순간 사용할 LKP도 함께 최신화한다.
			Enemy->UpdateLastKnownPlayerLocation(FacingLocation);
		}
	}

	RotateTowardLocation(
		*Enemy,
		*AIController,
		FacingLocation,
		InstanceData.RotationSpeed,
		0.0f,
		DeltaTime
	);

	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0f);

	if (bPlayerVisible)
	{
		InstanceData.VisibleElapsedTime += SafeDeltaTime;
		InstanceData.LostElapsedTime = 0.0f;

		if (InstanceData.VisibleElapsedTime >= FMath::Max(InstanceData.VisibleCommitDuration, 0.0f))
		{
			Enemy->CommitAlertToCombat();
		}
	}
	else
	{
		InstanceData.LostElapsedTime += SafeDeltaTime;
		InstanceData.VisibleElapsedTime = 0.0f;

		if (InstanceData.LostElapsedTime >= FMath::Max(InstanceData.LostCommitDuration, 0.0f))
		{
			Enemy->CommitAlertToNonCombat();
		}
	}

	return EStateTreeRunStatus::Running;
}

void FEnemyAlertHoldTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.bOwnsTaskDrivenPitch)
	{
		return;
	}

	if (AEnemyAIController* AIController = Cast<AEnemyAIController>(
		ResolveAIController(InstanceData.Enemy)))
	{
		AIController->EndTaskDrivenControlPitch();
	}
	InstanceData.bOwnsTaskDrivenPitch = false;
}

FEnemyRotateToAngleAndWaitTask::FEnemyRotateToAngleAndWaitTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyRotateToAngleAndWaitTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AAIController* AIController = ResolveAIController(InstanceData.Enemy);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	AIController->StopMovement();
	InstanceData.TargetYaw = FRotator::NormalizeAxis(
		InstanceData.Enemy->GetActorRotation().Yaw + InstanceData.RelativeYawDegrees
	);
	InstanceData.ElapsedWaitTime = 0.0f;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyRotateToAngleAndWaitTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AAIController* AIController = ResolveAIController(InstanceData.Enemy);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	const float CurrentYaw = InstanceData.Enemy->GetActorRotation().Yaw;
	const float NewYaw = FMath::FixedTurn(
		CurrentYaw,
		InstanceData.TargetYaw,
		FMath::Max(InstanceData.RotationSpeed, 0.0f) * DeltaTime
	);
	InstanceData.Enemy->SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));

	FRotator ControlRotation = AIController->GetControlRotation();
	ControlRotation.Yaw = NewYaw;
	ControlRotation.Roll = 0.0f;
	AIController->SetControlRotation(ControlRotation);

	const float YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		NewYaw,
		InstanceData.TargetYaw
	));
	if (YawError > InstanceData.AngleTolerance)
	{
		InstanceData.ElapsedWaitTime = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	InstanceData.ElapsedWaitTime += DeltaTime;
	return InstanceData.ElapsedWaitTime >= InstanceData.WaitDuration
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

FEnemyLookAroundTaskInstanceData::FEnemyLookAroundTaskInstanceData()
{
	Steps =
	{
		{FRotator::ZeroRotator, 0.15f},
		{FRotator(10.0f, -60.0f, 0.0f), 0.5f},
		{FRotator::ZeroRotator, 0.2f},
		{FRotator(-10.0f, 60.0f, 0.0f), 0.5f},
		{FRotator::ZeroRotator, 0.2f}
	};
}

FEnemyLookAroundTask::FEnemyLookAroundTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyLookAroundTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = ResolveEnemy(Context, InstanceData.Enemy);
	InstanceData.Enemy = Enemy;
	AAIController* AIController = ResolveAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.Steps.IsEmpty())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	InstanceData.BaseControlRotation = AIController->GetControlRotation().GetNormalized();
	InstanceData.BaseControlRotation.Roll = 0.0f;
	if (!InstanceData.bOwnsTaskDrivenPitch)
	{
		if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(AIController))
		{
			EnemyAIController->BeginTaskDrivenControlPitch();
			InstanceData.bOwnsTaskDrivenPitch = true;
		}
	}
	// 배열을 역순으로 순회하면 같은 설정으로 우측부터 시작하는 탐색도 표현할 수 있다.
	const bool bReverse = InstanceData.bRandomizeFirstDirection && FMath::RandBool();
	InstanceData.StepDirection = bReverse ? -1 : 1;
	InstanceData.CurrentStepIndex = bReverse ? InstanceData.Steps.Num() - 1 : 0;
	InstanceData.ElapsedHoldTime = 0.0f;

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyLookAroundTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = ResolveEnemy(Context, InstanceData.Enemy);
	InstanceData.Enemy = Enemy;
	AAIController* AIController = ResolveAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Steps.IsValidIndex(InstanceData.CurrentStepIndex))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	const FEnemyLookAroundStep& Step = InstanceData.Steps[InstanceData.CurrentStepIndex];
	// 시작 시점의 방향을 기준으로 계산해 각 단계의 회전 오차가 다음 단계에 누적되지 않게 한다.
	FRotator DesiredRotation =
		(InstanceData.BaseControlRotation + Step.RelativeRotation).GetNormalized();
	DesiredRotation.Roll = 0.0f;

	const FRotator NewRotation = FMath::RInterpConstantTo(
		AIController->GetControlRotation(),
		DesiredRotation,
		DeltaTime,
		FMath::Max(InstanceData.RotationSpeed, 0.0f)).GetNormalized();
	AIController->SetControlRotation(NewRotation);
	Enemy->SetActorRotation(FRotator(0.0f, NewRotation.Yaw, 0.0f));

	const float YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		NewRotation.Yaw,
		DesiredRotation.Yaw));
	const float PitchError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		NewRotation.Pitch,
		DesiredRotation.Pitch));
	if (YawError > InstanceData.AngleTolerance
		|| PitchError > InstanceData.AngleTolerance)
	{
		InstanceData.ElapsedHoldTime = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	InstanceData.ElapsedHoldTime += DeltaTime;
	if (InstanceData.ElapsedHoldTime < Step.HoldDuration)
	{
		return EStateTreeRunStatus::Running;
	}

	InstanceData.CurrentStepIndex += InstanceData.StepDirection;
	InstanceData.ElapsedHoldTime = 0.0f;
	if (!InstanceData.Steps.IsValidIndex(InstanceData.CurrentStepIndex))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FEnemyLookAroundTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.bOwnsTaskDrivenPitch)
	{
		return;
	}

	if (AEnemyAIController* AIController = Cast<AEnemyAIController>(
		ResolveAIController(InstanceData.Enemy)))
	{
		AIController->EndTaskDrivenControlPitch();
	}
	InstanceData.bOwnsTaskDrivenPitch = false;
}
