#include "EnemyStateTreeMovementTasks.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogEnemyFlight, Log, All);

TAutoConsoleVariable<int32> CVarEnemyFlightDebug(
	TEXT("outlier.Enemy.FlightDebug"), 0,
	TEXT("Log enemy flight. 0: off, 1: lifecycle/failures, 2: planned route checks and waypoint progress."));

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
	if (Data.bUsePathfinding || Data.bUsePlannedPath)
	{
		UE_LOG(LogEnemyFlight, Log,
			TEXT("[FlightPath] %s Enemy=%s Time=%.3f Room=%s Planned=%d AStar=%d Status=%s Waypoint=%d/%d TravelledCm=%.1f Speed=%.1f Retry=%d RetryElapsed=%.2f Waiting=%d"),
			Phase, *GetNameSafe(Enemy), Enemy && Enemy->GetWorld() ? Enemy->GetWorld()->GetTimeSeconds() : 0.0,
			*Data.PathRoomTag.ToString(), Data.bUsePlannedPath, Data.bUsePathfinding,
			*UEnum::GetValueAsString(Data.PathStatus), Data.WaypointIndex,
			Data.PathWaypoints.Num(), Data.TravelledPathLength, Enemy ? Enemy->GetVelocity().Size() : 0.0,
			Data.RepathAttempts, Data.RetryElapsed, Data.bWaitingForPath);
	}
	if (Data.bConstrainRetreatPath)
	{
		UE_LOG(LogEnemyFlight, Log,
			TEXT("[EnemyRetreatMove] %s Enemy=%s Result=%s Origin=%s Direction=%s CurrentDistance2D=%.1f GoalDistance2D=%.1f Acceptance=%.1f GoalSearchRadius=%.1f"),
			Phase, *GetNameSafe(Enemy), *UEnum::GetValueAsString(Data.MoveResult),
			*Data.RetreatOrigin.ToCompactString(), *Data.RetreatDirection.ToCompactString(),
			FVector::Dist2D(Location, Data.RetreatOrigin), FVector::Dist2D(Data.Destination, Data.RetreatOrigin),
			Data.AcceptanceRadius, Data.GoalSearchRadius);
	}
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
	Data.PathWaypoints.Reset();
	Data.CachedCapsule = nullptr;
	Data.CachedRoomSubsystem = nullptr;
	Data.CachedRoom.Reset();
	Data.bWaitingForPath = false;
	Data.bRepathEpisode = false;
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

void PauseFlyMovement(AEnemyBase& Enemy)
{
	Enemy.ConsumeMovementInputVector();
	if (UCharacterMovementComponent* Movement = Enemy.GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
}

FFlightPathRequest MakeFlightPathRequest(
	const FEnemyFlyToLocationTaskInstanceData& Data, const FVector& Goal)
{
	FFlightPathRequest Request;
	Request.Start = Data.CachedCapsule->GetComponentLocation();
	Request.Goal = Goal;
	Request.SearchBounds = FBox(Request.Start.ComponentMin(Goal), Request.Start.ComponentMax(Goal))
		.ExpandBy(Data.SearchPadding);
	Request.CellSize = Data.PathCellSize;
	Request.Clearance = Data.PathClearance;
	Request.GoalSearchRadius = Data.GoalSearchRadius;
	Request.MaxSearchNodes = Data.MaxSearchNodes;
	Request.MaxCollisionChecks = Data.MaxCollisionChecks;
	Request.MaxSearchMilliseconds = Data.MaxSearchMilliseconds;
	Request.bRequireRoom = true;
	Request.Room = Data.CachedRoom;
	Request.bRequireAwayHalfSpace = Data.bConstrainRetreatPath;
	Request.AwayOrigin = Data.RetreatOrigin + Request.Start - Data.Enemy->GetActorLocation();
	Request.AwayDirection = Data.RetreatDirection;
	return Request;
}

bool IsRetreatFlightConfigurationValid(const FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!Data.bConstrainRetreatPath) return true;
	const double Distance = FVector::Dist2D(Data.Destination, Data.RetreatOrigin);
	return Data.bUsePathfinding && !Data.bUsePlannedPath && Data.GoalSearchRadius == 0.0f
		&& !Data.RetreatOrigin.ContainsNaN() && !Data.RetreatDirection.ContainsNaN()
		&& FMath::IsNearlyZero(Data.RetreatDirection.Z)
		&& FMath::IsNearlyEqual(Data.RetreatDirection.SizeSquared2D(), 1.0, 0.001)
		&& FMath::IsFinite(Data.AcceptanceRadius) && Data.AcceptanceRadius >= 0.0f
		&& Data.AcceptanceRadius <= 50.0f && Distance > 1050.0 && Distance <= 1450.0;
}

bool HasCurrentFlightRoom(const FEnemyFlyToLocationTaskInstanceData& Data)
{
	return IsValid(Data.CachedCapsule) && IsValid(Data.CachedRoomSubsystem)
		&& Data.CachedRoom.IsValid() && Data.PathRoomTag == Data.Enemy->GetDefaultRoomTag()
		&& Data.CachedRoomSubsystem->GetRegisteredRoomVolume(Data.PathRoomTag) == Data.CachedRoom.Get();
}

bool IsRetryableFlightFailure(EFlightPathStatus Status)
{
	return Status == EFlightPathStatus::BlockedEndpoint
		|| Status == EFlightPathStatus::BlockedSegment
		|| Status == EFlightPathStatus::NoPathWithinBounds
		|| Status == EFlightPathStatus::BudgetExceeded;
}

bool RecordFlightPathProgress(FEnemyFlyToLocationTaskInstanceData& Data, float Distance, bool bReachedPoint)
{
	if (!bReachedPoint && Data.BestDistance - Distance < FMath::Max(Data.ProgressDistance, 1.0f))
	{
		return false;
	}
	Data.BestDistance = Distance;
	Data.NoProgressElapsed = 0.0f;
	// 경로 계산 성공이 아니라 실제 경유점 접근/통과가 재시도 한도를 복구한다.
	Data.bRepathEpisode = false;
	Data.RepathAttempts = 0;
	Data.RetryElapsed = 0.0f;
	Data.RetryDelay = 0.0f;
	return true;
}

bool HasFlightRepathLimitExpired(const FEnemyFlyToLocationTaskInstanceData& Data)
{
	// 마지막 허용 탐색이 성공했다면 이동할 수 있다. 횟수 제한은 다음 시도가 필요할 때 적용한다.
	return Data.bRepathEpisode && (Data.RetryElapsed >= Data.RepathTimeout
		|| (Data.bWaitingForPath && Data.RepathAttempts >= Data.MaxRepathAttempts));
}

void WaitForFlightPath(FEnemyFlyToLocationTaskInstanceData& Data)
{
	// 첫 실패부터 시간을 유지한다. 재탐색 성공만으로 한도를 초기화하지 않는다.
	if (!Data.bRepathEpisode)
	{
		Data.RetryElapsed = 0.0f;
		Data.RetryDelay = 0.0f;
		Data.bRepathEpisode = true;
	}
	Data.bWaitingForPath = true;
	Data.PathWaypoints.Reset();
	PauseFlyMovement(*Data.Enemy);
}

bool IsPlannedPathGeometryValid(const TArray<FVector>& Points, const FVector& Start, const FVector& Destination)
{
	if (Points.Num() < 2 || Points.Num() > 11 || Start.ContainsNaN() || Destination.ContainsNaN()) return false;
	if (Points[0].ContainsNaN() || !Points[0].Equals(Start, 25.0)
		|| !Points.Last().Equals(Destination, 1.0)) return false;
	double Length = 0.0;
	FVector Previous = Start;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		if (Points[Index].ContainsNaN()) return false;
		const double SegmentLength = FVector::Distance(Previous, Points[Index]);
		if (SegmentLength <= 0.0 || SegmentLength > 75.0) return false;
		Length += SegmentLength;
		Previous = Points[Index];
	}
	return Length > 0.0 && Length <= 500.01;
}

EStateTreeRunStatus FailPlannedFlight(FEnemyFlyToLocationTaskInstanceData& Data, const TCHAR* Reason)
{
	// 새 장애물이 생기면 같은 끝점으로 A* 우회하지 않는다. StateTree가 새 선회 후보를 요청한다.
	Data.MoveResult = EEnemyFlightMoveResult::Blocked;
	LogFlight(Reason, Data);
	StopFlyTask(Data);
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus StartPlannedFlight(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!HasCurrentFlightRoom(Data)
		|| !FMath::IsFinite(Data.MaxSearchMilliseconds) || Data.MaxSearchMilliseconds <= 0.0f
		|| !FMath::IsFinite(Data.PathClearance) || Data.PathClearance < 0.0f
		|| Data.MaxCollisionChecks < (Data.PlannedWaypoints.Num() - 1) * 4
		|| !IsPlannedPathGeometryValid(Data.PlannedWaypoints, Data.Enemy->GetActorLocation(), Data.Destination))
	{
		Data.PathStatus = EFlightPathStatus::InvalidRequest;
		LogFlight(TEXT("PlannedPathRejected: room/configuration/waypoint geometry"), Data);
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	const FVector Offset = Data.CachedCapsule->GetComponentLocation() - Data.Enemy->GetActorLocation();
	Data.PathWaypoints.Reserve(Data.PlannedWaypoints.Num());
	Data.PathWaypoints.Add(Data.CachedCapsule->GetComponentLocation());
	const double Started = FPlatformTime::Seconds();
	for (int32 Index = 1; Index < Data.PlannedWaypoints.Num(); ++Index)
	{
		FFlightPathRequest Request = MakeFlightPathRequest(Data, Data.PlannedWaypoints[Index] + Offset);
		Request.Start = Data.PathWaypoints.Last();
		Data.PathStatus = FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Data.CachedCapsule, Request);
		if ((FPlatformTime::Seconds() - Started) * 1000.0 >= Data.MaxSearchMilliseconds)
		{
			Data.PathStatus = EFlightPathStatus::BudgetExceeded;
		}
		if (CVarEnemyFlightDebug.GetValueOnGameThread() >= 2)
		{
			UE_LOG(LogEnemyFlight, Log, TEXT("[PlannedFlightSegment] Enemy=%s Segment=%d Start=%s End=%s Status=%s ElapsedMs=%.3f"),
				*GetNameSafe(Data.Enemy), Index, *Request.Start.ToCompactString(), *Request.Goal.ToCompactString(),
				*UEnum::GetValueAsString(Data.PathStatus), (FPlatformTime::Seconds() - Started) * 1000.0);
		}
		if (Data.PathStatus != EFlightPathStatus::Success) return FailPlannedFlight(Data, TEXT("PlannedPathRejected: segment validation"));
		Data.PathWaypoints.Add(Request.Goal);
	}
	Data.WaypointIndex = 1;
	Data.BestDistance = FVector::Distance(Data.PathWaypoints[0], Data.PathWaypoints[1]);
	Data.LastPathPosition = Data.PathWaypoints[0];
	Data.TravelledPathLength = 0.0f;
	Data.SegmentCheckElapsed = 0.0f;
	Data.MoveResult = EEnemyFlightMoveResult::Moving;
	LogFlight(TEXT("PlannedPathStarted"), Data);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus RequestFlightPath(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!HasCurrentFlightRoom(Data))
	{
		Data.PathStatus = EFlightPathStatus::Unavailable;
		Data.MoveResult = EEnemyFlightMoveResult::Invalid;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}

	// StateTree 목적지는 Actor 좌표다. 캡슐 중심과의 차이만 보정해 기존 바인딩을 유지한다.
	// 재진입한 Task도 Pawn에 남은 탐색 간격을 존중한다. 실패 후 Root 반복으로 예산을 우회하지 않는다.
	const double Now = Data.Enemy->GetWorld()->GetTimeSeconds();
	const double RemainingDelay = Data.Enemy->GetNextFlightPathRequestTime() - Now;
	if (RemainingDelay > 0.0)
	{
		WaitForFlightPath(Data);
		Data.RetryDelay = Data.RepathInterval - static_cast<float>(RemainingDelay);
		return EStateTreeRunStatus::Running;
	}
	Data.Enemy->SetNextFlightPathRequestTime(Now + Data.RepathInterval);
	const FVector CapsuleOffset = Data.CachedCapsule->GetComponentLocation() - Data.Enemy->GetActorLocation();
	FFlightPathRequest Request = MakeFlightPathRequest(Data, Data.Destination + CapsuleOffset);
	bool bRoomAdjusted = false;
	if (Data.GoalSearchRadius > 0.0f)
	{
		const float Radius = Data.CachedCapsule->GetScaledCapsuleRadius() + Data.PathClearance;
		const float HalfHeight = Data.CachedCapsule->GetScaledCapsuleHalfHeight() + Data.PathClearance;
		const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(
			Radius, HalfHeight, Data.CachedCapsule->GetComponentQuat());
		FVector RoomGoal;
		if (Data.CachedRoom->TryClampFlightLocation(Request.Goal, Extent, RoomGoal))
		{
			bRoomAdjusted = !RoomGoal.Equals(Request.Goal, 0.01);
			Request = MakeFlightPathRequest(Data, RoomGoal);
		}
	}
	FFlightPathResult Result = FlightPathfinding::FindPath(*Data.Enemy->GetWorld(), *Data.CachedCapsule, Request);
	Data.PathStatus = Result.Status;
	Data.LastDestination = Data.Destination;
	Data.SegmentCheckElapsed = 0.0f;
	if (CVarEnemyFlightDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyFlight, Log,
			TEXT("[FlightPath] Enemy=%s Status=%s Points=%d Nodes=%d Checks=%d Milliseconds=%.3f Retry=%d StartBlocked=%d GoalAdjusted=%d RoomAdjusted=%d ClearanceEscape=%d GoalSearchRadius=%.1f ResolvedGoal=%s"),
			*GetNameSafe(Data.Enemy), *UEnum::GetValueAsString(Result.Status), Result.Waypoints.Num(),
			Result.SearchNodes, Result.CollisionChecks, Result.ElapsedMilliseconds, Data.RepathAttempts,
			Result.bStartBlocked, Result.bGoalAdjusted, bRoomAdjusted, Result.bUsedClearanceEscape, Data.GoalSearchRadius,
			Result.Waypoints.IsEmpty() ? TEXT("None") : *Result.Waypoints.Last().ToString());
	}
	if (Result.Status == EFlightPathStatus::Success)
	{
		Data.PathWaypoints = MoveTemp(Result.Waypoints);
		Data.WaypointIndex = Data.PathWaypoints.Num() > 1 ? 1 : 0;
		Data.bWaitingForPath = false;
		Data.NoProgressElapsed = 0.0f;
		Data.BestDistance = FVector::Distance(Request.Start, Data.PathWaypoints[Data.WaypointIndex]);
		return EStateTreeRunStatus::Running;
	}
	if (IsRetryableFlightFailure(Result.Status))
	{
		WaitForFlightPath(Data);
		return EStateTreeRunStatus::Running;
	}
	Data.MoveResult = EEnemyFlightMoveResult::Invalid;
	StopFlyTask(Data);
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus TickFlightPath(FEnemyFlyToLocationTaskInstanceData& Data, float DeltaTime)
{
	// 전체 이동 제한 -> 목적지 변경 -> 제한된 재탐색 -> 구간 검증 -> 이동 입력 순서다.
	Data.MoveElapsed += DeltaTime;
	Data.SegmentCheckElapsed += DeltaTime;
	if (Data.MoveTimeout > 0.0f && Data.MoveElapsed >= Data.MoveTimeout)
	{
		Data.MoveResult = EEnemyFlightMoveResult::TimedOut;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bRepathEpisode)
	{
		Data.RetryElapsed += DeltaTime;
	}
	if (HasFlightRepathLimitExpired(Data))
	{
		Data.MoveResult = EEnemyFlightMoveResult::Blocked;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!IsValid(Data.CachedCapsule) || !Data.CachedRoom.IsValid()
		|| Data.PathRoomTag != Data.Enemy->GetDefaultRoomTag())
	{
		Data.PathStatus = EFlightPathStatus::Unavailable;
		Data.MoveResult = EEnemyFlightMoveResult::Invalid;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!Data.LastDestination.Equals(Data.Destination, FMath::Max(Data.DestinationChangeDistance, 0.0f)))
	{
		if (Data.bUsePlannedPath) return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: destination changed"));
		WaitForFlightPath(Data);
	}
	if (Data.bWaitingForPath)
	{
		Data.RetryDelay += DeltaTime;
		if (HasFlightRepathLimitExpired(Data))
		{
			Data.MoveResult = EEnemyFlightMoveResult::Blocked;
			StopFlyTask(Data);
			return EStateTreeRunStatus::Failed;
		}
		if (Data.RetryDelay >= Data.RepathInterval)
		{
			Data.RetryDelay = 0.0f;
			++Data.RepathAttempts;
			return RequestFlightPath(Data);
		}
		return EStateTreeRunStatus::Running;
	}

	const FVector Position = Data.CachedCapsule->GetComponentLocation();
	if (Data.bConstrainRetreatPath && FVector::DotProduct(
		Data.Enemy->GetActorLocation() - Data.RetreatOrigin, Data.RetreatDirection) < 0.0)
	{
		Data.PathStatus = EFlightPathStatus::BlockedSegment;
		Data.MoveResult = EEnemyFlightMoveResult::Blocked;
		LogFlight(TEXT("RetreatInterrupted: outside captured target half-space"), Data);
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!Data.PathWaypoints.IsValidIndex(Data.WaypointIndex))
	{
		Data.MoveResult = EEnemyFlightMoveResult::Invalid;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	FVector NextPoint = Data.PathWaypoints[Data.WaypointIndex];
	const bool bFinalPoint = Data.WaypointIndex == Data.PathWaypoints.Num() - 1;
	const float Radius = Data.bUsePlannedPath ? FMath::Min(10.0f, FMath::Max(Data.AcceptanceRadius, 1.0f))
		: bFinalPoint ? FMath::Max(Data.AcceptanceRadius, 0.0f)
		: FMath::Min(10.0f, Data.PathCellSize * 0.1f);
	const bool bReachedPoint = FVector::DistSquared(Position, NextPoint) <= FMath::Square(Radius);
	if (Data.bUsePlannedPath)
	{
		Data.TravelledPathLength += FVector::Distance(Data.LastPathPosition, Position);
		Data.LastPathPosition = Position;
		if (Data.TravelledPathLength >= 500.0f && !(bReachedPoint && bFinalPoint))
			return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: 500cm travel limit"));
	}

	// 중간점 허용 반경 때문에 모서리를 가로지르지 않도록 다음 구간을 먼저 검사한다.
	if (bReachedPoint && !bFinalPoint)
	{
		NextPoint = Data.PathWaypoints[Data.WaypointIndex + 1];
	}
	if (bReachedPoint || Data.SegmentCheckElapsed >= Data.PathCheckInterval)
	{
		Data.SegmentCheckElapsed = 0.0f;
		Data.PathStatus = HasCurrentFlightRoom(Data)
			? FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Data.CachedCapsule,
				MakeFlightPathRequest(Data, NextPoint)) : EFlightPathStatus::Unavailable;
		if (Data.PathStatus != EFlightPathStatus::Success)
		{
			if (IsRetryableFlightFailure(Data.PathStatus))
			{
				if (Data.bUsePlannedPath) return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: segment blocked or budget exceeded"));
				WaitForFlightPath(Data);
				return EStateTreeRunStatus::Running;
			}
			Data.MoveResult = EEnemyFlightMoveResult::Invalid;
			StopFlyTask(Data);
			return EStateTreeRunStatus::Failed;
		}
	}
	if (bReachedPoint && bFinalPoint)
	{
		Data.MoveResult = EEnemyFlightMoveResult::Arrived;
		LogFlight(TEXT("PathArrived"), Data);
		StopFlyTask(Data);
		return EStateTreeRunStatus::Succeeded;
	}
	if (bReachedPoint)
	{
		// 경유점 통과 시에만 출력한다. 매 프레임 로그가 이동 성능을 가리지 않게 한다.
		if (CVarEnemyFlightDebug.GetValueOnGameThread() >= 2) LogFlight(TEXT("WaypointReached"), Data);
		++Data.WaypointIndex;
	}

	const float Distance = FVector::Distance(Position, NextPoint);
	Data.NoProgressElapsed += DeltaTime;
	RecordFlightPathProgress(Data, Distance, bReachedPoint);
	const float NoProgressLimit = Data.bUsePlannedPath && Data.NoProgressTimeout <= 0.0f ? 2.0f : Data.NoProgressTimeout;
	if (NoProgressLimit > 0.0f && Data.NoProgressElapsed >= NoProgressLimit)
	{
		if (Data.bUsePlannedPath) return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: no progress"));
		WaitForFlightPath(Data);
		return EStateTreeRunStatus::Running;
	}
	if (Data.bRotateTowardDestination)
	{
		RotateTowardLocation(*Data.Enemy, *Data.CachedController, NextPoint,
			Data.RotationSpeed, 0.0f, DeltaTime);
	}
	ApplyFlyTaskSpeedMultiplier(*Data.Enemy, Data.SpeedMultiplier);
	if (Data.bConstrainRetreatPath)
	{
		// 경유점/최종점의 뒤까지 관성으로 지나치지 않는다. 가감속 커브는 후속 Slice에서 적용한다.
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
		{
			Movement->MaxFlySpeed = FMath::Min(Movement->MaxFlySpeed, Distance / FMath::Max(DeltaTime, UE_SMALL_NUMBER));
			Movement->Velocity = Movement->Velocity.GetClampedToMaxSize(Movement->MaxFlySpeed);
		}
	}
	if (Data.bUsePlannedPath)
	{
		// 짧은 경유점을 한 프레임에 지나치지 않도록 속도를 제한한다. 가감속 커브는 별도 Slice다.
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
		{
			const float Remaining = FMath::Max(500.0f - Data.TravelledPathLength, 0.0f);
			Movement->MaxFlySpeed = FMath::Min(Movement->MaxFlySpeed,
				FMath::Min(Distance, Remaining) / FMath::Max(DeltaTime, UE_SMALL_NUMBER));
			Movement->Velocity = Movement->Velocity.GetClampedToMaxSize(Movement->MaxFlySpeed);
		}
	}
	Data.Enemy->AddMovementInput((NextPoint - Position).GetSafeNormal());
	return EStateTreeRunStatus::Running;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightPathProgressTest,
	"Outlier.Enemy.Flight.PathProgress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightPathProgressTest::RunTest(const FString& Parameters)
{
	FEnemyFlyToLocationTaskInstanceData Data;
	TestFalse(TEXT("Existing assets retain direct movement by default"), Data.bUsePathfinding);
	TestEqual(TEXT("Path movement resolves nearby approach points by default"), Data.GoalSearchRadius, 200.0f);
	Data.bRepathEpisode = true;
	Data.RepathAttempts = 2;
	Data.RetryElapsed = 1.5f;
	Data.NoProgressElapsed = 0.75f;
	Data.BestDistance = 150.0f;
	Data.MoveElapsed = 5.0f;
	TestFalse(TEXT("No physical progress preserves retry episode"), RecordFlightPathProgress(Data, 150.0f, false));
	TestEqual(TEXT("No progress preserves attempt count"), Data.RepathAttempts, 2);
	TestEqual(TEXT("No progress preserves retry deadline"), Data.RetryElapsed, 1.5f);
	// 우회 중 최종 목적지와 멀어져도 다음 경유점에 접근했다면 정상 진행이다.
	TestTrue(TEXT("Approaching waypoint counts as progress"), RecordFlightPathProgress(Data, 125.0f, false));
	TestFalse(TEXT("Physical progress clears retry episode"), Data.bRepathEpisode);
	TestEqual(TEXT("Physical progress restores retry allowance"), Data.RepathAttempts, 0);
	TestEqual(TEXT("Progress does not reset total movement deadline"), Data.MoveElapsed, 5.0f);
	Data.bRepathEpisode = true;
	Data.RepathAttempts = 1;
	TestTrue(TEXT("Passing a short segment counts as progress"), RecordFlightPathProgress(Data, 300.0f, true));
	TestEqual(TEXT("New waypoint resets distance baseline"), Data.BestDistance, 300.0f);
	TestTrue(TEXT("Budget exhaustion is retryable"), IsRetryableFlightFailure(EFlightPathStatus::BudgetExceeded));
	TestFalse(TEXT("Unregistered room is not a collision retry"), IsRetryableFlightFailure(EFlightPathStatus::Unavailable));
	TestFalse(TEXT("Invalid settings fail without retries"), IsRetryableFlightFailure(EFlightPathStatus::InvalidRequest));
	TestTrue(TEXT("Dynamic segment blockage can be retried"), IsRetryableFlightFailure(EFlightPathStatus::BlockedSegment));
	Data.bRepathEpisode = true;
	Data.bWaitingForPath = true;
	Data.MaxRepathAttempts = 2;
	Data.RepathAttempts = 1;
	TestFalse(TEXT("Remaining attempts allow another search"), HasFlightRepathLimitExpired(Data));
	Data.RepathAttempts = 2;
	TestTrue(TEXT("Attempt limit stops further searches"), HasFlightRepathLimitExpired(Data));
	Data.bWaitingForPath = false;
	TestFalse(TEXT("Last successful retry still permits movement"), HasFlightRepathLimitExpired(Data));
	Data.RetryElapsed = Data.RepathTimeout;
	TestTrue(TEXT("No physical progress cannot extend retry deadline"), HasFlightRepathLimitExpired(Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRetreatFlightConfigurationTest,
	"Outlier.Enemy.Flight.RetreatConfiguration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRetreatFlightConfigurationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlyToLocationTaskInstanceData Data;
	TestTrue(TEXT("Other movements do not require retreat inputs"), IsRetreatFlightConfigurationValid(Data));
	Data.bConstrainRetreatPath = true;
	TestFalse(TEXT("Direct flight cannot bypass retreat path safety"), IsRetreatFlightConfigurationValid(Data));
	Data.bUsePathfinding = true;
	Data.GoalSearchRadius = 0.0f;
	Data.RetreatDirection = FVector::ForwardVector;
	Data.Destination = FVector(1250, 0, 250);
	TestTrue(TEXT("Retreat uses exact bounded goal and normalized horizontal direction"), IsRetreatFlightConfigurationValid(Data));
	Data.GoalSearchRadius = 200.0f;
	TestFalse(TEXT("Retreat endpoint cannot be adjusted into another distance band"), IsRetreatFlightConfigurationValid(Data));
	Data.GoalSearchRadius = 0.0f;
	Data.AcceptanceRadius = 100.0f;
	TestFalse(TEXT("Large acceptance radius cannot consume retreat boundary margin"), IsRetreatFlightConfigurationValid(Data));
	Data.AcceptanceRadius = 50.0f;
	Data.RetreatDirection = FVector::ZeroVector;
	TestFalse(TEXT("Missing binding does not disable safety"), IsRetreatFlightConfigurationValid(Data));
	Data.RetreatDirection = FVector::ForwardVector;
	Data.Destination.X = 1000.0;
	TestFalse(TEXT("Exactly ten metre goal is forbidden"), IsRetreatFlightConfigurationValid(Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPlannedFlightGeometryTest,
	"Outlier.Enemy.Flight.PlannedGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPlannedFlightGeometryTest::RunTest(const FString& Parameters)
{
	const FVector Start(100, 200, 300);
	TArray<FVector> Points;
	Points.Add(Start);
	for (int32 Index = 1; Index <= 10; ++Index) Points.Add(Start + FVector(Index * 50, 0, 0));
	TestTrue(TEXT("Ten short segments permit five metre route"),
		IsPlannedPathGeometryValid(Points, Start, Points.Last()));
	TestFalse(TEXT("Endpoint cannot substitute a different destination"),
		IsPlannedPathGeometryValid(Points, Start, Points.Last() + FVector(10, 0, 0)));
	TestFalse(TEXT("Moved start cannot accept stale geometry"),
		IsPlannedPathGeometryValid(Points, Start + FVector(30, 0, 0), Points.Last()));
	Points.Add(Start + FVector(550, 0, 0));
	TestFalse(TEXT("More than five metres is rejected"), IsPlannedPathGeometryValid(Points, Start, Points.Last()));
	Points = {Start, Start + FVector(300, 0, 0)};
	TestFalse(TEXT("Destination-only shortcut cannot replace an arc"), IsPlannedPathGeometryValid(Points, Start, Points.Last()));
	TestFalse(TEXT("Existing assets do not opt into planned movement"), FEnemyFlyToLocationTaskInstanceData().bUsePlannedPath);
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
	InstanceData.PathWaypoints.Reset();
	InstanceData.CachedRoom.Reset();
	InstanceData.CachedCapsule = nullptr;
	InstanceData.CachedRoomSubsystem = nullptr;
	InstanceData.bWaitingForPath = false;
	InstanceData.bRepathEpisode = false;
	InstanceData.RepathAttempts = 0;
	InstanceData.RetryElapsed = 0.0f;
	InstanceData.RetryDelay = 0.0f;
	InstanceData.PathStatus = EFlightPathStatus::Unavailable;
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
	if (InstanceData.Destination.ContainsNaN() || !IsRetreatFlightConfigurationValid(InstanceData))
	{
		InstanceData.PathStatus = EFlightPathStatus::InvalidRequest;
		LogFlight(TEXT("Rejected: invalid destination or retreat configuration"), InstanceData);
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (InstanceData.bUsePathfinding || InstanceData.bUsePlannedPath)
	{
		if (InstanceData.bUsePathfinding && InstanceData.bUsePlannedPath)
		{
			InstanceData.PathStatus = EFlightPathStatus::InvalidRequest;
			StopFlyTask(InstanceData);
			return EStateTreeRunStatus::Failed;
		}
		if (!FMath::IsFinite(InstanceData.RepathInterval) || InstanceData.RepathInterval <= 0.0f
			|| !FMath::IsFinite(InstanceData.RepathTimeout) || InstanceData.RepathTimeout <= 0.0f
			|| !FMath::IsFinite(InstanceData.PathCheckInterval) || InstanceData.PathCheckInterval <= 0.0f
			|| !FMath::IsFinite(InstanceData.SearchPadding) || InstanceData.SearchPadding <= 0.0f
			|| !FMath::IsFinite(InstanceData.DestinationChangeDistance) || InstanceData.DestinationChangeDistance < 0.0f
			|| InstanceData.MaxRepathAttempts < 0)
		{
			InstanceData.PathStatus = EFlightPathStatus::InvalidRequest;
			StopFlyTask(InstanceData);
			return EStateTreeRunStatus::Failed;
		}
		InstanceData.CachedCapsule = Enemy->GetCapsuleComponent();
		InstanceData.CachedRoomSubsystem = Enemy->GetWorld()->GetSubsystem<URoomCombatSubsystem>();
		InstanceData.PathRoomTag = Enemy->GetDefaultRoomTag();
		InstanceData.CachedRoom = InstanceData.CachedRoomSubsystem
			? InstanceData.CachedRoomSubsystem->GetRegisteredRoomVolume(InstanceData.PathRoomTag) : nullptr;
		InstanceData.MoveResult = EEnemyFlightMoveResult::Moving;
		if (InstanceData.bUsePlannedPath) return StartPlannedFlight(InstanceData);
		return RequestFlightPath(InstanceData);
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

	// Keep the task resumable if hacking is cancelled before the state transition.
	if (Enemy->IsPossessionInProgress())
	{
		Enemy->ConsumeMovementInputVector();
		if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		return EStateTreeRunStatus::Running;
	}

	if (InstanceData.bUsePathfinding || InstanceData.bUsePlannedPath)
	{
		return TickFlightPath(InstanceData, DeltaTime);
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

namespace
{
bool IsStationaryFireConfigurationValid(const FEnemyStationaryFireTaskInstanceData& Data)
{
	return FMath::IsFinite(Data.MinDuration) && FMath::IsFinite(Data.MaxDuration)
		&& Data.MinDuration > 0.0f && Data.MaxDuration >= Data.MinDuration;
}

bool CanHoldStationaryFire(const FEnemyStationaryFireTaskInstanceData& Data)
{
	const AEnemyBase* Enemy = Data.Enemy;
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !IsValid(Data.CachedController)) return false;
	if (Enemy->GetController() != Data.CachedController || Data.CachedController->GetPawn() != Enemy) return false;
	if (Enemy->IsDead() || !Enemy->CanUseEnemyPerception() || Enemy->IsPossessionInProgress()
		|| Enemy->GetCombatState() == EEnemyCombatState::Stun || !Enemy->IsPlayerCurrentlyVisible()) return false;
	return Data.CapturedTarget.IsValid() && Data.CachedController->GetCachedVisibleTarget() == Data.CapturedTarget.Get()
		&& Enemy->GetPoolGameplayGeneration() == Data.CapturedGeneration
		&& Enemy->GetPoolLeaseSerial() == Data.CapturedLease;
}

const TCHAR* GetStationaryFireReason(const FEnemyStationaryFireTaskInstanceData& Data)
{
	if (!IsStationaryFireConfigurationValid(Data)) return TEXT("InvalidDurationRange");
	const AEnemyBase* Enemy = Data.Enemy;
	if (!IsValid(Enemy)) return TEXT("InvalidEnemy");
	if (!Enemy->HasAuthority()) return TEXT("NoAuthority");
	if (!IsValid(Data.CachedController) || Enemy->GetController() != Data.CachedController
		|| Data.CachedController->GetPawn() != Enemy) return TEXT("ControllerChanged");
	if (Enemy->IsDead()) return TEXT("Dead");
	if (!Enemy->CanUseEnemyPerception()) return TEXT("PerceptionDisabled");
	if (Enemy->IsPossessionInProgress()) return TEXT("PossessionInProgress");
	if (Enemy->GetCombatState() == EEnemyCombatState::Stun) return TEXT("Stun");
	if (!Enemy->IsPlayerCurrentlyVisible()) return TEXT("SightLost");
	if (!Data.CapturedTarget.IsValid()
		|| Data.CachedController->GetCachedVisibleTarget() != Data.CapturedTarget.Get()) return TEXT("TargetChangedOrInvalid");
	if (Enemy->GetPoolGameplayGeneration() != Data.CapturedGeneration
		|| Enemy->GetPoolLeaseSerial() != Data.CapturedLease) return TEXT("PoolLifetimeChanged");
	return TEXT("Valid");
}

void LogStationaryFire(const TCHAR* Phase, const FEnemyStationaryFireTaskInstanceData& Data,
	const FStateTreeTransitionResult* Transition = nullptr)
{
	if (CVarEnemyFlightDebug.GetValueOnGameThread() == 0) return;
	UE_LOG(LogEnemyFlight, Log, TEXT("[StationaryFire] %s Enemy=%s Time=%.3f Target=%s Reason=%s Duration=%.2f Elapsed=%.2f Speed=%.1f Current=%s TargetState=%s RunStatus=%s"),
		Phase, *GetNameSafe(Data.Enemy),
		IsValid(Data.Enemy) && Data.Enemy->GetWorld() ? Data.Enemy->GetWorld()->GetTimeSeconds() : 0.0,
		*GetNameSafe(Data.CapturedTarget.Get()), GetStationaryFireReason(Data), Data.SelectedDuration, Data.Elapsed,
		IsValid(Data.Enemy) ? Data.Enemy->GetVelocity().Size() : 0.0,
		Transition ? *Transition->CurrentState.Describe() : TEXT("None"),
		Transition ? *Transition->TargetState.Describe() : TEXT("None"),
		Transition ? *UEnum::GetValueAsString(Transition->CurrentRunStatus) : TEXT("None"));
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyStationaryFireConfigurationTest,
	"Outlier.Enemy.Flight.StationaryFire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyStationaryFireConfigurationTest::RunTest(const FString& Parameters)
{
	FEnemyStationaryFireTaskInstanceData Data;
	TestTrue(TEXT("Default hold range is valid"), IsStationaryFireConfigurationValid(Data));
	TestEqual(TEXT("Diagnostics identify a missing enemy"), FString(GetStationaryFireReason(Data)), FString(TEXT("InvalidEnemy")));
	TestEqual(TEXT("Minimum pause is half a second"), Data.MinDuration, 0.5f);
	TestEqual(TEXT("Maximum pause is one second"), Data.MaxDuration, 1.0f);
	Data.MaxDuration = 0.25f;
	TestFalse(TEXT("Reversed duration range is rejected"), IsStationaryFireConfigurationValid(Data));
	TestEqual(TEXT("Diagnostics identify invalid duration before enemy validation"),
		FString(GetStationaryFireReason(Data)), FString(TEXT("InvalidDurationRange")));
	Data.MinDuration = 0.0f;
	TestFalse(TEXT("Immediate completion loop is rejected"), IsStationaryFireConfigurationValid(Data));
	TestFalse(TEXT("Missing enemy cannot hold movement"), CanHoldStationaryFire(Data));
	return true;
}
#endif

FEnemyStationaryFireTask::FEnemyStationaryFireTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = true;
}

EStateTreeRunStatus FEnemyStationaryFireTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Enemy = ResolveEnemy(Context, Data.Enemy);
	Data.Elapsed = 0.0f;
	Data.SelectedDuration = 0.0f;
	Data.CachedController = IsValid(Data.Enemy) ? Cast<AEnemyAIController>(Data.Enemy->GetController()) : nullptr;
	Data.CapturedTarget = IsValid(Data.CachedController) ? Data.CachedController->GetCachedVisibleTarget() : nullptr;
	Data.CapturedGeneration = IsValid(Data.Enemy) ? Data.Enemy->GetPoolGameplayGeneration() : 0;
	Data.CapturedLease = IsValid(Data.Enemy) ? Data.Enemy->GetPoolLeaseSerial() : 0;
	if (!IsStationaryFireConfigurationValid(Data) || !CanHoldStationaryFire(Data))
	{
		LogStationaryFire(TEXT("Rejected"), Data, &Transition);
		return EStateTreeRunStatus::Failed;
	}
	// 진입 시 한 번만 시간을 정한다. 이동을 소유하지 않고 조준/사격은 병렬 Task에 맡긴다.
	Data.SelectedDuration = FMath::FRandRange(Data.MinDuration, Data.MaxDuration);
	Data.CachedController->StopMovement();
	PauseFlyMovement(*Data.Enemy);
	LogStationaryFire(TEXT("Started"), Data, &Transition);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyStationaryFireTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	// 소유권/시야가 바뀌면 새 조종자의 이동에 손대지 않고 상위 전환을 따른다.
	if (!CanHoldStationaryFire(Data))
	{
		LogStationaryFire(TEXT("Interrupted"), Data);
		return EStateTreeRunStatus::Failed;
	}
	PauseFlyMovement(*Data.Enemy);
	Data.Elapsed += DeltaTime;
	if (Data.Elapsed < Data.SelectedDuration) return EStateTreeRunStatus::Running;
	LogStationaryFire(TEXT("Completed"), Data);
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyStationaryFireTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	// 상위 전환으로 Tick 없이 종료된 경우도 남긴다. 이동이나 사격 상태는 변경하지 않는다.
	LogStationaryFire(TEXT("Exit"), Context.GetInstanceData(*this), &Transition);
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
	// 증원은 직접 감지하지 못해도 같은 방의 타깃 공유를 받으면 즉시 Battle로 합류한다.
	// 청각으로 Alert에 들어온 경우도 이 조건을 통과할 수 있다. 소리만 듣고 합류하는 조건은 아니다.
	if (Enemy->IsPoolManaged() && Enemy->HasSharedTargetContact())
	{
		const bool bCommitted = Enemy->CommitAlertToCombat();
		UE_LOG(LogTemp, Display,
			TEXT("[Reinforcement] Enemy=%s Stage=AlertSharedTargetCommit Generation=%d Lease=%d Committed=%d State=%s"),
			*GetNameSafe(Enemy), Enemy->GetPoolGameplayGeneration(),
			Enemy->GetPoolLeaseSerial(), bCommitted,
			*UEnum::GetValueAsString(Enemy->GetCombatState()));
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
		// 공유 정보로 합류하지 않은 적은 직접 보이는 시간을 채워 Combat을 확정한다.
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
		{FRotator(0.0f, -60.0f, 0.0f), 0.5f},
		{FRotator::ZeroRotator, 0.2f},
		{FRotator(0.0f, 60.0f, 0.0f), 0.5f},
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
	InstanceData.BaseControlRotation.Pitch = 0.0f;
	InstanceData.BaseControlRotation.Roll = 0.0f;
	AIController->SetControlRotation(InstanceData.BaseControlRotation);
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
	// Existing StateTree steps may still contain authored pitch offsets.
	DesiredRotation.Pitch = 0.0f;
	DesiredRotation.Roll = 0.0f;

	FRotator NewRotation = FMath::RInterpConstantTo(
		AIController->GetControlRotation(),
		DesiredRotation,
		DeltaTime,
		FMath::Max(InstanceData.RotationSpeed, 0.0f)).GetNormalized();
	NewRotation.Pitch = 0.0f;
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
