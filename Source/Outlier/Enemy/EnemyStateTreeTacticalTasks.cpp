#include "Enemy/EnemyStateTreeTacticalTasks.h"

#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyEnvQuery.h"
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
	return IsValid(Enemy) && Enemy->HasAuthority() && !Enemy->IsDead()
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
	if (!CVarEnemyPatrolDebug.GetValueOnGameThread()) return;
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
	if (Now < Enemy.GetNextFlightPathRequestTime()) return EStateTreeRunStatus::Running;
	Enemy.SetNextFlightPathRequestTime(Now + Data.RetryInterval);
	ReleasePatrol(Data);
	Data.bHasDestination = false;
	Data.SelectedWaypoints.Reset();
	Data.PathStatus = EFlightPathStatus::Unavailable;
	ARoomVolume* Room = Data.CombatSubsystem->GetRegisteredRoomVolume(Enemy.GetDefaultRoomTag());
	UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	if (!IsValid(Room) || !IsValid(Capsule)) return EStateTreeRunStatus::Failed;

	const FVector Points[2] = { Enemy.GetPatrolPointA(), Enemy.GetPatrolPointB() };
	int32 Order[2] = { Data.NextPointIndex, 1 - Data.NextPointIndex };
	const bool bPreferredUsed = Data.RoomSubsystem->IsPatrolDestinationUsed(&Enemy, Points[Order[0]], Data.DestinationSeparation);
	const bool bOtherUsed = Data.RoomSubsystem->IsPatrolDestinationUsed(&Enemy, Points[Order[1]], Data.DestinationSeparation);
	if (bPreferredUsed && !bOtherUsed) Swap(Order[0], Order[1]);

	const FVector Offset = Capsule->GetComponentLocation() - Enemy.GetActorLocation();
	const double Started = FPlatformTime::Seconds();
	int32 RemainingNodes = Data.MaxSearchNodes;
	int32 RemainingChecks = Data.MaxCollisionChecks;
	bool bBudgetExceeded = false;
	for (int32 Index : Order)
	{
		// 스폰 지점 A 또는 겹치는 A/B를 성공 처리하면 매 프레임 같은 지점으로 재진입한다.
		if (Points[Index].ContainsNaN()
			|| FVector::DistSquared(Enemy.GetActorLocation(), Points[Index]) <= FMath::Square(Data.AcceptanceRadius)) continue;
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
		if (Result.Status != EFlightPathStatus::Success) continue;
		if (!Data.RoomSubsystem->ReservePatrolDestination(&Enemy, Points[Index])) return EStateTreeRunStatus::Failed;
		Data.Destination = Points[Index];
		for (const FVector& Point : Result.Waypoints) Data.SelectedWaypoints.Add(Point - Offset);
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
	if (bBudgetExceeded) Data.PathStatus = EFlightPathStatus::BudgetExceeded;
	if (CVarEnemyPatrolDebug.GetValueOnGameThread())
	{
		UE_LOG(LogEnemyPatrol, Log, TEXT("[EnemyPatrol] Enemy=%s Guard Status=%s"),
			*GetNameSafe(&Enemy), *UEnum::GetValueAsString(Data.PathStatus));
	}
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus EnterPatrol(FEnemyPatrolTaskInstanceData& Data)
{
	if (!IsValid(Data.Enemy)) return EStateTreeRunStatus::Failed;
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Data.Enemy->GetController());
	const int32 Generation = Data.Enemy->GetPoolGameplayGeneration();
	const int32 Lease = Data.Enemy->GetPoolLeaseSerial();
	if (Data.Controller.Get() != Controller || Data.Generation != Generation || Data.Lease != Lease) Data.NextPointIndex = 1;
	Data.Controller = Controller;
	Data.Generation = Generation;
	Data.Lease = Lease;
	Data.RoomSubsystem = Data.Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	Data.CombatSubsystem = Data.Enemy->GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	Data.bHasDestination = false;
	Data.SelectedWaypoints.Reset();
	Data.PathStatus = EFlightPathStatus::Unavailable;
	if (!IsPatrolLifetimeCurrent(Data)) return EStateTreeRunStatus::Failed;
	if (Data.Action == EEnemyPatrolAction::Session) return EStateTreeRunStatus::Running;
	if (Data.Action == EEnemyPatrolAction::Release)
	{
		ReleasePatrol(Data);
		Controller->StopMovement();
		Data.Enemy->ConsumeMovementInputVector();
		if (UCharacterMovementComponent* Movement = Data.Enemy->GetCharacterMovement()) Movement->StopMovementImmediately();
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
	if (Data.Action == EEnemyPatrolAction::Session) ReleasePatrol(Data);
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
	if (CVarEnemyEQSDebug.GetValueOnGameThread() < 2) return;
	const FEnvQueryDebugData& Debug = Query.DebugData;
	int32 Rejected = 0;
	for (int32 Index = 0; Index < Debug.DebugItemDetails.Num(); ++Index)
	{
		const FEnvQueryItemDetails& Detail = Debug.DebugItemDetails[Index];
		if (Detail.FailedTestIndex == INDEX_NONE) continue;
		++Rejected;
		// 큰 Grid 질의도 한 요청에서 상세 로그가 무제한으로 쌓이지 않게 한다.
		if (Rejected > 64) continue;
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
		if (!Manager) return INDEX_NONE;
		TSharedPtr<FEnvQueryInstance> Query = Manager->PrepareQueryInstance(Request, RunMode);
		if (!Query) return INDEX_NONE;
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
	if (Modes == 0) return true;
	if (Modes != 1) return false;
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
	if (!Data.bValidateOrbitPath) return true;
	if (!Data.bRequireVisibleTarget || !Data.QueryTemplate) return false;
	// 다른 Generator/복수 옵션을 선회로 해석하면 빈 결과를 잘못 정밀 사격으로 판단할 수 있다.
	const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
	return Options.Num() == 1 && Options[0]
		&& Options[0]->Generator && Options[0]->Generator->IsA<UEnemyEnvQueryGenerator_Orbit>();
}

bool IsRetreatQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!Data.bValidateRetreatPath) return true;
	if (!Data.bRequireVisibleTarget || !Data.QueryTemplate) return false;
	const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
	return Options.Num() == 1 && Options[0] && Options[0]->Generator
		&& Options[0]->Generator->GetClass() == UEnemyEnvQueryGenerator_Retreat::StaticClass();
}

bool IsChaseQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (!Data.bValidateChasePath) return true;
	if (!Data.bRequireVisibleTarget || !Data.QueryTemplate) return false;
	const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
	return Options.Num() == 1 && Options[0] && Options[0]->Generator
		&& Options[0]->Generator->GetClass() == UEnemyEnvQueryGenerator_Chase::StaticClass();
}

bool IsEnvQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (Data.SearchMode != EEnemySearchQueryMode::None)
	{
		if (Data.bRequireVisibleTarget || !Data.QueryTemplate
			|| !FMath::IsFinite(Data.SearchPointSeparation) || Data.SearchPointSeparation <= 0.0f) return false;
		const TArray<UEnvQueryOption*>& Options = Data.QueryTemplate->GetOptions();
		const UEnemyEnvQueryGenerator_Search* Generator = Options.Num() == 1 && Options[0]
			? Cast<UEnemyEnvQueryGenerator_Search>(Options[0]->Generator) : nullptr;
		if (!Generator || Generator->Mode != Data.SearchMode) return false;
	}
	if (!FMath::IsFinite(Data.TargetChangeDistance) || Data.TargetChangeDistance < 0.0f) return false;
	if (!IsReachabilityConfigurationValid(Data)) return false;
	if (!IsOrbitQueryConfigurationValid(Data)) return false;
	if (!IsRetreatQueryConfigurationValid(Data)) return false;
	if (!IsChaseQueryConfigurationValid(Data)) return false;
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
		&& Data.CachedRooms->GetRegisteredRoomVolume(Data.CapturedRoomTag) == Data.CapturedRoom.Get();
}

bool IsEnvQueryTargetCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	if (Data.SearchMode != EEnemySearchQueryMode::None)
	{
		const FEnemySearchEpisode& Episode = Data.Enemy->GetSearchEpisode();
		return Episode.bActive && !Episode.bLocalGuard && Episode.Serial == Data.CapturedSearchSerial
			&& !Data.Enemy->IsPlayerCurrentlyVisible()
			&& Data.Enemy->GetSharedTargetRevision() == Data.CapturedSharedRevision;
	}
	if (!Data.bRequireVisibleTarget)
	{
		return true;
	}
	if (!Data.Enemy->IsPlayerCurrentlyVisible() || !Data.CapturedTarget.IsValid()
		|| Data.CachedController->GetCachedVisibleTarget() != Data.CapturedTarget.Get()) return false;
	const float ChangeDistance = (Data.bValidateOrbitPath || Data.bValidateRetreatPath || Data.bValidateChasePath) && Data.TargetChangeDistance <= 0.0f
		? 200.0f : Data.TargetChangeDistance;
	return ChangeDistance <= 0.0f
		|| FVector::DistSquared2D(Data.SelectedTargetLocation, Data.CapturedTarget->GetActorLocation())
			< FMath::Square(ChangeDistance);
}

bool IsEnvQuerySnapshotCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 적 수명 -> 소유 컨트롤러 -> 초기 소속 방 -> 직접 목격 타깃 순서로 확인한다.
	// 앞 단계가 참조 유효성을 보장하므로 뒤 단계는 동일 참조를 다시 검색하지 않는다.
	return IsEnvQueryEnemyCurrent(Data)
		&& IsEnvQueryControllerCurrent(Data)
		&& IsEnvQueryRoomCurrent(Data)
		&& IsEnvQueryTargetCurrent(Data)
		&& (!(Data.bRequireVisibleTarget && Data.SearchMode == EEnemySearchQueryMode::None)
			|| EnemyFlightHeight::IsSnapshotCurrent(*Data.Enemy, Data.HeightSnapshot))
		&& (!Data.bValidateOrbitPath
			|| Data.Enemy->GetActorLocation().Equals(Data.CapturedOrbitStart, 25.0))
		&& (!Data.bValidateRetreatPath
			|| Data.Enemy->GetActorLocation().Equals(Data.CapturedRetreatStart, 25.0));
}

void LogEnvQuerySnapshotFailure(const FEnemyRunEnvQueryTaskInstanceData& Data, int32 CompletedRequest = INDEX_NONE)
{
	if (CVarEnemyEQSDebug.GetValueOnGameThread() == 0) return;
	const TCHAR* Reason = Data.bValidateRetreatPath ? TEXT("RetreatStartMoved") : TEXT("OrbitStartMoved");
	if (!IsEnvQueryEnemyCurrent(Data)) Reason = TEXT("EnemyLifetimeOrCombatStateChanged");
	else if (!IsEnvQueryControllerCurrent(Data)) Reason = TEXT("ControllerChanged");
	else if (!IsEnvQueryRoomCurrent(Data)) Reason = TEXT("RoomChangedOrUnavailable");
	else if (!IsEnvQueryTargetCurrent(Data)) Reason = TEXT("SightLostTargetChangedOrMoved");
	else if (Data.bRequireVisibleTarget && Data.SearchMode == EEnemySearchQueryMode::None
		&& !EnemyFlightHeight::IsSnapshotCurrent(*Data.Enemy, Data.HeightSnapshot)) Reason = TEXT("FlightHeightSettingsChanged");
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
	Data.CapturedRoomTag = Enemy->GetDefaultRoomTag();
	Data.CapturedRoom = Data.CachedRooms ? Data.CachedRooms->GetRegisteredRoomVolume(Data.CapturedRoomTag) : nullptr;
	Data.CapturedTarget = Data.CachedController.IsValid() && Data.bRequireVisibleTarget
		? Data.CachedController->GetCachedVisibleTarget() : nullptr;
	Data.SelectedTargetLocation = Data.CapturedTarget.IsValid()
		? Data.CapturedTarget->GetActorLocation() : FVector::ZeroVector;
	Data.SelectedTargetActor = Data.CapturedTarget.Get();
	Data.CapturedGeneration = Enemy->GetPoolGameplayGeneration();
	Data.CapturedLease = Enemy->GetPoolLeaseSerial();
	Data.CapturedCombatState = Enemy->GetCombatState();
	Data.CapturedOrbitStart = ToQueryParamLocation(Enemy->GetActorLocation());
	Data.CapturedOrbitCenter = ToQueryParamLocation(Data.SelectedTargetLocation);
	Data.CapturedOrbitLength = Data.bValidateOrbitPath ? FMath::FRandRange(300.0f, 500.0f) : 0.0f;
	Data.bCapturedOrbitLeft = Enemy->PrefersCombatLeft();
	Data.CapturedRetreatStart = ToQueryParamLocation(Enemy->GetActorLocation());
	Data.CapturedChaseStart = ToQueryParamLocation(Enemy->GetActorLocation());
	Data.CapturedChaseLane = Enemy->GetCombatChaseLane();
	Data.HeightSnapshot = FEnemyFlightHeightSnapshot();
	Data.HeightSnapshot.bEnabled = Enemy->FlightHeightSettings.bEnabled;
	Data.HeightSnapshot.Settings = Enemy->FlightHeightSettings;
	Data.HeightSnapshot.Preference = Enemy->GetCombatHeightPreference();
	Data.RetreatOrigin = ToQueryParamLocation(Data.SelectedTargetLocation);
	Data.RetreatDirection = (Data.CapturedRetreatStart - Data.RetreatOrigin).GetSafeNormal2D();
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
	if (bBudgetExceeded) return EEnemyEnvQueryResult::PathBudgetExceeded;
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
	Request.MaxSearchNodes = Nodes;
	Request.MaxCollisionChecks = Checks;
	Request.MaxSearchMilliseconds = Milliseconds;
	Request.bRequireAwayHalfSpace = Data.bValidateRetreatPath;
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
		for (int32 Index = 0; Index < Result.Items.Num(); ++Index) Candidates.Add(Result.GetItemAsLocation(Index));
		return;
	}
	// EQS의 동점 정렬에 개체 성향을 맡기지 않는다. 생성 순서에서 필터를 통과한 후보만 검사한다.
	TArray<FVector> Generated;
	if (Data.HeightSnapshot.bEnabled)
	{
		for (const FEnemyFlightHeightCandidate& Candidate : Data.HeightSnapshot.Candidates)
			if (Candidate.Status == EFlightPathStatus::Success) Generated.Add(Candidate.Location);
	}
	else EnemyChase::BuildCandidates(Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane, Generated);
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
		const FFlightPathRequest Request = MakeCandidatePathRequest(Data, *Capsule, Candidate,
			RemainingNodes, RemainingChecks, RemainingTime);
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
	if (bBudgetExceeded) Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
	return false;
}

bool HasCandidateResult(const FEnvQueryResult& Result, const FVector& Destination)
{
	for (int32 Index = 0; Index < Result.Items.Num(); ++Index)
	{
		if (Result.GetItemAsLocation(Index).Equals(Destination, 1.0)) return true;
	}
	return false;
}

EFlightPathStatus CheckOrbitRoute(FEnemyRunEnvQueryTaskInstanceData& Data,
	const TArray<FVector>& Waypoints, int32& RemainingChecks, double Started, int32 QueryId)
{
	if (Waypoints.Num() < 2) return EFlightPathStatus::BlockedSegment;
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
		RemainingChecks -= 5;
		FFlightPathRequest Request = MakeCandidatePathRequest(Data, *Capsule, Waypoints[Index],
			Data.MaxSearchNodes, 5, GetCandidatePathMilliseconds(Data));
		Request.Start = Waypoints[Index - 1] + Offset;
		const EFlightPathStatus Status = FlightPathfinding::CheckSegment(*Data.Enemy->GetWorld(), *Capsule, Request);
		// 경로가 막혔으면 시야 Trace는 생략한다. 상세 로그도 이번 검사 결과만 사용한다.
		const bool bSightBlocked = Status == EFlightPathStatus::Success
			&& Data.Enemy->GetWorld()->LineTraceTestByChannel(Waypoints[Index], Data.SelectedTargetLocation,
				ECC_Visibility, SightParams);
		if (CVarEnemyEQSDebug.GetValueOnGameThread() >= 2)
		{
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyOrbitSegment] Enemy=%s Request=%d Candidate=%d Segment=%d Start=%s End=%s PathStatus=%s SightBlocked=%d ElapsedMs=%.3f"),
				*GetNameSafe(Data.Enemy), QueryId, Data.CheckedPathCandidates, Index,
				*Waypoints[Index - 1].ToCompactString(), *Waypoints[Index].ToCompactString(),
				*UEnum::GetValueAsString(Status), bSightBlocked, (FPlatformTime::Seconds() - Started) * 1000.0);
		}
		if (Status != EFlightPathStatus::Success) return Status;
		if (bSightBlocked) return EFlightPathStatus::BlockedSegment;
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
		if (Candidates.Num() != Data.HeightSnapshot.Candidates.Num()) return false;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			Candidates[Index].Destination = Data.HeightSnapshot.Candidates[Index].Location;
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
		if (HasCandidateResult(Result, Candidate.Destination)) ++Available;
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
			&& Data.SelectionPathStatus != EFlightPathStatus::BlockedSegment) return false;
	}
	if (bHeightUnavailable) return false;
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
	if (!EnemyOrbit::IsCombatRadius(Data.CapturedOrbitStart, Data.CapturedOrbitCenter)) return false;
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
		if (!bFound) return false;
	}
	return true;
}

FFlightPathResult CheckRetreatCandidate(const FEnemyRunEnvQueryTaskInstanceData& Data, const FVector& Candidate,
	const FCollisionQueryParams& SightParams, int32 Nodes, int32& Checks, double Started, bool& bSightBlocked)
{
	// 시야가 없으면 A*를 생략한다. 시야 Trace와 A*가 전체 후보 선택 예산을 함께 사용한다.
	--Checks;
	bSightBlocked = Data.Enemy->GetWorld()->LineTraceTestByChannel(
		Candidate, Data.SelectedTargetLocation, ECC_Visibility, SightParams);
	FFlightPathResult Path;
	Path.Status = EFlightPathStatus::BlockedSegment;
	if (bSightBlocked) return Path;
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
	if (CVarEnemyEQSDebug.GetValueOnGameThread() == 0) return;
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
		if (Candidates.Num() != Data.HeightSnapshot.Candidates.Num()) return false;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			Candidates[Index] = Data.HeightSnapshot.Candidates[Index].Location;
	}
	const double Started = FPlatformTime::Seconds();
	if (Candidates.IsEmpty() || !IsRetreatStartClear(Data)) return false;
	// Generator의 전체 후보를 다시 확인한다. 빈 결과나 잘못된 EQS 필터를 후퇴 불가로 숨기지 않는다.
	if (!HasOnlyRetreatCandidates(Result, Candidates)) return false;
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
		if (!IsPhysicalRetreatFailure(Path.Status)) return false;
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
	if (bHeightUnavailable) return false;
	Data.QueryResult = EEnemyEnvQueryResult::NoReachableDestination;
	Data.bNoRetreatDestination = true;
	return false;
}

const TCHAR* GetSearchCandidateRejection(const FEnemyRunEnvQueryTaskInstanceData& Data, const FVector& Candidate,
	const UEnemyRoomSubsystem& Rooms, float Separation)
{
	if (!EnemySearch::IsCandidateInRange(Candidate, Data.CapturedOrbitCenter, Data.CapturedSearchHeight, Data.SearchMode))
		return TEXT("OutsideSearchRange");
	if (Data.SearchMode == EEnemySearchQueryMode::Anchor) return nullptr;
	if (Rooms.IsSearchDestinationReserved(Data.Enemy, Candidate, Separation)) return TEXT("ReservedByOtherDrone");
	for (const FVector& Point : Data.SelectedSearchPoints)
	{
		if (FVector::DistSquared(Candidate, Point) < FMath::Square(Separation)) return TEXT("NearSelectedPoint");
	}
	for (const FVector& Point : Data.Enemy->GetSearchEpisode().CheckedPoints)
	{
		if (FVector::DistSquared(Candidate, Point) < FMath::Square(Separation)) return TEXT("AlreadyCheckedPoint");
	}
	return nullptr;
}

bool SelectSearchDestinations(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	Data.bSearchSelectionPending = false;
	UEnemyRoomSubsystem* Rooms = Data.Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	UCapsuleComponent* Capsule = Data.Enemy->GetCapsuleComponent();
	if (!Rooms || !Capsule || (Data.SearchMode == EEnemySearchQueryMode::Points
		&& Data.Enemy->GetSearchEpisode().bPointsSelected)) return false;
	const float Separation = FMath::Max(Data.SearchPointSeparation,
		2.0f * (Capsule->GetScaledCapsuleRadius() + Data.Clearance));
	TArray<int32> Order;
	for (int32 Index = 0; Index < Result.Items.Num(); ++Index) Order.Add(Index);
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
			// 두 번째 이동의 시작은 현재 Pawn이 아니라 사전 선정한 첫 지점이다.
			Request.Start = Data.SelectedSearchPoints[0] + Capsule->GetComponentLocation() - Data.Enemy->GetActorLocation();
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
			if (Data.SelectedSearchPoints.Num() == 2) break;
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
			Data.SelectionPathStatus = EFlightPathStatus::BudgetExceeded;
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

void ApplyEnvQueryResult(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	Data.RequestId = INDEX_NONE;
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
		if (!SelectSearchDestinations(Data, Result)) return;
	}
	else if (Data.bValidateRetreatPath)
	{
		if (!SelectRetreatDestination(Data, Result)) return;
	}
	else if (Data.bValidateOrbitPath)
	{
		if (!CanClassifyEmptyOrbitResult(Data)) return;
		if (!SelectOrbitDestination(Data, Result)) return;
	}
	else if (Data.bValidateReachability || Data.bValidateChasePath)
	{
		if (!SelectReachableDestination(Data, Result)) return;
	}
	else
	{
		// 전투 전용 검증을 사용하지 않는 기존 질의는 첫 결과의 공간 검사만 유지한다.
		const FVector Destination = Result.GetItemAsLocation(0);
		if (!IsEnvQueryDestinationClear(Data, Destination)) return;
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
		if (!Rooms || !Rooms->ReserveSearchDestinations(Data.Enemy, Data.SelectedSearchPoints, Separation)) return;
		if (!Data.Enemy->SetSearchPoints(Data.SelectedSearchPoints))
		{
			Rooms->ReleaseSearchDestinations(Data.Enemy);
			return;
		}
	}
	Data.bHasDestination = true;
	Data.QueryResult = EEnemyEnvQueryResult::Success;
}

bool CanAcceptEnvQueryResult(const FEnemyRunEnvQueryTaskInstanceData& Data, int32 QueryId)
{
	return Data.bActive && Data.QueryResult == EEnemyEnvQueryResult::Running
		&& Data.RequestId != INDEX_NONE && Data.RequestId == QueryId;
}

void CancelEnvQuery(FEnemyRunEnvQueryTaskInstanceData& Data, EEnemyEnvQueryResult Reason)
{
	const int32 RequestId = Data.RequestId;
	// AbortQuery에서도 완료 Delegate가 호출될 수 있다. 먼저 번호/활성 상태를 내려 재진입 결과를 차단한다.
	Data.RequestId = INDEX_NONE;
	Data.bActive = false;
	Data.bHasDestination = false;
	Data.QueryResult = Reason;
	Data.SelectedWaypoints.Reset();
	Data.DeferredSearchResult.Reset();
	Data.bSearchSelectionPending = false;
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
		return EStateTreeRunStatus::Running;
	const TSharedPtr<FEnvQueryResult> Result = Data.DeferredSearchResult;
	if (!Result)
	{
		CancelEnvQuery(Data, EEnemyEnvQueryResult::Failed);
		return EStateTreeRunStatus::Failed;
	}
	// EQS는 재실행하지 않는다. 간격을 둔 새 예산으로 미검증 후보만 확인한다.
	LogEnvQuery(TEXT("SearchSelectionResume"), Data, Result->Items.Num(), Result->QueryID);
	ApplyEnvQueryResult(Data, *Result);
	if (Data.bSearchSelectionPending) return EStateTreeRunStatus::Running;
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
			UE_LOG(LogEnemyEQS, Log, TEXT("[EnemyFlightHeightQuery] Enemy=%s Status=%s Candidates=%d Checks=%d Ms=%.3f"),
				*GetNameSafe(Data.Enemy), *UEnum::GetValueAsString(HeightStatus), Data.HeightSnapshot.Candidates.Num(),
				Data.HeightSnapshot.CollisionChecks, Data.HeightSnapshot.Milliseconds);
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
		EnemyFlightHeight::SetQuerySnapshot(Request, Data.HeightSnapshot);
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
		if (Data.QueryResult != EEnemyEnvQueryResult::Waiting) LogEnvQuery(TEXT("WaitingForRequestInterval"), Data);
		Data.QueryResult = EEnemyEnvQueryResult::Waiting;
		return EStateTreeRunStatus::Running;
	}
	// 간격은 Pawn에 보관해 실패 후 같은 State로 재진입해도 요청이 한 프레임에 반복되지 않게 한다.
	Data.Enemy->SetNextEnvQueryRequestTime(Now + Data.QueryInterval);
	Data.QueryStartedAt = Now;
	Data.QueryResult = EEnemyEnvQueryResult::Running;
	FEnvQueryRequest Request(Data.QueryTemplate, Data.Enemy);
	if (!PrepareEnvQueryRequest(Data, Request)) return EStateTreeRunStatus::Failed;
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
	if (Data.RequestId != INDEX_NONE) FlightPathfinding::RecordTestQuery(Data.Enemy->GetWorld(), false);
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
	Data.NextSearchCandidate = 0;
	Data.SearchSelectionRetries = 0;
	Data.bSearchPathBudgetExceeded = false;
	Data.NextSearchSelectionTime = 0.0;
	Data.bSearchSelectionPending = false;
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
	if (IsValid(Enemy) && Enemy->GetWorld()) Data.MaxCollisionChecks = FlightPathfinding::GetTestBudget(*Enemy->GetWorld(), Data.MaxCollisionChecks);
#endif
	// 실행 환경/설정 검증 -> 요청 당시 상태 저장 -> 현재 상태 검증 -> 비동기 요청.
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !Enemy->GetWorld() || !IsEnvQueryConfigurationValid(Data))
	{
		Data.bActive = false;
		LogEnvQuery(TEXT("Rejected"), Data);
		return EStateTreeRunStatus::Failed;
	}
	if (!HasValidEnvQueryParameters(Data))
	{
		Data.bActive = false;
		LogEnvQuery(TEXT("RejectedParameters"), Data);
		return EStateTreeRunStatus::Failed;
	}
	CaptureEnvQuerySnapshot(Data);
	if (Data.bValidateRetreatPath && (Data.RetreatDirection.IsNearlyZero()
		|| FVector::Dist2D(Data.CapturedRetreatStart, Data.RetreatOrigin) > 1000.0))
	{
		Data.bActive = false;
		LogEnvQuery(TEXT("RejectedRetreatRadiusOrDirection"), Data);
		return EStateTreeRunStatus::Failed;
	}
	if (Data.bValidateOrbitPath && !EnemyOrbit::IsCombatRadius(Data.CapturedOrbitStart, Data.CapturedOrbitCenter))
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
	if (Data.bSearchSelectionPending) return ResumeSearchSelection(Data);
	return EStateTreeRunStatus::Running;
}

void FEnemyRunEnvQueryTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
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
	if (CVarEnemyEQSDebug.GetValueOnGameThread() == 0) return;
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
	Data.bSkipLossWait = IsValid(Data.Enemy) && Data.Enemy->HasFreshSharedTargetLocation();
	if (!IsValid(Data.Enemy) || !Data.Enemy->BeginSearchEpisode()) return EStateTreeRunStatus::Failed;
	Data.SearchCenter = Data.Enemy->GetSearchEpisode().Center;
	LogSearchEpisode(TEXT("Begin"), *Data.Enemy);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus TickSearchSession(FEnemySearchSessionTaskInstanceData& Data)
{
	if (!IsValid(Data.Enemy) || Data.Enemy->IsDead() || !Data.Enemy->CanUseEnemyPerception()
		|| Data.Enemy->GetCombatState() != EEnemyCombatState::Combat) return EStateTreeRunStatus::Failed;
	// 직접 재감지는 상위 전이가 처리한다. 부모 Task 완료가 그 전이를 덮어쓰지 않게 한다.
	if (Data.Enemy->IsPlayerCurrentlyVisible())
	{
		Data.bShouldRestart = false;
		return EStateTreeRunStatus::Running;
	}
	const FEnemySearchEpisode& Episode = Data.Enemy->GetSearchEpisode();
	if (!Episode.bActive) return EStateTreeRunStatus::Failed;
	// 부모 Task는 전이용 출력만 갱신한다. 새 정보로 회차를 교체하는 일은 RestartWithHint 자식이 맡는다.
	// 같은 공유 정보의 재전달이나 단순 LKP 갱신은 재시작 조건이 아니다.
	const bool bShouldRestart = Data.Enemy->GetSharedTargetRevision() > Episode.SharedRevision;
	if (bShouldRestart && !Data.bShouldRestart) LogSearchEpisode(TEXT("NewHintRestartRequested"), *Data.Enemy);
	Data.bShouldRestart = bShouldRestart;
	Data.bLocalGuard = Episode.bLocalGuard;
	Data.SearchCenter = Episode.Center;
	return EStateTreeRunStatus::Running;
}

void RefreshSearchSessionTransitions(FEnemySearchSessionTaskInstanceData& Data)
{
	// 자식 EnterState가 회차를 교체한 뒤 같은 프레임에 전이를 다시 평가할 수 있다.
	// 부모 Tick의 이전 출력으로 RestartHint에 재진입하지 않도록 조건 평가 전에 갱신한다.
	const bool bWasRestartRequested = Data.bShouldRestart;
	if (TickSearchSession(Data) == EStateTreeRunStatus::Failed) Data.bShouldRestart = false;
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
		LogSearchEpisode(TEXT("End"), *Data.Enemy);
		Data.Enemy->EndSearchEpisode();
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
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || Enemy->IsPlayerCurrentlyVisible()
		|| !Enemy->CanUseEnemyPerception() || !Enemy->GetSearchEpisode().bActive)
		return EStateTreeRunStatus::Failed;
	// 각 자식 State 진입 시 한 단계만 반영한다. SelectCurrentPoint는 조회만 하고,
	// PointChecked는 Move 성공 후 LookAround가 끝난 State에서 호출해야 한다.
	switch (Data.Action)
	{
	case EEnemySearchProgress::RestartWithHint:
		if (Enemy->GetSharedTargetRevision() <= Enemy->GetSearchEpisode().SharedRevision
			|| !Enemy->BeginSearchEpisode()) return EStateTreeRunStatus::Failed;
		break;
	case EEnemySearchProgress::AnchorArrived:
		Enemy->CompleteSearchAnchor();
		break;
	case EEnemySearchProgress::PointChecked:
		if (!Enemy->CompleteSearchPoint()) return EStateTreeRunStatus::Failed;
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
	if (!CanAcceptEnvQueryResult(Data, Result.QueryID)) return false;
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
