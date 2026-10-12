#include "Enemy/EnemyStateTreeTacticalTasks.h"

#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Drone/FlightPathfinding.h"
#include "Enemy/EnemyAIController.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_ActorBase.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "HAL/IConsoleManager.h"
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
constexpr float MinimumTacticalFlightZ = 150.0f;

DEFINE_LOG_CATEGORY_STATIC(LogEnemyEQS, Log, All);
TAutoConsoleVariable<int32> CVarEnemyEQSDebug(
	TEXT("outlier.Enemy.EQSDebug"), 0,
	TEXT("Log enemy EQS request, result and cancellation. 0: off, 1: on."));

void LogEnvQuery(const TCHAR* Phase, const FEnemyRunEnvQueryTaskInstanceData& Data, int32 Items = 0)
{
	if (CVarEnemyEQSDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyEQS, Log,
			TEXT("[EnemyEQS] %s Enemy=%s Query=%s Request=%d Result=%s Items=%d HasDestination=%d Destination=%s"),
			Phase, *GetNameSafe(Data.Enemy), *GetNameSafe(Data.QueryTemplate), Data.RequestId,
			*UEnum::GetValueAsString(Data.QueryResult), Items, Data.bHasDestination,
			*Data.Destination.ToCompactString());
	}
}

bool IsEnvQueryConfigurationValid(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
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
	if (!Data.bRequireVisibleTarget)
	{
		return true;
	}
	return Data.Enemy->IsPlayerCurrentlyVisible() && Data.CapturedTarget.IsValid()
		&& Data.CachedController->GetCachedVisibleTarget() == Data.CapturedTarget.Get();
}

bool IsEnvQuerySnapshotCurrent(const FEnemyRunEnvQueryTaskInstanceData& Data)
{
	// 적 수명 -> 소유 컨트롤러 -> 초기 소속 방 -> 직접 목격 타깃 순서로 확인한다.
	// 앞 단계가 참조 유효성을 보장하므로 뒤 단계는 동일 참조를 다시 검색하지 않는다.
	return IsEnvQueryEnemyCurrent(Data)
		&& IsEnvQueryControllerCurrent(Data)
		&& IsEnvQueryRoomCurrent(Data)
		&& IsEnvQueryTargetCurrent(Data);
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
	Data.CapturedGeneration = Enemy->GetPoolGameplayGeneration();
	Data.CapturedLease = Enemy->GetPoolLeaseSerial();
	Data.CapturedCombatState = Enemy->GetCombatState();
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

void ApplyEnvQueryResult(FEnemyRunEnvQueryTaskInstanceData& Data, const FEnvQueryResult& Result)
{
	Data.RequestId = INDEX_NONE;
	Data.QueryResult = EEnemyEnvQueryResult::Failed;
	// 요청 당시 상태 유지 -> 시간 제한 -> 결과 형식 -> 목적지 안전 검사 후에만 출력을 기록한다.
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
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
		return;
	}
	if (!IsLocationEnvQueryResult(Result))
	{
		return;
	}
	// AllMatching도 이동 목적지는 하나만 출력한다. 여러 지점 수색은 별도 행동 Slice에서 처리한다.
	const FVector Destination = Result.GetItemAsLocation(0);
	if (!IsEnvQueryDestinationClear(Data, Destination))
	{
		return;
	}
	Data.Destination = Destination;
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
	if (RequestId != INDEX_NONE && IsValid(Data.Enemy))
	{
		if (UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Data.Enemy))
		{
			Manager->AbortQuery(RequestId);
		}
	}
	LogEnvQuery(TEXT("Cancel"), Data);
}

EStateTreeRunStatus StartEnvQuery(FStateTreeExecutionContext& Context,
	FEnemyRunEnvQueryTaskInstanceData& Data)
{
	const double Now = Data.Enemy->GetWorld()->GetTimeSeconds();
	if (Now < Data.Enemy->GetNextEnvQueryRequestTime())
	{
		Data.QueryResult = EEnemyEnvQueryResult::Waiting;
		return EStateTreeRunStatus::Running;
	}
	// 간격은 Pawn에 보관해 실패 후 같은 State로 재진입해도 요청이 한 프레임에 반복되지 않게 한다.
	Data.Enemy->SetNextEnvQueryRequestTime(Now + Data.QueryInterval);
	Data.QueryStartedAt = Now;
	Data.QueryResult = EEnemyEnvQueryResult::Running;
	FEnvQueryRequest Request(Data.QueryTemplate, Data.Enemy);
	for (FAIDynamicParam& Param : Data.QueryConfig)
	{
		Request.SetDynamicParam(Param, nullptr);
	}
	Data.RequestId = Request.Execute(Data.RunMode,
		FQueryFinishedSignature::CreateLambda([WeakContext = Context.MakeWeakExecutionContext()]
			(TSharedPtr<FEnvQueryResult> Result) mutable
		{
			const FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();
			FEnemyRunEnvQueryTaskInstanceData* Pending =
				StrongContext.GetInstanceDataPtr<FEnemyRunEnvQueryTaskInstanceData>();
			if (!Pending || !Result || !CanAcceptEnvQueryResult(*Pending, Result->QueryID))
			{
				return;
			}
			ApplyEnvQueryResult(*Pending, *Result);
			const bool bSuccess = Pending->bHasDestination;
			LogEnvQuery(TEXT("Finished"), *Pending, Result->Items.Num());
			StrongContext.FinishTask(bSuccess ? EStateTreeFinishTaskType::Succeeded : EStateTreeFinishTaskType::Failed);
		}));
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
	Data.QueryResult = EEnemyEnvQueryResult::Invalid;
	Data.RequestId = INDEX_NONE;
	Data.bActive = true;
	AEnemyBase* Enemy = Data.Enemy;
	Data.Destination = IsValid(Enemy) ? Enemy->GetActorLocation() : FVector::ZeroVector;
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
	if (!IsEnvQuerySnapshotCurrent(Data))
	{
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
	return EStateTreeRunStatus::Running;
}

void FEnemyRunEnvQueryTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
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

#if WITH_DEV_AUTOMATION_TESTS
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
