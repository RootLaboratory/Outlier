#include "Enemy/EnemyStateTreeCombatMovementTasks.h"

#include "AIController.h"
#include "CollisionQueryParams.h"
#include "Enemy/EnemyAIController.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyBattleMovement, Log, All);

namespace
{
constexpr float MinimumEnemyFlightZ = 150.0f;

AEnemyAIController* ResolveEnemyAIController(AEnemyBase* Enemy)
{
	return Enemy ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
}

AActor* ResolveTarget(AEnemyAIController& AIController, AActor* ExplicitTarget)
{
	return IsValid(ExplicitTarget) ? ExplicitTarget : AIController.GetPreferredVisibleTarget();
}

float CalculateTacticalDistance(
	const FVector& From,
	const FVector& To,
	EEnemyTacticalDistanceMode DistanceMode)
{
	// 현재 전투는 수평 거리로 판단하지만, 이후 3D EQS 도입 시 공간 거리로 전환할 수 있다.
	return DistanceMode == EEnemyTacticalDistanceMode::Spatial3D
		? FVector::Distance(From, To)
		: FVector::Dist2D(From, To);
}

bool ResolveFloorAnchoredLocation(
	UWorld& World,
	const FVector& HorizontalLocation,
	const FVector& FloorProbeLocation,
	float FlightHeightOffset,
	float FloorTraceHalfHeight,
	FVector& OutLocation)
{
	const float TraceDepth = FMath::Max(FloorTraceHalfHeight, 1.0f);
	FHitResult FloorHit;
	const bool bFoundFloor = World.LineTraceSingleByChannel(
		FloorHit,
		FloorProbeLocation,
		FloorProbeLocation - FVector::UpVector * TraceDepth,
		ECC_WorldStatic);
	if (!bFoundFloor)
	{
		return false;
	}

	OutLocation = FVector(
		HorizontalLocation.X,
		HorizontalLocation.Y,
		FMath::Max(
			FloorHit.ImpactPoint.Z + FlightHeightOffset,
			MinimumEnemyFlightZ));
	return true;
}

float ResolveOrbitDirectionSign(
	const AEnemyBase& Enemy,
	const FVector& OrbitCenter,
	EEnemyOrbitDirection& OutDirection)
{
	const FVector Radial = (Enemy.GetActorLocation() - OrbitCenter).GetSafeNormal2D();
	const FVector LeftTangent(-Radial.Y, Radial.X, 0.0f);
	const float TangentialVelocity = FVector::DotProduct(Enemy.GetVelocity(), LeftTangent);

	if (!FMath::IsNearlyZero(TangentialVelocity))
	{
		// 이미 접선 방향으로 이동 중이면 현재 움직임을 이어받아 선회 전환의 꺾임을 줄인다.
		OutDirection = TangentialVelocity > 0.0f
			? EEnemyOrbitDirection::Left
			: EEnemyOrbitDirection::Right;
	}
	else
	{
		// 정지 상태에서는 좌우를 한 번만 선택하고 State가 끝날 때까지 유지한다.
		OutDirection = FMath::RandBool()
			? EEnemyOrbitDirection::Left
			: EEnemyOrbitDirection::Right;
	}

	return OutDirection == EEnemyOrbitDirection::Left ? 1.0f : -1.0f;
}

bool ShouldIssueMove(
	const FVector& Candidate,
	const FVector& PreviousDestination,
	bool bHasPreviousDestination,
	float MinRetargetDistance)
{
	return !bHasPreviousDestination
		|| FVector::DistSquared(Candidate, PreviousDestination)
			>= FMath::Square(FMath::Max(MinRetargetDistance, 0.0f));
}

void StopFlightMovement(
	AEnemyBase& Enemy,
	AEnemyAIController& AIController)
{
	// 남아 있는 NavMesh 이동 요청이 비행 입력과 경쟁하지 않도록 먼저 정리한다.
	AIController.StopMovement();
	if (UCharacterMovementComponent* Movement = Enemy.GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Flying);
	}
}

void ApplyFlightMovement(
	AEnemyBase& Enemy,
	AEnemyAIController& AIController,
	const FVector& Destination,
	const FVector& FacingLocation,
	float AcceptanceRadius,
	float RotationSpeed,
	float DeltaTime)
{
	UCharacterMovementComponent* Movement = Enemy.GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	if (Movement->MovementMode != MOVE_Flying)
	{
		Movement->SetMovementMode(MOVE_Flying);
	}

	const FVector EnemyLocation = Enemy.GetActorLocation();
	const FVector ToDestination = Destination - EnemyLocation;
	const float DistanceToDestination = ToDestination.Size();

	// 전투 이동 방향과 시선 방향을 분리한다. 선회 중에는 원의 접선으로 이동하더라도
	// ControlRotation은 타겟을 계속 향해야 시야 감지와 메시 조준이 끊기지 않는다.
	const FVector ToFacingLocation = FacingLocation - EnemyLocation;
	if (!ToFacingLocation.IsNearlyZero())
	{
		FRotator DesiredRotation = ToFacingLocation.Rotation();
		DesiredRotation.Roll = 0.0f;
		const FRotator NewControlRotation = FMath::RInterpConstantTo(
			AIController.GetControlRotation(),
			DesiredRotation,
			DeltaTime,
			FMath::Max(RotationSpeed, 0.0f)).GetNormalized();
		AIController.SetControlRotation(NewControlRotation);
		Enemy.SetActorRotation(FRotator(0.0f, NewControlRotation.Yaw, 0.0f));
	}

	if (DistanceToDestination
		<= FMath::Max(AcceptanceRadius, 0.0f))
	{
		Movement->StopMovementImmediately();
		return;
	}

	const float MaxSpeed = FMath::Max(Movement->GetMaxSpeed(), 1.0f);
	const float CurrentSpeed = Movement->Velocity.Size();
	const float BrakingDeceleration =
		FMath::Max(Movement->BrakingDecelerationFlying, 1.0f);
	const float StoppingDistance =
		FMath::Square(CurrentSpeed) / (2.0f * BrakingDeceleration);
	const float SlowdownDistance = FMath::Max(
		StoppingDistance * 1.5f,
		FMath::Max(AcceptanceRadius, 1.0f) * 2.0f);
	const float DesiredSpeedAlpha = FMath::Clamp(
		(DistanceToDestination - FMath::Max(AcceptanceRadius, 0.0f))
			/ SlowdownDistance,
		0.0f,
		1.0f);
	const FVector DesiredVelocity =
		ToDestination.GetSafeNormal() * MaxSpeed * DesiredSpeedAlpha;
	const FVector VelocityError = DesiredVelocity - Movement->Velocity;
	const float InputScale = FMath::Clamp(VelocityError.Size() / MaxSpeed, 0.0f, 1.0f);

	// 현재 속도를 목표 속도와 비교해 반대 방향 제동도 함께 입력한다.
	// 목적지 고도가 바뀌어도 기존 수직 속도가 남아 위아래로 지나치는 현상을 줄인다.
	Enemy.AddMovementInput(VelocityError.GetSafeNormal(), InputScale);
}

bool IsCandidateOccupied(
	UWorld& World,
	const AEnemyBase& Enemy,
	const FVector& Candidate,
	float SeparationRadius)
{
	if (SeparationRadius <= 0.0f)
	{
		return false;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyOrbitSeparation), false, &Enemy);
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	World.OverlapMultiByObjectType(
		Overlaps,
		Candidate,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(SeparationRadius),
		QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		const AEnemyBase* OtherEnemy = Cast<AEnemyBase>(Overlap.GetActor());
		if (IsValid(OtherEnemy) && OtherEnemy != &Enemy)
		{
			return true;
		}
	}

	return false;
}
}

FEnemyApproachTargetTask::FEnemyApproachTargetTask()
{
	bShouldStateChangeOnReselect = false;
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif
}

EStateTreeRunStatus FEnemyApproachTargetTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyAIController* AIController = ResolveEnemyAIController(InstanceData.Enemy);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority() || !AIController)
	{
		UE_LOG(
			LogEnemyBattleMovement,
			Warning,
			TEXT("[Battle][Approach] Enter failed. Enemy=%s Authority=%d AIController=%s"),
			*GetNameSafe(InstanceData.Enemy),
			InstanceData.Enemy && InstanceData.Enemy->HasAuthority(),
			*GetNameSafe(AIController));
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.RefreshElapsedTime = InstanceData.TargetRefreshInterval;
	InstanceData.bHasMoveDestination = false;
	InstanceData.bShouldEnterOrbit = false;
	StopFlightMovement(*InstanceData.Enemy, *AIController);
	UE_LOG(
		LogEnemyBattleMovement,
		Display,
		TEXT("[Battle][Approach] Enter. Enemy=%s Target=%s OrbitEnterDistance=%.1f"),
		*GetNameSafe(InstanceData.Enemy),
		*GetNameSafe(ResolveTarget(*AIController, InstanceData.TargetActor)),
		InstanceData.OrbitEnterDistance);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyApproachTargetTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = InstanceData.Enemy;
	AEnemyAIController* AIController = ResolveEnemyAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController || !Enemy->GetWorld())
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* Target = ResolveTarget(*AIController, InstanceData.TargetActor);
	if (!Target)
	{
		UE_LOG(
			LogEnemyBattleMovement,
			Warning,
			TEXT("[Battle][Approach] Target lost. Enemy=%s"),
			*GetNameSafe(Enemy));
		return EStateTreeRunStatus::Failed;
	}

	const FVector EnemyLocation = Enemy->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	InstanceData.TargetDistance = CalculateTacticalDistance(
		EnemyLocation,
		TargetLocation,
		InstanceData.DistanceMode);
	// Approach와 Orbit이 서로 다른 경계를 사용하므로 경계 부근에서 상태가 흔들리지 않는다.
	const bool bWasReadyForOrbit = InstanceData.bShouldEnterOrbit;
	InstanceData.bShouldEnterOrbit =
		InstanceData.TargetDistance <= InstanceData.OrbitEnterDistance;
	if (!bWasReadyForOrbit && InstanceData.bShouldEnterOrbit)
	{
		UE_LOG(
			LogEnemyBattleMovement,
			Display,
			TEXT("[Battle][Approach] Orbit threshold reached. Enemy=%s Target=%s Distance=%.1f Threshold=%.1f"),
			*GetNameSafe(Enemy),
			*GetNameSafe(Target),
			InstanceData.TargetDistance,
			InstanceData.OrbitEnterDistance);
	}

	InstanceData.RefreshElapsedTime += DeltaTime;
	if (InstanceData.RefreshElapsedTime < InstanceData.TargetRefreshInterval)
	{
		if (InstanceData.bHasMoveDestination)
		{
			ApplyFlightMovement(
				*Enemy,
				*AIController,
				InstanceData.MoveDestination,
				TargetLocation,
				InstanceData.AcceptanceRadius,
				InstanceData.RotationSpeed,
				DeltaTime);
		}
		return EStateTreeRunStatus::Running;
	}
	InstanceData.RefreshElapsedTime = 0.0f;

	FVector HorizontalDestination = TargetLocation;
	if (InstanceData.TargetDistance <= InstanceData.OrbitEntryPlanningDistance)
	{
		// 선회 반경에 가까워지면 중심으로 돌진하지 않고 원의 앞쪽 접선 진입점을 향한다.
		EEnemyOrbitDirection EntryDirection = EEnemyOrbitDirection::Left;
		const float DirectionSign = ResolveOrbitDirectionSign(
			*Enemy,
			TargetLocation,
			EntryDirection);
		const FVector Relative = EnemyLocation - TargetLocation;
		const float CurrentAngle = FMath::Atan2(Relative.Y, Relative.X);
		const float EntryAngle = CurrentAngle
			+ FMath::DegreesToRadians(InstanceData.EntryLookAheadDegrees) * DirectionSign;
		const float SafeRadius = FMath::Max(InstanceData.OrbitRadius, 0.0f);
		HorizontalDestination = FVector(
			TargetLocation.X + FMath::Cos(EntryAngle) * SafeRadius,
			TargetLocation.Y + FMath::Sin(EntryAngle) * SafeRadius,
			TargetLocation.Z);
	}

	FVector Candidate;
	if (!ResolveFloorAnchoredLocation(
		*Enemy->GetWorld(),
		HorizontalDestination,
		TargetLocation,
		InstanceData.FlightHeightOffset,
		InstanceData.FloorTraceHalfHeight,
		Candidate))
	{
		if (InstanceData.bHasMoveDestination)
		{
			ApplyFlightMovement(
				*Enemy,
				*AIController,
				InstanceData.MoveDestination,
				TargetLocation,
				InstanceData.AcceptanceRadius,
				InstanceData.RotationSpeed,
				DeltaTime);
		}
		return EStateTreeRunStatus::Running;
	}

	if (ShouldIssueMove(
		Candidate,
		InstanceData.MoveDestination,
		InstanceData.bHasMoveDestination,
		InstanceData.MinRetargetDistance))
	{
		InstanceData.MoveDestination = Candidate;
		InstanceData.bHasMoveDestination = true;
	}

	if (InstanceData.bHasMoveDestination)
	{
		ApplyFlightMovement(
			*Enemy,
			*AIController,
			InstanceData.MoveDestination,
			TargetLocation,
			InstanceData.AcceptanceRadius,
			InstanceData.RotationSpeed,
			DeltaTime);
	}

	return EStateTreeRunStatus::Running;
}

void FEnemyApproachTargetTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (AEnemyAIController* AIController = ResolveEnemyAIController(InstanceData.Enemy))
	{
		if (IsValid(InstanceData.Enemy))
		{
			StopFlightMovement(*InstanceData.Enemy, *AIController);
		}
	}
}

FEnemyOrbitTargetTask::FEnemyOrbitTargetTask()
{
	bShouldStateChangeOnReselect = false;
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif
}

EStateTreeRunStatus FEnemyOrbitTargetTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyAIController* AIController = ResolveEnemyAIController(InstanceData.Enemy);
	AActor* Target = AIController
		? ResolveTarget(*AIController, InstanceData.TargetActor)
		: nullptr;
	if (!InstanceData.Enemy
		|| !InstanceData.Enemy->HasAuthority()
		|| !AIController
		|| !Target)
	{
		UE_LOG(
			LogEnemyBattleMovement,
			Warning,
			TEXT("[Battle][Orbit] Enter failed. Enemy=%s Authority=%d AIController=%s Target=%s"),
			*GetNameSafe(InstanceData.Enemy),
			InstanceData.Enemy && InstanceData.Enemy->HasAuthority(),
			*GetNameSafe(AIController),
			*GetNameSafe(Target));
		return EStateTreeRunStatus::Failed;
	}

	ResolveOrbitDirectionSign(
		*InstanceData.Enemy,
		Target->GetActorLocation(),
		InstanceData.ActiveOrbitDirection);
	// 여러 드론이 같은 프레임에 같은 점을 고르는 현상을 줄이는 가벼운 위상 오프셋이다.
	InstanceData.PhaseOffsetDegrees = FMath::FRandRange(-5.0f, 5.0f);
	InstanceData.RefreshElapsedTime = InstanceData.TargetRefreshInterval;
	InstanceData.RetryElapsedTime = InstanceData.RetryCooldown;
	InstanceData.bHasMoveDestination = false;
	InstanceData.bShouldFallbackToApproach = false;
	InstanceData.bShouldExitOrbit = false;
	StopFlightMovement(*InstanceData.Enemy, *AIController);
	UE_LOG(
		LogEnemyBattleMovement,
		Display,
		TEXT("[Battle][Orbit] Enter. Enemy=%s Target=%s Direction=%s Radius=%.1f ExitDistance=%.1f"),
		*GetNameSafe(InstanceData.Enemy),
		*GetNameSafe(Target),
		*UEnum::GetValueAsString(InstanceData.ActiveOrbitDirection),
		InstanceData.OrbitRadius,
		InstanceData.OrbitExitDistance);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyOrbitTargetTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = InstanceData.Enemy;
	AEnemyAIController* AIController = ResolveEnemyAIController(Enemy);
	if (!Enemy || !Enemy->HasAuthority() || !AIController || !Enemy->GetWorld())
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* Target = ResolveTarget(*AIController, InstanceData.TargetActor);
	if (!Target)
	{
		UE_LOG(
			LogEnemyBattleMovement,
			Warning,
			TEXT("[Battle][Orbit] Target lost. Enemy=%s"),
			*GetNameSafe(Enemy));
		return EStateTreeRunStatus::Failed;
	}

	const FVector EnemyLocation = Enemy->GetActorLocation();
	const FVector OrbitCenter = Target->GetActorLocation();
	InstanceData.TargetDistance = CalculateTacticalDistance(
		EnemyLocation,
		OrbitCenter,
		InstanceData.DistanceMode);
	// 진입 거리보다 큰 이탈 거리를 사용해 선택된 선회 상태를 안정적으로 유지한다.
	const bool bWasReadyToExitOrbit = InstanceData.bShouldExitOrbit;
	InstanceData.bShouldExitOrbit =
		InstanceData.TargetDistance >= InstanceData.OrbitExitDistance;
	if (!bWasReadyToExitOrbit && InstanceData.bShouldExitOrbit)
	{
		UE_LOG(
			LogEnemyBattleMovement,
			Display,
			TEXT("[Battle][Orbit] Approach threshold reached. Enemy=%s Target=%s Distance=%.1f Threshold=%.1f"),
			*GetNameSafe(Enemy),
			*GetNameSafe(Target),
			InstanceData.TargetDistance,
			InstanceData.OrbitExitDistance);
	}

	InstanceData.RefreshElapsedTime += DeltaTime;
	InstanceData.RetryElapsedTime += DeltaTime;
	if (InstanceData.RefreshElapsedTime < InstanceData.TargetRefreshInterval
		|| InstanceData.RetryElapsedTime < InstanceData.RetryCooldown)
	{
		if (InstanceData.bHasMoveDestination)
		{
			ApplyFlightMovement(
				*Enemy,
				*AIController,
				InstanceData.MoveDestination,
				OrbitCenter,
				InstanceData.AcceptanceRadius,
				InstanceData.RotationSpeed,
				DeltaTime);
		}
		return EStateTreeRunStatus::Running;
	}
	InstanceData.RefreshElapsedTime = 0.0f;

	const float DirectionSign =
		InstanceData.ActiveOrbitDirection == EEnemyOrbitDirection::Left ? 1.0f : -1.0f;
	const FVector Relative = EnemyLocation - OrbitCenter;
	const float CurrentAngle = FMath::Atan2(Relative.Y, Relative.X);
	const float SafeRadius = FMath::Max(InstanceData.OrbitRadius, 0.0f);
	const int32 SafeAttemptCount = FMath::Clamp(InstanceData.MaxCandidateAttempts, 1, 8);

	for (int32 AttemptIndex = 0; AttemptIndex < SafeAttemptCount; ++AttemptIndex)
	{
		// 첫 후보가 다른 드론이나 경로 문제로 막히면 같은 방향의 더 앞선 후보를 순서대로 검사한다.
		const float CandidateDegrees = InstanceData.LookAheadDegrees
			+ InstanceData.CandidateAngularStep * AttemptIndex
			+ InstanceData.PhaseOffsetDegrees;
		const float CandidateAngle = CurrentAngle
			+ FMath::DegreesToRadians(CandidateDegrees) * DirectionSign;
		const FVector HorizontalCandidate(
			OrbitCenter.X + FMath::Cos(CandidateAngle) * SafeRadius,
			OrbitCenter.Y + FMath::Sin(CandidateAngle) * SafeRadius,
			OrbitCenter.Z);

		FVector Candidate;
		if (!ResolveFloorAnchoredLocation(
			*Enemy->GetWorld(),
			HorizontalCandidate,
			OrbitCenter,
			InstanceData.FlightHeightOffset,
			InstanceData.FloorTraceHalfHeight,
			Candidate)
			|| IsCandidateOccupied(
				*Enemy->GetWorld(),
				*Enemy,
				Candidate,
				InstanceData.SeparationRadius))
		{
			continue;
		}

		if (!ShouldIssueMove(
			Candidate,
			InstanceData.MoveDestination,
			InstanceData.bHasMoveDestination,
			InstanceData.MinRetargetDistance))
		{
			InstanceData.bShouldFallbackToApproach = false;
			ApplyFlightMovement(
				*Enemy,
				*AIController,
				InstanceData.MoveDestination,
				OrbitCenter,
				InstanceData.AcceptanceRadius,
				InstanceData.RotationSpeed,
				DeltaTime);
			return EStateTreeRunStatus::Running;
		}

		InstanceData.MoveDestination = Candidate;
		InstanceData.bHasMoveDestination = true;
		InstanceData.bShouldFallbackToApproach = false;
		ApplyFlightMovement(
			*Enemy,
			*AIController,
			InstanceData.MoveDestination,
			OrbitCenter,
			InstanceData.AcceptanceRadius,
			InstanceData.RotationSpeed,
			DeltaTime);
		return EStateTreeRunStatus::Running;
	}

	// 후보가 모두 막힌 동안에는 상태 전환용 신호를 올리고, 재시도 주기를 제한한다.
	const bool bWasFallbackRequested = InstanceData.bShouldFallbackToApproach;
	InstanceData.bShouldFallbackToApproach = true;
	InstanceData.RetryElapsedTime = 0.0f;
	if (!bWasFallbackRequested)
	{
		UE_LOG(
			LogEnemyBattleMovement,
			Warning,
			TEXT("[Battle][Orbit] No valid candidate. Enemy=%s Target=%s Attempts=%d"),
			*GetNameSafe(Enemy),
			*GetNameSafe(Target),
			SafeAttemptCount);
	}
	if (InstanceData.bHasMoveDestination)
	{
		ApplyFlightMovement(
			*Enemy,
			*AIController,
			InstanceData.MoveDestination,
			OrbitCenter,
			InstanceData.AcceptanceRadius,
			InstanceData.RotationSpeed,
			DeltaTime);
	}
	return EStateTreeRunStatus::Running;
}

void FEnemyOrbitTargetTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (AEnemyAIController* AIController = ResolveEnemyAIController(InstanceData.Enemy))
	{
		if (IsValid(InstanceData.Enemy))
		{
			StopFlightMovement(*InstanceData.Enemy, *AIController);
		}
	}
}
