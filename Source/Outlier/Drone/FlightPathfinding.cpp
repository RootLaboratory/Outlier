#include "Drone/FlightPathfinding.h"

#include "Algo/Reverse.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GraphAStar.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"
#include "Room/RoomVolume.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	TWeakObjectPtr<UWorld> TestBudgetWorld;
	int32 TestCollisionChecks = 0;
	FlightPathfinding::FTestCost TestCost;
#endif
	enum class EFlightCheckMode : uint8 { Path, Segment, Destination };
	DEFINE_LOG_CATEGORY_STATIC(LogFlightPathCost, Log, All);
	TAutoConsoleVariable<int32> CVarFlightPathCost(
		TEXT("outlier.Flight.PathCostLog"), 0,
		TEXT("Aggregate server flight path and segment cost. 0: off, 1: on."));
	TAutoConsoleVariable<int32> CVarFlightEndpointDebug(
		TEXT("outlier.Flight.EndpointDebug"), 0,
		TEXT("Log rejected flight endpoints and blocking components. 0: off, 1: on."));
	TAutoConsoleVariable<int32> CVarFlightSegmentDebug(
		TEXT("outlier.Flight.SegmentDebug"), 0,
		TEXT("Log failed movement segment bounds and blockers using extra diagnostic queries. 0: off, 1: on."));
	TAutoConsoleVariable<int32> CVarFlightSearchDebug(
		TEXT("outlier.Flight.SearchDebug"), 0,
		TEXT("Failed path diagnostics: per-pass cost/rejections and up to four extra blocker queries after search. 0: off, 1: on."));

	struct FFlightCostWindow
	{
		double StartedAt = 0.0;
		int32 Paths = 0;
		int32 Segments = 0;
		int32 Destinations = 0;
		int32 Failures = 0;
		int32 Budgets = 0;
		int64 Checks = 0;
		double PathMs = 0.0;
		double SegmentMs = 0.0;
		double DestinationMs = 0.0;
		double PeakCallMs = 0.0;
		uint64 Frame = MAX_uint64;
		double FrameMs = 0.0;
		double PeakFrameMs = 0.0;
	};
	FFlightCostWindow FlightCostWindow;

	void DumpFlightCost()
	{
		const FFlightCostWindow& Data = FlightCostWindow;
		if (Data.Paths + Data.Segments + Data.Destinations == 0)
		{
			return;
		}
		const double Seconds = FMath::Max(FPlatformTime::Seconds() - Data.StartedAt, 0.001);
		UE_LOG(LogFlightPathCost, Log,
			TEXT("[FlightCost] Scope=AllServerWorlds Window=%.2fs Paths=%d PathTotal=%.3fms PathAvg=%.3fms Segments=%d SegmentTotal=%.3fms SegmentAvg=%.3fms Destinations=%d DestinationTotal=%.3fms PeakCall=%.3fms PeakFrame=%.3fms Checks=%lld FailedPaths=%d BudgetExceeded=%d"),
			Seconds, Data.Paths, Data.PathMs, Data.Paths ? Data.PathMs / Data.Paths : 0.0,
			Data.Segments, Data.SegmentMs, Data.Segments ? Data.SegmentMs / Data.Segments : 0.0,
			Data.Destinations, Data.DestinationMs,
			Data.PeakCallMs, Data.PeakFrameMs, static_cast<long long>(Data.Checks), Data.Failures, Data.Budgets);
		// 같은 프레임 안에서 요약이 나가도 프레임 누적 부하는 끊지 않는다.
		const uint64 LastFrame = Data.Frame;
		const double LastFrameMs = Data.FrameMs;
		FlightCostWindow = FFlightCostWindow();
		FlightCostWindow.Frame = LastFrame;
		FlightCostWindow.FrameMs = LastFrameMs;
	}
	FAutoConsoleCommand DumpFlightCostCommand(TEXT("outlier.Flight.PathCostDump"),
		TEXT("Print and reset collected flight cost, including a final partial window."),
		FConsoleCommandDelegate::CreateStatic(&DumpFlightCost));

	struct FScopedFlightCost
	{
		const FFlightPathResult& Result;
		EFlightCheckMode Mode;
		double StartedAt = 0.0;

		FScopedFlightCost(const FFlightPathResult& InResult, EFlightCheckMode InMode, bool bServerWorld)
			: Result(InResult), Mode(InMode)
		{
			// 진단을 끄면 계측 시계도 읽지 않는다. 서버의 실제 충돌 탐색만 집계한다.
			if (bServerWorld && IsInGameThread() && CVarFlightPathCost.GetValueOnGameThread() != 0)
			{
				StartedAt = FPlatformTime::Seconds();
			}
		}

		~FScopedFlightCost()
		{
			if (StartedAt == 0.0)
			{
				return;
			}
			const double FinishedAt = FPlatformTime::Seconds();
			const double Milliseconds = (FinishedAt - StartedAt) * 1000.0;
			FFlightCostWindow& Data = FlightCostWindow;
			if (Data.StartedAt == 0.0)
			{
				Data.StartedAt = StartedAt;
			}
			if (Mode == EFlightCheckMode::Destination)
			{
				++Data.Destinations;
				Data.DestinationMs += Milliseconds;
			}
			else if (Mode == EFlightCheckMode::Segment)
			{
				++Data.Segments;
				Data.SegmentMs += Milliseconds;
			}
			else
			{
				++Data.Paths;
				Data.PathMs += Milliseconds;
				Data.Failures += Result.Status != EFlightPathStatus::Success ? 1 : 0;
				Data.Budgets += Result.Status == EFlightPathStatus::BudgetExceeded ? 1 : 0;
			}
			Data.Checks += Result.CollisionChecks;
			Data.PeakCallMs = FMath::Max(Data.PeakCallMs, Milliseconds);
			if (Data.Frame != GFrameCounter)
			{
				Data.Frame = GFrameCounter;
				Data.FrameMs = 0.0;
			}
			Data.FrameMs += Milliseconds;
			Data.PeakFrameMs = FMath::Max(Data.PeakFrameMs, Data.FrameMs);
			// 새 Tick 없이 탐색 호출이 이어지는 동안 약 1초마다 요약한다.
			if (FinishedAt - Data.StartedAt >= 1.0)
			{
				DumpFlightCost();
			}
		}
	};

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsValidRequest(const FFlightPathRequest& Request)
	{
		for (const FVector& Position : Request.RouteHistory)
			if (!IsFiniteVector(Position)) return false;
		if (Request.bRequireAwayHalfSpace && (!IsFiniteVector(Request.AwayOrigin)
			|| !IsFiniteVector(Request.AwayDirection) || !FMath::IsNearlyZero(Request.AwayDirection.Z)
			|| !FMath::IsNearlyEqual(Request.AwayDirection.SizeSquared2D(), 1.0, 0.001))) return false;
		return IsFiniteVector(Request.Start) && IsFiniteVector(Request.Goal)
			&& Request.SearchBounds.IsValid && IsFiniteVector(Request.SearchBounds.Min)
			&& IsFiniteVector(Request.SearchBounds.Max)
			&& Request.SearchBounds.Min.X <= Request.SearchBounds.Max.X
			&& Request.SearchBounds.Min.Y <= Request.SearchBounds.Max.Y
			&& Request.SearchBounds.Min.Z <= Request.SearchBounds.Max.Z
			&& Request.SearchBounds.IsInsideOrOn(Request.Start)
			&& (Request.TravelHorizon > 0.0f || Request.SearchBounds.IsInsideOrOn(Request.Goal))
			&& FMath::IsFinite(Request.TravelHorizon) && Request.TravelHorizon >= 0.0f
			&& Request.RouteHistory.Num() <= 32
			&& FMath::IsFinite(Request.CellSize) && Request.CellSize >= 1.0f
			&& FMath::IsFinite(Request.Clearance) && Request.Clearance >= 0.0f
			&& FMath::IsFinite(Request.GoalSearchRadius) && Request.GoalSearchRadius >= 0.0f
			&& FMath::IsFinite(Request.HeuristicScale) && Request.HeuristicScale >= 1.0 && Request.HeuristicScale <= 2.0
			&& FMath::IsFinite(Request.CoarseCellSize) && Request.CoarseCellSize >= 0.0f
			&& Request.LongConnectionStride >= 1 && Request.LongConnectionStride <= 8
			&& Request.MaxSearchNodes > 0 && Request.MaxSearchNodes <= 65536
			&& Request.MaxCollisionChecks > 0 && Request.MaxCollisionChecks <= 262144
			&& FMath::IsFinite(Request.MaxSearchMilliseconds) && Request.MaxSearchMilliseconds > 0.0;
	}

	struct FFlightGrid
	{
		using FNodeRef = int32;

		const FFlightPathRequest& Request;
		FIntVector MinCell = FIntVector::ZeroValue;
		FIntVector Dimensions = FIntVector::ZeroValue;
		int32 GoalNode = INDEX_NONE;
		mutable int32 OutOfBoundsNeighbours = 0;
		TFunction<FIntVector(int32)> ResolveStep;
		mutable TMap<int32, FIntVector> StepCache;
		mutable int32 WideExpansionNodes = 0;
		mutable int32 FineExpansionNodes = 0;
		bool bMixedResolution = false;
		bool bLocalRefinement = false;
		mutable TSet<int32> RefinementCandidates;
		mutable TArray<int32> PendingRefinement;
		mutable TSet<int32> CoarseProgressNodes;
		TSet<int32> RefinedNodes;

		bool UsesFineNeighbours(int32 Node) const
		{
			return bMixedResolution && (!bLocalRefinement || RefinedNodes.Contains(Node));
		}

		void RequestRefinement(int32 Node) const
		{
			const FIntVector* Step = StepCache.Find(Node);
			if (!bLocalRefinement || !Step || Step->GetMax() <= 1 || RefinementCandidates.Contains(Node)) return;
			// 목표 직결 실패가 아닌, 큰 격자의 막힌 연결 주변만 작은 이웃을 추가한다.
			RefinementCandidates.Add(Node);
			PendingRefinement.Add(Node);
		}

		FIntVector GetStep(int32 Node) const
		{
			if (const FIntVector* Cached = StepCache.Find(Node)) return *Cached;
			const FIntVector Step = ResolveStep ? ResolveStep(Node) : FIntVector(1);
			StepCache.Add(Node, Step);
			WideExpansionNodes += Step.GetMax() > 1 ? 1 : 0;
			FineExpansionNodes += Step.GetMax() > 1 ? 0 : 1;
			return Step;
		}

		bool Initialize()
		{
			// 시작점을 격자의 원점으로 두어 첫 경유점으로 이동하기 위한 별도 보정이 필요 없게 한다.
			const FVector Min = (Request.SearchBounds.Min - Request.Start) / Request.CellSize;
			const FVector Max = (Request.SearchBounds.Max - Request.Start) / Request.CellSize;

			// 전체 격자를 할당하지 않고 좌표만 계산한다. 정수 변환/노드 번호가 넘치는 범위는 거부한다.
			if (!IsFiniteVector(Min) || !IsFiniteVector(Max)
				|| Min.GetAbsMax() > 1000000.0 || Max.GetAbsMax() > 1000000.0)
			{
				return false;
			}

			MinCell = FIntVector(FMath::CeilToInt(Min.X), FMath::CeilToInt(Min.Y), FMath::CeilToInt(Min.Z));
			const FIntVector MaxCell(FMath::FloorToInt(Max.X), FMath::FloorToInt(Max.Y), FMath::FloorToInt(Max.Z));
			Dimensions = MaxCell - MinCell + FIntVector(1);
			const int64 Count = int64(Dimensions.X) * Dimensions.Y * Dimensions.Z;

			if (Dimensions.GetMin() <= 0 || Count <= 0 || Count >= MAX_int32)
			{
				return false;
			}

			// 격자 노드 뒤의 번호 하나를 실제 목적지 전용으로 남긴다.
			GoalNode = int32(Count);
			return true;
		}

		int32 ToNode(const FIntVector& Cell) const
		{
			const FIntVector Local = Cell - MinCell;
			if (Local.X < 0 || Local.Y < 0 || Local.Z < 0
				|| Local.X >= Dimensions.X || Local.Y >= Dimensions.Y || Local.Z >= Dimensions.Z)
			{
				return INDEX_NONE;
			}

			// X -> Y -> Z 순서로 좌표를 하나의 노드 번호에 담는다. ToCell은 이 계산의 역변환이다.
			return (Local.Z * Dimensions.Y + Local.Y) * Dimensions.X + Local.X;
		}

		FIntVector ToCell(int32 Node) const
		{
			return MinCell + FIntVector(Node % Dimensions.X,
				(Node / Dimensions.X) % Dimensions.Y, Node / (Dimensions.X * Dimensions.Y));
		}

		FVector ToPosition(int32 Node) const
		{
			return Node == GoalNode ? Request.Goal : Request.Start + FVector(ToCell(Node)) * Request.CellSize;
		}

		bool IsValidRef(int32 Node) const
		{
			return Node >= 0 && Node <= GoalNode;
		}

		int32 GetNeighbourCount(int32 Node) const
		{
			if (Request.bAdaptiveResolution && !UsesFineNeighbours(Node))
			{
				if (Node == GoalNode) return 0;
				GetStep(Node);
				return 27;
			}
			// 정밀화한 경계에만 단위 26방향을 추가한다. 기존 비계층 요청은 혼합 격자를 유지한다.
			return Node == GoalNode ? 0 : Request.LongConnectionStride > 1 ? 33 : 27;
		}

		int32 GetNeighbour(int32 Node, int32 NeighbourIndex) const
		{
			// 장애물을 돌아선 지점에서는 목적지 직결을 먼저 검사한다.
			// 거리만으로 허용하지 않고 Filter의 캡슐 Sweep을 통과한 연결만 사용한다.
			if (NeighbourIndex == 0)
			{
				return GoalNode;
			}
			if (Request.bAdaptiveResolution && !UsesFineNeighbours(Node))
			{
				const int32 Index = NeighbourIndex - 1;
				const int32 OffsetIndex = Index >= 13 ? Index + 1 : Index;
				const FIntVector Step = GetStep(Node);
				const FIntVector Offset((OffsetIndex % 3 - 1) * Step.X,
					(OffsetIndex / 3 % 3 - 1) * Step.Y, (OffsetIndex / 9 - 1) * Step.Z);
				const int32 Neighbour = ToNode(ToCell(Node) + Offset);
				OutOfBoundsNeighbours += Neighbour == INDEX_NONE ? 1 : 0;
				if (Neighbour == INDEX_NONE)
				{
					const FIntVector Local = ToCell(Node) + Offset - MinCell;
					// 높이가 고정된 격자/바닥의 단위 Z 연결은 입구 정밀화 사유가 아니다.
					for (int32 Axis = 0; Axis < 3; ++Axis)
						if (Step[Axis] > 1 && Dimensions[Axis] > Step[Axis]
							&& (Local[Axis] < 0 || Local[Axis] >= Dimensions[Axis])) RequestRefinement(Node);
				}
				return Neighbour;
			}

			// 3x3x3 이웃에서 중앙(13번)은 자기 자신이므로 건너뛴다.
			const bool bLongConnections = Request.LongConnectionStride > 1;
			if (bLongConnections && NeighbourIndex <= 6)
			{
				if (bMixedResolution && Request.bAdaptiveResolution && GetStep(Node)[(NeighbourIndex - 1) / 2] <= 1)
					return INDEX_NONE;
				FIntVector Offset = FIntVector::ZeroValue;
				Offset[(NeighbourIndex - 1) / 2] = (NeighbourIndex % 2 ? 1 : -1) * Request.LongConnectionStride;
				const int32 Neighbour = ToNode(ToCell(Node) + Offset);
				OutOfBoundsNeighbours += Neighbour == INDEX_NONE ? 1 : 0;
				return Neighbour;
			}
			const int32 GridNeighbourIndex = NeighbourIndex - (bLongConnections ? 7 : 1);
			const int32 OffsetIndex = GridNeighbourIndex >= 13 ? GridNeighbourIndex + 1 : GridNeighbourIndex;
			const FIntVector Offset(OffsetIndex % 3 - 1, (OffsetIndex / 3) % 3 - 1, OffsetIndex / 9 - 1);
			const int32 Neighbour = ToNode(ToCell(Node) + Offset);
			OutOfBoundsNeighbours += Neighbour == INDEX_NONE ? 1 : 0;
			return Neighbour;
		}
	};

	struct FFlightSearchFilter
	{
		const FFlightGrid& Grid;
		TFunctionRef<bool(const FVector&)> PositionCheck;
		TFunctionRef<bool(const FVector&, const FVector&)> SegmentCheck;
		double StartedAt;

		mutable int32 Checks = 0;
		mutable bool bBudgetExceeded = false;
		mutable bool bCancelled = false;
		mutable TMap<int32, bool> PositionCache;
		mutable TMap<uint64, bool> SegmentCache;
		mutable int32 SegmentCacheHits = 0;
		FGraphAStar<FFlightGrid>* Search = nullptr;
		mutable int32 PrunedTraversals = 0;
		mutable int32 RejectedPositions = 0;
		mutable int32 RejectedSegments = 0;
		mutable int32 ClearStartConnections = 0;
		mutable int32 GoalConnectionChecks = 0;
		mutable int32 ClearGoalConnections = 0;
		mutable int32 SkippedGoalConnections = 0;
		mutable TArray<int32> DeferredGoalConnections;
		mutable TSet<int32> DeferredGoalNodes;
		bool bCheckDeferredGoals = false;
		bool bResumable = false;
		double ActiveMilliseconds = 0.0;
		double SliceStartedAt = 0.0;

		double GetActiveMilliseconds() const
		{
			return bResumable ? ActiveMilliseconds + (FPlatformTime::Seconds() - SliceStartedAt) * 1000.0
				: (FPlatformTime::Seconds() - StartedAt) * 1000.0;
		}

		bool CanContinue() const
		{
			// 한 번 중단되면 같은 요청의 후속 검사에서도 충돌 조회나 취소 콜백을 다시 실행하지 않는다.
			if (bCancelled || bBudgetExceeded)
			{
				return false;
			}

			bCancelled |= Grid.Request.ShouldCancel && Grid.Request.ShouldCancel();
			// 재개 검색은 노드 중간의 시간 경계로 이웃을 버리지 않는다. 총 CPU 시간은 Advance에서 확인한다.
			bBudgetExceeded |= !bResumable && GetActiveMilliseconds() >= Grid.Request.MaxSearchMilliseconds;
			return !bCancelled && !bBudgetExceeded;
		}

		bool BeginCheck() const
		{
			if (!CanContinue())
			{
				return false;
			}

			if (Checks >= Grid.Request.MaxCollisionChecks)
			{
				bBudgetExceeded = true;
				return false;
			}

			++Checks;
			return true;
		}

		bool CanProbeDirectGoal() const
		{
			// 우회 가능한 빈 후보를 찾은 뒤의 직선 후보 조회는 보조 작업이다.
			// 전체 예산의 1/4까지만 사용하고 나머지는 기존 후보의 A* 우회 탐색에 남긴다.
			return Checks < FMath::Max(Grid.Request.MaxCollisionChecks / 4, 3)
				&& GetActiveMilliseconds() < Grid.Request.MaxSearchMilliseconds * 0.25
				&& CanContinue();
		}

		bool IsPositionClear(int32 Node) const
		{
			if (!CanContinue())
			{
				return false;
			}

			// 여러 이웃에서 같은 위치에 접근하므로, 위치 검사는 이 요청 안에서 한 번만 수행한다.
			if (const bool* Cached = PositionCache.Find(Node))
			{
				return *Cached;
			}

			if (!BeginCheck())
			{
				return false;
			}

			const FVector Position = Grid.ToPosition(Node);
			const bool bClear = FlightPathfinding::IsInsideAwayHalfSpace(Grid.Request, Position) && PositionCheck(Position);
			PositionCache.Add(Node, bClear);
			RejectedPositions += bClear ? 0 : 1;
			return bClear;
		}

		bool IsTraversalAllowed(int32 From, int32 To) const
		{
			if (!CanContinue())
			{
				return false;
			}
			if (Search)
			{
				// 먼 목표 직결은 시작 시 한 번만 검사한다. 이후에는 지역 경유지를 먼저 확보한다.
				if (To == Grid.GoalNode && Grid.Request.TravelHorizon > 0.0f
					&& !Grid.Request.SearchBounds.IsInsideOrOn(Grid.Request.Goal)) return false;
				// 출발 직선은 이미 검사했다. 먼 목표는 공간적으로 분산된 노드에서만 재확인한다.
				// 목표 근처에서는 제한을 풀어 실제 도착점으로 연결할 기회를 유지한다.
				if (To == Grid.GoalNode && Grid.Request.bLimitDistantGoalChecks && !bCheckDeferredGoals)
				{
					const FIntVector Cell = Grid.ToCell(From);
					const int32 Sample = FMath::Abs(Cell.X + Cell.Y * 3 + Cell.Z * 5) % 8;
					const double NearDistance = Grid.Request.CellSize * FMath::Max(Grid.Request.LongConnectionStride, 2) * 2.0;
					if (From == Grid.ToNode(FIntVector::ZeroValue)
						|| (Sample != 0 && GetHeuristicCost(From, To) > NearDistance))
					{
						++SkippedGoalConnections;
						if (From != Grid.ToNode(FIntVector::ZeroValue) && !DeferredGoalNodes.Contains(From))
						{
							DeferredGoalNodes.Add(From);
							DeferredGoalConnections.Add(From);
						}
						return false;
					}
				}
				const auto* FromNode = Search->NodePool.Find(From);
				const auto* ToNode = Search->NodePool.Find(To);
				// 엔진의 비용 비교를 충돌보다 먼저 적용한다. 더 좋은 연결의 안전 검사는 생략하지 않는다.
				// 같은 목표 노드의 휴리스틱은 동일하므로 엔진이 어차피 버릴 연결만 제외된다.
				const double Heuristic = To == Grid.GoalNode ? 0.0
					: GetHeuristicCost(To, Grid.GoalNode) * GetHeuristicScale();
				if (FromNode && ToNode && FromNode->TraversalCost + GetTraversalCost(From, To) + Heuristic >= ToNode->TotalCost)
				{
					// 비용상 버리는 연결도 이전 Sweep에서 통과했다면 큰 격자의 진행 가능성으로 남긴다.
					const uint64 CachedKey = (uint64(uint32(From)) << 32) | uint32(To);
					if (const bool* Cached = SegmentCache.Find(CachedKey))
						if (*Cached) RecordCoarseProgress(From, To);
					++PrunedTraversals;
					return false;
				}
			}
			// 위치만 비어 있어도 대각선 구간이 벽 모서리를 통과할 수 있으므로 전체 구간을 Sweep한다.
			// 후퇴 반공간은 볼록하므로 양 끝점이 내부이면 직선 연결도 플레이어 반대편으로 나가지 않는다.
			if (!IsPositionClear(To))
			{
				if (To != Grid.GoalNode && !bBudgetExceeded && !bCancelled) Grid.RequestRefinement(From);
				return false;
			}
			// A*는 비용 비교 전 충돌을 묻는다. 재방문한 연결은 같은 요청의 검사 결과를 재사용한다.
			// 출발점의 여유 공간 탈출은 방향에 따라 다르므로 역방향 연결과 캐시를 공유하지 않는다.
			const uint64 Key = (uint64(uint32(From)) << 32) | uint32(To);
			if (const bool* Cached = SegmentCache.Find(Key))
			{
				++SegmentCacheHits;
				if (*Cached) RecordCoarseProgress(From, To);
				if (!*Cached && To != Grid.GoalNode) Grid.RequestRefinement(From);
				return *Cached;
			}
		const int32 StartNode = Grid.ToNode(FIntVector::ZeroValue);
		const bool bReuseReverse = Grid.Request.bReuseReverseSegmentChecks && From != StartNode && To != StartNode;
		const uint64 ReverseKey = (uint64(uint32(To)) << 32) | uint32(From);
		if (bReuseReverse)
		{
			// 요청 내에서만 공유한다. 다음 재탐색에서는 새 장애물도 다시 검사한다.
			if (const bool* Cached = SegmentCache.Find(ReverseKey))
			{
				++SegmentCacheHits;
				if (*Cached) RecordCoarseProgress(From, To);
				if (!*Cached && To != Grid.GoalNode) Grid.RequestRefinement(From);
				return *Cached;
			}
		}
			if (!BeginCheck())
			{
				return false;
			}
			const bool bClear = SegmentCheck(Grid.ToPosition(From), Grid.ToPosition(To));
			if (bClear) RecordCoarseProgress(From, To);
			if (!bClear && To != Grid.GoalNode) Grid.RequestRefinement(From);
			RejectedSegments += bClear ? 0 : 1;
			ClearStartConnections += bClear && From == Grid.ToNode(FIntVector::ZeroValue) && To != Grid.GoalNode ? 1 : 0;
			GoalConnectionChecks += To == Grid.GoalNode ? 1 : 0;
			ClearGoalConnections += bClear && To == Grid.GoalNode ? 1 : 0;
			SegmentCache.Add(Key, bClear);
			return bClear;
		}

		void RecordCoarseProgress(int32 From, int32 To) const
		{
			if (!Grid.bLocalRefinement || To == Grid.GoalNode || Grid.UsesFineNeighbours(From)) return;
			const FIntVector* Step = Grid.StepCache.Find(From);
			if (!Step || Step->GetMax() <= 1) return;
			if (GetHeuristicCost(To, Grid.GoalNode) < GetHeuristicCost(From, Grid.GoalNode))
				Grid.CoarseProgressNodes.Add(From);
		}

		double GetHeuristicScale() const
		{
			return Grid.Request.HeuristicScale;
		}

		double GetHeuristicCost(int32 From, int32 To) const
		{
			return FVector::Distance(Grid.ToPosition(From), Grid.ToPosition(To));
		}

		double GetTraversalCost(int32 From, int32 To) const
		{
			double Cost = GetHeuristicCost(From, To);
			// 통과 이력은 충돌이 아니다. 되돌아가는 연결의 비용만 높여 다른 우회를 먼저 찾는다.
			if (Grid.Request.TravelHorizon > 0.0f && To != Grid.GoalNode)
			{
				const double RadiusSquared = FMath::Square(Grid.Request.TravelHorizon * 0.4);
				const FVector Position = Grid.ToPosition(To);
				double Penalty = 0.0;
				for (const FVector& Previous : Grid.Request.RouteHistory)
				{
					const double DistanceSquared = FVector::DistSquared(Position, Previous);
					if (DistanceSquared < RadiusSquared) Penalty = FMath::Max(Penalty, 8.0 * (1.0 - DistanceSquared / RadiusSquared));
				}
				Cost *= 1.0 + Penalty;
			}
			return Cost;
		}

		bool WantsPartialSolution() const
		{
			// 목적지에 도달하지 못한 부분 경로를 이동 가능한 경로로 오인하지 않도록 반환하지 않는다.
			return false;
		}

		bool ShouldIncludeStartNodeInPath() const
		{
			return true;
		}

		// FGraphAStar의 uint32 노드 수와 같은 타입으로 비교한다. 요청의 양수 범위는 진입 시 검증한다.
		uint32 GetMaxSearchNodes() const
		{
			return static_cast<uint32>(Grid.Request.MaxSearchNodes);
		}
	};

	bool IsCapsuleInsideRoom(const ARoomVolume& Room, const FVector& Position, const FVector& Extent)
	{
		// RoomVolume은 볼록한 박스다. 캡슐을 감싼 월드 박스의 모서리가 모두 내부이면
		// 양 끝점 사이의 캡슐 구간도 방 밖으로 나가지 않는다. 중심점만 검사하지 않는다.
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Offset((Corner & 1) ? Extent.X : -Extent.X,
				(Corner & 2) ? Extent.Y : -Extent.Y, (Corner & 4) ? Extent.Z : -Extent.Z);
			if (!Room.ContainsWorldLocation(Position + Offset))
			{
				return false;
			}
		}

		return true;
	}
}

bool FlightPathfinding::IsInsideAwayHalfSpace(const FFlightPathRequest& Request, const FVector& Position)
{
	return !Request.bRequireAwayHalfSpace
		|| FVector::DotProduct(Position - Request.AwayOrigin, Request.AwayDirection) >= 0.0;
}

FVector FlightPathfinding::GetCapsuleWorldExtent(float Radius, float HalfHeight, const FQuat& Rotation)
{
	// 구형 캡슐은 회전해도 크기가 같다. 긴 캡슐만 중심 선분의 기울기만큼 범위가 늘어난다.
	const FVector Axis = Rotation.GetAxisZ().GetAbs();
	return FVector(Radius) + Axis * FMath::Max(HalfHeight - Radius, 0.0f);
}

static FFlightPathResult FindGridPathWithChecks(
	const FFlightPathRequest& Request,
	TFunctionRef<bool(const FVector&)> IsPositionClear,
	TFunctionRef<bool(const FVector&, const FVector&)> IsSegmentClear,
	const TFunction<bool(const FVector&, const FVector&)>& IsRegionClear)
{
	// 요청 검증 -> 직선 경로 확인 -> 필요할 때만 A* 우회 탐색 -> 결과 정리 순서로 처리한다.
	FFlightPathResult Result;
	Result.ResolvedGoal = Request.Goal;
	Result.CellSizeUsed = Request.CellSize;
	Result.SearchPasses = 1;
	if (!IsValidRequest(Request))
	{
		return Result;
	}

	FFlightPathRequest ResolvedRequest = Request;
	FFlightGrid Grid{ ResolvedRequest };
	if (!Grid.Initialize())
	{
		return Result;
	}

	const double StartedAt = FPlatformTime::Seconds();
	FFlightSearchFilter Filter{ Grid, IsPositionClear, IsSegmentClear, StartedAt };
	const int32 StartNode = Grid.ToNode(FIntVector::ZeroValue);
	if (Request.bAdaptiveResolution)
	{
		Grid.ResolveStep = [&](int32 Node)
		{
			const FVector Position = Grid.ToPosition(Node);
			const int32 Stride = Request.LongConnectionStride;
			// 목표 근처/출발 여유 탈출은 작은 간격을 유지한다. 영역 검증이 없으면 보수적으로 세분화한다.
			if (!IsRegionClear || Stride <= 1 || Node == StartNode
				|| FVector::Distance(Position, ResolvedRequest.Goal) <= Request.CellSize * Stride)
			{
				return FIntVector(1);
			}
			// 개방도는 주변 1.5m 폭만 확인한다. 긴 연결 전체의 안전성은 별도 캡슐 Sweep으로 검증한다.
			constexpr float RegionHalfExtent = 75.0f;
			// 3D 공간을 우선 확인하고, 바닥/천장 때문에 실패하면 현재 높이의 수평 공간을 확인한다.
			for (const FIntVector Step : { FIntVector(Stride), FIntVector(Stride, Stride, 1) })
			{
				// 수평 개방도는 현재 높이에서 판단한다. 위/아래 연결의 안전성은 각각의 Sweep으로 확인한다.
				const FVector Extent(RegionHalfExtent, RegionHalfExtent,
					Step.Z > 1 ? RegionHalfExtent : 0.0f);
				if (!Request.SearchBounds.IsInsideOrOn(Position - Extent)
					|| !Request.SearchBounds.IsInsideOrOn(Position + Extent)) continue;
				if (!Filter.BeginCheck()) break;
				if (IsRegionClear(Position, Extent)) return Step;
			}
			return FIntVector(1);
		};
	}

	// 모든 종료 경로에서 중단 사유와 통계를 동일하게 정리한다.
	const auto Finish = [&](EFlightPathStatus Status)
	{
		// 마지막 충돌 검사 도중 취소되거나 시간 예산을 넘은 경우도 결과에 반영한다.
		Filter.CanContinue();
		// 실패에 부분 경로를 남기지 않는다. 예산 소진은 물리적 도달 불가능 판정이 아니다.
		Result.Status = Filter.bCancelled ? EFlightPathStatus::Cancelled
			: Filter.bBudgetExceeded ? EFlightPathStatus::BudgetExceeded : Status;
		Result.CollisionChecks = Filter.Checks;
		Result.SegmentCacheHits = Filter.SegmentCacheHits;
		Result.PrunedTraversals = Filter.PrunedTraversals;
		Result.RejectedPositions = Filter.RejectedPositions;
		Result.RejectedSegments = Filter.RejectedSegments;
		Result.OutOfBoundsNeighbours = Grid.OutOfBoundsNeighbours;
		Result.ClearStartConnections = Filter.ClearStartConnections;
		Result.GoalConnectionChecks = Filter.GoalConnectionChecks;
		Result.ClearGoalConnections = Filter.ClearGoalConnections;
		Result.SkippedGoalConnections = Filter.SkippedGoalConnections;
		Result.WideExpansionNodes = Grid.WideExpansionNodes;
		Result.FineExpansionNodes = Grid.FineExpansionNodes;
		Result.ResolvedGoal = ResolvedRequest.Goal;
		Result.ElapsedMilliseconds = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
		if (Result.Status != EFlightPathStatus::Success)
		{
			Result.Waypoints.Reset();
		}

		for (int32 Index = 1; Index < Result.Waypoints.Num(); ++Index)
		{
			Result.PathLength += FVector::Distance(Result.Waypoints[Index - 1], Result.Waypoints[Index]);
		}

		return Result;
	};

	// 시작점은 실제 위치이므로 보정/순간이동하지 않는다. 목적지만 제한된 근처 후보로 대체한다.
	if (!Filter.IsPositionClear(StartNode))
	{
		Result.bStartBlocked = true;
		return Finish(EFlightPathStatus::BlockedEndpoint);
	}
	if (!Filter.IsPositionClear(Grid.GoalNode))
	{
		if (Request.GoalSearchRadius <= 0.0f)
		{
			return Finish(EFlightPathStatus::BlockedEndpoint);
		}
		bool bFoundGoal = false;
		bool bFoundDirectGoal = false;
		FVector FallbackGoal = Request.Goal;
		// 후보 검증과 이후 A*는 같은 시간/충돌 예산을 사용한다. 여러 A*를 중첩 실행하지 않는다.
		for (int32 Ring = 1; Ring <= 2 && !bFoundDirectGoal && Filter.CanContinue()
			&& (!bFoundGoal || Filter.CanProbeDirectGoal()); ++Ring)
		{
			for (int32 Direction = 0; Direction < 28 && !bFoundDirectGoal && Filter.CanContinue()
				&& (!bFoundGoal || Filter.CanProbeDirectGoal()); ++Direction)
			{
				if (Direction == 14)
				{
					continue;
				}
				// 우선 현재 드론 쪽 접근점을 확인하고, 나머지는 주변 26방향을 확인한다.
				const int32 OffsetIndex = Direction - 1;
				const FVector Offset = Direction == 0 ? (Request.Start - Request.Goal).GetSafeNormal()
					: FVector(OffsetIndex % 3 - 1, (OffsetIndex / 3) % 3 - 1, OffsetIndex / 9 - 1).GetSafeNormal();
				const FVector Candidate = Request.Goal + Offset * (Request.GoalSearchRadius * Ring * 0.5f);
				if (Request.SearchBounds.IsInsideOrOn(Candidate) && FlightPathfinding::IsInsideAwayHalfSpace(Request, Candidate) && Filter.BeginCheck())
				{
					// 보정 후보도 실제 실행한 검사만 집계한다. 격자 캐시 밖의 조회를 통계에서 빠뜨리지 않는다.
					if (!IsPositionClear(Candidate))
					{
						++Filter.RejectedPositions;
						continue;
					}
					// 첫 빈 후보는 우회용으로 보관한다. 벽 뒤 후보를 고정하기 전에 직접 접근점을 찾는다.
					if (!bFoundGoal)
					{
						FallbackGoal = Candidate;
						bFoundGoal = true;
					}
					if (Filter.CanProbeDirectGoal() && Filter.BeginCheck())
					{
						++Filter.GoalConnectionChecks;
						if (IsSegmentClear(Request.Start, Candidate))
						{
							++Filter.ClearGoalConnections;
							FallbackGoal = Candidate;
							bFoundDirectGoal = true;
						}
						else
						{
							++Filter.RejectedSegments;
						}
					}
				}
			}
		}
		if (!bFoundGoal)
		{
			return Finish(EFlightPathStatus::BlockedEndpoint);
		}
		ResolvedRequest.Goal = FallbackGoal;
		Filter.PositionCache.Add(Grid.GoalNode, true);
		Result.bGoalAdjusted = true;
		if (bFoundDirectGoal)
		{
			// 바로 위에서 실제 Sweep을 통과한 구간이므로 직선 확인에서 같은 조회를 반복하지 않는다.
			Filter.SegmentCache.Add((uint64(uint32(StartNode)) << 32) | uint32(Grid.GoalNode), true);
		}
	}

	// 대부분의 열린 공간에서는 격자를 탐색하지 않고 직선 검증 한 번으로 끝낸다.
	if (Filter.IsTraversalAllowed(StartNode, Grid.GoalNode))
	{
		Result.Waypoints.Add(Request.Start);
		if (Request.Start != ResolvedRequest.Goal)
		{
			Result.Waypoints.Add(ResolvedRequest.Goal);
		}

		return Finish(EFlightPathStatus::Success);
	}
	if (!Filter.CanContinue())
	{
		return Finish(EFlightPathStatus::BudgetExceeded);
	}

	// 직선이 막힌 경우만 엔진 A*로 3D 이웃을 탐색한다. 각 위치와 연결 구간은 Filter가 검증한다.
	FGraphAStar<FFlightGrid> Search(Grid);
	Filter.Search = &Search;
	TArray<int32> Nodes;
	EGraphAStarResult SearchResult = Search.FindPath(
		FGraphAStar<FFlightGrid>::FSearchNode(StartNode),
		FGraphAStar<FFlightGrid>::FSearchNode(Grid.GoalNode), Filter, Nodes);
	Result.SearchNodes = Search.NodePool.Num();
	// 분산 검사는 우선순위만 바꾼다. 경로가 없다고 결론 내리기 전에 보류한 연결도 남은 예산으로 확인한다.
	if (SearchResult != EGraphAStarResult::SearchSuccess && Filter.CanContinue()
		&& !Search.HasReachMaxSearchNodes(Filter))
	{
		Filter.bCheckDeferredGoals = true;
		for (int32 From : Filter.DeferredGoalConnections)
		{
			if (!Filter.CanContinue())
			{
				break;
			}
			if (!Filter.IsTraversalAllowed(From, Grid.GoalNode))
			{
				continue;
			}
			// 보류 노드는 A*가 이미 도달한 위치다. 검증된 부모 경로에 마지막 Sweep 구간만 붙인다.
			Nodes.Reset();
			const auto* Node = Search.NodePool.Find(From);
			while (Node)
			{
				Nodes.Add(Node->NodeRef);
				if (Node->NodeRef == StartNode)
				{
					break;
				}
				Node = Search.NodePool.IsValidIndex(Node->ParentNodeIndex)
					? &Search.NodePool[Node->ParentNodeIndex] : nullptr;
			}
			if (!Nodes.IsEmpty() && Nodes.Last() == StartNode)
			{
				Algo::Reverse(Nodes);
				Nodes.Add(Grid.GoalNode);
				SearchResult = EGraphAStarResult::SearchSuccess;
				break;
			}
		}
	}

	if (SearchResult != EGraphAStarResult::SearchSuccess)
	{
		Filter.bBudgetExceeded |= Search.HasReachMaxSearchNodes(Filter);
		return Finish(EFlightPathStatus::NoPathWithinBounds);
	}

	// 엔진의 노드 번호 경로를 이동 코드가 사용할 실제 월드 좌표로 변환한다.
	Result.Waypoints.Reserve(Nodes.Num());
	for (int32 Node : Nodes)
	{
		Result.Waypoints.Add(Grid.ToPosition(Node));
	}

	return Finish(EFlightPathStatus::Success);
}

static void SimplifyFlightPath(const FFlightPathRequest& Request, FFlightPathResult& Result,
	double StartedAt, TFunctionRef<bool(const FVector&, const FVector&)> IsSegmentClear)
{
	if (!Request.bSimplifyPath || Result.Waypoints.Num() <= 2)
	{
		return;
	}
	// 경로가 확보된 뒤의 최적화다. 검사 횟수와 시간에 여유가 없으면 기존 안전 경로를 그대로 사용한다.
	const int32 MaxProbes = FMath::Min(32, Request.MaxCollisionChecks - Result.CollisionChecks);
	int32 Probes = 0;
	TArray<FVector> Simplified;
	Simplified.Reserve(Result.Waypoints.Num());
	Simplified.Add(Result.Waypoints[0]);
	for (int32 Anchor = 0; Anchor < Result.Waypoints.Num() - 1;)
	{
		int32 Next = Anchor + 1;
		// 가까운 지점부터 2/4/8개씩 범위를 늘린다. 먼 목표가 막힌 채로 매번 역순 조회하며 예산을 쓰지 않는다.
		int32 Candidate = FMath::Min(Anchor + 2, Result.Waypoints.Num() - 1);
		while (Candidate > Next)
		{
			// 탐색에 예약한 90% 이후의 시간도 압축에 사용한다. 전체 요청의 마지막 여유는 종료 처리에 남긴다.
			if (Probes >= MaxProbes || (FPlatformTime::Seconds() - StartedAt) * 1000.0 >= Request.MaxSearchMilliseconds * 0.98)
			{
				break;
			}
			if (Request.ShouldCancel && Request.ShouldCancel())
			{
				Result.Status = EFlightPathStatus::Cancelled;
				return;
			}
			++Probes;
			++Result.CollisionChecks;
			// 위치를 새로 만들지 않는다. 기존 경유점 사이의 실제 캡슐 Sweep 통과 구간만 합친다.
			if (IsSegmentClear(Result.Waypoints[Anchor], Result.Waypoints[Candidate]))
			{
				Next = Candidate;
				Candidate = FMath::Min(Anchor + (Candidate - Anchor) * 2, Result.Waypoints.Num() - 1);
			}
			else
			{
				break;
			}
		}
		Simplified.Add(Result.Waypoints[Next]);
		Anchor = Next;
	}
	Result.Waypoints = MoveTemp(Simplified);
}

static void AccumulateFlightSearchCost(FFlightPathResult& Result, const FFlightPathResult& Previous)
{
	Result.CollisionChecks += Previous.CollisionChecks;
	Result.SearchNodes += Previous.SearchNodes;
	Result.SegmentCacheHits += Previous.SegmentCacheHits;
	Result.PrunedTraversals += Previous.PrunedTraversals;
	Result.SearchPasses += Previous.SearchPasses;
	Result.RejectedPositions += Previous.RejectedPositions;
	Result.RejectedSegments += Previous.RejectedSegments;
	Result.OutOfBoundsNeighbours += Previous.OutOfBoundsNeighbours;
	Result.ClearStartConnections += Previous.ClearStartConnections;
	Result.GoalConnectionChecks += Previous.GoalConnectionChecks;
	Result.ClearGoalConnections += Previous.ClearGoalConnections;
	Result.SkippedGoalConnections += Previous.SkippedGoalConnections;
	Result.WideExpansionNodes += Previous.WideExpansionNodes;
	Result.FineExpansionNodes += Previous.FineExpansionNodes;
}

static void LogFlightSearchPass(const TCHAR* Pass, const FFlightPathRequest& Request, const FFlightPathResult& Result)
{
	UE_LOG(LogFlightPathCost, Log,
		TEXT("[FlightSearch] Pass=%s Status=%s Cell=%.1f Checks=%d/%d Nodes=%d/%d Ms=%.3f/%.3f PositionRejected=%d SegmentRejected=%d BoundsRejected=%d StartClear=%d GoalChecks=%d GoalClear=%d Start=%s Goal=%s ResolvedGoal=%s GoalSkipped=%d Stride=%d Adaptive=%d Wide=%d Fine=%d"),
		Pass, *UEnum::GetValueAsString(Result.Status), Request.CellSize,
		Result.CollisionChecks, Request.MaxCollisionChecks, Result.SearchNodes, Request.MaxSearchNodes,
		Result.ElapsedMilliseconds, Request.MaxSearchMilliseconds, Result.RejectedPositions, Result.RejectedSegments,
		Result.OutOfBoundsNeighbours, Result.ClearStartConnections, Result.GoalConnectionChecks, Result.ClearGoalConnections,
		*Request.Start.ToCompactString(), *Request.Goal.ToCompactString(), *Result.ResolvedGoal.ToCompactString(),
		Result.SkippedGoalConnections, Request.LongConnectionStride, Request.bAdaptiveResolution,
		Result.WideExpansionNodes, Result.FineExpansionNodes);
}

FFlightPathResult FlightPathfinding::FindPathWithChecks(
	const FFlightPathRequest& Request,
	TFunctionRef<bool(const FVector&)> IsPositionClear,
	TFunctionRef<bool(const FVector&, const FVector&)> IsSegmentClear,
	TFunction<bool(const FVector&, const FVector&)> IsRegionClear)
{
	if (!IsValidRequest(Request))
	{
		return FFlightPathResult();
	}
	const double StartedAt = FPlatformTime::Seconds();
	FFlightPathResult Result;
	// 단계 로그는 전체 실패가 확정된 뒤 출력해 탐색 시간 예산에 출력 비용이 섞이지 않게 한다.
	const bool bLogSearch = IsInGameThread() && CVarFlightSearchDebug.GetValueOnGameThread() > 0;
	FFlightPathRequest FirstRequest = Request;
	FFlightPathRequest FineRequest = Request;
	FFlightPathResult FirstResult;
	FFlightPathResult FinePassResult;
	bool bRanFine = false;
	// 탐색이 끝에서 예산을 모두 쓰면 안전한 경로도 수십 개의 격자 지점으로 남는다. 압축용 조회를 먼저 확보한다.
	const int32 SimplificationReserve = Request.bSimplifyPath
		? FMath::Min(32, Request.MaxCollisionChecks / 16) : 0;
	const bool bTryAdaptive = Request.bAdaptiveResolution && IsRegionClear && Request.LongConnectionStride > 1
		&& Request.MaxSearchNodes >= 2 && Request.MaxCollisionChecks >= 8;
	const bool bTryCoarse = (bTryAdaptive || Request.CoarseCellSize > Request.CellSize) && Request.MaxSearchNodes >= 2
		&& Request.MaxCollisionChecks >= 8;
	if (bTryCoarse)
	{
		// 개방도 기반 큰 연결/큰 격자가 입구를 놓쳐도 복구할 수 있도록 정밀 탐색용 예산을 남긴다.
		FFlightPathRequest Coarse = Request;
		Coarse.CellSize = FMath::Max(Request.CellSize, Request.CoarseCellSize);
		Coarse.MaxSearchNodes = Request.MaxSearchNodes / 2;
		Coarse.MaxCollisionChecks = (Request.MaxCollisionChecks - SimplificationReserve) / 2;
		Coarse.MaxSearchMilliseconds = Request.MaxSearchMilliseconds * 0.45;
		Result = FindGridPathWithChecks(Coarse, IsPositionClear, IsSegmentClear, IsRegionClear);
		if (bLogSearch)
		{
			FirstRequest = Coarse;
			FirstResult = Result;
		}
		if (Result.Status == EFlightPathStatus::BudgetExceeded || Result.Status == EFlightPathStatus::NoPathWithinBounds
			|| Result.Status == EFlightPathStatus::InvalidRequest)
		{
			FFlightPathRequest Fine = Request;
			Fine.bAdaptiveResolution = false;
			Fine.LongConnectionStride = bTryAdaptive ? 1 : Request.LongConnectionStride;
			Fine.MaxSearchNodes -= Result.SearchNodes;
			Fine.MaxCollisionChecks -= Result.CollisionChecks + SimplificationReserve;
			Fine.MaxSearchMilliseconds = Request.MaxSearchMilliseconds * 0.9
				- (FPlatformTime::Seconds() - StartedAt) * 1000.0;
			if (Fine.MaxSearchNodes > 0 && Fine.MaxCollisionChecks > 0 && Fine.MaxSearchMilliseconds > 0.0)
			{
				FFlightPathResult FineResult = FindGridPathWithChecks(Fine, IsPositionClear, IsSegmentClear, IsRegionClear);
				if (bLogSearch)
				{
					FineRequest = Fine;
					FinePassResult = FineResult;
				}
				bRanFine = true;
				AccumulateFlightSearchCost(FineResult, Result);
				Result = MoveTemp(FineResult);
			}
			else
			{
				Result.Status = EFlightPathStatus::BudgetExceeded;
			}
		}
	}
	else
	{
		FFlightPathRequest SearchRequest = Request;
		SearchRequest.MaxCollisionChecks -= SimplificationReserve;
		if (SimplificationReserve > 0) SearchRequest.MaxSearchMilliseconds *= 0.9;
		Result = FindGridPathWithChecks(SearchRequest, IsPositionClear, IsSegmentClear, IsRegionClear);
		if (bLogSearch)
		{
			FirstRequest = SearchRequest;
			FirstResult = Result;
		}
	}
	Result.RawWaypointCount = Result.Waypoints.Num();
	if (Result.Status == EFlightPathStatus::Success)
	{
		SimplifyFlightPath(Request, Result, StartedAt, IsSegmentClear);
	}
	// 두 탐색과 단순화 전체가 하나의 요청 예산을 공유한다. 실패 결과에 부분 경로를 남기지 않는다.
	Result.ElapsedMilliseconds = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
	if (Request.ShouldCancel && Request.ShouldCancel())
	{
		Result.Status = EFlightPathStatus::Cancelled;
	}
	else if (Result.ElapsedMilliseconds >= Request.MaxSearchMilliseconds && Result.Status != EFlightPathStatus::Cancelled)
	{
		Result.Status = EFlightPathStatus::BudgetExceeded;
	}
	if (Result.Status != EFlightPathStatus::Success)
	{
		Result.Waypoints.Reset();
		if (bLogSearch)
		{
			LogFlightSearchPass(bTryAdaptive ? TEXT("Adaptive") : bTryCoarse ? TEXT("Coarse") : TEXT("Single"), FirstRequest, FirstResult);
			if (bRanFine) LogFlightSearchPass(TEXT("Fine"), FineRequest, FinePassResult);
		}
	}
	Result.PathLength = 0.0;
	for (int32 Index = 1; Index < Result.Waypoints.Num(); ++Index)
	{
		Result.PathLength += FVector::Distance(Result.Waypoints[Index - 1], Result.Waypoints[Index]);
	}
	return Result;
}

static void LogFailedFlightSegment(
	UWorld& World, const UCapsuleComponent& Capsule, const FFlightPathRequest& Request,
	const FFlightPathResult& Result, bool bClearanceEscape)
{
	if (CVarFlightSegmentDebug.GetValueOnGameThread() == 0
		|| (Result.Status != EFlightPathStatus::BlockedEndpoint && Result.Status != EFlightPathStatus::BlockedSegment))
	{
		return;
	}

	// 실제 구간 판정은 유지한다. 실패한 구간에만 별도 조회해 충돌 대상과 방 경계를 구분한다.
	const AActor* Owner = Capsule.GetOwner();
	const ARoomVolume* Room = Request.Room.Get();
	const float Radius = Capsule.GetScaledCapsuleRadius() + Request.Clearance;
	const float HalfHeight = Capsule.GetScaledCapsuleHalfHeight() + Request.Clearance;
	const FQuat Rotation = Capsule.GetComponentQuat();
	const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(Radius, HalfHeight, Rotation);
	const FVector ActualExtent = FlightPathfinding::GetCapsuleWorldExtent(
		Capsule.GetScaledCapsuleRadius(), Capsule.GetScaledCapsuleHalfHeight(), Rotation);
	const bool bStartInsideRoom = !Request.bRequireRoom
		|| IsCapsuleInsideRoom(*Room, Request.Start, bClearanceEscape ? ActualExtent : Extent);
	const bool bGoalInsideRoom = !Request.bRequireRoom || IsCapsuleInsideRoom(*Room, Request.Goal, Extent);
	UE_LOG(LogFlightPathCost, Log,
		TEXT("[FlightSegment] Enemy=%s Status=%s StartBlocked=%d From=%s To=%s Room=%s StartInsideRoom=%d GoalInsideRoom=%d Radius=%.1f HalfHeight=%.1f ClearanceEscape=%d"),
		*GetNameSafe(Owner), *UEnum::GetValueAsString(Result.Status), Result.bStartBlocked,
		*Request.Start.ToString(), *Request.Goal.ToString(), *GetNameSafe(Room),
		bStartInsideRoom, bGoalInsideRoom, Radius, HalfHeight, bClearanceEscape);
	if (!bStartInsideRoom || !bGoalInsideRoom)
	{
		UE_LOG(LogFlightPathCost, Log, TEXT("[FlightSegment] Enemy=%s Reason=OutsideRoom"), *GetNameSafe(Owner));
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FlightSegmentDebug), false, Owner);
	Params.AddIgnoredActors(Capsule.GetMoveIgnoreActors());
	Params.AddIgnoredComponents(Capsule.GetMoveIgnoreComponents());
	const FCollisionResponseParams Responses(Capsule.GetCollisionResponseToChannels());
	const ECollisionChannel Channel = Capsule.GetCollisionObjectType();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	// 진단 조회는 탐색 검사 한도와 별도이며, PathCostLog에는 진단 비용도 포함된다.
	if (Result.Status == EFlightPathStatus::BlockedEndpoint)
	{
		const FVector Position = Result.bStartBlocked ? Request.Start : Request.Goal;
		TArray<FOverlapResult> Overlaps;
		World.OverlapMultiByChannel(Overlaps, Position, Rotation, Channel, Shape, Params, Responses);
		int32 LoggedBlockers = 0;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (Overlap.bBlockingHit)
			{
				UE_LOG(LogFlightPathCost, Log,
					TEXT("[FlightSegment] Enemy=%s Endpoint=%s Blocker=%s Component=%s"),
					*GetNameSafe(Owner), Result.bStartBlocked ? TEXT("Start") : TEXT("Goal"),
					*GetNameSafe(Overlap.GetActor()), *GetNameSafe(Overlap.GetComponent()));
				if (++LoggedBlockers >= 8)
				{
					break;
				}
			}
		}
		UE_LOG(LogFlightPathCost, Log, TEXT("[FlightSegment] Enemy=%s DiagnosticBlockers=%d Limit=8"),
			*GetNameSafe(Owner), LoggedBlockers);
		return;
	}
	if (bClearanceEscape
		&& FVector::DistSquared(Request.Start, Request.Goal) > FMath::Square(Request.CellSize * 1.75f))
	{
		UE_LOG(LogFlightPathCost, Log, TEXT("[FlightSegment] Enemy=%s Reason=ClearanceEscapeLengthLimit"),
			*GetNameSafe(Owner));
		return;
	}
	FHitResult Hit;
	const FCollisionShape SweepShape = bClearanceEscape
		? FCollisionShape::MakeCapsule(Capsule.GetScaledCapsuleRadius(), Capsule.GetScaledCapsuleHalfHeight()) : Shape;
	const bool bHit = World.SweepSingleByChannel(Hit, Request.Start, Request.Goal,
		Rotation, Channel, SweepShape, Params, Responses);
	UE_LOG(LogFlightPathCost, Log,
		TEXT("[FlightSegment] Enemy=%s DiagnosticHit=%d Blocker=%s Component=%s ImpactPoint=%s ImpactNormal=%s StartPenetrating=%d PenetrationDepth=%.2f Channel=%d"),
		*GetNameSafe(Owner), bHit, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
		*Hit.ImpactPoint.ToString(), *Hit.ImpactNormal.ToString(), Hit.bStartPenetrating, Hit.PenetrationDepth,
		static_cast<int32>(Channel));
}

static FFlightPathResult FindPathOrCheckSegment(
	UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request,
	EFlightCheckMode Mode)
{
	// 게임 호출부: 권한/환경 검증 -> 캡슐 충돌 설정 구성 -> 공용 탐색에 검사 함수 전달.
	FFlightPathResult Failure;
#if WITH_DEV_AUTOMATION_TESTS
	const double TestStarted = TestBudgetWorld.Get() == &World ? FPlatformTime::Seconds() : 0.0;
	// 최종 결과를 scope 종료 시 집계하므로 직선 성공과 조기 실패도 빠지지 않는다.
	ON_SCOPE_EXIT
	{
		if (TestBudgetWorld.Get() == &World)
		{
			TestCost.Checks += Failure.CollisionChecks;
			TestCost.Nodes += Failure.SearchNodes;
			const double Ms = (FPlatformTime::Seconds() - TestStarted) * 1000.0;
			TestCost.TotalMs += Ms;
			TestCost.PeakMs = FMath::Max(TestCost.PeakMs, Ms);
			if (Mode == EFlightCheckMode::Path)
			{
				++TestCost.Paths;
				TestCost.Failures += Failure.Status != EFlightPathStatus::Success;
				TestCost.Budgets += Failure.Status == EFlightPathStatus::BudgetExceeded;
			}
		}
	};
#endif
	FScopedFlightCost Cost(Failure, Mode, World.GetNetMode() != NM_Client);
	if (!IsInGameThread() || !IsValidRequest(Request))
	{
		return Failure;
	}

	const AActor* Owner = MovementCapsule.GetOwner();
	const ARoomVolume* Room = Request.Room.Get();
	// 필요한 방이 없거나 다른 월드에 있으면 제한을 풀지 않는다. 호출자가 다시 요청해야 한다.
	if (World.GetNetMode() == NM_Client || !IsValid(Owner) || !Owner->HasAuthority()
		|| !World.GetPhysicsScene() || MovementCapsule.GetWorld() != &World || !MovementCapsule.IsRegistered()
		|| !MovementCapsule.IsQueryCollisionEnabled()
		|| (Request.bRequireRoom && (!IsValid(Room) || Room->GetWorld() != &World)))
	{
		Failure.Status = EFlightPathStatus::Unavailable;
		return Failure;
	}

	// 고정 반경/Visibility가 아니라 실제 이동 캡슐의 크기와 충돌 응답을 그대로 사용한다.

	const float Radius = MovementCapsule.GetScaledCapsuleRadius() + Request.Clearance;
	const float HalfHeight = MovementCapsule.GetScaledCapsuleHalfHeight() + Request.Clearance;
	if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0.0f || HalfHeight < Radius)
	{
		return Failure;
	}

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const float ActualRadius = MovementCapsule.GetScaledCapsuleRadius();
	const float ActualHalfHeight = MovementCapsule.GetScaledCapsuleHalfHeight();
	const FCollisionShape ActualShape = FCollisionShape::MakeCapsule(ActualRadius, ActualHalfHeight);
	const FQuat Rotation = MovementCapsule.GetComponentQuat();
	const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(Radius, HalfHeight, Rotation);
	const FVector ActualExtent = FlightPathfinding::GetCapsuleWorldExtent(ActualRadius, ActualHalfHeight, Rotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FlightPathfinding), false, Owner);
	// 실제 이동에서 무시하는 대상은 경로 검증에서도 동일하게 무시한다.
	Params.AddIgnoredActors(MovementCapsule.GetMoveIgnoreActors());
	Params.AddIgnoredComponents(MovementCapsule.GetMoveIgnoreComponents());
	const FCollisionResponseParams Responses(MovementCapsule.GetCollisionResponseToChannels());
	const ECollisionChannel Channel = MovementCapsule.GetCollisionObjectType();

	const auto IsInsideRoom = [&](const FVector& Position)
	{
		return FlightPathfinding::IsInsideAwayHalfSpace(Request, Position)
			&& (!Request.bRequireRoom || IsCapsuleInsideRoom(*Room, Position, Extent));
	};
	if (Mode == EFlightCheckMode::Destination)
	{
		// 목적지 후보에는 출발점의 여유 공간 탈출 예외를 적용하지 않는다.
		// 직선 통과 여부도 여기서 거르지 않아 벽 뒤의 우회 가능한 후보는 남긴다.
		if (Request.ShouldCancel && Request.ShouldCancel())
		{
			Failure.Status = EFlightPathStatus::Cancelled;
		}
		else
		{
			Failure.CollisionChecks = 1;
			Failure.Status = IsInsideRoom(Request.Goal) && !World.OverlapBlockingTestByChannel(
				Request.Goal, Rotation, Channel, Shape, Params, Responses)
				? EFlightPathStatus::Success : EFlightPathStatus::BlockedEndpoint;
		}
		return Failure;
	}
	bool bStartNeedsClearanceEscape = false;
	bool bCheckedStart = false;
	bool bStartClear = false;
	const bool bSearchDebug = Mode == EFlightCheckMode::Path && CVarFlightSearchDebug.GetValueOnGameThread() > 0;
	TArray<FVector, TInlineAllocator<2>> RejectedPositionSamples;
	TArray<TPair<FVector, FVector>, TInlineAllocator<2>> RejectedSegmentSamples;

	const auto IsPositionClear = [&](const FVector& Position)
	{
		++Failure.CollisionChecks;
		const bool bAtStart = Position.Equals(Request.Start, 0.01);
		if (bAtStart && bCheckedStart)
		{
			return bStartClear;
		}
		const bool bClear = IsInsideRoom(Position) && !World.OverlapBlockingTestByChannel(
			Position, Rotation, Channel, Shape, Params, Responses);
		if (!bAtStart)
		{
			if (bSearchDebug && !bClear && RejectedPositionSamples.Num() < 2) RejectedPositionSamples.Add(Position);
			return bClear;
		}
		bCheckedStart = true;
		bStartClear = bClear;
		// 안전 여유만 겹친 실제 현재 위치에서는 탈출할 수 있어야 한다. 실제 관통/방 이탈은 계속 거부한다.
		if (!bClear && Request.Start.Equals(MovementCapsule.GetComponentLocation(), 0.01))
		{
			bStartClear = FlightPathfinding::IsInsideAwayHalfSpace(Request, Position)
				&& (!Request.bRequireRoom || IsCapsuleInsideRoom(*Room, Position, ActualExtent))
				&& !World.OverlapBlockingTestByChannel(Position, Rotation, Channel, ActualShape, Params, Responses);
			bStartNeedsClearanceEscape = bStartClear;
		}
		if (bSearchDebug && !bStartClear && RejectedPositionSamples.Num() < 2) RejectedPositionSamples.Add(Position);
		return bStartClear;
	};

	const auto IsSegmentClear = [&](const FVector& From, const FVector& To)
	{
		++Failure.CollisionChecks;
		bool bClear = false;
		if (bStartNeedsClearanceEscape && From.Equals(Request.Start, 0.01))
		{
			// 첫 짧은 구간만 실제 크기로 Sweep한다. 도착점은 확장 캡슐 검사 통과점이며 이후 구간은 원래 여유를 쓴다.
			// 긴 직선 전체에서 여유를 없애지 않도록 A* 이웃 한 구간 길이까지만 허용한다.
			bClear = FVector::DistSquared(From, To) <= FMath::Square(Request.CellSize * 1.75f)
				&& (!Request.bRequireRoom || (IsCapsuleInsideRoom(*Room, From, ActualExtent)
					&& IsCapsuleInsideRoom(*Room, To, ActualExtent)))
				&& !World.SweepTestByChannel(From, To, Rotation, Channel, ActualShape, Params, Responses);
		}
		else
		{
			bClear = IsInsideRoom(From) && IsInsideRoom(To) && !World.SweepTestByChannel(
				From, To, Rotation, Channel, Shape, Params, Responses);
		}
		if (bSearchDebug && !bClear && RejectedSegmentSamples.Num() < 2)
		{
			RejectedSegmentSamples.Emplace(From, To);
		}
		return bClear;
	};

	if (Mode == EFlightCheckMode::Segment)
	{
		if (Request.ShouldCancel && Request.ShouldCancel())
		{
			Failure.Status = EFlightPathStatus::Cancelled;
			return Failure;
		}
		if (!IsPositionClear(Request.Start))
		{
			Failure.bStartBlocked = true;
			Failure.Status = EFlightPathStatus::BlockedEndpoint;
		}
		else if (!IsPositionClear(Request.Goal))
		{
			Failure.Status = EFlightPathStatus::BlockedEndpoint;
		}
		else
		{
			Failure.Status = IsSegmentClear(Request.Start, Request.Goal)
				? EFlightPathStatus::Success : EFlightPathStatus::BlockedSegment;
		}
		LogFailedFlightSegment(World, MovementCapsule, Request, Failure, bStartNeedsClearanceEscape);
		return Failure;
	}
	const auto IsRegionClear = [&](const FVector& Center, const FVector& RegionExtent)
	{
		// 이동 캡슐 전체를 포함한 영역이 비어야 큰 간격을 허용한다. 영역 검사는 연결 Sweep을 대체하지 않는다.
		const FVector BoxExtent = RegionExtent + Extent;
		if (Request.bRequireAwayHalfSpace)
		{
			const double MinimumDistance = FVector::DotProduct(Center - Request.AwayOrigin, Request.AwayDirection)
				- FVector::DotProduct(RegionExtent, Request.AwayDirection.GetAbs());
			if (MinimumDistance < 0.0) return false;
		}
		if (Request.bRequireRoom && !IsCapsuleInsideRoom(*Room, Center, BoxExtent)) return false;
		return !World.OverlapBlockingTestByChannel(Center, FQuat::Identity, Channel,
			FCollisionShape::MakeBox(BoxExtent), Params, Responses);
	};
	Failure = FlightPathfinding::FindPathWithChecks(Request, IsPositionClear, IsSegmentClear, IsRegionClear);
	Failure.bUsedClearanceEscape = bStartNeedsClearanceEscape;
	if (bSearchDebug && Failure.Status != EFlightPathStatus::Success)
	{
		// 탐색 중에는 좌표만 최대 네 개 보관한다. 실패 확정 후 별도 조회하며 탐색 예산/통계에는 합산하지 않는다.
		const double DiagnosticStartedAt = FPlatformTime::Seconds();
		for (const FVector& Position : RejectedPositionSamples)
		{
			TArray<FOverlapResult> Overlaps;
			World.OverlapMultiByChannel(Overlaps, Position, Rotation, Channel, Shape, Params, Responses);
			int32 Blockers = 0;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (!Overlap.bBlockingHit) continue;
				UE_LOG(LogFlightPathCost, Log,
					TEXT("[FlightSearchBlocker] Owner=%s Kind=Position At=%s Blocker=%s Component=%s"),
					*GetNameSafe(Owner), *Position.ToCompactString(), *GetNameSafe(Overlap.GetActor()), *GetNameSafe(Overlap.GetComponent()));
				if (++Blockers >= 2) break;
			}
			UE_LOG(LogFlightPathCost, Log, TEXT("[FlightSearchBlocker] Owner=%s Kind=Position At=%s InsideConstraints=%d Blockers=%d"),
				*GetNameSafe(Owner), *Position.ToCompactString(), IsInsideRoom(Position), Blockers);
		}
		for (const TPair<FVector, FVector>& Segment : RejectedSegmentSamples)
		{
			const bool bEscape = bStartNeedsClearanceEscape && Segment.Key.Equals(Request.Start, 0.01);
			FHitResult Hit;
			const bool bHit = World.SweepSingleByChannel(Hit, Segment.Key, Segment.Value, Rotation, Channel,
				bEscape ? ActualShape : Shape, Params, Responses);
			UE_LOG(LogFlightPathCost, Log,
				TEXT("[FlightSearchBlocker] Owner=%s Kind=Segment From=%s To=%s Hit=%d Blocker=%s Component=%s Impact=%s StartPenetrating=%d InsideConstraints=%d EscapeLengthRejected=%d"),
				*GetNameSafe(Owner), *Segment.Key.ToCompactString(), *Segment.Value.ToCompactString(), bHit,
				*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString(), Hit.bStartPenetrating,
				IsInsideRoom(Segment.Key) && IsInsideRoom(Segment.Value),
				bEscape && FVector::DistSquared(Segment.Key, Segment.Value) > FMath::Square(Request.CellSize * 1.75f));
		}
		UE_LOG(LogFlightPathCost, Log, TEXT("[FlightSearchDiagnostic] Owner=%s ExtraQueries=%d ExtraMs=%.3f ExcludedFromSearchBudget=1"),
			*GetNameSafe(Owner), RejectedPositionSamples.Num() + RejectedSegmentSamples.Num(),
			(FPlatformTime::Seconds() - DiagnosticStartedAt) * 1000.0);
	}
	if (Failure.Status == EFlightPathStatus::BlockedEndpoint && CVarFlightEndpointDebug.GetValueOnGameThread() != 0)
	{
		const FVector Position = Failure.bStartBlocked ? Request.Start : Request.Goal;
		const bool bInsideRoom = IsInsideRoom(Position);
		const bool bActualBlocked = World.OverlapBlockingTestByChannel(Position, Rotation, Channel,
			FCollisionShape::MakeCapsule(MovementCapsule.GetScaledCapsuleRadius(),
				MovementCapsule.GetScaledCapsuleHalfHeight()), Params, Responses);
		UE_LOG(LogFlightPathCost, Log,
			TEXT("[FlightEndpoint] Enemy=%s Endpoint=%s Position=%s Room=%s InsideRoom=%d ActualBlocked=%d Radius=%.1f HalfHeight=%.1f Clearance=%.1f"),
			*GetNameSafe(Owner), Failure.bStartBlocked ? TEXT("Start") : TEXT("Goal"),
			*Position.ToString(), *GetNameSafe(Room), bInsideRoom, bActualBlocked, Radius, HalfHeight, Request.Clearance);
		// 진단용 조회는 탐색 예산/검사 횟수와 별도다. 비용 로그에는 진단 비용도 포함된다.
		if (bInsideRoom)
		{
			TArray<FOverlapResult> Overlaps;
			World.OverlapMultiByChannel(Overlaps, Position, Rotation, Channel, Shape, Params, Responses);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (Overlap.bBlockingHit)
				{
					UE_LOG(LogFlightPathCost, Log, TEXT("[FlightEndpoint] Blocker=%s Component=%s"),
						*GetNameSafe(Overlap.GetActor()), *GetNameSafe(Overlap.GetComponent()));
				}
			}
		}
	}
	return Failure;
}

FFlightPathResult FlightPathfinding::FindPath(
	UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request)
{
	return FindPathOrCheckSegment(World, MovementCapsule, Request, EFlightCheckMode::Path);
}

EFlightPathStatus FlightPathfinding::CheckSegment(
	UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request)
{
	return FindPathOrCheckSegment(World, MovementCapsule, Request, EFlightCheckMode::Segment).Status;
}

EFlightPathStatus FlightPathfinding::CheckDestination(
	UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request)
{
	return FindPathOrCheckSegment(World, MovementCapsule, Request, EFlightCheckMode::Destination).Status;
}

namespace
{
	constexpr int32 FlightNodeCheckAllowance = 68;
	TAutoConsoleVariable<int32> CVarFlightSearchFrameChecks(TEXT("outlier.Flight.SearchFrameChecks"), 1024,
		TEXT("Shared collision budget per world/frame for NEW incremental flight searches only; synchronous Enemy searches are excluded."));
	TAutoConsoleVariable<float> CVarFlightSearchFrameMs(TEXT("outlier.Flight.SearchFrameMs"), 4.0f,
		TEXT("Shared soft CPU budget per world/frame for NEW incremental flight searches only; nodes finish atomically."));

	class FResumableFlightSearch final : public FFlightPathSearch
	{
		enum class EPhase : uint8 { Start, Goal, AdjustGoal, Direct, Expand, Refine, Deferred, Simplify, Validate, Done };
		FFlightPathRequest OriginalRequest;
		FFlightPathRequest ResolvedRequest;
		TFunction<bool(const FVector&)> PositionCheck;
		TFunction<bool(const FVector&, const FVector&)> SegmentCheck;
		TFunction<bool(const FVector&, const FVector&)> RegionCheck;
		double CreatedAt = FPlatformTime::Seconds();
		FFlightGrid Grid;
		FFlightSearchFilter Filter;
		FGraphAStar<FFlightGrid> GraphSearch;
		FFlightPathResult Result;
		EPhase Phase = EPhase::Start;
		int32 BestNodeIndex = INDEX_NONE;
		double BestNodeCost = TNumericLimits<double>::Max();
		int32 GoalCandidateIndex = 0;
		bool bHaveFallbackGoal = false;
		FVector FallbackGoal = FVector::ZeroVector;
		int32 DeferredIndex = 0;
		bool bCoarseSearch = false;
		int32 FallbackRefinedNodes = 0;
		int32 WideRegion3DNodes = 0;
		int32 WideRegion2DNodes = 0;
		int32 SimplifyIndex = 0;
		int32 SimplifyProbes = 0;
		int32 ValidationIndex = 0;
		TArray<FVector> PendingWaypoints;

		void Finish(EFlightPathStatus Status)
		{
			Result.Status = Status;
			Phase = EPhase::Done;
			if (Status == EFlightPathStatus::Success)
			{
				Result.Waypoints = MoveTemp(PendingWaypoints);
				for (int32 Index = 1; Index < Result.Waypoints.Num(); ++Index)
					Result.PathLength += FVector::Distance(Result.Waypoints[Index - 1], Result.Waypoints[Index]);
			}
			else
			{
				Result.Waypoints.Reset();
				Result.PathLength = 0.0;
			}
		}

		FIntVector ResolveRegionStep(int32 Node)
		{
			const FVector Position = Grid.ToPosition(Node);
			const int32 Stride = ResolvedRequest.LongConnectionStride;
			if (!RegionCheck || !ResolvedRequest.bAdaptiveResolution || Stride <= 1
				|| (!OriginalRequest.bHierarchicalResolution && Node == Grid.ToNode(FIntVector::ZeroValue)))
				return FIntVector(1);
			// 영역은 간격 선택에만 사용한다. 실제 긴 연결은 별도의 캡슐 Sweep으로 검증한다.
			for (const FIntVector Step : { FIntVector(Stride), FIntVector(Stride, Stride, 1) })
			{
				const FVector Extent(75.0, 75.0, Step.Z > 1 ? 75.0 : 0.0);
				if (!ResolvedRequest.SearchBounds.IsInsideOrOn(Position - Extent)
					|| !ResolvedRequest.SearchBounds.IsInsideOrOn(Position + Extent)) continue;
				if (!Filter.BeginCheck()) break;
				if (RegionCheck(Position, Extent))
				{
					WideRegion3DNodes += Step.Z > 1 ? 1 : 0;
					WideRegion2DNodes += Step.Z > 1 ? 0 : 1;
					return Step;
				}
			}
			return FIntVector(1);
		}

		void StartGraph()
		{
			Filter.Search = &GraphSearch;
			const int32 StartNode = Grid.ToNode(FIntVector::ZeroValue);
			auto& Node = GraphSearch.NodePool.Add(FGraphAStar<FFlightGrid>::FSearchNode(StartNode));
			Node.TraversalCost = 0.0;
			Node.TotalCost = Filter.GetHeuristicCost(StartNode, Grid.GoalNode) * Filter.GetHeuristicScale();
			BestNodeIndex = Node.SearchNodeIndex;
			BestNodeCost = Node.TotalCost;
			GraphSearch.OpenList.Push(Node);
			Phase = EPhase::Expand;
		}

		bool ShouldRefineSearch() const
		{
			if (!bCoarseSearch || Grid.PendingRefinement.IsEmpty()) return false;
			const double ActiveMs = Filter.ActiveMilliseconds
				+ (FPlatformTime::Seconds() - Filter.SliceStartedAt) * 1000.0;
			return Filter.Checks >= OriginalRequest.MaxCollisionChecks / 2
				|| GraphSearch.NodePool.Num() >= OriginalRequest.MaxSearchNodes / 2
				|| ActiveMs >= OriginalRequest.MaxSearchMilliseconds * 0.5;
		}

		void BeginRefinement()
		{
			// 큰 간격이 좁은 입구를 건너뛰었더라도 NoPath로 확정하지 않는다.
			// 비용/부모/충돌 캐시는 보존한다. 열린 공간 전체를 작은 격자로 다시 탐색하지 않는다.
			bCoarseSearch = false;
			Grid.bMixedResolution = true;
			Result.CoarseSearchNodes = GraphSearch.NodePool.Num();
			Result.CoarseCollisionChecks = Filter.Checks;
			Result.SearchPasses = 2;
			DeferredIndex = 0;
			Filter.bCheckDeferredGoals = false;
			Phase = EPhase::Refine;
		}

		void RefineVisitedNode()
		{
			// 후보를 한꺼번에 열지 않는다. 기존 OpenList보다 유망한 막힌 경계 하나만 추가한다.
			const bool bFrontierExhausted = GraphSearch.OpenList.Num() == 0;
			double BestCost = bFrontierExhausted ? TNumericLimits<double>::Max()
				: GraphSearch.NodePool[GraphSearch.OpenList[0]].TotalCost;
			int32 CandidateIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Grid.PendingRefinement.Num(); ++Index)
			{
				const int32 NodeRef = Grid.PendingRefinement[Index];
				if (!bFrontierExhausted && Grid.CoarseProgressNodes.Contains(NodeRef)) continue;
				const auto* Node = GraphSearch.NodePool.Find(NodeRef);
				if (!Node || !Node->IsClosed()) continue;
				if (Node->TotalCost <= BestCost) { BestCost = Node->TotalCost; CandidateIndex = Index; }
			}
			if (CandidateIndex == INDEX_NONE) return;
			const int32 NodeRef = Grid.PendingRefinement[CandidateIndex];
			Grid.PendingRefinement.RemoveAtSwap(CandidateIndex, 1, EAllowShrinking::No);
			Grid.RefinedNodes.Add(NodeRef);
			// 큰 격자 우회가 모두 소진되면 진행 가능 후보도 보완해 어긋난 입구를 놓치지 않는다.
			FallbackRefinedNodes += bFrontierExhausted && Grid.CoarseProgressNodes.Contains(NodeRef) ? 1 : 0;
			auto* Node = GraphSearch.NodePool.Find(NodeRef);
			Node->MarkNotClosed();
			GraphSearch.OpenList.Push(*Node);
		}

		void PreparePath(int32 LastNodeIndex, bool bAppendGoal = false)
		{
			PendingWaypoints.Reset();
			for (int32 Count = 0; GraphSearch.NodePool.IsValidIndex(LastNodeIndex)
				&& Count <= OriginalRequest.MaxSearchNodes; ++Count)
			{
				const auto& Node = GraphSearch.NodePool[LastNodeIndex];
				PendingWaypoints.Add(Grid.ToPosition(Node.NodeRef));
				if (Node.NodeRef == Grid.ToNode(FIntVector::ZeroValue))
				{
					Algo::Reverse(PendingWaypoints);
					if (bAppendGoal) PendingWaypoints.Add(ResolvedRequest.Goal);
					PrepareValidation();
					return;
				}
				LastNodeIndex = Node.ParentNodeIndex;
			}
			Finish(EFlightPathStatus::NoPathWithinBounds);
		}

		void PrepareValidation()
		{
			Result.RawWaypointCount = PendingWaypoints.Num();
			Phase = OriginalRequest.bSimplifyPath && PendingWaypoints.Num() > 2 ? EPhase::Simplify : EPhase::Validate;
		}

		bool TryPrepareLocalRoute()
		{
			if (OriginalRequest.TravelHorizon <= 0.0f || GraphSearch.OpenList.Num() == 0) return false;
			const auto& Node = GraphSearch.NodePool[GraphSearch.OpenList[0]];
			if (Node.NodeRef == Grid.GoalNode) return false;
			const FVector Position = Grid.ToPosition(Node.NodeRef);
			if (FVector::DistSquared(Position, OriginalRequest.Start) < FMath::Square(OriginalRequest.TravelHorizon)) return false;
			// 목표 방향이 막히면 옆/뒤로 우회해도 된다. 방문 이력은 비용에만 반영해 탈출을 막지 않는다.
			Result.bPartialRoute = true;
			Result.ResolvedGoal = Position;
			PreparePath(Node.SearchNodeIndex);
			return true;
		}

		void ResolveAdjustedGoal()
		{
			if (!bHaveFallbackGoal) { Finish(EFlightPathStatus::BlockedEndpoint); return; }
			ResolvedRequest.Goal = FallbackGoal;
			Filter.PositionCache.Add(Grid.GoalNode, true);
			Result.bGoalAdjusted = true;
			Phase = EPhase::Direct;
		}

		void ProbeGoalCandidate()
		{
			if (GoalCandidateIndex >= 56 || (bHaveFallbackGoal && !Filter.CanProbeDirectGoal()))
			{
				ResolveAdjustedGoal();
				return;
			}
			const int32 Ring = GoalCandidateIndex / 28 + 1;
			const int32 Direction = GoalCandidateIndex++ % 28;
			if (Direction == 14) return;
			const int32 OffsetIndex = Direction - 1;
			const FVector Offset = Direction == 0 ? (OriginalRequest.Start - OriginalRequest.Goal).GetSafeNormal()
				: FVector(OffsetIndex % 3 - 1, OffsetIndex / 3 % 3 - 1, OffsetIndex / 9 - 1).GetSafeNormal();
			const FVector Candidate = OriginalRequest.Goal + Offset * (OriginalRequest.GoalSearchRadius * Ring * 0.5);
			if (!OriginalRequest.SearchBounds.IsInsideOrOn(Candidate)
				|| !FlightPathfinding::IsInsideAwayHalfSpace(OriginalRequest, Candidate) || !Filter.BeginCheck()) return;
			if (!PositionCheck(Candidate)) { ++Filter.RejectedPositions; return; }
			if (!bHaveFallbackGoal) { FallbackGoal = Candidate; bHaveFallbackGoal = true; }
			if (Filter.CanProbeDirectGoal() && Filter.BeginCheck())
			{
				++Filter.GoalConnectionChecks;
				if (SegmentCheck(OriginalRequest.Start, Candidate))
				{
					++Filter.ClearGoalConnections;
					FallbackGoal = Candidate;
					ResolveAdjustedGoal();
					Filter.SegmentCache.Add((uint64(uint32(Grid.ToNode(FIntVector::ZeroValue))) << 32)
						| uint32(Grid.GoalNode), true);
				}
				else ++Filter.RejectedSegments;
			}
		}

		void Step()
		{
			const int32 StartNode = Grid.ToNode(FIntVector::ZeroValue);
			switch (Phase)
			{
			case EPhase::Start:
				if (!Filter.IsPositionClear(StartNode))
				{
					Result.bStartBlocked = true;
					Finish(EFlightPathStatus::BlockedEndpoint);
				}
				else Phase = EPhase::Goal;
				break;
			case EPhase::Goal:
				if (OriginalRequest.TravelHorizon > 0.0f && !ResolvedRequest.SearchBounds.IsInsideOrOn(ResolvedRequest.Goal)) Phase = EPhase::Direct;
				else if (Filter.IsPositionClear(Grid.GoalNode)) Phase = EPhase::Direct;
				else if (OriginalRequest.GoalSearchRadius > 0.0f) Phase = EPhase::AdjustGoal;
				else Finish(EFlightPathStatus::BlockedEndpoint);
				break;
			case EPhase::AdjustGoal:
				ProbeGoalCandidate();
				break;
			case EPhase::Direct:
				if (Filter.IsTraversalAllowed(StartNode, Grid.GoalNode))
				{
					PendingWaypoints.Add(OriginalRequest.Start);
					if (OriginalRequest.Start != ResolvedRequest.Goal) PendingWaypoints.Add(ResolvedRequest.Goal);
					PrepareValidation();
				}
				else StartGraph();
				break;
			case EPhase::Expand:
				if (TryPrepareLocalRoute()) break;
				if (ShouldRefineSearch()) { BeginRefinement(); break; }
				if (!bCoarseSearch && Grid.bLocalRefinement) RefineVisitedNode();
				if (GraphSearch.OpenList.Num() == 0)
				{
					Filter.bCheckDeferredGoals = true;
					Phase = EPhase::Deferred;
					break;
				}
				// OpenList/NodePool/각 캐시를 그대로 둔 채 노드 하나의 모든 이웃을 처리한다.
				if (!GraphSearch.ProcessSingleNode(FGraphAStar<FFlightGrid>::FSearchNode(Grid.GoalNode),
					true, Filter, BestNodeIndex, BestNodeCost))
				{
					if (BestNodeCost == 0.0) PreparePath(BestNodeIndex);
					else { Filter.bCheckDeferredGoals = true; Phase = EPhase::Deferred; }
				}
				break;
			case EPhase::Refine:
				RefineVisitedNode();
				Phase = EPhase::Expand;
				break;
			case EPhase::Deferred:
				if (DeferredIndex >= Filter.DeferredGoalConnections.Num())
				{
					if (bCoarseSearch) { BeginRefinement(); break; }
					Finish(GraphSearch.HasReachMaxSearchNodes(Filter)
						? EFlightPathStatus::BudgetExceeded : EFlightPathStatus::NoPathWithinBounds);
					break;
				}
				if (ShouldRefineSearch()) { BeginRefinement(); break; }
				{
					const int32 From = Filter.DeferredGoalConnections[DeferredIndex++];
					if (Filter.IsTraversalAllowed(From, Grid.GoalNode))
					{
						const auto* Node = GraphSearch.NodePool.Find(From);
						if (Node) PreparePath(Node->SearchNodeIndex, true);
					}
				}
				break;
			case EPhase::Simplify:
				if (SimplifyProbes >= 32 || SimplifyIndex + 2 >= PendingWaypoints.Num())
				{
					Phase = EPhase::Validate;
					break;
				}
				if (Filter.BeginCheck())
				{
					++SimplifyProbes;
					if (SegmentCheck(PendingWaypoints[SimplifyIndex], PendingWaypoints[SimplifyIndex + 2]))
						PendingWaypoints.RemoveAt(SimplifyIndex + 1, 1, EAllowShrinking::No);
					else ++SimplifyIndex;
				}
				break;
			case EPhase::Validate:
				// 프레임 사이에 장애물이 바뀔 수 있으므로 성공 직전에는 탐색 캐시를 사용하지 않는다.
				if (ValidationIndex >= PendingWaypoints.Num()) { Finish(EFlightPathStatus::Success); break; }
				if (!Filter.BeginCheck()) break;
				if (!PositionCheck(PendingWaypoints[ValidationIndex]))
				{
					Result.bStartBlocked = ValidationIndex == 0;
					Finish(EFlightPathStatus::BlockedEndpoint);
					break;
				}
				if (ValidationIndex > 0)
				{
					if (!Filter.BeginCheck()) break;
					if (!SegmentCheck(PendingWaypoints[ValidationIndex - 1], PendingWaypoints[ValidationIndex]))
					{
						Finish(EFlightPathStatus::BlockedSegment);
						break;
					}
				}
				++ValidationIndex;
				break;
			case EPhase::Done: break;
			}
		}

		void UpdateStatistics()
		{
			Result.CollisionChecks = Filter.Checks;
			Result.SearchNodes = GraphSearch.NodePool.Num();
			Result.SegmentCacheHits = Filter.SegmentCacheHits;
			Result.PrunedTraversals = Filter.PrunedTraversals;
			Result.RejectedPositions = Filter.RejectedPositions;
			Result.RejectedSegments = Filter.RejectedSegments;
			Result.OutOfBoundsNeighbours = Grid.OutOfBoundsNeighbours;
			Result.ClearStartConnections = Filter.ClearStartConnections;
			Result.GoalConnectionChecks = Filter.GoalConnectionChecks;
			Result.ClearGoalConnections = Filter.ClearGoalConnections;
			Result.SkippedGoalConnections = Filter.SkippedGoalConnections;
			Result.WideExpansionNodes = Grid.WideExpansionNodes;
			Result.FineExpansionNodes = Grid.FineExpansionNodes;
			Result.RefinementCandidates = Grid.RefinementCandidates.Num();
			Result.RefinedSearchNodes = Grid.RefinedNodes.Num();
			Result.CoarseProgressNodes = Grid.CoarseProgressNodes.Num();
			Result.FallbackRefinedNodes = FallbackRefinedNodes;
			Result.WideRegion3DNodes = WideRegion3DNodes;
			Result.WideRegion2DNodes = WideRegion2DNodes;
			if (bCoarseSearch)
			{
				Result.CoarseSearchNodes = Result.SearchNodes;
				Result.CoarseCollisionChecks = Result.CollisionChecks;
			}
			if (!Result.bPartialRoute) Result.ResolvedGoal = ResolvedRequest.Goal;
		}

		int32 GetPhaseCheckAllowance() const
		{
			switch (Phase)
			{
			case EPhase::Expand: return FlightNodeCheckAllowance;
			case EPhase::AdjustGoal: return 3;
			case EPhase::Start:
			case EPhase::Goal:
			case EPhase::Validate: return 2;
			default: return 1;
			}
		}

	public:
		TFunction<EFlightPathStatus()> ValidateEnvironment;
		TFunction<bool()> UsedClearanceEscape;

		FResumableFlightSearch(const FFlightPathRequest& Request, TFunction<bool(const FVector&)> InPosition,
			TFunction<bool(const FVector&, const FVector&)> InSegment,
			TFunction<bool(const FVector&, const FVector&)> InRegion)
			: OriginalRequest(Request), ResolvedRequest(Request), PositionCheck(MoveTemp(InPosition)),
			SegmentCheck(MoveTemp(InSegment)), RegionCheck(MoveTemp(InRegion)), Grid{ ResolvedRequest },
			Filter{ Grid, PositionCheck, SegmentCheck, CreatedAt }, GraphSearch(Grid)
		{
			// Coarse->fine 재시작 없이 요청의 기본 격자 하나를 사용한다.
			ResolvedRequest.CoarseCellSize = 0.0f;
			if (Request.TravelHorizon > 0.0f)
			{
				const double Extent = FMath::Max(Request.TravelHorizon, Request.CellSize * 2.0f)
					+ Request.CellSize * Request.LongConnectionStride;
				ResolvedRequest.SearchBounds = Request.SearchBounds.Overlap(FBox(Request.Start - FVector(Extent), Request.Start + FVector(Extent)));
			}
			Result.ResolvedGoal = Request.Goal;
			Result.CellSizeUsed = Request.CellSize;
			if (!IsValidRequest(ResolvedRequest) || !PositionCheck || !SegmentCheck || !Grid.Initialize())
			{
				Finish(EFlightPathStatus::InvalidRequest);
				return;
			}
			Result.Status = EFlightPathStatus::InProgress;
			Result.SearchPasses = 1;
			Filter.bResumable = true;
			bCoarseSearch = Request.bHierarchicalResolution && Request.bAdaptiveResolution
				&& Request.LongConnectionStride > 1 && bool(RegionCheck);
			Grid.bMixedResolution = !bCoarseSearch;
			Grid.bLocalRefinement = bCoarseSearch;
			Grid.ResolveStep = [this](int32 Node) { return ResolveRegionStep(Node); };
		}

		bool ChargeAdditionalCheck() { return Filter.BeginCheck(); }
		void Fail(EFlightPathStatus Status) { Finish(Status); }
		const FFlightPathRequest& GetRequest() const override { return OriginalRequest; }
		const FFlightPathResult& GetResult() const override { return Result; }

		bool Advance(int32 MaxChecks, double MaxMilliseconds) override
		{
			if (Phase == EPhase::Done) return true;
			const double StartedAt = FPlatformTime::Seconds();
			Filter.SliceStartedAt = StartedAt;
			if (ValidateEnvironment)
			{
				const EFlightPathStatus Status = ValidateEnvironment();
				if (Status != EFlightPathStatus::InProgress) Finish(Status);
			}
			if (OriginalRequest.ShouldCancel && OriginalRequest.ShouldCancel()) Finish(EFlightPathStatus::Cancelled);
			if (Phase == EPhase::Done || MaxChecks <= 0 || !FMath::IsFinite(MaxMilliseconds) || MaxMilliseconds <= 0.0)
				return Phase == EPhase::Done;
			++Result.SearchSlices;
			const int32 InitialChecks = Filter.Checks;
			while (Phase != EPhase::Done)
			{
				if (!Filter.CanContinue())
				{
					Finish(Filter.bBudgetExceeded ? EFlightPathStatus::BudgetExceeded : EFlightPathStatus::Cancelled);
					break;
				}
				const double SliceMs = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
				if (Filter.ActiveMilliseconds + SliceMs >= OriginalRequest.MaxSearchMilliseconds)
				{
					Finish(EFlightPathStatus::BudgetExceeded);
					break;
				}
				if (Filter.Checks >= OriginalRequest.MaxCollisionChecks)
				{
					Finish(EFlightPathStatus::BudgetExceeded);
					break;
				}
				const int32 Allowance = GetPhaseCheckAllowance();
				const int32 RemainingTotal = OriginalRequest.MaxCollisionChecks - Filter.Checks;
				if (MaxChecks - (Filter.Checks - InitialChecks) < FMath::Min(Allowance, RemainingTotal)
					|| SliceMs >= MaxMilliseconds) break;
				Step();
				if (Filter.bBudgetExceeded) Finish(EFlightPathStatus::BudgetExceeded);
				else if (Filter.bCancelled) Finish(EFlightPathStatus::Cancelled);
			}
			const double Elapsed = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
			Filter.ActiveMilliseconds += Elapsed;
			Result.ElapsedMilliseconds = Filter.ActiveMilliseconds;
			Result.PeakSliceMilliseconds = FMath::Max(Result.PeakSliceMilliseconds, Elapsed);
			Result.WallMilliseconds = (FPlatformTime::Seconds() - CreatedAt) * 1000.0;
			if (UsedClearanceEscape) Result.bUsedClearanceEscape = UsedClearanceEscape();
			if (Filter.ActiveMilliseconds >= OriginalRequest.MaxSearchMilliseconds && Result.Status != EFlightPathStatus::Cancelled)
				Finish(EFlightPathStatus::BudgetExceeded);
			UpdateStatistics();
			return Phase == EPhase::Done;
		}
	};
}

TSharedPtr<FFlightPathSearch> FlightPathfinding::BeginSearchWithChecks(const FFlightPathRequest& Request,
	TFunction<bool(const FVector&)> IsPositionClear, TFunction<bool(const FVector&, const FVector&)> IsSegmentClear,
	TFunction<bool(const FVector&, const FVector&)> IsRegionClear)
{
	return MakeShared<FResumableFlightSearch>(Request, MoveTemp(IsPositionClear), MoveTemp(IsSegmentClear), MoveTemp(IsRegionClear));
}

namespace
{
	// 비동기 작업이 아닌 게임 스레드 분할 실행이다. UObject는 소유하지 않고 매 차례 유효성을 확인한다.
	struct FFlightWorldChecks
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<const UCapsuleComponent> Capsule;
		TWeakPtr<FResumableFlightSearch> Search;
		FFlightPathRequest Request;
		FCollisionQueryParams Params;
		FCollisionResponseParams Responses;
		ECollisionChannel Channel;
		FQuat Rotation;
		float Radius;
		float HalfHeight;
		FCollisionShape Shape;
		FCollisionShape ActualShape;
		FVector Extent;
		FVector ActualExtent;
		TArray<TWeakObjectPtr<AActor>> IgnoredActors;
		TArray<TWeakObjectPtr<UPrimitiveComponent>> IgnoredComponents;
		bool bClearanceEscape = false;

		FFlightWorldChecks(UWorld& InWorld, const UCapsuleComponent& InCapsule, const FFlightPathRequest& InRequest)
			: World(&InWorld), Capsule(&InCapsule), Request(InRequest),
			Params(SCENE_QUERY_STAT(FlightPathfinding), false, InCapsule.GetOwner()),
			Responses(InCapsule.GetCollisionResponseToChannels()), Channel(InCapsule.GetCollisionObjectType()),
			Rotation(InCapsule.GetComponentQuat()), Radius(InCapsule.GetScaledCapsuleRadius()),
			HalfHeight(InCapsule.GetScaledCapsuleHalfHeight()),
			Shape(FCollisionShape::MakeCapsule(Radius + Request.Clearance, HalfHeight + Request.Clearance)),
			ActualShape(FCollisionShape::MakeCapsule(Radius, HalfHeight)),
			Extent(FlightPathfinding::GetCapsuleWorldExtent(Radius + Request.Clearance, HalfHeight + Request.Clearance, Rotation)),
			ActualExtent(FlightPathfinding::GetCapsuleWorldExtent(Radius, HalfHeight, Rotation))
		{
			Params.AddIgnoredActors(InCapsule.GetMoveIgnoreActors());
			Params.AddIgnoredComponents(InCapsule.GetMoveIgnoreComponents());
			for (AActor* Actor : InCapsule.GetMoveIgnoreActors()) IgnoredActors.Add(Actor);
			for (UPrimitiveComponent* Component : InCapsule.GetMoveIgnoreComponents()) IgnoredComponents.Add(Component);
		}

		bool SameIgnoredObjects(const UCapsuleComponent& CurrentCapsule) const
		{
			const TArray<AActor*>& Actors = CurrentCapsule.GetMoveIgnoreActors();
			const TArray<UPrimitiveComponent*>& Components = CurrentCapsule.GetMoveIgnoreComponents();
			if (Actors.Num() != IgnoredActors.Num() || Components.Num() != IgnoredComponents.Num()) return false;
			for (int32 Index = 0; Index < Actors.Num(); ++Index)
				if (IgnoredActors[Index].Get() != Actors[Index]) return false;
			for (int32 Index = 0; Index < Components.Num(); ++Index)
				if (IgnoredComponents[Index].Get() != Components[Index]) return false;
			return true;
		}

		EFlightPathStatus Validate() const
		{
			const UWorld* CurrentWorld = World.Get();
			const UCapsuleComponent* CurrentCapsule = Capsule.Get();
			const AActor* Owner = CurrentCapsule ? CurrentCapsule->GetOwner() : nullptr;
			const ARoomVolume* Room = Request.Room.Get();
			if (!IsInGameThread() || !CurrentWorld || !CurrentCapsule || !IsValid(Owner)
				|| !Owner->HasAuthority() || CurrentWorld->GetNetMode() == NM_Client
				|| !CurrentWorld->GetPhysicsScene() || CurrentCapsule->GetWorld() != CurrentWorld
				|| !CurrentCapsule->IsRegistered() || !CurrentCapsule->IsQueryCollisionEnabled()
				|| (Request.bRequireRoom && (!IsValid(Room) || Room->GetWorld() != CurrentWorld)))
				return EFlightPathStatus::Unavailable;
			// 캐시에 사용했던 캡슐 설정이 바뀌면 기존 결과를 전달하지 않는다. 위치 이동은 경로 합류로 처리한다.
			if (!FMath::IsNearlyEqual(Radius, CurrentCapsule->GetScaledCapsuleRadius())
				|| !FMath::IsNearlyEqual(HalfHeight, CurrentCapsule->GetScaledCapsuleHalfHeight())
				|| Channel != CurrentCapsule->GetCollisionObjectType()
				|| Responses.CollisionResponse != CurrentCapsule->GetCollisionResponseToChannels()
				|| !SameIgnoredObjects(*CurrentCapsule)
				|| !Rotation.GetAxisZ().Equals(CurrentCapsule->GetComponentQuat().GetAxisZ(), 0.001))
				return EFlightPathStatus::Cancelled;
			return EFlightPathStatus::InProgress;
		}

		bool Inside(const FVector& Position, const FVector& BoundsExtent) const
		{
			const ARoomVolume* Room = Request.Room.Get();
			return FlightPathfinding::IsInsideAwayHalfSpace(Request, Position)
				&& (!Request.bRequireRoom || (Room && IsCapsuleInsideRoom(*Room, Position, BoundsExtent)));
		}

		bool PositionClear(const FVector& Position)
		{
			UWorld* CurrentWorld = World.Get();
			if (!CurrentWorld) return false;
			const bool bClear = Inside(Position, Extent) && !CurrentWorld->OverlapBlockingTestByChannel(
				Position, Rotation, Channel, Shape, Params, Responses);
			if (!Position.Equals(Request.Start, 0.01)) return bClear;
			bClearanceEscape = false;
			if (bClear) return true;
			const UCapsuleComponent* CurrentCapsule = Capsule.Get();
			const TSharedPtr<FResumableFlightSearch> CurrentSearch = Search.Pin();
			if (!CurrentCapsule || !Position.Equals(CurrentCapsule->GetComponentLocation(), 0.01)
				|| !Inside(Position, ActualExtent) || !CurrentSearch || !CurrentSearch->ChargeAdditionalCheck()) return false;
			bClearanceEscape = !CurrentWorld->OverlapBlockingTestByChannel(Position, Rotation, Channel, ActualShape, Params, Responses);
			return bClearanceEscape;
		}

		bool SegmentClear(const FVector& From, const FVector& To) const
		{
			UWorld* CurrentWorld = World.Get();
			if (!CurrentWorld) return false;
			const bool bEscape = bClearanceEscape && From.Equals(Request.Start, 0.01);
			if (bEscape && FVector::DistSquared(From, To) > FMath::Square(Request.CellSize * 1.75f)) return false;
			const FVector& BoundsExtent = bEscape ? ActualExtent : Extent;
			return Inside(From, BoundsExtent) && Inside(To, BoundsExtent) && !CurrentWorld->SweepTestByChannel(
				From, To, Rotation, Channel, bEscape ? ActualShape : Shape, Params, Responses);
		}

		bool RegionClear(const FVector& Center, const FVector& RegionExtent) const
		{
			UWorld* CurrentWorld = World.Get();
			if (!CurrentWorld) return false;
			if (Request.bRequireAwayHalfSpace && FVector::DotProduct(Center - Request.AwayOrigin, Request.AwayDirection)
				- FVector::DotProduct(RegionExtent, Request.AwayDirection.GetAbs()) < 0.0) return false;
			const FVector BoundsExtent = RegionExtent + Extent;
			return Inside(Center, BoundsExtent) && !CurrentWorld->OverlapBlockingTestByChannel(Center, FQuat::Identity,
				Channel, FCollisionShape::MakeBox(BoundsExtent), Params, Responses);
		}
	};

	struct FFlightSearchTurn
	{
		TWeakPtr<FFlightPathSearch> Search;
		uint64 LastSeenFrame = 0;
		uint64 LastAdvancedFrame = MAX_uint64;
	};
	struct FFlightFrameBudget
	{
		uint64 Frame = MAX_uint64;
		int32 Checks = 0;
		double Milliseconds = 0.0;
		TArray<FFlightSearchTurn> Turns;
	};
	TMap<TWeakObjectPtr<UWorld>, FFlightFrameBudget> FlightFrameBudgets;
}

TSharedPtr<FFlightPathSearch> FlightPathfinding::BeginSearch(
	UWorld& World, const UCapsuleComponent& Capsule, const FFlightPathRequest& Request)
{
	if (!IsInGameThread()) return nullptr;
	const TSharedRef<FFlightWorldChecks> Checks = MakeShared<FFlightWorldChecks>(World, Capsule, Request);
	const TSharedRef<FResumableFlightSearch> Search = MakeShared<FResumableFlightSearch>(Request,
		[Checks](const FVector& Position) { return Checks->PositionClear(Position); },
		[Checks](const FVector& From, const FVector& To) { return Checks->SegmentClear(From, To); },
		[Checks](const FVector& Center, const FVector& Extent) { return Checks->RegionClear(Center, Extent); });
	Checks->Search = Search;
	Search->ValidateEnvironment = [Checks]() { return Checks->Validate(); };
	Search->UsedClearanceEscape = [Checks]() { return Checks->bClearanceEscape; };
	return Search;
}

bool FlightPathfinding::AdvanceSearch(UWorld& World, FFlightPathSearch& Search, int32 SliceChecks, double SliceMs)
{
	if (!IsInGameThread()) return false;
	// 차례를 기다려도 취소/월드 소멸은 즉시 관찰한다. 이 호출은 충돌 검사를 실행하지 않는다.
	if (Search.Advance(0, 0.0)) return true;
	for (auto It = FlightFrameBudgets.CreateIterator(); It; ++It)
		if (!It.Key().IsValid()) It.RemoveCurrent();
	FFlightFrameBudget& Budget = FlightFrameBudgets.FindOrAdd(&World);
	const uint64 Frame = GFrameCounter;
	if (Budget.Frame != Frame)
	{
		Budget.Frame = Frame;
		Budget.Checks = 0;
		Budget.Milliseconds = 0.0;
	}
	Budget.Turns.RemoveAll([Frame](const FFlightSearchTurn& Turn)
	{
		const TSharedPtr<FFlightPathSearch> Pending = Turn.Search.Pin();
		return !Pending || Pending->GetResult().Status != EFlightPathStatus::InProgress || Frame > Turn.LastSeenFrame + 1;
	});
	int32 Index = Budget.Turns.IndexOfByPredicate([&Search](const FFlightSearchTurn& Turn)
	{
		return Turn.Search.Pin().Get() == &Search;
	});
	if (Index == INDEX_NONE)
	{
		Index = Budget.Turns.Add({ Search.AsShared(), Frame, MAX_uint64 });
	}
	Budget.Turns[Index].LastSeenFrame = Frame;
	if (Index != 0 || Budget.Turns[Index].LastAdvancedFrame == Frame) return false;
	const int32 RemainingChecks = FMath::Max(0, CVarFlightSearchFrameChecks.GetValueOnGameThread() - Budget.Checks);
	const double RemainingMs = CVarFlightSearchFrameMs.GetValueOnGameThread() - Budget.Milliseconds;
	// 노드 도중 중단하지 않으므로 최소 한 노드의 검사 공간을 남겨둔다.
	if (RemainingChecks < FlightNodeCheckAllowance || RemainingMs <= 0.0
		|| SliceChecks < FlightNodeCheckAllowance || SliceMs <= 0.0) return false;
	const int32 BeforeChecks = Search.GetResult().CollisionChecks;
	const double StartedAt = FPlatformTime::Seconds();
	const bool bFinished = Search.Advance(FMath::Min(SliceChecks, RemainingChecks), FMath::Min(SliceMs, RemainingMs));
	Budget.Checks += Search.GetResult().CollisionChecks - BeforeChecks;
	Budget.Milliseconds += (FPlatformTime::Seconds() - StartedAt) * 1000.0;
	// 라운드 로빈으로 다음 프레임의 첫 호출 순서와 관계없이 대기 중인 요청에 차례를 넘긴다.
	FFlightSearchTurn Turn = Budget.Turns[0];
	Budget.Turns.RemoveAt(0, 1, EAllowShrinking::No);
	Turn.LastAdvancedFrame = Frame;
	if (!bFinished) Budget.Turns.Add(MoveTemp(Turn));
	return bFinished;
}

#if WITH_DEV_AUTOMATION_TESTS
void FlightPathfinding::BeginTestBudget(UWorld& World, int32 CollisionChecks)
{
	check(IsInGameThread() && !TestBudgetWorld.IsValid());
	TestBudgetWorld = &World;
	TestCollisionChecks = FMath::Clamp(CollisionChecks, 1, 262144);
	TestCost = FTestCost();
}

void FlightPathfinding::EndTestBudget()
{
	TestBudgetWorld.Reset();
	TestCollisionChecks = 0;
}

int32 FlightPathfinding::GetTestBudget(UWorld& World, int32 AssetBudget)
{
	return TestBudgetWorld.Get() == &World ? TestCollisionChecks : AssetBudget;
}

FlightPathfinding::FTestCost FlightPathfinding::GetTestCost()
{
	return TestCost;
}

void FlightPathfinding::RecordTestQuery(UWorld* World, bool bFinished)
{
	if (!World || TestBudgetWorld.Get() != World) return;
	if (bFinished) ++TestCost.QueriesFinished;
	else ++TestCost.QueriesStarted;
}
#endif
