#include "Enemy/EnemyEnvQuery.h"

#include "Components/CapsuleComponent.h"
#include "Drone/FlightPathfinding.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "HAL/IConsoleManager.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogEnemyEQSCandidates, Log, All);

bool ShouldLogCandidateDetails()
{
	static const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.EQSDebug"));
	return Debug && Debug->GetInt() >= 2;
}

void LogGeneratorResult(const FEnvQueryInstance& Query, const AEnemyBase* Enemy, const AActor* Target,
	const TCHAR* Reason, int32 Attempted = 0, int32 Generated = 0, int32 NoFloor = 0)
{
	if (!ShouldLogCandidateDetails()) return;
	UE_LOG(LogEnemyEQSCandidates, Log,
		TEXT("[EnemyEQSGenerate] Enemy=%s Request=%d Target=%s RoomTag=%s Reason=%s Attempted=%d Generated=%d NoFloor=%d"),
		*GetNameSafe(Enemy), Query.QueryID, *GetNameSafe(Target),
		Enemy ? *Enemy->GetDefaultRoomTag().ToString() : TEXT("None"), Reason, Attempted, Generated, NoFloor);
}

bool IsDestinationCapsuleInsideRoom(const ARoomVolume& Room, const FVector& Position, const FVector& Extent)
{
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FVector Offset((Index & 1) ? Extent.X : -Extent.X,
			(Index & 2) ? Extent.Y : -Extent.Y, (Index & 4) ? Extent.Z : -Extent.Z);
		if (!Room.ContainsWorldLocation(Position + Offset)) return false;
	}
	return true;
}

void LogDestinationCheck(const FEnvQueryInstance& Query, int32 Index, const AEnemyBase* Enemy,
	const UCapsuleComponent* Capsule, const FFlightPathRequest& Request, EFlightPathStatus Status, bool bWantsValid)
{
	if (!ShouldLogCandidateDetails()) return;
	const ARoomVolume* Room = Request.Room.Get();
	const TCHAR* Reason = TEXT("InvalidEnemyOrCapsule");
	if (Status == EFlightPathStatus::Success) Reason = TEXT("Clear");
	else if (Capsule && Room && Status == EFlightPathStatus::BlockedEndpoint)
	{
		const float Radius = Capsule->GetScaledCapsuleRadius() + Request.Clearance;
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight() + Request.Clearance;
		const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(Radius, HalfHeight, Capsule->GetComponentQuat());
		// 충돌 Trace를 반복하지 않고 기존 검사 결과와 동일한 방 경계 계산으로 원인을 구분한다.
		Reason = IsDestinationCapsuleInsideRoom(*Room, Request.Goal, Extent)
			? TEXT("BlockingOverlap") : TEXT("OutsideRoom");
	}
	else if (!Room) Reason = TEXT("MissingInitialRoom");
	else if (Status == EFlightPathStatus::Unavailable) Reason = TEXT("CollisionOrWorldUnavailable");
	else if (Status == EFlightPathStatus::InvalidRequest) Reason = TEXT("InvalidRequest");
	UE_LOG(LogEnemyEQSCandidates, Log,
		TEXT("[EnemyEQSDestination] Enemy=%s Request=%d Item=%d RoomTag=%s Room=%s Goal=%s Status=%s Reason=%s BoolMatch=%d"),
		*GetNameSafe(Enemy), Query.QueryID, Index, Enemy ? *Enemy->GetDefaultRoomTag().ToString() : TEXT("None"),
		*GetNameSafe(Room), *Request.Goal.ToCompactString(), *UEnum::GetValueAsString(Status), Reason, bWantsValid);
}

FVector MakeApproachCandidate(const FVector& Center, const FVector& Radial, float Radius, float Degrees)
{
	return Center + Radial.RotateAngleAxis(Degrees, FVector::UpVector) * Radius;
}

bool CanProvideVisibleTarget(const AEnemyBase* Enemy)
{
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->CanUseEnemyPerception())
	{
		return false;
	}
	return !Enemy->IsDead() && Enemy->GetCombatState() != EEnemyCombatState::Stun
		&& Enemy->IsPlayerCurrentlyVisible();
}

AActor* GetVisibleEnvQueryTarget(const AEnemyBase* Enemy)
{
	if (!CanProvideVisibleTarget(Enemy))
	{
		return nullptr;
	}
	const AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	return Controller ? Controller->GetCachedVisibleTarget() : nullptr;
}

bool CanCheckFlightDestination(const AEnemyBase* Enemy, const UWorld* World, const UCapsuleComponent* Capsule)
{
	if (!Enemy || !Enemy->HasAuthority() || !World || !Capsule)
	{
		return false;
	}
	return !Enemy->IsDead() && Enemy->CanUseEnemyPerception();
}
}

void UEnemyEnvQueryContext_VisibleTarget::ProvideContext(
	FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	if (AActor* Target = GetVisibleEnvQueryTarget(Enemy))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, Target);
	}
}

UEnemyEnvQueryGenerator_Approach::UEnemyEnvQueryGenerator_Approach()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnemyEnvQueryGenerator_Approach::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	const AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	AActor* Target = GetVisibleEnvQueryTarget(Enemy);
	UWorld* World = QueryInstance.World;
	if (!Target || !World || !FMath::IsFinite(PreferredDistance) || !FMath::IsFinite(DistanceSpread)
		|| !FMath::IsFinite(HalfAngleDegrees) || !FMath::IsFinite(FlightHeight)
		|| !FMath::IsFinite(FloorProbeDistance) || FloorProbeDistance <= 0.0f)
	{
		LogGeneratorResult(QueryInstance, Enemy, Target, !Target ? TEXT("NoVisibleTarget") : TEXT("InvalidConfigurationOrWorld"));
		return;
	}
	const FVector Center = Target->GetActorLocation();
	const FVector Radial = (Enemy->GetActorLocation() - Center).GetSafeNormal2D();
	if (Radial.IsNearlyZero())
	{
		LogGeneratorResult(QueryInstance, Enemy, Target, TEXT("ZeroHorizontalDirection"));
		return;
	}
	const int32 Samples = FMath::Clamp(AngularSamples, 1, 9);
	const float Angle = FMath::Clamp(HalfAngleDegrees, 0.0f, 80.0f);
	const float Spread = FMath::Clamp(DistanceSpread, 0.0f, 500.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyApproachFloor), false, Enemy);
	Params.AddIgnoredActor(Target);
	int32 Generated = 0;
	int32 NoFloor = 0;
	// 거리 세 줄의 작은 부채꼴만 생성한다. 타깃 뒤편을 둘러싸는 전체 Grid는 만들지 않는다.
	for (int32 Ring = -1; Ring <= 1; ++Ring)
	{
		const float Radius = FMath::Clamp(PreferredDistance + Ring * Spread, 1000.0f, 2000.0f);
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float Degrees = Samples == 1 ? 0.0f : FMath::Lerp(-Angle, Angle, float(Index) / (Samples - 1));
			FVector Candidate = MakeApproachCandidate(Center, Radial, Radius, Degrees);
			FHitResult Floor;
			const FVector Probe(Candidate.X, Candidate.Y, FMath::Max(Center.Z, Enemy->GetActorLocation().Z));
			if (!World->LineTraceSingleByChannel(Floor, Probe, Probe - FVector::UpVector * FloorProbeDistance,
				ECC_WorldStatic, Params))
			{
				++NoFloor;
				continue;
			}
			Candidate.Z = Floor.ImpactPoint.Z + FMath::Max(FlightHeight, 0.0f);
			QueryInstance.AddItemData<UEnvQueryItemType_Point>(Candidate);
			++Generated;
		}
	}
	LogGeneratorResult(QueryInstance, Enemy, Target, TEXT("Completed"), Samples * 3, Generated, NoFloor);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyApproachCandidatesTest,
	"Outlier.Enemy.EQS.ApproachCandidates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyApproachCandidatesTest::RunTest(const FString& Parameters)
{
	const FVector Center(100, 200, 300);
	const FVector Radial = FVector::ForwardVector;
	for (float Radius : {1000.0f, 1500.0f, 2000.0f})
	{
		for (float Angle : {-80.0f, 0.0f, 80.0f})
		{
			const FVector Candidate = MakeApproachCandidate(Center, Radial, Radius, Angle);
			TestTrue(TEXT("Candidate preserves horizontal combat distance"),
				FMath::IsNearlyEqual(FVector::Dist2D(Center, Candidate), double(Radius), 0.01));
			TestTrue(TEXT("Candidate remains on drone side of target"), FVector::DotProduct(Candidate - Center, Radial) > 0.0);
		}
	}
	return true;
}
#endif

UEnemyEnvQueryTest_FlightDestination::UEnemyEnvQueryTest_FlightDestination()
{
	Cost = EEnvTestCost::High;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
	SetWorkOnFloatValues(false);
	TestPurpose = EEnvTestPurpose::Filter;
	BoolValue.DefaultValue = true;
}

void UEnemyEnvQueryTest_FlightDestination::RunTest(FEnvQueryInstance& QueryInstance) const
{
	// 한 검사 실행에서만 참조를 조회한다. 후보마다 Subsystem/Controller를 다시 검색하지 않는다.
	AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	UWorld* World = QueryInstance.World;
	UCapsuleComponent* Capsule = Enemy ? Enemy->GetCapsuleComponent() : nullptr;
	URoomCombatSubsystem* Rooms = World ? World->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	FFlightPathRequest Request;
	Request.Clearance = Clearance;
	Request.bRequireRoom = true;
	Request.Room = Rooms && Enemy ? Rooms->GetRegisteredRoomVolume(Enemy->GetDefaultRoomTag()) : nullptr;
	const FVector CapsuleOffset = Capsule && Enemy
		? Capsule->GetComponentLocation() - Enemy->GetActorLocation() : FVector::ZeroVector;
	BoolValue.BindData(QueryInstance.Owner.Get(), QueryInstance.QueryID);
	const bool bWantsValidDestination = BoolValue.GetValue();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		bool bClear = false;
		EFlightPathStatus Status = EFlightPathStatus::Unavailable;
		if (CanCheckFlightDestination(Enemy, World, Capsule))
		{
			Request.Start = Capsule->GetComponentLocation();
			Request.Goal = GetItemLocation(QueryInstance, It.GetIndex()) + CapsuleOffset;
			Request.SearchBounds = FBox(Request.Start, Request.Start);
			Request.SearchBounds += Request.Goal;
			Status = FlightPathfinding::CheckDestination(*World, *Capsule, Request);
			bClear = Status == EFlightPathStatus::Success;
		}
		LogDestinationCheck(QueryInstance, It.GetIndex(), Enemy, Capsule, Request, Status, bWantsValidDestination);
		It.SetScore(TestPurpose, FilterType, bClear, bWantsValidDestination);
	}
}
