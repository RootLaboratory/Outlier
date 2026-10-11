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

	Request.Start = Request.Goal;
	Result = FindAroundBox(Request, FarObstacle);
	TestTrue(TEXT("Already at goal succeeds"), Result.Status == EFlightPathStatus::Success);
	TestEqual(TEXT("Already at goal has one waypoint"), Result.Waypoints.Num(), 1);
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

	// 방을 좁히면 Enemy는 실패하지만, 방 제한 없는 Partner 요청은 같은 벽을 우회할 수 있다.
	RoomBox->SetBoxExtent(FVector(400.0, 100.0, 200.0));
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Enemy cannot detour outside room"), Result.Status == EFlightPathStatus::NoPathWithinBounds);
	Request.bRequireRoom = false;
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Partner can use the same route without room restriction"), Result.Status == EFlightPathStatus::Success);

	// 방 참조 누락은 제한 해제가 아니며, 경계 근처 목적지는 캡슐 전체가 들어가야 한다.
	Request.bRequireRoom = true;
	Request.Room.Reset();
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Missing required room does not remove restriction"), Result.Status == EFlightPathStatus::Unavailable);
	Request.Room = Room;
	RoomBox->SetBoxExtent(FVector(400.0, 350.0, 200.0));
	Request.Goal = FVector(390.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Capsule clearance must fit, not just its center"), Result.Status == EFlightPathStatus::BlockedEndpoint);

	// 회전/스케일된 방도 기존 ContainsWorldLocation의 로컬 좌표 판정을 재사용한다.
	Room->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	Room->SetActorScale3D(FVector(2.0, 1.0, 1.0));
	Request.Goal = FVector(340.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Rotated room rejects capsule crossing its local boundary"),
		Result.Status == EFlightPathStatus::BlockedEndpoint);
	Request.Goal = FVector(310.0, 0.0, 0.0);
	Result = FlightPathfinding::FindPath(*World, *Capsule, Request);
	TestTrue(TEXT("Scaled rotated room permits an internal detour"), Result.Status == EFlightPathStatus::Success);

	Cleanup();
	return true;
}

#endif
