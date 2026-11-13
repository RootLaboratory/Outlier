#include "EnemyStateTreeMovementTasks.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/AutoTurret.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
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
	if (CVarEnemyFlightDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
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
	if (Data.bUsePathfinding || (Data.bUsePlannedPath && !Data.bCombatNavigationPath))
	{
		UE_LOG(LogEnemyFlight, Log,
			TEXT("[FlightPath] %s Enemy=%s Time=%.3f Room=%s Planned=%d AStar=%d Status=%s Waypoint=%d/%d TravelledCm=%.1f Speed=%.1f Retry=%d RetryElapsed=%.2f Waiting=%d"),
			Phase, *GetNameSafe(Enemy), Enemy && Enemy->GetWorld() ? Enemy->GetWorld()->GetTimeSeconds() : 0.0,
			*Data.PathRoomTag.ToString(), (Data.bUsePlannedPath && !Data.bCombatNavigationPath), Data.bUsePathfinding,
			*UEnum::GetValueAsString(Data.PathStatus), Data.WaypointIndex,
			Data.PathWaypoints.Num(), Data.TravelledPathLength, Enemy ? Enemy->GetVelocity().Size() : 0.0,
			Data.RepathAttempts, Data.RetryElapsed, Data.bWaitingForPath);
	}
	if (Data.bConstrainRetreatPath)
	{
		AActor* ObservedTarget = Enemy ? Enemy->GetTrackedTarget() : nullptr;
		// Origin은 계획 시점 스냅샷이다. 추격하는 플레이어와의 실제 관측 간격은 별도로 기록한다.
		const double ObservedDistance = Enemy && IsValid(ObservedTarget)
			? FVector::Dist2D(Location, Enemy->GetCombatNavigationTargetLocation(ObservedTarget)) : -1.0;
		UE_LOG(LogEnemyFlight, Log,
			TEXT("[EnemyRetreatMove] %s Enemy=%s Result=%s Origin=%s Direction=%s CurrentDistance2D=%.1f GoalDistance2D=%.1f Acceptance=%.1f GoalSearchRadius=%.1f ShortAllowed=%d StartDistance2D=%.1f Gain=%.1f Now=%.3f Lease=%d Generation=%d Target=%s ObservedDistance2D=%.1f Episode=%llu"),
			Phase, *GetNameSafe(Enemy), *UEnum::GetValueAsString(Data.MoveResult),
			*Data.RetreatOrigin.ToCompactString(), *Data.RetreatDirection.ToCompactString(),
			FVector::Dist2D(Location, Data.RetreatOrigin), FVector::Dist2D(Data.Destination, Data.RetreatOrigin),
			Data.AcceptanceRadius, Data.GoalSearchRadius, Data.bAllowShortRetreat,
			FVector::Dist2D(Data.RetreatStart, Data.RetreatOrigin),
			FVector::Dist2D(Location, Data.RetreatOrigin) - FVector::Dist2D(Data.RetreatStart, Data.RetreatOrigin),
			Enemy && Enemy->GetWorld() ? Enemy->GetWorld()->GetTimeSeconds() : 0.0,
			Enemy ? Enemy->GetPoolLeaseSerial() : 0, Enemy ? Enemy->GetPoolGameplayGeneration() : 0,
			*GetNameSafe(ObservedTarget), ObservedDistance, Data.CapturedCombatTacticalSerial);
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

bool ShouldYieldRotationToAttack(const AAIController& AIController)
{
	const AEnemyAIController* EnemyController = Cast<AEnemyAIController>(&AIController);
	const AEnemyBase* Enemy = Cast<AEnemyBase>(AIController.GetPawn());
	return (EnemyController && EnemyController->IsAttackAimActive())
		|| (Enemy && (Enemy->IsHitDirectionConfirmationActive() || Enemy->IsNoiseDirectionConfirmationActive()));
}

bool ShouldYieldPassiveRotation(const AEnemyBase& Enemy, const AAIController& AIController)
{
	const AAutoTurret* Turret = Cast<AAutoTurret>(&Enemy);
	return ShouldYieldRotationToAttack(AIController)
		|| (Enemy.HasHitResponse() && !Enemy.IsLocationSearchActive())
		|| (Turret && Turret->IsTurretGuardRotationActive())
		|| Enemy.GetCombatState() == EEnemyCombatState::Stun
		|| Enemy.IsDead() || Enemy.IsEnemyPossessed();
}

// 이동/전투 회전의 속도 계약은 유지하고, 대기 탐색에만 목표 근처 감속을 적용한다.
FRotator GetPassiveRotation(const FRotator& Current, const FRotator& Desired,
	float MaxSpeed, float SlowdownRate, float AngleTolerance, float DeltaTime)
{
	const float SafeSpeed = FMath::IsFinite(MaxSpeed) ? FMath::Max(0.0f, MaxSpeed) : 0.0f;
	const float SafeTime = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0f, DeltaTime) : 0.0f;
	const float Tolerance = FMath::IsFinite(AngleTolerance) ? FMath::Max(0.0f, AngleTolerance) : 1.0f;
	const float Rate = FMath::IsFinite(SlowdownRate) ? FMath::Max(0.0f, SlowdownRate) : 6.0f;
	const float Error = FMath::Max(
		FMath::Abs(FMath::FindDeltaAngleDegrees(Current.Yaw, Desired.Yaw)),
		FMath::Abs(FMath::FindDeltaAngleDegrees(Current.Pitch, Desired.Pitch)));
	if (Error <= Tolerance || SafeSpeed == 0.0f || SafeTime == 0.0f)
	{
		return Current;
	}
	// 오차가 작아져도 완료 허용 범위에는 도달한다. 속도 0은 의도된 정지 설정이다.
	const float Speed = FMath::Min(SafeSpeed, FMath::Max(FMath::Max(1.0f, Tolerance * 2.0f), Error * Rate));
	FRotator Next = FMath::RInterpConstantTo(Current, Desired, SafeTime, Speed).GetNormalized();
	Next.Roll = 0.0f;
	return Next;
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
	// 병렬 MaintainFacing/이동 Task가 공격 조준을 본체 기준 회전으로 덮지 않게 한다.
	if (ShouldYieldRotationToAttack(AIController))
	{
		return false;
	}
	// 공통 Alert에서도 고정형 본체를 돌리지 않는다. 헤드의 기존 각도 제한과 도달 판정을 사용한다.
	if (AAutoTurret* Turret = Cast<AAutoTurret>(&Enemy))
	{
		return Turret->UpdateTurretAimAtLocation(TargetLocation, DeltaTime, false);
	}

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
	const bool bReplacedCombatMove = Data.CapturedCombatReservationRequest != 0 && IsValid(Data.Enemy)
		&& Data.Enemy->GetCombatReservationRequest() != 0
		&& Data.Enemy->GetCombatReservationRequest() != Data.CapturedCombatReservationRequest;
	const bool bReplacedLifetime = IsValid(Data.Enemy)
		&& (Data.CapturedMovementGeneration != Data.Enemy->GetPoolGameplayGeneration()
			|| Data.CapturedMovementLease != Data.Enemy->GetPoolLeaseSerial());
	if (bReplacedCombatMove || bReplacedLifetime)
	{
		// 같은 Controller/전술 회차라도 새 요청이 이동을 인수했으면 옛 Exit는 물리 이동을 멈추지 않는다.
		Data.bOwnsMovement = false;
	}
	if (Data.bOwnsMovement && Data.MoveResult == EEnemyFlightMoveResult::Arrived && Data.CapturedCombatTacticalSerial != 0
		&& IsValid(Data.Enemy) && Data.Enemy->IsCombatTacticalEpisodeCurrent(Data.CapturedCombatTacticalSerial))
	{
		AActor* Target = Data.Enemy->GetTrackedTarget();
		int32 SightChecks = 16;
		// 후보 선정 성공만으로 실패 예산을 돌려주지 않는다. 실제 도착과 발사 경로까지 확인한다.
		if (Target && !Data.Enemy->IsTargetOutsideWeaponRange(Target)
			&& Data.Enemy->HasCombatAttackPathFrom(Target, Data.Enemy->GetActorLocation(), SightChecks))
		{
			// 정상 도착은 같은 회차의 Guard를 유지해 다음 정지 대기로 인계한다.
			// 사선 조기 복구와 달리 여기에는 취소할 미완료 후보 요청이 없다.
			Data.Enemy->CompleteCombatTacticalSelectionSuccess(false);
		}
	}
	// 인계 뒤 늦게 종료되는 원래 Task는 새 Move의 전술 상태까지 종료하지 않는다.
	if (Data.bOwnsMovement && Data.CapturedCombatTacticalSerial != 0 && IsValid(Data.Enemy)
		&& Data.CapturedCombatTacticalSerial == Data.Enemy->GetCombatTacticalEpisodeSerial())
	{
		Data.Enemy->EndCombatTacticalMovement();
	}
	if (Data.bOwnsMovement && IsValid(Data.Enemy))
	{
		Data.Enemy->ClearCombatFlightHandoff(false);
	}
	if (Data.bOwnsMovement && Data.CapturedCombatReservationRequest != 0 && IsValid(Data.Enemy))
	{
		Data.Enemy->ReleaseCombatReservation(Data.CapturedCombatReservationRequest);
	}
	Data.SpeedProfileState.Reset();
	LogFlight(TEXT("Stop"), Data);
	Data.PathWaypoints.Reset();
	Data.WaypointSpeedLimits.Reset();
	Data.CachedCapsule = nullptr;
	Data.CachedRoomSubsystem = nullptr;
	Data.CachedRoom.Reset();
	Data.bWaitingForPath = false;
	Data.bRepathEpisode = false;
	if (!Data.bOwnsMovement || !IsValid(Data.Enemy) || !Data.Enemy->HasAuthority())
	{
		return;
	}
	// 빙의/재점유 또는 조사 회차 종료 뒤의 새 이동은 이전 Task가 정리하지 않는다.
	// 회차를 얻기 전 진입 실패(CapturedNoiseSerial == 0)는 자신의 초기화만 정리한다.
	const bool bReplacedNoiseMove = Data.bInvestigateNoise && Data.CapturedNoiseSerial != 0
		&& Data.Enemy->GetLocationInvestigationSerial() != Data.CapturedNoiseSerial;
	if (!bReplacedNoiseMove && IsValid(Data.CachedController) && Data.Enemy->GetController() == Data.CachedController)
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

bool IsNoiseFlightConfigurationValid(const FEnemyFlyToLocationTaskInstanceData& Data)
{
	return !Data.bInvestigateNoise || (Data.bUsePathfinding && !Data.bReturnHome
		&& !(Data.bUsePlannedPath && !Data.bCombatNavigationPath) && !Data.bUseInitialPath && !Data.bConstrainRetreatPath);
}

bool PrepareNoiseFlight(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!IsNoiseFlightConfigurationValid(Data))
	{
		return false;
	}
	if (!Data.bInvestigateNoise)
	{
		return true;
	}
	if (!IsValid(Data.Enemy))
	{
		return false;
	}
	Data.Destination = Data.Enemy->GetPerceptionGoal().Location;
	// 조사 이동은 목적지가 계속 바뀌어도 무한히 지속되지 않게 제한한다.
	if (!FMath::IsFinite(Data.MoveTimeout) || Data.MoveTimeout <= 0.0f)
	{
		Data.MoveTimeout = 30.0f;
	}
	if (!FMath::IsFinite(Data.NoProgressTimeout) || Data.NoProgressTimeout <= 0.0f)
	{
		Data.NoProgressTimeout = 3.0f;
	}
	return true;
}

bool BeginNoiseFlight(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!Data.bInvestigateNoise)
	{
		return true;
	}
	if (!Data.Enemy->BeginLocationInvestigation(Data.Destination))
	{
		return false;
	}
	Data.CapturedNoiseSerial = Data.Enemy->GetLocationInvestigationSerial();
	Data.LastNoiseProgressLocation = Data.Enemy->GetActorLocation();
	Data.NoiseInvestigationSearchBounds = FBox(ForceInit);
	Data.Enemy->UpdateNoiseInvestigationProgress(Data.CapturedNoiseSerial,
		Data.LastNoiseProgressLocation, Data.LastNoiseProgressLocation, Data.AcceptanceRadius);
	return true;
}

bool UpdateNoiseFlightDestination(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!Data.bInvestigateNoise)
	{
		return true;
	}
	if (!Data.Enemy->IsLocationInvestigationCurrent(Data.CapturedNoiseSerial))
	{
		Data.MoveResult = EEnemyFlightMoveResult::Interrupted;
		StopFlyTask(Data);
		return false;
	}
	const FVector CurrentLocation = Data.Enemy->GetActorLocation();
	Data.Enemy->UpdateNoiseInvestigationProgress(Data.CapturedNoiseSerial,
		Data.LastNoiseProgressLocation, CurrentLocation, Data.AcceptanceRadius);
	Data.LastNoiseProgressLocation = CurrentLocation;
	// 소리 기록은 매번 받아도 실제 이동 경로는 Pawn의 갱신 간격/거리 제한을 따른다.
	Data.Enemy->TryUpdateLocationInvestigationDestination(Data.Destination);
	return true;
}

EStateTreeRunStatus FinishNoiseFlight(FEnemyFlyToLocationTaskInstanceData& Data, EStateTreeRunStatus Status)
{
	if (!Data.bInvestigateNoise || Status == EStateTreeRunStatus::Running || !IsValid(Data.Enemy)
		|| !Data.Enemy->IsLocationInvestigationCurrent(Data.CapturedNoiseSerial))
	{
		return Status;
	}
	const bool bReachedOrExhausted = Data.MoveResult == EEnemyFlightMoveResult::Arrived
		|| Data.MoveResult == EEnemyFlightMoveResult::Blocked || Data.MoveResult == EEnemyFlightMoveResult::TimedOut;
	if (!bReachedOrExhausted)
	{
		return Status;
	}
	const bool bGeometryFailure = Data.MoveResult != EEnemyFlightMoveResult::Arrived
		&& (Data.PathStatus == EFlightPathStatus::NoPathWithinBounds || Data.PathStatus == EFlightPathStatus::BlockedEndpoint
			|| Data.PathStatus == EFlightPathStatus::BlockedSegment || Data.PathStatus == EFlightPathStatus::Success);
	// 예산 부족/사용 불가/잘못된 요청은 도달 불가의 증거가 아니다. 실제 경로 실패와 분리한다.
	if (bGeometryFailure)
	{
		Data.Enemy->RecordNoiseInvestigationFailure(Data.CapturedNoiseSerial, Data.Destination, Data.NoiseInvestigationSearchBounds);
	}
	// 도달하지 않은 소리 좌표를 수색 중심으로 채택하지 않는다.
	// 우회/재탐색을 소진한 경우에도 실제 이동이 끝난 안전한 위치에서 수색한다.
	StopFlyTask(Data);
	Data.bNoiseSearchReady = Data.Enemy->BeginLocationInvestigationSearch(
		Data.CapturedNoiseSerial, Data.Enemy->GetActorLocation());
	return Status;
}

void EndNoiseFlight(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (Data.bInvestigateNoise && !Data.bNoiseSearchReady && IsValid(Data.Enemy)
		&& Data.CapturedNoiseSerial != 0
		&& Data.Enemy->GetLocationInvestigationSerial() == Data.CapturedNoiseSerial)
	{
		// 이전 회차의 Exit가 새 조사나 수색 인계를 정리하지 않도록 소유권을 확인한다.
		Data.Enemy->EndLocationInvestigation();
	}
}

float GetFlightBrakingDeceleration(const UCharacterMovementComponent& Movement)
{
	// 비행 감속이 비어 있는 에셋은 기존 가속 성능으로 속도 상한을 낮춘다.
	// 0으로 정지 거리를 계산하거나 최소값 때문에 전체 경로를 저속으로 이동하지 않게 한다.
	return Movement.BrakingDecelerationFlying > 0.0f
		? Movement.BrakingDecelerationFlying : FMath::Max(Movement.GetMaxAcceleration(), 1.0f);
}

float GetCornerSpeedLimit(const FVector& Incoming, const FVector& Outgoing, float Acceleration, float Clearance)
{
	if (Incoming.IsNearlyZero() || Outgoing.IsNearlyZero())
	{
		return 0.0f;
	}
	const double Dot = FMath::Clamp(FVector::DotProduct(Incoming.GetSafeNormal(), Outgoing.GetSafeNormal()), -1.0, 1.0);
	if (Dot > 0.9999)
	{
		return TNumericLimits<float>::Max();
	}
	// 방향 변경량이 클수록 관성을 줄인다. 180도 반전은 코너까지 감속한 뒤 새 방향으로 출발한다.
	const double SinHalf = FMath::Sqrt((1.0 - Dot) * 0.5);
	const double CosHalf = FMath::Sqrt((1.0 + Dot) * 0.5);
	return FMath::Sqrt(FMath::Max(Acceleration, 1.0f) * FMath::Max(Clearance, 1.0f) * CosHalf / SinHalf);
}

float GetFlightWaypointRadius(const FEnemyFlyToLocationTaskInstanceData& Data, bool bFinalPoint)
{
	if (Data.bUsePlannedPath && !Data.bCombatNavigationPath)
	{
		return FMath::Min(10.0f, FMath::Max(Data.AcceptanceRadius, 1.0f));
	}
	return bFinalPoint ? FMath::Max(Data.AcceptanceRadius, 0.0f) : FMath::Min(10.0f, Data.PathCellSize * 0.1f);
}

void PrepareFlightSpeedLimits(FEnemyFlyToLocationTaskInstanceData& Data)
{
	// 새 경로의 모든 구간 검증이 끝난 뒤에만 이전 Move의 가감속 상태를 인계한다.
	Data.Enemy->ResumeCombatFlightHandoff(Data);
	Data.SpeedProfileState.SetPath(Data.PathWaypoints);
	Data.WaypointSpeedLimits.SetNumZeroed(Data.PathWaypoints.Num());
	UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}
	const float Braking = GetFlightBrakingDeceleration(*Movement);
	// 뒤에서 앞으로 정리하여 짧은 구간 다음의 급회전도 현재 구간부터 감속한다.
	for (int32 Index = Data.PathWaypoints.Num() - 2; Index > 0; --Index)
	{
		const FVector Incoming = Data.PathWaypoints[Index] - Data.PathWaypoints[Index - 1];
		const FVector Outgoing = Data.PathWaypoints[Index + 1] - Data.PathWaypoints[Index];
		const float Corner = GetCornerSpeedLimit(Incoming, Outgoing, Movement->GetMaxAcceleration(), Data.PathClearance * 0.5f);
		const float NextSpeed = Data.WaypointSpeedLimits[Index + 1];
		const float NextRadius = GetFlightWaypointRadius(Data, Index + 1 == Data.PathWaypoints.Num() - 1);
		const float BrakingDistance = FMath::Max(Outgoing.Size() - GetFlightWaypointRadius(Data, false) - NextRadius, 0.0);
		const float ReachableSpeed = FMath::Sqrt(NextSpeed * NextSpeed + 2.0f * Braking * BrakingDistance);
		Data.WaypointSpeedLimits[Index] = FMath::Min(Corner, ReachableSpeed);
	}
	FVector PreviousVelocity;
	if (Data.bCombatNavigationPath && Data.PathWaypoints.Num() >= 2
		&& Data.Enemy->ConsumeCombatNavigationRetargetVelocity(Data.Destination, PreviousVelocity))
	{
		// 새 대상의 경로·예약 검증이 끝난 뒤에만 관성을 복원한다. 역방향 성분과 새 코너의 과속은 버린다.
		const FVector Direction = (Data.PathWaypoints[1] - Data.PathWaypoints[0]).GetSafeNormal();
		const float Distance = FMath::Max(FVector::Distance(Data.PathWaypoints[0], Data.PathWaypoints[1])
			- GetFlightWaypointRadius(Data, Data.PathWaypoints.Num() == 2), 0.0);
		const float CornerLimit = FMath::Sqrt(FMath::Square(Data.WaypointSpeedLimits[1]) + 2.0f * Braking * Distance);
		const float Speed = FMath::Clamp(static_cast<float>(FVector::DotProduct(PreviousVelocity, Direction)),
			0.0f, FMath::Min(Movement->MaxFlySpeed, CornerLimit));
		Movement->Velocity = Direction * Speed;
		Data.SpeedProfileState.TargetSpeed = Speed;
	}
}

float GetFlightSegmentSpeedLimit(float ExitSpeed, float Distance, float Braking, float ApproachSpeed)
{
	return FMath::Max(ApproachSpeed, FMath::Sqrt(ExitSpeed * ExitSpeed + 2.0f * Braking * FMath::Max(Distance, 0.0f)));
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
	Request.AllowedRooms = Data.MovementRooms.Rooms;
	Request.bRequireAwayHalfSpace = Data.bConstrainRetreatPath && !Data.bCombatNavigationPath;
	Request.AwayOrigin = Data.RetreatOrigin + Request.Start - Data.Enemy->GetActorLocation();
	Request.AwayDirection = Data.RetreatDirection;
	return Request;
}

bool IsRetreatFlightConfigurationValid(const FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!Data.bConstrainRetreatPath || Data.bCombatNavigationPath)
	{
		return true;
	}
	const double Distance = FVector::Dist2D(Data.Destination, Data.RetreatOrigin);
	const FVector Start = IsValid(Data.Enemy) ? Data.Enemy->GetActorLocation() : Data.RetreatStart;
	const bool bShort = Data.bAllowShortRetreat && !Data.RetreatStart.ContainsNaN()
		&& Data.AcceptanceRadius <= 25.0f
		&& FVector::Dist2D(Data.RetreatStart, Data.RetreatOrigin) <= 500.0
		&& FVector::Dist2D(Data.Destination, Data.RetreatStart) <= 300.01
		&& Distance >= FVector::Dist2D(Start, Data.RetreatOrigin) + 50.0 + Data.AcceptanceRadius
		&& FVector::DotProduct(Data.Destination - Data.RetreatStart, Data.RetreatDirection) >= 75.0;
	return Data.bUsePathfinding && !(Data.bUsePlannedPath && !Data.bCombatNavigationPath) && Data.GoalSearchRadius == 0.0f
		&& !Data.RetreatOrigin.ContainsNaN() && !Data.RetreatDirection.ContainsNaN()
		&& FMath::IsNearlyZero(Data.RetreatDirection.Z)
		&& FMath::IsNearlyEqual(Data.RetreatDirection.SizeSquared2D(), 1.0, 0.001)
		&& FMath::IsFinite(Data.AcceptanceRadius) && Data.AcceptanceRadius >= 0.0f
		&& Data.AcceptanceRadius <= 50.0f && ((Distance > 1050.0 && Distance <= 1450.0) || bShort);
}

bool HasCurrentFlightRoom(const FEnemyFlyToLocationTaskInstanceData& Data)
{
	return IsValid(Data.Enemy) && !Data.Enemy->IsDead() && !Data.Enemy->IsEnemyPossessed()
		&& (!Data.bReturnHome || Data.Enemy->IsReturnHomeRequestCurrent(Data.CapturedReturnHomeSerial))
		&& (Data.CapturedSearchSerial == 0
			|| (Data.Enemy->GetSearchEpisode().Serial == Data.CapturedSearchSerial
				&& Data.Enemy->RefreshSearchEpisodeLimit()))
		&& (Data.CapturedCombatTacticalSerial == 0
			|| Data.Enemy->IsCombatTacticalEpisodeCurrent(Data.CapturedCombatTacticalSerial))
		&& IsValid(Data.CachedCapsule) && IsValid(Data.CachedRoomSubsystem)
		&& Data.CachedRoom.IsValid() && Data.PathRoomTag == Data.Enemy->GetDefaultRoomTag()
		&& Data.CapturedMovementState == Data.Enemy->GetCombatState()
		&& Data.CapturedMovementGeneration == Data.Enemy->GetPoolGameplayGeneration()
		&& Data.CapturedMovementLease == Data.Enemy->GetPoolLeaseSerial()
		&& Data.CachedRoomSubsystem->IsEnemyMovementRoomContextCurrent(Data.Enemy, Data.MovementRooms);
}

bool IsFlightStepInsideMovementRooms(const FEnemyFlyToLocationTaskInstanceData& Data, float DeltaTime)
{
	if (!HasCurrentFlightRoom(Data))
	{
		return false;
	}
	const UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}
	const float Step = FMath::Max(DeltaTime, 0.0f);
	const FVector Center = Data.CachedCapsule->GetComponentLocation();
	const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(
		Data.CachedCapsule->GetScaledCapsuleRadius() + Data.PathClearance,
		Data.CachedCapsule->GetScaledCapsuleHalfHeight() + Data.PathClearance,
		Data.CachedCapsule->GetComponentQuat()) + FVector(Movement->GetMaxAcceleration() * Step * Step);
	// 경로 검사는 간격을 유지하되 관성에 의한 그룹 경계 이탈은 매 이동 입력 전에 막는다.
	const FFlightPathRequest Request = MakeFlightPathRequest(Data, Center);
	return FlightPathfinding::IsSegmentInsideMovementRooms(Request, Center, Center + Movement->Velocity * Step, Extent);
}

bool CheckDirectFlightBoundary(FEnemyFlyToLocationTaskInstanceData& Data, float DeltaTime)
{
	if (!IsFlightStepInsideMovementRooms(Data, DeltaTime))
	{
		return false;
	}
	Data.SegmentCheckElapsed += DeltaTime;
	const bool bDestinationChanged = !Data.LastDestination.Equals(Data.Destination, 0.01);
	if (bDestinationChanged || Data.SegmentCheckElapsed >= FMath::Max(Data.PathCheckInterval, 0.01f))
	{
		Data.SegmentCheckElapsed = 0.0f;
		const FVector Offset = Data.CachedCapsule->GetComponentLocation() - Data.Enemy->GetActorLocation();
		Data.PathStatus = FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Data.CachedCapsule,
			MakeFlightPathRequest(Data, Data.Destination + Offset));
	}
	return Data.PathStatus == EFlightPathStatus::Success;
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
	// 이전 경로를 따라 전진한 것은 새 목적지 탐색 성공이 아니다. 재시도 시간/횟수는 유지한다.
	if (Data.bWaitingForPath)
	{
		return true;
	}
	// 경로 계산 성공이 아니라 실제 경유점 접근/통과가 재시도 한도를 복구한다.
	Data.bRepathEpisode = false;
	Data.RepathAttempts = 0;
	Data.RetryElapsed = 0.0f;
	Data.RetryDelay = 0.0f;
	return true;
}

bool HasFlightRepathLimitExpired(const FEnemyFlyToLocationTaskInstanceData& Data)
{
	// 귀환 실패는 SelectHome의 영속 예산으로 모은다. Fly 내부 재시도가 별도 횟수를 만들지 않는다.
	if (Data.bReturnHome && Data.bWaitingForPath)
	{
		return true;
	}
	// 마지막 허용 탐색이 성공했다면 이동할 수 있다. 횟수 제한은 다음 시도가 필요할 때 적용한다.
	return Data.bRepathEpisode && (Data.RetryElapsed >= Data.RepathTimeout
		|| (Data.bWaitingForPath && Data.RepathAttempts >= Data.MaxRepathAttempts));
}

void WaitForFlightPath(FEnemyFlyToLocationTaskInstanceData& Data, bool bKeepValidatedPath = false)
{
	if (Data.CapturedCombatTacticalSerial != 0 && Data.Enemy
		&& Data.CapturedCombatTacticalSerial == Data.Enemy->GetCombatTacticalEpisodeSerial())
	{
		Data.Enemy->EndCombatTacticalMovement();
	}
	// 첫 실패부터 시간을 유지한다. 재탐색 성공만으로 한도를 초기화하지 않는다.
	if (!Data.bRepathEpisode)
	{
		Data.RetryElapsed = 0.0f;
		Data.RetryDelay = 0.0f;
		Data.bRepathEpisode = true;
	}
	Data.bWaitingForPath = true;
	if (bKeepValidatedPath && Data.PathWaypoints.IsValidIndex(Data.WaypointIndex))
	{
		Data.SegmentCheckElapsed = Data.PathCheckInterval;
		return;
	}
	Data.PathWaypoints.Reset();
	Data.WaypointSpeedLimits.Reset();
	PauseFlyMovement(*Data.Enemy);
	Data.SpeedProfileState.Reset();
}

bool IsPlannedPathGeometryValid(const TArray<FVector>& Points, const FVector& Start, const FVector& Destination)
{
	if (Points.Num() < 2 || Points.Num() > 11 || Start.ContainsNaN() || Destination.ContainsNaN())
	{
		return false;
	}
	if (Points[0].ContainsNaN() || !Points[0].Equals(Start, 25.0)
		|| !Points.Last().Equals(Destination, 1.0))
	{
		return false;
	}
	double Length = 0.0;
	FVector Previous = Start;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		if (Points[Index].ContainsNaN())
		{
			return false;
		}
		const double SegmentLength = FVector::Distance(Previous, Points[Index]);
		if (SegmentLength <= 0.0 || SegmentLength > 75.0)
		{
			return false;
		}
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
		if (Data.PathStatus != EFlightPathStatus::Success)
		{
			return FailPlannedFlight(Data, TEXT("PlannedPathRejected: segment validation"));
		}
		Data.PathWaypoints.Add(Request.Goal);
	}
	Data.WaypointIndex = 1;
	Data.BestDistance = FVector::Distance(Data.PathWaypoints[0], Data.PathWaypoints[1]);
	PrepareFlightSpeedLimits(Data);
	Data.LastPathPosition = Data.PathWaypoints[0];
	Data.TravelledPathLength = 0.0f;
	Data.bDiagnosticMovementReported = false;
	Data.SegmentCheckElapsed = 0.0f;
	Data.MoveResult = EEnemyFlightMoveResult::Moving;
	if (Data.CapturedCombatTacticalSerial != 0)
	{
		Data.Enemy->BeginCombatTacticalMovement(Data.bTacticalSightReposition);
	}
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
		// 탐색 주기 대기는 장애물 발견과 다르다. 남아 있는 경로는 아래 추종 단계에서 재검증한다.
		WaitForFlightPath(Data, true);
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
		if (FlightPathfinding::ClampToMovementRooms(Request, Request.Goal, Extent, RoomGoal))
		{
			bRoomAdjusted = !RoomGoal.Equals(Request.Goal, 0.01);
			Request = MakeFlightPathRequest(Data, RoomGoal);
		}
	}
	if (Data.bInvestigateNoise)
	{
		Data.NoiseInvestigationSearchBounds = Request.SearchBounds;
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
		PrepareFlightSpeedLimits(Data);
		Data.WaypointIndex = Data.PathWaypoints.Num() > 1 ? 1 : 0;
		Data.bWaitingForPath = false;
		Data.NoProgressElapsed = 0.0f;
		Data.BestDistance = FVector::Distance(Request.Start, Data.PathWaypoints[Data.WaypointIndex]);
		if (Data.CapturedCombatTacticalSerial != 0)
		{
			Data.Enemy->BeginCombatTacticalMovement(Data.bTacticalSightReposition);
		}
		return EStateTreeRunStatus::Running;
	}
	if (IsRetryableFlightFailure(Result.Status))
	{
		WaitForFlightPath(Data, Result.Status == EFlightPathStatus::BudgetExceeded);
		return EStateTreeRunStatus::Running;
	}
	Data.MoveResult = EEnemyFlightMoveResult::Invalid;
	StopFlyTask(Data);
	return EStateTreeRunStatus::Failed;
}

bool IsInitialFlightGeometryValid(const TArray<FVector>& Points, const FVector& Start,
	const FVector& Destination, int32 MaxSearchNodes)
{
	if (Points.Num() < 2 || static_cast<int64>(Points.Num()) > static_cast<int64>(MaxSearchNodes) + 2
		|| Start.ContainsNaN() || Destination.ContainsNaN()
		|| !Points[0].Equals(Start, 25.0f) || !Points.Last().Equals(Destination, 1.0f))
	{
		return false;
	}
	for (const FVector& Point : Points)
	{
		if (Point.ContainsNaN())
		{
			return false;
		}
	}
	return true;
}

EStateTreeRunStatus StartInitialFlight(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!HasCurrentFlightRoom(Data) || !FMath::IsFinite(Data.MaxSearchMilliseconds) || Data.MaxSearchMilliseconds <= 0.0f
		|| !FMath::IsFinite(Data.PathCellSize) || Data.PathCellSize <= 0.0f
		|| !FMath::IsFinite(Data.PathClearance) || Data.PathClearance < 0.0f
		|| Data.MaxSearchNodes <= 0 || Data.MaxCollisionChecks <= 0
		|| !IsInitialFlightGeometryValid(Data.PlannedWaypoints, Data.Enemy->GetActorLocation(), Data.Destination, Data.MaxSearchNodes))
	{
		Data.PathStatus = EFlightPathStatus::InvalidRequest;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	const FVector Offset = Data.CachedCapsule->GetComponentLocation() - Data.Enemy->GetActorLocation();
	const double Started = FPlatformTime::Seconds();
	Data.PathWaypoints.Add(Data.CachedCapsule->GetComponentLocation());
	for (int32 Index = 1; Index < Data.PlannedWaypoints.Num(); ++Index)
	{
		// Select와 Move 사이에도 장애물이 생길 수 있다. 예산 안에서 구간을 재검증하고 막히면 정지 후 재탐색한다.
		if (Index > Data.MaxCollisionChecks / 4
			|| (FPlatformTime::Seconds() - Started) * 1000.0 >= Data.MaxSearchMilliseconds)
		{
			Data.PathStatus = EFlightPathStatus::BudgetExceeded;
			WaitForFlightPath(Data);
			return EStateTreeRunStatus::Running;
		}
		FFlightPathRequest Request = MakeFlightPathRequest(Data, Data.PlannedWaypoints[Index] + Offset);
		Request.Start = Data.PathWaypoints.Last();
		Data.PathStatus = FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Data.CachedCapsule, Request);
		if (Data.PathStatus != EFlightPathStatus::Success)
		{
			WaitForFlightPath(Data);
			return EStateTreeRunStatus::Running;
		}
		Data.PathWaypoints.Add(Request.Goal);
	}
	Data.WaypointIndex = 1;
	Data.BestDistance = FVector::Distance(Data.PathWaypoints[0], Data.PathWaypoints[1]);
	Data.LastPathPosition = Data.PathWaypoints[0];
	Data.TravelledPathLength = 0.0f;
	Data.bDiagnosticMovementReported = false;
	Data.SegmentCheckElapsed = 0.0f;
	Data.bWaitingForPath = false;
	LogFlight(TEXT("InitialPathStarted"), Data);
	if (Data.CapturedCombatTacticalSerial != 0)
	{
		Data.Enemy->BeginCombatTacticalMovement(Data.bTacticalSightReposition);
	}
	PrepareFlightSpeedLimits(Data);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus TickFlightPath(FEnemyFlyToLocationTaskInstanceData& Data, float DeltaTime)
{
	if (Data.CapturedCombatReservationRequest != 0
		&& !Data.Enemy->RefreshCombatReservation(Data.CapturedCombatReservationRequest))
	{
		Data.MoveResult = EEnemyFlightMoveResult::Interrupted;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	// 방향 확인 구간에는 기존/새 비행이 이동과 시간 제한을 진행하지 않는다. 귀환은 원래 경로를 유지한다.
	if (IsValid(Data.Enemy) && Data.Enemy->IsHitDirectionConfirmationActive()
		&& Data.Enemy->GetCombatState() != EEnemyCombatState::Returning)
	{
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		return EStateTreeRunStatus::Running;
	}
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
	if (!HasCurrentFlightRoom(Data))
	{
		Data.PathStatus = EFlightPathStatus::Unavailable;
		Data.MoveResult = EEnemyFlightMoveResult::Invalid;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!Data.bWaitingForPath && !Data.LastDestination.Equals(Data.Destination, FMath::Max(Data.DestinationChangeDistance, 0.0f)))
	{
		if (Data.bUsePlannedPath && !Data.bCombatNavigationPath)
		{
			return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: destination changed"));
		}
		const EStateTreeRunStatus Status = RequestFlightPath(Data);
		if (Status != EStateTreeRunStatus::Running)
		{
			return Status;
		}
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
			const EStateTreeRunStatus Status = RequestFlightPath(Data);
			if (Status != EStateTreeRunStatus::Running)
			{
				return Status;
			}
		}
		// 새 경로를 기다려도 유효한 이전 구간은 추종한다. 경로가 없으면 정지 상태로 대기한다.
		if (!Data.PathWaypoints.IsValidIndex(Data.WaypointIndex))
		{
			return EStateTreeRunStatus::Running;
		}
	}

	const FVector Position = Data.CachedCapsule->GetComponentLocation();
	if (Data.bConstrainRetreatPath && !Data.bCombatNavigationPath && FVector::DotProduct(
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
	const float Radius = GetFlightWaypointRadius(Data, bFinalPoint);
	const bool bReachedPoint = FVector::DistSquared(Position, NextPoint) <= FMath::Square(Radius);
	if (Data.bUsePlannedPath && !Data.bCombatNavigationPath)
	{
		Data.TravelledPathLength += FVector::Distance(Data.LastPathPosition, Position);
		Data.LastPathPosition = Position;
		if (Data.TravelledPathLength >= 500.0f && !(bReachedPoint && bFinalPoint))
		{
			return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: 500cm travel limit"));
		}
	}

	// 중간점 허용 반경 때문에 모서리를 가로지르지 않도록 다음 구간을 먼저 검사한다.
	if (bReachedPoint && !bFinalPoint)
	{
		NextPoint = Data.PathWaypoints[Data.WaypointIndex + 1];
	}
	if (bReachedPoint || Data.SegmentCheckElapsed >= Data.PathCheckInterval)
	{
		if (Data.CapturedCombatReservationRequest != 0
			&& !Data.Enemy->IsCombatSegmentReserved(Data.CapturedCombatReservationRequest, Position, NextPoint))
		{
			// 경로 재탐색은 예약을 자동 확장하지 않는다. 새 통로가 필요하면 Select로 돌려 원자적으로 확보한다.
			Data.MoveResult = EEnemyFlightMoveResult::Interrupted;
			StopFlyTask(Data);
			return EStateTreeRunStatus::Failed;
		}
		Data.SegmentCheckElapsed = 0.0f;
		Data.PathStatus = HasCurrentFlightRoom(Data)
			? FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Data.CachedCapsule,
				MakeFlightPathRequest(Data, NextPoint)) : EFlightPathStatus::Unavailable;
		if (Data.PathStatus != EFlightPathStatus::Success)
		{
			if (IsRetryableFlightFailure(Data.PathStatus))
			{
				if (Data.bUsePlannedPath && !Data.bCombatNavigationPath)
				{
					return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: segment blocked or budget exceeded"));
				}
				WaitForFlightPath(Data);
				return EStateTreeRunStatus::Running;
			}
			Data.MoveResult = EEnemyFlightMoveResult::Invalid;
			StopFlyTask(Data);
			return EStateTreeRunStatus::Failed;
		}
	}
	if (Data.bInvestigateNoise)
	{
		Data.Enemy->UpdateNoiseInvestigationTravelDirection(Data.CapturedNoiseSerial, NextPoint - Position, Data.RotationSpeed);
	}
	// 경로 승인과 물리 이동 시작을 구분한다. 짧은 경로는 첫 이동에 도착할 수도 있으므로 도착 판정보다 먼저 기록한다.
	if (Data.bConstrainRetreatPath && !Data.bDiagnosticMovementReported
		&& FVector::DistSquared2D(Data.Enemy->GetActorLocation(), Data.RetreatStart) > 1.0)
	{
		Data.bDiagnosticMovementReported = true;
		LogFlight(TEXT("ActualMovementStarted"), Data);
	}
	if (bReachedPoint && bFinalPoint)
	{
		if (Data.bWaitingForPath)
		{
			// 이전 목적지에 도달한 것은 새 목적지의 도착이 아니다. 여기서 안전하게 멈추고 재시도한다.
			WaitForFlightPath(Data);
			return EStateTreeRunStatus::Running;
		}
		Data.MoveResult = EEnemyFlightMoveResult::Arrived;
		LogFlight(TEXT("PathArrived"), Data);
		StopFlyTask(Data);
		return EStateTreeRunStatus::Succeeded;
	}
	if (bReachedPoint)
	{
		// 경유점 통과 시에만 출력한다. 매 프레임 로그가 이동 성능을 가리지 않게 한다.
		if (CVarEnemyFlightDebug.GetValueOnGameThread() >= 2)
		{
			LogFlight(TEXT("WaypointReached"), Data);
		}
		++Data.WaypointIndex;
		if (Data.bInvestigateNoise && Data.PathWaypoints.IsValidIndex(Data.WaypointIndex))
		{
			Data.Enemy->UpdateNoiseInvestigationTravelDirection(Data.CapturedNoiseSerial,
				Data.PathWaypoints[Data.WaypointIndex] - Position, Data.RotationSpeed);
			Data.Enemy->NotifyNoiseInvestigationNaturalBoundary(Data.CapturedNoiseSerial, false);
		}
		// 코너 진입 전에 감속했으므로 정상 통과에서는 속도와 커브 진행도를 초기화하지 않는다.
	}

	const float Distance = FVector::Distance(Position, NextPoint);
	if (!IsFlightStepInsideMovementRooms(Data, DeltaTime))
	{
		Data.PathStatus = EFlightPathStatus::BlockedSegment;
		WaitForFlightPath(Data);
		return EStateTreeRunStatus::Running;
	}
	Data.NoProgressElapsed += DeltaTime;
	RecordFlightPathProgress(Data, Distance, bReachedPoint);
	const float NoProgressLimit = (Data.bUsePlannedPath && !Data.bCombatNavigationPath) && Data.NoProgressTimeout <= 0.0f ? 2.0f : Data.NoProgressTimeout;
	if (NoProgressLimit > 0.0f && Data.NoProgressElapsed >= NoProgressLimit)
	{
		if (Data.bUsePlannedPath && !Data.bCombatNavigationPath)
		{
			return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: no progress"));
		}
		WaitForFlightPath(Data);
		return EStateTreeRunStatus::Running;
	}
	if (Data.bRotateTowardDestination)
	{
		RotateTowardLocation(*Data.Enemy, *Data.CachedController, NextPoint,
			Data.RotationSpeed, 0.0f, DeltaTime);
	}
	ApplyFlyTaskSpeedMultiplier(*Data.Enemy, Data.SpeedMultiplier);
	if (Data.SpeedProfile.IsEnabled())
	{
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
		{
			// 가까운 경유점이 아닌 최종 도착까지의 경로 길이로 감속한다.
			const float ArrivalRadius = (Data.bUsePlannedPath && !Data.bCombatNavigationPath) ? FMath::Min(10.0f, FMath::Max(Data.AcceptanceRadius, 0.0f))
				: FMath::Max(Data.AcceptanceRadius, 0.0f);
			const float Remaining = FMath::Max(Data.SpeedProfileState.GetRemainingDistance(
				Position, Data.PathWaypoints, Data.WaypointIndex) - ArrivalRadius, 0.0f);
			Movement->MaxFlySpeed = Data.SpeedProfileState.Update(Data.SpeedProfile, Movement->MaxFlySpeed,
				Movement->Velocity.Size(), Remaining, DeltaTime, true, Data.Enemy, TEXT("EnemyFly"));
		}
	}
	if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
	{
		const float Step = FMath::Max(DeltaTime, UE_SMALL_NUMBER);
		const bool bAtFinalSegment = Data.WaypointIndex == Data.PathWaypoints.Num() - 1;
		// 중심점이 아니라 통과 판정 반경에 진입하기 전에 코너 속도까지 낮춘다.
		const float ArrivalRadius = GetFlightWaypointRadius(Data, bAtFinalSegment);
		const float ExitSpeed = Data.WaypointSpeedLimits.IsValidIndex(Data.WaypointIndex)
			? Data.WaypointSpeedLimits[Data.WaypointIndex] : 0.0f;
		const float Braking = GetFlightBrakingDeceleration(*Movement);
		// 도착 판정까지는 최소 속도로 접근한다. 반전 코너도 진입 전에 저속으로 낮춘다.
		const float SegmentLimit = GetFlightSegmentSpeedLimit(ExitSpeed, Distance - ArrivalRadius, Braking, 1.0f);
		const float DesiredSpeed = FMath::Min(Movement->MaxFlySpeed, SegmentLimit);
		const float ActualSpeed = Movement->Velocity.Size();
		const float Rate = DesiredSpeed < ActualSpeed ? Braking : FMath::Max(Movement->GetMaxAcceleration(), 1.0f);
		Movement->MaxFlySpeed = FMath::FInterpConstantTo(ActualSpeed, DesiredSpeed, Step, Rate);

		// 정상 감속과 별개의 최종 안전 제한이다. 낮은 FPS에서도 미검증 다음 구간을 넘지 않는다.
		const float TravelLimit = (Data.bUsePlannedPath && !Data.bCombatNavigationPath) ? FMath::Min(Distance, FMath::Max(500.0f - Data.TravelledPathLength, 0.0f)) : Distance;
		Movement->MaxFlySpeed = FMath::Min(Movement->MaxFlySpeed, TravelLimit / Step);
		Movement->Velocity = Movement->Velocity.GetClampedToMaxSize(Movement->MaxFlySpeed);

		// 회전 중에는 기존 관성이 다음 입력 방향과 다르다. 실제로 진행할 한 프레임 구간도 검증한다.
		// 직선에서는 추가 Trace가 없고, 회전 검사도 CheckSegment의 고정 예산을 그대로 사용한다.
		const FVector Direction = (NextPoint - Position).GetSafeNormal();
		if (!Movement->Velocity.IsNearlyZero()
			&& FVector::DotProduct(Movement->Velocity.GetSafeNormal(), Direction) < 0.99)
		{
			// 가속으로 바뀔 수 있는 이동량까지 여유를 더해 현재 속도만 검사하는 틈을 막는다.
			FFlightPathRequest Request = MakeFlightPathRequest(Data, Position + Movement->Velocity * Step);
			Request.Clearance += Movement->GetMaxAcceleration() * Step * Step;
			Data.PathStatus = FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Data.CachedCapsule, Request);
			if (Data.PathStatus != EFlightPathStatus::Success)
			{
				if (Data.bUsePlannedPath && !Data.bCombatNavigationPath)
				{
					return FailPlannedFlight(Data, TEXT("PlannedPathInterrupted: turn clearance"));
				}
				WaitForFlightPath(Data);
				return EStateTreeRunStatus::Running;
			}
		}
	}
	if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
	{
		// 방향 입력만 주면 옆 방향 관성이 남아 작은 경유점 주위를 선회한다.
		// CharacterMovement의 목표 속도 추종을 사용해 현재 속도와의 차이를 가속 한도 안에서 상쇄한다.
		// 위에서 계산한 속도 상한과 충돌/Room 검증은 그대로 적용하며 경유점에서 속도를 초기화하지 않는다.
		Movement->RequestDirectMove((NextPoint - Position).GetSafeNormal() * Movement->MaxFlySpeed, false);
	}
	return EStateTreeRunStatus::Running;
}
}

void AEnemyBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	AEnemyBase* Enemy = CastChecked<AEnemyBase>(InThis);
	// Task 인스턴스 밖으로 인계한 짧은 구간도 커브/컴포넌트 참조를 GC에 알린다.
	if (Enemy->CombatFlightHandoff)
	{
		Collector.AddPropertyReferences(FEnemyFlyToLocationTaskInstanceData::StaticStruct(), Enemy->CombatFlightHandoff.Get(), Enemy);
	}
	Super::AddReferencedObjects(InThis, Collector);
}

bool AEnemyBase::OfferCombatFlightHandoff(FEnemyFlyToLocationTaskInstanceData& Data)
{
	if (!HasAuthority() || !GetWorld() || !Data.bOwnsMovement || Data.Enemy != this
		|| Data.MoveResult != EEnemyFlightMoveResult::Moving || Data.bWaitingForPath
		|| Data.CapturedCombatTacticalSerial == 0 || !HasCurrentFlightRoom(Data)
		|| IsAIControlSuppressed() || IsLocationInvestigationActive() || !GetTrackedTarget()
		|| Data.CachedController != GetController() || !Data.PathWaypoints.IsValidIndex(Data.WaypointIndex)
		|| !FMath::IsFinite(Data.RepathInterval) || Data.RepathInterval <= 0.0f)
	{
		return false;
	}
	if (Data.CapturedCombatReservationRequest != 0 && !RefreshCombatReservation(Data.CapturedCombatReservationRequest))
	{
		return false;
	}
	ClearCombatFlightHandoff(false);
	CombatFlightHandoff = MakeShared<FEnemyFlyToLocationTaskInstanceData>(Data);
	CombatFlightHandoffTarget = GetTrackedTarget();
	CombatFlightHandoffStart = GetActorLocation();
	// 혼합 경로는 Query 제한 안에서 기존 안전 구간을 이어간다. 전용 선회/후퇴는 짧은 인계만 허용한다.
	const float HandoffSeconds = Data.bCombatNavigationPath ? FMath::Max(Data.RepathInterval, 2.0f) : Data.RepathInterval;
	CombatFlightHandoffDeadline = GetWorld()->GetTimeSeconds() + HandoffSeconds;
	const TWeakPtr<FEnemyFlyToLocationTaskInstanceData> Offered = CombatFlightHandoff;
	GetWorldTimerManager().SetTimer(CombatFlightHandoffTimer, FTimerDelegate::CreateWeakLambda(this, [this, Offered]
	{
		if (CombatFlightHandoff == Offered.Pin())
		{
			ClearCombatFlightHandoff();
		}
	}), HandoffSeconds, false);
	Data.bOwnsMovement = false;
	ConsumeMovementInputVector();
	// Select 동안의 짧은 관성 계승은 정상 Move가 아니다. 기존 실패/재요청 예산의 시계는 계속 흐른다.
	EndCombatTacticalMovement();
	LogFlight(TEXT("HandoffOffered"), *CombatFlightHandoff);
	// Exit 다음 물리 갱신부터 제한한다. Select의 첫 Tick까지 이전 속도가 그대로 남지 않게 한다.
	if (GetWorld()->GetDeltaSeconds() > 0.0f)
	{
		TickCombatFlightHandoff(GetWorld()->GetDeltaSeconds());
	}
	return true;
}

bool AEnemyBase::GetCombatNavigationHandoff(TArray<FVector>& OutWaypoints, FVector& OutDestination,
	bool* OutProgressOnly, bool bCaptureRetarget) const
{
	// 시야 집합이 먼저 B로 바뀌면 A의 추적 유예를 설정하기 전 GetTrackedTarget은 비어 있을 수 있다.
	// 유효한 B를 채택하는 직전 캡처만 저장된 A의 형상을 읽고, 일반 조회는 현재 추적 유효성을 지킨다.
	const AActor* ExpectedTarget = bCaptureRetarget ? TrackedTarget.Get() : GetTrackedTarget();
	const AEnemyAIController* TrackingAI = TrackingController.Get();
	if (!ExpectedTarget || !TrackingAI || TrackingAI != GetController() || TrackingAI->GetPawn() != this
		|| !TrackingAI->IsValidTrackingTarget(ExpectedTarget))
	{
		return false;
	}
	const auto Previous = CombatFlightHandoff;
	if (!Previous || !Previous->bCombatNavigationPath || !GetWorld()
		|| GetWorld()->GetTimeSeconds() >= CombatFlightHandoffDeadline || !HasCurrentFlightRoom(*Previous)
		|| Previous->CachedController != GetController() || CombatFlightHandoffTarget.Get() != ExpectedTarget
		|| !Previous->PathWaypoints.IsValidIndex(Previous->WaypointIndex))
	{
		// 같은 대상의 새 관측으로 회차만 바뀌어도 경로 형태는 비교 후보로 보존한다.
		// 이동 권한은 인계하지 않는다. 새 회차의 사선/경로/점유 검증을 통과한 뒤에만 다시 움직인다.
		if (!HasAuthority() || IsDead() || IsEnemyPossessed() || IsAIControlSuppressed()
			|| CombatNavigationPath.Num() < 2 || !CombatNavigationPathTarget.IsValid()
			|| FVector::DistSquared(GetActorLocation(), CombatNavigationPathDestination) < FMath::Square(60.0)
			|| CombatNavigationPathTarget.Get() != ExpectedTarget || CombatNavigationPathController.Get() != GetController()
			|| CombatNavigationPathGeneration != GetPoolGameplayGeneration() || CombatNavigationPathLease != GetPoolLeaseSerial())
		{
			return false;
		}
		int32 Nearest = 1;
		for (int32 Index = 2; Index < CombatNavigationPath.Num(); ++Index)
		{
			if (FVector::DistSquared(GetActorLocation(), CombatNavigationPath[Index])
				< FVector::DistSquared(GetActorLocation(), CombatNavigationPath[Nearest]))
			{
				Nearest = Index;
			}
		}
		OutWaypoints = {GetActorLocation()};
		for (int32 Index = Nearest; Index < CombatNavigationPath.Num(); ++Index)
		{
			OutWaypoints.Add(CombatNavigationPath[Index]);
		}
		OutDestination = CombatNavigationPathDestination;
		if (OutProgressOnly)
		{
			*OutProgressOnly = bCombatNavigationProgressOnly;
		}
		return true;
	}
	const FVector Offset = Previous->CachedCapsule->GetComponentLocation() - GetActorLocation();
	OutWaypoints = {GetActorLocation()};
	for (int32 Index = Previous->WaypointIndex; Index < Previous->PathWaypoints.Num(); ++Index)
	{
		OutWaypoints.Add(Previous->PathWaypoints[Index] - Offset);
	}
	OutDestination = Previous->Destination;
	if (OutProgressOnly)
	{
		*OutProgressOnly = Previous->bCombatNavigationProgressOnly;
	}
	return OutWaypoints.Num() >= 2;
}

bool AEnemyBase::ConsumeCombatNavigationRetargetVelocity(const FVector& Destination, FVector& OutVelocity)
{
	const double ExpiresAt = CombatNavigationRetargetVelocityExpiresAt;
	CombatNavigationRetargetVelocityExpiresAt = -1.0;
	OutVelocity = FVector::ZeroVector;
	// 형상만 남아 있는 동안은 이동 권한이 없다. 새 요청으로 같은 목적지를 재승인한 경우에만 한 번 소비한다.
	if (!HasAuthority() || !GetWorld() || ExpiresAt < 0.0 || GetWorld()->GetTimeSeconds() >= ExpiresAt
		|| IsDead() || IsEnemyPossessed() || IsAIControlSuppressed() || bCombatNavigationRetargetPending
		|| CombatNavigationRetargetVelocity.ContainsNaN()
		|| !Destination.Equals(CombatNavigationRetargetVelocityDestination, 1.0)
		|| !Destination.Equals(CombatNavigationPathDestination, 1.0)
		|| CombatNavigationPathController.Get() != GetController() || CombatNavigationPathTarget.Get() != GetTrackedTarget()
		|| CombatNavigationPathGeneration != GetPoolGameplayGeneration() || CombatNavigationPathLease != GetPoolLeaseSerial()
		|| CombatNavigationPathReservation == 0 || CombatNavigationPathReservation != GetCombatReservationRequest()
		|| !IsCombatReservationRequestCurrent(CombatNavigationPathReservation)
		|| !IsCombatTacticalEpisodeCurrent(CombatNavigationPathSerial))
	{
		return false;
	}
	OutVelocity = CombatNavigationRetargetVelocity;
	return true;
}

void AEnemyBase::ClearCombatFlightHandoff(bool bStopMovement)
{
	if (!HasAuthority())
	{
		return;
	}
	// 먼저 소유권을 내려 Stop의 재진입과 이전 타이머가 새 이동을 정리하는 것을 막는다.
	TSharedPtr<FEnemyFlyToLocationTaskInstanceData> Previous = MoveTemp(CombatFlightHandoff);
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(CombatFlightHandoffTimer);
	}
	CombatFlightHandoffTarget.Reset();
	CombatFlightHandoffDeadline = -1.0;
	if (!Previous || !bStopMovement)
	{
		return;
	}
	const bool bSameOwner = Previous->CachedController == GetController()
		&& Previous->CapturedMovementGeneration == GetPoolGameplayGeneration()
		&& Previous->CapturedMovementLease == GetPoolLeaseSerial();
	if (!bSameOwner)
	{
		return;
	}
	Previous->MoveResult = EEnemyFlightMoveResult::Interrupted;
	StopFlyTask(*Previous);
}

void AEnemyBase::TickCombatFlightHandoff(float DeltaTime)
{
	const TSharedPtr<FEnemyFlyToLocationTaskInstanceData> Previous = CombatFlightHandoff;
	if (!Previous)
	{
		return;
	}
	if (!GetWorld() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f
		|| GetWorld()->GetTimeSeconds() >= CombatFlightHandoffDeadline || !HasCurrentFlightRoom(*Previous)
		|| Previous->CachedController != GetController() || IsAIControlSuppressed()
		|| IsLocationInvestigationActive() || !CombatFlightHandoffTarget.IsValid()
		|| CombatFlightHandoffTarget.Get() != GetTrackedTarget())
	{
		ClearCombatFlightHandoff();
		return;
	}
	// 혼합 경로는 채택 시 연결 구간을 재검증한다. 전용 Orbit/Retreat는 기존 25cm 스냅샷 범위를 지킨다.
	const float Remaining = Previous->bCombatNavigationPath ? TNumericLimits<float>::Max()
		: FMath::Max(25.0f - Previous->PathClearance, 0.0f)
			- FVector::Distance(GetActorLocation(), CombatFlightHandoffStart);
	if (Remaining <= 1.0f)
	{
		ClearCombatFlightHandoff();
		return;
	}
	// 마지막 구간 도착으로 새 선택의 실패 예산을 초기화하지 않도록 인계 종료를 먼저 처리한다.
	if (!Previous->PathWaypoints.IsValidIndex(Previous->WaypointIndex)
		|| (Previous->WaypointIndex == Previous->PathWaypoints.Num() - 1
			&& FVector::Distance(Previous->CachedCapsule->GetComponentLocation(), Previous->PathWaypoints.Last())
				<= GetFlightWaypointRadius(*Previous, true)))
	{
		ClearCombatFlightHandoff();
		return;
	}
	// TickFlightPath는 통과 판정 뒤 Index를 바꿀 수 있다. 이번 Tick에서 검증한 입력 방향을 그대로 제한한다.
	const FVector Next = Previous->PathWaypoints[Previous->WaypointIndex];
	if (TickFlightPath(*Previous, DeltaTime) != EStateTreeRunStatus::Running || CombatFlightHandoff != Previous
		|| Previous->bWaitingForPath || !Previous->PathWaypoints.IsValidIndex(Previous->WaypointIndex))
	{
		ClearCombatFlightHandoff();
		return;
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// 새 입력으로 가속되더라도 한 프레임에 스냅샷 범위를 넘어가지 않는 최종 제한이다.
		Movement->MaxFlySpeed = FMath::Min(Movement->MaxFlySpeed, Remaining / DeltaTime);
		Movement->Velocity = Movement->Velocity.GetClampedToMaxSize(Movement->MaxFlySpeed);
		Movement->RequestDirectMove((Next - Previous->CachedCapsule->GetComponentLocation()).GetSafeNormal()
			* Movement->MaxFlySpeed, false);
	}
}

void AEnemyBase::ResumeCombatFlightHandoff(FEnemyFlyToLocationTaskInstanceData& Data)
{
	const TSharedPtr<FEnemyFlyToLocationTaskInstanceData> Previous = CombatFlightHandoff;
	if (!Previous || &Data == Previous.Get())
	{
		return;
	}
	const bool bCurrent = GetWorld() && GetWorld()->GetTimeSeconds() < CombatFlightHandoffDeadline
		&& HasCurrentFlightRoom(Data) && HasCurrentFlightRoom(*Previous)
		&& Previous->CachedController == Data.CachedController
		&& CombatFlightHandoffTarget.IsValid() && CombatFlightHandoffTarget.Get() == GetTrackedTarget()
		&& Data.CapturedCombatTacticalSerial == Previous->CapturedCombatTacticalSerial;
	if (bCurrent && Data.SpeedProfile.AccelerationCurve == Previous->SpeedProfile.AccelerationCurve
		&& Data.SpeedProfile.AccelerationDuration == Previous->SpeedProfile.AccelerationDuration)
	{
		Data.SpeedProfileState = Previous->SpeedProfileState;
		LogFlight(TEXT("HandoffResumed"), Data);
	}
	// 신규 Move가 이미 검증한 구간의 속도/코너 제한은 다음 갱신에서 적용한다. 옛 경로는 다시 쓰지 않는다.
	ClearCombatFlightHandoff(false);
}

#if WITH_DEV_AUTOMATION_TESTS
namespace EnemyFlightTesting
{
EStateTreeRunStatus StartPath(FEnemyFlyToLocationTaskInstanceData& Data)
{
	Data.bNoiseSearchReady = false;
	Data.CapturedNoiseSerial = 0;
	if (!PrepareNoiseFlight(Data))
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bReturnHome && (!IsValid(Data.Enemy) || !Data.Enemy->GetSelectedReturnHomeDestination(Data.Destination)))
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bConstrainRetreatPath && IsValid(Data.Enemy)) Data.RetreatStart = Data.Enemy->GetActorLocation();
	if (!IsRetreatFlightConfigurationValid(Data))
	{
		return EStateTreeRunStatus::Failed;
	}
	if (!IsValid(Data.Enemy) || !IsValid(Data.CachedRoomSubsystem)
		|| !Data.CachedRoomSubsystem->BuildEnemyMovementRoomContext(Data.Enemy, Data.MovementRooms))
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.CapturedMovementState = Data.Enemy->GetCombatState();
	Data.CapturedMovementGeneration = Data.Enemy->GetPoolGameplayGeneration();
	Data.CapturedMovementLease = Data.Enemy->GetPoolLeaseSerial();
	Data.CapturedReturnHomeSerial = Data.bReturnHome ? Data.Enemy->GetReturnHomeRequestSerial() : 0;
	Data.CapturedSearchSerial = !Data.bReturnHome && Data.Enemy->GetSearchEpisode().bActive
		? Data.Enemy->GetSearchEpisode().Serial : 0;
	Data.CapturedCombatReservationRequest = Data.CapturedCombatTacticalSerial != 0
		? Data.Enemy->GetCombatReservationRequest() : 0;
	if (!BeginNoiseFlight(Data))
	{
		return EStateTreeRunStatus::Failed;
	}
	return (Data.bUsePlannedPath && !Data.bCombatNavigationPath) ? StartPlannedFlight(Data)
		: Data.bUseInitialPath ? StartInitialFlight(Data) : FinishNoiseFlight(Data, RequestFlightPath(Data));
}

EStateTreeRunStatus TickPath(FEnemyFlyToLocationTaskInstanceData& Data, float DeltaTime)
{
	if (Data.bInvestigateNoise && (!HasCurrentFlightRoom(Data) || !UpdateNoiseFlightDestination(Data)))
	{
		Data.MoveResult = EEnemyFlightMoveResult::Interrupted;
		StopFlyTask(Data);
		return EStateTreeRunStatus::Failed;
	}
	return FinishNoiseFlight(Data, TickFlightPath(Data, DeltaTime));
}

bool CheckDirectBoundary(FEnemyFlyToLocationTaskInstanceData& Data, float DeltaTime)
{
	return CheckDirectFlightBoundary(Data, DeltaTime);
}

void PrepareSpeedLimits(FEnemyFlyToLocationTaskInstanceData& Data)
{
	PrepareFlightSpeedLimits(Data);
}

void Stop(FEnemyFlyToLocationTaskInstanceData& Data)
{
	StopFlyTask(Data);
	EndNoiseFlight(Data);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightProgressTest,
	"Outlier.Enemy.Flight.Progress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAttackAimOwnershipTest,
	"Outlier.Enemy.Combat.AttackAimOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPassiveRotationTest,
	"Outlier.Enemy.Rotation.PassiveInterpolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPassiveRotationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FRotator Start(30.0f, 179.0f, 0.0f);
	const FRotator Desired(0.0f, -179.0f, 0.0f);
	const FRotator First = GetPassiveRotation(Start, Desired, 120.0f, 6.0f, 1.0f, 1.0f / 60.0f);
	TestTrue(TEXT("Pitch starts at the real direction and respects the speed cap"),
		First.Pitch < 30.0f && First.Pitch >= 28.0f);
	TestTrue(TEXT("Yaw crosses the wrap boundary by the short path"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(First.Yaw, Desired.Yaw)) < 2.0f);
	const FRotator Far = GetPassiveRotation(FRotator::ZeroRotator, FRotator(0, 90, 0), 120, 6, 1, 0.01f);
	const FRotator Near = GetPassiveRotation(FRotator::ZeroRotator, FRotator(0, 5, 0), 120, 6, 1, 0.01f);
	TestTrue(TEXT("Near-target rotation is slower than a distant turn"), Near.Yaw > 0 && Near.Yaw < Far.Yaw);
	for (const float Step : {1.0f / 120.0f, 1.0f / 30.0f, 0.2f})
	{
		FRotator Current(25, 0, 0);
		for (int32 Index = 0; Index < 1200; ++Index)
		{
			Current = GetPassiveRotation(Current, FRotator(0, 180, 0), 120, 6, 1, Step);
		}
		TestTrue(TEXT("A 180-degree turn reaches the completion tolerance at different frame rates"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(Current.Yaw, 180.0f)) <= 1.0f && FMath::Abs(Current.Pitch) <= 1.0f);
		TestTrue(TEXT("Completed rotation remains stable without micro oscillation"),
			GetPassiveRotation(Current, FRotator(0, 180, 0), 120, 6, 1, Step).Equals(Current));
	}
	TestTrue(TEXT("Zero speed preserves an intentional stationary setting"),
		GetPassiveRotation(Start, Desired, 0, 6, 1, 1).Equals(Start));
	TestTrue(TEXT("Negative time cannot rewind rotation"),
		GetPassiveRotation(Start, Desired, 120, 6, 1, -1).Equals(Start));
	TestTrue(TEXT("A large time step stops at the target rather than overshooting"),
		GetPassiveRotation(Start, Desired, 120, 6, 1, 10).Equals(Desired));
	return true;
}

bool FEnemyAttackAimOwnershipTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("AttackAimOwnership")));
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>();
	AEnemyAIController* Controller = World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Enemy exists"), Enemy)
		|| !TestNotNull(TEXT("Controller exists"), Controller))
	{
		return false;
	}
	Controller->Possess(Enemy);
	const FRotator AttackRotation(-25.0f, 70.0f, 0.0f);
	Controller->SetControlRotation(AttackRotation);
	Controller->SetAttackAimActive(true);

	// 기본 AI 갱신과 병렬 이동 회전을 순서대로 실행해 연사 사이의 덮어쓰기를 재현한다.
	Controller->UpdateControlRotation(0.1f);
	TestTrue(TEXT("AI update preserves attack pitch and yaw"),
		Controller->GetControlRotation().Equals(AttackRotation));
	TestFalse(TEXT("Movement rotation waits while attack owns aim"),
		RotateTowardLocation(*Enemy, *Controller, FVector(0, -500, 300), 360.0f, 5.0f, 1.0f));
	TestTrue(TEXT("Movement cannot overwrite attack aim"),
		Controller->GetControlRotation().Equals(AttackRotation));

	// 무기가 없어져도 공통 중단 경로가 소유권을 해제해야 한다.
	Enemy->StopCurrentWeaponAttack();
	TestFalse(TEXT("Weapon stop releases aim without a weapon"), Controller->IsAttackAimActive());
	Controller->UpdateControlRotation(0.1f);
	TestTrue(TEXT("Normal AI pitch reset resumes after stop"),
		FMath::IsNearlyZero(Controller->GetControlRotation().Pitch));
	TestTrue(TEXT("Movement rotation resumes after stop"),
		RotateTowardLocation(*Enemy, *Controller, FVector(0, -500, 300), 360.0f, 5.0f, 1.0f));

	Controller->SetAttackAimActive(true);
	Enemy->EnterStun();
	TestFalse(TEXT("Stun releases attack aim"), Controller->IsAttackAimActive());
	Controller->SetAttackAimActive(true);
	Controller->UnPossess();
	TestFalse(TEXT("Unpossess releases attack aim"), Controller->IsAttackAimActive());

	AAutoTurret* Turret = World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Stationary turret exists"), Turret))
	{
		return false;
	}
	Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
	Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
	FEnumProperty* Lifecycle = FindFProperty<FEnumProperty>(Turret->GetClass(), TEXT("TurretLifecycleState"));
	if (!TestNotNull(TEXT("Turret can select the existing active lifecycle"), Lifecycle))
	{
		return false;
	}
	Lifecycle->GetUnderlyingProperty()->SetIntPropertyValue(Lifecycle->ContainerPtrToValuePtr<void>(Turret),
		static_cast<int64>(EAutoTurretLifecycleState::Active));
	Controller->Possess(Turret);
	const FTransform Body = Turret->GetActorTransform();
	const FVector HitLocation = Turret->GetPawnViewLocation() + FVector(0, 1000, 100);
	FEnemyHitClue Hit;
	if (!TestTrue(TEXT("Turret accepts a positional clue"),
		Turret->CreateHitClue(HitLocation, World->GetTimeSeconds(), 6.0, Hit) && Turret->RecordHitClue(Hit)))
	{
		return false;
	}
	Turret->TickHitResponse(0.0f);
	TestTrue(TEXT("Turret starts direction confirmation"), Turret->IsHitDirectionConfirmationActive());
	TestFalse(TEXT("Stationary turret never adopts investigation flight"), Turret->CanRunHitInvestigationMovement());
	World->TimeSeconds += 2.1f;
	Turret->TickHitResponse(2.1f);
	TestFalse(TEXT("Turret confirmation finishes without location search"), Turret->HasHitResponse());
	RotateTowardLocation(*Turret, *Controller, HitLocation, 360.0f, 3.0f, 1.0f);
	TestTrue(TEXT("Confirmation and subsequent common alert facing keep the body fixed"),
		Turret->GetActorTransform().Equals(Body));
	TestTrue(TEXT("The head can turn while the body stays fixed"), FMath::Abs(Turret->GetViewRotation().Yaw) > 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightWaypointTurnTest,
	"Outlier.Enemy.Flight.WaypointTurn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightWaypointTurnTest::RunTest(const FString& Parameters)
{
	const float Straight = GetCornerSpeedLimit(FVector(100, 0, 0), FVector(100, 0, 0), 500, 5);
	const float Gentle = GetCornerSpeedLimit(FVector(100, 0, 0), FVector(100, 25, 0), 500, 5);
	const float RightAngle = GetCornerSpeedLimit(FVector(100, 0, 0), FVector(0, 100, 0), 500, 5);
	TestTrue(TEXT("Straight points do not introduce a corner speed cap"), Straight > 10000);
	TestTrue(TEXT("Sharper corners require lower speed"), Straight > Gentle && Gentle > RightAngle);
	TestTrue(TEXT("Right angle can be crossed without resetting velocity to zero"), RightAngle > 0);
	TestEqual(TEXT("Reversal plans a stop before turning"),
		GetCornerSpeedLimit(FVector(100, 0, 0), FVector(-100, 0, 0), 500, 5), 0.0f);
	TestEqual(TEXT("Duplicate points fail conservatively"),
		GetCornerSpeedLimit(FVector::ZeroVector, FVector(100, 0, 0), 500, 5), 0.0f);
	TestTrue(TEXT("Less clearance lowers corner speed"),
		GetCornerSpeedLimit(FVector(100, 0, 0), FVector(0, 100, 0), 500, 1) < RightAngle);
	TestEqual(TEXT("Braking starts before a zero-speed endpoint"), GetFlightSegmentSpeedLimit(0, 100, 500, 1), FMath::Sqrt(100000.0f));
	TestEqual(TEXT("Corner endpoint retains its planned speed"), GetFlightSegmentSpeedLimit(50, 0, 500, 1), 50.0f);
	TestEqual(TEXT("Minimum approach avoids stopping outside acceptance radius"), GetFlightSegmentSpeedLimit(0, 0, 500, 1), 1.0f);

	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightCleanupTest,
	"Outlier.Enemy.Flight.Cleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightCleanupTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("StateExit"), TEXT("Stun"), TEXT("Impact"), TEXT("Death"), TEXT("ControllerChanged")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyFlightCleanupTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("FlightCleanup")));
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>();
	AEnemyAIController* Controller = World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Enemy exists"), Enemy) || !TestNotNull(TEXT("Controller exists"), Controller))
	{
		return false;
	}
	Controller->Possess(Enemy);
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	FEnemyFlyToLocationTaskInstanceData Data;
	Data.Enemy = Enemy;
	Data.CachedController = Controller;
	Data.bOwnsMovement = true;
	Data.bWaitingForPath = true;
	Data.bRepathEpisode = true;
	Data.CachedCapsule = Enemy->GetCapsuleComponent();
	Data.PathWaypoints = {FVector::ZeroVector, FVector(500, 0, 0)};
	Data.WaypointSpeedLimits = {100.0f, 0.0f};
	Data.SpeedProfileState.TargetSpeed = 100.0f;
	Movement->MaxFlySpeed = 100.0f;
	Movement->Velocity = FVector(100, 0, 0);
	Enemy->AddMovementInput(FVector::ForwardVector, 1.0f, true);

	// 중단 사유가 달라도 Exit에서 같은 정리 경로를 사용한다. 새 Controller의 입력만 예외로 보호한다.
	if (Parameters == TEXT("Stun"))
	{
		Enemy->EnterStun();
	}
	else if (Parameters == TEXT("Impact"))
	{
		Enemy->ApplyMagneticPullVelocity(FVector(1000, 0, 0), Enemy);
		if (!TestTrue(TEXT("Impact reaction starts"), Enemy->BeginImpactReaction()))
		{
			return false;
		}
	}
	else if (Parameters == TEXT("Death"))
	{
		// 사망 시 StopLogic의 Exit 정리는 AI Controller를 파괴하기 전에 수행된다.
		Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
		TestTrue(TEXT("Death state is active before exit cleanup"), Enemy->IsDead());
	}
	else if (Parameters == TEXT("ControllerChanged"))
	{
		AEnemyAIController* Replacement = World->SpawnActor<AEnemyAIController>();
		if (!TestNotNull(TEXT("Replacement controller exists"), Replacement))
		{
			return false;
		}
		Replacement->Possess(Enemy);
		Movement->Velocity = FVector(250, 0, 0);
		Movement->MaxFlySpeed = 250.0f;
		Enemy->ConsumeMovementInputVector();
		Enemy->AddMovementInput(FVector::RightVector, 1.0f, true);
	}
	StopFlyTask(Data);
	TestTrue(TEXT("Exit releases path and capsule"), Data.PathWaypoints.IsEmpty() && !Data.CachedCapsule);
	TestTrue(TEXT("Exit releases cached corner speeds"), Data.WaypointSpeedLimits.IsEmpty());
	TestFalse(TEXT("Exit clears waiting and repath flags"), Data.bWaitingForPath || Data.bRepathEpisode);
	TestFalse(TEXT("Exit releases movement ownership"), Data.bOwnsMovement);
	TestEqual(TEXT("Exit clears profile state"), Data.SpeedProfileState.TargetSpeed, -1.0f);
	if (Parameters == TEXT("ControllerChanged"))
	{
		TestEqual(TEXT("Old task cannot change new controller speed"), Movement->MaxFlySpeed, 250.0f);
		TestEqual(TEXT("Old task cannot stop new controller velocity"), Movement->Velocity, FVector(250, 0, 0));
		TestEqual(TEXT("Old task cannot consume new controller input"), Enemy->GetPendingMovementInputVector(), FVector::RightVector);
	}
	else
	{
		TestTrue(TEXT("Exit clears pending input and velocity"),
			Enemy->GetPendingMovementInputVector().IsNearlyZero() && Movement->Velocity.IsNearlyZero());
		TestEqual(TEXT("Exit restores AI base speed"), Movement->MaxFlySpeed, FMath::Max(Enemy->GetRuntimeStat().MoveSpeed, 0.0f));
	}
	// 중복 종료도 정리 이후의 새 입력을 지우지 않아야 한다.
	Enemy->AddMovementInput(FVector::UpVector, 1.0f, true);
	const FVector InputAfterExit = Enemy->GetPendingMovementInputVector();
	StopFlyTask(Data);
	TestEqual(TEXT("Duplicate cleanup cannot consume later input"), Enemy->GetPendingMovementInputVector(), InputAfterExit);
	return true;
}

bool FEnemyFlightProgressTest::RunTest(const FString& Parameters)
{
	FEnemyFlyToLocationTaskInstanceData Data;
	TestFalse(TEXT("Sustained Move reselection does not invoke stop/restart callbacks"),
		FEnemyFlyToLocationTask().bShouldStateChangeOnReselect);
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
	Data.bReturnHome = true;
	Data.bWaitingForPath = true;
	Data.bRepathEpisode = false;
	Data.RepathAttempts = 0;
	Data.RetryElapsed = 0.0f;
	TestTrue(TEXT("Return failure yields to the persistent home-selection budget"), HasFlightRepathLimitExpired(Data));
	Data.bWaitingForPath = false;
	TestFalse(TEXT("Normal return travel keeps its separate movement deadline"), HasFlightRepathLimitExpired(Data));
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
	Data.bAllowShortRetreat = true;
	Data.RetreatStart = FVector(200, 0, 250);
	Data.Destination = FVector(500, 0, 250);
	TestFalse(TEXT("Short retreat cannot use large arrival tolerance"), IsRetreatFlightConfigurationValid(Data));
	Data.AcceptanceRadius = 25.0f;
	TestTrue(TEXT("Three metre short retreat retains safe path configuration"), IsRetreatFlightConfigurationValid(Data));
	Data.Destination = FVector(275, 0, 250);
	TestTrue(TEXT("Planned gain includes arrival tolerance"), IsRetreatFlightConfigurationValid(Data));
	Data.Destination.X = 274.0;
	TestFalse(TEXT("Small shuffle is not short retreat"), IsRetreatFlightConfigurationValid(Data));
	Data.Destination = FVector(501, 0, 250);
	TestFalse(TEXT("Short retreat stays within three metre displacement"), IsRetreatFlightConfigurationValid(Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPlannedFlightGeometryTest,
	"Outlier.Enemy.Flight.PlannedGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPlannedFlightGeometryTest::RunTest(const FString& Parameters)
{
	const FVector Start(100, 200, 300);
	TArray<FVector> Points;
	Points.Add(Start);
	for (int32 Index = 1; Index <= 10; ++Index)
	{
		Points.Add(Start + FVector(Index * 50, 0, 0));
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyInitialFlightGeometryTest,
	"Outlier.Enemy.Patrol.InitialPathGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyInitialFlightGeometryTest::RunTest(const FString& Parameters)
{
	const FVector Start(100.0, 200.0, 300.0);
	const FVector Goal = Start + FVector(1200.0, 0.0, 0.0);
	TArray<FVector> Points = { Start, Start + FVector(600.0, 200.0, 0.0), Goal };
	TestTrue(TEXT("Initial A* path may exceed orbit distance and segment limits"), IsInitialFlightGeometryValid(Points, Start, Goal, 512));
	TestFalse(TEXT("Stale start is rejected"), IsInitialFlightGeometryValid(Points, Start + FVector(30.0, 0.0, 0.0), Goal, 512));
	TestFalse(TEXT("Endpoint must match destination binding"), IsInitialFlightGeometryValid(Points, Start, Goal + FVector(10.0), 512));
	Points.Reset();
	TestFalse(TEXT("Missing waypoint binding is rejected"), IsInitialFlightGeometryValid(Points, Start, Goal, 512));
	TestFalse(TEXT("Existing assets do not opt into initial path reuse"), FEnemyFlyToLocationTaskInstanceData().bUseInitialPath);
	return true;
}
#endif

FEnemyFlyToLocationTask::FEnemyFlyToLocationTask()
{
	// 같은 Move 상태의 재선정은 Exit/Enter를 건너뛰어 경로와 가속 시간을 유지한다.
	// Select로 이탈하면 검증된 남은 구간만 짧게 인계하고, 안전 구간이 없으면 정지한다.
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
#if WITH_DEV_AUTOMATION_TESTS
	if (Enemy)
	{
		InstanceData.MaxCollisionChecks = FlightPathfinding::GetTestBudget(*Enemy->GetWorld(), InstanceData.MaxCollisionChecks);
	}
#endif
	InstanceData.CachedController = AIController;
	InstanceData.MoveResult = EEnemyFlightMoveResult::Invalid;
	InstanceData.MoveElapsed = 0.0f;
	InstanceData.NoProgressElapsed = 0.0f;
	InstanceData.bOwnsMovement = false;
	InstanceData.bNoiseSearchReady = false;
	InstanceData.CapturedNoiseSerial = 0;
	InstanceData.CapturedCombatTacticalSerial = Enemy && Enemy->GetCombatState() == EEnemyCombatState::Combat
		&& Enemy->GetRuntimeStat().Type == EEnemyType::Gun && Enemy->HasTrackableTarget()
		&& !InstanceData.bInvestigateNoise && !InstanceData.bReturnHome
		? Enemy->GetCombatTacticalEpisodeSerial() : 0;
	InstanceData.bTacticalSightReposition = Enemy && Enemy->NeedsCombatTacticalReposition();
	InstanceData.CapturedCombatReservationRequest = InstanceData.CapturedCombatTacticalSerial != 0
		? Enemy->GetCombatReservationRequest() : 0;
	InstanceData.bCombatNavigationPath = Enemy && InstanceData.CapturedCombatReservationRequest != 0
		&& Enemy->GetCombatNavigationPath(InstanceData.CapturedCombatReservationRequest,
			InstanceData.Destination, InstanceData.PlannedWaypoints, &InstanceData.bCombatNavigationProgressOnly);
	InstanceData.SpeedProfileState.Reset();
	InstanceData.PathWaypoints.Reset();
	InstanceData.WaypointSpeedLimits.Reset();
	InstanceData.CachedRoom.Reset();
	InstanceData.CachedCapsule = nullptr;
	InstanceData.CachedRoomSubsystem = nullptr;
	InstanceData.MovementRooms = FEnemyMovementRoomContext();
	InstanceData.bWaitingForPath = false;
	InstanceData.bRepathEpisode = false;
	InstanceData.RepathAttempts = 0;
	InstanceData.RetryElapsed = 0.0f;
	InstanceData.RetryDelay = 0.0f;
	InstanceData.SegmentCheckElapsed = 0.0f;
	InstanceData.PathStatus = EFlightPathStatus::Unavailable;
	LogFlight(TEXT("Enter"), InstanceData, &Transition);
	if (!Enemy || !Enemy->HasAuthority() || !AIController)
	{
		LogFlight(TEXT("Rejected: missing enemy/authority/AIController"), InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (!PrepareNoiseFlight(InstanceData))
	{
		return EStateTreeRunStatus::Failed;
	}
	if (InstanceData.bReturnHome && !Enemy->GetSelectedReturnHomeDestination(InstanceData.Destination))
	{
		return EStateTreeRunStatus::Failed;
	}

	// 이전 NavMesh 이동 요청이 비행 입력과 경쟁하지 않도록 진입 시 정리한다.
	AIController->StopMovement();
	InstanceData.bOwnsMovement = true;
	Enemy->ConsumeMovementInputVector();
	InstanceData.LastDestination = InstanceData.Destination;
	InstanceData.CachedCapsule = Enemy->GetCapsuleComponent();
	InstanceData.CachedRoomSubsystem = Enemy->GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	InstanceData.PathRoomTag = Enemy->GetDefaultRoomTag();
	InstanceData.CachedRoom = InstanceData.CachedRoomSubsystem
		? InstanceData.CachedRoomSubsystem->GetRegisteredRoomVolume(InstanceData.PathRoomTag) : nullptr;
	InstanceData.CapturedMovementState = Enemy->GetCombatState();
	InstanceData.CapturedMovementGeneration = Enemy->GetPoolGameplayGeneration();
	InstanceData.CapturedMovementLease = Enemy->GetPoolLeaseSerial();
	InstanceData.CapturedReturnHomeSerial = InstanceData.bReturnHome ? Enemy->GetReturnHomeRequestSerial() : 0;
	InstanceData.CapturedSearchSerial = !InstanceData.bReturnHome && Enemy->GetSearchEpisode().bActive
		? Enemy->GetSearchEpisode().Serial : 0;
	if (!InstanceData.CachedRoomSubsystem
		|| !InstanceData.CachedRoomSubsystem->BuildEnemyMovementRoomContext(Enemy, InstanceData.MovementRooms))
	{
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	InstanceData.BestDistance = FVector::Distance(Enemy->GetActorLocation(), InstanceData.Destination);
	// A sibling query task cannot bind its transient output into this state. Capture the actual move origin.
	if (InstanceData.bConstrainRetreatPath) InstanceData.RetreatStart = Enemy->GetActorLocation();
	if (InstanceData.Destination.ContainsNaN() || !IsRetreatFlightConfigurationValid(InstanceData))
	{
		InstanceData.PathStatus = EFlightPathStatus::InvalidRequest;
		LogFlight(TEXT("Rejected: invalid destination or retreat configuration"), InstanceData);
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (!InstanceData.bCombatNavigationPath && InstanceData.bUseInitialPath && (!InstanceData.bUsePathfinding || InstanceData.bUsePlannedPath
		|| InstanceData.bConstrainRetreatPath || InstanceData.GoalSearchRadius != 0.0f))
	{
		InstanceData.PathStatus = EFlightPathStatus::InvalidRequest;
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (InstanceData.bCombatNavigationPath || InstanceData.bUsePathfinding || InstanceData.bUsePlannedPath)
	{
		if (!InstanceData.bCombatNavigationPath && InstanceData.bUsePathfinding && InstanceData.bUsePlannedPath)
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
		if (!BeginNoiseFlight(InstanceData))
		{
			StopFlyTask(InstanceData);
			return EStateTreeRunStatus::Failed;
		}
		if (InstanceData.bCombatNavigationPath)
		{
			return StartInitialFlight(InstanceData);
		}
		if (InstanceData.bUsePlannedPath)
		{
			return StartPlannedFlight(InstanceData);
		}
		if (InstanceData.bUseInitialPath)
		{
			return StartInitialFlight(InstanceData);
		}
		return FinishNoiseFlight(InstanceData, RequestFlightPath(InstanceData));
	}

	// 직접 비행도 같은 경계 계약을 사용한다. A* 옵션을 꺼서 미등록 구간을 통과할 수 없다.
	const FVector Offset = InstanceData.CachedCapsule->GetComponentLocation() - Enemy->GetActorLocation();
	InstanceData.PathStatus = FlightPathfinding::CheckSegment(*Enemy->GetWorld(), *InstanceData.CachedCapsule,
		MakeFlightPathRequest(InstanceData, InstanceData.Destination + Offset));
	if (InstanceData.PathStatus != EFlightPathStatus::Success)
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
	if (bAlreadyAtDestination)
	{
		StopFlyTask(InstanceData);
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
	AEnemyBase* Enemy = InstanceData.Enemy;
	AAIController* AIController = InstanceData.CachedController;
	// 저장된 트리는 매 Tick PatrolPointA를 다시 복사한다. 귀환은 그 입력 대신
	// 현재 요청이 소유한 목적지를 사용해야 대체 경로가 원래 막힌 위치로 되돌아가지 않는다.
	if (IsValid(Enemy) && InstanceData.bReturnHome
		&& !Enemy->GetSelectedReturnHomeDestination(InstanceData.Destination))
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Interrupted;
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !IsValid(AIController)
		|| Enemy->GetController() != AIController || InstanceData.Destination.ContainsNaN())
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Invalid;
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (!HasCurrentFlightRoom(InstanceData))
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Interrupted;
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
	if (!UpdateNoiseFlightDestination(InstanceData))
	{
		return EStateTreeRunStatus::Failed;
	}

	// Keep the task resumable if hacking is cancelled before the state transition.
	if (Enemy->IsPossessionInProgress())
	{
		InstanceData.SpeedProfileState.Reset();
		InstanceData.SpeedProfileState.SetPath(InstanceData.PathWaypoints);
		Enemy->ConsumeMovementInputVector();
		if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		return EStateTreeRunStatus::Running;
	}

	if (InstanceData.bCombatNavigationPath || InstanceData.bUsePathfinding || InstanceData.bUsePlannedPath)
	{
		return FinishNoiseFlight(InstanceData, TickFlightPath(InstanceData, DeltaTime));
	}

	ApplyFlyTaskSpeedMultiplier(*Enemy, InstanceData.SpeedMultiplier);
	if (!CheckDirectFlightBoundary(InstanceData, DeltaTime))
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Blocked;
		StopFlyTask(InstanceData);
		return EStateTreeRunStatus::Failed;
	}
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
		if (InstanceData.bInvestigateNoise)
		{
			Enemy->UpdateNoiseInvestigationTravelDirection(InstanceData.CapturedNoiseSerial, ToDestination, InstanceData.RotationSpeed);
		}
		RotateTowardLocation(
			*Enemy,
			*AIController,
			InstanceData.Destination,
			InstanceData.RotationSpeed,
			0.0f,
			DeltaTime);
	}

	// XY와 Z를 함께 입력해 PatrolPoint의 비행 고도를 그대로 유지한다.
	if (InstanceData.SpeedProfile.IsEnabled())
	{
		if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
		{
			Movement->MaxFlySpeed = InstanceData.SpeedProfileState.Update(InstanceData.SpeedProfile,
				Movement->MaxFlySpeed, Movement->Velocity.Size(), FMath::Max(ToDestination.Size() - AcceptanceRadius, 0.0),
				DeltaTime, true, Enemy, TEXT("EnemyFly"));
			Movement->MaxFlySpeed = FMath::Min(Movement->MaxFlySpeed,
				ToDestination.Size() / FMath::Max(DeltaTime, UE_SMALL_NUMBER));
			Movement->Velocity = Movement->Velocity.GetClampedToMaxSize(Movement->MaxFlySpeed);
		}
	}
	Enemy->AddMovementInput(ToDestination.GetSafeNormal());
	return EStateTreeRunStatus::Running;
}

void FEnemyFlyToLocationTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (IsValid(InstanceData.Enemy) && InstanceData.Enemy->OfferCombatFlightHandoff(InstanceData))
	{
		// 검증 경로의 소유권을 Pawn에 넘겼다. 이 Exit는 물리 속도나 새 소유자의 상태를 지우지 않는다.
		LogFlight(TEXT("ExitToHandoff"), InstanceData, &Transition);
		InstanceData.PathWaypoints.Reset();
		InstanceData.WaypointSpeedLimits.Reset();
		InstanceData.SpeedProfileState.Reset();
		InstanceData.CachedController = nullptr;
		return;
	}
	if (InstanceData.MoveResult == EEnemyFlightMoveResult::Moving)
	{
		InstanceData.MoveResult = EEnemyFlightMoveResult::Interrupted;
	}
	StopFlyTask(InstanceData);
	LogFlight(TEXT("Exit"), InstanceData, &Transition);
	EndNoiseFlight(InstanceData);
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
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !IsValid(Data.CachedController))
	{
		return false;
	}
	if (Enemy->GetController() != Data.CachedController || Data.CachedController->GetPawn() != Enemy)
	{
		return false;
	}
	if (Enemy->IsDead() || !Enemy->CanUseEnemyPerception() || Enemy->IsPossessionInProgress()
		|| Enemy->GetCombatState() == EEnemyCombatState::Stun || !Enemy->IsPlayerCurrentlyVisible())
	{
		return false;
	}
	return Data.CapturedTarget.IsValid() && !Enemy->IsTargetOutsideWeaponRange(Data.CapturedTarget.Get())
		&& Data.CachedController->GetCachedVisibleTarget() == Data.CapturedTarget.Get()
		&& Enemy->GetPoolGameplayGeneration() == Data.CapturedGeneration
		&& Enemy->GetPoolLeaseSerial() == Data.CapturedLease;
}

const TCHAR* GetStationaryFireReason(const FEnemyStationaryFireTaskInstanceData& Data)
{
	if (!IsStationaryFireConfigurationValid(Data))
	{
		return TEXT("InvalidDurationRange");
	}
	const AEnemyBase* Enemy = Data.Enemy;
	if (!IsValid(Enemy))
	{
		return TEXT("InvalidEnemy");
	}
	if (!Enemy->HasAuthority())
	{
		return TEXT("NoAuthority");
	}
	if (!IsValid(Data.CachedController) || Enemy->GetController() != Data.CachedController
		|| Data.CachedController->GetPawn() != Enemy)
	{
		return TEXT("ControllerChanged");
	}
	if (Enemy->IsDead())
	{
		return TEXT("Dead");
	}
	if (!Enemy->CanUseEnemyPerception())
	{
		return TEXT("PerceptionDisabled");
	}
	if (Enemy->IsPossessionInProgress())
	{
		return TEXT("PossessionInProgress");
	}
	if (Enemy->GetCombatState() == EEnemyCombatState::Stun)
	{
		return TEXT("Stun");
	}
	if (!Enemy->IsPlayerCurrentlyVisible())
	{
		return TEXT("SightLost");
	}
	if (!Data.CapturedTarget.IsValid()
		|| Data.CachedController->GetCachedVisibleTarget() != Data.CapturedTarget.Get())
	{
		return TEXT("TargetChangedOrInvalid");
	}
	if (Enemy->IsTargetOutsideWeaponRange(Data.CapturedTarget.Get()))
	{
		return TEXT("WeaponRangeLost");
	}
	if (Enemy->GetPoolGameplayGeneration() != Data.CapturedGeneration
		|| Enemy->GetPoolLeaseSerial() != Data.CapturedLease)
	{
		return TEXT("PoolLifetimeChanged");
	}
	return TEXT("Valid");
}

void LogStationaryFire(const TCHAR* Phase, const FEnemyStationaryFireTaskInstanceData& Data,
	const FStateTreeTransitionResult* Transition = nullptr)
{
	if (CVarEnemyFlightDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
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
	Data.Enemy->ClearCombatFlightHandoff();
	Data.Enemy->ReleaseCombatReservation();
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
	if (Data.Elapsed < Data.SelectedDuration)
	{
		return EStateTreeRunStatus::Running;
	}
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
	if (bValid && !InstanceData.Enemy->IsEnemyPossessed() && InstanceData.Enemy->GetRuntimeStat().Type == EEnemyType::Gun)
	{
		AAIController* Controller = ResolveAIController(InstanceData.Enemy);
		const FVector Input = ResolveFacingLocation(InstanceData, *Controller);
		const FVector Origin = InstanceData.Enemy->GetCombatShotOrigin();
		// 공격 Task와 같은 좌표를 받았는지 진입 시에만 남긴다. 시선 Task의 바인딩 오류를 실제 레벨에서 확인한다.
		if (FVector::DistSquared2D(Origin, Input) <= FMath::Square(500.0))
		{
			UE_LOG(LogEnemyFlight, Display, TEXT("[EnemyAimTask] Task=MaintainFacing.Enter Enemy=%s Actor=%s Preferred=%d Input=%s Origin=%s InputPitch=%.3f ControlPitch=%.3f AttackOwns=%d"),
				*InstanceData.Enemy->GetName(), *GetNameSafe(InstanceData.TargetActor), InstanceData.bUsePreferredVisibleTarget,
				*Input.ToCompactString(), *Origin.ToCompactString(), (Input - Origin).Rotation().Pitch,
				Controller->GetControlRotation().Pitch, ShouldYieldRotationToAttack(*Controller));
		}
	}
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
	InstanceData.bScanInitialized = false;
	InstanceData.ScanElapsedTime = 0.0f;
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
	// 조사는 자체 완료 조건을 유지한다. 조사 중에도 청각 인지 시간을 누적해 Combat을 확정할 수 있다.
	// 이 분기에서는 미발견 타이머나 Alert 회전으로 조사 이동·수색을 끊지 않는다.
	if (Enemy->IsLocationInvestigationGoal() || Enemy->IsLocationSearchActive())
	{
		if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(AIController))
		{
			EnemyAIController->ResolveInitialAwarenessAlert();
		}
		InstanceData.VisibleElapsedTime = InstanceData.LostElapsedTime = 0.0f;
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

	const float SafeDeltaTime = FMath::IsFinite(DeltaTime) ? FMath::Max(DeltaTime, 0.0f) : 0.0f;
	if (bPlayerVisible)
	{
		InstanceData.bScanInitialized = false;
	}
	if (!ShouldYieldPassiveRotation(*Enemy, *AIController))
	{
		if (bPlayerVisible || Cast<AAutoTurret>(Enemy))
		{
			// 터렛은 전용 헤드 탐색/기본 방향 복귀를 유지한다. 공통 탐색으로 본체를 돌리지 않는다.
			RotateTowardLocation(*Enemy, *AIController, FacingLocation,
				InstanceData.RotationSpeed, 0.0f, SafeDeltaTime);
		}
		else
		{
			const double ClueAt = Enemy->GetLastAlertClueAt();
			if (!InstanceData.bScanInitialized || InstanceData.ScanClueAt != ClueAt)
			{
				// 실제 새 단서만 중심을 갱신한다. 이동/중복 조회로 중심이나 경계 시계를 재시작하지 않는다.
				InstanceData.bScanInitialized = true;
				InstanceData.ScanClueAt = ClueAt;
				const FVector Direction = FacingLocation - Enemy->GetActorLocation();
				InstanceData.ScanCenterYaw = Direction.SizeSquared2D() > 1.0f && !Direction.ContainsNaN()
					? Direction.Rotation().Yaw : AIController->GetControlRotation().Yaw;
				InstanceData.ScanElapsedTime = 0.0f;
			}
			const float HalfPeriod = FMath::IsFinite(InstanceData.ScanHalfPeriod)
				? FMath::Max(0.1f, InstanceData.ScanHalfPeriod) : 1.5f;
			const float HalfAngle = FMath::IsFinite(InstanceData.ScanHalfAngle)
				? FMath::Clamp(InstanceData.ScanHalfAngle, 0.0f, 180.0f) : 35.0f;
			InstanceData.ScanElapsedTime = FMath::Fmod(InstanceData.ScanElapsedTime + SafeDeltaTime, HalfPeriod * 2.0f);
			const float Offset = HalfAngle * FMath::Sin(PI * InstanceData.ScanElapsedTime / HalfPeriod);
			const FRotator Desired(0.0f, InstanceData.ScanCenterYaw + Offset, 0.0f);
			const FRotator Next = GetPassiveRotation(AIController->GetControlRotation(), Desired,
				InstanceData.RotationSpeed, InstanceData.RotationSlowdownRate, InstanceData.AngleTolerance, SafeDeltaTime);
			AIController->SetControlRotation(Next);
			Enemy->SetActorRotation(FRotator(0.0f, Next.Yaw, 0.0f));
		}
	}

	// 최초 시각·청각 발견은 대상별 인지와 Pawn의 최소 경계 기한을 함께 판정한다.
	// 기존 에셋에 저장된 LostCommitDuration=5는 이 경로에 적용하지 않는다.
	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(AIController))
	{
		if (EnemyAIController->ResolveInitialAwarenessAlert())
		{
			return EStateTreeRunStatus::Running;
		}
	}

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

	if (ShouldYieldRotationToAttack(*AIController))
	{
		return EStateTreeRunStatus::Running;
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
	// 수색은 확정한 단서 방향을 중심으로 훑는다. 숨은 대상의 현재 좌표는 읽지 않는다.
	float InitialCluePitch = 0.0f;
	if (Enemy->GetSearchLookPitch(InitialCluePitch))
	{
		InstanceData.BaseControlRotation.Pitch = InitialCluePitch;
		InstanceData.BaseControlRotation.Yaw = (Enemy->GetSearchEpisode().LookLocation - Enemy->GetCombatShotOrigin()).Rotation().Yaw;
	}
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

	// 공격/피격 확인/피격 접근이 소유한 회전을 덮지 않고, 보류 중에는 두리번거리기 단계도 소비하지 않는다.
	if (ShouldYieldPassiveRotation(*Enemy, *AIController))
	{
		return EStateTreeRunStatus::Running;
	}

	if (!InstanceData.Steps.IsValidIndex(InstanceData.CurrentStepIndex))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	const FEnemyLookAroundStep& Step = InstanceData.Steps[InstanceData.CurrentStepIndex];
	// 시작 시점의 방향을 기준으로 계산해 각 단계의 회전 오차가 다음 단계에 누적되지 않게 한다.
	FRotator DesiredRotation =
		(InstanceData.BaseControlRotation + Step.RelativeRotation).GetNormalized();
	// 단서가 있는 수색만 높이를 확인한다. 일반 경계는 기존 수평 회전과 속도를 유지한다.
	float CluePitch = 0.0f;
	DesiredRotation.Pitch = Enemy->GetSearchLookPitch(CluePitch) ? CluePitch : 0.0f;
	DesiredRotation.Roll = 0.0f;

	const float SafeDeltaTime = FMath::IsFinite(DeltaTime) ? FMath::Max(DeltaTime, 0.0f) : 0.0f;
	const FRotator NewRotation = GetPassiveRotation(AIController->GetControlRotation(), DesiredRotation,
		InstanceData.RotationSpeed, InstanceData.RotationSlowdownRate, InstanceData.AngleTolerance, SafeDeltaTime);
	AIController->SetControlRotation(NewRotation);
	Enemy->SetActorRotation(FRotator(0.0f, NewRotation.Yaw, 0.0f));

	const float YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		NewRotation.Yaw,
		DesiredRotation.Yaw));
	const float PitchError = FMath::Abs(FMath::FindDeltaAngleDegrees(
		NewRotation.Pitch,
		DesiredRotation.Pitch));
	const float Tolerance = FMath::IsFinite(InstanceData.AngleTolerance) ? FMath::Max(0.0f, InstanceData.AngleTolerance) : 1.0f;
	if (YawError > Tolerance || PitchError > Tolerance)
	{
		InstanceData.ElapsedHoldTime = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	InstanceData.ElapsedHoldTime += SafeDeltaTime;
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
