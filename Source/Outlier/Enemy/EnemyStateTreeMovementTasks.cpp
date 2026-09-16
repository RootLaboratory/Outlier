#include "EnemyStateTreeMovementTasks.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogEnemyFlight, Log, All);

TAutoConsoleVariable<int32> CVarEnemyFlightDebug(
	TEXT("outlier.Enemy.FlightDebug"), 0,
	TEXT("Log enemy flight task entry, exit and facing failures. 0: off, 1: on."));

void LogFlight(
	const TCHAR* Phase,
	const FEnemyFlyToLocationTaskInstanceData& Data,
	const FStateTreeTransitionResult* Transition = nullptr)
{
	if (CVarEnemyFlightDebug.GetValueOnGameThread() == 0) return;
	const AEnemyBase* Enemy = IsValid(Data.Enemy) ? Data.Enemy.Get() : nullptr;
	const FVector Location = Enemy ? Enemy->GetActorLocation() : FVector::ZeroVector;
	const FString ChangeType = Transition
		? UEnum::GetValueAsString(Transition->ChangeType) : TEXT("None");
	const FString CurrentState = Transition
		? Transition->CurrentState.Describe() : TEXT("None");
	const FString TargetState = Transition
		? Transition->TargetState.Describe() : TEXT("None");
	const FString RunStatus = Transition
		? UEnum::GetValueAsString(Transition->CurrentRunStatus) : TEXT("None");
	const FString Priority = Transition
		? UEnum::GetValueAsString(Transition->Priority) : TEXT("None");
	UE_LOG(LogEnemyFlight, Log,
		TEXT("[Flight] %s Enemy=%s Result=%s Location=%s Destination=%s Distance=%.1f Elapsed=%.2f NoProgress=%.2f Rotate=%d Controller=%s Cached=%s Authority=%d Change=%s Current=%s Target=%s RunStatus=%s Priority=%s"),
		Phase, *GetNameSafe(Enemy), *UEnum::GetValueAsString(Data.MoveResult),
		*Location.ToCompactString(), *Data.Destination.ToCompactString(),
		FVector::Distance(Location, Data.Destination), Data.MoveElapsed, Data.NoProgressElapsed,
		Data.bRotateTowardDestination, *GetNameSafe(Enemy ? Enemy->GetController() : nullptr),
		*GetNameSafe(Data.CachedController), Enemy && Enemy->HasAuthority(),
		*ChangeType, *CurrentState, *TargetState, *RunStatus, *Priority);
}

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

EEnemyFlightMoveResult UpdateFlightProgress(FEnemyFlyToLocationTaskInstanceData& Data, float Distance, float DeltaTime)
{
	Data.MoveElapsed += DeltaTime;
	Data.NoProgressElapsed += DeltaTime;
	const float ProgressDistance = FMath::Max(Data.ProgressDistance, 1.0f);
	if (!Data.LastDestination.Equals(Data.Destination, ProgressDistance))
	{
		Data.LastDestination = Data.Destination;
		Data.BestDistance = Distance;
		Data.NoProgressElapsed = 0.0f;
	}
	else if (Data.BestDistance - Distance >= ProgressDistance)
	{
		Data.BestDistance = Distance;
		Data.NoProgressElapsed = 0.0f;
	}
	if (Data.MoveTimeout > 0.0f && Data.MoveElapsed >= Data.MoveTimeout)
	{
		return EEnemyFlightMoveResult::TimedOut;
	}
	if (Data.NoProgressTimeout > 0.0f && Data.NoProgressElapsed >= Data.NoProgressTimeout)
	{
		return EEnemyFlightMoveResult::Blocked;
	}
	return EEnemyFlightMoveResult::Moving;
}

void StopFlyTask(FEnemyFlyToLocationTaskInstanceData& Data)
{
	LogFlight(TEXT("Stop"), Data);
	if (!Data.bOwnsMovement || !IsValid(Data.Enemy) || !Data.Enemy->HasAuthority()) return;
	// 빙의/재점유 이후 새 Controller의 이동은 이전 AI Task가 정리하지 않는다.
	if (IsValid(Data.CachedController) && Data.Enemy->GetController() == Data.CachedController)
	{
		Data.Enemy->ConsumeMovementInputVector();
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		RestoreFlyTaskBaseSpeed(*Data.Enemy);
	}
	Data.bOwnsMovement = false;
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightProgressTest,
	"Outlier.Enemy.Flight.Progress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightProgressTest::RunTest(const FString& Parameters)
{
	FEnemyFlyToLocationTaskInstanceData Data;
	TestTrue(TEXT("Legacy destination facing remains enabled"), Data.bRotateTowardDestination);
	Data.BestDistance = 1000.0f;
	TestTrue(TEXT("Legacy timeouts remain disabled"),
		UpdateFlightProgress(Data, 1000.0f, 60.0f) == EEnemyFlightMoveResult::Moving);
	Data = FEnemyFlyToLocationTaskInstanceData();
	Data.BestDistance = 1000.0f;
	Data.NoProgressTimeout = 1.0f;
	TestTrue(TEXT("Before no-progress deadline"),
		UpdateFlightProgress(Data, 1000.0f, 0.5f) == EEnemyFlightMoveResult::Moving);
	TestTrue(TEXT("At no-progress deadline"),
		UpdateFlightProgress(Data, 1000.0f, 0.5f) == EEnemyFlightMoveResult::Blocked);
	TestTrue(TEXT("Progress resets deadline"),
		UpdateFlightProgress(Data, 975.0f, 0.1f) == EEnemyFlightMoveResult::Moving);
	TestEqual(TEXT("Progress timer reset"), Data.NoProgressElapsed, 0.0f);
	Data.Destination = FVector(100.0f, 0.0f, 0.0f);
	TestTrue(TEXT("Retarget resets no-progress measurement"),
		UpdateFlightProgress(Data, 2000.0f, 1.0f) == EEnemyFlightMoveResult::Moving);
	Data.MoveTimeout = Data.MoveElapsed + 0.5f;
	TestTrue(TEXT("Retarget cannot reset total timeout"),
		UpdateFlightProgress(Data, 1900.0f, 0.5f) == EEnemyFlightMoveResult::TimedOut);
	return true;
}
#endif

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
	InstanceData.CachedController = AIController;
	InstanceData.MoveResult = EEnemyFlightMoveResult::Invalid;
	InstanceData.MoveElapsed = 0.0f;
	InstanceData.NoProgressElapsed = 0.0f;
	InstanceData.bOwnsMovement = false;
	LogFlight(TEXT("Enter"), InstanceData, &Transition);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		LogFlight(TEXT("Rejected: missing enemy/authority/AIController"), InstanceData);
		return EStateTreeRunStatus::Failed;
	}

	// 이전 NavMesh 이동 요청이 비행 입력과 경쟁하지 않도록 진입 시 정리한다.
	AIController->StopMovement();
	InstanceData.bOwnsMovement = true;
	Enemy->ConsumeMovementInputVector();
	InstanceData.LastDestination = InstanceData.Destination;
	InstanceData.BestDistance = FVector::Distance(Enemy->GetActorLocation(), InstanceData.Destination);
	if (InstanceData.Destination.ContainsNaN())
	{
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}

	const float AcceptanceRadius = FMath::Max(InstanceData.AcceptanceRadius, 0.0f);
	const bool bAlreadyAtDestination =
		FVector::DistSquared(Enemy->GetActorLocation(), InstanceData.Destination)
		<= FMath::Square(AcceptanceRadius);
	if (!bAlreadyAtDestination)
	{
		ApplyFlyTaskSpeedMultiplier(*Enemy, InstanceData.SpeedMultiplier);
	}
	InstanceData.MoveResult = bAlreadyAtDestination ? EEnemyFlightMoveResult::Arrived : EEnemyFlightMoveResult::Moving;
	LogFlight(TEXT("Started"), InstanceData);
	if (bAlreadyAtDestination) StopFlyTask(InstanceData);
	return bAlreadyAtDestination
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyFlyToLocationTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = InstanceData.Enemy;
	AAIController* AIController = InstanceData.CachedController;
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !IsValid(AIController)
		|| Enemy->GetController() != AIController || InstanceData.Destination.ContainsNaN())
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Invalid;
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}

	ApplyFlyTaskSpeedMultiplier(*Enemy, InstanceData.SpeedMultiplier);
	const FVector ToDestination = InstanceData.Destination - Enemy->GetActorLocation();
	const float AcceptanceRadius = FMath::Max(InstanceData.AcceptanceRadius, 0.0f);

	if (ToDestination.SizeSquared() <= FMath::Square(AcceptanceRadius))
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Arrived;
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Succeeded;
	}

	InstanceData.MoveResult = UpdateFlightProgress(InstanceData, ToDestination.Size(), DeltaTime);
	if (InstanceData.MoveResult != EEnemyFlightMoveResult::Moving)
	{
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (InstanceData.bRotateTowardDestination)
	{
		RotateTowardLocation(
			*Enemy,
			*AIController,
			InstanceData.Destination,
			InstanceData.RotationSpeed,
			0.0f,
			DeltaTime);
	}

	// XY와 Z를 함께 입력해 PatrolPoint의 비행 고도를 그대로 유지한다.
	Enemy->AddMovementInput(ToDestination.GetSafeNormal());
	return EStateTreeRunStatus::Running;
}

void FEnemyFlyToLocationTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.MoveResult == EEnemyFlightMoveResult::Moving)
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Interrupted;
	}
	StopFlyTask(InstanceData);
	LogFlight(TEXT("Exit"), InstanceData, &Transition);
	InstanceData.CachedController = nullptr;
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
	const bool bValid = IsValid(InstanceData.Enemy)
		&& InstanceData.Enemy->HasAuthority()
		&& ResolveAIController(InstanceData.Enemy);
	if (CVarEnemyFlightDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyFlight, Log, TEXT("[Facing] Enter Enemy=%s Target=%s Valid=%d"),
			*GetNameSafe(InstanceData.Enemy), *GetNameSafe(InstanceData.TargetActor), bValid);
	}
	return bValid ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemyMaintainFacingTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AAIController* AIController = ResolveAIController(InstanceData.Enemy);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority() || !AIController)
	{
		if (CVarEnemyFlightDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogEnemyFlight, Warning, TEXT("[Facing] Failed Enemy=%s Controller=%s"),
				*GetNameSafe(InstanceData.Enemy), *GetNameSafe(AIController));
		}
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
