#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/FlightPathfinding.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomVolume.h"
#include <limits>

namespace
{
	class FScopedFlightSearchCVar
	{
		IConsoleVariable* Variable;
		FString PreviousValue;
		EConsoleVariableFlags Priority = ECVF_SetByCode;
	public:
		FScopedFlightSearchCVar(const TCHAR* Name, const TCHAR* Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				PreviousValue = Variable->GetString();
				Priority = static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask);
				Variable->Set(Value, Priority);
			}
		}
		~FScopedFlightSearchCVar() { if (Variable) Variable->Set(*PreviousValue, Priority); }
		bool IsValid() const { return Variable != nullptr; }
	};

	struct FScopedFlightSearchWorld
	{
		UWorld* World = nullptr;
		FScopedFlightSearchWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("FlightSchedulerTest")));
			if (World)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}
		~FScopedFlightSearchWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
			}
		}
	};

	FFlightPathRequest MakeRequest()
	{
		FFlightPathRequest Request;
		Request.Start = FVector(-300.0, 0.0, 0.0);
		Request.Goal = FVector(325.0, 25.0, 0.0);
		Request.SearchBounds = FBox(FVector(-400.0, -400.0, -300.0), FVector(400.0, 400.0, 300.0));
		Request.MaxSearchNodes = 4096;
		Request.MaxCollisionChecks = 20000;
		Request.MaxSearchMilliseconds = 10000.0;
		return Request;
	}

	FFlightPathResult FindAroundBox(const FFlightPathRequest& Request, const FBox& Obstacle)
	{
		return FlightPathfinding::FindPathWithChecks(Request,
			[&](const FVector& Point)
			{
				return !Obstacle.IsInsideOrOn(Point);
			},
			[&](const FVector& From, const FVector& To)
			{
				return !FMath::LineBoxIntersection(Obstacle, From, To, To - From);
			});
	}

	void CheckPath(FAutomationTestBase& Test, const FFlightPathRequest& Request,
		const FFlightPathResult& Result, const FBox& Obstacle)
	{
		Test.TestTrue(TEXT("Path succeeds"), Result.Status == EFlightPathStatus::Success);
		if (Result.Waypoints.IsEmpty())
		{
			return;
		}

		Test.TestEqual(TEXT("Path preserves exact start"), Result.Waypoints[0], Request.Start);
		Test.TestEqual(TEXT("Path preserves off-grid goal"), Result.Waypoints.Last(), Request.Goal);
		for (int32 Index = 0; Index < Result.Waypoints.Num(); ++Index)
		{
			Test.TestTrue(TEXT("Waypoint remains in bounds"), Request.SearchBounds.IsInsideOrOn(Result.Waypoints[Index]));
			Test.TestFalse(TEXT("Waypoint is outside obstacle"), Obstacle.IsInsideOrOn(Result.Waypoints[Index]));
			if (Index > 0)
			{
				const FVector& From = Result.Waypoints[Index - 1];
				const FVector& To = Result.Waypoints[Index];
				Test.TestFalse(TEXT("Segment cannot cut obstacle corners"),
					FMath::LineBoxIntersection(Obstacle, From, To, To - From));
			}
		}
	}

	bool FinishSearch(FAutomationTestBase& Test, FFlightPathSearch& Search, int32 SliceChecks)
	{
		// 재개가 영원히 대기하는 회귀도 테스트 실행을 끝낼 수 있도록 반복 횟수를 제한한다.
		for (int32 Slice = 0; Slice < 20001; ++Slice)
		{
			const int32 Before = Search.GetResult().CollisionChecks;
			const bool bDone = Search.Advance(SliceChecks, 10000.0);
			Test.TestTrue(TEXT("Each slice respects its collision cap"), Search.GetResult().CollisionChecks - Before <= SliceChecks);
			if (bDone)
			{
				return true;
			}
			Test.TestEqual(TEXT("Pending search reports InProgress"), Search.GetResult().Status, EFlightPathStatus::InProgress);
			Test.TestTrue(TEXT("Pending search exposes no partial route"), Search.GetResult().Waypoints.IsEmpty());
		}
		Test.AddError(TEXT("Resumable search did not terminate within the bounded slice driver"));
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightRollingRouteTest,
	"Outlier.Flight.Pathfinding.Resumable.RollingRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightRollingRouteTest::RunTest(const FString& Parameters)
{
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(20000, 0, 0);
	Request.TravelHorizon = 1000;
	Request.CellSize = 100;
	Request.MaxCollisionChecks = 2048;
	Request.MaxSearchNodes = 512;
	Request.bSimplifyPath = true;
	const FBox Wall(FVector(4000, -400, -100), FVector(4100, 400, 100));
	const auto PositionClear = [&](const FVector& Position) { return !Wall.IsInsideOrOn(Position); };
	const auto SegmentClear = [&](const FVector& From, const FVector& To)
	{
		return !FMath::LineBoxIntersection(Wall, From, To, To - From);
	};
	bool bArrived = false;
	bool bSawPartial = false;
	for (int32 Chunk = 0; Chunk < 32; ++Chunk)
	{
		Request.SearchBounds = FBox(Request.Start - FVector(1200, 1200, 1), Request.Start + FVector(1200, 1200, 1));
		const auto Search = FlightPathfinding::BeginSearchWithChecks(Request, PositionClear, SegmentClear);
		int32 Slices = 0;
		while (!Search->Advance(128, 10000) && ++Slices < 1000) {}
		const FFlightPathResult& Result = Search->GetResult();
		if (!TestEqual(TEXT("Every rolling chunk completes safely"), Result.Status, EFlightPathStatus::Success)) break;
		if (!TestTrue(TEXT("Every chunk contains a route"), Result.Waypoints.Num() >= 2)) break;
		TestTrue(TEXT("Each chunk keeps the collision budget"), Result.CollisionChecks <= Request.MaxCollisionChecks);
		for (int32 Index = 1; Index < Result.Waypoints.Num(); ++Index)
			TestTrue(TEXT("Every returned segment avoids the wall"), SegmentClear(Result.Waypoints[Index - 1], Result.Waypoints[Index]));
		if (!Result.bPartialRoute)
		{
			TestEqual(TEXT("Only a complete route ends at the distant goal"), Result.Waypoints.Last(), Request.Goal);
			bArrived = true;
			break;
		}
		bSawPartial = true;
		TestTrue(TEXT("Partial endpoint stays in the local bounds"), Request.SearchBounds.IsInsideOrOn(Result.Waypoints.Last()));
		TestEqual(TEXT("Partial resolved goal is the runtime waypoint"), Result.ResolvedGoal, Result.Waypoints.Last());
		for (const FVector& Previous : Request.RouteHistory)
			TestTrue(TEXT("A chunk does not return to a recent waypoint"),
				FVector::DistSquared(Result.Waypoints.Last(), Previous) >= FMath::Square(Request.TravelHorizon * 0.4f));
		if (Request.RouteHistory.Num() >= 16) Request.RouteHistory.RemoveAt(0);
		Request.RouteHistory.Add(Request.Start);
		Request.Start = Result.Waypoints.Last();
	}
	TestTrue(TEXT("Blocked long direct route uses local chunks"), bSawPartial);
	TestTrue(TEXT("Rolling route reaches the final goal around the wall"), bArrived);

	// 앞과 양옆이 막힌 복도에서는 목표에서 멀어지는 경유지도 허용해야 한다.
	Request.Start = FVector::ZeroVector;
	Request.RouteHistory.Reset();
	Request.SearchBounds = FBox(FVector(-1200, -1200, -1), FVector(1200, 1200, 1));
	const TArray<FBox> CorridorWalls = {
		FBox(FVector(-2000, 150, -100), FVector(600, 250, 100)),
		FBox(FVector(-2000, -250, -100), FVector(600, -150, 100)),
		FBox(FVector(550, -250, -100), FVector(650, 250, 100)) };
	const auto CorridorPosition = [&](const FVector& Position)
	{
		for (const FBox& Box : CorridorWalls) if (Box.IsInsideOrOn(Position)) return false;
		return true;
	};
	const auto CorridorSegment = [&](const FVector& From, const FVector& To)
	{
		for (const FBox& Box : CorridorWalls)
			if (FMath::LineBoxIntersection(Box, From, To, To - From)) return false;
		return true;
	};
	auto BackwardSearch = FlightPathfinding::BeginSearchWithChecks(Request, CorridorPosition, CorridorSegment);
	while (!BackwardSearch->Advance(128, 10000)) {}
	TestEqual(TEXT("Forward blockage permits a backward route chunk"), BackwardSearch->GetResult().Status, EFlightPathStatus::Success);
	if (!BackwardSearch->GetResult().Waypoints.IsEmpty())
		TestTrue(TEXT("Runtime waypoint can move away from the goal to exit a corridor"), BackwardSearch->GetResult().Waypoints.Last().X <= -1000);
	Request.RouteHistory.Add(FVector(-1000, 0, 0));
	BackwardSearch = FlightPathfinding::BeginSearchWithChecks(Request, CorridorPosition, CorridorSegment);
	while (!BackwardSearch->Advance(128, 10000)) {}
	TestEqual(TEXT("Visited corridor remains usable when backtracking is the only exit"),
		BackwardSearch->GetResult().Status, EFlightPathStatus::Success);

	// 완전히 닫힌 공간에서는 경유지를 반환하지 않는다. 예산 실패도 성공으로 바꾸지 않는다.
	Request.Start = FVector::ZeroVector;
	Request.RouteHistory.Reset();
	Request.SearchBounds = FBox(FVector(-1200, -1200, -1), FVector(1200, 1200, 1));
	const auto ClosedPosition = [](const FVector& Position) { return Position.GetAbsMax() < 250.0; };
	const auto ClosedSegment = [&](const FVector& From, const FVector& To) { return ClosedPosition(From) && ClosedPosition(To); };
	auto Search = FlightPathfinding::BeginSearchWithChecks(Request, ClosedPosition, ClosedSegment);
	while (!Search->Advance(128, 10000)) {}
	TestEqual(TEXT("Closed space has no runtime waypoint"), Search->GetResult().Status, EFlightPathStatus::NoPathWithinBounds);
	TestTrue(TEXT("Closed space returns no unsafe partial path"), Search->GetResult().Waypoints.IsEmpty());
	Request.MaxCollisionChecks = 3;
	Search = FlightPathfinding::BeginSearchWithChecks(Request, PositionClear, SegmentClear);
	while (!Search->Advance(128, 10000)) {}
	TestEqual(TEXT("Exhausted local budget remains explicit"), Search->GetResult().Status, EFlightPathStatus::BudgetExceeded);
	TestTrue(TEXT("Budget failure never publishes an unvalidated path"), Search->GetResult().Waypoints.IsEmpty());
	Request.MaxCollisionChecks = 2048;
	Request.TravelHorizon = 0;
	Search = FlightPathfinding::BeginSearchWithChecks(Request, PositionClear, SegmentClear);
	TestEqual(TEXT("Ordinary full-route requests still require the goal inside bounds"), Search->GetResult().Status, EFlightPathStatus::InvalidRequest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightResumableDetourTest,
	"Outlier.Flight.Pathfinding.Resumable.DetourPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightResumableDetourTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.bReuseReverseSegmentChecks = true;
	const FBox Wall(FVector(-50, -150, -1000), FVector(50, 150, 1000));
	int32 PositionCalls[2] = {};
	int32 SegmentCalls[2] = {};
	TSharedPtr<FFlightPathSearch> Searches[2];
	for (int32 Run = 0; Run < 2; ++Run)
	{
		Searches[Run] = FlightPathfinding::BeginSearchWithChecks(Request,
			[&, Run](const FVector& Point)
			{
				++PositionCalls[Run];
				return !Wall.IsInsideOrOn(Point);
			},
			[&, Run](const FVector& From, const FVector& To)
			{
				++SegmentCalls[Run];
				return !FMath::LineBoxIntersection(Wall, From, To, To - From);
			});
		if (!TestTrue(TEXT("Factory returns a search"), Searches[Run].IsValid())) return false;
	}
	FFlightPathSearch& Sliced = *Searches[0];
	TestFalse(TEXT("One check cannot finish a detour"), Sliced.Advance(1, 10000.0));
	TestEqual(TEXT("Yield is not lifetime budget exhaustion"), Sliced.GetResult().Status, EFlightPathStatus::InProgress);
	TestTrue(TEXT("Yield exposes no route"), Sliced.GetResult().Waypoints.IsEmpty());
	const int32 InitialChecks = Sliced.GetResult().CollisionChecks;
	// The search must own its request rather than reference this caller's mutable object.
	const FVector OriginalGoal = Request.Goal;
	Request.Goal = FVector(9999, 9999, 9999);
	TestEqual(TEXT("Search preserves original request"), Sliced.GetRequest().Goal, OriginalGoal);
	Request.Goal = OriginalGoal;
	if (!FinishSearch(*this, Sliced, 68) || !FinishSearch(*this, *Searches[1], Request.MaxCollisionChecks)) return false;
	const FFlightPathResult& Result = Sliced.GetResult();
	const FFlightPathResult& Large = Searches[1]->GetResult();
	CheckPath(*this, Request, Result, Wall);
	CheckPath(*this, Request, Large, Wall);
	TestTrue(TEXT("Detour spans multiple slices"), Result.SearchSlices > 1);
	TestTrue(TEXT("Checks accumulate after the first slice"), Result.CollisionChecks > InitialChecks);
	TestEqual(TEXT("Slicing does not repeat position predicates"), PositionCalls[0], PositionCalls[1]);
	TestEqual(TEXT("Slicing does not repeat segment predicates"), SegmentCalls[0], SegmentCalls[1]);
	TestEqual(TEXT("Slicing preserves total checks"), Result.CollisionChecks, Large.CollisionChecks);
	TestEqual(TEXT("Slicing preserves node expansion"), Result.SearchNodes, Large.SearchNodes);
	TestEqual(TEXT("Segment cache survives yields"), Result.SegmentCacheHits, Large.SegmentCacheHits);
	TestTrue(TEXT("Slicing preserves the exact route"), Result.Waypoints == Large.Waypoints);
	TestTrue(TEXT("Slice timing is finite and nonnegative"), FMath::IsFinite(Result.PeakSliceMilliseconds) && Result.PeakSliceMilliseconds >= 0.0);
	TestTrue(TEXT("Wall timing is finite and nonnegative"), FMath::IsFinite(Result.WallMilliseconds) && Result.WallMilliseconds >= 0.0);
	const int32 CompletedChecks = Result.CollisionChecks;
	const int32 CompletedNodes = Result.SearchNodes;
	TestTrue(TEXT("Completed search stays complete"), Sliced.Advance(1, 10000.0));
	TestEqual(TEXT("Completed search performs no more checks"), Sliced.GetResult().CollisionChecks, CompletedChecks);
	TestEqual(TEXT("Completed search expands no more nodes"), Sliced.GetResult().SearchNodes, CompletedNodes);
	TestEqual(TEXT("Every predicate invocation is accounted for"), PositionCalls[0] + SegmentCalls[0], CompletedChecks);
	// Exhausting a sealed graph guarantees reverse-edge cache opportunities.
	const FBox Barrier(FVector(-50, -1000, -1000), FVector(50, 1000, 1000));
	FFlightPathResult CachedResults[2];
	for (int32 Run = 0; Run < 2; ++Run)
	{
		TSharedPtr<FFlightPathSearch> Cached = FlightPathfinding::BeginSearchWithChecks(Request,
			[&](const FVector& Point) { return !Barrier.IsInsideOrOn(Point); },
			[&](const FVector& From, const FVector& To)
			{
				return !FMath::LineBoxIntersection(Barrier, From, To, To - From);
			});
		if (!TestTrue(TEXT("Sealed cache search exists"), Cached.IsValid())) return false;
		if (!FinishSearch(*this, *Cached, Run == 0 ? 68 : Request.MaxCollisionChecks)) return false;
		CachedResults[Run] = Cached->GetResult();
	}
	TestEqual(TEXT("Sealed searches have the same terminal status"), CachedResults[0].Status, CachedResults[1].Status);
	TestTrue(TEXT("Sealed fixture exercises cache reuse"), CachedResults[0].SegmentCacheHits > 0);
	TestEqual(TEXT("Sealed graph retains cache entries between slices"), CachedResults[0].SegmentCacheHits, CachedResults[1].SegmentCacheHits);
	TestEqual(TEXT("Sealed graph does not repeat checks on resume"), CachedResults[0].CollisionChecks, CachedResults[1].CollisionChecks);
	TestEqual(TEXT("Sealed graph does not repeat node expansion on resume"), CachedResults[0].SearchNodes, CachedResults[1].SearchNodes);
	TestTrue(TEXT("Sealed graph exposes no unsafe partial route"), CachedResults[0].Waypoints.IsEmpty() && CachedResults[1].Waypoints.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightResumableCancellationTest,
	"Outlier.Flight.Pathfinding.Resumable.Cancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightResumableCancellationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	bool bCancel = false;
	Request.ShouldCancel = [&] { return bCancel; };
	int32 PredicateCalls = 0;
	const auto Begin = [&]()
	{
		return FlightPathfinding::BeginSearchWithChecks(Request,
			[&](const FVector&) { ++PredicateCalls; return true; },
			[&](const FVector&, const FVector&) { ++PredicateCalls; return false; });
	};
	TSharedPtr<FFlightPathSearch> Search = Begin();
	if (!TestTrue(TEXT("Cancellation search exists"), Search.IsValid())) return false;
	TestFalse(TEXT("Search yields before cancellation"), Search->Advance(1, 10000.0));
	const int32 CallsBeforeCancel = PredicateCalls;
	bCancel = true;
	TestTrue(TEXT("Cancellation completes pending search"), Search->Advance(1, 10000.0));
	TestEqual(TEXT("Cancellation is explicit"), Search->GetResult().Status, EFlightPathStatus::Cancelled);
	TestTrue(TEXT("Cancellation discards partial route"), Search->GetResult().Waypoints.IsEmpty());
	TestEqual(TEXT("Cancellation performs no additional collision work"), PredicateCalls, CallsBeforeCancel);
	TestTrue(TEXT("Cancelled search stays complete"), Search->Advance(100, 10000.0));
	TestEqual(TEXT("Cancelled search cannot resume collision work"), PredicateCalls, CallsBeforeCancel);
	PredicateCalls = 0;
	Search = Begin();
	if (!TestTrue(TEXT("Initially cancelled search exists"), Search.IsValid())) return false;
	TestTrue(TEXT("Initial cancellation completes immediately"), Search->Advance(100, 10000.0));
	TestEqual(TEXT("Initial cancellation reports Cancelled"), Search->GetResult().Status, EFlightPathStatus::Cancelled);
	TestEqual(TEXT("Initial cancellation prevents predicate calls"), PredicateCalls, 0);
	TestTrue(TEXT("Initial cancellation has no route"), Search->GetResult().Waypoints.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightResumableLifetimeBudgetTest,
	"Outlier.Flight.Pathfinding.Resumable.LifetimeBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightResumableLifetimeBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (int32 Limit = 0; Limit < 2; ++Limit)
	{
		FFlightPathRequest Request = MakeRequest();
		if (Limit == 0) Request.MaxCollisionChecks = 80;
		else Request.MaxSearchNodes = 2;
		// A sealed wall allows useful exploration but never a safe complete route.
		const FBox Wall(FVector(-50, -1000, -1000), FVector(50, 1000, 1000));
		TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearchWithChecks(Request,
			[&](const FVector& Point) { return !Wall.IsInsideOrOn(Point); },
			[&](const FVector& From, const FVector& To)
			{
				return !FMath::LineBoxIntersection(Wall, From, To, To - From);
			});
		if (!TestTrue(TEXT("Budget search exists"), Search.IsValid())) return false;
		TestFalse(TEXT("Small slice yields before lifetime exhaustion"), Search->Advance(1, 10000.0));
		if (!FinishSearch(*this, *Search, 68)) return false;
		const FFlightPathResult& Result = Search->GetResult();
		TestEqual(TEXT("Lifetime exhaustion is BudgetExceeded"), Result.Status, EFlightPathStatus::BudgetExceeded);
		TestTrue(TEXT("Lifetime checks never reset per slice"), Result.CollisionChecks <= Request.MaxCollisionChecks);
		TestTrue(TEXT("Lifetime nodes never reset per slice"), Result.SearchNodes <= Request.MaxSearchNodes);
		TestTrue(TEXT("Budget failure excludes unsafe partial route"), Result.Waypoints.IsEmpty());
		TestTrue(TEXT("Lifetime limit spans multiple slices"), Result.SearchSlices > 1);
		TestTrue(TEXT("Budget failure stays complete"), Search->Advance(1000, 10000.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightResumableNarrowMixedGraphTest,
	"Outlier.Flight.Pathfinding.Resumable.NarrowMixedGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightResumableNarrowMixedGraphTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector(800, 50, 0);
	Request.Goal = FVector(500, 0, 0);
	Request.SearchBounds = FBox(FVector(450, -260, -1), FVector(900, 260, 1));
	Request.LongConnectionStride = 4;
	Request.bAdaptiveResolution = true;
	Request.bLimitDistantGoalChecks = true;
	const FBox Wall(FVector(610, -230, -1000), FVector(690, 230, 1000));
	TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearchWithChecks(Request,
		[&](const FVector& Point) { return !Wall.IsInsideOrOn(Point); },
		[&](const FVector& From, const FVector& To)
		{
			return !FMath::LineBoxIntersection(Wall, From, To, To - From);
		}, [](const FVector&, const FVector&) { return false; });
	if (!TestTrue(TEXT("Narrow mixed search exists"), Search.IsValid())) return false;
	if (!FinishSearch(*this, *Search, 68)) return false;
	CheckPath(*this, Request, Search->GetResult(), Wall);
	TestEqual(TEXT("Offset narrow route uses one mixed graph"), Search->GetResult().SearchPasses, 1);
	TestTrue(TEXT("Narrow graph persists across slices"), Search->GetResult().SearchSlices > 1);
	TestTrue(TEXT("Narrow graph retains small neighbours when every wide region is blocked"), Search->GetResult().FineExpansionNodes > 0);
	TestEqual(TEXT("Blocked wide regions never enable long expansion"), Search->GetResult().WideExpansionNodes, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightHierarchicalLongDistanceTest,
	"Outlier.Flight.Pathfinding.Resumable.Hierarchy.LongDistanceCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightHierarchicalLongDistanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(12000, 0, 0);
	Request.SearchBounds = FBox(FVector(-1500, -2400, -1), FVector(13500, 2400, 1));
	Request.CellSize = 150.0f;
	Request.LongConnectionStride = 4;
	Request.HeuristicScale = 1.25;
	Request.bAdaptiveResolution = true;
	Request.bHierarchicalResolution = true;
	Request.bReuseReverseSegmentChecks = true;
	Request.bLimitDistantGoalChecks = true;
	Request.bSimplifyPath = true;
	Request.MaxCollisionChecks = 8196;
	Request.MaxSearchNodes = 2048;
	const FBox Wall(FVector(4500, -1200, -1000), FVector(4800, 1200, 1000));
	auto Begin = [&Wall](const FFlightPathRequest& Input)
	{
		return FlightPathfinding::BeginSearchWithChecks(Input,
			[&Wall](const FVector& Point) { return !Wall.IsInsideOrOn(Point); },
			[&Wall](const FVector& From, const FVector& To)
			{
				return !FMath::LineBoxIntersection(Wall, From, To, To - From);
			}, [&Wall](const FVector& Point, const FVector& Extent)
			{
				return !FBox(Point - Extent, Point + Extent).Intersect(Wall);
			});
	};
	TSharedPtr<FFlightPathSearch> Hierarchy = Begin(Request);
	if (!FinishSearch(*this, *Hierarchy, 68)) return false;
	const FFlightPathResult& Result = Hierarchy->GetResult();
	CheckPath(*this, Request, Result, Wall);
	TestEqual(TEXT("120m detour is found before fine refinement"), Result.SearchPasses, 1);
	TestEqual(TEXT("An open coarse route never enables fine neighbours globally"), Result.RefinedSearchNodes, 0);
	TestTrue(TEXT("Coarse path leaves at least half the check budget unused"), Result.CollisionChecks < Request.MaxCollisionChecks / 2);

	Request.bHierarchicalResolution = false;
	TSharedPtr<FFlightPathSearch> Mixed = Begin(Request);
	if (!FinishSearch(*this, *Mixed, 68)) return false;
	TestTrue(TEXT("Coarse-first needs fewer collision checks than the previous mixed graph"),
		Result.CollisionChecks < Mixed->GetResult().CollisionChecks);
	AddInfo(FString::Printf(TEXT("120m Hierarchy Checks=%d Nodes=%d Passes=%d; Mixed Checks=%d Nodes=%d Status=%s"),
		Result.CollisionChecks, Result.SearchNodes, Result.SearchPasses,
		Mixed->GetResult().CollisionChecks, Mixed->GetResult().SearchNodes,
		*UEnum::GetValueAsString(Mixed->GetResult().Status)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightHierarchicalEntranceTest,
	"Outlier.Flight.Pathfinding.Resumable.Hierarchy.NarrowEntranceAndBlockedDoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightHierarchicalEntranceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(2000, 0, 0);
	Request.SearchBounds = FBox(FVector(-100, -700, -1), FVector(2100, 700, 1));
	Request.LongConnectionStride = 4;
	Request.bAdaptiveResolution = true;
	Request.bHierarchicalResolution = true;
	Request.bReuseReverseSegmentChecks = true;
	Request.bLimitDistantGoalChecks = true;
	const FBox LowerWall(FVector(900, -1000, -1000), FVector(1100, 150, 1000));
	const FBox UpperWall(FVector(900, 250, -1000), FVector(1100, 1000, 1000));
	const FBox Door(FVector(900, 150, -1000), FVector(1100, 250, 1000));
	// 두 입구를 엇갈리게 배치해 큰 격자의 지점에서 목적지로 바로 연결되는 우연한 성공을 막는다.
	const FBox SecondLowerWall(FVector(1400, -1000, -1000), FVector(1600, -250, 1000));
	const FBox SecondUpperWall(FVector(1400, -150, -1000), FVector(1600, 1000, 1000));

	for (const bool bDoorClosed : { false, true })
	{
		TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearchWithChecks(Request,
			[=](const FVector& Point)
			{
				return !LowerWall.IsInsideOrOn(Point) && !UpperWall.IsInsideOrOn(Point)
					&& !SecondLowerWall.IsInsideOrOn(Point) && !SecondUpperWall.IsInsideOrOn(Point)
					&& (!bDoorClosed || !Door.IsInsideOrOn(Point));
			}, [=](const FVector& From, const FVector& To)
			{
				return !FMath::LineBoxIntersection(LowerWall, From, To, To - From)
					&& !FMath::LineBoxIntersection(UpperWall, From, To, To - From)
					&& !FMath::LineBoxIntersection(SecondLowerWall, From, To, To - From)
					&& !FMath::LineBoxIntersection(SecondUpperWall, From, To, To - From)
					&& (!bDoorClosed || !FMath::LineBoxIntersection(Door, From, To, To - From));
			}, [](const FVector&, const FVector&) { return true; });
		if (!FinishSearch(*this, *Search, 68)) return false;
		const FFlightPathResult& Result = Search->GetResult();
		TestEqual(TEXT("Off-phase entrance triggers refinement in the retained graph"), Result.SearchPasses, 2);
		TestTrue(TEXT("Refinement keeps its original collision budget"), Result.CollisionChecks <= Request.MaxCollisionChecks);
		TestTrue(TEXT("Refinement keeps its original node budget"), Result.SearchNodes <= Request.MaxSearchNodes);
		TestTrue(TEXT("Only blocked coarse frontier nodes enable fine neighbours"),
			Result.RefinedSearchNodes > 0 && Result.RefinedSearchNodes <= Result.RefinementCandidates);
		if (bDoorClosed)
		{
			TestEqual(TEXT("A sealed door is not reported as a valid route"), Result.Status, EFlightPathStatus::NoPathWithinBounds);
			TestTrue(TEXT("A failed route exposes no waypoints"), Result.Waypoints.IsEmpty());
		}
		else
		{
			CheckPath(*this, Request, Result, LowerWall);
			CheckPath(*this, Request, Result, UpperWall);
			CheckPath(*this, Request, Result, SecondLowerWall);
			CheckPath(*this, Request, Result, SecondUpperWall);
			TestTrue(TEXT("Refinement preserves collision cache entries"), Result.SegmentCacheHits > 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightHierarchicalProgressTest,
	"Outlier.Flight.Pathfinding.Resumable.Hierarchy.ProgressingBoundaryStaysCoarse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightHierarchicalProgressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(16000, 400, 0);
	Request.SearchBounds = FBox(FVector(-100, -500, -1), FVector(16100, 500, 1));
	Request.LongConnectionStride = 4;
	Request.bAdaptiveResolution = true;
	Request.bHierarchicalResolution = true;
	Request.bReuseReverseSegmentChecks = true;
	Request.bLimitDistantGoalChecks = true;
	Request.MaxSearchNodes = 128;
	// 한쪽 벽은 매 지점의 일부 연결을 막지만, 벽을 따라 목표 쪽으로 진행하는 큰 연결은 열려 있다.
	const FBox Wall(FVector(-100, 100, -1000), FVector(14000, 1000, 1000));
	TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearchWithChecks(Request,
		[Wall](const FVector& Point) { return !Wall.IsInsideOrOn(Point); },
		[Wall](const FVector& From, const FVector& To)
		{
			return !FMath::LineBoxIntersection(Wall, From, To, To - From);
		}, [](const FVector&, const FVector&) { return true; });
	if (!FinishSearch(*this, *Search, 68)) return false;
	const FFlightPathResult& Result = Search->GetResult();
	CheckPath(*this, Request, Result, Wall);
	TestEqual(TEXT("Node threshold exercises selective refinement"), Result.SearchPasses, 2);
	TestTrue(TEXT("Blocked side links remain potential fallback candidates"), Result.RefinementCandidates > 0);
	TestTrue(TEXT("Clear forward links are recorded"), Result.CoarseProgressNodes > 0);
	TestEqual(TEXT("An advancing wall-side route needs no fine expansion"), Result.RefinedSearchNodes, 0);
	TestEqual(TEXT("An advancing route never consumes fallback refinement"), Result.FallbackRefinedNodes, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightHierarchicalLocalFrontierTest,
	"Outlier.Flight.Pathfinding.Resumable.Hierarchy.LongOffsetEntrance3D",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightHierarchicalLocalFrontierTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(12000, 0, 0);
	Request.SearchBounds = FBox(FVector(-100, -700, -100), FVector(12100, 700, 100));
	Request.LongConnectionStride = 4;
	Request.bAdaptiveResolution = true;
	Request.bHierarchicalResolution = true;
	Request.bReuseReverseSegmentChecks = true;
	Request.bLimitDistantGoalChecks = true;
	Request.bSimplifyPath = true;
	// 바닥/천장을 포함하는 3D 격자에서 큰 연결이 건너뛰는 두 입구를 재현한다.
	const TArray<FBox> Walls = {
		FBox(FVector(900, -1000, -1000), FVector(1100, 150, 1000)),
		FBox(FVector(900, 250, -1000), FVector(1100, 1000, 1000)),
		FBox(FVector(1400, -1000, -1000), FVector(1600, -250, 1000)),
		FBox(FVector(1400, -150, -1000), FVector(1600, 1000, 1000)) };
	TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearchWithChecks(Request,
		[Walls](const FVector& Point)
		{
			for (const FBox& Wall : Walls) if (Wall.IsInsideOrOn(Point)) return false;
			return true;
		}, [Walls](const FVector& From, const FVector& To)
		{
			for (const FBox& Wall : Walls)
				if (FMath::LineBoxIntersection(Wall, From, To, To - From)) return false;
			return true;
		}, [](const FVector&, const FVector&) { return true; });
	if (!FinishSearch(*this, *Search, 68)) return false;
	const FFlightPathResult& Result = Search->GetResult();
	for (const FBox& Wall : Walls) CheckPath(*this, Request, Result, Wall);
	TestTrue(TEXT("Narrow frontier enables fine neighbours"), Result.RefinedSearchNodes > 0);
	TestTrue(TEXT("Refinement does not reopen the entire discovered graph"), Result.RefinedSearchNodes < Result.SearchNodes);
	TestTrue(TEXT("Local refinement respects collision budget"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	AddInfo(FString::Printf(TEXT("120m OffsetEntrance Checks=%d Nodes=%d Candidates=%d FineEnabled=%d CpuMs=%.3f"),
		Result.CollisionChecks, Result.SearchNodes, Result.RefinementCandidates, Result.RefinedSearchNodes, Result.ElapsedMilliseconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightHierarchicalSafetyTest,
	"Outlier.Flight.Pathfinding.Resumable.Hierarchy.BudgetCancellationAndRevalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightHierarchicalSafetyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.bAdaptiveResolution = true;
	Request.bHierarchicalResolution = true;
	Request.LongConnectionStride = 4;
	const FBox Wall(FVector(-50, -150, -1000), FVector(50, 150, 1000));
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		bool bCancel = false;
		bool bBlock = false;
		int32 StartChecks = 0;
		// 시작/목표 검사 이후 예산을 소진시켜 탐색 최적화와 무관하게 실패를 재현한다.
		Request.MaxCollisionChecks = Mode == 0 ? 3 : 20000;
		Request.ShouldCancel = [&bCancel]() { return bCancel; };
		TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearchWithChecks(Request,
			[&](const FVector& Point)
			{
				// 최초 캐시 생성 후 성공 직전 재검사에서 문이 닫히는 상황도 재현한다.
				if (Mode == 3 && Point.Equals(Request.Start, 0.01) && ++StartChecks == 2) bBlock = true;
				return (Mode == 3 || !bBlock) && !Wall.IsInsideOrOn(Point);
			},
			[&](const FVector& From, const FVector& To)
			{
				return !bBlock && !FMath::LineBoxIntersection(Wall, From, To, To - From);
			}, [](const FVector&, const FVector&) { return true; });
		Search->Advance(2, 10000.0);
		bCancel = Mode == 1;
		bBlock = Mode == 2;
		if (!FinishSearch(*this, *Search, 68)) return false;
		const FFlightPathResult& Result = Search->GetResult();
		const EFlightPathStatus Expected = Mode == 0 ? EFlightPathStatus::BudgetExceeded
			: Mode == 1 ? EFlightPathStatus::Cancelled
			: Mode == 2 ? EFlightPathStatus::BlockedEndpoint : EFlightPathStatus::BlockedSegment;
		TestEqual(FString::Printf(TEXT("Hierarchical search preserves failure semantics (mode %d)"), Mode), Result.Status, Expected);
		TestTrue(TEXT("Failure does not expose an unvalidated coarse route"), Result.Waypoints.IsEmpty());
		TestTrue(TEXT("All passes share one check cap"), Result.CollisionChecks <= Request.MaxCollisionChecks);
		if (Mode == 3) TestTrue(TEXT("Coarse route gets fresh validation before handoff"), bBlock);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightResumableWorldTest,
	"Outlier.Flight.Pathfinding.Resumable.WorldLifetimeAndRevalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightResumableWorldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("FlightResumableTest")));
	if (!TestNotNull(TEXT("Resumable test world exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const auto Cleanup = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	AActor* Drone = World->SpawnActor<AActor>();
	AActor* Wall = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Resumable owner exists"), Drone) || !TestNotNull(TEXT("Dynamic wall exists"), Wall)
		|| !TestNotNull(TEXT("Physics scene exists"), World->GetPhysicsScene()))
	{
		Cleanup();
		return false;
	}
	UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Drone);
	Capsule->InitCapsuleSize(20.0f, 40.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	Drone->SetRootComponent(Capsule);
	Capsule->RegisterComponent();
	FFlightPathRequest Request = MakeRequest();
	Drone->SetActorLocation(Request.Start);
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	WallBox->SetBoxExtent(FVector(30, 150, 500));
	WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallBox->SetCollisionObjectType(ECC_WorldStatic);
	WallBox->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->SetRootComponent(WallBox);
	WallBox->RegisterComponent();
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();
	TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearch(*World, *Capsule, Request);
	if (!TestTrue(TEXT("World factory returns a search"), Search.IsValid()))
	{
		Cleanup();
		return false;
	}
	FlightPathfinding::AdvanceSearch(*World, *Search, 68, 10000.0);
	// Drive the remaining work directly; the world scheduler permits only one slice per frame.
	if (!FinishSearch(*this, *Search, 68))
	{
		Cleanup();
		return false;
	}
	TestEqual(TEXT("World capsule detour succeeds"), Search->GetResult().Status, EFlightPathStatus::Success);
	TestTrue(TEXT("World detour exposes a route"), Search->GetResult().Waypoints.Num() > 2);
	if (Search->GetResult().Waypoints.Num() > 1)
	{
		FFlightPathRequest Segment = Request;
		Segment.Goal = Search->GetResult().Waypoints[1];
		TestEqual(TEXT("Completed first segment is initially clear"),
			FlightPathfinding::CheckSegment(*World, *Capsule, Segment), EFlightPathStatus::Success);
		Wall->SetActorLocation((Segment.Start + Segment.Goal) * 0.5);
		WallBox->SetBoxExtent(FVector(5, 5, 100));
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		TestTrue(TEXT("Moved wall invalidates a previously completed route segment"),
			FlightPathfinding::CheckSegment(*World, *Capsule, Segment) != EFlightPathStatus::Success);
	}
	Wall->SetActorLocation(FVector::ZeroVector);
	WallBox->SetBoxExtent(FVector(30, 150, 500));
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();
	Search = FlightPathfinding::BeginSearch(*World, *Capsule, Request);
	if (TestTrue(TEXT("Owner invalidation search exists"), Search.IsValid()))
	{
		TestFalse(TEXT("Owner search is pending before destruction"), Search->Advance(1, 10000.0));
		Drone->Destroy();
		TestTrue(TEXT("Destroyed owner terminates pending search"), Search->Advance(68, 10000.0));
		TestTrue(TEXT("Destroyed owner cannot produce success or stay pending"),
			Search->GetResult().Status != EFlightPathStatus::Success
			&& Search->GetResult().Status != EFlightPathStatus::InProgress);
		TestTrue(TEXT("Destroyed owner exposes no route"), Search->GetResult().Waypoints.IsEmpty());
	}
	Search.Reset();
	Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightResumableSchedulerTest,
	"Outlier.Flight.Pathfinding.Resumable.SharedFrameBudgetAndFairness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightResumableSchedulerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("Frame simulation runs on the game thread"), IsInGameThread())) return false;
	// Destroy every simulated-frame world/session before restoring the real frame counter.
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FScopedFlightSearchCVar CheckBudget(TEXT("outlier.Flight.SearchFrameChecks"), TEXT("68"));
	FScopedFlightSearchCVar TimeBudget(TEXT("outlier.Flight.SearchFrameMs"), TEXT("10000"));
	if (!TestTrue(TEXT("Scheduler check CVar exists"), CheckBudget.IsValid())
		|| !TestTrue(TEXT("Scheduler time CVar exists"), TimeBudget.IsValid())) return false;
	FScopedFlightSearchWorld Fixture;
	FScopedFlightSearchWorld OtherFixture;
	if (!TestNotNull(TEXT("Scheduler world exists"), Fixture.World)
		|| !TestNotNull(TEXT("Independent budget world exists"), OtherFixture.World)) return false;
	FFlightPathRequest Request = MakeRequest();
	const FBox Barrier(FVector(-50, -1000, -1000), FVector(50, 1000, 1000));
	const auto Begin = [&]()
	{
		return FlightPathfinding::BeginSearchWithChecks(Request,
			[&](const FVector& Point) { return !Barrier.IsInsideOrOn(Point); },
			[&](const FVector& From, const FVector& To)
			{
				return !FMath::LineBoxIntersection(Barrier, From, To, To - From);
			});
	};
	TSharedPtr<FFlightPathSearch> Searches[3] = { Begin(), Begin(), Begin() };
	TSharedPtr<FFlightPathSearch> Independent = Begin();
	for (const auto& Search : Searches)
		if (!TestTrue(TEXT("Queued search exists"), Search.IsValid())) return false;
	if (!TestTrue(TEXT("Independent search exists"), Independent.IsValid())) return false;
	int32 ProgressFrames[3] = {};
	int32 LastProgressFrame[3] = { -1, -1, -1 };
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		++GFrameCounter;
		int32 FrameChecks = 0;
		// Keep caller order fixed: FIFO must prevent the first caller from monopolizing frames.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FFlightPathSearch& Search = *Searches[Index];
			const int32 Before = Search.GetResult().CollisionChecks;
			FlightPathfinding::AdvanceSearch(*Fixture.World, Search, 68, 10000.0);
			const int32 After = Search.GetResult().CollisionChecks;
			const int32 Slices = Search.GetResult().SearchSlices;
			const int32 Nodes = Search.GetResult().SearchNodes;
			FrameChecks += After - Before;
			if (After > Before)
			{
				++ProgressFrames[Index];
				if (LastProgressFrame[Index] >= 0)
					TestTrue(TEXT("FIFO search receives another turn within four frames"), Frame - LastProgressFrame[Index] <= 4);
				LastProgressFrame[Index] = Frame;
			}
			for (int32 Repeat = 0; Repeat < 3; ++Repeat)
				FlightPathfinding::AdvanceSearch(*Fixture.World, Search, 68, 10000.0);
			TestEqual(TEXT("Repeated same-frame calls perform no checks"), Search.GetResult().CollisionChecks, After);
			TestEqual(TEXT("A session advances at most once per frame"), Search.GetResult().SearchSlices, Slices);
			TestEqual(TEXT("Repeated same-frame calls expand no nodes"), Search.GetResult().SearchNodes, Nodes);
			TestEqual(TEXT("Scheduler yield is still pending"), Search.GetResult().Status, EFlightPathStatus::InProgress);
			TestTrue(TEXT("Queued search exposes no partial route"), Search.GetResult().Waypoints.IsEmpty());
		}
		TestTrue(TEXT("Combined search frame checks respect the CVar cap"), FrameChecks >= 0 && FrameChecks <= 68);
		TestTrue(TEXT("A ready queue makes progress every frame"), FrameChecks > 0);
		if (Frame == 0)
		{
			FlightPathfinding::AdvanceSearch(*OtherFixture.World, *Independent, 68, 10000.0);
			TestTrue(TEXT("Another world has an independent same-frame budget"), Independent->GetResult().CollisionChecks > 0);
			TestTrue(TEXT("Independent world also respects its frame cap"), Independent->GetResult().CollisionChecks <= 68);
		}
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(TEXT("Every queued session receives multiple turns"), ProgressFrames[Index] >= 2);
		TestTrue(TEXT("No session is starved at the end of the fixed-order run"), LastProgressFrame[Index] >= 8);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightResumableChangedObstacleTest,
	"Outlier.Flight.Pathfinding.Resumable.ChangedObstacleBeforeCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightResumableChangedObstacleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FFlightPathRequest Request = MakeRequest();
	const FBox Wall(FVector(-50, -150, -1000), FVector(50, 150, 1000));
	int32 StartChecks = 0;
	bool bNewObstruction = false;
	const TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearchWithChecks(Request,
		[&](const FVector& Position)
		{
			// 경로 복원이 끝난 뒤의 출발점 재검사 시점에 장애물이 구간을 차단한 상황을 만든다.
			if (Position.Equals(Request.Start, 0.01) && ++StartChecks == 2) bNewObstruction = true;
			return !Wall.IsInsideOrOn(Position);
		},
		[&](const FVector& From, const FVector& To)
		{
			return !bNewObstruction && !FMath::LineBoxIntersection(Wall, From, To, To - From);
		});
	if (!TestTrue(TEXT("Changing obstacle search exists"), Search.IsValid())) return false;
	if (!FinishSearch(*this, *Search, 68)) return false;
	TestTrue(TEXT("Final validation rechecks the cached start"), bNewObstruction);
	TestEqual(TEXT("Changed geometry prevents stale successful handoff"), Search->GetResult().Status, EFlightPathStatus::BlockedSegment);
	TestTrue(TEXT("Changed geometry exposes no route"), Search->GetResult().Waypoints.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightCapsuleExtentTest,
	"Outlier.Flight.Pathfinding.CapsuleExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightNarrowGridPhaseTest,
	"Outlier.Flight.Pathfinding.NarrowGridPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightMultiResolutionTest,
	"Outlier.Flight.Pathfinding.MultiResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightMixedConnectionsTest,
	"Outlier.Flight.Pathfinding.MixedConnections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightAdaptiveResolutionTest,
	"Outlier.Flight.Pathfinding.AdaptiveResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightAdaptiveResolutionTest::RunTest(const FString& Parameters)
{
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(5000, 0, 0);
	Request.SearchBounds = FBox(FVector(-800, -800, -800), FVector(5800, 800, 800));
	Request.LongConnectionStride = 4;
	Request.bAdaptiveResolution = true;
	Request.bLimitDistantGoalChecks = true;
	Request.bReuseReverseSegmentChecks = true;
	Request.bSimplifyPath = true;
	Request.HeuristicScale = 1.25;
	const FBox Wall(FVector(2000, -200, -1000), FVector(2100, 200, 1000));
	const auto PositionClear = [&](const FVector& Point) { return !Wall.IsInsideOrOn(Point); };
	const auto SegmentClear = [&](const FVector& From, const FVector& To)
	{
		return !FMath::LineBoxIntersection(Wall, From, To, To - From);
	};
	double LargestRegionHalfExtent = 0.0;
	const auto RegionClear = [&](const FVector& Center, const FVector& Extent)
	{
		LargestRegionHalfExtent = FMath::Max(LargestRegionHalfExtent, Extent.GetAbsMax());
		return !Wall.Intersect(FBox(Center - Extent, Center + Extent));
	};
	FFlightPathResult Result = FlightPathfinding::FindPathWithChecks(Request, PositionClear, SegmentClear, RegionClear);
	CheckPath(*this, Request, Result, Wall);
	TestTrue(TEXT("Open regions use wide expansion"), Result.WideExpansionNodes > 0);
	TestEqual(TEXT("Local clearance probe spans 1.5m before capsule padding"), LargestRegionHalfExtent, 75.0);
	TestTrue(TEXT("Start and obstacle regions use fine expansion"), Result.FineExpansionNodes > 0);
	TestTrue(TEXT("Detour compression actually removes grid points"), Result.Waypoints.Num() < Result.RawWaypointCount);
	TestTrue(TEXT("All passes share the collision budget"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	TestTrue(TEXT("All passes share the node budget"), Result.SearchNodes <= Request.MaxSearchNodes);
	AddInfo(FString::Printf(TEXT("Adaptive Checks=%d Nodes=%d Wide=%d Fine=%d Passes=%d"),
		Result.CollisionChecks, Result.SearchNodes, Result.WideExpansionNodes, Result.FineExpansionNodes, Result.SearchPasses));
	FFlightPathRequest Baseline = Request;
	Baseline.bAdaptiveResolution = false;
	Baseline.LongConnectionStride = 1;
	const FFlightPathResult BaselineResult = FlightPathfinding::FindPathWithChecks(Baseline, PositionClear, SegmentClear);
	CheckPath(*this, Baseline, BaselineResult, Wall);
	AddInfo(FString::Printf(TEXT("FineBaseline Checks=%d Nodes=%d Ms=%.3f AdaptiveMs=%.3f"),
		BaselineResult.CollisionChecks, BaselineResult.SearchNodes, BaselineResult.ElapsedMilliseconds, Result.ElapsedMilliseconds));

	// 비행 높이가 제한되어도 수평으로 열린 공간은 큰 간격을 사용할 수 있다.
	FFlightPathRequest Horizontal = Request;
	Horizontal.SearchBounds.Min.Z = -1;
	Horizontal.SearchBounds.Max.Z = 1;
	const FFlightPathResult HorizontalResult = FlightPathfinding::FindPathWithChecks(Horizontal, PositionClear, SegmentClear, RegionClear);
	CheckPath(*this, Horizontal, HorizontalResult, Wall);
	TestTrue(TEXT("Horizontal clearance works without vertical clearance"), HorizontalResult.WideExpansionNodes > 0);

	// 100m 우회도 예산을 늘리는 대신 큰 기본 격자로 먼저 검사하고 실제 연결은 그대로 검증한다.
	FFlightPathRequest LongRange = Request;
	LongRange.Goal = FVector(10000, 0, 0);
	LongRange.SearchBounds.Max.X = 10800;
	LongRange.CoarseCellSize = 200.0f;
	LongRange.MaxSearchNodes = 512;
	LongRange.MaxCollisionChecks = 2048;
	const FFlightPathResult LongRangeResult = FlightPathfinding::FindPathWithChecks(LongRange, PositionClear, SegmentClear, RegionClear);
	CheckPath(*this, LongRange, LongRangeResult, Wall);
	TestTrue(TEXT("Long range uses larger cells before fine fallback"),
		LongRangeResult.CellSizeUsed == 200.0f || LongRangeResult.SearchPasses == 2);
	TestTrue(TEXT("Long range stays within node budget"), LongRangeResult.SearchNodes <= 512);
	TestTrue(TEXT("Long range stays within collision budget"), LongRangeResult.CollisionChecks <= 2048);
	TestTrue(TEXT("Compression preserves or reduces point count"),
		LongRangeResult.Waypoints.Num() <= LongRangeResult.RawWaypointCount);
	AddInfo(FString::Printf(TEXT("LongRange100m Checks=%d Nodes=%d Points=%d Raw=%d Passes=%d Cell=%.1f"),
		LongRangeResult.CollisionChecks, LongRangeResult.SearchNodes, LongRangeResult.Waypoints.Num(),
		LongRangeResult.RawWaypointCount, LongRangeResult.SearchPasses, LongRangeResult.CellSizeUsed));

	// 개방도 판정이 좁은 출구를 놓쳐도 연결 검증을 생략하지 않고 남은 예산의 정밀 탐색으로 복구한다.
	Request.Goal = FVector(2000, 50, 0);
	Request.bSimplifyPath = false;
	const auto CorridorPositionClear = [&](const FVector& Point)
	{
		return Point == Request.Goal || (Point.Y == 0 && Point.Z == 0 && Point.X >= 0 && Point.X <= 600);
	};
	const auto CorridorSegmentClear = [&](const FVector& From, const FVector& To)
	{
		if (From == FVector(600, 0, 0) && To == Request.Goal) return true;
		return From.Y == 0 && To.Y == 0 && From.Z == 0 && To.Z == 0
			&& From.X >= 0 && To.X >= 0 && From.X <= 600 && To.X <= 600;
	};
	Result = FlightPathfinding::FindPathWithChecks(Request, CorridorPositionClear, CorridorSegmentClear,
		[](const FVector&, const FVector&) { return true; });
	TestEqual(TEXT("Fine fallback recovers off-phase exit"), Result.Status, EFlightPathStatus::Success);
	TestEqual(TEXT("Recovery uses a bounded second pass"), Result.SearchPasses, 2);
	TestTrue(TEXT("Recovery retains exit waypoint"), Result.Waypoints.Contains(FVector(600, 0, 0)));
	TestTrue(TEXT("Recovery does not multiply collision budget"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	TestTrue(TEXT("Recovery does not multiply node budget"), Result.SearchNodes <= Request.MaxSearchNodes);

	Request.ShouldCancel = [] { return true; };
	Result = FlightPathfinding::FindPathWithChecks(Request, PositionClear, SegmentClear, RegionClear);
	TestEqual(TEXT("Adaptive search honors cancellation"), Result.Status, EFlightPathStatus::Cancelled);
	TestEqual(TEXT("Cancellation prevents collision work"), Result.CollisionChecks, 0);
	Request.ShouldCancel = {};
	Request.MaxCollisionChecks = 1;
	Result = FlightPathfinding::FindPathWithChecks(Request, PositionClear, SegmentClear, RegionClear);
	TestEqual(TEXT("Tiny budget remains bounded"), Result.Status, EFlightPathStatus::BudgetExceeded);
	TestTrue(TEXT("Region checks cannot exceed budget"), Result.CollisionChecks <= 1);
	return true;
}

bool FFlightMixedConnectionsTest::RunTest(const FString& Parameters)
{
	// 큰 격자가 놓치는 좁은 통로를 짧은 연결로 통과하면서 같은 탐색에서 긴 연결도 사용할 수 있어야 한다.
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector(800, 50, 0);
	Request.Goal = FVector(500, 0, 0);
	Request.SearchBounds = FBox(FVector(450, -260, -1), FVector(900, 260, 1));
	Request.LongConnectionStride = 4;
	Request.bLimitDistantGoalChecks = true;
	Request.bSimplifyPath = true;
	const FBox NarrowWall(FVector(610, -230, -1000), FVector(690, 230, 1000));
	FFlightPathResult Result = FindAroundBox(Request, NarrowWall);
	CheckPath(*this, Request, Result, NarrowWall);
	TestEqual(TEXT("Mixed connections need only one search"), Result.SearchPasses, 1);

	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(5000, 0, 0);
	Request.SearchBounds = FBox(FVector(-800, -800, -100), FVector(5800, 800, 100));
	Request.HeuristicScale = 1.25;
	const FBox Wall(FVector(2000, -200, -1000), FVector(2100, 200, 1000));
	Result = FindAroundBox(Request, Wall);
	CheckPath(*this, Request, Result, Wall);
	TestTrue(TEXT("Distant goal probes are skipped before collision work"), Result.SkippedGoalConnections > 0);
	TestTrue(TEXT("Mixed route respects collision limit"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	TestTrue(TEXT("Mixed route respects node limit"), Result.SearchNodes <= Request.MaxSearchNodes);

	// 격자에서는 입구만 밟을 수 있고 그곳에서 목표까지 직선 연결만 가능한 통로도 놓치지 않는다.
	FFlightPathRequest DeferredRequest = Request;
	DeferredRequest.Goal = FVector(2000, 50, 0);
	const FVector Mouth(100, 0, 0);
	const FFlightPathResult DeferredResult = FlightPathfinding::FindPathWithChecks(DeferredRequest,
		[&](const FVector& Point)
		{
			return Point == DeferredRequest.Start || Point == Mouth || Point == DeferredRequest.Goal;
		},
		[&](const FVector& From, const FVector& To)
		{
			return (From == DeferredRequest.Start && To == Mouth)
				|| (From == Mouth && To == DeferredRequest.Goal);
		});
	TestEqual(TEXT("Deferred goal connection recovers narrow exit"), DeferredResult.Status, EFlightPathStatus::Success);
	TestEqual(TEXT("Deferred route retains its entrance"), DeferredResult.Waypoints.Num(), 3);
	TestTrue(TEXT("Deferred connection stays within collision budget"), DeferredResult.CollisionChecks <= DeferredRequest.MaxCollisionChecks);

	// 빈 목적지가 있어도 장벽을 관통하는 긴 연결은 허용하지 않는다.
	const FBox Barrier(FVector(2000, -2000, -2000), FVector(2100, 2000, 2000));
	Result = FindAroundBox(Request, Barrier);
	TestTrue(TEXT("Sealed barrier is not bypassed by long connections"), Result.Status != EFlightPathStatus::Success);
	TestTrue(TEXT("Failure exposes no partial route"), Result.Waypoints.IsEmpty());
	const int32 UncachedChecks = Result.CollisionChecks;
	Request.bReuseReverseSegmentChecks = true;
	const FFlightPathResult CachedBarrier = FindAroundBox(Request, Barrier);
	TestEqual(TEXT("Symmetric cache preserves sealed barrier result"), CachedBarrier.Status, Result.Status);
	TestTrue(TEXT("Symmetric cache reduces sealed search collision work"), CachedBarrier.CollisionChecks < UncachedChecks);
	TestTrue(TEXT("Symmetric search records reuse"), CachedBarrier.SegmentCacheHits > 0);
	Request.ShouldCancel = [] { return true; };
	TestEqual(TEXT("Mixed search honors cancellation"), FindAroundBox(Request, Wall).Status, EFlightPathStatus::Cancelled);
	return true;
}

bool FFlightMultiResolutionTest::RunTest(const FString& Parameters)
{
	// 긴 우회에서 노드 간격은 커져도 실제 장애물 검사는 생략하지 않는다.
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(5000, 0, 0);
	Request.SearchBounds = FBox(FVector(-800, -800, -800), FVector(5800, 800, 800));
	Request.CoarseCellSize = 400.0f;
	Request.bSimplifyPath = true;
	const FBox Wall(FVector(2000, -200, -1000), FVector(2100, 200, 1000));
	FFlightPathResult Result = FindAroundBox(Request, Wall);
	CheckPath(*this, Request, Result, Wall);
	TestEqual(TEXT("Long detour uses coarse grid"), Result.CellSizeUsed, 400.0f);
	TestEqual(TEXT("Coarse success does not repeat fine search"), Result.SearchPasses, 1);
	TestTrue(TEXT("Simplification never adds waypoints"), Result.Waypoints.Num() <= Result.RawWaypointCount);
	TestTrue(TEXT("Combined collision work stays bounded"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	TestTrue(TEXT("Combined node work stays bounded"), Result.SearchNodes <= Request.MaxSearchNodes);
	TestTrue(TEXT("Blocked positions are counted"), Result.RejectedPositions > 0);
	TestTrue(TEXT("Blocked segments are counted"), Result.RejectedSegments > 0);
	TestTrue(TEXT("Successful detour connects to goal"), Result.ClearGoalConnections > 0);

	// 큰 격자로 표현할 수 없는 통로는 작은 격자의 남은 예산으로 찾아야 한다.
	Request.Start = FVector(800, 50, 0);
	Request.Goal = FVector(500, 0, 0);
	Request.SearchBounds = FBox(FVector(450, -260, -1), FVector(900, 260, 1));
	const FBox NarrowWall(FVector(610, -230, -1000), FVector(690, 230, 1000));
	Result = FindAroundBox(Request, NarrowWall);
	CheckPath(*this, Request, Result, NarrowWall);
	TestEqual(TEXT("Narrow corridor retries at fine resolution"), Result.CellSizeUsed, Request.CellSize);
	TestEqual(TEXT("Both passes are reported"), Result.SearchPasses, 2);
	TestTrue(TEXT("Bounds rejection is distinct from collision rejection"), Result.OutOfBoundsNeighbours > 0);
	TestTrue(TEXT("Fallback and simplification share collision budget"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	TestTrue(TEXT("Fallback shares node budget"), Result.SearchNodes <= Request.MaxSearchNodes);

	Request.MaxCollisionChecks = 8;
	Result = FindAroundBox(Request, NarrowWall);
	TestEqual(TEXT("Insufficient combined budget is explicit"), Result.Status, EFlightPathStatus::BudgetExceeded);
	TestTrue(TEXT("Failed search never exposes a partial route"), Result.Waypoints.IsEmpty());
	TestTrue(TEXT("Even failed fallback stays bounded"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	Request.ShouldCancel = [] { return true; };
	Result = FindAroundBox(Request, NarrowWall);
	TestEqual(TEXT("Cancellation is not retried at another resolution"), Result.Status, EFlightPathStatus::Cancelled);
	TestTrue(TEXT("Cancelled request has no route"), Result.Waypoints.IsEmpty());
	Request = MakeRequest();
	Request.GoalSearchRadius = 100.0f;
	Request.SearchBounds = Request.SearchBounds.ExpandBy(1000.0f);
	Result = FlightPathfinding::FindPathWithChecks(Request,
		[&](const FVector& Position) { return Position.Equals(Request.Start); },
		[](const FVector&, const FVector&) { return true; });
	TestEqual(TEXT("Blocked goal includes all 54 rejected adjustment probes"), Result.RejectedPositions, 55);
	TestEqual(TEXT("No goal connection is queried for blocked candidates"), Result.GoalConnectionChecks, 0);
	return true;
}

bool FFlightNarrowGridPhaseTest::RunTest(const FString& Parameters)
{
	// 캡슐 크기까지 확장한 장애물과 축소한 통로를 모델링한다. 검사 예산 부족과 격자 해상도를 구분한다.
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector(800, 0, 0);
	Request.Goal = FVector(500, 0, 0);
	Request.SearchBounds = FBox(FVector(450, -260, -1), FVector(900, 260, 1));
	const FBox Wall(FVector(610, -230, -1000), FVector(690, 230, 1000));
	TestEqual(TEXT("100cm grid misses the physically open 30cm strip"), FindAroundBox(Request, Wall).Status,
		EFlightPathStatus::NoPathWithinBounds);
	Request.Start.Y = 50;
	TestEqual(TEXT("Shifted start exposes the same corridor without increasing budget"), FindAroundBox(Request, Wall).Status,
		EFlightPathStatus::Success);
	Request.Start.Y = 0;
	Request.CellSize = 50;
	TestEqual(TEXT("Finer grid represents the narrow corridor"), FindAroundBox(Request, Wall).Status, EFlightPathStatus::Success);
	return true;
}

bool FFlightCapsuleExtentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 경계 근처의 구형 드론은 회전 후에도 동일한 공간을 차지해야 한다.
	for (const FRotator Rotation : { FRotator::ZeroRotator, FRotator(0.0, 45.0, 0.0), FRotator(35.0, 60.0, 20.0) })
	{
		TestTrue(TEXT("Sphere extent does not grow with rotation"),
			FlightPathfinding::GetCapsuleWorldExtent(65.0f, 65.0f, Rotation.Quaternion()).Equals(FVector(65.0), 0.001));
		TestTrue(TEXT("Upright capsule extent does not grow with yaw"),
			FlightPathfinding::GetCapsuleWorldExtent(30.0f, 80.0f, FRotator(0.0, Rotation.Yaw, 0.0).Quaternion())
				.Equals(FVector(30.0, 30.0, 80.0), 0.001));
	}
	TestTrue(TEXT("Horizontal capsule preserves its axial length"),
		FlightPathfinding::GetCapsuleWorldExtent(30.0f, 80.0f, FRotator(90.0, 0.0, 0.0).Quaternion())
			.Equals(FVector(80.0, 30.0, 30.0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightPathGeometryTest,
	"Outlier.Flight.Pathfinding.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightPathGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 열린 공간은 A* 없이 직선 경로로 끝나고, 실제 시작점/목적지를 그대로 유지해야 한다.
	FFlightPathRequest Request = MakeRequest();
	const FBox FarObstacle(FVector(1000.0), FVector(1100.0));
	FFlightPathResult Result = FindAroundBox(Request, FarObstacle);
	CheckPath(*this, Request, Result, FarObstacle);
	TestEqual(TEXT("Open space uses only endpoints"), Result.Waypoints.Num(), 2);
	TestEqual(TEXT("Open space avoids AStar"), Result.SearchNodes, 0);
	TestTrue(TEXT("Length matches straight distance"), FMath::IsNearlyEqual(Result.PathLength,
		FVector::Distance(Request.Start, Request.Goal), 0.001));

	// 직선을 막으면 수평 우회를 찾되, 경유점 사이에서 벽 모서리를 가로지르면 안 된다.
	const FBox Wall(FVector(-50.0, -150.0, -500.0), FVector(50.0, 150.0, 500.0));
	Result = FindAroundBox(Request, Wall);
	CheckPath(*this, Request, Result, Wall);
	TestTrue(TEXT("Non-improving connections are pruned before collision queries"), Result.PrunedTraversals > 0);
	TestTrue(TEXT("Wall requires multiple waypoints"), Result.Waypoints.Num() > 2);
	TestTrue(TEXT("Detour is longer than direct line"), Result.PathLength > FVector::Distance(Request.Start, Request.Goal));

	// 수평 우회를 막고 벽 위 공간만 남겨 Z축 우회도 찾는지 확인한다.
	const FBox LowWall(FVector(-50.0, -1000.0, -1000.0), FVector(50.0, 1000.0, 50.0));
	Result = FindAroundBox(Request, LowWall);
	CheckPath(*this, Request, Result, LowWall);
	TestTrue(TEXT("3D detour goes over wall"), Result.Waypoints.ContainsByPredicate(
		[](const FVector& Point)
		{
			return Point.Z > 50.0;
		}));

	// 작은 장애물 뒤의 긴 열린 구간은 끝까지 격자를 확장하지 않고 검증된 직선으로 연결한다.
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(10000.0, 0.0, 0.0);
	Request.SearchBounds = FBox(FVector(-100.0, -300.0, -100.0), FVector(10100.0, 300.0, 100.0));
	Request.MaxSearchNodes = 512;
	Request.MaxCollisionChecks = 2048;
	const FBox SmallBlocker(FVector(150.0, -80.0, -500.0), FVector(250.0, 80.0, 500.0));
	Result = FindAroundBox(Request, SmallBlocker);
	CheckPath(*this, Request, Result, SmallBlocker);
	if (Result.Waypoints.Num() > 1)
	{
		TestTrue(TEXT("Clear final segment skips distant grid expansion"),
			FVector::Distance(Result.Waypoints[Result.Waypoints.Num() - 2], Result.Waypoints.Last()) > Request.CellSize * 1.75f);
	}

	Request.Start = Request.Goal;
	Result = FindAroundBox(Request, FarObstacle);
	TestTrue(TEXT("Already at goal succeeds"), Result.Status == EFlightPathStatus::Success);
	TestEqual(TEXT("Already at goal has one waypoint"), Result.Waypoints.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightRetreatHalfSpaceTest,
	"Outlier.Flight.Pathfinding.RetreatHalfSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightRetreatHalfSpaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector(500, 0, 0);
	Request.Goal = FVector(1400, 0, 0);
	Request.SearchBounds = FBox(FVector(-400, -500, -300), FVector(1600, 500, 300));
	Request.bRequireAwayHalfSpace = true;
	Request.AwayDirection = FVector::ForwardVector;
	const FBox Wall(FVector(800, -150, -500), FVector(1000, 150, 500));
	const FFlightPathResult Detour = FindAroundBox(Request, Wall);
	CheckPath(*this, Request, Detour, Wall);
	TestTrue(TEXT("Retreat uses AStar to go around a wall"), Detour.SearchNodes > 0);
	for (const FVector& Point : Detour.Waypoints)
	{
		TestTrue(TEXT("Every detour point remains on drone side"), FlightPathfinding::IsInsideAwayHalfSpace(Request, Point));
	}
	const FBox FarObstacle(FVector(10000), FVector(11000));
	Request.Goal = FVector(-200, 0, 0);
	TestTrue(TEXT("Goal across player is rejected even in open space"),
		FindAroundBox(Request, FarObstacle).Status == EFlightPathStatus::BlockedEndpoint);
	Request.bRequireAwayHalfSpace = false;
	TestTrue(TEXT("Other movements retain unconstrained paths"),
		FindAroundBox(Request, FarObstacle).Status == EFlightPathStatus::Success);
	Request.bRequireAwayHalfSpace = true;
	Request.Goal = FVector(-25, 0, 0);
	Request.GoalSearchRadius = 200.0f;
	const FFlightPathResult Adjusted = FindAroundBox(Request, FarObstacle);
	TestTrue(TEXT("Optional endpoint adjustment respects the same half-space"),
		Adjusted.Status == EFlightPathStatus::Success && Adjusted.bGoalAdjusted
		&& !Adjusted.Waypoints.IsEmpty() && Adjusted.Waypoints.Last().X >= 0.0);
	Request.AwayDirection = FVector::ZeroVector;
	TestTrue(TEXT("Invalid constraint does not silently disable retreat safety"),
		FindAroundBox(Request, FarObstacle).Status == EFlightPathStatus::InvalidRequest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightPathFailureTest,
	"Outlier.Flight.Pathfinding.Failures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightAdjustedGoalAccessTest,
	"Outlier.Flight.Pathfinding.AdjustedGoalAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightAdjustedGoalAccessTest::RunTest(const FString& Parameters)
{
	// 첫 빈 보정점은 벽 뒤에 있고, 바깥 반경의 후보는 출발점에서 바로 접근 가능하다.
	// 빈 공간이라는 이유만으로 벽 뒤 목표를 고정해 장거리 A* 예산을 소진하면 안 된다.
	FFlightPathRequest Request = MakeRequest();
	Request.Start = FVector(-10000, 0, 0);
	Request.Goal = FVector::ZeroVector;
	Request.SearchBounds = FBox(FVector(-10800, -800, -800), FVector(800));
	Request.GoalSearchRadius = 150;
	Request.MaxSearchNodes = 512;
	Request.MaxCollisionChecks = 2048;
	const FBox Wall(FVector(-100, -1000, -1000), FVector(-80, 1000, 1000));
	const FBox OccupiedGoal(FVector(-10), FVector(10));
	const FFlightPathResult Result = FlightPathfinding::FindPathWithChecks(Request,
		[&](const FVector& Point)
		{
			return !Wall.IsInsideOrOn(Point) && !OccupiedGoal.IsInsideOrOn(Point);
		},
		[&](const FVector& From, const FVector& To)
		{
			return !FMath::LineBoxIntersection(Wall, From, To, To - From)
				&& !FMath::LineBoxIntersection(OccupiedGoal, From, To, To - From);
		});
	TestEqual(TEXT("Accessible adjusted candidate avoids distant blocked-goal search"), Result.Status, EFlightPathStatus::Success);
	TestTrue(TEXT("Goal adjustment is reported"), Result.bGoalAdjusted);
	TestEqual(TEXT("Visible adjusted goal avoids AStar"), Result.SearchNodes, 0);
	TestEqual(TEXT("Long route has only endpoints"), Result.Waypoints.Num(), 2);
	if (!Result.Waypoints.IsEmpty())
	{
		TestTrue(TEXT("Adjusted goal is on accessible side of wall"), Result.Waypoints.Last().X < Wall.Min.X);
		TestTrue(TEXT("Adjustment respects radius"), Result.Waypoints.Last().Size() <= Request.GoalSearchRadius);
	}
	// 현재 캡슐이 안전 여유와 겹치면 첫 짧은 구간만 허용한다. 직선 후보 조회가 우회 예산을 빼앗지 않는다.
	Request.Start = FVector::ZeroVector;
	Request.Goal = FVector(1000, 1, 0);
	Request.SearchBounds = FBox(FVector(-100, 0, 0), FVector(1100, 1, 0));
	Request.GoalSearchRadius = 100;
	Request.MaxCollisionChecks = 12;
	const FFlightPathResult ClearanceEscape = FlightPathfinding::FindPathWithChecks(Request,
		[&](const FVector& Point)
		{
			return Point != Request.Goal;
		},
		[&](const FVector& From, const FVector& To)
		{
			return From != Request.Start || FVector::Distance(From, To) <= 175;
		});
	TestEqual(TEXT("Optional goal probes reserve budget for clearance escape"), ClearanceEscape.Status, EFlightPathStatus::Success);
	TestTrue(TEXT("Clearance escape keeps first segment short"), ClearanceEscape.Waypoints.Num() > 2);
	return true;
}

bool FFlightPathFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 경로 없음, 끝점 차단, 예산 초과, 취소, 잘못된 요청을 서로 다른 결과로 유지한다.
	FFlightPathRequest Request = MakeRequest();
	const FBox SealedWall(FVector(-50.0, -1000.0, -1000.0), FVector(50.0, 1000.0, 1000.0));
	FFlightPathResult Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Sealed wall has no route within bounds"), Result.Status == EFlightPathStatus::NoPathWithinBounds);
	TestTrue(TEXT("Failure has no partial route"), Result.Waypoints.IsEmpty());
	Result = FlightPathfinding::FindPathWithChecks(Request,
		[](const FVector&)
		{
			return true;
		},
		[](const FVector&, const FVector&)
		{
			return false;
		});
	TestTrue(TEXT("Clear endpoints alone never prove reachability"), Result.Status == EFlightPathStatus::NoPathWithinBounds);

	const FBox GoalObstacle(Request.Goal - FVector(10.0), Request.Goal + FVector(10.0));
	Result = FindAroundBox(Request, GoalObstacle);
	TestTrue(TEXT("Blocked goal is explicit"), Result.Status == EFlightPathStatus::BlockedEndpoint);
	const FBox StartObstacle(Request.Start - FVector(10.0), Request.Start + FVector(10.0));
	Result = FindAroundBox(Request, StartObstacle);
	TestTrue(TEXT("Blocked start is explicit"), Result.Status == EFlightPathStatus::BlockedEndpoint);
	TestTrue(TEXT("Blocked start is distinguished from blocked goal"), Result.bStartBlocked);

	// 보정은 막힌 목적지에만 적용한다. 시작점/방 경계/경로 검사와 요청당 예산은 우회하지 않는다.
	Request.GoalSearchRadius = 100.0f;
	Result = FindAroundBox(Request, GoalObstacle);
	TestTrue(TEXT("Blocked goal can resolve to a nearby reachable point"), Result.Status == EFlightPathStatus::Success);
	TestTrue(TEXT("Successful fallback reports adjusted goal"), Result.bGoalAdjusted);
	if (!Result.Waypoints.IsEmpty())
	{
		TestTrue(TEXT("Resolved goal stays within configured radius"),
			FVector::Distance(Result.Waypoints.Last(), Request.Goal) <= Request.GoalSearchRadius);
		TestFalse(TEXT("Resolved goal is outside blocked volume"), GoalObstacle.IsInsideOrOn(Result.Waypoints.Last()));
		TestFalse(TEXT("Final segment does not cut blocked volume"), FMath::LineBoxIntersection(
			GoalObstacle, Result.Waypoints[0], Result.Waypoints.Last(), Result.Waypoints.Last() - Result.Waypoints[0]));
	}
	Result = FindAroundBox(Request, StartObstacle);
	TestTrue(TEXT("Goal fallback never moves an invalid start"),
		Result.Status == EFlightPathStatus::BlockedEndpoint && Result.bStartBlocked);
	const FBox LargeGoalObstacle(Request.Goal - FVector(150.0), Request.Goal + FVector(150.0));
	Result = FindAroundBox(Request, LargeGoalObstacle);
	TestTrue(TEXT("No candidate within radius fails without partial path"),
		Result.Status == EFlightPathStatus::BlockedEndpoint && Result.Waypoints.IsEmpty());
	Request.MaxCollisionChecks = 3;
	Result = FindAroundBox(Request, LargeGoalObstacle);
	TestTrue(TEXT("Goal candidates share collision budget with path search"),
		Result.Status == EFlightPathStatus::BudgetExceeded && Result.CollisionChecks == 3);
	Request = MakeRequest();

	Request.MaxSearchNodes = 1;
	Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Node exhaustion is not unreachable"), Result.Status == EFlightPathStatus::BudgetExceeded);
	Request = MakeRequest();
	Request.MaxCollisionChecks = 2;
	Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Collision exhaustion is not unreachable"), Result.Status == EFlightPathStatus::BudgetExceeded);
	TestEqual(TEXT("Collision checks respect limit"), Result.CollisionChecks, 2);
	Request = MakeRequest();
	Request.MaxSearchMilliseconds = UE_DOUBLE_SMALL_NUMBER;
	Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Time exhaustion is not unreachable"), Result.Status == EFlightPathStatus::BudgetExceeded);

	Request = MakeRequest();
	Request.ShouldCancel = []()
	{
		return true;
	};
	Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Cancellation is explicit"), Result.Status == EFlightPathStatus::Cancelled);
	TestEqual(TEXT("Cancelled request performs no collision checks"), Result.CollisionChecks, 0);
	Request = MakeRequest();
	Request.Goal.X = 1000.0;
	Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Goal outside search bounds is invalid"), Result.Status == EFlightPathStatus::InvalidRequest);
	Request = MakeRequest();
	Request.CellSize = 0.0f;
	Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Zero cell size is invalid"), Result.Status == EFlightPathStatus::InvalidRequest);
	Request = MakeRequest();
	Request.HeuristicScale = std::numeric_limits<double>::infinity();
	TestEqual(TEXT("Nonfinite heuristic cannot bypass validation"),
		FindAroundBox(Request, SealedWall).Status, EFlightPathStatus::InvalidRequest);
	Request = MakeRequest();
	Request.SearchBounds = FBox(FVector(-1000000.0), FVector(1000000.0));
	Request.CellSize = 1.0f;
	Result = FindAroundBox(Request, SealedWall);
	TestTrue(TEXT("Unrepresentable grid is rejected"), Result.Status == EFlightPathStatus::InvalidRequest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightPathWorldTest,
	"Outlier.Flight.Pathfinding.CollisionAndRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightPathWorldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 실제 물리 월드에서 Pawn 충돌 응답과 RoomVolume 검사가 순수 탐색에 전달되는지 확인한다.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("FlightPathTest")));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	const auto Cleanup = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AActor* Drone = World->SpawnActor<AActor>();
	AActor* Wall = World->SpawnActor<AActor>();
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
	if (!TestNotNull(TEXT("Drone exists"), Drone) || !TestNotNull(TEXT("Wall exists"), Wall)
		|| !TestNotNull(TEXT("Room exists"), Room) || !TestNotNull(TEXT("Physics scene exists"), World->GetPhysicsScene()))
	{
		Cleanup();
		return false;
	}

	UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Drone);
	Capsule->InitCapsuleSize(20.0f, 40.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	Drone->SetRootComponent(Capsule);
	Capsule->RegisterComponent();
	Drone->SetActorLocation(FVector(-300.0, 0.0, 0.0));

	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	WallBox->SetBoxExtent(FVector(30.0, 150.0, 500.0));
	WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallBox->SetCollisionObjectType(ECC_WorldStatic);
	WallBox->SetCollisionResponseToAllChannels(ECR_Block);
	// Visibility를 무시하는 벽도 실제 Pawn 이동을 막으면 경로 탐색에서 존중해야 한다.
	WallBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Wall->SetRootComponent(WallBox);
	WallBox->RegisterComponent();

	UBoxComponent* RoomBox = CastChecked<UBoxComponent>(Room->GetRootComponent());
	RoomBox->SetBoxExtent(FVector(400.0, 350.0, 200.0));
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();

	FFlightPathRequest Request = MakeRequest();
	Request.Room = Room;
	Request.bRequireRoom = true;
	FFlightPathResult Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Actual capsule can detour inside room"), Result.Status == EFlightPathStatus::Success);
	TestTrue(TEXT("Pawn-blocking wall requires a detour"), Result.Waypoints.Num() > 2);
	TestTrue(TEXT("Straight segment rejects wall without running a detour search"),
		FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::BlockedSegment);
	Capsule->IgnoreActorWhenMoving(Wall, true);
	TestTrue(TEXT("Path validation respects movement ignored actors"),
		FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::Success);
	Capsule->IgnoreActorWhenMoving(Wall, false);
	Capsule->IgnoreComponentWhenMoving(WallBox, true);
	TestTrue(TEXT("Path validation respects movement ignored components"),
		FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::Success);
	Capsule->IgnoreComponentWhenMoving(WallBox, false);
	if (Result.Waypoints.Num() > 1)
	{
		const FVector Goal = Request.Goal;
		Request.Goal = Result.Waypoints[1];
		TestTrue(TEXT("First detour segment is traversable"),
			FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::Success);
		Request.Goal = Goal;
	}

	// 플레이어처럼 목적지 중심을 점유하는 Pawn도 무시하지 않고 주변 접근점으로 보정한다.
	AActor* Target = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("Destination occupant exists"), Target))
	{
		UCapsuleComponent* TargetCapsule = NewObject<UCapsuleComponent>(Target);
		TargetCapsule->InitCapsuleSize(45.0f, 96.0f);
		TargetCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TargetCapsule->SetCollisionObjectType(ECC_Pawn);
		TargetCapsule->SetCollisionResponseToAllChannels(ECR_Block);
		Target->SetRootComponent(TargetCapsule);
		TargetCapsule->RegisterComponent();
		Target->SetActorLocation(Request.Goal);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
		TestTrue(TEXT("Exact player center is blocked"), Result.Status == EFlightPathStatus::BlockedEndpoint);
		Request.GoalSearchRadius = 150.0f;
		Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
		TestTrue(TEXT("Occupied destination resolves to a reachable nearby point"),
			Result.Status == EFlightPathStatus::Success && Result.bGoalAdjusted);
		if (!Result.Waypoints.IsEmpty())
		{
			TestTrue(TEXT("Resolved point stays outside target capsule"),
				FVector::Distance(Result.Waypoints.Last(), Request.Goal) >= 70.0);
		}
		Request.GoalSearchRadius = 0.0f;
		Target->Destroy();
	}

	// 방을 좁히면 Enemy는 실패하지만, 방 제한 없는 Partner 요청은 같은 벽을 우회할 수 있다.
	RoomBox->SetBoxExtent(FVector(400.0, 100.0, 200.0));
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Enemy cannot detour outside room"), Result.Status == EFlightPathStatus::NoPathWithinBounds);
	Request.bRequireRoom = false;
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Partner can use the same route without room restriction"), Result.Status == EFlightPathStatus::Success);
	TestTrue(TEXT("Destination filtering does not reject a point merely behind a wall"),
		FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::Success);
	const FVector UnblockedGoal = Request.Goal;
	Request.Goal = FVector::ZeroVector;
	TestTrue(TEXT("Destination filtering rejects actual movement-channel overlap"),
		FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::BlockedEndpoint);
	Request.Goal = UnblockedGoal;

	// 방 참조 누락은 제한 해제가 아니며, 경계 근처 목적지는 캡슐 전체가 들어가야 한다.
	Request.bRequireRoom = true;
	Request.Room.Reset();
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Missing required room does not remove restriction"), Result.Status == EFlightPathStatus::Unavailable);
	TestTrue(TEXT("Segment check also requires a valid room"),
		FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::Unavailable);
	TestTrue(TEXT("Destination check requires the registered room too"),
		FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::Unavailable);
	Request.Room = Room;
	RoomBox->SetBoxExtent(FVector(400.0, 350.0, 200.0));
	Request.Goal = FVector(390.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Capsule clearance must fit, not just its center"), Result.Status == EFlightPathStatus::BlockedEndpoint);
	TestTrue(TEXT("Destination filtering also requires full capsule clearance inside room"),
		FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::BlockedEndpoint);
	TestTrue(TEXT("Segment check cannot cut across room clearance"),
		FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::BlockedEndpoint);
	Request.GoalSearchRadius = 100.0f;
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Goal near boundary can resolve inside room"), Result.Status == EFlightPathStatus::Success);
	if (!Result.Waypoints.IsEmpty())
	{
		TestTrue(TEXT("Resolved boundary goal respects actual capsule clearance"),
			Result.Waypoints.Last().X <= 375.0);
	}
	Request.GoalSearchRadius = 0.0f;

	// 실제 캡슐은 벽에서 2cm 떨어져 있지만 5cm 안전 여유는 겹친다. 첫 탈출 구간만 실제 크기를 쓴다.
	Drone->SetActorLocation(FVector(-52.0, 0.0, 0.0));
	Request.Start = Drone->GetActorLocation();
	Request.Goal = FVector(-300.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Clearance-only overlap permits a validated escape"), Result.Status == EFlightPathStatus::Success);
	TestTrue(TEXT("Clearance escape is reported"), Result.bUsedClearanceEscape);
	const FVector EscapeGoal = Request.Goal;
	Request.Goal = Request.Start;
	TestTrue(TEXT("A candidate cannot use the start clearance escape exception"),
		FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::BlockedEndpoint);
	Request.Goal = EscapeGoal;
	TestTrue(TEXT("Long direct route cannot bypass clearance"), Result.Waypoints.Num() > 2);
	TestTrue(TEXT("Segment validator rejects a long clearance escape"),
		FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::BlockedSegment);
	if (Result.Waypoints.Num() > 1)
	{
		Request.Goal = Result.Waypoints[1];
		TestTrue(TEXT("Following the first short escape segment is allowed"),
			FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::Success);
	}
	Drone->SetActorLocation(FVector(-49.0, 0.0, 0.0));
	Request.Start = Drone->GetActorLocation();
	Request.Goal = FVector(-300.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Actual penetration still rejects start"),
		Result.Status == EFlightPathStatus::BlockedEndpoint && Result.bStartBlocked);
	Drone->SetActorLocation(FVector(-300.0, 0.0, 0.0));
	Request = MakeRequest();
	Request.Room = Room;
	Request.bRequireRoom = true;

	FVector ClampedGoal;
	TestTrue(TEXT("Room can clamp an outside destination with capsule extent"),
		Room->TryClampFlightLocation(FVector(900.0, 0.0, 0.0), FVector(25.0, 25.0, 45.0), ClampedGoal));
	TestTrue(TEXT("Clamped capsule stays fully inside room"), ClampedGoal.X < 375.0);

	// 회전/스케일된 방도 기존 ContainsWorldLocation의 로컬 좌표 판정을 재사용한다.
	Room->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	Room->SetActorScale3D(FVector(2.0, 1.0, 1.0));
	TestTrue(TEXT("Rotated scaled room clamps in its local axes"),
		Room->TryClampFlightLocation(FVector(1000.0, 1000.0, 0.0), FVector(25.0, 25.0, 45.0), ClampedGoal));
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Offset((Corner & 1) ? 25.0 : -25.0,
			(Corner & 2) ? 25.0 : -25.0, (Corner & 4) ? 45.0 : -45.0);
		TestTrue(TEXT("Every clamped clearance corner is inside rotated room"), Room->ContainsWorldLocation(ClampedGoal + Offset));
	}
	Request.Goal = FVector(340.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Rotated room rejects capsule crossing its local boundary"),
		Result.Status == EFlightPathStatus::BlockedEndpoint);
	Request.Goal = FVector(310.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Scaled rotated room permits an internal detour"), Result.Status == EFlightPathStatus::Success);

	// 목적지 보정 후 방향 전환만으로 구간이 OutsideRoom이 되는 회귀를 재현한다.
	WallBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Room->SetActorRotation(FRotator::ZeroRotator);
	Room->SetActorScale3D(FVector::OneVector);
	Capsule->SetCapsuleSize(60.0f, 60.0f);
	Drone->SetActorRotation(FRotator::ZeroRotator);
	Request = MakeRequest();
	Request.Room = Room;
	Request.bRequireRoom = true;
	Request.Start = Capsule->GetComponentLocation();
	TestTrue(TEXT("Sphere destination clamps near room boundary"),
		Room->TryClampFlightLocation(FVector(900.0, 0.0, 0.0),
			FlightPathfinding::GetCapsuleWorldExtent(65.0f, 65.0f, Capsule->GetComponentQuat()), Request.Goal));
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Clamped sphere path succeeds before rotation"), Result.Status == EFlightPathStatus::Success);
	Drone->SetActorRotation(FRotator(0.0, 45.0, 0.0));
	TestTrue(TEXT("Clamped sphere destination stays valid after yaw"),
		FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::Success);
	TestTrue(TEXT("Clamped sphere segment stays valid after yaw"),
		FlightPathfinding::CheckSegment(*World, *Capsule, Request) == EFlightPathStatus::Success);
	Request.Goal.X = 400.0;
	TestTrue(TEXT("Actual capsule crossing room boundary is still rejected"),
		FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::BlockedEndpoint);

	Cleanup();
	return true;
}

#endif
