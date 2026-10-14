#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/FlightPathfinding.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomVolume.h"

namespace
{
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightCapsuleExtentTest,
	"Outlier.Flight.Pathfinding.CapsuleExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightNarrowGridPhaseTest,
	"Outlier.Flight.Pathfinding.NarrowGridPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
