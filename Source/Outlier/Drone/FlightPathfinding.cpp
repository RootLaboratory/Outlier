#include "Drone/FlightPathfinding.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GraphAStar.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "CoreGlobals.h"
#include "Room/RoomVolume.h"

namespace
{
	enum class EFlightCheckMode : uint8 { Path, Segment, Destination };
	DEFINE_LOG_CATEGORY_STATIC(LogFlightPathCost, Log, All);
	TAutoConsoleVariable<int32> CVarFlightPathCost(
		TEXT("outlier.Flight.PathCostLog"), 0,
		TEXT("Aggregate server flight path and segment cost. 0: off, 1: on."));
	TAutoConsoleVariable<int32> CVarFlightEndpointDebug(
		TEXT("outlier.Flight.EndpointDebug"), 0,
		TEXT("Log rejected flight endpoints and blocking components. 0: off, 1: on."));

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
		return IsFiniteVector(Request.Start) && IsFiniteVector(Request.Goal)
			&& Request.SearchBounds.IsValid && IsFiniteVector(Request.SearchBounds.Min)
			&& IsFiniteVector(Request.SearchBounds.Max)
			&& Request.SearchBounds.Min.X <= Request.SearchBounds.Max.X
			&& Request.SearchBounds.Min.Y <= Request.SearchBounds.Max.Y
			&& Request.SearchBounds.Min.Z <= Request.SearchBounds.Max.Z
			&& Request.SearchBounds.IsInsideOrOn(Request.Start)
			&& Request.SearchBounds.IsInsideOrOn(Request.Goal)
			&& FMath::IsFinite(Request.CellSize) && Request.CellSize >= 1.0f
			&& FMath::IsFinite(Request.Clearance) && Request.Clearance >= 0.0f
			&& FMath::IsFinite(Request.GoalSearchRadius) && Request.GoalSearchRadius >= 0.0f
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
			// 주변 26방향과 실제 목적지 연결 하나를 검사한다. 목적지에서는 더 탐색하지 않는다.
			return Node == GoalNode ? 0 : 27;
		}

		int32 GetNeighbour(int32 Node, int32 NeighbourIndex) const
		{
			// 목적지는 격자 중심으로 반올림하지 않는다. 근처 격자에서 정확한 목적지로 연결한다.
			if (NeighbourIndex == 26)
			{
				return FVector::DistSquared(ToPosition(Node), Request.Goal)
					<= FMath::Square(Request.CellSize * 1.75f) ? GoalNode : INDEX_NONE;
			}

			// 3x3x3 이웃에서 중앙(13번)은 자기 자신이므로 건너뛴다.
			const int32 OffsetIndex = NeighbourIndex >= 13 ? NeighbourIndex + 1 : NeighbourIndex;
			const FIntVector Offset(OffsetIndex % 3 - 1, (OffsetIndex / 3) % 3 - 1, OffsetIndex / 9 - 1);
			return ToNode(ToCell(Node) + Offset);
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

		bool CanContinue() const
		{
			// 한 번 중단되면 같은 요청의 후속 검사에서도 충돌 조회나 취소 콜백을 다시 실행하지 않는다.
			if (bCancelled || bBudgetExceeded)
			{
				return false;
			}

			bCancelled |= Grid.Request.ShouldCancel && Grid.Request.ShouldCancel();
			bBudgetExceeded |= (FPlatformTime::Seconds() - StartedAt) * 1000.0 >= Grid.Request.MaxSearchMilliseconds;
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

			const bool bClear = PositionCheck(Grid.ToPosition(Node));
			PositionCache.Add(Node, bClear);
			return bClear;
		}

		bool IsTraversalAllowed(int32 From, int32 To) const
		{
			// 위치만 비어 있어도 대각선 구간이 벽 모서리를 통과할 수 있으므로 전체 구간을 Sweep한다.
			return IsPositionClear(To) && BeginCheck()
				&& SegmentCheck(Grid.ToPosition(From), Grid.ToPosition(To));
		}

		double GetHeuristicScale() const
		{
			return 1.0;
		}

		double GetHeuristicCost(int32 From, int32 To) const
		{
			return FVector::Distance(Grid.ToPosition(From), Grid.ToPosition(To));
		}

		double GetTraversalCost(int32 From, int32 To) const
		{
			return GetHeuristicCost(From, To);
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

FFlightPathResult FlightPathfinding::FindPathWithChecks(
	const FFlightPathRequest& Request,
	TFunctionRef<bool(const FVector&)> IsPositionClear,
	TFunctionRef<bool(const FVector&, const FVector&)> IsSegmentClear)
{
	// 요청 검증 -> 직선 경로 확인 -> 필요할 때만 A* 우회 탐색 -> 결과 정리 순서로 처리한다.
	FFlightPathResult Result;
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

	// 모든 종료 경로에서 중단 사유와 통계를 동일하게 정리한다.
	const auto Finish = [&](EFlightPathStatus Status)
	{
		// 마지막 충돌 검사 도중 취소되거나 시간 예산을 넘은 경우도 결과에 반영한다.
		Filter.CanContinue();
		// 실패에 부분 경로를 남기지 않는다. 예산 소진은 물리적 도달 불가능 판정이 아니다.
		Result.Status = Filter.bCancelled ? EFlightPathStatus::Cancelled
			: Filter.bBudgetExceeded ? EFlightPathStatus::BudgetExceeded : Status;
		Result.CollisionChecks = Filter.Checks;
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
		// 후보 검증과 이후 A*는 같은 시간/충돌 예산을 사용한다. 여러 A*를 중첩 실행하지 않는다.
		for (int32 Ring = 1; Ring <= 2 && !bFoundGoal && Filter.CanContinue(); ++Ring)
		{
			for (int32 Direction = 0; Direction < 28 && !bFoundGoal && Filter.CanContinue(); ++Direction)
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
				if (Request.SearchBounds.IsInsideOrOn(Candidate) && Filter.BeginCheck()
					&& IsPositionClear(Candidate))
				{
					ResolvedRequest.Goal = Candidate;
					Filter.PositionCache.Add(Grid.GoalNode, true);
					Result.bGoalAdjusted = true;
					bFoundGoal = true;
				}
			}
		}
		if (!bFoundGoal)
		{
			return Finish(EFlightPathStatus::BlockedEndpoint);
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
	TArray<int32> Nodes;
	const EGraphAStarResult SearchResult = Search.FindPath(
		FGraphAStar<FFlightGrid>::FSearchNode(StartNode),
		FGraphAStar<FFlightGrid>::FSearchNode(Grid.GoalNode), Filter, Nodes);
	Result.SearchNodes = Search.NodePool.Num();

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

static FFlightPathResult FindPathOrCheckSegment(
	UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request,
	EFlightCheckMode Mode)
{
	// 게임 호출부: 권한/환경 검증 -> 캡슐 충돌 설정 구성 -> 공용 탐색에 검사 함수 전달.
	FFlightPathResult Failure;
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
	const FVector Extent = FBox(-FVector(Radius, Radius, HalfHeight), FVector(Radius, Radius, HalfHeight))
		.TransformBy(FTransform(Rotation)).GetExtent();
	const FVector ActualExtent = FBox(-FVector(ActualRadius, ActualRadius, ActualHalfHeight),
		FVector(ActualRadius, ActualRadius, ActualHalfHeight)).TransformBy(FTransform(Rotation)).GetExtent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FlightPathfinding), false, Owner);
	// 실제 이동에서 무시하는 대상은 경로 검증에서도 동일하게 무시한다.
	Params.AddIgnoredActors(MovementCapsule.GetMoveIgnoreActors());
	Params.AddIgnoredComponents(MovementCapsule.GetMoveIgnoreComponents());
	const FCollisionResponseParams Responses(MovementCapsule.GetCollisionResponseToChannels());
	const ECollisionChannel Channel = MovementCapsule.GetCollisionObjectType();

	const auto IsInsideRoom = [&](const FVector& Position)
	{
		return !Request.bRequireRoom || IsCapsuleInsideRoom(*Room, Position, Extent);
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
			return bClear;
		}
		bCheckedStart = true;
		bStartClear = bClear;
		// 안전 여유만 겹친 실제 현재 위치에서는 탈출할 수 있어야 한다. 실제 관통/방 이탈은 계속 거부한다.
		if (!bClear && Request.Start.Equals(MovementCapsule.GetComponentLocation(), 0.01))
		{
			bStartClear = (!Request.bRequireRoom || IsCapsuleInsideRoom(*Room, Position, ActualExtent))
				&& !World.OverlapBlockingTestByChannel(Position, Rotation, Channel, ActualShape, Params, Responses);
			bStartNeedsClearanceEscape = bStartClear;
		}
		return bStartClear;
	};

	const auto IsSegmentClear = [&](const FVector& From, const FVector& To)
	{
		++Failure.CollisionChecks;
		if (bStartNeedsClearanceEscape && From.Equals(Request.Start, 0.01))
		{
			// 첫 짧은 구간만 실제 크기로 Sweep한다. 도착점은 확장 캡슐 검사 통과점이며 이후 구간은 원래 여유를 쓴다.
			// 긴 직선 전체에서 여유를 없애지 않도록 A* 이웃 한 구간 길이까지만 허용한다.
			return FVector::DistSquared(From, To) <= FMath::Square(Request.CellSize * 1.75f)
				&& (!Request.bRequireRoom || (IsCapsuleInsideRoom(*Room, From, ActualExtent)
					&& IsCapsuleInsideRoom(*Room, To, ActualExtent)))
				&& !World.SweepTestByChannel(From, To, Rotation, Channel, ActualShape, Params, Responses);
		}
		return IsInsideRoom(From) && IsInsideRoom(To) && !World.SweepTestByChannel(
			From, To, Rotation, Channel, Shape, Params, Responses);
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
		return Failure;
	}
	Failure = FlightPathfinding::FindPathWithChecks(Request, IsPositionClear, IsSegmentClear);
	Failure.bUsedClearanceEscape = bStartNeedsClearanceEscape;
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
