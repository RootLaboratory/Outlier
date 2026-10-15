#include "Enemy/EnemyEnvQuery.h"

#include "Components/CapsuleComponent.h"
#include "Drone/FlightPathfinding.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQueryManager.h"
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

const FName HeightCandidateCount(TEXT("FlightHeightCandidateCount"));

FName HeightCandidateParam(int32 Index, const TCHAR* Field)
{
	return FName(*FString::Printf(TEXT("FlightHeight%d%s"), Index, Field));
}

bool AddHeightSnapshotItems(FEnvQueryInstance& Query)
{
	const float* Count = Query.NamedParams.Find(HeightCandidateCount);
	if (!Count) return false;
	// 스냅샷이 손상됐을 때 현재 Pawn 좌표로 재생성하지 않는다.
	if (!FMath::IsFinite(*Count) || *Count < 0.0f || *Count > 81.0f || *Count != FMath::FloorToFloat(*Count)) return true;
	for (int32 Index = 0; Index < int32(*Count); ++Index)
	{
		const float* X = Query.NamedParams.Find(HeightCandidateParam(Index, TEXT("X")));
		const float* Y = Query.NamedParams.Find(HeightCandidateParam(Index, TEXT("Y")));
		const float* Z = Query.NamedParams.Find(HeightCandidateParam(Index, TEXT("Z")));
		if (X && Y && Z && FMath::IsFinite(*X) && FMath::IsFinite(*Y) && FMath::IsFinite(*Z))
			Query.AddItemData<UEnvQueryItemType_Point>(FVector(*X, *Y, *Z));
	}
	return true;
}

bool GenerateHeightCandidates(FEnvQueryInstance& Query, const AEnemyBase* Enemy, const UEnvQueryGenerator& Generator,
	const FVector& Start, const FVector& Target, float Length = 400.0f, bool bLeft = false, int32 Lane = 0)
{
	if (AddHeightSnapshotItems(Query)) return true;
	if (!Enemy || !Enemy->FlightHeightSettings.bEnabled) return false;
	// Testing Pawn/직접 EQS 실행도 같은 계산을 사용한다. StateTree 요청은 위의 고정 결과만 읽는다.
	FEnemyFlightHeightSnapshot Snapshot;
	const EFlightPathStatus Status = EnemyFlightHeight::PrepareQuery(*Enemy, Generator, Start, Target,
		Length, bLeft, Lane, 5.0f, 512, 4.0, Snapshot);
	if (Status == EFlightPathStatus::Success)
	{
		for (const FEnemyFlightHeightCandidate& Candidate : Snapshot.Candidates)
			if (Candidate.Status == EFlightPathStatus::Success) Query.AddItemData<UEnvQueryItemType_Point>(Candidate.Location);
	}
	return true;
}

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

const FName OrbitStartX(TEXT("OrbitStartX"));
const FName OrbitStartY(TEXT("OrbitStartY"));
const FName OrbitStartZ(TEXT("OrbitStartZ"));
const FName OrbitCenterX(TEXT("OrbitCenterX"));
const FName OrbitCenterY(TEXT("OrbitCenterY"));
const FName OrbitCenterZ(TEXT("OrbitCenterZ"));
const FName OrbitLength(TEXT("OrbitLength"));
const FName OrbitLeft(TEXT("OrbitLeft"));
const FName RetreatStartX(TEXT("RetreatStartX"));
const FName RetreatStartY(TEXT("RetreatStartY"));
const FName RetreatStartZ(TEXT("RetreatStartZ"));
const FName RetreatCenterX(TEXT("RetreatCenterX"));
const FName RetreatCenterY(TEXT("RetreatCenterY"));
const FName RetreatCenterZ(TEXT("RetreatCenterZ"));

FVector MakeOrbitPoint(const FVector& Start, const FVector& Center, float Length, bool bLeft)
{
	FVector Radial = Start - Center;
	Radial.Z = 0.0;
	// 플레이어를 바라보는 드론 기준 Left는 양의 Z축 회전이다.
	const float Degrees = FMath::RadiansToDegrees(Length / Radial.Size()) * (bLeft ? 1.0f : -1.0f);
	FVector Point = Center + Radial.RotateAngleAxis(Degrees, FVector::UpVector);
	Point.Z = Start.Z;
	return Point;
}
}

bool EnemyOrbit::IsCombatRadius(const FVector& Start, const FVector& Center)
{
	if (Start.ContainsNaN() || Center.ContainsNaN()) return false;
	const double Radius = FVector::Dist2D(Start, Center);
	return Radius > 1000.0 && Radius <= 2000.0;
}

namespace
{
const FName SearchCenterX(TEXT("SearchCenterX"));
const FName SearchCenterY(TEXT("SearchCenterY"));
const FName SearchCenterZ(TEXT("SearchCenterZ"));
const FName SearchFlightHeight(TEXT("SearchFlightHeight"));
}

void EnemySearch::SetQuerySnapshot(FEnvQueryRequest& Request, const FVector& Center, float FlightHeight)
{
	Request.SetFloatParam(SearchCenterX, Center.X).SetFloatParam(SearchCenterY, Center.Y)
		.SetFloatParam(SearchCenterZ, Center.Z).SetFloatParam(SearchFlightHeight, FlightHeight);
}

bool EnemySearch::IsCandidateInRange(const FVector& Candidate, const FVector& Center,
	float FlightHeight, EEnemySearchQueryMode Mode)
{
	if (Candidate.ContainsNaN() || Center.ContainsNaN() || !FMath::IsFinite(FlightHeight)) return false;
	const double Radius = FVector::Dist2D(Candidate, Center);
	const bool bRadiusValid = Mode == EEnemySearchQueryMode::Anchor ? Radius <= 400.01
		: Mode == EEnemySearchQueryMode::Points && Radius >= 499.99 && Radius <= 800.01;
	return bRadiusValid && FMath::Abs(Candidate.Z - FlightHeight) <= 100.01;
}

void EnemySearch::BuildCandidates(const FVector& Center, float FlightHeight,
	EEnemySearchQueryMode Mode, TArray<FVector>& OutCandidates)
{
	OutCandidates.Reset();
	if (Center.ContainsNaN() || !FMath::IsFinite(FlightHeight) || Mode == EEnemySearchQueryMode::None) return;
	// 마지막 목격 위치는 먼저 같은 XY의 안전 비행 높이로 접근한다. 막혔을 때만 주변을 보정한다.
	if (Mode == EEnemySearchQueryMode::Anchor) OutCandidates.Add(FVector(Center.X, Center.Y, FlightHeight));
	for (int32 Ring = 0; Ring < 3; ++Ring)
	{
		const float Radius = Mode == EEnemySearchQueryMode::Anchor ? 100.0f * (1 << Ring) : 500.0f + Ring * 150.0f;
		for (float HeightOffset : {0.0f, -100.0f, 100.0f})
		{
			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				FVector Point = Center + FVector::ForwardVector.RotateAngleAxis(Direction * 45.0f, FVector::UpVector) * Radius;
				Point.Z = FlightHeight + HeightOffset;
				OutCandidates.Add(Point);
			}
		}
	}
}

UEnemyEnvQueryGenerator_Search::UEnemyEnvQueryGenerator_Search()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnemyEnvQueryGenerator_Search::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	const AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	if (!Enemy || !Enemy->GetSearchEpisode().bActive || Enemy->IsPlayerCurrentlyVisible()
		|| !QueryInstance.NamedParams.Contains(SearchCenterX)
		|| !QueryInstance.NamedParams.Contains(SearchCenterY)
		|| !QueryInstance.NamedParams.Contains(SearchCenterZ)
		|| !QueryInstance.NamedParams.Contains(SearchFlightHeight))
	{
		LogGeneratorResult(QueryInstance, Enemy, nullptr, TEXT("MissingSearchSnapshot"));
		return;
	}
	const FVector Center(QueryInstance.NamedParams.FindRef(SearchCenterX), QueryInstance.NamedParams.FindRef(SearchCenterY),
		QueryInstance.NamedParams.FindRef(SearchCenterZ));
	TArray<FVector> Candidates;
	EnemySearch::BuildCandidates(Center, QueryInstance.NamedParams.FindRef(SearchFlightHeight), Mode, Candidates);
	for (const FVector& Point : Candidates) QueryInstance.AddItemData<UEnvQueryItemType_Point>(Point);
	LogGeneratorResult(QueryInstance, Enemy, nullptr, TEXT("SearchCompleted"), Candidates.Num(), Candidates.Num());
}

void EnemyRetreat::BuildCandidates(const FVector& Start, const FVector& Center, TArray<FVector>& OutCandidates)
{
	OutCandidates.Reset();
	if (Start.ContainsNaN() || Center.ContainsNaN()) return;
	const FVector Away = (Start - Center).GetSafeNormal2D();
	if (Away.IsNearlyZero() || FVector::Dist2D(Start, Center) > 1000.0) return;
	// 10m 경계/도착 허용 반경을 피한 세 거리에서 직후방부터 확인한다.
	// 사선 후보도 현재 위치보다 뒤에 있어야 한다. 고도는 현재 드론 높이를 유지한다.
	for (float Distance : {1250.0f, 1100.0f, 1400.0f})
	{
		for (float Degrees : {0.0f, -30.0f, 30.0f})
		{
			FVector Candidate = MakeApproachCandidate(Center, Away, Distance, Degrees);
			Candidate.Z = Start.Z;
			if (FVector::DotProduct(Candidate - Start, Away) >= 25.0) OutCandidates.Add(Candidate);
		}
	}
}

void EnemyRetreat::SetQuerySnapshot(FEnvQueryRequest& Request, const FVector& Start, const FVector& Center)
{
	Request.SetFloatParam(RetreatStartX, Start.X).SetFloatParam(RetreatStartY, Start.Y).SetFloatParam(RetreatStartZ, Start.Z);
	Request.SetFloatParam(RetreatCenterX, Center.X).SetFloatParam(RetreatCenterY, Center.Y).SetFloatParam(RetreatCenterZ, Center.Z);
}

UEnemyEnvQueryGenerator_Retreat::UEnemyEnvQueryGenerator_Retreat()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnemyEnvQueryGenerator_Retreat::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	const AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	const AActor* Target = GetVisibleEnvQueryTarget(Enemy);
	if (!Target) return;
	FVector Start = Enemy->GetActorLocation();
	FVector Center = Target->GetActorLocation();
	if (QueryInstance.NamedParams.Contains(RetreatStartX))
	{
		Start = FVector(QueryInstance.NamedParams.FindRef(RetreatStartX), QueryInstance.NamedParams.FindRef(RetreatStartY),
			QueryInstance.NamedParams.FindRef(RetreatStartZ));
		Center = FVector(QueryInstance.NamedParams.FindRef(RetreatCenterX), QueryInstance.NamedParams.FindRef(RetreatCenterY),
			QueryInstance.NamedParams.FindRef(RetreatCenterZ));
	}
	TArray<FVector> Candidates;
	if (GenerateHeightCandidates(QueryInstance, Enemy, *this, Start, Center)) return;
	EnemyRetreat::BuildCandidates(Start, Center, Candidates);
	for (const FVector& Candidate : Candidates) QueryInstance.AddItemData<UEnvQueryItemType_Point>(Candidate);
	LogGeneratorResult(QueryInstance, Enemy, Target, TEXT("RetreatCompleted"), Candidates.Num(), Candidates.Num());
}

void EnemyOrbit::BuildCandidates(const FVector& Start, const FVector& Center,
	float PreferredLength, bool bPrefersLeft, TArray<FEnemyOrbitCandidate>& OutCandidates)
{
	OutCandidates.Reset();
	if (!IsCombatRadius(Start, Center) || !FMath::IsFinite(PreferredLength)) return;
	const float Length = FMath::Clamp(PreferredLength, 300.0f, 500.0f);
	// 선호 구간 -> 같은 방향 3m/1.5m -> 반대 방향 동일 순서. 점수로 이 순서를 뒤집지 않는다.
	for (bool bLeft : {bPrefersLeft, !bPrefersLeft})
	{
		for (float ArcLength : {Length, 300.0f, 150.0f})
		{
			if (ArcLength == 300.0f && Length == 300.0f
				&& !OutCandidates.IsEmpty() && OutCandidates.Last().bLeft == bLeft) continue;
			FEnemyOrbitCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
			Candidate.ArcLength = ArcLength;
			Candidate.bLeft = bLeft;
			Candidate.Destination = MakeOrbitPoint(Start, Center, ArcLength, bLeft);
		}
	}
}

void EnemyOrbit::BuildWaypoints(const FVector& Start, const FVector& Center,
	const FEnemyOrbitCandidate& Candidate, TArray<FVector>& OutWaypoints)
{
	OutWaypoints.Reset();
	if (!IsCombatRadius(Start, Center) || !FMath::IsFinite(Candidate.ArcLength)
		|| Candidate.Destination.ContainsNaN() || Candidate.ArcLength <= 0.0f || Candidate.ArcLength > 500.0f) return;
	const double SpatialLength = FMath::Sqrt(FMath::Square(double(Candidate.ArcLength))
		+ FMath::Square(Candidate.Destination.Z - Start.Z));
	if (SpatialLength > 500.01) return;
	// 높이 변화까지 최대 50cm 간격으로 나눈다. Fly도 동일한 곡선을 끊김 없이 따라간다.
	const int32 Segments = FMath::Min(10, FMath::CeilToInt(SpatialLength / 50.0));
	OutWaypoints.Reserve(Segments + 1);
	OutWaypoints.Add(Start);
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		FVector Point = MakeOrbitPoint(Start, Center, Candidate.ArcLength * Index / Segments, Candidate.bLeft);
		Point.Z = FMath::Lerp(Start.Z, Candidate.Destination.Z, double(Index) / Segments);
		OutWaypoints.Add(Point);
	}
}

void EnemyOrbit::SetQuerySnapshot(FEnvQueryRequest& Request, const FVector& Start,
	const FVector& Center, float PreferredLength, bool bPrefersLeft)
{
	Request.SetFloatParam(OrbitStartX, Start.X).SetFloatParam(OrbitStartY, Start.Y).SetFloatParam(OrbitStartZ, Start.Z);
	Request.SetFloatParam(OrbitCenterX, Center.X).SetFloatParam(OrbitCenterY, Center.Y).SetFloatParam(OrbitCenterZ, Center.Z);
	Request.SetFloatParam(OrbitLength, PreferredLength).SetFloatParam(OrbitLeft, bPrefersLeft ? 1.0f : 0.0f);
}

namespace
{
const FName ChaseStartX(TEXT("ChaseStartX"));
const FName ChaseStartY(TEXT("ChaseStartY"));
const FName ChaseStartZ(TEXT("ChaseStartZ"));
const FName ChaseTargetX(TEXT("ChaseTargetX"));
const FName ChaseTargetY(TEXT("ChaseTargetY"));
const FName ChaseTargetZ(TEXT("ChaseTargetZ"));
const FName ChaseLane(TEXT("ChaseLane"));
}

void EnemyChase::BuildCandidates(const FVector& Start, const FVector& Target, int32 Lane, TArray<FVector>& OutCandidates)
{
	OutCandidates.Reset();
	if (Start.ContainsNaN() || Target.ContainsNaN() || Lane < -1 || Lane > 1) return;
	FVector Toward = (Target - Start).GetSafeNormal2D();
	if (Toward.IsNearlyZero()) Toward = FVector::ForwardVector;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Toward);
	const float Blend = FMath::Clamp(float(FVector::Dist2D(Start, Target) - 300.0) / 700.0f, 0.0f, 1.0f);
	const float HeightBias = FMath::Clamp(float(Start.Z - Target.Z), -150.0f, 150.0f) * Blend;
	const int32 Alternative = Lane == 0 ? -1 : 0;
	const int32 Opposite = Lane == 0 ? 1 : -Lane;
	// 각 거리/높이에서 선호 성향 -> 중앙 -> 반대 성향을 확인한다. 작은 후보 한도로도 양쪽을 검사할 수 있다.
	// 3~4.5m 접근점은 타깃 캡슐과 겹치지 않으면서 5m 돌진 진입을 검사할 수 있는 초기 튜닝값이다.
	for (float Radius : {350.0f, 300.0f, 450.0f})
	{
		for (float HeightScale : {1.0f, 0.5f, 0.0f})
		{
			for (int32 CandidateLane : {Lane, Alternative, Opposite})
			{
				FVector Candidate = Target - Toward * Radius + Right * (CandidateLane * 150.0f * Blend);
				Candidate.Z = Target.Z + HeightBias * HeightScale;
				OutCandidates.AddUnique(Candidate);
			}
		}
	}
}

void EnemyChase::SetQuerySnapshot(FEnvQueryRequest& Request, const FVector& Start, const FVector& Target, int32 Lane)
{
	Request.SetFloatParam(ChaseStartX, Start.X).SetFloatParam(ChaseStartY, Start.Y).SetFloatParam(ChaseStartZ, Start.Z);
	Request.SetFloatParam(ChaseTargetX, Target.X).SetFloatParam(ChaseTargetY, Target.Y).SetFloatParam(ChaseTargetZ, Target.Z);
	Request.SetFloatParam(ChaseLane, float(Lane));
}

UEnemyEnvQueryGenerator_Chase::UEnemyEnvQueryGenerator_Chase()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnemyEnvQueryGenerator_Chase::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	const AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	const AActor* Target = GetVisibleEnvQueryTarget(Enemy);
	if (!Target)
	{
		LogGeneratorResult(QueryInstance, Enemy, Target, TEXT("ChaseNoVisibleTarget"));
		return;
	}
	FVector Start = Enemy->GetActorLocation();
	FVector Center = Target->GetActorLocation();
	int32 Lane = Enemy->GetCombatChaseLane();
	if (QueryInstance.NamedParams.Contains(ChaseLane))
	{
		// 질의가 여러 프레임에 걸쳐 실행되어도 후보와 A* 검증은 요청 당시의 동일 좌표를 사용한다.
		Start = FVector(QueryInstance.NamedParams.FindRef(ChaseStartX), QueryInstance.NamedParams.FindRef(ChaseStartY), QueryInstance.NamedParams.FindRef(ChaseStartZ));
		Center = FVector(QueryInstance.NamedParams.FindRef(ChaseTargetX), QueryInstance.NamedParams.FindRef(ChaseTargetY), QueryInstance.NamedParams.FindRef(ChaseTargetZ));
		const float LaneValue = QueryInstance.NamedParams.FindRef(ChaseLane);
		if (!FMath::IsFinite(LaneValue) || LaneValue < -1.0f || LaneValue > 1.0f) return;
		Lane = FMath::RoundToInt(LaneValue);
	}
	TArray<FVector> Candidates;
	if (GenerateHeightCandidates(QueryInstance, Enemy, *this, Start, Center, 0.0f, false, Lane)) return;
	EnemyChase::BuildCandidates(Start, Center, Lane, Candidates);
	for (const FVector& Candidate : Candidates) QueryInstance.AddItemData<UEnvQueryItemType_Point>(Candidate);
	LogGeneratorResult(QueryInstance, Enemy, Target, TEXT("ChaseCompleted"), Candidates.Num(), Candidates.Num());
}

UEnemyEnvQueryGenerator_Orbit::UEnemyEnvQueryGenerator_Orbit()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnemyEnvQueryGenerator_Orbit::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	const AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	const AActor* Target = GetVisibleEnvQueryTarget(Enemy);
	if (!Target) return;
	FVector Start = Enemy->GetActorLocation();
	FVector Center = Target->GetActorLocation();
	float Length = 400.0f;
	bool bLeft = Enemy->PrefersCombatLeft();
	if (QueryInstance.NamedParams.Contains(OrbitLength))
	{
		// 비동기 EQS 실행 중 위치가 바뀌어도 요청 당시의 곡선 후보를 유지한다.
		Start = FVector(QueryInstance.NamedParams.FindRef(OrbitStartX), QueryInstance.NamedParams.FindRef(OrbitStartY),
			QueryInstance.NamedParams.FindRef(OrbitStartZ));
		Center = FVector(QueryInstance.NamedParams.FindRef(OrbitCenterX), QueryInstance.NamedParams.FindRef(OrbitCenterY),
			QueryInstance.NamedParams.FindRef(OrbitCenterZ));
		Length = QueryInstance.NamedParams.FindRef(OrbitLength);
		bLeft = QueryInstance.NamedParams.FindRef(OrbitLeft) != 0.0f;
	}
	TArray<FEnemyOrbitCandidate> Candidates;
	if (GenerateHeightCandidates(QueryInstance, Enemy, *this, Start, Center, Length, bLeft)) return;
	EnemyOrbit::BuildCandidates(Start, Center, Length, bLeft, Candidates);
	for (const FEnemyOrbitCandidate& Candidate : Candidates)
	{
		QueryInstance.AddItemData<UEnvQueryItemType_Point>(Candidate.Destination);
	}
	LogGeneratorResult(QueryInstance, Enemy, Target, TEXT("OrbitCompleted"), Candidates.Num(), Candidates.Num());
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
	if (GenerateHeightCandidates(QueryInstance, Enemy, *this, Enemy->GetActorLocation(), Center)) return;
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

namespace
{
bool BuildHeightSourceCandidates(const UEnvQueryGenerator& Generator, const FVector& Start, const FVector& Target,
	float Length, bool bLeft, int32 Lane, TArray<FVector>& OutCandidates)
{
	if (Generator.IsA<UEnemyEnvQueryGenerator_Orbit>())
	{
		TArray<FEnemyOrbitCandidate> Orbit;
		EnemyOrbit::BuildCandidates(Start, Target, Length, bLeft, Orbit);
		for (const FEnemyOrbitCandidate& Candidate : Orbit) OutCandidates.Add(Candidate.Destination);
	}
	else if (Generator.IsA<UEnemyEnvQueryGenerator_Retreat>())
		EnemyRetreat::BuildCandidates(Start, Target, OutCandidates);
	else if (Generator.IsA<UEnemyEnvQueryGenerator_Chase>())
	{
		TArray<FVector> Chase;
		EnemyChase::BuildCandidates(Start, Target, Lane, Chase);
		// 기존 높이 편차 후보는 안전 높이 탐색으로 대체한다. 같은 XY를 세 번 Trace하지 않는다.
		for (FVector Candidate : Chase)
		{
			Candidate.Z = Target.Z;
			OutCandidates.AddUnique(Candidate);
		}
	}
	else if (const UEnemyEnvQueryGenerator_Approach* Approach = Cast<UEnemyEnvQueryGenerator_Approach>(&Generator))
	{
		if (!FMath::IsFinite(Approach->PreferredDistance) || !FMath::IsFinite(Approach->DistanceSpread)
			|| !FMath::IsFinite(Approach->HalfAngleDegrees)) return false;
		const FVector Radial = (Start - Target).GetSafeNormal2D();
		if (Radial.IsNearlyZero()) return false;
		const int32 Samples = FMath::Clamp(Approach->AngularSamples, 1, 9);
		const float Angle = FMath::Clamp(Approach->HalfAngleDegrees, 0.0f, 80.0f);
		for (int32 Ring = -1; Ring <= 1; ++Ring)
		{
			const float Radius = FMath::Clamp(Approach->PreferredDistance
				+ Ring * FMath::Clamp(Approach->DistanceSpread, 0.0f, 500.0f), 1000.0f, 2000.0f);
			for (int32 Index = 0; Index < Samples; ++Index)
			{
				const float Degrees = Samples == 1 ? 0.0f : FMath::Lerp(-Angle, Angle, float(Index) / (Samples - 1));
				OutCandidates.Add(MakeApproachCandidate(Target, Radial, Radius, Degrees));
			}
		}
	}
	else return false;
	return true;
}

bool HasHeightBudget(const FEnemyFlightHeightSnapshot& Snapshot, int32 MaxChecks, double Started, double MaxMilliseconds)
{
	return Snapshot.CollisionChecks < MaxChecks && (FPlatformTime::Seconds() - Started) * 1000.0 < MaxMilliseconds;
}

EFlightPathStatus ResolveCandidateHeight(const AEnemyBase& Enemy, const UCapsuleComponent& Capsule, ARoomVolume& Room,
	const FVector& Start, const FVector& Target, bool bChase, float Clearance, int32 MaxChecks,
	double Started, double MaxMilliseconds, FEnemyFlightHeightSnapshot& Snapshot, FEnemyFlightHeightCandidate& OutCandidate)
{
	FVector& Candidate = OutCandidate.Location;
	OutCandidate.Reason = TEXT("BudgetExceeded");
	if (!HasHeightBudget(Snapshot, MaxChecks, Started, MaxMilliseconds)) return EFlightPathStatus::BudgetExceeded;
	const FEnemyFlightHeightSettings& Settings = Snapshot.Settings;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyFlightHeightFloor), false, &Enemy);
	// Object 검사에서는 채널 응답과 관계없이 방 트리거가 잡힐 수 있다. 지형과 분리한다.
	Params.AddIgnoredActor(&Room);
	Params.AddIgnoredActors(Capsule.GetMoveIgnoreActors());
	Params.AddIgnoredComponents(Capsule.GetMoveIgnoreComponents());
	// 현재 비행 층에서 아래쪽 바닥만 찾는다. 천장 위에서 내려 쏴 천장을 바닥으로 선택하지 않는다.
	const FVector Probe(Candidate.X, Candidate.Y, Start.Z);
	FHitResult Floor;
	const FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	++Snapshot.CollisionChecks;
	if (!Enemy.GetWorld()->LineTraceSingleByObjectType(Floor, Probe,
		Probe - FVector::UpVector * Settings.FloorProbeDistance, FloorObjects, Params)
		|| Floor.ImpactNormal.Z < 0.5f)
	{
		OutCandidate.Reason = TEXT("NoFloorInCurrentLayer");
		return EFlightPathStatus::Unavailable;
	}

	const FVector Offset = Capsule.GetComponentLocation() - Enemy.GetActorLocation();
	const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(Capsule.GetScaledCapsuleRadius() + Clearance,
		Capsule.GetScaledCapsuleHalfHeight() + Clearance, Capsule.GetComponentQuat());
	const double MinimumZ = Floor.ImpactPoint.Z + Extent.Z - Offset.Z + 1.0;
	double DesiredZ = Floor.ImpactPoint.Z + Settings.GetHeight(Snapshot.Preference);
	if (bChase)
	{
		// 근접 시 타깃 높이에 수렴하되 캡슐이 바닥을 관통하는 높이는 허용하지 않는다.
		const double Blend = FMath::Clamp((FVector::Dist2D(Start, Target) - 300.0) / 700.0, 0.0, 1.0);
		DesiredZ = FMath::Lerp(Target.Z, DesiredZ, Blend);
	}
	if (!HasHeightBudget(Snapshot, MaxChecks, Started, MaxMilliseconds)) return EFlightPathStatus::BudgetExceeded;
	FHitResult Ceiling;
	const FVector CeilingStart(Candidate.X, Candidate.Y, Floor.ImpactPoint.Z + 1.0);
	const double ProbeTop = FMath::Max(DesiredZ, MinimumZ) + Offset.Z + Extent.Z
		+ Settings.CorrectionStep * Settings.CorrectionSteps + 1.0;
	++Snapshot.CollisionChecks;
	const bool bHasCeiling = Enemy.GetWorld()->LineTraceSingleByObjectType(Ceiling, CeilingStart,
		FVector(Candidate.X, Candidate.Y, ProbeTop), FloorObjects, Params);
	const double MaximumZ = bHasCeiling ? Ceiling.ImpactPoint.Z - Extent.Z - Offset.Z - 1.0 : ProbeTop;
	if (MaximumZ < MinimumZ)
	{
		OutCandidate.Reason = TEXT("InsufficientVerticalSpace");
		return EFlightPathStatus::BlockedEndpoint;
	}
	// 좁은 공간의 유효 높이가 50cm 표본 사이에 있어도 놓치지 않도록 먼저 수직 여유 범위로 보정한다.
	DesiredZ = FMath::Clamp(DesiredZ, MinimumZ, MaximumZ);
	FFlightPathRequest Request;
	Request.bRequireRoom = true;
	Request.Room = &Room;
	Request.Clearance = Clearance;
	Request.Start = Capsule.GetComponentLocation();

	// 선호 높이 -> 가까운 아래/위 순서의 작은 집합만 검사한다. 우회 경로는 이후 A*의 책임이다.
	for (int32 Step = 0; Step <= Settings.CorrectionSteps; ++Step)
	{
		for (int32 Sign : {-1, 1})
		{
			if (Step == 0 && Sign == 1) continue;
			const double Height = DesiredZ + Sign * Step * Settings.CorrectionStep;
			if (Height < MinimumZ || Height > MaximumZ) continue;
			if (!HasHeightBudget(Snapshot, MaxChecks, Started, MaxMilliseconds)) return EFlightPathStatus::BudgetExceeded;
			Request.Goal = FVector(Candidate.X, Candidate.Y, Height) + Offset;
			Request.SearchBounds = FBox(Request.Start, Request.Start);
			Request.SearchBounds += Request.Goal;
			++Snapshot.CollisionChecks;
			const EFlightPathStatus Status = FlightPathfinding::CheckDestination(*Enemy.GetWorld(), Capsule, Request);
			if (Status == EFlightPathStatus::Success)
			{
				Candidate.Z = Height;
				OutCandidate.Reason = TEXT("SafeHeight");
				return Status;
			}
			if (Status != EFlightPathStatus::BlockedEndpoint)
			{
				OutCandidate.Reason = TEXT("DestinationCheckUnavailable");
				return Status;
			}
		}
	}
	OutCandidate.Reason = TEXT("NoClearHeightOrOutsideRoom");
	return EFlightPathStatus::BlockedEndpoint;
}
}

EFlightPathStatus EnemyFlightHeight::PrepareQuery(const AEnemyBase& Enemy, const UEnvQueryGenerator& Generator,
	const FVector& Start, const FVector& Target, float PreferredOrbitLength, bool bLeft, int32 Lane,
	float Clearance, int32 MaxChecks, double MaxMilliseconds, FEnemyFlightHeightSnapshot& OutSnapshot)
{
	OutSnapshot = FEnemyFlightHeightSnapshot();
	OutSnapshot.bEnabled = Enemy.FlightHeightSettings.bEnabled;
	if (!OutSnapshot.bEnabled) return EFlightPathStatus::Success;
	OutSnapshot.Settings = Enemy.FlightHeightSettings;
	OutSnapshot.Preference = Enemy.GetCombatHeightPreference();
	const UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	if (!Enemy.HasAuthority() || !Enemy.GetWorld() || !Capsule || !OutSnapshot.Settings.IsValid()
		|| Start.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(Clearance) || Clearance < 0.0f
		|| MaxChecks <= 0 || !FMath::IsFinite(MaxMilliseconds) || MaxMilliseconds <= 0.0)
		return EFlightPathStatus::InvalidRequest;
	const URoomCombatSubsystem* Rooms = Enemy.GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	ARoomVolume* Room = Rooms ? Rooms->GetRegisteredRoomVolume(Enemy.GetDefaultRoomTag()) : nullptr;
	if (!Room) return EFlightPathStatus::Unavailable;
	TArray<FVector> Sources;
	if (!BuildHeightSourceCandidates(Generator, Start, Target, PreferredOrbitLength, bLeft, Lane, Sources))
		return EFlightPathStatus::InvalidRequest;
	const double Started = FPlatformTime::Seconds();
	EFlightPathStatus Result = EFlightPathStatus::Success;
	for (FVector Source : Sources)
	{
		FEnemyFlightHeightCandidate& Candidate = OutSnapshot.Candidates.AddDefaulted_GetRef();
		Candidate.Location = Source;
		Candidate.Status = ResolveCandidateHeight(Enemy, *Capsule, *Room, Start, Target,
			Generator.IsA<UEnemyEnvQueryGenerator_Chase>(), Clearance, MaxChecks, Started, MaxMilliseconds,
			OutSnapshot, Candidate);
		// NamedParam은 float다. Task도 EQS가 실제로 받은 정밀도의 좌표를 사용한다.
		Candidate.Location = FVector(float(Candidate.Location.X), float(Candidate.Location.Y), float(Candidate.Location.Z));
		if (ShouldLogCandidateDetails())
			UE_LOG(LogEnemyEQSCandidates, Log,
				TEXT("[EnemyFlightHeight] Enemy=%s Candidate=%d Preference=%s FloorOffset=%.1f Raw=%s Goal=%s Status=%s Reason=%s Checks=%d"),
				*Enemy.GetName(), OutSnapshot.Candidates.Num() - 1, *UEnum::GetValueAsString(OutSnapshot.Preference),
				OutSnapshot.Settings.GetHeight(OutSnapshot.Preference), *Source.ToCompactString(),
				*Candidate.Location.ToCompactString(), *UEnum::GetValueAsString(Candidate.Status), Candidate.Reason, OutSnapshot.CollisionChecks);
		if (Candidate.Status == EFlightPathStatus::BudgetExceeded || Candidate.Status == EFlightPathStatus::InvalidRequest)
		{
			Result = Candidate.Status;
			break;
		}
	}
	OutSnapshot.Milliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
	if (OutSnapshot.Milliseconds >= MaxMilliseconds) Result = EFlightPathStatus::BudgetExceeded;
	return Result;
}

void EnemyFlightHeight::SetQuerySnapshot(FEnvQueryRequest& Request, const FEnemyFlightHeightSnapshot& Snapshot)
{
	if (!Snapshot.bEnabled) return;
	int32 Index = 0;
	for (const FEnemyFlightHeightCandidate& Candidate : Snapshot.Candidates)
	{
		if (Candidate.Status != EFlightPathStatus::Success) continue;
		Request.SetFloatParam(HeightCandidateParam(Index, TEXT("X")), Candidate.Location.X)
			.SetFloatParam(HeightCandidateParam(Index, TEXT("Y")), Candidate.Location.Y)
			.SetFloatParam(HeightCandidateParam(Index, TEXT("Z")), Candidate.Location.Z);
		++Index;
	}
	Request.SetFloatParam(HeightCandidateCount, Index);
}

bool EnemyFlightHeight::IsSnapshotCurrent(const AEnemyBase& Enemy, const FEnemyFlightHeightSnapshot& Snapshot)
{
	if (!Snapshot.bEnabled) return !Enemy.FlightHeightSettings.bEnabled;
	const FEnemyFlightHeightSettings& Current = Enemy.FlightHeightSettings;
	const FEnemyFlightHeightSettings& Captured = Snapshot.Settings;
	return Current.bEnabled && Current.IsValid() && Enemy.GetCombatHeightPreference() == Snapshot.Preference
		&& Current.bRandomizePreference == Captured.bRandomizePreference && Current.FixedPreference == Captured.FixedPreference
		&& Current.LowHeight == Captured.LowHeight && Current.MidHeight == Captured.MidHeight && Current.HighHeight == Captured.HighHeight
		&& Current.FloorProbeDistance == Captured.FloorProbeDistance && Current.CorrectionStep == Captured.CorrectionStep
		&& Current.CorrectionSteps == Captured.CorrectionSteps;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchCandidatesTest,
	"Outlier.Enemy.Search.Candidates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchCandidatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FVector Center(100, 200, 0);
	TArray<FVector> Candidates;
	EnemySearch::BuildCandidates(Center, 350.0f, EEnemySearchQueryMode::Anchor, Candidates);
	TestEqual(TEXT("Anchor correction candidate count is bounded"), Candidates.Num(), 73);
	TestTrue(TEXT("Anchor starts at remembered XY and current flight height"), Candidates[0].Equals(FVector(100, 200, 350)));
	for (const FVector& Candidate : Candidates)
	{
		TestTrue(TEXT("Anchor correction remains nearby and within height band"),
			EnemySearch::IsCandidateInRange(Candidate, Center, 350.0f, EEnemySearchQueryMode::Anchor));
	}
	EnemySearch::BuildCandidates(Center, 350.0f, EEnemySearchQueryMode::Points, Candidates);
	TestEqual(TEXT("Search generates a bounded set before selecting at most two"), Candidates.Num(), 72);
	for (const FVector& Candidate : Candidates)
	{
		TestTrue(TEXT("Search points remain five to eight metres and within one metre altitude"),
			EnemySearch::IsCandidateInRange(Candidate, Center, 350.0f, EEnemySearchQueryMode::Points));
	}
	TestFalse(TEXT("Old twelve metre ring cannot enter new search"),
		EnemySearch::IsCandidateInRange(Center + FVector(1200, 0, 350), Center, 350.0f, EEnemySearchQueryMode::Points));
	TestFalse(TEXT("Points above allowed flight band are rejected"),
		EnemySearch::IsCandidateInRange(Center + FVector(600, 0, 451), Center, 350.0f, EEnemySearchQueryMode::Points));
	EnemySearch::BuildCandidates(Center, 350.0f, EEnemySearchQueryMode::None, Candidates);
	TestTrue(TEXT("Disabled search does not produce candidates"), Candidates.IsEmpty());
	return true;
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRetreatCandidatesTest,
	"Outlier.Enemy.EQS.RetreatCandidates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRetreatCandidatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FVector Center(100, 200, 1000);
	TArray<FVector> Candidates;
	for (const FVector Start : {FVector(600, 200, 250), FVector(-400, 200, 250), FVector(700, 1000, 250)})
	{
		EnemyRetreat::BuildCandidates(Start, Center, Candidates);
		TestFalse(TEXT("Near and ten metre targets have retreat candidates"), Candidates.IsEmpty());
		TestTrue(TEXT("Candidate count is bounded"), Candidates.Num() <= 9);
		const FVector Away = (Start - Center).GetSafeNormal2D();
		for (const FVector& Candidate : Candidates)
		{
			const double Distance = FVector::Dist2D(Candidate, Center);
			TestTrue(TEXT("Goal keeps margin beyond ten and within fifteen metres"), Distance > 1050.0 && Distance <= 1450.0);
			TestEqual(TEXT("Goal preserves current altitude"), Candidate.Z, Start.Z);
			TestTrue(TEXT("Goal moves away rather than across player"), FVector::DotProduct(Candidate - Start, Away) >= 25.0);
		}
	}
	EnemyRetreat::BuildCandidates(Center, Center, Candidates);
	TestTrue(TEXT("Undefined away direction cannot make arbitrary goals"), Candidates.IsEmpty());
	EnemyRetreat::BuildCandidates(Center + FVector(1001, 0, 0), Center, Candidates);
	TestTrue(TEXT("Beyond ten metres does not start retreat"), Candidates.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOrbitCandidatesTest,
	"Outlier.Enemy.EQS.OrbitCandidates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOrbitCandidatesTest::RunTest(const FString& Parameters)
{
	const FVector Center(100.0, 200.0, 0.0);
	const FVector Start(1600.0, 200.0, 250.0);
	TArray<FEnemyOrbitCandidate> Candidates;
	TArray<FVector> Waypoints;
	EnemyOrbit::BuildCandidates(Start, Center, 400.0f, true, Candidates);
	TestEqual(TEXT("Two directions each have three ordered candidates"), Candidates.Num(), 6);
	if (Candidates.Num() != 6) return false;
	TestTrue(TEXT("Preference precedes opposite direction"), Candidates[0].bLeft && !Candidates[3].bLeft);
	TestEqual(TEXT("Preferred random segment is first"), Candidates[0].ArcLength, 400.0f);
	TestEqual(TEXT("Same direction minimum full segment is second"), Candidates[1].ArcLength, 300.0f);
	TestEqual(TEXT("Short fallback is before the opposite direction"), Candidates[2].ArcLength, 150.0f);
	TestTrue(TEXT("Left is drone-relative when facing the player"), Candidates[0].Destination.Y > Start.Y);
	for (const FEnemyOrbitCandidate& Candidate : Candidates)
	{
		EnemyOrbit::BuildWaypoints(Start, Center, Candidate, Waypoints);
		TestTrue(TEXT("Curve has intermediate points, not only a destination"), Waypoints.Num() > 2);
		double Length = 0.0;
		for (int32 Index = 0; Index < Waypoints.Num(); ++Index)
		{
			TestTrue(TEXT("Curve preserves combat radius"),
				FMath::IsNearlyEqual(FVector::Dist2D(Waypoints[Index], Center), 1500.0, 0.01));
			TestEqual(TEXT("Curve preserves flight height"), Waypoints[Index].Z, Start.Z);
			if (Index > 0) Length += FVector::Distance(Waypoints[Index - 1], Waypoints[Index]);
		}
		TestTrue(TEXT("Polyline approximates arc length within one centimetre"),
			Length <= Candidate.ArcLength && Candidate.ArcLength - Length < 1.0);
		TestTrue(TEXT("Final waypoint matches EQS candidate"), Waypoints.Last().Equals(Candidate.Destination, 0.01));
	}
	EnemyOrbit::BuildCandidates(Start, Center, 500.0f, false, Candidates);
	EnemyOrbit::BuildWaypoints(Start, Center, Candidates[0], Waypoints);
	TestEqual(TEXT("Maximum arc has ten segments"), Waypoints.Num(), 11);
	TestTrue(TEXT("Right preference is preserved"), !Candidates[0].bLeft && Candidates[0].Destination.Y < Start.Y);
	EnemyOrbit::BuildCandidates(Start, Center, 300.0f, true, Candidates);
	TestEqual(TEXT("Duplicate minimum-length candidates are removed"), Candidates.Num(), 4);
	EnemyOrbit::BuildCandidates(Center + FVector(1000, 0, 250), Center, 400.0f, true, Candidates);
	TestTrue(TEXT("Ten metre boundary belongs to retreat"), Candidates.IsEmpty());
	EnemyOrbit::BuildCandidates(Center + FVector(2000, 0, 250), Center, 400.0f, true, Candidates);
	TestFalse(TEXT("Twenty metre boundary permits orbit"), Candidates.IsEmpty());
	EnemyOrbit::BuildCandidates(Center + FVector(2001, 0, 250), Center, 400.0f, true, Candidates);
	TestTrue(TEXT("Beyond twenty metres belongs to approach"), Candidates.IsEmpty());
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
