#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Shooter/ShooterCharacter.h"

#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyCombatNavigation.h"
#include "Enemy/VECDrone.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/LaserWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Drone/FlightPathfinding.h"
#include "Enemy/EnemyAIController.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_ActorBase.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "StateTreeAsyncExecutionContext.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Misc/AutomationTest.h"
#endif

namespace
{
TAutoConsoleVariable<int32> CVarEnemyPatrolDebug(
	TEXT("outlier.Enemy.PatrolDebug"), 0, TEXT("Log patrol selection, rejection and reservation lifetime."));
DEFINE_LOG_CATEGORY_STATIC(LogEnemyPatrol, Log, All);

bool IsPatrolLifetimeCurrent(const FEnemyPatrolTaskInstanceData& Data)
{
	const AEnemyBase* Enemy = Data.Enemy;
	const UCapsuleComponent* Capsule = IsValid(Enemy) ? Enemy->GetCapsuleComponent() : nullptr;
	ARoomVolume* Home = IsValid(Enemy) && IsValid(Data.CombatSubsystem)
		? Data.CombatSubsystem->GetRegisteredRoomVolume(Enemy->GetDefaultRoomTag()) : nullptr;
	FFlightPathRequest HomeRequest;
	HomeRequest.bRequireRoom = true;
	HomeRequest.Room = Home;
	// 현재 RoomTag가 남아 있어도 실제 캡슐이 귀환하기 전에는 순찰을 시작하지 않는다.
	const bool bInsideHome = Capsule && Home && FlightPathfinding::IsInsideMovementRooms(HomeRequest,
		Capsule->GetComponentLocation(), FlightPathfinding::GetCapsuleWorldExtent(
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetComponentQuat()));
	return IsValid(Enemy) && Enemy->HasAuthority() && !Enemy->IsDead()
		&& bInsideHome
		&& Enemy->CanUseEnemyPerception() && !Enemy->IsAIControlSuppressed()
		&& Enemy->GetCombatState() == EEnemyCombatState::NonCombat
		&& Enemy->GetNonCombatBehavior() == EEnemyNonCombatBehavior::PatrolRoute
		&& Data.Controller.IsValid() && Enemy->GetController() == Data.Controller.Get()
		&& Enemy->GetPoolGameplayGeneration() == Data.Generation
		&& Enemy->GetPoolLeaseSerial() == Data.Lease;
}

bool IsPatrolConfigurationValid(const FEnemyPatrolTaskInstanceData& Data)
{
	return FMath::IsFinite(Data.AcceptanceRadius) && Data.AcceptanceRadius > 0.0f
		&& FMath::IsFinite(Data.DestinationSeparation) && Data.DestinationSeparation > 0.0f
		&& FMath::IsFinite(Data.RetryInterval) && Data.RetryInterval > 0.0f
		&& FMath::IsFinite(Data.CellSize) && Data.CellSize > 0.0f
		&& FMath::IsFinite(Data.Clearance) && Data.Clearance >= 0.0f
		&& FMath::IsFinite(Data.SearchPadding) && Data.SearchPadding > 0.0f
		&& FMath::IsFinite(Data.MaxSearchMilliseconds) && Data.MaxSearchMilliseconds > 0.0f
		&& Data.MaxSearchNodes > 0 && Data.MaxCollisionChecks > 0;
}

void ReleasePatrol(FEnemyPatrolTaskInstanceData& Data)
{
	if (IsValid(Data.RoomSubsystem))
	{
		Data.RoomSubsystem->ReleasePatrolDestination(Data.Enemy, Data.Generation, Data.Lease, Data.Controller.Get());
		if (CVarEnemyPatrolDebug.GetValueOnGameThread())
		{
			UE_LOG(LogEnemyPatrol, Log, TEXT("[EnemyPatrol] ReleaseRequested Enemy=%s Generation=%d Lease=%d Controller=%s"),
				*GetNameSafe(Data.Enemy), Data.Generation, Data.Lease, *GetNameSafe(Data.Controller.Get()));
		}
	}
}

void LogPatrolCandidate(const FEnemyPatrolTaskInstanceData& Data, int32 Index, const FFlightPathResult& Result)
{
	if (!CVarEnemyPatrolDebug.GetValueOnGameThread())
	{
		return;
	}
	UE_LOG(LogEnemyPatrol, Log, TEXT("[EnemyPatrol] Enemy=%s Point=%d Status=%s Checks=%d Nodes=%d Ms=%.3f Waypoints=%d"),
		*GetNameSafe(Data.Enemy), Index, *UEnum::GetValueAsString(Result.Status),
		Result.CollisionChecks, Result.SearchNodes, Result.ElapsedMilliseconds, Result.Waypoints.Num());
}

EStateTreeRunStatus SelectPatrolDestination(FEnemyPatrolTaskInstanceData& Data)
{
	if (!IsPatrolLifetimeCurrent(Data) || !IsValid(Data.RoomSubsystem) || !IsValid(Data.CombatSubsystem))
	{
		ReleasePatrol(Data);
		return EStateTreeRunStatus::Failed;
	}
	AEnemyBase& Enemy = *Data.Enemy;
	const double Now = Enemy.GetWorld()->GetTimeSeconds();
	// Root 재진입도 Pawn의 탐색 간격을 공유한다. 기다리는 프레임에는 경로 탐색을 하지 않는다.
	if (Now < Enemy.GetNextFlightPathRequestTime())
	{
		return EStateTreeRunStatus::Running;
	}
	Enemy.SetNextFlightPathRequestTime(Now + Data.RetryInterval);
	ReleasePatrol(Data);
	Data.bHasDestination = false;
	Data.SelectedWaypoints.Reset();
	Data.PathStatus = EFlightPathStatus::Unavailable;
	ARoomVolume* Room = Data.CombatSubsystem->GetRegisteredRoomVolume(Enemy.GetDefaultRoomTag());
	UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	if (!IsValid(Room) || !IsValid(Capsule))
	{
		return EStateTreeRunStatus::Failed;
	}

	const FVector Points[2] = { Enemy.GetPatrolPointA(), Enemy.GetPatrolPointB() };
	int32 Order[2] = { Data.NextPointIndex, 1 - Data.NextPointIndex };
	const bool bPreferredUsed = Data.RoomSubsystem->IsPatrolDestinationUsed(&Enemy, Points[Order[0]], Data.DestinationSeparation);
	const bool bOtherUsed = Data.RoomSubsystem->IsPatrolDestinationUsed(&Enemy, Points[Order[1]], Data.DestinationSeparation);
	if (bPreferredUsed && !bOtherUsed)
	{
		Swap(Order[0], Order[1]);
	}

	const FVector Offset = Capsule->GetComponentLocation() - Enemy.GetActorLocation();
	const double Started = FPlatformTime::Seconds();
	int32 RemainingNodes = Data.MaxSearchNodes;
	int32 RemainingChecks = Data.MaxCollisionChecks;
	bool bBudgetExceeded = false;
	for (int32 Index : Order)
	{
		// 스폰 지점 A 또는 겹치는 A/B를 성공 처리하면 매 프레임 같은 지점으로 재진입한다.
		if (Points[Index].ContainsNaN()
			|| FVector::DistSquared(Enemy.GetActorLocation(), Points[Index]) <= FMath::Square(Data.AcceptanceRadius))
		{
			continue;
		}
		const double RemainingMs = Data.MaxSearchMilliseconds - (FPlatformTime::Seconds() - Started) * 1000.0;
		if (RemainingNodes <= 0 || RemainingChecks <= 0 || RemainingMs <= 0.0)
		{
			bBudgetExceeded = true;
			break;
		}
		FFlightPathRequest Request;
		Request.Start = Capsule->GetComponentLocation();
		Request.Goal = Points[Index] + Offset;
		Request.SearchBounds = (FBox(Request.Start, Request.Start) + Request.Goal).ExpandBy(Data.SearchPadding);
		Request.CellSize = Data.CellSize;
		Request.Clearance = Data.Clearance;
		Request.bRequireRoom = true;
		Request.Room = Room;
		Request.MaxSearchNodes = RemainingNodes;
		Request.MaxCollisionChecks = RemainingChecks;
		Request.MaxSearchMilliseconds = RemainingMs;
		// 고정 순찰점은 보정하지 않는다. 벽 속의 B를 다른 좌표로 바꾸지 않고 A를 검사한다.
		const FFlightPathResult Result = FlightPathfinding::FindPath(*Enemy.GetWorld(), *Capsule, Request);
		RemainingNodes -= Result.SearchNodes;
		RemainingChecks -= Result.CollisionChecks;
		Data.PathStatus = Result.Status;
		bBudgetExceeded |= Result.Status == EFlightPathStatus::BudgetExceeded;
		LogPatrolCandidate(Data, Index, Result);
		if (Result.Status != EFlightPathStatus::Success)
		{
			continue;
		}
		if (!Data.RoomSubsystem->ReservePatrolDestination(&Enemy, Points[Index]))
		{
			return EStateTreeRunStatus::Failed;
		}
		Data.Destination = Points[Index];
		for (const FVector& Point : Result.Waypoints)
		{
			Data.SelectedWaypoints.Add(Point - Offset);
		}
		Data.NextPointIndex = 1 - Index;
		Data.bHasDestination = true;
		if (CVarEnemyPatrolDebug.GetValueOnGameThread())
		{
			UE_LOG(LogEnemyPatrol, Log, TEXT("[EnemyPatrol] Selected Enemy=%s Point=%d Destination=%s NextPoint=%d"),
				*GetNameSafe(&Enemy), Index, *Data.Destination.ToCompactString(), Data.NextPointIndex);
		}
		return EStateTreeRunStatus::Succeeded;
	}
	// 예산 부족은 도달 불가능과 구분한다. 둘 다 Guard에서 기다린 뒤 제한된 간격으로 재검사한다.
	if (bBudgetExceeded)
	{
		Data.PathStatus = EFlightPathStatus::BudgetExceeded;
	}
	if (CVarEnemyPatrolDebug.GetValueOnGameThread())
	{
		UE_LOG(LogEnemyPatrol, Log, TEXT("[EnemyPatrol] Enemy=%s Guard Status=%s"),
			*GetNameSafe(&Enemy), *UEnum::GetValueAsString(Data.PathStatus));
	}
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus EnterPatrol(FEnemyPatrolTaskInstanceData& Data)
{
	if (!IsValid(Data.Enemy))
	{
		return EStateTreeRunStatus::Failed;
	}
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Data.Enemy->GetController());
	const int32 Generation = Data.Enemy->GetPoolGameplayGeneration();
	const int32 Lease = Data.Enemy->GetPoolLeaseSerial();
	if (Data.Controller.Get() != Controller || Data.Generation != Generation || Data.Lease != Lease)
	{
		Data.NextPointIndex = 1;
	}
	Data.Controller = Controller;
	Data.Generation = Generation;
	Data.Lease = Lease;
	Data.RoomSubsystem = Data.Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	Data.CombatSubsystem = Data.Enemy->GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	Data.bHasDestination = false;
	Data.SelectedWaypoints.Reset();
	Data.PathStatus = EFlightPathStatus::Unavailable;
	if (!IsPatrolLifetimeCurrent(Data))
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Data.Action == EEnemyPatrolAction::Session)
	{
		return EStateTreeRunStatus::Running;
	}
	if (Data.Action == EEnemyPatrolAction::Release)
	{
		ReleasePatrol(Data);
		Controller->StopMovement();
		Data.Enemy->ConsumeMovementInputVector();
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		return EStateTreeRunStatus::Succeeded;
	}
	if (!IsPatrolConfigurationValid(Data))
	{
		ReleasePatrol(Data);
		Data.PathStatus = EFlightPathStatus::InvalidRequest;
		return EStateTreeRunStatus::Failed;
	}
	return SelectPatrolDestination(Data);
}

EStateTreeRunStatus TickPatrol(FEnemyPatrolTaskInstanceData& Data)
{
	if (!IsPatrolLifetimeCurrent(Data))
	{
		ReleasePatrol(Data);
		Data.bHasDestination = false;
		Data.SelectedWaypoints.Reset();
		Data.PathStatus = EFlightPathStatus::Cancelled;
		return EStateTreeRunStatus::Failed;
	}
	if (Data.Action == EEnemyPatrolAction::Select)
	{
		return Data.bHasDestination ? EStateTreeRunStatus::Succeeded : SelectPatrolDestination(Data);
	}
	return Data.Action == EEnemyPatrolAction::Release ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

void ExitPatrol(FEnemyPatrolTaskInstanceData& Data)
{
	// Select 성공 직후에는 Move가 예약을 사용한다. 부모 Session 종료 때만 최종 해제한다.
	if (Data.Action == EEnemyPatrolAction::Session)
	{
		ReleasePatrol(Data);
	}
}
}

FEnemyPatrolTask::FEnemyPatrolTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = true;
}

FEnemyPatrolSessionTask::FEnemyPatrolSessionTask()
{
	bShouldStateChangeOnReselect = false;
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif
}

EStateTreeRunStatus FEnemyPatrolSessionTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Action = EEnemyPatrolAction::Session;
	return EnterPatrol(Data);
}

EStateTreeRunStatus FEnemyPatrolTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	return EnterPatrol(Context.GetInstanceData(*this));
}

EStateTreeRunStatus FEnemyPatrolTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	return TickPatrol(Context.GetInstanceData(*this));
}

void FEnemyPatrolTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	ExitPatrol(Context.GetInstanceData(*this));
}

#if WITH_DEV_AUTOMATION_TESTS
namespace EnemyPatrolTesting
{
EStateTreeRunStatus Enter(FEnemyPatrolTaskInstanceData& Data) { return EnterPatrol(Data); }
EStateTreeRunStatus Tick(FEnemyPatrolTaskInstanceData& Data) { return TickPatrol(Data); }
void Exit(FEnemyPatrolTaskInstanceData& Data) { ExitPatrol(Data); }
}
#endif

namespace
{
constexpr float MinimumTacticalFlightZ = 150.0f;

DEFINE_LOG_CATEGORY_STATIC(LogEnemyEQS, Log, All);
TAutoConsoleVariable<int32> CVarEnemyEQSDebug(
	TEXT("outlier.Enemy.EQSDebug"), 0,
	TEXT("Log enemy EQS. 0: off, 1: request/result/path, 2: candidate rejection details (debug builds)."));

void LogEnvQueryRejections(const FEnvQueryInstance& Query, const FEnemyRunEnvQueryTaskInstanceData& Data)
{
#if USE_EQS_DEBUGGER
	if (CVarEnemyEQSDebug.GetValueOnGameThread() < 2)
	{
		return;
	}
	const FEnvQueryDebugData& Debug = Query.DebugData;
	int32 Rejected = 0;
	for (int32 Index = 0; Index < Debug.DebugItemDetails.Num(); ++Index)
	{
		const FEnvQueryItemDetails& Detail = Debug.DebugItemDetails[Index];
		if (Detail.FailedTestIndex == INDEX_NONE)
		{
			continue;
		}
		++Rejected;
		// 큰 Grid 질의도 한 요청에서 상세 로그가 무제한으로 쌓이지 않게 한다.
		if (Rejected > 64)
		{
			continue;
		}
		const FString Test = Debug.PerformedTestNames.IsValidIndex(Detail.FailedTestIndex)
			? Debug.PerformedTestNames[Detail.FailedTestIndex] : TEXT("UnknownTest");
		UE_LOG(LogEnemyEQS, Log,
			TEXT("[EnemyEQSReject] Enemy=%s Query=%s Request=%d Item=%d TestIndex=%d Test=%s Reason=%s"),
			*GetNameSafe(Data.Enemy), *GetNameSafe(Data.QueryTemplate), Query.QueryID,
			Detail.ItemIndex, Detail.FailedTestIndex, *Test, *Detail.FailedDescription);
	}
	UE_LOG(LogEnemyEQS, Log,
		TEXT("[EnemyEQSFilterSummary] Enemy=%s Query=%s Request=%d Generated=%d Rejected=%d ResultItems=%d DetailLimit=64"),
		*GetNameSafe(Data.Enemy), *GetNameSafe(Data.QueryTemplate), Query.QueryID,
		Debug.DebugItems.Num(), Rejected, Query.Items.Num());
#endif
}

int32 ExecuteEnvQueryWithDiagnostics(FEnvQueryRequest& Request, EEnvQueryRunMode::Type RunMode,
	const FQueryFinishedSignature& Finished, UWorld& World)
{
#if USE_EQS_DEBUGGER
	if (CVarEnemyEQSDebug.GetValueOnGameThread() >= 2)
	{
		UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(&World);
		if (!Manager)
		{
			return INDEX_NONE;
		}
		TSharedPtr<FEnvQueryInstance> Query = Manager->PrepareQueryInstance(Request, RunMode);
		if (!Query)
		{
			return INDEX_NONE;
		}
		// 완료 시 살아남은 결과만 남으므로 상세 모드에서만 필터 전 후보 정보를 보존한다.
		Query->bStoreDebugInfo = true;
		return Manager->RunQuery(Query, Finished);
	}
#endif
	return Request.Execute(RunMode, Finished);
}

void LogEnvQuery(const TCHAR* Phase, const FEnemyRunEnvQueryTaskInstanceData& Data, int32 Items = 0,
	int32 CompletedRequest = INDEX_NONE)
{
	if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyEQS, Log,
			TEXT("[EnemyEQS] %s Enemy=%s Query=%s Request=%d Result=%s Items=%d HasDestination=%d Destination=%s SelectedTarget=%s SelectedTargetLocation=%s CheckedCandidates=%d SelectionPathStatus=%s SearchRetry=%d/%d NextSearchCandidate=%d SelectionPending=%d"),
			Phase, *GetNameSafe(Data.Enemy), *GetNameSafe(Data.QueryTemplate),
			CompletedRequest != INDEX_NONE ? CompletedRequest : Data.RequestId,
			*UEnum::GetValueAsString(Data.QueryResult), Items, Data.bHasDestination,
			*Data.Destination.ToCompactString(), *GetNameSafe(Data.SelectedTargetActor),
			*Data.SelectedTargetLocation.ToCompactString(), Data.CheckedPathCandidates,
			*UEnum::GetValueAsString(Data.SelectionPathStatus), Data.SearchSelectionRetries,
			Data.MaxSearchSelectionRetries, Data.NextSearchCandidate, Data.bSearchSelectionPending);
		// 결과 적용 시 RequestId가 해제되므로 완료 번호를 별도로 남겨 요청과 결과를 연결한다.
		UE_LOG(LogEnemyEQS, Log,
			TEXT("[EnemyEQSContext] %s Enemy=%s Request=%d Time=%.3f Room=%s Orbit=%d Reachability=%d Target=%s Start=%s Center=%s PreferredLeft=%d PreferredArc=%.1f CandidateLimit=%d CheckLimit=%d TimeLimitMs=%.3f"),
			Phase, *GetNameSafe(Data.Enemy), CompletedRequest != INDEX_NONE ? CompletedRequest : Data.RequestId,
			IsValid(Data.Enemy) && Data.Enemy->GetWorld() ? Data.Enemy->GetWorld()->GetTimeSeconds() : 0.0,
			*Data.CapturedRoomTag.ToString(), Data.bValidateOrbitPath, Data.bValidateReachability,
			*GetNameSafe(Data.SelectedTargetActor), *Data.CapturedOrbitStart.ToCompactString(),
			*Data.CapturedOrbitCenter.ToCompactString(), Data.bCapturedOrbitLeft, Data.CapturedOrbitLength,
			Data.MaxPathCandidates, Data.MaxCollisionChecks, Data.MaxSearchMilliseconds);
		if (Data.bValidateOrbitPath)
		{
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyOrbitResult] %s Enemy=%s NoOrbitDestination=%d Left=%d ArcLength=%.1f Points=%d"),
				Phase, *GetNameSafe(Data.Enemy), Data.bNoOrbitDestination, Data.bSelectedOrbitLeft,
				Data.SelectedArcLength, Data.SelectedWaypoints.Num());
		}
		if (Data.bValidateRetreatPath)
		{
			UE_LOG(LogEnemyEQS, Log,
				TEXT("[EnemyRetreatResult] %s Enemy=%s Result=%s NoRetreatDestination=%d Start=%s Origin=%s Direction=%s Goal=%s GoalDistance2D=%.1f Checked=%d Path=%s"),
				Phase, *GetNameSafe(Data.Enemy), *UEnum::GetValueAsString(Data.QueryResult), Data.bNoRetreatDestination,
				*Data.CapturedRetreatStart.ToCompactString(), *Data.RetreatOrigin.ToCompactString(),
				*Data.RetreatDirection.ToCompactString(), *Data.Destination.ToCompactString(),
				FVector::Dist2D(Data.Destination, Data.RetreatOrigin), Data.CheckedPathCandidates,
				*UEnum::GetValueAsString(Data.SelectionPathStatus));
		}
		if (Data.bValidateChasePath)
		{
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyChaseResult] %s Enemy=%s Lane=%d Start=%s Target=%s Goal=%s Checked=%d Result=%s Path=%s"),
				Phase, *GetNameSafe(Data.Enemy), Data.CapturedChaseLane, *Data.CapturedChaseStart.ToCompactString(),
				*Data.CapturedOrbitCenter.ToCompactString(), *Data.Destination.ToCompactString(), Data.CheckedPathCandidates,
				*UEnum::GetValueAsString(Data.QueryResult), *UEnum::GetValueAsString(Data.SelectionPathStatus));
		}
		if (Data.SearchMode != EEnemySearchQueryMode::None)
		{
			UE_LOG(LogEnemyEQS, Log,
				TEXT("[EnemySearchQuery] %s Enemy=%s Episode=%llu Revision=%llu Mode=%s Center=%s Points=%d Limited=%d"),
				Phase, *GetNameSafe(Data.Enemy), Data.CapturedSearchSerial, Data.CapturedSharedRevision,
				*UEnum::GetValueAsString(Data.SearchMode), *Data.CapturedOrbitCenter.ToCompactString(),
				Data.SelectedSearchPoints.Num(), Data.bSearchSelectionLimited);
		}
	}
}

bool IsReachabilityConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	const int32 Modes = int32(Data.bValidateReachability) + int32(Data.bValidateOrbitPath)
		+ int32(Data.bValidateRetreatPath) + int32(Data.bValidateChasePath)
		+ int32(Data.SearchMode != EEnemySearchQueryMode::None);
	if (Modes == 0)
	{
		return true;
	}
	if (Modes != 1)
	{
		return false;
	}
	const bool bValidLimits = Data.MaxPathCandidates >= 1 && Data.MaxPathCandidates <= 32
		&& Data.MaxSearchSelectionRetries >= 0 && Data.MaxSearchSelectionRetries <= 8
		&& Data.MaxSearchNodes >= 1 && Data.MaxSearchNodes <= 65536
		&& Data.MaxCollisionChecks >= 1 && Data.MaxCollisionChecks <= 262144;
	const bool bValidGeometry = FMath::IsFinite(Data.PathCellSize) && Data.PathCellSize >= 1.0f
		&& FMath::IsFinite(Data.SearchPadding) && Data.SearchPadding >= 1.0f;
	const bool bValidTime = FMath::IsFinite(Data.MaxSearchMilliseconds) && Data.MaxSearchMilliseconds > 0.0f;
	return Data.RunMode == EEnvQueryRunMode::AllMatching && bValidLimits && bValidGeometry && bValidTime;
}

bool IsOrbitQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!Data.bValidateOrbitPath)
	{
		return true;
	}
	if (!Data.bRequireVisibleTarget || !Data.QueryTemplate)
	{
		return false;
	}
	// 다른 Generator/복수 옵션을 선회로 해석하면 빈 결과를 잘못 정밀 사격으로 판단할 수 있다.
	const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
	return Options.Num() == 1 && Options[0]
		&& Options[0]->Generator && Options[0]->Generator->IsA<UEnemyEnvQueryGenerator_Orbit>();
}

bool IsRetreatQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!Data.bValidateRetreatPath)
	{
		return true;
	}
	if (!Data.bRequireVisibleTarget || !Data.QueryTemplate)
	{
		return false;
	}
	const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
	return Options.Num() == 1 && Options[0] && Options[0]->Generator
		&& Options[0]->Generator->GetClass() == UEnemyEnvQueryGenerator_Retreat::StaticClass();
}

bool IsChaseQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!Data.bValidateChasePath)
	{
		return true;
	}
	if (!Data.bRequireVisibleTarget || !Data.QueryTemplate)
	{
		return false;
	}
	const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
	return Options.Num() == 1 && Options[0] && Options[0]->Generator
		&& Options[0]->Generator->GetClass() == UEnemyEnvQueryGenerator_Chase::StaticClass();
}

bool IsEnvQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (Data.SearchMode != EEnemySearchQueryMode::None)
	{
		if (Data.bRequireVisibleTarget || !Data.QueryTemplate
			|| !FMath::IsFinite(Data.SearchPointSeparation) || Data.SearchPointSeparation <= 0.0f)
		{
			return false;
		}
		const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
		const UEnemyEnvQueryGenerator_Search* Generator = Options.Num() == 1 && Options[0]
			? Cast<UEnemyEnvQueryGenerator_Search>(Options[0]->Generator) : nullptr;
		if (!Generator || Generator->Mode != Data.SearchMode)
		{
			return false;
		}
	}
	if (!FMath::IsFinite(Data.TargetChangeDistance) || Data.TargetChangeDistance < 0.0f)
	{
		return false;
	}
	if (!IsReachabilityConfigurationValid(Data))
	{
		return false;
	}
	if (!IsOrbitQueryConfigurationValid(Data))
	{
		return false;
	}
	if (!IsRetreatQueryConfigurationValid(Data))
	{
		return false;
	}
	if (!IsChaseQueryConfigurationValid(Data))
	{
		return false;
	}
	if (!FMath::IsFinite(Data.Clearance) || Data.Clearance < 0.0f)
	{
		return false;
	}
	if (!FMath::IsFinite(Data.QueryInterval) || Data.QueryInterval <= 0.0f
		|| !FMath::IsFinite(Data.QueryTimeout) || Data.QueryTimeout <= 0.0f)
	{
		return false;
	}
	return Data.QueryTemplate && Data.RunMode <= EEnvQueryRunMode::AllMatching;
}

bool HasValidEnvQueryParameters(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// Blackboard 없는 Task이므로 복사된 에셋의 키가 엔진 check까지 전달되지 않게 한다.
	for (const FAIDynamicParam& Param : Data.QueryConfig)
	{
		if (Param.BBKey.IsSet() || !FMath::IsFinite(Param.Value))
		{
			return false;
		}
	}
	return true;
}

bool IsEnvQueryEnemyCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	const AEnemyBase* Enemy = Data.Enemy;
	if (!Data.bActive || !IsValid(Enemy) || !Enemy->HasAuthority() || !Enemy->GetWorld())
	{
		return false;
	}
	if (Enemy->IsDead() || !Enemy->CanUseEnemyPerception()
		|| Enemy->GetCombatState() == EEnemyCombatState::Stun || !IsValid(Enemy->GetCapsuleComponent()))
	{
		return false;
	}
	// 같은 Pawn이라도 풀에서 다시 대여되었다면 이전 요청의 결과를 사용할 수 없다.
	return Enemy->GetCombatState() == Data.CapturedCombatState
		&& Enemy->GetPoolGameplayGeneration() == Data.CapturedGeneration
		&& Enemy->GetPoolLeaseSerial() == Data.CapturedLease;
}

bool IsEnvQueryControllerCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	const AEnemyAIController* Controller = Data.CachedController.Get();
	return Controller && Data.Enemy->GetController() == Controller && Controller->GetPawn() == Data.Enemy;
}

bool IsEnvQueryRoomCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	return Data.Enemy->GetDefaultRoomTag() == Data.CapturedRoomTag
		&& Data.CapturedRoom.IsValid() && Data.CachedRooms
		&& Data.CachedRooms->IsEnemyMovementRoomContextCurrent(Data.Enemy, Data.MovementRooms);
}

bool IsEnvQueryTargetCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (Data.SearchMode != EEnemySearchQueryMode::None)
	{
		const FEnemySearchEpisode& Episode = Data.Enemy->GetSearchEpisode();
		return Episode.bActive && !Episode.bLocalGuard && Episode.Serial == Data.CapturedSearchSerial
			&& Data.Enemy->RefreshSearchEpisodeLimit()
			&& Data.Enemy->CanContinueSearchEpisode()
			&& (Data.Enemy->IsLocationSearchActive() || !Data.Enemy->HasMeaningfulSearchHint());
	}
	if (!Data.bRequireVisibleTarget)
	{
		return true;
	}
	if (Data.Enemy->IsLocationInvestigationActive() || !Data.Enemy->HasTrackableTarget() || !Data.CapturedTarget.IsValid()
		|| Data.Enemy->GetTrackedTarget() != Data.CapturedTarget.Get())
	{
		return false;
	}
	const float ChangeDistance = (Data.CapturedCombatSerial || Data.bValidateOrbitPath || Data.bValidateRetreatPath || Data.bValidateChasePath) && Data.TargetChangeDistance <= 0.0f
		? 200.0f : Data.TargetChangeDistance;
	const double MovedDistanceSquared = Data.CapturedCombatSerial
		? FVector::DistSquared(Data.SelectedTargetLocation, Data.Enemy->GetCombatNavigationTargetLocation(Data.CapturedTarget.Get()))
		: FVector::DistSquared2D(Data.SelectedTargetLocation, Data.CapturedTarget->GetActorLocation());
	return ChangeDistance <= 0.0f
		|| MovedDistanceSquared < FMath::Square(ChangeDistance);
}

bool IsCombatYieldSnapshotCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (Data.CapturedCombatYieldSerial == 0)
	{
		return true;
	}
	FVector Start;
	FVector End;
	uint64 Serial = 0;
	return IsValid(Data.Enemy) && Data.Enemy->GetCombatYieldContext(Start, End, Serial)
		&& Serial == Data.CapturedCombatYieldSerial
		&& Start.Equals(Data.CapturedCombatYieldStart, 1.0)
		&& End.Equals(Data.CapturedCombatYieldEnd, 1.0);
}

bool IsEnvQuerySnapshotCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 적 수명 -> 소유 컨트롤러 -> 초기 소속 방 -> 직접 시야 또는 유예 추적 타깃 순서로 확인한다.
	// 앞 단계가 참조 유효성을 보장하므로 뒤 단계는 동일 참조를 다시 검색하지 않는다.
	return IsEnvQueryEnemyCurrent(Data)
		&& IsCombatYieldSnapshotCurrent(Data)
		&& (Data.CombatReservationRequest == 0 || Data.bCombatObservationWaiting
			|| Data.Enemy->IsCombatReservationRequestCurrent(Data.CombatReservationRequest))
		&& (!Data.CapturedCombatSerial || ((Data.bCombatObservationWaiting
			? Data.Enemy->GetCombatTacticalEpisodeSerial() == Data.CapturedCombatSerial
			: Data.Enemy->IsCombatTacticalEpisodeCurrent(Data.CapturedCombatSerial))
			&& (!Data.bCapturedForSightReposition || Data.bCombatObservationWaiting
				|| Data.Enemy->NeedsCombatTacticalReposition())))
		&& IsEnvQueryControllerCurrent(Data)
		&& IsEnvQueryRoomCurrent(Data)
		&& IsEnvQueryTargetCurrent(Data)
		&& (!(Data.bRequireVisibleTarget && Data.SearchMode == EEnemySearchQueryMode::None)
			|| EnemyFlightHeight::IsSnapshotCurrent(*Data.Enemy, Data.HeightSnapshot))
		&& (Data.bUnifiedCombatNavigation || !Data.bValidateOrbitPath
			|| Data.Enemy->GetActorLocation().Equals(Data.CapturedOrbitStart, 25.0))
		&& (Data.bUnifiedCombatNavigation || !Data.bValidateRetreatPath
			|| Data.Enemy->GetActorLocation().Equals(Data.CapturedRetreatStart, 25.0));
}

void LogEnvQuerySnapshotFailure(const FEnemyRunEnvQueryTaskInstanceData& Data, int32 CompletedRequest = INDEX_NONE)
{
	if (CVarEnemyEQSDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
	const TCHAR* Reason = Data.bValidateRetreatPath ? TEXT("RetreatStartMoved") : TEXT("OrbitStartMoved");
	if (!IsEnvQueryEnemyCurrent(Data))
	{
		Reason = TEXT("EnemyLifetimeOrCombatStateChanged");
	}
	else if (!IsEnvQueryControllerCurrent(Data))
	{
		Reason = TEXT("ControllerChanged");
	}
	else if (!IsEnvQueryRoomCurrent(Data))
	{
		Reason = TEXT("RoomChangedOrUnavailable");
	}
	else if (!IsEnvQueryTargetCurrent(Data))
	{
		Reason = TEXT("TrackingLostTargetChangedOrMoved");
	}
	else if (Data.bRequireVisibleTarget && Data.SearchMode == EEnemySearchQueryMode::None
		&& !EnemyFlightHeight::IsSnapshotCurrent(*Data.Enemy, Data.HeightSnapshot))
	{
		Reason = TEXT("FlightHeightSettingsChanged");
	}
	UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyEQSSnapshot] Enemy=%s Request=%d Reason=%s Start=%s Current=%s Target=%s CurrentTargetLocation=%s"),
		*GetNameSafe(Data.Enemy), CompletedRequest != INDEX_NONE ? CompletedRequest : Data.RequestId,
		Reason, *Data.CapturedOrbitStart.ToCompactString(),
		IsValid(Data.Enemy) ? *Data.Enemy->GetActorLocation().ToCompactString() : TEXT("None"),
		*GetNameSafe(Data.CapturedTarget.Get()),
		Data.CapturedTarget.IsValid() ? *Data.CapturedTarget->GetActorLocation().ToCompactString() : TEXT("None"));
}

FVector ToQueryParamLocation(const FVector& Location)
{
	// EQS NamedParam은 float다. Generator와 검증이 같은 정밀도의 좌표를 사용한다.
	return FVector(static_cast<float>(Location.X), static_cast<float>(Location.Y), static_cast<float>(Location.Z));
}

void CaptureEnvQuerySnapshot(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	AEnemyBase* Enemy = Data.Enemy;
	Data.CachedController = Cast<AEnemyAIController>(Enemy->GetController());
	Data.CachedRooms = Enemy->GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	Data.CachedReservationRooms = Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	Data.CapturedRoomTag = Enemy->GetDefaultRoomTag();
	Data.CapturedRoom = Data.CachedRooms ? Data.CachedRooms->GetRegisteredRoomVolume(Data.CapturedRoomTag) : nullptr;
	Data.MovementRooms = FEnemyMovementRoomContext();
	if (Data.CachedRooms)
	{
		Data.CachedRooms->BuildEnemyMovementRoomContext(Enemy, Data.MovementRooms);
	}
	Data.CapturedTarget = Data.CachedController.IsValid() && Data.bRequireVisibleTarget
		? Data.Enemy->GetTrackedTarget() : nullptr;
	Data.SelectedTargetLocation = Data.CapturedTarget.IsValid()
		? Enemy->GetCombatNavigationTargetLocation(Data.CapturedTarget.Get()) : FVector::ZeroVector;
	Data.SelectedTargetActor = Data.CapturedTarget.Get();
	if (Data.SearchMode == EEnemySearchQueryMode::None && !Data.bValidateChasePath
		&& (Data.bValidateReachability || Data.bValidateOrbitPath || Data.bValidateRetreatPath)
		&& Enemy->UsesTargetAttackPreparation() && Data.CapturedTarget.IsValid())
	{
		Data.CapturedCombatSerial = Enemy->EnsureCombatTacticalEpisode(Data.CapturedTarget.Get(),
			Data.SelectedTargetLocation);
		Data.bCapturedForSightReposition = Enemy->NeedsCombatTacticalReposition();
		Enemy->GetCombatYieldContext(Data.CapturedCombatYieldStart, Data.CapturedCombatYieldEnd,
			Data.CapturedCombatYieldSerial);
		Data.bNavigationTargetVisible = Data.CachedController.IsValid()
			&& Data.CachedController->IsTargetDirectlyVisible(Data.CapturedTarget.Get());
		Data.bUnifiedCombatNavigation = Enemy->IsA<AVECDrone>() && Enemy->GetRuntimeStat().Type == EEnemyType::Gun
			&& !Cast<ALaserWeapon>(Enemy->GetCurrentWeapon())
			&& (Data.CapturedCombatYieldSerial != 0 || Data.bCapturedForSightReposition
				|| !Data.bNavigationTargetVisible || Enemy->IsCombatNavigationRetargetPending());
		Data.bCombatObservationWaiting = Enemy->IsCombatObservationWaiting();
		if (Data.CapturedCombatSerial != 0 && !Data.bCombatObservationWaiting)
		{
			Data.CombatReservationRequest = Enemy->BeginCombatReservationRequest();
		}
	}
	Data.CapturedGeneration = Enemy->GetPoolGameplayGeneration();
	Data.CapturedLease = Enemy->GetPoolLeaseSerial();
	Data.CapturedCombatState = Enemy->GetCombatState();
	Data.CapturedOrbitStart = ToQueryParamLocation(Enemy->GetActorLocation());
	Data.CapturedOrbitCenter = ToQueryParamLocation(Data.SelectedTargetLocation);
	Data.CapturedOrbitLength = Data.bValidateOrbitPath ? FMath::FRandRange(300.0f, 500.0f) : 0.0f;
	Data.bCapturedOrbitLeft = Enemy->PrefersCombatOrbitLeft();
	Data.CapturedRetreatStart = ToQueryParamLocation(Enemy->GetActorLocation());
	Data.CapturedChaseStart = ToQueryParamLocation(Enemy->GetActorLocation());
	Data.CapturedChaseLane = Enemy->GetCombatChaseLane();
	Data.HeightSnapshot = FEnemyFlightHeightSnapshot();
	Data.HeightSnapshot.bEnabled = Enemy->FlightHeightSettings.bEnabled;
	Data.HeightSnapshot.Settings = Enemy->FlightHeightSettings;
	Data.HeightSnapshot.Preference = Enemy->GetCombatHeightPreference();
	Data.RetreatOrigin = ToQueryParamLocation(Data.SelectedTargetLocation);
	Data.RetreatDirection = (Data.CapturedRetreatStart - Data.RetreatOrigin).GetSafeNormal2D();
	if (Data.bValidateRetreatPath && Data.RetreatDirection.IsNearlyZero())
		Data.RetreatDirection = FVector::ForwardVector;
	// Emergency distance gain uses the bounded long/short retreat set, not an approach-like mixed candidate.
	if (Data.bValidateRetreatPath && FVector::Dist2D(Data.CapturedRetreatStart, Data.RetreatOrigin) <= 500.0
		&& Data.bNavigationTargetVisible && Data.CapturedCombatYieldSerial == 0)
		Data.bUnifiedCombatNavigation = false;
	if (Data.SearchMode != EEnemySearchQueryMode::None)
	{
		const FEnemySearchEpisode& Episode = Enemy->GetSearchEpisode();
		Data.CapturedSearchSerial = Episode.Serial;
		// 중심과 정보 번호는 반드시 같은 회차에서 가져온다. 새 힌트가 왔다면 이 질의는 폐기한다.
		Data.CapturedSharedRevision = Episode.SharedRevision;
		Data.CapturedOrbitCenter = ToQueryParamLocation(Episode.Center);
		Data.CapturedSearchHeight = Enemy->GetActorLocation().Z;
		Data.SelectedTargetLocation = Episode.Center;
	}
}

bool HasEnvQueryTimedOut(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	return Data.Enemy->GetWorld()->GetTimeSeconds() - Data.QueryStartedAt >= Data.QueryTimeout;
}

bool IsEmptyEnvQueryResult(const FEnvQueryResult& Result)
{
	return Result.Items.IsEmpty()
		&& (Result.IsSuccessful() || Result.GetRawStatus() == EEnvQueryStatus::Failed);
}

bool IsLocationEnvQueryResult(const FEnvQueryResult& Result)
{
	if (!Result.IsSuccessful() || Result.Items.IsEmpty() || !Result.ItemType)
	{
		return false;
	}
	return Result.ItemType->IsChildOf(UEnvQueryItemType_VectorBase::StaticClass())
		&& !Result.ItemType->IsChildOf(UEnvQueryItemType_ActorBase::StaticClass());
}

bool IsEnvQueryDestinationClear(const FEnemyRunEnvQueryTaskInstanceData& Data, const FVector& Destination)
{
	UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
	FFlightPathRequest Check;
	Check.bRequireRoom = true;
	Check.Room = Data.CapturedRoom;
	Check.AllowedRooms = Data.MovementRooms.Rooms;
	Check.Clearance = Data.Clearance;
	Check.Start = Capsule->GetComponentLocation();
	Check.Goal = Destination + Check.Start - Data.Enemy->GetActorLocation();
	Check.SearchBounds = FBox(Check.Start, Check.Start);
	Check.SearchBounds += Check.Goal;
	// 질의 중 장애물이 바뀌거나 에셋에 안전 Test가 빠져도 최종 목적지는 다시 검사한다.
	return FlightPathfinding::CheckDestination(*Data.Enemy->GetWorld(), *Capsule, Check)
		== EFlightPathStatus::Success;
}

EEnemyEnvQueryResult ResolveCandidateFailure(bool bBudgetExceeded, int32 Checked, int32 Available)
{
	if (bBudgetExceeded)
	{
		return EEnemyEnvQueryResult::PathBudgetExceeded;
	}
	return Checked < Available ? EEnemyEnvQueryResult::CandidateLimitReached : EEnemyEnvQueryResult::NoReachableDestination;
}

bool HasCandidatePathBudget(int32 RemainingNodes, int32 RemainingChecks, double RemainingTime)
{
	return RemainingNodes > 0 && RemainingChecks > 0 && RemainingTime > 0.0;
}

double GetCandidatePathMilliseconds(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 높이 보정도 같은 선택 시도의 비용이다. A*를 시작한다고 검사/시간 예산을 새로 주지 않는다.
	return Data.MaxSearchMilliseconds - Data.HeightSnapshot.Milliseconds;
}

FFlightPathRequest MakeCandidatePathRequest(const FEnemyRunEnvQueryTaskInstanceData& Data,
	const UCapsuleComponent& Capsule, const FVector& Candidate, int32 Nodes, int32 Checks, double Milliseconds)
{
	FFlightPathRequest Request;
	Request.Start = Capsule.GetComponentLocation();
	Request.Goal = Candidate + Request.Start - Data.Enemy->GetActorLocation();
	Request.SearchBounds = FBox(Request.Start, Request.Start);
	Request.SearchBounds += Request.Goal;
	Request.SearchBounds = Request.SearchBounds.ExpandBy(Data.SearchPadding);
	Request.CellSize = Data.PathCellSize;
	Request.Clearance = Data.Clearance;
	Request.bRequireRoom = true;
	Request.Room = Data.CapturedRoom;
	Request.AllowedRooms = Data.MovementRooms.Rooms;
	Request.MaxSearchNodes = Nodes;
	Request.MaxCollisionChecks = Checks;
	Request.MaxSearchMilliseconds = Milliseconds;
	Request.bRequireAwayHalfSpace = Data.bValidateRetreatPath && !Data.bUnifiedCombatNavigation;
	Request.AwayOrigin = Data.RetreatOrigin + Request.Start - Data.Enemy->GetActorLocation();
	Request.AwayDirection = Data.RetreatDirection;
	return Request;
}

void GetReachableCandidateOrder(const FEnemyRunEnvQueryTaskInstanceData& Data,
	const FEnvQueryResult& Result, TArray<FVector>& Candidates)
{
	Candidates.Reset();
	if (!Data.bValidateChasePath)
	{
		for (int32 Index = 0; Index < Result.Items.Num(); ++Index)
		{
			Candidates.Add(Result.GetItemAsLocation(Index));
		}
		return;
	}
	// EQS의 동점 정렬에 개체 성향을 맡기지 않는다. 생성 순서에서 필터를 통과한 후보만 검사한다.
	TArray<FVector> Generated;
	if (Data.HeightSnapshot.bEnabled)
	{
		for (const FEnemyFlightHeightCandidate& Candidate : Data.HeightSnapshot.Candidates)
		{
			if (Candidate.Status == EFlightPathStatus::Success)
			{
				Generated.Add(Candidate.Location);
			}
		}
	}
	else
	{
		EnemyChase::BuildCandidates(Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane, Generated);
	}
	for (const FVector& Candidate : Generated)
	{
		for (int32 Index = 0; Index < Result.Items.Num(); ++Index)
		{
			if (Candidate.Equals(Result.GetItemAsLocation(Index), 1.0))
			{
				Candidates.Add(Candidate);
				break;
			}
		}
	}
}

bool SelectReachableDestination(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
	const double Started = FPlatformTime::Seconds();
	int32 RemainingNodes = Data.MaxSearchNodes;
	int32 RemainingChecks = Data.MaxCollisionChecks - Data.HeightSnapshot.CollisionChecks;
	bool bBudgetExceeded = false;
	// EQS는 점수순으로 정렬한 후보를 제공한다. 각 후보가 아니라 전체 선택 시도가 예산을 공유한다.
	TArray<FVector> Candidates;
	GetReachableCandidateOrder(Data, Result, Candidates);
	const int32 Limit = FMath::Min(Data.MaxPathCandidates, Candidates.Num());
	for (int32 Index = 0; Index < Limit; ++Index)
	{
		const double RemainingTime = GetCandidatePathMilliseconds(Data) - (FPlatformTime::Seconds() - Started) * 1000.0;
		if (!HasCandidatePathBudget(RemainingNodes, RemainingChecks, RemainingTime))
		{
			bBudgetExceeded = true;
			Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
			break;
		}
		if (!IsEnvQuerySnapshotCurrent(Data))
		{
			Data.QueryResult = EEnemyEnvQueryResult::Stale;
			return false;
		}
		if (HasEnvQueryTimedOut(Data))
		{
			Data.QueryResult = EEnemyEnvQueryResult::TimedOut;
			return false;
		}
		const FVector Candidate = Candidates[Index];
		if (!Data.bValidateChasePath && Cast<AShooterCharacter>(Data.CapturedTarget.Get())
			&& !Data.Enemy->HasCombatAttackPathFrom(Data.CapturedTarget.Get(), Candidate, RemainingChecks))
		{
			++Data.CheckedPathCandidates;
			Data.SelectionPathStatus = EFlightPathStatus::BlockedSegment;
			if (RemainingChecks < 2)
			{
				bBudgetExceeded = true;
				Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
				break;
			}
			continue;
		}
		const FFlightPathRequest Request = MakeCandidatePathRequest(Data, *Capsule, Candidate,
			RemainingNodes, RemainingChecks,
			GetCandidatePathMilliseconds(Data) - (FPlatformTime::Seconds() - Started) * 1000.0);
		const FFlightPathResult Path = FlightPathfinding::FindPath(*Data.Enemy->GetWorld(), *Capsule, Request);
		++Data.CheckedPathCandidates;
		Data.SelectionPathStatus = Path.Status;
		RemainingNodes -= Path.SearchNodes;
		RemainingChecks -= Path.CollisionChecks;
		bBudgetExceeded |= Path.Status == EFlightPathStatus::BudgetExceeded;
		if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyEQSPath] Enemy=%s Candidate=%d Status=%s Nodes=%d Checks=%d Milliseconds=%.3f"),
				*GetNameSafe(Data.Enemy), Index, *UEnum::GetValueAsString(Path.Status), Path.SearchNodes, Path.CollisionChecks, Path.ElapsedMilliseconds);
		}
		if (Path.Status == EFlightPathStatus::Success)
		{
			Data.Destination = Candidate;
			return true;
		}
		if (Path.Status == EFlightPathStatus::InvalidRequest || Path.Status == EFlightPathStatus::Unavailable
			|| Path.Status == EFlightPathStatus::Cancelled)
		{
			Data.QueryResult = EEnemyEnvQueryResult::Failed;
			return false;
		}
	}
	Data.QueryResult = ResolveCandidateFailure(bBudgetExceeded, Data.CheckedPathCandidates, Candidates.Num());
	if (bBudgetExceeded)
	{
		Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
	}
	return false;
}

bool HasCandidateResult(const FEnvQueryResult& Result, const FVector& Destination)
{
	for (int32 Index = 0; Index < Result.Items.Num(); ++Index)
	{
		if (Result.GetItemAsLocation(Index).Equals(Destination, 1.0))
		{
			return true;
		}
	}
	return false;
}

EFlightPathStatus CheckOrbitRoute(FEnemyRunEnvQueryTaskInstanceData& Data,
	const TArray<FVector>& Waypoints, int32& RemainingChecks, double Started, int32 QueryId)
{
	if (Waypoints.Num() < 2)
	{
		return EFlightPathStatus::BlockedSegment;
	}
	UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
	const FVector Offset = Capsule->GetComponentLocation() - Data.Enemy->GetActorLocation();
	FCollisionQueryParams SightParams(SCENE_QUERY_STAT(EnemyOrbitSight), false, Data.Enemy);
	SightParams.AddIgnoredActor(Data.SelectedTargetActor);
	for (int32 Index = 1; Index < Waypoints.Num(); ++Index)
	{
		// 출발점 여유 공간 탈출 예외까지 캡슐 최대 4회 + 시야 1회로 보수적으로 예산을 잡는다.
		if (RemainingChecks < 5 || (FPlatformTime::Seconds() - Started) * 1000.0 >= GetCandidatePathMilliseconds(Data))
		{
			if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
			{
				UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyOrbitSegment] Enemy=%s Request=%d Segment=%d Reason=BudgetExceeded RemainingChecks=%d ElapsedMs=%.3f"),
					*GetNameSafe(Data.Enemy), QueryId, Index, RemainingChecks, (FPlatformTime::Seconds() - Started) * 1000.0);
			}
			return EFlightPathStatus::BudgetExceeded;
		}
		RemainingChecks -= 4;
		FFlightPathRequest Request = MakeCandidatePathRequest(Data, *Capsule, Waypoints[Index],
			Data.MaxSearchNodes, 5, GetCandidatePathMilliseconds(Data));
		Request.Start = Waypoints[Index - 1] + Offset;
		const EFlightPathStatus Status = FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Capsule, Request);
		// 경로가 막혔으면 시야 Trace는 생략한다. 상세 로그도 이번 검사 결과만 사용한다.
		bool bSightBlocked = false;
		if (Status == EFlightPathStatus::Success)
		{
			if (Cast<AShooterCharacter>(Data.CapturedTarget.Get()))
			{
				bSightBlocked = !Data.Enemy->HasCombatAttackPathFrom(Data.CapturedTarget.Get(), Waypoints[Index], RemainingChecks);
				if (RemainingChecks < 2 && bSightBlocked)
				{
					return EFlightPathStatus::BudgetExceeded;
				}
			}
			else
			{
				--RemainingChecks;
				bSightBlocked = Data.Enemy->GetWorld()->LineTraceTestByChannel(Waypoints[Index], Data.SelectedTargetLocation,
					ECC_Visibility, SightParams);
			}
		}
		if (CVarEnemyEQSDebug.GetValueOnGameThread() >= 2)
		{
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyOrbitSegment] Enemy=%s Request=%d Candidate=%d Segment=%d Start=%s End=%s PathStatus=%s SightBlocked=%d ElapsedMs=%.3f"),
				*GetNameSafe(Data.Enemy), QueryId, Data.CheckedPathCandidates, Index,
				*Waypoints[Index - 1].ToCompactString(), *Waypoints[Index].ToCompactString(),
				*UEnum::GetValueAsString(Status), bSightBlocked, (FPlatformTime::Seconds() - Started) * 1000.0);
		}
		if (Status != EFlightPathStatus::Success)
		{
			return Status;
		}
		if (bSightBlocked)
		{
			return EFlightPathStatus::BlockedSegment;
		}
	}
	return EFlightPathStatus::Success;
}

bool SelectOrbitDestination(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	TArray<FEnemyOrbitCandidate> Candidates;
	EnemyOrbit::BuildCandidates(Data.CapturedOrbitStart, Data.CapturedOrbitCenter,
		Data.CapturedOrbitLength, Data.bCapturedOrbitLeft, Candidates);
	if (Data.HeightSnapshot.bEnabled)
	{
		if (Candidates.Num() != Data.HeightSnapshot.Candidates.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Candidates[Index].Destination = Data.HeightSnapshot.Candidates[Index].Location;
		}
	}
	if (Candidates.IsEmpty())
	{
		LogEnvQuery(TEXT("OrbitRejected: no canonical candidates"), Data);
		return false;
	}
	// 결과 점수순이 아니라 Generator와 동일한 방향/거리 순서로 살아남은 후보를 확인한다.
	int32 Available = 0;
	for (const FEnemyOrbitCandidate& Candidate : Candidates)
	{
		if (HasCandidateResult(Result, Candidate.Destination))
		{
			++Available;
		}
	}
	if (Available != Result.Items.Num())
	{
		LogEnvQuery(TEXT("OrbitRejected: query items do not match orbit generator"), Data, Result.Items.Num());
		return false;
	}
	int32 RemainingChecks = Data.MaxCollisionChecks - Data.HeightSnapshot.CollisionChecks - 6;
	const double Started = FPlatformTime::Seconds();
	bool bHeightUnavailable = false;
	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const FEnemyOrbitCandidate& Candidate = Candidates[CandidateIndex];
		if (Data.HeightSnapshot.bEnabled)
		{
			const EFlightPathStatus HeightStatus = Data.HeightSnapshot.Candidates[CandidateIndex].Status;
			if (HeightStatus != EFlightPathStatus::Success)
			{
				bHeightUnavailable |= HeightStatus != EFlightPathStatus::BlockedEndpoint;
				continue;
			}
		}
		const bool bPassedQuery = HasCandidateResult(Result, Candidate.Destination);
		if (Data.CheckedPathCandidates >= Data.MaxPathCandidates)
		{
			Data.QueryResult = EEnemyEnvQueryResult::CandidateLimitReached;
			LogEnvQuery(TEXT("OrbitRejected: candidate limit; not proof of both sides blocked"), Data);
			return false;
		}
		TArray<FVector> Waypoints;
		const double SpatialLength = FMath::Sqrt(FMath::Square(double(Candidate.ArcLength))
			+ FMath::Square(Candidate.Destination.Z - Data.CapturedOrbitStart.Z));
		// 고도 변화까지 포함해 한 구간 5m를 지킨다. 긴 수직 이동을 선회로 숨기지 않는다.
		if (SpatialLength > 500.01)
		{
			// 거리 제한으로 제외한 후보는 물리적으로 막혔다는 증거가 아니다.
			bHeightUnavailable = true;
			continue;
		}
		EnemyOrbit::BuildWaypoints(Data.CapturedOrbitStart, Data.CapturedOrbitCenter, Candidate, Waypoints);
		++Data.CheckedPathCandidates;
		Data.SelectionPathStatus = CheckOrbitRoute(Data, Waypoints, RemainingChecks, Started, Result.QueryID);
		if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyOrbit] Enemy=%s Request=%d Candidate=%d Left=%d ArcLength=%.1f PassedQuery=%d Points=%d Status=%s RemainingChecks=%d"),
				*GetNameSafe(Data.Enemy), Result.QueryID, Data.CheckedPathCandidates, Candidate.bLeft, Candidate.ArcLength, bPassedQuery, Waypoints.Num(),
				*UEnum::GetValueAsString(Data.SelectionPathStatus), RemainingChecks);
		}
		if ((FPlatformTime::Seconds() - Started) * 1000.0 >= GetCandidatePathMilliseconds(Data))
		{
			Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
		}
		if (Data.SelectionPathStatus == EFlightPathStatus::Success)
		{
			// EQS가 제거한 후보도 실제 경로가 유효하면 양쪽 불가라고 확정할 수 없다.
			// 잘못된 Trace/Context 설정은 정밀 사격 대신 Failed로 드러낸다.
			if (!bPassedQuery)
			{
				LogEnvQuery(TEXT("OrbitRejected: clear route was filtered by EQS; check tests/context"), Data);
				return false;
			}
			Data.Destination = Candidate.Destination;
			Data.SelectedWaypoints = MoveTemp(Waypoints);
			Data.SelectedArcLength = SpatialLength;
			Data.bSelectedOrbitLeft = Candidate.bLeft;
			return true;
		}
		if (Data.SelectionPathStatus == EFlightPathStatus::BudgetExceeded)
		{
			Data.QueryResult = EEnemyEnvQueryResult::PathBudgetExceeded;
			return false;
		}
		if (Data.SelectionPathStatus != EFlightPathStatus::BlockedEndpoint
			&& Data.SelectionPathStatus != EFlightPathStatus::BlockedSegment)
		{
			return false;
		}
	}
	if (bHeightUnavailable)
	{
		return false;
	}
	Data.QueryResult = EEnemyEnvQueryResult::NoReachableDestination;
	Data.bNoOrbitDestination = true;
	return false;
}

EFlightPathStatus CheckSelectionStart(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 높이 보정과 출발점 검사는 같은 예산을 사용한다. 예산 부족을 물리적 막힘으로 확정하지 않는다.
	const int32 Checks = Data.MaxCollisionChecks - Data.HeightSnapshot.CollisionChecks;
	const double Time = GetCandidatePathMilliseconds(Data);
	if (Checks < 6 || !HasCandidatePathBudget(Data.MaxSearchNodes, Checks, Time))
	{
		Data.QueryResult = EEnemyEnvQueryResult::PathBudgetExceeded;
		return EFlightPathStatus::BudgetExceeded;
	}
	const UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
	const FFlightPathRequest Request = MakeCandidatePathRequest(Data, *Capsule, Data.Enemy->GetActorLocation(),
		Data.MaxSearchNodes, Checks, Time);
	return FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Capsule, Request);
}

bool CanClassifyEmptyOrbitResult(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!EnemyOrbit::IsCombatRadius(Data.CapturedOrbitStart, Data.CapturedOrbitCenter))
	{
		return false;
	}
	// 환경 설정 누락/출발점 관통을 양쪽 후보 없음으로 오인하지 않는다.
	Data.SelectionPathStatus = CheckSelectionStart(Data);
	return Data.SelectionPathStatus == EFlightPathStatus::Success;
}

bool IsRetreatStartClear(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	Data.SelectionPathStatus = CheckSelectionStart(Data);
	if (Data.SelectionPathStatus != EFlightPathStatus::Success)
	{
		LogEnvQuery(TEXT("RetreatRejected: start unavailable or blocked; not proof of no destination"), Data);
		return false;
	}
	return true;
}

bool IsPhysicalRetreatFailure(EFlightPathStatus Status)
{
	return Status == EFlightPathStatus::BlockedEndpoint || Status == EFlightPathStatus::BlockedSegment
		|| Status == EFlightPathStatus::NoPathWithinBounds;
}

bool HasOnlyRetreatCandidates(const FEnvQueryResult& Result, const TArray<FVector>& Candidates)
{
	for (int32 Index = 0; Index < Result.Items.Num(); ++Index)
	{
		bool bFound = false;
		for (const FVector& Candidate : Candidates)
		{
			if (Candidate.Equals(Result.GetItemAsLocation(Index), 1.0))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			return false;
		}
	}
	return true;
}

FFlightPathResult CheckRetreatCandidate(const FEnemyRunEnvQueryTaskInstanceData& Data, const FVector& Candidate,
	const FCollisionQueryParams& SightParams, int32 Nodes, int32& Checks, double Started, bool& bSightBlocked)
{
	// 노출 후보의 사선과 A*는 같은 예산을 소비한다. 중앙 차폐만으로 머리 사격 가능한 자리를 버리지 않는다.
	if (Cast<AShooterCharacter>(Data.CapturedTarget.Get()))
	{
		bSightBlocked = !Data.Enemy->HasCombatAttackPathFrom(Data.CapturedTarget.Get(), Candidate, Checks);
	}
	else
	{
		--Checks;
		bSightBlocked = Data.Enemy->GetWorld()->LineTraceTestByChannel(
			Candidate, Data.SelectedTargetLocation, ECC_Visibility, SightParams);
	}
	FFlightPathResult Path;
	if (Checks < 2 && bSightBlocked) { Path.Status = EFlightPathStatus::BudgetExceeded; return Path; }
	Path.Status = EFlightPathStatus::BlockedSegment;
	if (bSightBlocked)
	{
		return Path;
	}
	const double Time = GetCandidatePathMilliseconds(Data) - (FPlatformTime::Seconds() - Started) * 1000.0;
	if (!HasCandidatePathBudget(Nodes, Checks, Time))
	{
		Path.Status = EFlightPathStatus::BudgetExceeded;
		return Path;
	}
	const UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
	const FFlightPathRequest Request = MakeCandidatePathRequest(Data, *Capsule, Candidate, Nodes, Checks, Time);
	return FlightPathfinding::FindPath(*Data.Enemy->GetWorld(), *Capsule, Request);
}

void LogRetreatCandidate(const FEnemyRunEnvQueryTaskInstanceData& Data, int32 QueryId, const FVector& Candidate,
	const FFlightPathResult& Path, bool bPassedQuery, bool bSightBlocked)
{
	if (CVarEnemyEQSDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
	UE_LOG(LogEnemyEQS, Log,
		TEXT("[EnemyRetreatCandidate] Enemy=%s Request=%d Candidate=%d Goal=%s Distance2D=%.1f PassedQuery=%d SightBlocked=%d Path=%s Nodes=%d Checks=%d Milliseconds=%.3f"),
		*GetNameSafe(Data.Enemy), QueryId, Data.CheckedPathCandidates, *Candidate.ToCompactString(),
		FVector::Dist2D(Candidate, Data.RetreatOrigin), bPassedQuery, bSightBlocked,
		*UEnum::GetValueAsString(Path.Status), Path.SearchNodes, Path.CollisionChecks, Path.ElapsedMilliseconds);
}

bool SelectRetreatDestination(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	TArray<FVector> Candidates;
	EnemyRetreat::BuildCandidates(Data.CapturedRetreatStart, Data.RetreatOrigin, Candidates);
	if (Data.HeightSnapshot.bEnabled)
	{
		if (Candidates.Num() != Data.HeightSnapshot.Candidates.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Candidates[Index] = Data.HeightSnapshot.Candidates[Index].Location;
		}
	}
	const double Started = FPlatformTime::Seconds();
	if (Candidates.IsEmpty() || !IsRetreatStartClear(Data))
	{
		return false;
	}
	// Generator의 전체 후보를 다시 확인한다. 빈 결과나 잘못된 EQS 필터를 후퇴 불가로 숨기지 않는다.
	if (!HasOnlyRetreatCandidates(Result, Candidates))
	{
		return false;
	}
	FCollisionQueryParams SightParams(SCENE_QUERY_STAT(EnemyRetreatSight), false, Data.Enemy);
	SightParams.AddIgnoredActor(Data.CapturedTarget.Get());
	int32 RemainingNodes = Data.MaxSearchNodes;
	// 시작점의 확장/실제 캡슐 예외까지 포함한 최대 검사 수를 먼저 확보한다.
	int32 RemainingChecks = Data.MaxCollisionChecks - Data.HeightSnapshot.CollisionChecks - 6;
	bool bHeightUnavailable = false;
	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const FVector& Candidate = Candidates[CandidateIndex];
		if (Data.HeightSnapshot.bEnabled)
		{
			const EFlightPathStatus HeightStatus = Data.HeightSnapshot.Candidates[CandidateIndex].Status;
			if (HeightStatus != EFlightPathStatus::Success)
			{
				bHeightUnavailable |= HeightStatus != EFlightPathStatus::BlockedEndpoint;
				continue;
			}
		}
		if (!IsEnvQuerySnapshotCurrent(Data))
		{
			Data.QueryResult = EEnemyEnvQueryResult::Stale;
			return false;
		}
		if (HasEnvQueryTimedOut(Data))
		{
			Data.QueryResult = EEnemyEnvQueryResult::TimedOut;
			return false;
		}
		if (Data.CheckedPathCandidates >= Data.MaxPathCandidates)
		{
			Data.QueryResult = EEnemyEnvQueryResult::CandidateLimitReached;
			return false;
		}
		const double RemainingTime = GetCandidatePathMilliseconds(Data) - (FPlatformTime::Seconds() - Started) * 1000.0;
		if (!HasCandidatePathBudget(RemainingNodes, RemainingChecks, RemainingTime))
		{
			Data.QueryResult = EEnemyEnvQueryResult::PathBudgetExceeded;
			Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
			return false;
		}
		++Data.CheckedPathCandidates;
		bool bSightBlocked = false;
		const FFlightPathResult Path = CheckRetreatCandidate(Data, Candidate, SightParams,
			RemainingNodes, RemainingChecks, Started, bSightBlocked);
		RemainingNodes -= Path.SearchNodes;
		RemainingChecks -= Path.CollisionChecks;
		Data.SelectionPathStatus = Path.Status;
		const bool bPassedQuery = HasCandidateResult(Result, Candidate);
		LogRetreatCandidate(Data, Result.QueryID, Candidate, Path, bPassedQuery, bSightBlocked);
		if (Path.Status == EFlightPathStatus::Success)
		{
			// 공간/시야/경로가 있는데 EQS가 제외했으면 설정 오류다. 제자리 사격으로 덮지 않는다.
			if (!bPassedQuery)
			{
				LogEnvQuery(TEXT("RetreatRejected: clear candidate filtered by EQS; check tests/context"), Data);
				return false;
			}
			Data.Destination = Candidate;
			return true;
		}
		if (Path.Status == EFlightPathStatus::BudgetExceeded)
		{
			Data.QueryResult = EEnemyEnvQueryResult::PathBudgetExceeded;
			return false;
		}
		if (!IsPhysicalRetreatFailure(Path.Status))
		{
			return false;
		}
	}
	if (HasEnvQueryTimedOut(Data))
	{
		Data.QueryResult = EEnemyEnvQueryResult::TimedOut;
		return false;
	}
	if ((FPlatformTime::Seconds() - Started) * 1000.0 >= GetCandidatePathMilliseconds(Data))
	{
		Data.QueryResult = EEnemyEnvQueryResult::PathBudgetExceeded;
		Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
		return false;
	}
	if (bHeightUnavailable)
	{
		return false;
	}
	Data.QueryResult = EEnemyEnvQueryResult::NoReachableDestination;
	Data.bNoRetreatDestination = true;
	return false;
}

const TCHAR* GetSearchCandidateRejection(const FEnemyRunEnvQueryTaskInstanceData& Data, const FVector& Candidate,
	const UEnemyRoomSubsystem& Rooms, float Separation)
{
	if (!EnemySearch::IsCandidateInRange(Candidate, Data.CapturedOrbitCenter, Data.CapturedSearchHeight, Data.SearchMode))
	{
		return TEXT("OutsideSearchRange");
	}
	if (Data.SearchMode == EEnemySearchQueryMode::Anchor)
	{
		return nullptr;
	}
	if (Rooms.IsSearchDestinationReserved(Data.Enemy, Candidate, Separation))
	{
		return TEXT("ReservedByOtherDrone");
	}
	for (const FVector& Point : Data.SelectedSearchPoints)
	{
		if (FVector::DistSquared(Candidate, Point) < FMath::Square(Separation))
		{
			return TEXT("NearSelectedPoint");
		}
	}
	for (const FVector& Point : Data.Enemy->GetSearchEpisode().CheckedPoints)
	{
		if (FVector::DistSquared(Candidate, Point) < FMath::Square(Separation))
		{
			return TEXT("AlreadyCheckedPoint");
		}
	}
	return nullptr;
}

void PrioritizeUncoveredSearchDirection(TArray<int32>& Order, int32 NextCandidate,
	const FEnvQueryResult& Result, const FVector& Center, const FVector& Start,
	const TArray<FVector>& SelectedPoints, const TArray<FVector>& CheckedPoints,
	const UEnemyRoomSubsystem* Rooms = nullptr, const AEnemyBase* Enemy = nullptr)
{
	const bool bOwnCoverage = !SelectedPoints.IsEmpty() || !CheckedPoints.IsEmpty();
	if (!bOwnCoverage && !Rooms)
	{
		return;
	}
	int32 Best = NextCandidate;
	double BestCoverage = -1.0;
	double BestTravel = TNumericLimits<double>::Max();
	double FirstCrowding = 0.0;
	double BestCrowding = 0.0;
	for (int32 Position = NextCandidate; Position < Order.Num(); ++Position)
	{
		const FVector Candidate = Result.GetItemAsLocation(Order[Position]);
		const FVector Direction = (Candidate - Center).GetSafeNormal2D();
		const double Crowding = Rooms ? Rooms->GetCombatObservationCrowding(Enemy, Center, Candidate) : 0.0;
		if (Position == NextCandidate)
		{
			FirstCrowding = Crowding;
		}
		double Coverage = 4.0;
		for (const TArray<FVector>* CoveredPoints : {&SelectedPoints, &CheckedPoints})
		{
			for (const FVector& Point : *CoveredPoints)
			{
				Coverage = FMath::Min(Coverage,
					FVector::DistSquared(Direction, (Point - Center).GetSafeNormal2D()));
			}
		}
		const double Travel = FVector::DistSquared(Start, Candidate);
		// 자신이 아직 보지 않은 방향을 먼저 넓히고, 같은 범위에서는 동료와 다른 각을 고른다.
		if (Coverage > BestCoverage || (Coverage == BestCoverage
			&& (Crowding < BestCrowding || (Crowding == BestCrowding && Travel < BestTravel))))
		{
			Best = Position;
			BestCoverage = Coverage;
			BestTravel = Travel;
			BestCrowding = Crowding;
		}
	}
	// 동료와 겹치지 않는 첫 지점은 원래 EQS 순서를 유지한다. 분산을 이유로 불필요하게 우회하지 않는다.
	if (Order.IsValidIndex(NextCandidate) && (bOwnCoverage || BestCrowding < FirstCrowding))
	{
		Order.Swap(NextCandidate, Best);
	}
}

bool SelectSearchDestinations(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	Data.bSearchSelectionPending = false;
	UEnemyRoomSubsystem* Rooms = Data.Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
	if (!Rooms || !Capsule || (Data.SearchMode == EEnemySearchQueryMode::Points
		&& Data.Enemy->GetSearchEpisode().bPointsSelected))
	{
		return false;
	}
	const float Separation = FMath::Max(Data.SearchPointSeparation,
		2.0f * (Capsule->GetScaledCapsuleRadius() + Data.Clearance));
	TArray<int32>& Order = Data.SearchCandidateOrder;
	if (Order.IsEmpty())
	{
		for (int32 Index = 0; Index < Result.Items.Num(); ++Index)
		{
			Order.Add(Index);
		}
	}
	if (Data.SearchMode == EEnemySearchQueryMode::Anchor)
	{
		// EQS 점수보다 원래 목격 위치의 XY를 우선한다. 장애물 보정이 불필요한 우회로 바뀌지 않게 한다.
		Order.StableSort([&Data, &Result](int32 Left, int32 Right)
		{
			return FVector::DistSquared2D(Result.GetItemAsLocation(Left), Data.CapturedOrbitCenter)
				< FVector::DistSquared2D(Result.GetItemAsLocation(Right), Data.CapturedOrbitCenter);
		});
	}
	const double Started = FPlatformTime::Seconds();
	int32 RemainingNodes = Data.MaxSearchNodes;
	int32 RemainingChecks = Data.MaxCollisionChecks;
	// 한 검증 묶음 안에서는 예산을 공유한다. 다음 묶음은 간격/횟수/전체 질의 제한 안에서만 실행한다.
	// 이전 묶음의 예산 초과 후보는 미확정이다. 뒤 후보가 전부 탈락해도 전체 경로 불가로 바꾸지 않는다.
	EEnemyEnvQueryResult SearchFailure = Data.bSearchPathBudgetExceeded
		? EEnemyEnvQueryResult::PathBudgetExceeded : EEnemyEnvQueryResult::NoReachableDestination;
	for (; Data.NextSearchCandidate < Order.Num();)
	{
		if (Data.SearchMode == EEnemySearchQueryMode::Points)
		{
			// 단서 중심의 미확인 방향을 먼저 고른다. 동률이면 직전 지점에서 가까운 곳을 택한다.
			// 이미 검사한 앞부분은 고정하므로 제한된 예산을 재개해도 실패 후보를 되풀이하지 않는다.
			const FVector Start = Data.SelectedSearchPoints.IsEmpty()
				? Data.Enemy->GetActorLocation() : Data.SelectedSearchPoints.Last();
			PrioritizeUncoveredSearchDirection(Order, Data.NextSearchCandidate, Result, Data.CapturedOrbitCenter,
				Start, Data.SelectedSearchPoints, Data.Enemy->GetSearchEpisode().CheckedPoints, Rooms, Data.Enemy);
		}
		const int32 Index = Order[Data.NextSearchCandidate];
		const FVector Candidate = Result.GetItemAsLocation(Index);
		// 범위/예약/중복을 먼저 거른 뒤, 남은 후보에만 실제 비행 경로 검증 비용을 쓴다.
		if (const TCHAR* Reason = GetSearchCandidateRejection(Data, Candidate, *Rooms, Separation))
		{
			if (CVarEnemyEQSDebug.GetValueOnGameThread() >= 2)
			{
				UE_LOG(LogEnemyEQS, Log, TEXT("[EnemySearchCandidate] Enemy=%s Episode=%llu Candidate=%d Reason=%s Goal=%s"),
					*GetNameSafe(Data.Enemy), Data.CapturedSearchSerial, Index, Reason, *Candidate.ToCompactString());
			}
			++Data.NextSearchCandidate;
			continue;
		}
		const double RemainingTime = Data.MaxSearchMilliseconds - (FPlatformTime::Seconds() - Started) * 1000.0;
		const bool bHasBudget = HasCandidatePathBudget(RemainingNodes, RemainingChecks, RemainingTime);
		if (Data.CheckedPathCandidates >= Data.MaxPathCandidates || !bHasBudget)
		{
			Data.bSearchSelectionLimited = true;
			SearchFailure = bHasBudget ? EEnemyEnvQueryResult::CandidateLimitReached : EEnemyEnvQueryResult::PathBudgetExceeded;
			Data.bSearchPathBudgetExceeded |= !bHasBudget;
			break;
		}
		FFlightPathRequest Request = MakeCandidatePathRequest(Data, *Capsule, Candidate,
			RemainingNodes, RemainingChecks, RemainingTime);
		if (!Data.SelectedSearchPoints.IsEmpty())
		{
			// 각 구간은 직전 선정 지점에서 이어진다. 네 지점도 실제 방문 순서대로 검증한다.
			Request.Start = Data.SelectedSearchPoints.Last() + Capsule->GetComponentLocation() - Data.Enemy->GetActorLocation();
			Request.SearchBounds = FBox(Request.Start, Request.Start);
			Request.SearchBounds += Request.Goal;
			Request.SearchBounds = Request.SearchBounds.ExpandBy(Data.SearchPadding);
		}
		const FFlightPathResult Path = FlightPathfinding::FindPath(*Data.Enemy->GetWorld(), *Capsule, Request);
		++Data.NextSearchCandidate;
		++Data.CheckedPathCandidates;
		RemainingNodes -= Path.SearchNodes;
		RemainingChecks -= Path.CollisionChecks;
		Data.SelectionPathStatus = Path.Status;
		if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogEnemyEQS, Log,
				TEXT("[EnemySearchPath] Enemy=%s Episode=%llu Candidate=%d From=%s Goal=%s Status=%s Nodes=%d Checks=%d Ms=%.3f"),
				*GetNameSafe(Data.Enemy), Data.CapturedSearchSerial, Index, *Request.Start.ToCompactString(),
				*Candidate.ToCompactString(), *UEnum::GetValueAsString(Path.Status), Path.SearchNodes,
				Path.CollisionChecks, Path.ElapsedMilliseconds);
		}
		if (Path.Status == EFlightPathStatus::Success)
		{
			if (Data.SearchMode == EEnemySearchQueryMode::Anchor)
			{
				Data.Destination = Candidate;
				return true;
			}
			Data.SelectedSearchPoints.Add(Candidate);
			if (Data.SelectedSearchPoints.Num() >= Data.Enemy->GetSearchPointLimit())
			{
				break;
			}
		}
		else if (Path.Status == EFlightPathStatus::BudgetExceeded)
		{
			Data.bSearchSelectionLimited = true;
			SearchFailure = EEnemyEnvQueryResult::PathBudgetExceeded;
			Data.bSearchPathBudgetExceeded = true;
			break;
		}
		else if (Path.Status == EFlightPathStatus::InvalidRequest || Path.Status == EFlightPathStatus::Unavailable)
		{
			Data.SelectedSearchPoints.Reset();
			return false;
		}
	}
	if (Data.SelectedSearchPoints.IsEmpty())
	{
		Data.QueryResult = SearchFailure;
		if (SearchFailure == EEnemyEnvQueryResult::PathBudgetExceeded)
		{
			Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
		}
		// 예산 부족은 경로 불가의 증거가 아니다. 미검증 후보가 남으면 Task를 완료하지 않고 재개한다.
		if (SearchFailure == EEnemyEnvQueryResult::PathBudgetExceeded
			&& Data.NextSearchCandidate < Order.Num() && Data.CheckedPathCandidates < Data.MaxPathCandidates
			&& Data.SearchSelectionRetries < Data.MaxSearchSelectionRetries)
		{
			++Data.SearchSelectionRetries;
			Data.bSearchSelectionPending = true;
			Data.QueryResult = EEnemyEnvQueryResult::Running;
			Data.NextSearchSelectionTime = Data.Enemy->GetWorld()->GetTimeSeconds() + Data.QueryInterval;
			LogEnvQuery(TEXT("SearchSelectionDeferred"), Data, Result.Items.Num(), Result.QueryID);
		}
		else if (SearchFailure == EEnemyEnvQueryResult::PathBudgetExceeded)
		{
			LogEnvQuery(TEXT("SearchSelectionBudgetExhausted"), Data, Result.Items.Num(), Result.QueryID);
		}
		return false;
	}
	// 예산 안에서 한 곳만 검증됐어도 그 지점은 사용한다. 제한 종료를 전체 경로 불가로 취급하지 않는다.
	Data.Destination = Data.SelectedSearchPoints[0];
	return true;
}

FName GetCombatSelectionIntent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (Data.CapturedCombatYieldSerial != 0)
	{
		return TEXT("YieldPosition");
	}
	if (Data.bUnifiedCombatNavigation)
	{
		return TEXT("AttackPosition");
	}
	return Data.bValidateRetreatPath ? FName(TEXT("Retreat"))
		: Data.bValidateOrbitPath ? FName(TEXT("Orbit")) : FName(TEXT("Approach"));
}

// 한 후보의 사선과 경로를 같은 공유 스케줄러 차례에서 검사한다.
// 선회는 각 곡선 구간만 Sweep하며 A* 지름길로 바꾸지 않는다.
class FCombatCandidatePathSearch final : public FFlightPathSearch
{
public:
	FCombatCandidatePathSearch(const FEnemyRunEnvQueryTaskInstanceData& Data, const FEnemyOrbitCandidate& Candidate,
		const TArray<FVector>& InWaypoints)
		: Enemy(Data.Enemy), Target(Data.CapturedTarget), Waypoints(InWaypoints),
		  CandidateLocation(Candidate.Destination), TargetLocation(Data.SelectedTargetLocation),
		  bOrbit((Data.bValidateOrbitPath && !Data.bUnifiedCombatNavigation) || InWaypoints.Num() >= 2),
		  bUnified(Data.bUnifiedCombatNavigation),
		  bProgressOnly(Data.bUnifiedCombatNavigation && Data.CombatCandidatesProgressOnly.IsValidIndex(Data.NextCombatCandidate)
			  && Data.CombatCandidatesProgressOnly[Data.NextCombatCandidate])
	{
		Request = MakeCandidatePathRequest(Data, *Data.Enemy->GetCapsuleComponent(), Candidate.Destination,
			Data.CombatRemainingNodes, Data.CombatRemainingChecks, Data.CombatRemainingMilliseconds);
		const TWeakObjectPtr<AEnemyAIController> Controller = Data.CachedController;
		const uint64 Serial = Data.CapturedCombatSerial;
		const bool bRecoveryProbe = Data.bCombatObservationWaiting;
		const int32 Lease = Data.CapturedLease, Generation = Data.CapturedGeneration;
		Request.ShouldCancel = [WeakEnemy = Enemy, WeakTarget = Target, Controller, Serial, Lease, Generation, bRecoveryProbe]()
		{
			const AEnemyBase* Pawn = WeakEnemy.Get();
			return !Pawn || !WeakTarget.IsValid() || Pawn->IsDead() || Pawn->IsEnemyPossessed()
				|| Pawn->IsAIControlSuppressed() || Pawn->GetController() != Controller.Get()
				|| Pawn->GetPoolLeaseSerial() != Lease || Pawn->GetPoolGameplayGeneration() != Generation
				|| (bRecoveryProbe ? Pawn->GetCombatTacticalEpisodeSerial() != Serial : !Pawn->IsCombatTacticalEpisodeCurrent(Serial))
				|| !Pawn->HasTrackableTarget()
				|| Pawn->GetTrackedTarget() != WeakTarget.Get();
		};
		Result.Status = EFlightPathStatus::InProgress;
	}

	virtual const FFlightPathResult& GetResult() const override { return Result; }
	virtual const FFlightPathRequest& GetRequest() const override { return Request; }
	virtual bool Advance(int32 MaxChecks, double MaxMilliseconds) override
	{
		if (Result.Status != EFlightPathStatus::InProgress)
		{
			return true;
		}
		if (Request.ShouldCancel())
		{
			Result.Status = EFlightPathStatus::Cancelled;
			return true;
		}
		// Advance(0,0) is the scheduler's cancellation poll. It must never issue a Trace.
		if (MaxChecks <= 0 || MaxMilliseconds <= 0.0)
		{
			return false;
		}
		const double Started = FPlatformTime::Seconds();
		int32 Checks = FMath::Min(MaxChecks, Request.MaxCollisionChecks - Result.CollisionChecks);
		const int32 BeforeChecks = Checks;
		AEnemyBase* Pawn = Enemy.Get();
		// 공격 후보의 끝점만 총구 사선을 요구한다. 우회 중간과 조사 경유점은 이동 안전성으로 판정한다.
		if (bUnified && !bSightChecked)
		{
			bSightChecked = true;
			if (!bProgressOnly && !CheckSight(CandidateLocation, Checks))
			{
				Result.Status = Checks < 2 ? EFlightPathStatus::BudgetExceeded : EFlightPathStatus::BlockedSegment;
			}
			SightCollisionChecks = BeforeChecks - Checks;
			SightMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
			if (Result.Status != EFlightPathStatus::InProgress)
			{
				Result.CollisionChecks += SightCollisionChecks;
				Result.ElapsedMilliseconds += SightMilliseconds;
				return true;
			}
		}
		if (!bOrbit)
		{
			if (!bSightChecked)
			{
				if (!CheckSight(CandidateLocation, Checks))
				{
					Result.Status = Checks < 2 ? EFlightPathStatus::BudgetExceeded : EFlightPathStatus::BlockedSegment;
				}
				SightCollisionChecks = BeforeChecks - Checks;
				SightMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
				bSightChecked = true;
			}
			if (Result.Status == EFlightPathStatus::InProgress)
			{
				if (!Path)
				{
					FFlightPathRequest PathRequest = Request;
					PathRequest.MaxCollisionChecks -= SightCollisionChecks;
					PathRequest.MaxSearchMilliseconds -= SightMilliseconds;
					Path = FlightPathfinding::BeginSearch(*Pawn->GetWorld(), *Pawn->GetCapsuleComponent(), PathRequest);
				}
				if (!Path)
				{
					Result.Status = EFlightPathStatus::Unavailable;
				}
				else
				{
					Path->Advance(Checks, FMath::Min(MaxMilliseconds, Request.MaxSearchMilliseconds - Result.ElapsedMilliseconds));
					Result = Path->GetResult();
					Result.CollisionChecks += SightCollisionChecks;
					Result.ElapsedMilliseconds += SightMilliseconds;
					return Result.Status != EFlightPathStatus::InProgress;
				}
			}
		}
		else
		{
			const FVector Offset = Pawn->GetCapsuleComponent()->GetComponentLocation() - Pawn->GetActorLocation();
			if (!bStartChecked && Checks >= 4)
			{
				FFlightPathRequest StartCheck = Request;
				StartCheck.Goal = StartCheck.Start;
				Checks -= 4;
				bStartChecked = true;
				if (FlightPathfinding::CheckSegment(*Pawn->GetWorld(), *Pawn->GetCapsuleComponent(), StartCheck)
					!= EFlightPathStatus::Success)
				{
					Result.Status = EFlightPathStatus::Unavailable;
				}
			}
			while (NextSegment < Waypoints.Num() && Checks >= 36
				&& Result.Status == EFlightPathStatus::InProgress
				&& (FPlatformTime::Seconds() - Started) * 1000.0 < MaxMilliseconds)
			{
				FFlightPathRequest Segment = Request;
				Segment.Start = Waypoints[NextSegment - 1] + Offset;
				Segment.Goal = Waypoints[NextSegment] + Offset;
				Checks -= 4;
				Result.Status = FlightPathfinding::CheckSegment(*Pawn->GetWorld(), *Pawn->GetCapsuleComponent(), Segment);
				if (Result.Status != EFlightPathStatus::Success)
				{
					break;
				}
				if (!bUnified && !CheckSight(Waypoints[NextSegment], Checks))
				{
					Result.Status = Checks < 2 ? EFlightPathStatus::BudgetExceeded : EFlightPathStatus::BlockedSegment;
					break;
				}
				++NextSegment;
				Result.Status = EFlightPathStatus::InProgress;
			}
			if (NextSegment >= Waypoints.Num())
			{
				Result.Status = Waypoints.Num() >= 2 ? EFlightPathStatus::Success : EFlightPathStatus::InvalidRequest;
				Result.Waypoints = Waypoints;
			}
		}
		Result.CollisionChecks += BeforeChecks - Checks;
		Result.ElapsedMilliseconds += (FPlatformTime::Seconds() - Started) * 1000.0;
		if (Result.Status == EFlightPathStatus::InProgress
			&& (Result.CollisionChecks + 36 > Request.MaxCollisionChecks || Result.ElapsedMilliseconds >= Request.MaxSearchMilliseconds))
		{
			Result.Status = EFlightPathStatus::BudgetExceeded;
		}
		return Result.Status != EFlightPathStatus::InProgress;
	}

private:
	bool CheckSight(const FVector& Location, int32& Checks) const
	{
		if (bUnified)
		{
			return Enemy->HasCombatAttackPathFrom(Target.Get(), Location, Checks);
		}
		if (Cast<AShooterCharacter>(Target.Get()))
		{
			return Enemy->HasCombatAttackPathFrom(Target.Get(), Location, Checks);
		}
		if (Checks < 1)
		{
			return false;
		}
		--Checks;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyCombatCandidateSight), false, Enemy.Get());
		Params.AddIgnoredActor(Target.Get());
		return !Enemy->GetWorld()->LineTraceTestByChannel(Location, TargetLocation, ECC_Visibility, Params);
	}
	TWeakObjectPtr<AEnemyBase> Enemy;
	TWeakObjectPtr<AActor> Target;
	TArray<FVector> Waypoints;
	FVector CandidateLocation, TargetLocation;
	FFlightPathRequest Request;
	FFlightPathResult Result;
	TSharedPtr<FFlightPathSearch> Path;
	int32 NextSegment = 1;
	bool bOrbit = false;
	bool bUnified = false;
	bool bProgressOnly = false;
	bool bSightChecked = false;
	bool bStartChecked = false;
	int32 SightCollisionChecks = 0;
	double SightMilliseconds = 0.0;
};

// 탐색 중 이동한 캡슐을 새 경로에 연결하는 검사도 전역 스케줄러의 허가를 받아 실행한다.
class FCombatRouteJoinSearch final : public FFlightPathSearch
{
public:
	FCombatRouteJoinSearch(AEnemyBase* InEnemy, const FFlightPathRequest& InRequest, const FVector& Goal)
		: Enemy(InEnemy), Request(InRequest)
	{
		Request.Goal = Goal;
		Result.Status = EFlightPathStatus::InProgress;
	}
	virtual const FFlightPathResult& GetResult() const override { return Result; }
	virtual const FFlightPathRequest& GetRequest() const override { return Request; }
	virtual bool Advance(int32 MaxChecks, double MaxMilliseconds) override
	{
		if (!Enemy.IsValid() || Request.ShouldCancel())
		{
			Result.Status = EFlightPathStatus::Cancelled;
			return true;
		}
		if (MaxChecks < 4 || MaxMilliseconds <= 0.0)
		{
			return false;
		}
		const double Started = FPlatformTime::Seconds();
		Request.Start = Enemy->GetCapsuleComponent()->GetComponentLocation();
		Result.Status = FlightPathfinding::CheckSegment(*Enemy->GetWorld(), *Enemy->GetCapsuleComponent(), Request);
		Result.CollisionChecks = 4;
		Result.ElapsedMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
		Result.Waypoints = {Request.Start, Request.Goal};
		return true;
	}
private:
	TWeakObjectPtr<AEnemyBase> Enemy;
	FFlightPathRequest Request;
	FFlightPathResult Result;
};

// 경로 검증이 끝난 뒤 같은 공유 실행 차례/예산에서 통로 폭을 분류한다.
// 긴 직선도 100cm씩 나눠 좁은 부분만 예약하며, 분류가 덜 끝난 경로는 성공으로 공개하지 않는다.
class FCombatReservationPathSearch final : public FFlightPathSearch
{
public:
	FCombatReservationPathSearch(const FEnemyRunEnvQueryTaskInstanceData& Data, const FEnemyOrbitCandidate& Candidate,
		const TArray<FVector>& Waypoints)
		: Path(MakeShared<FCombatCandidatePathSearch>(Data, Candidate, Waypoints)), Enemy(Data.Enemy),
		  bOrbit((Data.bValidateOrbitPath && !Data.bUnifiedCombatNavigation) || Waypoints.Num() >= 2)
	{
		Result.Status = EFlightPathStatus::InProgress;
	}
	virtual const FFlightPathResult& GetResult() const override { return Result; }
	virtual const FFlightPathRequest& GetRequest() const override { return Path->GetRequest(); }
	const TArray<FEnemyCombatCorridor>& GetCorridors() const { return Corridors; }
	const TArray<FVector>& GetRoute() const { return Route; }
	TSharedPtr<FFlightPathSearch> BeginRebaseRoute() const
	{
		if (Route.Num() < 2)
		{
			return nullptr;
		}
		const FVector Start = Enemy->GetCapsuleComponent()->GetComponentLocation();
		int32 Nearest = 1;
		for (int32 Index = 2; Index < Route.Num(); ++Index)
		{
			if (FVector::DistSquared(Start, Route[Index]) < FVector::DistSquared(Start, Route[Nearest]))
			{
				Nearest = Index;
			}
		}
		return MakeShared<FCombatRouteJoinSearch>(Enemy.Get(), GetRequest(), Route[Nearest]);
	}
	bool ApplyRebasedRoute(const FFlightPathResult& Join)
	{
		if (Join.Status != EFlightPathStatus::Success || Join.Waypoints.Num() != 2 || Corridors.Num() >= 128)
		{
			return false;
		}
		const int32 Nearest = Route.IndexOfByPredicate([&Join](const FVector& Point) { return Point.Equals(Join.Waypoints.Last(), 0.01); });
		if (Nearest < 1)
		{
			return false;
		}
		Route.RemoveAt(0, Nearest);
		Route.Insert(Join.Waypoints[0], 0);
		// 새 연결 구간은 폭과 무관하게 보수적으로 예약한다. 옛 통로 목록만으로 새 구간을 승인하지 않는다.
		Corridors.Add({Join.Waypoints[0], Join.Waypoints[1]});
		return true;
	}
	virtual bool Advance(int32 MaxChecks, double MaxMilliseconds) override
	{
		if (Result.Status != EFlightPathStatus::InProgress)
		{
			return true;
		}
		if (GetRequest().ShouldCancel())
		{
			Result.Status = EFlightPathStatus::Cancelled;
			return true;
		}
		if (MaxChecks <= 0 || MaxMilliseconds <= 0.0)
		{
			return false;
		}
		if (!bPathComplete)
		{
			const bool bComplete = Path->Advance(MaxChecks, MaxMilliseconds);
			Result = Path->GetResult();
			if (!bComplete || Result.Status != EFlightPathStatus::Success)
			{
				return bComplete;
			}
			bPathComplete = true;
			Route = Result.Waypoints;
			if (bOrbit)
			{
				const FVector Offset = Enemy->GetCapsuleComponent()->GetComponentLocation() - Enemy->GetActorLocation();
				for (FVector& Point : Route)
				{
					Point += Offset;
				}
			}
			Result.Status = EFlightPathStatus::InProgress;
			// 경로가 사용한 이번 슬라이스 비용을 다시 배정하지 않는다. 분류는 다음 차례부터 진행한다.
			return false;
		}
		const double Started = FPlatformTime::Seconds();
		int32 Remaining = FMath::Min(MaxChecks, GetRequest().MaxCollisionChecks - Result.CollisionChecks);
		while (Route.IsValidIndex(SegmentIndex) && Remaining >= 4
			&& (FPlatformTime::Seconds() - Started) * 1000.0 < MaxMilliseconds)
		{
			const FVector SegmentStart = FMath::Lerp(Route[SegmentIndex - 1], Route[SegmentIndex], SegmentAlpha);
			const double Length = FVector::Distance(Route[SegmentIndex - 1], Route[SegmentIndex]);
			const double NextAlpha = FMath::Min(1.0, SegmentAlpha + 100.0 / FMath::Max(Length, 1.0));
			FFlightPathRequest Probe = GetRequest();
			Probe.Start = SegmentStart;
			Probe.Goal = FMath::Lerp(Route[SegmentIndex - 1], Route[SegmentIndex], NextAlpha);
			const FFlightPathResult Passing = FlightPathfinding::CheckPassingSpace(*Enemy->GetWorld(),
				*Enemy->GetCapsuleComponent(), Probe);
			Result.CollisionChecks += Passing.CollisionChecks;
			Remaining -= 4;
			if (Passing.Status == EFlightPathStatus::BlockedEndpoint || Passing.Status == EFlightPathStatus::BlockedSegment)
			{
				if (Corridors.Num() >= 128)
				{
					Result.Status = EFlightPathStatus::BudgetExceeded;
					break;
				}
				Corridors.Add({Probe.Start, Probe.Goal});
			}
			else if (Passing.Status != EFlightPathStatus::Success)
			{
				Result.Status = Passing.Status;
				break;
			}
			SegmentAlpha = NextAlpha;
			if (NextAlpha >= 1.0)
			{
				++SegmentIndex;
				SegmentAlpha = 0.0;
			}
		}
		Result.ElapsedMilliseconds += (FPlatformTime::Seconds() - Started) * 1000.0;
		if (Result.Status == EFlightPathStatus::InProgress)
		{
			if (!Route.IsValidIndex(SegmentIndex))
			{
				Result.Status = Route.Num() >= 2 ? EFlightPathStatus::Success : EFlightPathStatus::InvalidRequest;
			}
			else if (Result.CollisionChecks + 4 > GetRequest().MaxCollisionChecks
				|| Result.ElapsedMilliseconds >= GetRequest().MaxSearchMilliseconds)
			{
				Result.Status = EFlightPathStatus::BudgetExceeded;
			}
		}
		return Result.Status != EFlightPathStatus::InProgress;
	}
private:
	TSharedPtr<FCombatCandidatePathSearch> Path;
	TWeakObjectPtr<AEnemyBase> Enemy;
	FFlightPathResult Result;
	TArray<FEnemyCombatCorridor> Corridors;
	TArray<FVector> Route;
	int32 SegmentIndex = 1;
	double SegmentAlpha = 0.0;
	bool bPathComplete = false;
	bool bOrbit = false;
};

bool IsYieldDestinationClear(const FVector& Destination, const FVector& CapsuleOffset,
	float CapsuleRadius, float CapsuleHalfHeight, float Clearance, const FVector& Start, const FVector& End)
{
	const FVector Center = Destination + CapsuleOffset;
	const FVector Closest = FMath::ClosestPointOnSegment(Center, Start, End);
	// 보호할 경로의 캡슐 폭까지 비운다. 예약을 반환했다는 이유만으로 실제 몸이 빠진 것으로 보지 않는다.
	return FMath::Abs(Center.Z - Closest.Z) > CapsuleHalfHeight * 2.0f + Clearance
		|| FVector::DistSquared2D(Center, Closest) > FMath::Square(CapsuleRadius * 2.0f + Clearance);
}

void BuildCombatYieldCandidates(const FVector& Origin, const FVector& CapsuleOffset,
	float CapsuleRadius, float CapsuleHalfHeight, float Clearance, float PreferredRadius,
	const FVector& Start, const FVector& End, TArray<EnemyCombatNavigation::FCandidate>& Candidates)
{
	Candidates.Reset();
	if (Origin.ContainsNaN() || CapsuleOffset.ContainsNaN() || Start.ContainsNaN() || End.ContainsNaN()
		|| !FMath::IsFinite(CapsuleRadius) || !FMath::IsFinite(CapsuleHalfHeight)
		|| !FMath::IsFinite(Clearance) || !FMath::IsFinite(PreferredRadius)
		|| CapsuleRadius <= 0.0f || CapsuleHalfHeight < CapsuleRadius || Clearance < 0.0f || PreferredRadius <= 0.0f)
	{
		return;
	}
	FVector Along = (End - Start).GetSafeNormal2D();
	if (Along.IsNearlyZero())
	{
		Along = FVector::ForwardVector;
	}
	const float Radius = FMath::Max(PreferredRadius, CapsuleRadius * 2.0f + Clearance);
	// 양쪽 옆과 대각선만 최대 8개 만든다. 앞의 동료에게 더 다가가는 직진 후보는 넣지 않는다.
	for (const float Distance : {Radius, Radius * 2.0f})
	{
		for (const float Angle : {90.0f, -90.0f, 45.0f, -45.0f})
		{
			const FVector Destination = Origin + Along.RotateAngleAxis(Angle, FVector::UpVector) * Distance;
			if (IsYieldDestinationClear(Destination, CapsuleOffset, CapsuleRadius, CapsuleHalfHeight, Clearance, Start, End))
			{
				Candidates.Add({Destination, true});
			}
		}
	}
}

void BeginUnifiedCombatSelection(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 새 평가가 시작되면 이전 소진 승인은 거둔다. 이미 사용한 시도 사격 기록은 Pawn이 별도로 보존한다.
	Data.Enemy->ClearTargetAttackAlternativesExhausted();
	Data.CombatCandidates.Reset();
	Data.CombatCandidatesProgressOnly.Reset();
	Data.ValidatedCombatPaths.Reset();
	Data.CurrentCombatRoute.Reset();
	Data.CurrentCombatCandidate = INDEX_NONE;
	Data.bPreserveCurrentCombatRoute = false;
	Data.bAcceptingRetargetRoute = false;
	Data.BestCombatPath.Reset();
	Data.CombatJoinSearch.Reset();
	Data.CombatJoinCandidate = INDEX_NONE;
	Data.CurrentCombatPath.Reset();
	Data.CurrentCombatSeconds = TNumericLimits<double>::Max();
	Data.BestCombatCandidate = INDEX_NONE;
	Data.BestCombatSeconds = Data.BestCombatDistanceError = TNumericLimits<double>::Max();
	Data.BestCombatCrowding = 0.0;
	const ARangedWeaponBase* Weapon = Data.Enemy->GetCurrentWeapon();
	TArray<EnemyCombatNavigation::FCandidate> Candidates;
	const bool bYield = Data.CapturedCombatYieldSerial != 0;
	if (bYield)
	{
		const UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
		BuildCombatYieldCandidates(Data.Enemy->GetActorLocation(), Capsule->GetComponentLocation() - Data.Enemy->GetActorLocation(),
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), Data.Clearance,
			Data.Enemy->GetRuntimeStat().CombatYieldCandidateRadius, Data.CapturedCombatYieldStart,
			Data.CapturedCombatYieldEnd, Candidates);
	}
	else
	{
		EnemyCombatNavigation::BuildCandidates(Data.Enemy->GetActorLocation(), Data.SelectedTargetLocation,
			Weapon ? Weapon->GetEffectiveRange() : 1500.0f, Data.bCapturedOrbitLeft, Candidates);
	}
	if (!bYield && !Data.bNavigationTargetVisible)
	{
		// 은폐 중에는 공격 반경의 자리 대신 관측 지점 주변으로 접근한다. 숨은 대상의 본/현재 위치는 읽지 않는다.
		Candidates.RemoveAll([](const auto& Candidate) { return !Candidate.bProgressOnly; });
		const FVector Away = (Data.Enemy->GetActorLocation() - Data.SelectedTargetLocation).GetSafeNormal2D();
		FVector NearObservation = Data.SelectedTargetLocation + Away * 150.0;
		NearObservation.Z = Data.Enemy->GetActorLocation().Z;
		Candidates.Insert({NearObservation, true}, 0);
	}
	Candidates.RemoveAll([&Data](const auto& Candidate)
	{
		return FVector::DistSquared(Candidate.Destination, Data.Enemy->GetActorLocation()) < FMath::Square(60.0);
	});
	Candidates.StableSort([&Data](const auto& A, const auto& B)
	{
		if (!Data.bNavigationTargetVisible && Data.CapturedCombatYieldSerial == 0)
		{
			const double Radius = Data.Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const double DistanceA = FVector::Distance(Data.SelectedTargetLocation, A.Destination);
			const double DistanceB = FVector::Distance(Data.SelectedTargetLocation, B.Destination);
			// 거리 구간을 고정해 정렬의 전이성을 지킨다. 쌍마다 허용 오차를 적용하면 순환 순서가 생긴다.
			const double DistanceBand = FMath::Max(1.0, Radius * 2.0);
			if (Data.CachedReservationRooms && FMath::FloorToDouble(DistanceA / DistanceBand)
				== FMath::FloorToDouble(DistanceB / DistanceBand))
			{
				const double CrowdingA = Data.CachedReservationRooms->GetCombatObservationCrowding(
					Data.Enemy, Data.SelectedTargetLocation, A.Destination);
				const double CrowdingB = Data.CachedReservationRooms->GetCombatObservationCrowding(
					Data.Enemy, Data.SelectedTargetLocation, B.Destination);
				if (CrowdingA != CrowdingB)
				{
					return CrowdingA < CrowdingB;
				}
			}
			return FVector::DistSquared(Data.SelectedTargetLocation, A.Destination)
				< FVector::DistSquared(Data.SelectedTargetLocation, B.Destination);
		}
		if (A.bProgressOnly != B.bProgressOnly)
		{
			return !A.bProgressOnly;
		}
		return FVector::DistSquared(Data.Enemy->GetActorLocation(), A.Destination)
			< FVector::DistSquared(Data.Enemy->GetActorLocation(), B.Destination);
	});
	if (!bYield && !Data.bNavigationTargetVisible)
	{
		// 먼 은폐 지점의 우회 탐색이 예산을 모두 쓰기 전에 짧은 안전 접근을 하나 확보한다.
		// 뒤의 후보가 실패해도 이 경로를 이어갈 수 있으며, 이것만으로 실패 회차를 초기화하지 않는다.
		const int32 ShortApproach = Candidates.IndexOfByPredicate([&Data](const auto& Candidate)
		{
			return FVector::DistSquared(Candidate.Destination, Data.Enemy->GetActorLocation()) <= FMath::Square(650.0)
				&& FVector::DistSquared(Candidate.Destination, Data.SelectedTargetLocation)
					< FVector::DistSquared(Data.Enemy->GetActorLocation(), Data.SelectedTargetLocation);
		});
		if (ShortApproach > 0)
		{
			const auto Candidate = Candidates[ShortApproach];
			Candidates.RemoveAt(ShortApproach);
			Candidates.Insert(Candidate, 0);
		}
	}
	// 한 차례가 공격 후보만으로 끝나지 않도록 안전 경유점도 같은 제한 안에서 검사한다.
	const int32 ProgressIndex = Candidates.IndexOfByPredicate([](const auto& Candidate) { return Candidate.bProgressOnly; });
	if (ProgressIndex > 2)
	{
		const auto Progress = Candidates[ProgressIndex];
		Candidates.RemoveAt(ProgressIndex);
		Candidates.Insert(Progress, 2);
	}
	FVector CurrentDestination;
	bool bCurrentProgressOnly = false;
	if (!bYield && Data.Enemy->GetCombatNavigationHandoff(Data.CurrentCombatRoute, CurrentDestination, &bCurrentProgressOnly))
	{
		const bool bRetarget = Data.Enemy->IsCombatNavigationRetargetPending();
		// 이전 대상의 경유점 자격을 새 대상에 넘기지 않는다. 새 시야 대상은 끝점의 공격 사선을 다시 요구한다.
		// 숨은 대상은 새 관측점에 실제로 가까워지는 안전 경로만 조사 후보로 계승한다.
		const bool bUsefulForObservation = Data.bNavigationTargetVisible || !bRetarget
			|| FVector::DistSquared(CurrentDestination, Data.SelectedTargetLocation)
				< FVector::DistSquared(Data.Enemy->GetActorLocation(), Data.SelectedTargetLocation);
		if (bUsefulForObservation)
		{
			Candidates.Insert({CurrentDestination, !Data.bNavigationTargetVisible || (!bRetarget && bCurrentProgressOnly)}, 0);
			Data.CurrentCombatCandidate = 0;
			Data.bPreserveCurrentCombatRoute = bRetarget;
		}
	}
	TArray<FVector> Locations;
	for (const auto& Candidate : Candidates)
	{
		Locations.Add(Candidate.Destination);
		Data.CombatCandidatesProgressOnly.Add(!Data.bNavigationTargetVisible || Candidate.bProgressOnly);
	}
	const FVector HeightStart = Data.Enemy->GetActorLocation();
	EnemyFlightHeight::PrepareCombatCandidates(*Data.Enemy, Locations, HeightStart,
		Data.SelectedTargetLocation, Data.Clearance, Data.MaxCollisionChecks, Data.MaxSearchMilliseconds, Data.HeightSnapshot);
	Data.CombatCandidatesPassedQuery.Init(true, Locations.Num());
	Data.CombatCandidatesHeightLimited.Reset();
	const bool bCanRecordHeightFailure = !bYield && Data.HeightSnapshot.bEnabled
		&& Data.HeightSnapshot.Candidates.Num() == Locations.Num()
		&& EnemyFlightHeight::IsSnapshotCurrent(*Data.Enemy, Data.HeightSnapshot)
		&& IsEnvQuerySnapshotCurrent(Data)
		&& Data.Enemy->IsCombatReservationRequestCurrent(Data.CombatReservationRequest);
	for (int32 Index = 0; Index < Locations.Num(); ++Index)
	{
		const FEnemyFlightHeightCandidate Height = Data.HeightSnapshot.Candidates.IsValidIndex(Index)
			? Data.HeightSnapshot.Candidates[Index] : FEnemyFlightHeightCandidate();
		Data.CombatCandidates.Add({Height.Location, 0.0f, false});
		const bool bPhysicalHeightFailure = bCanRecordHeightFailure
			&& Height.Status == EFlightPathStatus::BlockedEndpoint && Height.Location.Equals(Locations[Index], 0.01);
		if (bPhysicalHeightFailure)
		{
			// 높이 검사가 끝낸 물리적 탈락은 예산 부족과 구분한다. 안전 높이가 없는 원래 Z로
			// A*를 재시도하지 않고, 후속 선택기는 같은 후보의 완료된 실패 증거만 재사용한다.
			const UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
			const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(Capsule->GetScaledCapsuleRadius() + Data.Clearance,
				Capsule->GetScaledCapsuleHalfHeight() + Data.Clearance, Capsule->GetComponentQuat());
			const FEnemyFlightHeightSettings& Settings = Data.HeightSnapshot.Settings;
			// 바닥/천장 탐색 범위를 함께 보관해야 해당 수직 공간이 열린 뒤 옛 실패를 폐기한다.
			const FBox Bounds(FVector(Height.Location.X, Height.Location.Y, HeightStart.Z - Settings.FloorProbeDistance),
				FVector(Height.Location.X, Height.Location.Y, HeightStart.Z + Settings.HighHeight + 2.0 * Extent.Z
					+ Settings.CorrectionStep * Settings.CorrectionSteps + 2.0));
			const FName Intent = GetCombatSelectionIntent(Data);
			Data.Enemy->MarkCombatTacticalCandidateChecked(Intent, Height.Location);
			Data.Enemy->RecordCombatTacticalCandidateFailure(Intent, Height.Location, Bounds.ExpandBy(Extent));
		}
		Data.CombatCandidatesHeightLimited.Add(!bPhysicalHeightFailure && (Height.Status != EFlightPathStatus::Success
			|| (Index == Data.CurrentCombatCandidate && !Height.Location.Equals(Locations[Index], 0.01))
			|| (bYield && !IsYieldDestinationClear(Height.Location,
				Data.Enemy->GetCapsuleComponent()->GetComponentLocation() - Data.Enemy->GetActorLocation(),
				Data.Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(), Data.Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
				Data.Clearance, Data.CapturedCombatYieldStart, Data.CapturedCombatYieldEnd))));
	}
	Data.NextCombatCandidate = 0;
	Data.ValidatedCombatPaths.SetNum(Data.CombatCandidates.Num());
	Data.CombatRemainingNodes = Data.MaxSearchNodes;
	Data.CombatRemainingChecks = Data.MaxCollisionChecks - Data.HeightSnapshot.CollisionChecks;
	Data.CombatRemainingMilliseconds = GetCandidatePathMilliseconds(Data);
	Data.bCombatHadLimitedCandidate = false;
	Data.CombatPathSearch.Reset();
	Data.bCombatSelectionPending = true;
	Data.QueryResult = EEnemyEnvQueryResult::Running;
}

void BeginCombatSelection(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	if (Data.bUnifiedCombatNavigation)
	{
		BeginUnifiedCombatSelection(Data);
		return;
	}
	Data.CombatCandidates.Reset();
	Data.CombatCandidatesPassedQuery.Reset();
	Data.CombatCandidatesHeightLimited.Reset();
	Data.NextCombatCandidate = 0;
	Data.CombatRemainingNodes = Data.MaxSearchNodes;
	Data.CombatRemainingChecks = Data.MaxCollisionChecks - Data.HeightSnapshot.CollisionChecks;
	Data.CombatRemainingMilliseconds = GetCandidatePathMilliseconds(Data);
	Data.bCombatHadLimitedCandidate = false;
	Data.CombatPathSearch.Reset();
	if (Data.bValidateOrbitPath)
	{
		EnemyOrbit::BuildCandidates(Data.CapturedOrbitStart, Data.CapturedOrbitCenter,
			Data.CapturedOrbitLength, Data.bCapturedOrbitLeft, Data.CombatCandidates);
	}
	else
	{
		TArray<FVector> Locations;
		if (Data.bValidateRetreatPath)
		{
			EnemyRetreat::BuildCandidates(Data.CapturedRetreatStart, Data.RetreatOrigin, Locations);
		}
		else
		{
			GetReachableCandidateOrder(Data, Result, Locations);
		}
		for (const FVector& Location : Locations)
		{
			Data.CombatCandidates.Add({Location, 0.0f, false});
		}
	}
	if (Data.HeightSnapshot.bEnabled && (Data.bValidateOrbitPath || Data.bValidateRetreatPath)
		&& Data.CombatCandidates.Num() != Data.HeightSnapshot.Candidates.Num())
	{
		return;
	}
	for (int32 Index = 0; Index < Data.CombatCandidates.Num(); ++Index)
	{
		FEnemyOrbitCandidate& Candidate = Data.CombatCandidates[Index];
		const bool bHeightAdjusted = Data.HeightSnapshot.bEnabled && (Data.bValidateOrbitPath || Data.bValidateRetreatPath);
		const EFlightPathStatus HeightStatus = bHeightAdjusted ? Data.HeightSnapshot.Candidates[Index].Status : EFlightPathStatus::Success;
		if (bHeightAdjusted)
		{
			Candidate.Destination = Data.HeightSnapshot.Candidates[Index].Location;
		}
		Data.CombatCandidatesHeightLimited.Add(HeightStatus != EFlightPathStatus::Success && HeightStatus != EFlightPathStatus::BlockedEndpoint);
		Data.CombatCandidatesPassedQuery.Add(HasCandidateResult(Result, Candidate.Destination));
		// 잘못된 설정으로 EQS가 제거한 정상 후보도 검증해 물리 실패로 덮지 않는다.
	}
	if (Data.bValidateRetreatPath)
	{
		TArray<FVector> Locations;
		for (const FEnemyOrbitCandidate& Candidate : Data.CombatCandidates)
		{
			Locations.Add(Candidate.Destination);
		}
		if (!HasOnlyRetreatCandidates(Result, Locations))
		{
			return;
		}
	}
	if (Data.bValidateOrbitPath)
	{
		TArray<FVector> Locations;
		for (const FEnemyOrbitCandidate& Candidate : Data.CombatCandidates)
		{
			Locations.Add(Candidate.Destination);
		}
		if (!HasOnlyRetreatCandidates(Result, Locations))
		{
			return;
		}
	}
	Data.bCombatSelectionPending = true;
	Data.bCombatReservationConflict = false;
	Data.CombatYieldStartedAt = -1.0;
	Data.CombatYieldPath.Reset();
	Data.QueryResult = EEnemyEnvQueryResult::Running;
}

bool TryAcceptReservedCombatCandidate(FEnemyRunEnvQueryTaskInstanceData& Data,
	const FEnemyOrbitCandidate& Candidate, const FCombatReservationPathSearch& Path)
{
	UEnemyRoomSubsystem* Rooms = Data.CachedReservationRooms;
	if (!Rooms || !Rooms->TryReserveCombatDestination(Data.Enemy, Data.CombatReservationRequest,
		Candidate.Destination, Path.GetCorridors(), Path.GetRoute(), Data.Clearance))
	{
		return false;
	}
	if (!Data.Enemy->AdoptCombatReservation(Data.CombatReservationRequest))
	{
		return false;
	}
	Data.Destination = Candidate.Destination;
	if (Data.bUnifiedCombatNavigation)
	{
		// 사선 확보 경로는 A* 결과를 그대로 Move에 넘긴다. 목적지만 넘겨 새 지름길을 만들지 않는다.
		Data.SelectedWaypoints = Path.GetRoute();
		const FVector Offset = Data.Enemy->GetCapsuleComponent()->GetComponentLocation() - Data.Enemy->GetActorLocation();
		for (FVector& Point : Data.SelectedWaypoints)
		{
			Point -= Offset;
		}
		const int32 CandidateIndex = Data.CombatCandidates.IndexOfByPredicate([&Candidate](const auto& Entry)
		{
			return Entry.Destination.Equals(Candidate.Destination, 0.01);
		});
		Data.Enemy->RememberCombatNavigationPath(Data.CombatReservationRequest, Candidate.Destination, Data.SelectedWaypoints,
			Data.CombatCandidatesProgressOnly.IsValidIndex(CandidateIndex) && Data.CombatCandidatesProgressOnly[CandidateIndex]);
		if (Data.CapturedCombatYieldSerial != 0)
		{
			// 실제 경로와 예약을 모두 얻은 뒤에만 양보 이동을 인계한다. 요청 만료로 진행 중 허가를 잃지 않는다.
			Data.Enemy->BeginCombatYieldMovement(Data.CombatReservationRequest, Data.CapturedCombatYieldSerial);
		}
		// 공격 자리뿐 아니라 안전 경유 경로도 실행 가능한 대안이다. 환경 차단 예외보다 이동을 우선한다.
		Data.Enemy->ClearTargetAttackAlternativesExhausted();
	}
	else if (Data.bValidateOrbitPath)
	{
		Data.SelectedWaypoints = Path.GetResult().Waypoints;
		Data.SelectedArcLength = FMath::Sqrt(FMath::Square(double(Candidate.ArcLength))
			+ FMath::Square(Candidate.Destination.Z - Data.CapturedOrbitStart.Z));
		Data.bSelectedOrbitLeft = Candidate.bLeft;
		Data.Enemy->RememberCombatOrbitDirection(Data.SelectedTargetActor, Data.bSelectedOrbitLeft);
	}
	Data.bHasDestination = true;
	Data.bCombatSelectionPending = false;
	Data.QueryResult = EEnemyEnvQueryResult::Success;
	if (Data.bValidateRetreatPath && CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
		UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyRetreatSelected] Enemy=%s Short=%d Start=%s Goal=%s StartDistance2D=%.1f GoalDistance2D=%.1f Checked=%d Episode=%llu Now=%.3f Lease=%d Generation=%d Target=%s"),
			*GetNameSafe(Data.Enemy), FVector::Dist2D(Data.Destination, Data.CapturedRetreatStart) <= 300.01,
			*Data.CapturedRetreatStart.ToCompactString(), *Data.Destination.ToCompactString(),
			FVector::Dist2D(Data.CapturedRetreatStart, Data.RetreatOrigin), FVector::Dist2D(Data.Destination, Data.RetreatOrigin),
			Data.CheckedPathCandidates, Data.CapturedCombatSerial, Data.Enemy->GetWorld()->GetTimeSeconds(),
			Data.Enemy->GetPoolLeaseSerial(), Data.Enemy->GetPoolGameplayGeneration(), *GetNameSafe(Data.SelectedTargetActor));
	Data.CombatYieldPath.Reset();
	return true;
}

double EstimateCombatCandidateSeconds(const FEnemyRunEnvQueryTaskInstanceData& Data,
	const FEnemyOrbitCandidate& Candidate, const FCombatReservationPathSearch& Path)
{
	double Length = 0.0;
	const TArray<FVector>& Route = Path.GetRoute();
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		Length += FVector::Distance(Route[Index - 1], Route[Index]);
	}
	const UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement();
	if (Data.CapturedCombatYieldSerial != 0)
	{
		// 양보는 공격 준비가 아니라 보호 경로를 비우는 이동이다. 이동 시간으로 가까운 안전 자리를 고른다.
		return Movement && Movement->MaxFlySpeed > 0.0f
			? Length / Movement->MaxFlySpeed : TNumericLimits<double>::Max();
	}
	const FVector Facing = (Data.SelectedTargetLocation - Candidate.Destination).GetSafeNormal();
	const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(Data.Enemy->GetActorForwardVector(), Facing), -1.0, 1.0)));
	const ARangedWeaponBase* Weapon = Data.Enemy->GetCurrentWeapon();
	return EnemyCombatNavigation::EstimateAttackSeconds(Length, Data.Enemy->GetVelocity().Size(),
		Movement ? Movement->MaxFlySpeed : 0.0, Movement ? Movement->MaxAcceleration : 0.0,
		Angle, Data.Enemy->GetRuntimeStat().AttackRotationSpeed, Data.Enemy->GetTargetAttackPreparationRemaining(),
		Data.Enemy->GetTargetAttackRecoveryRemaining(), Weapon ? Weapon->GetAttackRetryDelay() : 0.0,
		Data.bNavigationTargetVisible);
}

EStateTreeRunStatus AcceptBestCombatNavigation(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!Data.BestCombatPath || !Data.CombatCandidates.IsValidIndex(Data.BestCombatCandidate))
	{
		return EStateTreeRunStatus::Failed;
	}
	// 대상 교체는 새 대상에도 유효한 경로를 그대로 유지한다. 같은 대상일 때만 변경 이득을 비교한다.
	if (Data.CurrentCombatPath && Data.BestCombatCandidate != Data.CurrentCombatCandidate
		&& (Data.bPreserveCurrentCombatRoute
			|| (Data.CombatCandidatesProgressOnly[Data.CurrentCombatCandidate] == Data.CombatCandidatesProgressOnly[Data.BestCombatCandidate]
				&& !EnemyCombatNavigation::ShouldReplacePath(Data.CurrentCombatSeconds, Data.BestCombatSeconds,
					Data.Enemy->GetRuntimeStat().TacticalPathChangeGain))))
	{
		Data.BestCombatCandidate = Data.CurrentCombatCandidate;
		Data.BestCombatPath = Data.CurrentCombatPath;
	}
	TArray<int32> Order;
	for (int32 Index = 0; Index < Data.ValidatedCombatPaths.Num(); ++Index)
	{
		if (Data.ValidatedCombatPaths[Index])
		{
			Order.Add(Index);
		}
	}
	Order.StableSort([&Data](int32 A, int32 B)
	{
		if (A == B)
		{
			return false;
		}
		if (A == Data.BestCombatCandidate || B == Data.BestCombatCandidate)
		{
			return A == Data.BestCombatCandidate;
		}
		if (Data.CombatCandidatesProgressOnly[A] != Data.CombatCandidatesProgressOnly[B])
		{
			return !Data.CombatCandidatesProgressOnly[A];
		}
		return EstimateCombatCandidateSeconds(Data, Data.CombatCandidates[A],
			*StaticCastSharedPtr<FCombatReservationPathSearch>(Data.ValidatedCombatPaths[A]))
			< EstimateCombatCandidateSeconds(Data, Data.CombatCandidates[B],
				*StaticCastSharedPtr<FCombatReservationPathSearch>(Data.ValidatedCombatPaths[B]));
	});
	for (const int32 Index : Order)
	{
		const auto Path = StaticCastSharedPtr<FCombatReservationPathSearch>(Data.ValidatedCombatPaths[Index]);
		if (Path->GetRoute().Num() >= 2 && Data.Enemy->GetCapsuleComponent()->GetComponentLocation().Equals(Path->GetRoute()[0], 0.01))
		{
			// 출발점이 그대로면 이미 검증한 첫 구간을 사용한다. 후속 후보의 예산 소진으로 안전 경로를 버리지 않는다.
			if (TryAcceptReservedCombatCandidate(Data, Data.CombatCandidates[Index], *Path))
			{
				return EStateTreeRunStatus::Succeeded;
			}
			Data.bCombatReservationConflict = true;
			Data.ValidatedCombatPaths[Index].Reset();
			continue;
		}
		// 탐색 중 이동한 캡슐과의 연결도 다시 검사한다. 점유 충돌이면 검증된 차순위 후보를 사용한다.
		if (Data.CombatRemainingChecks < 4 || Data.CombatRemainingMilliseconds <= 0.0)
		{
			Data.Enemy->NoteCombatTacticalBudgetShortage();
			return EStateTreeRunStatus::Failed;
		}
		if (!Data.CombatJoinSearch || Data.CombatJoinCandidate != Index)
		{
			Data.CombatJoinSearch = Path->BeginRebaseRoute();
			Data.CombatJoinCandidate = Index;
		}
		bool bGranted = false;
		if (Data.CombatJoinSearch && !FlightPathfinding::AdvanceSearch(*Data.Enemy->GetWorld(), *Data.CombatJoinSearch,
			4, FMath::Min(1.0, Data.CombatRemainingMilliseconds), &bGranted))
		{
			return EStateTreeRunStatus::Running;
		}
		if (Data.CombatJoinSearch)
		{
			const FFlightPathResult Join = Data.CombatJoinSearch->GetResult();
			Data.CombatRemainingChecks -= Join.CollisionChecks;
			Data.CombatRemainingMilliseconds -= Join.ElapsedMilliseconds;
			Data.CombatJoinSearch.Reset();
			if (Path->ApplyRebasedRoute(Join) && TryAcceptReservedCombatCandidate(Data, Data.CombatCandidates[Index], *Path))
			{
				return EStateTreeRunStatus::Succeeded;
			}
		}
		Data.bCombatReservationConflict = true;
		Data.ValidatedCombatPaths[Index].Reset();
	}
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus AdvanceCombatSelection(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!IsEnvQuerySnapshotCurrent(Data) || !Data.Enemy->IsCombatReservationRequestCurrent(Data.CombatReservationRequest))
	{
		Data.QueryResult = EEnemyEnvQueryResult::Stale;
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bAcceptingRetargetRoute)
	{
		// 검증 도중 이동한 연결 구간의 공유 예산을 기다린다. 그동안 다른 목적지 탐색으로 우선권을 바꾸지 않는다.
		const EStateTreeRunStatus Accepted = AcceptBestCombatNavigation(Data);
		if (Accepted != EStateTreeRunStatus::Failed)
		{
			return Accepted;
		}
		Data.bAcceptingRetargetRoute = false;
	}
	if (Data.CombatYieldStartedAt >= 0.0)
	{
		const double Now = Data.Enemy->GetWorld()->GetTimeSeconds();
		if (Now - Data.CombatYieldStartedAt >= 1.0)
		{
			Data.QueryResult = EEnemyEnvQueryResult::ReservationConflict;
			Data.bCombatSelectionPending = false;
			Data.CombatYieldPath.Reset();
			return EStateTreeRunStatus::Failed;
		}
		if (Now >= Data.NextCombatYieldCheckAt)
		{
			Data.NextCombatYieldCheckAt = Now + 0.5;
			const TSharedPtr<FCombatReservationPathSearch> Path = StaticCastSharedPtr<FCombatReservationPathSearch>(Data.CombatYieldPath);
			if (Path && Data.CombatCandidates.IsValidIndex(Data.CombatYieldCandidate)
				&& TryAcceptReservedCombatCandidate(Data, Data.CombatCandidates[Data.CombatYieldCandidate], *Path))
			{
				return EStateTreeRunStatus::Succeeded;
			}
		}
		return EStateTreeRunStatus::Running;
	}
	const FName Intent = GetCombatSelectionIntent(Data);
	while (Data.NextCombatCandidate < Data.CombatCandidates.Num())
	{
		const int32 Index = Data.NextCombatCandidate;
		const FEnemyOrbitCandidate& Candidate = Data.CombatCandidates[Index];
		if (Data.CombatCandidatesHeightLimited[Index])
		{
			Data.bCombatHadLimitedCandidate = true;
			++Data.NextCombatCandidate;
			continue;
		}
		if (!Data.CombatPathSearch && Data.Enemy->WasCombatTacticalCandidateChecked(Intent, Candidate.Destination))
		{
			Data.bCombatHadLimitedCandidate |= !Data.Enemy->IsCombatTacticalCandidateFailure(Intent, Candidate.Destination);
			++Data.NextCombatCandidate;
			continue;
		}
		if (!Data.CombatPathSearch && Data.Enemy->IsCombatTacticalCandidateFailure(Intent, Candidate.Destination))
		{
			++Data.NextCombatCandidate;
			continue;
		}
		if (!Data.CombatPathSearch)
		{
			if (Data.CheckedPathCandidates >= Data.MaxPathCandidates)
			{
				Data.QueryResult = EEnemyEnvQueryResult::CandidateLimitReached;
				break;
			}
			if (!HasCandidatePathBudget(Data.CombatRemainingNodes, Data.CombatRemainingChecks, Data.CombatRemainingMilliseconds))
			{
				Data.QueryResult = EEnemyEnvQueryResult::PathBudgetExceeded;
				break;
			}
			TArray<FVector> Waypoints;
			if (Data.bUnifiedCombatNavigation && Index == Data.CurrentCombatCandidate)
			{
				Waypoints = Data.CurrentCombatRoute;
			}
			else if (Data.bValidateOrbitPath && !Data.bUnifiedCombatNavigation)
			{
				const double SpatialLength = FMath::Sqrt(FMath::Square(double(Candidate.ArcLength))
					+ FMath::Square(Candidate.Destination.Z - Data.CapturedOrbitStart.Z));
				if (SpatialLength > 500.01) { Data.bCombatHadLimitedCandidate = true; ++Data.NextCombatCandidate; continue; }
				EnemyOrbit::BuildWaypoints(Data.CapturedOrbitStart, Data.CapturedOrbitCenter, Candidate, Waypoints);
			}
			Data.CombatPathSearch = MakeShared<FCombatReservationPathSearch>(Data, Candidate, Waypoints);
			++Data.CheckedPathCandidates;
		}
		bool bGranted = false;
		if (!FlightPathfinding::AdvanceSearch(*Data.Enemy->GetWorld(), *Data.CombatPathSearch,
			FMath::Min(256, Data.CombatRemainingChecks), FMath::Min(1.0, Data.CombatRemainingMilliseconds), &bGranted))
		{
			if (!bGranted)
			{
				Data.Enemy->NoteCombatTacticalBudgetShortage();
			}
			return EStateTreeRunStatus::Running;
		}
		const FFlightPathResult Path = Data.CombatPathSearch->GetResult();
		if (Data.bValidateRetreatPath && CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyRetreatCandidate] Enemy=%s Candidate=%d Short=%d Goal=%s Distance2D=%.1f PassedQuery=%d Path=%s Nodes=%d Checks=%d Milliseconds=%.3f"),
				*GetNameSafe(Data.Enemy), Index, FVector::Dist2D(Candidate.Destination, Data.CapturedRetreatStart) <= 300.01,
				*Candidate.Destination.ToCompactString(), FVector::Dist2D(Candidate.Destination, Data.RetreatOrigin),
				Data.CombatCandidatesPassedQuery[Index], *UEnum::GetValueAsString(Path.Status),
				Path.SearchNodes, Path.CollisionChecks, Path.ElapsedMilliseconds);
		const FBox Bounds = Data.CombatPathSearch->GetRequest().SearchBounds;
		const TSharedPtr<FCombatReservationPathSearch> ReservedPath = StaticCastSharedPtr<FCombatReservationPathSearch>(Data.CombatPathSearch);
		Data.CombatPathSearch.Reset();
		Data.SelectionPathStatus = Path.Status;
		Data.CombatRemainingNodes -= Path.SearchNodes;
		Data.CombatRemainingChecks -= Path.CollisionChecks;
		Data.CombatRemainingMilliseconds -= Path.ElapsedMilliseconds;
		if (Path.bStartBlocked)
		{
			// 출발점의 관통은 목적지 후보 전체가 막혔다는 증거가 아니다.
			Data.QueryResult = EEnemyEnvQueryResult::Failed;
			break;
		}
		if (Path.Status == EFlightPathStatus::Success)
		{
			if (Data.bUnifiedCombatNavigation)
			{
				Data.ValidatedCombatPaths[Index] = ReservedPath;
				const double Seconds = EstimateCombatCandidateSeconds(Data, Candidate, *ReservedPath);
				const double Error = Data.CapturedCombatYieldSerial != 0
					? FVector::Distance(Candidate.Destination, Data.CapturedOrbitStart)
					: Data.bNavigationTargetVisible
					? FMath::Abs(FVector::Distance(Candidate.Destination, Data.SelectedTargetLocation)
						- FVector::Distance(Data.CapturedOrbitStart, Data.SelectedTargetLocation))
					: FVector::Distance(Candidate.Destination, Data.SelectedTargetLocation);
				const bool bProgress = Data.CombatCandidatesProgressOnly[Index];
				const bool bBestProgress = Data.CombatCandidatesProgressOnly.IsValidIndex(Data.BestCombatCandidate)
					&& Data.CombatCandidatesProgressOnly[Data.BestCombatCandidate];
				const double Crowding = Data.CachedReservationRooms && Data.CapturedCombatYieldSerial == 0
					? Data.CachedReservationRooms->GetCombatObservationCrowding(Data.Enemy, Data.SelectedTargetLocation, Candidate.Destination)
					: 0.0;
				const double TimeTie = Data.Enemy->GetRuntimeStat().TacticalAttackTimeTie;
				const bool bComparable = FMath::Abs(Seconds - Data.BestCombatSeconds) <= TimeTie
					&& (Data.bNavigationTargetVisible || FMath::Abs(Error - Data.BestCombatDistanceError)
						<= Data.Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2.0);
				const bool bPreferCandidate = bComparable && !FMath::IsNearlyEqual(Crowding, Data.BestCombatCrowding)
					? Crowding < Data.BestCombatCrowding
					: (Data.bNavigationTargetVisible
						? EnemyCombatNavigation::IsBetterCandidate(Seconds, Error, Data.BestCombatSeconds,
							Data.BestCombatDistanceError, TimeTie)
						: Error < Data.BestCombatDistanceError);
				if (Index == Data.CurrentCombatCandidate)
				{
					Data.CurrentCombatPath = ReservedPath;
					Data.CurrentCombatSeconds = Seconds;
				}
				if (!Data.BestCombatPath || (!bProgress && bBestProgress)
					|| (bProgress == bBestProgress && bPreferCandidate))
				{
					Data.BestCombatPath = ReservedPath;
					Data.BestCombatCandidate = Index;
					Data.BestCombatSeconds = Seconds;
					Data.BestCombatDistanceError = Error;
					Data.BestCombatCrowding = Crowding;
				}
				++Data.NextCombatCandidate;
				if (Data.bPreserveCurrentCombatRoute && Index == Data.CurrentCombatCandidate)
				{
					// 새 대상의 사선·경로가 확인되면 바로 예약을 인계한다. 더 빠른 새 후보 때문에 왕복하지 않는다.
					const EStateTreeRunStatus Accepted = AcceptBestCombatNavigation(Data);
					if (Accepted != EStateTreeRunStatus::Failed)
					{
						Data.bAcceptingRetargetRoute = Accepted == EStateTreeRunStatus::Running;
						return Accepted;
					}
				}
				continue;
			}
			if (!Data.CombatCandidatesPassedQuery[Index]) { Data.QueryResult = EEnemyEnvQueryResult::Failed; break; }
			if (!TryAcceptReservedCombatCandidate(Data, Candidate, *ReservedPath))
			{
				// 다른 개체의 예약은 장애물 실패가 아니다. 후보 실패 캐시/예산 복구 권한을 만들지 않는다.
				Data.bCombatReservationConflict = true;
				if (!Data.CombatYieldPath)
				{
					Data.CombatYieldPath = ReservedPath;
					Data.CombatYieldCandidate = Index;
				}
				++Data.NextCombatCandidate;
				continue;
			}
			return EStateTreeRunStatus::Succeeded;
		}
		Data.Enemy->MarkCombatTacticalCandidateChecked(Intent, Candidate.Destination);
		if (IsPhysicalRetreatFailure(Path.Status))
		{
			Data.Enemy->RecordCombatTacticalCandidateFailure(Intent, Candidate.Destination, Bounds);
		}
		else if (Path.Status != EFlightPathStatus::BudgetExceeded)
		{
			Data.QueryResult = Path.Status == EFlightPathStatus::Cancelled ? EEnemyEnvQueryResult::Stale : EEnemyEnvQueryResult::Failed;
			break;
		}
		Data.bCombatHadLimitedCandidate |= Path.Status == EFlightPathStatus::BudgetExceeded;
		++Data.NextCombatCandidate;
	}
	if (Data.bUnifiedCombatNavigation)
	{
		const EStateTreeRunStatus Accepted = AcceptBestCombatNavigation(Data);
		if (Accepted != EStateTreeRunStatus::Failed)
		{
			return Accepted;
		}
		if (Data.bCombatReservationConflict)
		{
			Data.QueryResult = EEnemyEnvQueryResult::ReservationConflict;
		}
	}
	if (Data.CombatYieldPath && (Data.QueryResult == EEnemyEnvQueryResult::Running
		|| Data.QueryResult == EEnemyEnvQueryResult::CandidateLimitReached))
	{
		// 후보가 모두 경쟁 중이면 새 EQS 없이 최대 1초만 양보한다. 기존 회차의 3회/3초 상한은 계속 흐른다.
		Data.CombatYieldStartedAt = Data.Enemy->GetWorld()->GetTimeSeconds();
		Data.NextCombatYieldCheckAt = Data.CombatYieldStartedAt + 0.5;
		Data.QueryResult = EEnemyEnvQueryResult::Running;
		return EStateTreeRunStatus::Running;
	}
	Data.bCombatSelectionPending = false;
	if (Data.QueryResult == EEnemyEnvQueryResult::Running)
	{
		Data.QueryResult = Data.bCombatHadLimitedCandidate ? EEnemyEnvQueryResult::PathBudgetExceeded : EEnemyEnvQueryResult::NoReachableDestination;
		Data.bNoOrbitDestination = Data.bValidateOrbitPath && !Data.bCombatHadLimitedCandidate;
		Data.bNoRetreatDestination = Data.bValidateRetreatPath && !Data.bCombatHadLimitedCandidate;
	}
	if (Data.QueryResult == EEnemyEnvQueryResult::PathBudgetExceeded)
	{
		Data.Enemy->NoteCombatTacticalBudgetShortage();
	}
	if (Data.bUnifiedCombatNavigation && Data.CapturedCombatYieldSerial == 0 && Data.bNavigationTargetVisible
		&& Data.QueryResult == EEnemyEnvQueryResult::NoReachableDestination
		&& !Data.bCombatHadLimitedCandidate && !Data.bCombatReservationConflict
		&& !Data.BestCombatPath && !Data.CombatYieldPath && !Data.CombatJoinSearch
		&& !Data.CombatCandidates.IsEmpty() && Data.NextCombatCandidate == Data.CombatCandidates.Num()
		&& IsEnvQuerySnapshotCurrent(Data)
		&& Data.Enemy->IsCombatReservationRequestCurrent(Data.CombatReservationRequest))
	{
		// 회차/요청 상한이나 빈 결과를 불가능의 증거로 쓰지 않는다. 공격 후보와 안전 경유점을
		// 모두 검사했고 각 후보에 현재 조건의 물리 실패가 남은 경우에만 시도 사격 자격을 전달한다.
		const bool bAllPhysicallyRejected = !Data.CombatCandidates.ContainsByPredicate([&](const auto& Candidate)
		{
			return !Data.Enemy->IsCombatTacticalCandidateFailure(Intent, Candidate.Destination);
		});
		if (bAllPhysicallyRejected)
		{
			Data.Enemy->NoteTargetAttackAlternativesExhausted();
		}
	}
	if (Data.CapturedCombatYieldSerial != 0 && Data.CachedReservationRooms && IsValid(Data.Enemy))
	{
		// 실패한 양보 요청만 닫는다. 늦은 결과의 이전 번호로 새 요청까지 지우지 않는다.
		Data.CachedReservationRooms->FinishCombatYield(Data.Enemy, Data.CapturedCombatYieldSerial);
	}
	return EStateTreeRunStatus::Failed;
}

void ApplyEnvQueryResult(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	Data.RequestId = INDEX_NONE;
	Data.bHasDestination = false;
	Data.QueryResult = EEnemyEnvQueryResult::Failed;
	// 요청 당시 상태 유지 -> 시간 제한 -> 결과 형식 -> 목적지 안전 검사 후에만 출력을 기록한다.
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
		LogEnvQuerySnapshotFailure(Data, Result.QueryID);
		Data.QueryResult = EEnemyEnvQueryResult::Stale;
		return;
	}
	if (HasEnvQueryTimedOut(Data))
	{
		Data.QueryResult = EEnemyEnvQueryResult::TimedOut;
		return;
	}
	if (Data.CapturedCombatSerial)
	{
		if (IsLocationEnvQueryResult(Result) || (IsEmptyEnvQueryResult(Result)
			&& (Data.bValidateOrbitPath || Data.bValidateRetreatPath)))
		{
			BeginCombatSelection(Data, Result);
		}
		else
		{
			Data.QueryResult = IsEmptyEnvQueryResult(Result) ? EEnemyEnvQueryResult::NoResults : EEnemyEnvQueryResult::Failed;
		}
		return;
	}
	if (IsEmptyEnvQueryResult(Result))
	{
		Data.QueryResult = EEnemyEnvQueryResult::NoResults;
		if (Data.bValidateRetreatPath)
		{
			Data.QueryResult = EEnemyEnvQueryResult::Failed;
			SelectRetreatDestination(Data, Result);
		}
		if (Data.bValidateOrbitPath && CanClassifyEmptyOrbitResult(Data))
		{
			// 빈 결과만으로는 후보 전체 탈락과 설정 오류를 구분할 수 없다. 곡선을 다시 검증한다.
			Data.QueryResult = EEnemyEnvQueryResult::Failed;
			SelectOrbitDestination(Data, Result);
		}
		return;
	}
	if (!IsLocationEnvQueryResult(Result))
	{
		return;
	}
	if (Data.SearchMode != EEnemySearchQueryMode::None)
	{
		if (!SelectSearchDestinations(Data, Result))
		{
			return;
		}
	}
	else if (Data.bValidateRetreatPath)
	{
		if (!SelectRetreatDestination(Data, Result))
		{
			return;
		}
	}
	else if (Data.bValidateOrbitPath)
	{
		if (!CanClassifyEmptyOrbitResult(Data))
		{
			return;
		}
		if (!SelectOrbitDestination(Data, Result))
		{
			return;
		}
	}
	else if (Data.bValidateReachability || Data.bValidateChasePath)
	{
		if (!SelectReachableDestination(Data, Result))
		{
			return;
		}
	}
	else
	{
		// 전투 전용 검증을 사용하지 않는 기존 질의는 첫 결과의 공간 검사만 유지한다.
		const FVector Destination = Result.GetItemAsLocation(0);
		if (!IsEnvQueryDestinationClear(Data, Destination))
		{
			return;
		}
		Data.Destination = Destination;
	}
	// 마지막 탐색에서 시간 제한을 넘었으면 성공 좌표도 후속 Move에 넘기지 않는다.
	if (HasEnvQueryTimedOut(Data))
	{
		Data.QueryResult = EEnemyEnvQueryResult::TimedOut;
		return;
	}
	if (Data.SearchMode == EEnemySearchQueryMode::Points)
	{
		// 비동기 질의 사이 다른 드론이 예약했을 수 있으므로, 실제 예약 시 다시 충돌을 확인한다.
		// 예약과 회차 저장이 모두 성공해야 Move에 목적지를 공개한다.
		UEnemyRoomSubsystem* Rooms = Data.Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
		const float Separation = FMath::Max(Data.SearchPointSeparation,
			2.0f * (Data.Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + Data.Clearance));
		if (!Rooms || !Rooms->ReserveSearchDestinations(Data.Enemy, Data.SelectedSearchPoints, Separation))
		{
			return;
		}
		if (!Data.Enemy->SetSearchPoints(Data.SelectedSearchPoints))
		{
			Rooms->ReleaseSearchDestinations(Data.Enemy);
			return;
		}
	}
	Data.bHasDestination = true;
	Data.QueryResult = EEnemyEnvQueryResult::Success;
	if (Data.bValidateOrbitPath)
	{
		Data.Enemy->RememberCombatOrbitDirection(Data.SelectedTargetActor, Data.bSelectedOrbitLeft);
	}
}

bool CanAcceptEnvQueryResult(const FEnemyRunEnvQueryTaskInstanceData& Data, int32 QueryId)
{
	return Data.bActive && Data.QueryResult == EEnemyEnvQueryResult::Running
		&& (Data.CombatReservationRequest == 0 || (IsValid(Data.Enemy)
			&& Data.Enemy->IsCombatReservationRequestCurrent(Data.CombatReservationRequest)))
		&& (!Data.CapturedCombatSerial || (IsValid(Data.Enemy)
			&& Data.Enemy->IsCombatTacticalEpisodeCurrent(Data.CapturedCombatSerial)))
		&& Data.RequestId != INDEX_NONE && Data.RequestId == QueryId;
}

void CancelEnvQuery(FEnemyRunEnvQueryTaskInstanceData& Data, EEnemyEnvQueryResult Reason)
{
	if (Data.CapturedCombatYieldSerial != 0 && Data.CachedReservationRooms && IsValid(Data.Enemy))
	{
		// 실패한 양보 요청만 닫는다. 늦은 결과의 이전 번호로 새 요청까지 지우지 않는다.
		Data.CachedReservationRooms->FinishCombatYield(Data.Enemy, Data.CapturedCombatYieldSerial);
	}
	const int32 RequestId = Data.RequestId;
	// AbortQuery에서도 완료 Delegate가 호출될 수 있다. 먼저 번호/활성 상태를 내려 재진입 결과를 차단한다.
	Data.RequestId = INDEX_NONE;
	Data.bActive = false;
	if (Data.CombatReservationRequest != 0 && IsValid(Data.Enemy))
	{
		Data.Enemy->ReleaseCombatReservation(Data.CombatReservationRequest);
	}
	Data.CombatYieldPath.Reset();
	Data.bHasDestination = false;
	Data.QueryResult = Reason;
	Data.SelectedWaypoints.Reset();
	Data.DeferredSearchResult.Reset();
	Data.SearchCandidateOrder.Reset();
	Data.bSearchSelectionPending = false;
	Data.CombatPathSearch.Reset();
	Data.CombatJoinSearch.Reset();
	Data.BestCombatPath.Reset();
	Data.CurrentCombatPath.Reset();
	Data.ValidatedCombatPaths.Reset();
	Data.CurrentCombatRoute.Reset();
	Data.CombatCandidatesProgressOnly.Reset();
	Data.bCombatSelectionPending = false;
	Data.CombatCandidates.Reset();
	Data.CombatCandidatesPassedQuery.Reset();
	Data.CombatCandidatesHeightLimited.Reset();
	Data.SelectedArcLength = 0.0f;
	Data.bNoOrbitDestination = false;
	Data.bNoRetreatDestination = false;
	Data.HeightSnapshot.Candidates.Reset();
	if (RequestId != INDEX_NONE && IsValid(Data.Enemy))
	{
		if (UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Data.Enemy))
		{
			Manager->AbortQuery(RequestId);
		}
	}
	LogEnvQuery(TEXT("Cancel"), Data, 0, RequestId);
}

void BeginCombatObservation(FEnemyRunEnvQueryTaskInstanceData& Data, bool bBudgetShortage)
{
	// 먼저 Pawn 회차를 닫고 요청을 지운다. Abort의 동기 완료 콜백도 옛 결과를 적용하지 못한다.
	if (bBudgetShortage)
	{
		Data.Enemy->NoteCombatTacticalBudgetShortage();
	}
	Data.Enemy->EnterCombatObservationWait(bBudgetShortage);
	CancelEnvQuery(Data, EEnemyEnvQueryResult::Running);
	Data.CombatReservationRequest = 0;
	Data.CapturedCombatSerial = Data.Enemy->GetCombatTacticalEpisodeSerial();
	Data.bActive = true;
	Data.bCombatObservationWaiting = true;
}

EStateTreeRunStatus AdvanceCombatObservation(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
		return EStateTreeRunStatus::Failed;
	}
	if (!Data.Enemy->CanResumeCombatTacticalBudget())
	{
		return EStateTreeRunStatus::Running;
	}
	if (!Data.CombatPathSearch)
	{
		// 복구 대기는 종료된 EQS 결과를 재사용하지 않는다. 새 회차 번호의 별도 검사만 대기열에 둔다.
		Data.CombatRemainingNodes = Data.MaxSearchNodes;
		Data.CombatRemainingChecks = Data.MaxCollisionChecks;
		Data.CombatRemainingMilliseconds = Data.MaxSearchMilliseconds;
		const FVector Location = Data.Enemy->GetActorLocation();
		FEnemyOrbitCandidate Probe{Location, 0.0f, false};
		Data.CombatPathSearch = MakeShared<FCombatCandidatePathSearch>(Data, Probe, TArray<FVector>{Location, Location});
	}
	bool bGranted = false;
	FlightPathfinding::AdvanceSearch(*Data.Enemy->GetWorld(), *Data.CombatPathSearch,
		FMath::Min(256, Data.MaxCollisionChecks), FMath::Min(1.0, double(Data.MaxSearchMilliseconds)), &bGranted);
	if (!Data.Enemy->TryResumeCombatTacticalBudget(bGranted))
	{
		return EStateTreeRunStatus::Running;
	}
	// 실제 배정은 복구 구간을 여는 사건이다. Probe 결과를 목적지로 쓰지 않고 새 EQS부터 시작한다.
	Data.CombatPathSearch.Reset();
	Data.CapturedCombatSerial = Data.Enemy->GetCombatTacticalEpisodeSerial();
	Data.bCombatObservationWaiting = false;
	Data.CombatReservationRequest = Data.Enemy->BeginCombatReservationRequest();
	Data.QueryResult = EEnemyEnvQueryResult::Waiting;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus TickCombatSelection(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 이전 대상/풀 수명의 Task는 새 회차의 예산과 관찰 상태를 변경할 수 없다.
	// 관찰 진입은 같은 소유자의 요청과 회차 번호를 의도적으로 만료시키므로 여기서는 소유권만 검사한다.
	if (!IsEnvQueryEnemyCurrent(Data) || !IsEnvQueryControllerCurrent(Data)
		|| !IsEnvQueryRoomCurrent(Data) || !IsEnvQueryTargetCurrent(Data))
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::Stale);
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bCombatObservationWaiting)
	{
		return AdvanceCombatObservation(Data);
	}
	if (!Data.Enemy->CanContinueCombatTacticalRequest())
	{
		BeginCombatObservation(Data, false);
		return EStateTreeRunStatus::Running;
	}
	// 관찰 전환을 처리한 뒤에는 기존 요청/회차까지 일치해야 후보 선택을 계속할 수 있다.
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::Stale);
		return EStateTreeRunStatus::Failed;
	}
	if ((Data.QueryResult == EEnemyEnvQueryResult::Running || Data.bCombatSelectionPending) && HasEnvQueryTimedOut(Data))
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::TimedOut);
		return EStateTreeRunStatus::Failed;
	}
	return Data.bCombatSelectionPending ? AdvanceCombatSelection(Data) : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus ResumeSearchSelection(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 대기 중 새 정보/시야/풀 수명이 바뀌면 저장 결과를 버린다. 이전 회차의 목적지는 공개하지 않는다.
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::Stale);
		return EStateTreeRunStatus::Failed;
	}
	if (HasEnvQueryTimedOut(Data))
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::TimedOut);
		return EStateTreeRunStatus::Failed;
	}
	if (Data.Enemy->GetWorld()->GetTimeSeconds() < Data.NextSearchSelectionTime)
	{
		return EStateTreeRunStatus::Running;
	}
	const TSharedPtr<FEnvQueryResult> Result = Data.DeferredSearchResult;
	if (!Result)
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::Failed);
		return EStateTreeRunStatus::Failed;
	}
	// EQS는 재실행하지 않는다. 간격을 둔 새 예산으로 미검증 후보만 확인한다.
	LogEnvQuery(TEXT("SearchSelectionResume"), Data, Result->Items.Num(), Result->QueryID);
	ApplyEnvQueryResult(Data, *Result);
	if (Data.bSearchSelectionPending)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.DeferredSearchResult.Reset();
	LogEnvQuery(TEXT("Finished"), Data, Result->Items.Num(), Result->QueryID);
	return Data.bHasDestination ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

bool PrepareEnvQueryRequest(FEnemyRunEnvQueryTaskInstanceData& Data, FEnvQueryRequest& Request)
{
	// 보정 후보 생성 -> 공유 예산 기록 -> NamedParam 저장 순서로 준비한다.
	// 실행 방식과 분리해도 실제 Task와 통합 테스트가 같은 후보/좌표를 사용한다.
	if (Data.bRequireVisibleTarget && Data.SearchMode == EEnemySearchQueryMode::None && Data.HeightSnapshot.bEnabled)
	{
		const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
		if (Options.Num() != 1 || !Options[0] || !Options[0]->Generator)
		{
			Data.QueryResult = EEnemyEnvQueryResult::Invalid;
			return false;
		}
		const EFlightPathStatus HeightStatus = EnemyFlightHeight::PrepareQuery(*Data.Enemy, *Options[0]->Generator,
			Data.CapturedOrbitStart, Data.CapturedOrbitCenter, Data.CapturedOrbitLength,
			Data.bCapturedOrbitLeft, Data.CapturedChaseLane, Data.Clearance, Data.MaxCollisionChecks,
			Data.MaxSearchMilliseconds, Data.HeightSnapshot);
		Data.SelectionPathStatus = HeightStatus;
		if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyFlightHeightQuery] Enemy=%s Status=%s Candidates=%d Checks=%d Ms=%.3f"),
				*GetNameSafe(Data.Enemy), *UEnum::GetValueAsString(HeightStatus), Data.HeightSnapshot.Candidates.Num(),
				Data.HeightSnapshot.CollisionChecks, Data.HeightSnapshot.Milliseconds);
		}
		if (HeightStatus != EFlightPathStatus::Success)
		{
			Data.QueryResult = HeightStatus == EFlightPathStatus::BudgetExceeded
				? EEnemyEnvQueryResult::PathBudgetExceeded : EEnemyEnvQueryResult::Invalid;
			return false;
		}
	}
	for (FAIDynamicParam& Param : Data.QueryConfig)
	{
		Request.SetDynamicParam(Param, nullptr);
	}
	if (Data.bRequireVisibleTarget && Data.SearchMode == EEnemySearchQueryMode::None)
	{
		EnemyFlightHeight::SetQuerySnapshot(Request, Data.HeightSnapshot);
	}
	if (Data.bValidateOrbitPath)
	{
		EnemyOrbit::SetQuerySnapshot(Request, Data.CapturedOrbitStart, Data.CapturedOrbitCenter,
			Data.CapturedOrbitLength, Data.bCapturedOrbitLeft);
	}
	if (Data.bValidateRetreatPath)
	{
		EnemyRetreat::SetQuerySnapshot(Request, Data.CapturedRetreatStart, Data.RetreatOrigin);
	}
	if (Data.bValidateChasePath)
	{
		EnemyChase::SetQuerySnapshot(Request, Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane);
	}
	if (Data.SearchMode != EEnemySearchQueryMode::None)
	{
		EnemySearch::SetQuerySnapshot(Request, Data.CapturedOrbitCenter, Data.CapturedSearchHeight);
	}
	return true;
}

EStateTreeRunStatus StartEnvQuery(FStateTreeExecutionContext& Context,
	FEnemyRunEnvQueryTaskInstanceData& Data)
{
	const double Now = Data.Enemy->GetWorld()->GetTimeSeconds();
	if (Now < Data.Enemy->GetNextEnvQueryRequestTime())
	{
		if (Data.QueryResult != EEnemyEnvQueryResult::Waiting)
		{
			LogEnvQuery(TEXT("WaitingForRequestInterval"), Data);
		}
		Data.QueryResult = EEnemyEnvQueryResult::Waiting;
		return EStateTreeRunStatus::Running;
	}
	if (Data.CapturedCombatSerial && !Data.Enemy->TryBeginCombatTacticalRequest())
	{
		BeginCombatObservation(Data, false);
		return EStateTreeRunStatus::Running;
	}
	// 간격은 Pawn에 보관해 실패 후 같은 State로 재진입해도 요청이 한 프레임에 반복되지 않게 한다.
	Data.Enemy->SetNextEnvQueryRequestTime(Now + Data.QueryInterval);
	Data.QueryStartedAt = Now;
	if (Data.CapturedCombatSerial != 0 && Data.CombatReservationRequest == 0)
	{
		Data.CombatReservationRequest = Data.Enemy->BeginCombatReservationRequest();
	}
	Data.QueryResult = EEnemyEnvQueryResult::Running;
	if (Data.bUnifiedCombatNavigation)
	{
		BeginUnifiedCombatSelection(Data);
		return EStateTreeRunStatus::Running;
	}
	FEnvQueryRequest Request(Data.QueryTemplate, Data.Enemy);
	if (!PrepareEnvQueryRequest(Data, Request))
	{
		if (!Data.CapturedCombatSerial)
		{
			return EStateTreeRunStatus::Failed;
		}
		BeginCombatObservation(Data, Data.QueryResult == EEnemyEnvQueryResult::PathBudgetExceeded);
		return EStateTreeRunStatus::Running;
	}
	const FQueryFinishedSignature Finished = FQueryFinishedSignature::CreateLambda([WeakContext = Context.MakeWeakExecutionContext()
#if WITH_DEV_AUTOMATION_TESTS
		, QueryWorld = TWeakObjectPtr<UWorld>(Data.Enemy->GetWorld())
#endif
		]
			(TSharedPtr<FEnvQueryResult> Result) mutable
		{
#if WITH_DEV_AUTOMATION_TESTS
			// 무효화된 결과도 완료 횟수에는 포함한다. 다른 월드의 요청은 집계하지 않는다.
			FlightPathfinding::RecordTestQuery(QueryWorld.Get(), true);
#endif
			const FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();
			FEnemyRunEnvQueryTaskInstanceData* Pending =
				StrongContext.GetInstanceDataPtr<FEnemyRunEnvQueryTaskInstanceData>();
			if (!Pending || !Result || !CanAcceptEnvQueryResult(*Pending, Result->QueryID))
			{
				return;
			}
			// EQS Manager는 완료 Delegate에 실제 FEnvQueryInstance를 전달한다.
			LogEnvQueryRejections(*StaticCastSharedPtr<FEnvQueryInstance>(Result), *Pending);
			ApplyEnvQueryResult(*Pending, *Result);
			if (Pending->bCombatSelectionPending)
			{
				return;
			}
			if (Pending->CapturedCombatSerial && Pending->QueryResult == EEnemyEnvQueryResult::Failed)
			{
				BeginCombatObservation(*Pending, false);
				return;
			}
			if (Pending->bSearchSelectionPending)
			{
				Pending->DeferredSearchResult = Result;
				return;
			}
			const bool bSuccess = Pending->bHasDestination;
			LogEnvQuery(TEXT("Finished"), *Pending, Result->Items.Num(), Result->QueryID);
			StrongContext.FinishTask(bSuccess ? EStateTreeFinishTaskType::Succeeded : EStateTreeFinishTaskType::Failed);
		});
	Data.RequestId = ExecuteEnvQueryWithDiagnostics(Request, Data.RunMode, Finished, *Data.Enemy->GetWorld());
#if WITH_DEV_AUTOMATION_TESTS
	if (Data.RequestId != INDEX_NONE)
	{
		FlightPathfinding::RecordTestQuery(Data.Enemy->GetWorld(), false);
	}
#endif
	if (Data.RequestId == INDEX_NONE)
	{
		Data.QueryResult = EEnemyEnvQueryResult::Failed;
		LogEnvQuery(TEXT("RequestFailed"), Data);
		return EStateTreeRunStatus::Failed;
	}
	LogEnvQuery(TEXT("Request"), Data);
	return EStateTreeRunStatus::Running;
}

}

FEnemyRunEnvQueryTask::FEnemyRunEnvQueryTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = true;
}

#if WITH_EDITOR
void FEnemyRunEnvQueryTask::PostEditInstanceDataChangeChainProperty(
	const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FInstanceDataType, QueryTemplate))
	{
		FInstanceDataType& Data = InstanceDataView.GetMutable<FInstanceDataType>();
		Data.QueryConfig.Reset();
		if (Data.QueryTemplate)
		{
			Data.QueryTemplate->CollectQueryParams(*Data.QueryTemplate, Data.QueryConfig);
			for (FAIDynamicParam& Param : Data.QueryConfig)
			{
				Param.bAllowBBKey = false;
			}
		}
	}
}
#endif

EStateTreeRunStatus FEnemyRunEnvQueryTask::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.bHasDestination = false;
	Data.SelectedSearchPoints.Reset();
	Data.bSearchSelectionLimited = false;
	Data.DeferredSearchResult.Reset();
	Data.SearchCandidateOrder.Reset();
	Data.NextSearchCandidate = 0;
	Data.SearchSelectionRetries = 0;
	Data.bSearchPathBudgetExceeded = false;
	Data.NextSearchSelectionTime = 0.0;
	Data.bSearchSelectionPending = false;
	Data.CapturedCombatSerial = 0;
	Data.CombatReservationRequest = 0;
	Data.bCombatReservationConflict = false;
	Data.CombatYieldStartedAt = -1.0;
	Data.CombatYieldPath.Reset();
	Data.bCapturedForSightReposition = false;
	Data.CapturedCombatYieldSerial = 0;
	Data.CapturedCombatYieldStart = FVector::ZeroVector;
	Data.CapturedCombatYieldEnd = FVector::ZeroVector;
	Data.bUnifiedCombatNavigation = false;
	Data.bCombatSelectionPending = false;
	Data.bCombatObservationWaiting = false;
	Data.CombatPathSearch.Reset();
	Data.CombatCandidates.Reset();
	Data.CombatCandidatesPassedQuery.Reset();
	Data.CombatCandidatesHeightLimited.Reset();
	Data.SelectedWaypoints.Reset();
	Data.SelectedArcLength = 0.0f;
	Data.bSelectedOrbitLeft = false;
	Data.bNoOrbitDestination = false;
	Data.bNoRetreatDestination = false;
	Data.RetreatOrigin = FVector::ZeroVector;
	Data.RetreatDirection = FVector::ZeroVector;
	Data.QueryResult = EEnemyEnvQueryResult::Invalid;
	Data.CheckedPathCandidates = 0;
	Data.SelectionPathStatus = EFlightPathStatus::Unavailable;
	Data.SelectedTargetLocation = FVector::ZeroVector;
	Data.SelectedTargetActor = nullptr;
	Data.RequestId = INDEX_NONE;
	Data.bActive = true;
	AEnemyBase* Enemy = Data.Enemy;
	Data.Destination = IsValid(Enemy) ? Enemy->GetActorLocation() : FVector::ZeroVector;
#if WITH_DEV_AUTOMATION_TESTS
	if (IsValid(Enemy) && Enemy->GetWorld())
	{
		Data.MaxCollisionChecks = FlightPathfinding::GetTestBudget(*Enemy->GetWorld(), Data.MaxCollisionChecks);
	}
#endif
	// 실행 환경/설정 검증 -> 요청 당시 상태 저장 -> 현재 상태 검증 -> 비동기 요청.
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !Enemy->GetWorld())
	{
		Data.bActive = false;
		LogEnvQuery(TEXT("Rejected"), Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!IsEnvQueryConfigurationValid(Data) || !HasValidEnvQueryParameters(Data))
	{
		CaptureEnvQuerySnapshot(Data);
		if (Data.CapturedCombatSerial)
		{
			BeginCombatObservation(Data, false);
			return EStateTreeRunStatus::Running;
		}
		Data.bActive = false;
		LogEnvQuery(TEXT("RejectedParameters"), Data);
		return EStateTreeRunStatus::Failed;
	}
	CaptureEnvQuerySnapshot(Data);
	if (!Data.bUnifiedCombatNavigation && Data.bValidateRetreatPath && (Data.RetreatDirection.IsNearlyZero()
		|| FVector::Dist2D(Data.CapturedRetreatStart, Data.RetreatOrigin) > 1000.0))
	{
		Data.bActive = false;
		LogEnvQuery(TEXT("RejectedRetreatRadiusOrDirection"), Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!Data.bUnifiedCombatNavigation && Data.bValidateOrbitPath && !EnemyOrbit::IsCombatRadius(Data.CapturedOrbitStart, Data.CapturedOrbitCenter))
	{
		Data.bActive = false;
		LogEnvQuery(TEXT("RejectedOrbitRadius"), Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
		LogEnvQuerySnapshotFailure(Data);
		Data.bActive = false;
		LogEnvQuery(TEXT("RejectedSnapshot"), Data);
		return EStateTreeRunStatus::Failed;
	}
	return StartEnvQuery(Context, Data);
}

EStateTreeRunStatus FEnemyRunEnvQueryTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	// Move에서 넘긴 기존 안전 구간만 유지한다. Query의 실패/관찰 대기는 아래 경계에서 즉시 정리한다.
	if (IsValid(Data.Enemy))
	{
		Data.Enemy->TickCombatFlightHandoff(DeltaTime);
	}
	if (Data.CapturedCombatSerial)
	{
		const EStateTreeRunStatus Status = TickCombatSelection(Data);
		if (Status != EStateTreeRunStatus::Running || Data.bCombatObservationWaiting || Data.bCombatSelectionPending)
		{
			return Status;
		}
	}
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
		LogEnvQuerySnapshotFailure(Data);
		CancelEnvQuery(Data, EEnemyEnvQueryResult::Stale);
		return EStateTreeRunStatus::Failed;
	}
	if (Data.QueryResult == EEnemyEnvQueryResult::Success)
	{
		return EStateTreeRunStatus::Succeeded;
	}
	if (Data.bCombatSelectionPending)
	{
		return AdvanceCombatSelection(Data);
	}
	if (Data.QueryResult == EEnemyEnvQueryResult::Waiting)
	{
		return StartEnvQuery(Context, Data);
	}
	if (Data.QueryResult != EEnemyEnvQueryResult::Running)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (HasEnvQueryTimedOut(Data))
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::TimedOut);
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bSearchSelectionPending)
	{
		return ResumeSearchSelection(Data);
	}
	return EStateTreeRunStatus::Running;
}

void FEnemyRunEnvQueryTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (IsValid(Data.Enemy) && Data.QueryResult != EEnemyEnvQueryResult::Success)
	{
		// 이전 Select의 Exit가 이미 채택된 새 이동/예약을 정리하지 않는다.
		if (Data.CombatReservationRequest == 0 || Data.Enemy->IsCombatReservationRequestCurrent(Data.CombatReservationRequest))
		{
			Data.Enemy->ClearCombatFlightHandoff();
		}
		if (Data.CombatReservationRequest != 0)
		{
			Data.Enemy->ReleaseCombatReservation(Data.CombatReservationRequest);
		}
	}
	if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyEQSTransition] Enemy=%s Result=%s NoOrbitDestination=%d Current=%s Target=%s RunStatus=%s"),
			*GetNameSafe(Data.Enemy), *UEnum::GetValueAsString(Data.QueryResult), Data.bNoOrbitDestination,
			*Transition.CurrentState.Describe(), *Transition.TargetState.Describe(),
			*UEnum::GetValueAsString(Transition.CurrentRunStatus));
	}
	if (Data.QueryResult == EEnemyEnvQueryResult::Waiting || Data.QueryResult == EEnemyEnvQueryResult::Running)
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::Cancelled);
	}
	else
	{
		Data.bActive = false;
		LogEnvQuery(TEXT("Exit"), Data);
	}
}

namespace
{
void LogSearchEpisode(const TCHAR* Phase, const AEnemyBase& Enemy)
{
	if (CVarEnemyEQSDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
	const FEnemySearchEpisode& Episode = Enemy.GetSearchEpisode();
	UE_LOG(LogEnemyEQS, Log,
		TEXT("[EnemySearch] %s Enemy=%s Episode=%llu Revision=%llu FreshHint=%d Center=%s Selected=%d Index=%d Checked=%d Guard=%d Time=%.3f Location=%s Visible=%d Combat=%s"),
		Phase, *GetNameSafe(&Enemy), Episode.Serial, Episode.SharedRevision, Enemy.HasFreshSharedTargetLocation(),
		*Episode.Center.ToCompactString(), Episode.Points.Num(), Episode.PointIndex,
		Episode.CheckedPoints.Num(), Episode.bLocalGuard, Enemy.GetWorld()->GetTimeSeconds(),
		*Enemy.GetActorLocation().ToCompactString(), Enemy.IsPlayerCurrentlyVisible(),
		*UEnum::GetValueAsString(Enemy.GetCombatState()));
}
}

FEnemySearchSessionTask::FEnemySearchSessionTask()
{
	bShouldCallTick = true;
	bShouldAffectTransitions = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = false;
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
#endif
}

namespace
{
EStateTreeRunStatus EnterSearchSession(FEnemySearchSessionTaskInstanceData& Data)
{
	Data.bShouldRestart = false;
	Data.bLocalGuard = false;
	Data.bShouldReturnHome = false;
	Data.bShouldInvestigateNoise = Data.bNoiseSearchComplete = false;
	Data.CapturedNoiseSerial = 0;
	Data.CapturedSearchSerial = 0;
	Data.CapturedSearchSharedRevision = 0;
	if (Data.bNoiseInvestigationSearch)
	{
		if (IsValid(Data.Enemy))
		{
			Data.NoiseSearchDuration = Data.Enemy->GetLocationSearchDuration(Data.NoiseSearchDuration);
		}
		if (!IsValid(Data.Enemy) || !Data.Enemy->IsLocationSearchActive()
			|| !Data.Enemy->CanContinueSearchEpisode() || !FMath::IsFinite(Data.NoiseSearchDuration)
			|| Data.NoiseSearchDuration <= 0.0f)
		{
			return EStateTreeRunStatus::Failed;
		}
		Data.CapturedNoiseSerial = Data.Enemy->GetLocationInvestigationSerial();
		Data.bSkipLossWait = true;
		if (!Data.Enemy->GetSearchEpisode().bActive && !Data.Enemy->BeginSearchEpisode())
		{
			return EStateTreeRunStatus::Failed;
		}
		// 부모 Task 재진입은 같은 청각/피격 회차의 시계를 다시 시작하지 않는다.
		Data.NoiseSearchStartedAt = Data.Enemy->GetSearchEpisode().StartedAt;
		Data.SearchCenter = Data.Enemy->GetSearchEpisode().Center;
		Data.CapturedSearchSerial = Data.Enemy->GetSearchEpisode().Serial;
		return EStateTreeRunStatus::Running;
	}
	Data.bSkipLossWait = IsValid(Data.Enemy) && (Data.Enemy->HasFreshSharedTargetLocation() || Data.Enemy->ShouldSkipTrackingLossWait());
	if (!IsValid(Data.Enemy) || !Data.Enemy->BeginSearchEpisode())
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.SearchCenter = Data.Enemy->GetSearchEpisode().Center;
	Data.CapturedSearchSerial = Data.Enemy->GetSearchEpisode().Serial;
	Data.CapturedSearchSharedRevision = Data.Enemy->GetSearchEpisode().SharedRevision;
	LogSearchEpisode(TEXT("Begin"), *Data.Enemy);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus TickSearchSession(FEnemySearchSessionTaskInstanceData& Data)
{
	Data.bShouldReturnHome = false;
	if (Data.bNoiseInvestigationSearch)
	{
		// 전이 직전 재평가에서도 이전 완료 출력이 새 전투·이동 의도를 덮지 않게 한다.
		Data.bNoiseSearchComplete = false;
		Data.bShouldInvestigateNoise = false;
		Data.bShouldRestart = false;
		AEnemyBase* Enemy = Data.Enemy;
		if (!IsValid(Enemy) || !Enemy->HasAuthority() || Enemy->IsDead() || !Enemy->CanUseEnemyPerception())
		{
			return EStateTreeRunStatus::Failed;
		}
		// 같은 프레임에 소리/기한이 바뀌었으면 완료보다 새 의도를 먼저 본다. 귀환 후 재진입하는 중간 전이를 막는다.
		Enemy->RefreshPerceptionGoal();
		// 상위 전투 전이와 새 소리 이동 전이는 출력으로만 요청한다. 부모 완료로 덮어쓰지 않는다.
		const bool bOwnsEpisode = Enemy->IsLocationInvestigationActive()
			&& Data.CapturedNoiseSerial == Enemy->GetLocationInvestigationSerial();
		// 새 피격이 회차를 교체했어도 이전 수색에서 새 이동으로 빠져나가야 한다.
		// Exit의 회차 검사가 새 조사를 보호하므로, 전환 출력은 현재 의도를 기준으로 낸다.
		Data.bShouldInvestigateNoise = Enemy->IsLocationInvestigationActive() && Enemy->IsLocationInvestigationGoal();
		if (!bOwnsEpisode || Data.bShouldInvestigateNoise)
		{
			return EStateTreeRunStatus::Running;
		}
		if (!Enemy->CanContinueSearchEpisode() || !Enemy->GetSearchEpisode().bActive)
		{
			return EStateTreeRunStatus::Failed;
		}
		Enemy->RefreshSearchEpisodeLimit();
		// Task의 짧은 조사 설정은 유지하되 회차의 절대 기한을 넘기지 않는다.
		// 두 기한 모두 같은 시작 시각을 쓰므로 부모 재진입으로 시간이 늘어나지 않는다.
		const FEnemySearchEpisode& Episode = Enemy->GetSearchEpisode();
		const double SearchEndsAt = FMath::Min(Episode.EndsAt, Episode.StartedAt + Data.NoiseSearchDuration);
		Data.bNoiseSearchComplete = Enemy->GetWorld()->GetTimeSeconds() >= SearchEndsAt;
		Data.bShouldRestart = false;
		Data.bLocalGuard = Enemy->GetSearchEpisode().bLocalGuard;
		Data.SearchCenter = Enemy->GetSearchEpisode().Center;
		return EStateTreeRunStatus::Running;
	}
	if (!IsValid(Data.Enemy) || Data.Enemy->IsDead() || !Data.Enemy->CanUseEnemyPerception()
		|| Data.Enemy->GetCombatState() != EEnemyCombatState::Combat)
	{
		return EStateTreeRunStatus::Failed;
	}
	// 일반 접촉 소실 수색도 청각 수색과 같은 순서로 판단한다.
	// 새 단서의 채택 자격을 먼저 갱신해야 남은 LookAround/공유 재시작이 새 이동을 늦추지 않는다.
	Data.Enemy->RefreshPerceptionGoal();
	Data.bShouldInvestigateNoise = Data.Enemy->IsLocationInvestigationGoal();
	if (Data.bShouldInvestigateNoise)
	{
		Data.bShouldRestart = false;
		return EStateTreeRunStatus::Running;
	}
	// 새 조사가 이미 도착 수색까지 진행했다면 옛 부모가 그 회차를 자기 것으로 다시 잡지 않는다.
	if (Data.Enemy->IsLocationInvestigationActive())
	{
		Data.bShouldRestart = false;
		return EStateTreeRunStatus::Running;
	}
	// 직접 재감지는 상위 전이가 처리한다. 부모 Task 완료가 그 전이를 덮어쓰지 않게 한다.
	if (Data.Enemy->IsPlayerCurrentlyVisible() || Data.Enemy->HasTrackableTarget())
	{
		Data.bShouldRestart = false;
		return EStateTreeRunStatus::Running;
	}
	// 새 전투/조사 의도가 우선이다. 그 외에는 자식 이동·회전 중에도 회차 기한을 소진한다.
	Data.Enemy->RefreshSearchEpisodeLimit();
	const FEnemySearchEpisode& Episode = Data.Enemy->GetSearchEpisode();
	if (!Episode.bActive)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Episode.Serial != Data.CapturedSearchSerial)
	{
		// 이 부모가 요청했던 공유 갱신만 RestartWithHint의 회차 교체로 인정한다.
		// 다른 수색의 늦은 Tick이 현재 회차의 정리 권한을 가로채지 못하게 한다.
		if (!Data.bShouldRestart || Episode.SharedRevision <= Data.CapturedSearchSharedRevision)
		{
			return EStateTreeRunStatus::Failed;
		}
		Data.CapturedSearchSerial = Episode.Serial;
		Data.CapturedSearchSharedRevision = Episode.SharedRevision;
	}
	// 부모 Task는 전이용 출력만 갱신한다. 새 정보로 회차를 교체하는 일은 RestartWithHint 자식이 맡는다.
	// 같은 공유 정보의 재전달이나 단순 LKP 갱신은 재시작 조건이 아니다.
	const bool bShouldRestart = Data.Enemy->HasMeaningfulSearchHint();
	if (bShouldRestart && !Data.bShouldRestart)
	{
		LogSearchEpisode(TEXT("NewHintRestartRequested"), *Data.Enemy);
	}
	Data.bShouldRestart = bShouldRestart;
	Data.bLocalGuard = Episode.bLocalGuard;
	Data.SearchCenter = Episode.Center;
	// 새 단서/재발견 전이가 항상 먼저다. 경계 종료는 그 의도가 없을 때만 출력한다.
	Data.bShouldReturnHome = !bShouldRestart
		&& Data.Enemy->CanCompleteGeneralSearch(Data.CapturedSearchSerial);
	return EStateTreeRunStatus::Running;
}

void RefreshSearchSessionTransitions(FEnemySearchSessionTaskInstanceData& Data)
{
	// 자식 EnterState가 회차를 교체한 뒤 같은 프레임에 전이를 다시 평가할 수 있다.
	// 부모 Tick의 이전 출력으로 RestartHint에 재진입하지 않도록 조건 평가 전에 갱신한다.
	const bool bWasRestartRequested = Data.bShouldRestart;
	if (TickSearchSession(Data) == EStateTreeRunStatus::Failed)
	{
		Data.bShouldRestart = false;
	}
	if (bWasRestartRequested && !Data.bShouldRestart && IsValid(Data.Enemy))
	{
		LogSearchEpisode(TEXT("RestartRequestClearedBeforeTransitions"), *Data.Enemy);
	}
}
}

void FEnemySearchSessionTask::TriggerTransitions(FStateTreeExecutionContext& Context) const
{
	RefreshSearchSessionTransitions(Context.GetInstanceData(*this));
}

EStateTreeRunStatus FEnemySearchSessionTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	return EnterSearchSession(Context.GetInstanceData(*this));
}

EStateTreeRunStatus FEnemySearchSessionTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	return TickSearchSession(Context.GetInstanceData(*this));
}

namespace
{
void ExitSearchSession(FEnemySearchSessionTaskInstanceData& Data)
{
	if (IsValid(Data.Enemy))
	{
		if (Data.bNoiseInvestigationSearch)
		{
			if (Data.CapturedNoiseSerial != 0 && Data.Enemy->GetLocationInvestigationSerial() == Data.CapturedNoiseSerial)
			{
				// 새 소리로 이동할 때만 의도를 넘긴다. 완료·중단은 소리를 소비해 반복 조사를 막는다.
				if (Data.Enemy->IsLocationInvestigationGoal())
				{
					Data.Enemy->EndSearchEpisode();
				}
				else
				{
					Data.Enemy->EndLocationInvestigation();
				}
			}
			return;
		}
		LogSearchEpisode(TEXT("End"), *Data.Enemy);
		// 새 조사 Enter가 먼저 실행됐어도 옛 일반 수색 Exit는 그 회차/예약을 해제하지 않는다.
		if (Data.CapturedSearchSerial != 0
			&& Data.Enemy->GetSearchEpisode().Serial == Data.CapturedSearchSerial)
		{
			Data.Enemy->EndSearchEpisode();
		}
	}
}
}

void FEnemySearchSessionTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	ExitSearchSession(Context.GetInstanceData(*this));
}

FEnemySearchProgressTask::FEnemySearchProgressTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = true;
}

namespace
{
EStateTreeRunStatus AdvanceSearchProgress(FEnemySearchProgressTaskInstanceData& Data)
{
	Data.bHasPoint = false;
	Data.PointIndex = 0;
	Data.Destination = FVector::ZeroVector;
	AEnemyBase* Enemy = Data.Enemy;
	if (Data.Action == EEnemySearchProgress::FinishGeneralSearch)
	{
		return IsValid(Enemy) && Enemy->TryCompleteGeneralSearch(Enemy->GetSearchEpisode().Serial)
			? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}
	if (Data.Action == EEnemySearchProgress::FinishNoiseInvestigation)
	{
		return IsValid(Enemy) && Enemy->CompleteLocationInvestigation()
			? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}
	if (!IsValid(Enemy) || !Enemy->CanContinueSearchEpisode() || !Enemy->GetSearchEpisode().bActive)
	{
		return EStateTreeRunStatus::Failed;
	}
	// 각 자식 State 진입 시 한 단계만 반영한다. SelectCurrentPoint는 조회만 하고,
	// PointChecked는 Move 성공 후 LookAround가 끝난 State에서 호출해야 한다.
	switch (Data.Action)
	{
	case EEnemySearchProgress::RestartWithHint:
		if (!Enemy->HasMeaningfulSearchHint() || !Enemy->BeginSearchEpisode())
		{
			return EStateTreeRunStatus::Failed;
		}
		break;
	case EEnemySearchProgress::AnchorArrived:
		Enemy->CompleteSearchAnchor();
		break;
	case EEnemySearchProgress::PointChecked:
		if (!Enemy->CompleteSearchPoint())
		{
			return EStateTreeRunStatus::Failed;
		}
		break;
	case EEnemySearchProgress::LocalGuard:
		Enemy->EnterSearchLocalGuard();
		break;
	case EEnemySearchProgress::SelectCurrentPoint:
		break;
	default:
		return EStateTreeRunStatus::Failed;
	}
	const FEnemySearchEpisode& Episode = Enemy->GetSearchEpisode();
	Data.PointIndex = Episode.PointIndex;
	Data.bHasPoint = !Episode.bLocalGuard && Episode.Points.IsValidIndex(Episode.PointIndex);
	Data.Destination = Data.bHasPoint ? Episode.Points[Episode.PointIndex] : Enemy->GetActorLocation();
	LogSearchEpisode(*UEnum::GetValueAsString(Data.Action), *Enemy);
	if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyEQS, Log, TEXT("[EnemySearchProgress] Enemy=%s Episode=%llu Action=%s HasPoint=%d Index=%d Destination=%s"),
			*GetNameSafe(Enemy), Episode.Serial, *UEnum::GetValueAsString(Data.Action), Data.bHasPoint,
			Data.PointIndex, *Data.Destination.ToCompactString());
	}
	return Data.Action == EEnemySearchProgress::SelectCurrentPoint && !Data.bHasPoint
		? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Succeeded;
}
}

EStateTreeRunStatus FEnemySearchProgressTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	return AdvanceSearchProgress(Context.GetInstanceData(*this));
}

#if WITH_DEV_AUTOMATION_TESTS
namespace EnemySearchTesting
{
bool PrepareRequest(FEnemyRunEnvQueryTaskInstanceData& Data, FEnvQueryRequest& Request)
{
	return IsEnvQueryConfigurationValid(Data) && HasValidEnvQueryParameters(Data)
		&& IsEnvQuerySnapshotCurrent(Data) && PrepareEnvQueryRequest(Data, Request);
}

bool CompletePendingQuery(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	// Delegate와 같은 요청 번호/활성 검사 후 결과를 적용한다. 종료된 요청은 출력에 손대지 않는다.
	if (!CanAcceptEnvQueryResult(Data, Result.QueryID))
	{
		return false;
	}
	ApplyEnvQueryResult(Data, Result);
	return true;
}

void CaptureQuery(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	Data.bActive = true;
	CaptureEnvQuerySnapshot(Data);
	Data.QueryStartedAt = Data.Enemy->GetWorld()->GetTimeSeconds();
}

void CompleteQuery(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	ApplyEnvQueryResult(Data, Result);
}

bool SelectDestinations(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	return SelectSearchDestinations(Data, Result);
}

EStateTreeRunStatus ResumeSelection(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	return ResumeSearchSelection(Data);
}

EStateTreeRunStatus ResumeCombatSelection(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	return TickCombatSelection(Data);
}

void BeginUnifiedSelection(FEnemyRunEnvQueryTaskInstanceData& Data)
{
	BeginUnifiedCombatSelection(Data);
}

bool IsYieldSnapshotCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	return IsCombatYieldSnapshotCurrent(Data);
}

void BuildYieldDestinations(const FVector& Origin, const FVector& CapsuleOffset,
	float CapsuleRadius, float CapsuleHalfHeight, float Clearance, float PreferredRadius,
	const FVector& Start, const FVector& End, TArray<FVector>& Destinations)
{
	TArray<EnemyCombatNavigation::FCandidate> Candidates;
	BuildCombatYieldCandidates(Origin, CapsuleOffset, CapsuleRadius, CapsuleHalfHeight, Clearance,
		PreferredRadius, Start, End, Candidates);
	Destinations.Reset();
	for (const auto& Candidate : Candidates)
	{
		Destinations.Add(Candidate.Destination);
	}
}

EStateTreeRunStatus EnterSession(FEnemySearchSessionTaskInstanceData& Data)
{
	return EnterSearchSession(Data);
}

EStateTreeRunStatus TickSession(FEnemySearchSessionTaskInstanceData& Data)
{
	return TickSearchSession(Data);
}

void RefreshSessionTransitions(FEnemySearchSessionTaskInstanceData& Data)
{
	RefreshSearchSessionTransitions(Data);
}

void ExitSession(FEnemySearchSessionTaskInstanceData& Data)
{
	ExitSearchSession(Data);
}

EStateTreeRunStatus Progress(FEnemySearchProgressTaskInstanceData& Data)
{
	return AdvanceSearchProgress(Data);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchQueryConfigurationTest,
	"Outlier.Enemy.Search.QueryConfiguration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchQueryConfigurationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.SearchMode = EEnemySearchQueryMode::Anchor;
	Data.QueryTemplate = NewObject<UEnvQuery>();
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Data.QueryTemplate);
	Data.QueryTemplate->GetOptionsMutable().Add(Option);
	UEnemyEnvQueryGenerator_Search* Generator = NewObject<UEnemyEnvQueryGenerator_Search>(Option);
	Option->Generator = Generator;
	TestFalse(TEXT("Search requires all matching results for fallback"), IsEnvQueryConfigurationValid(Data));
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	TestFalse(TEXT("Search must not read a live visible target"), IsEnvQueryConfigurationValid(Data));
	Data.bRequireVisibleTarget = false;
	TestTrue(TEXT("Anchor search configuration is valid"), IsEnvQueryConfigurationValid(Data));
	Data.SearchMode = EEnemySearchQueryMode::Points;
	TestFalse(TEXT("Points task rejects an anchor generator"), IsEnvQueryConfigurationValid(Data));
	Generator->Mode = EEnemySearchQueryMode::Points;
	TestTrue(TEXT("Matching points generator is valid"), IsEnvQueryConfigurationValid(Data));
	Data.bValidateReachability = true;
	TestFalse(TEXT("Search already owns bounded path validation"), IsEnvQueryConfigurationValid(Data));
	Data.bValidateReachability = false;
	Data.SearchPointSeparation = 0;
	TestFalse(TEXT("Zero separation is invalid"), IsEnvQueryConfigurationValid(Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchDirectionCoverageTest,
	"Outlier.Enemy.Search.DirectionCoverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchDirectionCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnvQueryInstance Result;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	const FVector Center(100.0, 200.0, 50.0);
	const FVector East = Center + FVector(500.0, 0.0, 0.0);
	const FVector North = Center + FVector(0.0, 500.0, 0.0);
	const FVector West = Center + FVector(-500.0, 0.0, 0.0);
	const FVector South = Center + FVector(0.0, -500.0, 0.0);
	for (const FVector& Point : {East, North, West, South})
	{
		Result.AddItemData<UEnvQueryItemType_Point>(Point);
	}
	TArray<int32> Order = {0, 1, 2, 3};
	TArray<FVector> Selected = {East};
	TArray<FVector> Checked;
	PrioritizeUncoveredSearchDirection(Order, 1, Result, Center, East, Selected, Checked);
	TestEqual(TEXT("Next point covers the opposite clue-relative direction"), Order[1], 2);
	TestEqual(TEXT("Already evaluated prefix remains fixed"), Order[0], 0);

	// 반대편을 골랐으면 이미 방문한 북쪽 대신 남쪽으로 확장한다.
	Selected.Add(West);
	Checked.Add(North);
	PrioritizeUncoveredSearchDirection(Order, 2, Result, Center, West, Selected, Checked);
	TestEqual(TEXT("Checked direction is not preferred over uncovered region"), Order[2], 3);
	TestEqual(TEXT("Retry cannot reinsert the consumed candidate"), Order[1], 2);
	PrioritizeUncoveredSearchDirection(Order, 2, Result, Center, West, Selected, Checked);
	TestEqual(TEXT("Repeated evaluation keeps deterministic ordering"), Order[2], 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChaseQueryConfigurationTest,
	"Outlier.Enemy.EQS.ChaseConfiguration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChaseQueryConfigurationTest::RunTest(const FString& Parameters)
{
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.QueryTemplate = NewObject<UEnvQuery>();
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Data.QueryTemplate);
	Data.QueryTemplate->GetOptionsMutable().Add(Option);
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Chase>(Option);
	Data.bValidateChasePath = true;
	TestFalse(TEXT("Chase requires all matching"), IsEnvQueryConfigurationValid(Data));
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	TestTrue(TEXT("Chase has bounded path validation"), IsEnvQueryConfigurationValid(Data));
	Data.bValidateReachability = true;
	TestFalse(TEXT("Two validation modes cannot be combined"), IsEnvQueryConfigurationValid(Data));
	Data.bValidateReachability = false;
	Data.bRequireVisibleTarget = false;
	TestFalse(TEXT("Chase cannot use hidden target data"), IsEnvQueryConfigurationValid(Data));
	Data.bRequireVisibleTarget = true;
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Approach>(Option);
	TestFalse(TEXT("Gun approach cannot be used for self destruct chase"), IsEnvQueryConfigurationValid(Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyEnvQueryPathSelectionTest,
	"Outlier.Enemy.EQS.PathSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyEnvQueryPathSelectionTest::RunTest(const FString& Parameters)
{
	FEnemyRunEnvQueryTaskInstanceData Data;
	TestTrue(TEXT("Legacy query keeps path validation disabled"), IsReachabilityConfigurationValid(Data));
	Data.bValidateReachability = true;
	TestFalse(TEXT("Single result cannot provide fallback candidates"), IsReachabilityConfigurationValid(Data));
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	TestTrue(TEXT("All matching enables bounded path selection"), IsReachabilityConfigurationValid(Data));
	Data.MaxPathCandidates = 0;
	TestFalse(TEXT("Empty candidate budget is rejected"), IsReachabilityConfigurationValid(Data));
	Data.MaxPathCandidates = 4;
	Data.MaxCollisionChecks = 0;
	TestFalse(TEXT("Empty collision budget is rejected"), IsReachabilityConfigurationValid(Data));
	TestTrue(TEXT("Positive shared budget permits next candidate"), HasCandidatePathBudget(1, 1, 0.01));
	TestFalse(TEXT("Consumed node budget stops selection"), HasCandidatePathBudget(0, 1, 1));
	TestFalse(TEXT("Consumed collision budget stops selection"), HasCandidatePathBudget(1, 0, 1));
	TestFalse(TEXT("Consumed time budget stops selection"), HasCandidatePathBudget(1, 1, 0));
	TestEqual(TEXT("Unexamined candidates are not classified unreachable"),
		ResolveCandidateFailure(false, 4, 15), EEnemyEnvQueryResult::CandidateLimitReached);
	TestEqual(TEXT("Budget exhaustion takes precedence over candidate limit"),
		ResolveCandidateFailure(true, 4, 15), EEnemyEnvQueryResult::PathBudgetExceeded);
	TestEqual(TEXT("All failed candidates have distinct result"),
		ResolveCandidateFailure(false, 3, 3), EEnemyEnvQueryResult::NoReachableDestination);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOrbitQueryConfigurationTest,
	"Outlier.Enemy.EQS.OrbitConfiguration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOrbitQueryConfigurationTest::RunTest(const FString& Parameters)
{
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.QueryTemplate = NewObject<UEnvQuery>();
	Data.bValidateOrbitPath = true;
	TestFalse(TEXT("Orbit requires all matching results"), IsReachabilityConfigurationValid(Data));
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	TestTrue(TEXT("Orbit permits bounded validation without A star"), IsReachabilityConfigurationValid(Data));
	Data.bValidateReachability = true;
	TestFalse(TEXT("Orbit cannot also request unrestricted A star"), IsReachabilityConfigurationValid(Data));
	Data.bValidateReachability = false;
	TestFalse(TEXT("Missing orbit generator cannot classify empty results"), IsOrbitQueryConfigurationValid(Data));
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Data.QueryTemplate);
	Data.QueryTemplate->GetOptionsMutable().Add(Option);
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Orbit>(Option);
	TestTrue(TEXT("Single orbit generator is accepted"), IsOrbitQueryConfigurationValid(Data));
	Data.bRequireVisibleTarget = false;
	TestFalse(TEXT("Orbit requires a visible target"), IsOrbitQueryConfigurationValid(Data));
	Data.bRequireVisibleTarget = true;
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Approach>(Option);
	TestFalse(TEXT("Approach query cannot be misclassified as orbit"), IsOrbitQueryConfigurationValid(Data));
	TestFalse(TEXT("Precision flag defaults to false"), Data.bNoOrbitDestination);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRetreatQueryConfigurationTest,
	"Outlier.Enemy.EQS.RetreatConfiguration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRetreatQueryConfigurationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyRunEnvQueryTaskInstanceData Data;
	TestFalse(TEXT("Legacy tasks do not enable retreat validation"), Data.bValidateRetreatPath);
	TestFalse(TEXT("No destination must be proven, not defaulted"), Data.bNoRetreatDestination);
	Data.QueryTemplate = NewObject<UEnvQuery>();
	Data.bValidateRetreatPath = true;
	TestFalse(TEXT("Retreat requires all matching results"), IsReachabilityConfigurationValid(Data));
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Data.QueryTemplate);
	Data.QueryTemplate->GetOptionsMutable().Add(Option);
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Retreat>(Option);
	TestTrue(TEXT("Retreat generator and bounded path settings are valid"), IsEnvQueryConfigurationValid(Data));
	Data.bValidateReachability = true;
	TestFalse(TEXT("Retreat owns its constrained path validation"), IsReachabilityConfigurationValid(Data));
	Data.bValidateReachability = false;
	Data.bValidateOrbitPath = true;
	TestFalse(TEXT("Retreat and orbit cannot be enabled together"), IsReachabilityConfigurationValid(Data));
	Data.bValidateOrbitPath = false;
	Data.bRequireVisibleTarget = false;
	TestFalse(TEXT("Retreat requires direct sight"), IsRetreatQueryConfigurationValid(Data));
	Data.bRequireVisibleTarget = true;
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Approach>(Option);
	TestFalse(TEXT("Approach cannot stand in for retreat generator"), IsRetreatQueryConfigurationValid(Data));
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Retreat>(Option);
	Data.QueryTemplate->GetOptionsMutable().Add(NewObject<UEnvQueryOption>(Data.QueryTemplate));
	TestFalse(TEXT("Additional options cannot invalidate no-candidate proof"), IsRetreatQueryConfigurationValid(Data));
	TestTrue(TEXT("Blocked segment is a physical candidate rejection"), IsPhysicalRetreatFailure(EFlightPathStatus::BlockedSegment));
	TestFalse(TEXT("Budget exhaustion is not a no-destination proof"), IsPhysicalRetreatFailure(EFlightPathStatus::BudgetExceeded));
	TestFalse(TEXT("Missing room is not a no-destination proof"), IsPhysicalRetreatFailure(EFlightPathStatus::Unavailable));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyEnvQueryValidationTest,
	"Outlier.Enemy.EQS.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyEnvQueryValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyRunEnvQueryTaskInstanceData Data;
	TestFalse(TEXT("Missing query template is invalid"), IsEnvQueryConfigurationValid(Data));
	Data.QueryTemplate = NewObject<UEnvQuery>();
	TestTrue(TEXT("Default query settings are valid"), IsEnvQueryConfigurationValid(Data));
	Data.Clearance = -1.0f;
	TestFalse(TEXT("Negative clearance is invalid"), IsEnvQueryConfigurationValid(Data));
	Data.Clearance = 5.0f;
	Data.QueryInterval = 0.0f;
	TestFalse(TEXT("Zero request interval is invalid"), IsEnvQueryConfigurationValid(Data));
	Data.QueryInterval = 0.5f;
	Data.QueryTimeout = 0.0f;
	TestFalse(TEXT("Zero timeout is invalid"), IsEnvQueryConfigurationValid(Data));
	TestTrue(TEXT("Empty parameter list is valid"), HasValidEnvQueryParameters(Data));
	FAIDynamicParam& Param = Data.QueryConfig.AddDefaulted_GetRef();
	TestTrue(TEXT("Constant parameter without blackboard key is valid"), HasValidEnvQueryParameters(Data));
	UBlackboardData* Blackboard = NewObject<UBlackboardData>();
	FBlackboardEntry& Entry = Blackboard->Keys.AddDefaulted_GetRef();
	Entry.EntryName = TEXT("Target");
	Entry.KeyType = NewObject<UBlackboardKeyType_Bool>(Blackboard);
	Param.BBKey.SelectedKeyName = TEXT("Target");
	Param.BBKey.ResolveSelectedKey(*Blackboard);
	TestTrue(TEXT("Test parameter has a resolved blackboard key"), Param.BBKey.IsSet());
	TestFalse(TEXT("Blackboard key is rejected before engine request"), HasValidEnvQueryParameters(Data));

	FEnvQueryResult Result;
	TestFalse(TEXT("Processing result is not an empty completion"), IsEmptyEnvQueryResult(Result));
	Result.MarkAsFailed();
	TestTrue(TEXT("Failed empty query has no results"), IsEmptyEnvQueryResult(Result));
	Result.MarkAsAborted();
	TestFalse(TEXT("Aborted query is not classified as no results"), IsEmptyEnvQueryResult(Result));
	Result.MarkAsFinishedWithoutIssues();
	TestTrue(TEXT("Successful empty query has no results"), IsEmptyEnvQueryResult(Result));
	TestFalse(TEXT("Empty query cannot provide a destination"), IsLocationEnvQueryResult(Result));
	Result.Items.AddDefaulted();
	TestFalse(TEXT("Missing item type cannot provide a destination"), IsLocationEnvQueryResult(Result));
	Result.ItemType = UEnvQueryItemType_VectorBase::StaticClass();
	TestTrue(TEXT("Successful vector result can provide a destination"), IsLocationEnvQueryResult(Result));
	TestFalse(TEXT("Nonempty result is not classified as no results"), IsEmptyEnvQueryResult(Result));
	Result.ItemType = UEnvQueryItemType_ActorBase::StaticClass();
	TestFalse(TEXT("Actor result is not a movement destination"), IsLocationEnvQueryResult(Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHeightSelectionBudgetTest,
	"Outlier.Enemy.EQS.FlightHeight.SelectionBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHeightSelectionBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.MaxCollisionChecks = 32;
	Data.MaxSearchMilliseconds = 4.0;
	Data.HeightSnapshot.CollisionChecks = 27;
	TestEqual(TEXT("Height checks leave insufficient start-check budget"),
		CheckSelectionStart(Data), EFlightPathStatus::BudgetExceeded);
	TestEqual(TEXT("Insufficient checks are not physical failure"), Data.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
	TestFalse(TEXT("Insufficient checks never enable stationary firing"), Data.bNoOrbitDestination);
	TestFalse(TEXT("Insufficient checks never prove retreat impossible"), Data.bNoRetreatDestination);
	Data.HeightSnapshot.CollisionChecks = 0;
	Data.HeightSnapshot.Milliseconds = 4.0;
	TestEqual(TEXT("Height time exhausts the shared selection budget"),
		CheckSelectionStart(Data), EFlightPathStatus::BudgetExceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyEnvQueryCompletionTest,
	"Outlier.Enemy.EQS.CompletionGuard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyEnvQueryCompletionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyRunEnvQueryTaskInstanceData Data;
	TestFalse(TEXT("Inactive task rejects callbacks"), CanAcceptEnvQueryResult(Data, 1));
	Data.bActive = true;
	Data.RequestId = 1;
	Data.QueryResult = EEnemyEnvQueryResult::Running;
	Data.bHasDestination = true;
	TestTrue(TEXT("Active matching request accepts callback"), CanAcceptEnvQueryResult(Data, 1));
	TestFalse(TEXT("Previous request cannot finish a new request"), CanAcceptEnvQueryResult(Data, 2));
	Data.RequestId = INDEX_NONE;
	TestFalse(TEXT("Missing request id cannot accept completion"), CanAcceptEnvQueryResult(Data, INDEX_NONE));
	Data.RequestId = 1;
	Data.bActive = false;
	TestFalse(TEXT("Inactive matching request rejects completion"), CanAcceptEnvQueryResult(Data, 1));
	Data.bActive = true;
	CancelEnvQuery(Data, EEnemyEnvQueryResult::Cancelled);
	TestFalse(TEXT("Abort callback cannot restore a cancelled destination"), CanAcceptEnvQueryResult(Data, 1));
	TestFalse(TEXT("Cancellation clears destination validity"), Data.bHasDestination);
	Data.bActive = true;
	Data.RequestId = 1;
	Data.QueryResult = EEnemyEnvQueryResult::Waiting;
	TestFalse(TEXT("Waiting task cannot accept a previous completion"), CanAcceptEnvQueryResult(Data, 1));
	Data.QueryResult = EEnemyEnvQueryResult::Success;
	TestFalse(TEXT("Completed task rejects duplicate completion"), CanAcceptEnvQueryResult(Data, 1));
	Data.QueryResult = EEnemyEnvQueryResult::Running;
	CancelEnvQuery(Data, EEnemyEnvQueryResult::TimedOut);
	TestFalse(TEXT("Late result cannot finish a timed out task"), CanAcceptEnvQueryResult(Data, 1));
	TestFalse(TEXT("Missing pawn cannot pass lifetime validation"), IsEnvQuerySnapshotCurrent(Data));
	return true;
}
#endif


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

	InstanceData.Destination = HorizontalLocation;
	InstanceData.Destination.Z = Enemy->GetActorLocation().Z;

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
