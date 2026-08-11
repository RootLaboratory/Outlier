#include "Enemy/EnemyStateTreeTacticalTasks.h"

#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/World.h"

namespace
{
constexpr float MinimumTacticalFlightZ = 150.0f;

bool ResolveFloorAdjustedLocation(
	UWorld& World,
	const FVector& HorizontalLocation,
	float FlightHeightOffset,
	float FloorTraceHalfHeight,
	FVector& OutLocation)
{
	const float TraceHalfHeight = FMath::Max(FloorTraceHalfHeight, 1.0f);
	FHitResult FloorHit;
	const bool bFoundFloor = World.LineTraceSingleByChannel(
		FloorHit,
		HorizontalLocation + FVector::UpVector * TraceHalfHeight,
		HorizontalLocation - FVector::UpVector * TraceHalfHeight,
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
			MinimumTacticalFlightZ));
	return true;
}
}

FEnemyRequestSearchRingSlotTask::FEnemyRequestSearchRingSlotTask()
{
	bShouldStateChangeOnReselect = false;
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif
}

EStateTreeRunStatus FEnemyRequestSearchRingSlotTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	// 슬롯 요청이 실패해도 기본값인 월드 원점이 후속 이동 Task로 전달되지 않게 한다.
	InstanceData.Destination = InstanceData.Enemy->GetActorLocation();
	InstanceData.Destination.Z = FMath::Max(
		InstanceData.Destination.Z,
		MinimumTacticalFlightZ);
	InstanceData.FacingLocation = InstanceData.SearchCenter;

	UEnemyRoomSubsystem* RoomSubsystem =
		InstanceData.Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	if (!RoomSubsystem
		|| !RoomSubsystem->RequestSearchRingSlot(
			InstanceData.Enemy,
			InstanceData.SearchCenter,
			InstanceData.RingRadius,
			InstanceData.FlightHeightOffset,
			InstanceData.ReassignmentDistance,
			InstanceData.FloorTraceHalfHeight,
			InstanceData.Destination))
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

void FEnemyRequestSearchRingSlotTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.Enemy)
	{
		InstanceData.Enemy->ReleaseSearchRingSlot();
	}
}

FEnemyReleaseSearchRingSlotTask::FEnemyReleaseSearchRingSlotTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyReleaseSearchRingSlotTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Enemy || !InstanceData.Enemy->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Enemy->ReleaseSearchRingSlot();
	return EStateTreeRunStatus::Succeeded;
}

FEnemySelectHorizontalOrbitLocationTask::FEnemySelectHorizontalOrbitLocationTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemySelectHorizontalOrbitLocationTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AEnemyBase* Enemy = InstanceData.Enemy;
	if (!Enemy
		|| !Enemy->HasAuthority()
		|| !Enemy->GetWorld()
		|| Enemy->GetRuntimeStat().Type == EEnemyType::Melee)
	{
		return EStateTreeRunStatus::Failed;
	}

	const FVector RelativeLocation = Enemy->GetActorLocation() - InstanceData.OrbitCenter;
	const float CurrentAngle = FMath::Atan2(RelativeLocation.Y, RelativeLocation.X);
	const float DirectionSign =
		InstanceData.OrbitDirection == EEnemyOrbitDirection::Left ? 1.0f : -1.0f;
	const float TargetAngle = CurrentAngle
		+ FMath::DegreesToRadians(InstanceData.AngularStepDegrees) * DirectionSign;
	const float SafeRadius = FMath::Max(InstanceData.OrbitRadius, 0.0f);
	const FVector HorizontalLocation(
		InstanceData.OrbitCenter.X + FMath::Cos(TargetAngle) * SafeRadius,
		InstanceData.OrbitCenter.Y + FMath::Sin(TargetAngle) * SafeRadius,
		InstanceData.OrbitCenter.Z);

	if (!ResolveFloorAdjustedLocation(
		*Enemy->GetWorld(),
		HorizontalLocation,
		InstanceData.FlightHeightOffset,
		InstanceData.FloorTraceHalfHeight,
		InstanceData.Destination))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.FacingLocation = InstanceData.OrbitCenter;
	return EStateTreeRunStatus::Succeeded;
}

FEnemySelectSuppressiveFireLocationTask::FEnemySelectSuppressiveFireLocationTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemySelectSuppressiveFireLocationTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const float SafeSpreadRadius = FMath::Max(InstanceData.SpreadRadius, 0.0f);
	const float RandomAngle = FMath::FRandRange(0.0f, UE_TWO_PI);
	const float RandomRadius = FMath::Sqrt(FMath::FRand()) * SafeSpreadRadius;

	// 위협 사격은 마지막 목격 위치와 같은 높이의 원판 안에서만 분산한다.
	// 구 표면 난수를 사용하면 지면 아래 좌표를 조준해 드론 메시가 급격히 숙여질 수 있다.
	InstanceData.AttackLocation = InstanceData.LastKnownPlayerLocation
		+ FVector(
			FMath::Cos(RandomAngle) * RandomRadius,
			FMath::Sin(RandomAngle) * RandomRadius,
			0.0f);
	InstanceData.AttackLocation.Z = FMath::Max(
		InstanceData.AttackLocation.Z,
		MinimumTacticalFlightZ);
	return EStateTreeRunStatus::Succeeded;
}
