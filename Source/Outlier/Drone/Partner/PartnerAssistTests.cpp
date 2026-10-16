#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/Partner/PartnerMovementComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "AIController.h"
#include "Shooter/ShooterCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerCameraAssistInputTest,
	"Outlier.Partner.AutoFollow.InputRearm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerCameraAssistInputTest::RunTest(const FString& Parameters)
{
	FPartnerFollowInputState Client;
	FPartnerFollowInputState Server;
	TestTrue(TEXT("Initial press starts an input session"), Client.Press());
	const uint32 FirstSerial = Client.Serial;
	TestTrue(TEXT("Server accepts initial session"), Server.AcceptPress(FirstSerial));
	TestFalse(TEXT("Held Triggered cannot start a second session"), Client.Press());
	TestFalse(TEXT("Duplicate server press is ignored"), Server.AcceptPress(FirstSerial));

	// 경로 실패/상태 중단은 버튼 해제가 아니다. 늦은 성공 경로도 기능을 재개하면 안 된다.
	Client.Stop();
	Server.Stop();
	TestFalse(TEXT("Failure keeps held input latched"), Client.Press());
	TestFalse(TEXT("Server also requires release after failure"), Server.AcceptPress(FirstSerial + 1));
	TestFalse(TEXT("Late success after failure is discarded"), Client.AcceptsPath(FirstSerial));
	TestFalse(TEXT("Old release cannot clear current input"), Server.Release(FirstSerial + 1));
	TestTrue(TEXT("Client release rearms"), Client.Release(FirstSerial));
	TestTrue(TEXT("Server release rearms"), Server.Release(FirstSerial));
	TestTrue(TEXT("A new press can start again"), Client.Press());
	TestTrue(TEXT("Server accepts the new session"), Server.AcceptPress(Client.Serial));
	TestFalse(TEXT("Old path cannot overwrite new session"), Client.AcceptsPath(FirstSerial));
	TestFalse(TEXT("Old release cannot stop the new session"), Server.Release(FirstSerial));
	TestTrue(TEXT("Current path is accepted"), Client.AcceptsPath(Client.Serial));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerFollowRetryTest,
	"Outlier.Partner.AutoFollow.RetryLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerFollowRetryTest::RunTest(const FString& Parameters)
{
	FPartnerFollowRetryState Retry;
	TestFalse(TEXT("A fresh session has no failure"), Retry.IsExhausted(10.0, 3, 2.0));
	Retry.Fail(10.0, 0.25);
	TestFalse(TEXT("One blocked segment or budget does not disable assist"), Retry.IsExhausted(10.0, 3, 2.0));
	TestEqual(TEXT("Retry has a minimum delay"), Retry.NextAttemptAt, 10.25);
	Retry.Fail(10.25, 0.25);
	TestFalse(TEXT("Two failures remain retryable"), Retry.IsExhausted(10.25, 3, 2.0));
	Retry.Fail(10.5, 0.25);
	TestTrue(TEXT("Third failed attempt exhausts the configured limit"), Retry.IsExhausted(10.5, 3, 2.0));
	Retry.Reset();
	Retry.Fail(20.0, 0.25);
	TestTrue(TEXT("Time limit also ends the failure episode"), Retry.IsExhausted(22.0, 10, 2.0));
	Retry.Reset();
	TestFalse(TEXT("Real movement or reactivation clears the failure episode"), Retry.IsExhausted(30.0, 3, 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerAssistNavigationTest,
	"Outlier.Partner.AutoFollow.Navigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerAssistNavigationTest::RunTest(const FString& Parameters)
{
	// PIE 없이 실제 Partner 캡슐과 Component의 선정/실패/추종 흐름을 검사한다.
	// Shooter의 C++ 클래스는 추상이므로 기존 테스트처럼 실제 BP 클래스를 사용한다.
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete Shooter class exists"), ShooterClass))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PartnerAssistTest")));
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	AActor* Wall = World->SpawnActor<AActor>();
	UPartnerMovementComponent* Move = Partner ? Partner->FindComponentByClass<UPartnerMovementComponent>() : nullptr;
	if (!TestNotNull(TEXT("Partner movement exists"), Move) || !TestNotNull(TEXT("Wall exists"), Wall)
		|| !TestNotNull(TEXT("Physics scene exists"), World->GetPhysicsScene()))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	Move->RefreshCharacterRefsFromPlayerState();
	Move->FollowMaxSearchNodes = 4096;
	Move->FollowMaxCollisionChecks = 20000;
	Move->FollowMaxSearchMilliseconds = 10000.0f;
	Move->FollowPathMargin = 400.0f;
	Move->FollowGoalSearchRadius = 0.0f;
	// 기존 이동 회귀는 요청 완료까지 명시적으로 진행한다. 프레임 분산 자체는 별도 테스트에서 검증한다.
	const auto FinishSearch = [&](double Now)
	{
		if (!Move->FollowSearch) return;
		int32 Slices = 0;
		while (!Move->FollowSearch->Advance(256, 10000.0) && ++Slices < 10000) {}
		TestTrue(TEXT("Follow test search completes within bounded slices"), Slices < 10000);
		if (Slices < 10000) Move->CompleteFollowPath(Now);
	};
	const auto FindPath = [&](double Now, const FVector& Target)
	{
		Move->FindFollowPath(Now, Target);
		FinishSearch(Now);
	};
	const auto UpdatePath = [&](double Now, const FVector& Target)
	{
		Move->UpdateFollowPath(Now, Target);
		FinishSearch(Now);
	};
	// 목표를 Shooter 캡슐 중심에 고정하지 않는다. SyncMove의 기존 상대 위치는 별도로 유지한다.
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(
		ShooterClass, FVector(0, 2000, 0), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Shooter exists for safe follow target"), Shooter))
	{
		Move->ShooterCharacter = Shooter;
		Move->FollowMode = EPartnerMoveMode::CameraAssist;
		Partner->AssistTargetLocalOffset = FVector(0, 0, 50);
		const float Required = Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius()
			+ Partner->GetCapsuleComponent()->GetScaledCapsuleRadius() + Move->FollowPathClearance
			+ Move->GetFollowAcceptanceRadius();
		TestTrue(TEXT("CameraAssist goal leaves both capsule radii and arrival margin"),
			FVector::DistSquared2D(Move->GetAutoFollowTarget(), Shooter->GetActorLocation()) >= FMath::Square(Required) - 0.1);
		Move->FollowMode = EPartnerMoveMode::SyncMove;
		Partner->SyncLocalOffset = FVector(500, 30, 50);
		const FVector CapsuleOffset = Partner->GetCapsuleComponent()->GetComponentLocation() - Partner->GetActorLocation();
		TestEqual(TEXT("SyncMove keeps configured relative target"), Move->GetAutoFollowTarget(),
			Shooter->GetActorLocation() + Partner->SyncLocalOffset + CapsuleOffset);
		Move->FollowMode = EPartnerMoveMode::CameraAssist;
		Shooter->SetActorEnableCollision(false);
	}
	UCapsuleComponent* Capsule = Partner->GetCapsuleComponent();
	Capsule->SetCapsuleSize(20.0f, 40.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	Partner->SetActorLocation(FVector(-300.0, 0.0, 0.0));
	const FVector Goal(300.0, 0.0, 0.0);

	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(30.0, 150.0, 500.0));
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->RegisterComponent();
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();

	Move->ApplyCameraAssist();
	const float ExistingSpeed = Move->GetFollowDesiredSpeed();
	Move->CameraAssistMaxSpeed = 600.0f;
	TestEqual(TEXT("CameraAssist can use a dedicated speed above manual speed"), Move->GetFollowDesiredSpeed(), 600.0f);
	Move->CameraAssistMaxSpeed = 170.0f;
	TestEqual(TEXT("CameraAssist accepts a lower dedicated maximum"), Move->GetFollowDesiredSpeed(), 170.0f);
	Move->FollowMode = EPartnerMoveMode::SyncMove;
	TestEqual(TEXT("CameraAssist maximum never changes SyncMove speed"), Move->GetFollowDesiredSpeed(), ExistingSpeed);
	Move->FollowMode = EPartnerMoveMode::CameraAssist;
	Move->CameraAssistMaxSpeed = 0.0f;
	TestEqual(TEXT("Zero preserves existing automatic speed settings"), Move->GetFollowDesiredSpeed(), ExistingSpeed);
	// 기본 탐색 예산에서 장거리 직선 경로가 생기는지 먼저 검사한다. 기존 우회 테스트의 큰 예산과 구분한다.
	Move->FollowMaxSearchNodes = 512;
	Move->FollowMaxCollisionChecks = 2048;
	FindPath(0.0, FVector(5000, 0, 0));
	TestEqual(TEXT("Long distance is not a follow distance limit"), Move->FollowPath.Num(), 2);
	Move->CameraAssistMaxSpeed = 170.0f;
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("CameraAssist applies its dedicated maximum to actual movement"),
		Partner->GetCharacterMovement()->MaxFlySpeed <= 170.0f);
	Partner->ConsumeMovementInputVector();
	Move->CameraAssistMaxSpeed = 0.0f;
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("Long distance produces input toward Shooter side"), Partner->ConsumeMovementInputVector().X > 0.0f);
	// 직선 중간 경유점은 최종 도착 거리 감속을 적용하지 않는다.
	Move->FollowPath = { Capsule->GetComponentLocation(), Capsule->GetComponentLocation() + FVector(150, 0, 0),
		Capsule->GetComponentLocation() + FVector(3000, 0, 0) };
	Move->FollowWaypoint = 1;
	Partner->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("Straight intermediate waypoint retains full movement input"), Partner->ConsumeMovementInputVector().X > 0.99f);
	// 방향만 꺾인다고 입력을 65%로 낮추지 않는다. 안전 속도 제한은 실제 가속도/남은 거리가 담당한다.
	Move->FollowPath = { Capsule->GetComponentLocation(), Capsule->GetComponentLocation() + FVector(80, 0, 0),
		Capsule->GetComponentLocation() + FVector(80, 1000, 0) };
	Partner->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("Open corner retains full forward input instead of artificial 65 percent slowdown"),
		Partner->ConsumeMovementInputVector().X > 0.99f);
	Move->FollowPath = { Capsule->GetComponentLocation(), Capsule->GetComponentLocation() + FVector(150, 0, 0),
		Capsule->GetComponentLocation() + FVector(3000, 0, 0) };
	Move->FollowPath[1] = Capsule->GetComponentLocation() + FVector(50, 0, 0);
	Partner->GetCharacterMovement()->Velocity = FVector(500, 0, 0);
	Move->FollowStoredPath(0.2f);
	TestTrue(TEXT("Low FPS cannot overshoot the intermediate waypoint even without a curve"),
		Partner->GetCharacterMovement()->MaxFlySpeed <= 250.01f
		&& Partner->GetCharacterMovement()->Velocity.Size() <= 250.01f);
	Partner->ConsumeMovementInputVector();
	// 모서리에 도달해도 속도를 0으로 만들거나 한 프레임 입력을 비우지 않는다.
	const FVector CornerStart = Capsule->GetComponentLocation();
	Move->FollowPath = { CornerStart, CornerStart + FVector(10, 0, 0), CornerStart + FVector(10, 300, 0) };
	Move->FollowWaypoint = 1;
	Partner->GetCharacterMovement()->Velocity = FVector(200, 0, 0);
	Move->FollowStoredPath(0.016f);
	TestEqual(TEXT("Corner advances without an idle frame"), Move->FollowWaypoint, 2);
	TestTrue(TEXT("Corner retains nonzero movement velocity"), Partner->GetCharacterMovement()->Velocity.Size() > 100.0f);
	TestTrue(TEXT("Corner immediately supplies next segment input"), Partner->ConsumeMovementInputVector().Y > 0.0f);
	// 좁은 도착 반경을 비껴 지나쳐도 안전한 다음 구간으로 이어지고, 최종 목적지는 통과로 끝내지 않는다.
	Partner->SetActorLocation(CornerStart + FVector(30, 40, 0));
	Move->FollowWaypoint = 1;
	Move->FollowStoredPath(0.016f);
	TestEqual(TEXT("Passing an intermediate waypoint does not turn back"), Move->FollowWaypoint, 2);
	Partner->ConsumeMovementInputVector();
	Partner->SetActorLocation(CornerStart + FVector(250, 40, 0));
	Move->FollowWaypoint = 1;
	TestFalse(TEXT("Far outside the corner corridor cannot skip a waypoint"),
		Move->HasReachedFollowWaypoint(Capsule->GetComponentLocation()));
	Move->FollowWaypoint = 2;
	Partner->SetActorLocation(Move->FollowPath.Last() + FVector(0, Move->GetFollowAcceptanceRadius() + 30, 0));
	TestFalse(TEXT("Final arrival still requires its configured radius"),
		Move->HasReachedFollowWaypoint(Capsule->GetComponentLocation()));
	Partner->SetActorLocation(CornerStart);
	Move->FollowPath = { CornerStart, CornerStart + FVector(1000, 0, 0), CornerStart + FVector(2000, 0, 0) };
	Move->FollowWaypoint = 1;
	Partner->GetCharacterMovement()->Velocity = FVector(0, 400, 0);
	Move->FollowStoredPath(0.016f);
	const FVector CorrectedInput = Partner->ConsumeMovementInputVector();
	TestTrue(TEXT("Steering counteracts sideways inertia instead of orbiting"), CorrectedInput.X > 0 && CorrectedInput.Y < 0);
	Move->FollowProgressWaypoint = 1;
	Move->FollowProgressLocation = CornerStart;
	Move->FollowLastProgressAt = 10.0;
	Move->UpdateFollowProgress(11.0, CornerStart + FVector(0, 100, 0));
	Move->UpdateFollowProgress(12.0, CornerStart);
	TestEqual(TEXT("Sideways motion and returning do not refresh progress"), Move->FollowLastProgressAt, 10.0);
	Move->UpdateFollowProgress(13.0, CornerStart + FVector(30, 0, 0));
	TestEqual(TEXT("Approaching the waypoint refreshes progress"), Move->FollowLastProgressAt, 13.0);
	TestEqual(TEXT("Long follow retains fine grid in the same search"),
		Move->MakeFollowPathRequest(FVector(10000, 0, 0)).CoarseCellSize, 0.0f);
	TestTrue(TEXT("Long follow selects coarse-first refinement without raising budgets"),
		Move->MakeFollowPathRequest(FVector(10000, 0, 0)).bHierarchicalResolution);
	Move->StopFollowMovement();
	FindPath(0.0, FVector(5000, 0, 0));
	// 열린 직선만으로 장거리 우회를 검증하지 않는다. 실제 캡슐로 120m 앞 목표까지 벽을 돌아간다.
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();
	Partner->SetActorLocation(FVector(-90, 40, 0));
	Move->FollowPath = { FVector(-300, 0, 0), FVector(-100, 0, 0), FVector(300, 0, 0) };
	Move->FollowWaypoint = 1;
	Move->FollowStoredPath(0.016f);
	TestEqual(TEXT("Blocked early turn retains the reachable corner before replanning"), Move->FollowWaypoint, 1);
	TestFalse(TEXT("Early turn rejection does not reset the current path"), Move->FollowPath.IsEmpty());
	Partner->SetActorLocation(FVector(-100, 0, 0));
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("A genuinely blocked turn at the corner still discards the path"), Move->FollowPath.IsEmpty());
	Partner->SetActorLocation(FVector(-90, 40, 0));
	// 서버에서 받은 경로를 로컬 충돌 차이로 붙잡으면 서버/소유자 경유점이 갈라진다.
	// 같은 위치와 경로에 클라이언트만 다른 장애물을 보는 상황을 실제 역할 변경으로 재현한다.
	const ENetRole PreviousRole = Partner->GetLocalRole();
	Partner->SetRole(ROLE_AutonomousProxy);
	Move->FollowPath = { FVector(-300, 0, 0), FVector(-100, 0, 0), FVector(300, 0, 0) };
	Move->FollowWaypoint = 1;
	Move->FollowStoredPath(0.016f);
	TestEqual(TEXT("Owner follows the server route despite different local next-segment collision"), Move->FollowWaypoint, 2);
	TestFalse(TEXT("Owner collision mismatch does not discard a server route"), Move->FollowPath.IsEmpty());
	Partner->ConsumeMovementInputVector();
	Partner->SetRole(PreviousRole);
	Move->ClearFollowPath();
	Move->FollowRetry.Reset();
	Move->FollowLastFailure = EFlightPathStatus::Success;
	Partner->SetActorLocation(FVector(-300, 0, 0));
	const TSharedPtr<FFlightPathSearch> LongSearch = FlightPathfinding::BeginSearch(*World, *Capsule,
		Move->MakeFollowPathRequest(FVector(12000, 0, 0)));
	int32 LongSlices = 0;
	while (LongSearch && !LongSearch->Advance(256, 10000.0) && ++LongSlices < 10000) {}
	TestTrue(TEXT("Long incremental detour completes within bounded slices"), LongSearch && LongSlices < 10000);
	const FFlightPathResult LongDetour = LongSearch ? LongSearch->GetResult() : FFlightPathResult();
	TestEqual(TEXT("Long detour succeeds without increasing collision or node limits"), LongDetour.Status, EFlightPathStatus::Success);
	TestTrue(TEXT("Long detour includes a validated turn"), LongDetour.Waypoints.Num() > 2);
	TestTrue(TEXT("Long detour respects collision budget"), LongDetour.CollisionChecks <= 2048);
	TestTrue(TEXT("Long detour respects node budget"), LongDetour.SearchNodes <= 512);
	TestTrue(TEXT("Partner enables automatic local resolution"), Move->MakeFollowPathRequest(FVector(5000, 0, 0)).bAdaptiveResolution);
	TestTrue(TEXT("Long detour enables retained-graph fine refinement when needed"),
		Move->MakeFollowPathRequest(FVector(12000, 0, 0)).bHierarchicalResolution);
	for (int32 Index = 1; Index < LongDetour.Waypoints.Num(); ++Index)
	{
		FFlightPathRequest Segment = Move->MakeFollowPathRequest(LongDetour.Waypoints[Index]);
		Segment.Start = LongDetour.Waypoints[Index - 1];
		Segment.GoalSearchRadius = 0;
		TestEqual(TEXT("Long detour never shortcuts through a wall"),
			FlightPathfinding::CheckSegment(*World, *Capsule, Segment), EFlightPathStatus::Success);
	}
	AddInfo(FString::Printf(TEXT("LongDetour Checks=%d Nodes=%d Ms=%.3f CacheHits=%d Pruned=%d Wide=%d Fine=%d Passes=%d"),
		LongDetour.CollisionChecks, LongDetour.SearchNodes, LongDetour.ElapsedMilliseconds,
		LongDetour.SegmentCacheHits, LongDetour.PrunedTraversals,
		LongDetour.WideExpansionNodes, LongDetour.FineExpansionNodes, LongDetour.SearchPasses));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	World->GetPhysicsScene()->Flush();
	// 실제 캡슐은 통과하지만 5cm 여유만 벽에 닿는 구간을 분리해 검증한다.
	Partner->SetActorLocation(FVector(-300, 172, 0));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	World->GetPhysicsScene()->Flush();
	FFlightPathRequest TightSegment = Move->MakeFollowPathRequest(FVector(300, 172, 0));
	TightSegment.GoalSearchRadius = 0.0f;
	TestEqual(TEXT("Planning clearance rejects the tight wall edge"),
		FlightPathfinding::CheckSegment(*World, *Capsule, TightSegment), EFlightPathStatus::BlockedSegment);
	TestEqual(TEXT("Execution keeps a segment clear for the actual capsule"),
		Move->CheckFollowSegment(TightSegment.Goal), EFlightPathStatus::Success);
	TestEqual(TEXT("Execution never ignores real capsule collision"),
		Move->CheckFollowSegment(FVector(300, 0, 0)), EFlightPathStatus::BlockedSegment);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	World->GetPhysicsScene()->Flush();
	// 이동 중 끝점에서 준비한 다음 구간을 연결해도 속도/현재 경유점을 초기화하지 않는다.
	Move->ClearFollowPath();
	Partner->SetActorLocation(FVector(-300, 0, 0));
	Move->FollowPath = { FVector(-300, 0, 0), FVector(200, 0, 0) };
	Move->FollowWaypoint = 1;
	Move->bFollowPartialRoute = true;
	Move->bFollowSearchContinuation = true;
	Move->FollowSpeedState.SetPath(Move->FollowPath);
	TestTrue(TEXT("Partial route prepares the next chunk before arrival"),
		Move->ShouldPrefetchFollowPath(Partner->GetActorLocation()));
	Move->FollowPath.Last() = Partner->GetActorLocation() + FVector(1200, 0, 0);
	Move->FollowSpeedState.SetPath(Move->FollowPath);
	TestTrue(TEXT("Next chunk preparation starts before the old fixed 1000cm threshold"),
		Move->ShouldPrefetchFollowPath(Partner->GetActorLocation()));
	Move->FollowPath.Last() = FVector(200, 0, 0);
	Move->FollowSpeedState.SetPath(Move->FollowPath);
	const float OriginalHorizon = Move->GetFollowTravelHorizon();
	Move->FollowLastFailure = EFlightPathStatus::BudgetExceeded;
	Move->FollowProgressWaypoint = 1;
	Move->FollowProgressLocation = Partner->GetActorLocation();
	Move->UpdateFollowProgress(1.0, Partner->GetActorLocation() + FVector(30, 0, 0));
	TestEqual(TEXT("Moving along the old route retains the next-route budget failure"),
		Move->FollowLastFailure, EFlightPathStatus::BudgetExceeded);
	TestTrue(TEXT("Budget retry shortens the local search without increasing limits"),
		Move->GetFollowTravelHorizon() < OriginalHorizon);
	Move->FollowLastFailure = EFlightPathStatus::Success;
	FFlightPathRequest Continuation = Move->MakeFollowPathRequest(FVector(500, 0, 0));
	Continuation.Start = Move->FollowPath.Last();
	Move->FollowSearch = FlightPathfinding::BeginSearchWithChecks(Continuation,
		[](const FVector&) { return true; }, [](const FVector&, const FVector&) { return true; });
	Partner->GetCharacterMovement()->Velocity = FVector(250, 0, 0);
	FinishSearch(0.1);
	TestEqual(TEXT("Prepared chunk appends to the active route"), Move->FollowPath.Num(), 3);
	TestEqual(TEXT("Appending does not rewind the active waypoint"), Move->FollowWaypoint, 1);
	TestEqual(TEXT("Appending preserves current velocity"), Partner->GetCharacterMovement()->Velocity, FVector(250, 0, 0));
	Move->bFollowPartialRoute = true;
	Partner->SetActorLocation(Move->FollowPath.Last());
	Move->FollowWaypoint = Move->FollowPath.Num() - 1;
	Move->FollowStoredPath(0.016f);
	TestFalse(TEXT("Runtime waypoint arrival does not hold as final arrival"), Move->bFollowHolding);
	Move->ClearFollowPath();
	Move->FollowRouteHistory.Reset();
	Move->FollowRouteSegments = 0;
	Partner->SetActorLocation(FVector(-300, 0, 0));
	FindPath(0.0, FVector(5000, 0, 0));
	AAIController* FollowController = World->SpawnActor<AAIController>();
	if (TestNotNull(TEXT("Follow simulation controller"), FollowController))
	{
		FollowController->Possess(Partner);
		UCharacterMovementComponent* Movement = Partner->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Flying);
		Movement->MaxFlySpeed = 400;
		Movement->MaxAcceleration = 2048;
		const FVector Start = Partner->GetActorLocation();
		for (int32 Step = 0; Step < 10; ++Step)
		{
			Move->FollowStoredPath(0.05f);
			Movement->TickComponent(0.05f, LEVELTICK_All, nullptr);
		}
		TestTrue(TEXT("CharacterMovement actually advances on a long route"), Partner->GetActorLocation().X > Start.X + 25);
		// 경유점 판정만 검사하지 않고 실제 관성 이동으로 두 번 꺾이는 경로를 끝까지 따라간다.
		const float PreviousAcceleration = Movement->MaxAcceleration;
		for (const EPartnerMoveMode Mode : { EPartnerMoveMode::CameraAssist, EPartnerMoveMode::SyncMove })
		{
			for (const float StepTime : { 0.016f, 0.05f })
			{
				Partner->SetActorLocation(Start);
				Move->FollowMode = Mode;
				Move->FollowPath = { Start, Start + FVector(400, 0, 0),
					Start + FVector(400, 400, 0), Start + FVector(800, 400, 0) };
				Move->FollowWaypoint = 1;
				Move->FollowSpeedState.SetPath(Move->FollowPath);
				Movement->Velocity = FVector(0, 300, 0);
				Movement->MaxAcceleration = 600;
				Partner->ConsumeMovementInputVector();
				for (int32 Step = 0; Step < FMath::CeilToInt(10.0f / StepTime) && !Move->FollowPath.IsEmpty(); ++Step)
				{
					Move->FollowStoredPath(StepTime);
					Movement->TickComponent(StepTime, LEVELTICK_All, nullptr);
				}
				TestTrue(FString::Printf(TEXT("Inertial corner route arrives Mode=%d Dt=%.3f"), static_cast<int32>(Mode), StepTime),
					Move->FollowPath.IsEmpty() && Move->bFollowHolding);
			}
		}
		Movement->MaxAcceleration = PreviousAcceleration;
		Move->FollowMode = EPartnerMoveMode::CameraAssist;
		FollowController->UnPossess();
		Move->StopFollowMovement();
		Partner->SetActorLocation(FVector(-300, 0, 0));
		Move->ApplyCameraAssist();
	}
	Move->FollowMaxSearchNodes = 4096;
	Move->FollowMaxCollisionChecks = 20000;
	const FFlightPathRequest Request = Move->MakeFollowPathRequest(Goal);
	TestFalse(TEXT("Partner request never restricts RoomTag"), Request.bRequireRoom);
	TestEqual(TEXT("Partner uses bounded goal-biased search"), Request.HeuristicScale, 1.25);
	FindPath(0.0, Goal);
	TestEqual(TEXT("Open space uses a direct path"), Move->FollowPath.Num(), 2);
	// 새 요청의 실패/대기 종료는 아직 안전한 기존 경로와 이동 속도를 초기화하지 않는다.
	const TArray<FVector> PreviousPath = Move->FollowPath;
	Partner->GetCharacterMovement()->Velocity = FVector(200, 0, 0);
	Move->FailFollowPath(0.01, EFlightPathStatus::BudgetExceeded, true);
	TestTrue(TEXT("Retarget failure retains the existing path"), Move->FollowPath == PreviousPath);
	TestTrue(TEXT("Retarget failure retains velocity"), Partner->GetCharacterMovement()->Velocity.Size() > 100);
	Move->FollowRetry.Reset();
	Move->FollowLastFailure = EFlightPathStatus::Success;
	Move->FollowSearch = FlightPathfinding::BeginSearchWithChecks(Request,
		[](const FVector&) { return true; }, [](const FVector&, const FVector&) { return false; });
	Move->FollowSearchStartedAt = 0.0;
	Move->AdvanceFollowPath(Move->FollowSearchTimeout + 0.1, Goal);
	TestFalse(TEXT("Pending request has a bounded wall-clock lifetime"), Move->FollowSearch.IsValid());
	TestTrue(TEXT("Pending timeout does not discard a safe previous path"), Move->FollowPath == PreviousPath);
	Move->FollowRetry.Reset();
	Move->FollowLastFailure = EFlightPathStatus::Success;
	Move->FollowSearch = FlightPathfinding::BeginSearchWithChecks(Request,
		[](const FVector&) { return true; }, [](const FVector&, const FVector&) { return false; });
	Move->FollowSearchStartedAt = 0.0;
	Move->AdvanceFollowPath(0.1, Goal + FVector(1000, 0, 0));
	TestFalse(TEXT("Abrupt target movement cancels pending work"), Move->FollowSearch.IsValid());
	TestTrue(TEXT("Cancellation retains a safe previous path"), Move->FollowPath == PreviousPath);
	TestEqual(TEXT("Cancellation does not count as unreachable"), Move->FollowRetry.Failures, 0);
	Move->FollowSearch = FlightPathfinding::BeginSearchWithChecks(Request,
		[](const FVector&) { return true; }, [](const FVector&, const FVector&) { return false; });
	const TSharedPtr<FFlightPathSearch> PendingAfterArrival = Move->FollowSearch;
	Partner->SetActorLocation(PreviousPath.Last());
	Move->FollowWaypoint = PreviousPath.Num() - 1;
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("Arrival stops the old path without discarding the pending retarget"),
		Move->FollowPath.IsEmpty() && Move->FollowSearch == PendingAfterArrival && Move->bFollowHolding);
	TestTrue(TEXT("Old route arrival stops velocity while waiting for retarget"), Partner->GetCharacterMovement()->Velocity.IsNearlyZero());
	Partner->SetActorLocation(Request.Start);
	Move->ClearFollowPath();
	TestFalse(TEXT("Clearing movement also releases pending search"), Move->FollowSearch.IsValid());
	FindPath(0.0, Goal);
	Move->FollowStoredPath(0.016f);
	TestFalse(TEXT("A route produces normal CharacterMovement input"), Partner->ConsumeMovementInputVector().IsNearlyZero());

	// 자동 추종의 커브 적용/재선정/수동 복구를 실제 Component 경로에서 검증한다.
	UCurveFloat* SpeedCurve = NewObject<UCurveFloat>(Move);
	SpeedCurve->FloatCurve.AddKey(0, 0);
	SpeedCurve->FloatCurve.AddKey(1, 1);
	Move->CameraAssistSpeedProfile.AccelerationCurve = SpeedCurve;
	Move->CameraAssistSpeedProfile.AccelerationDuration = 1.0f;
	Partner->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Move->FollowStoredPath(0.25f);
	const float BaseSpeed = Partner->bIsAccelerate ? Partner->BoostSpeed : Partner->MoveSpeed;
	TestEqual(TEXT("CameraAssist applies quarter-ramp speed"), Partner->GetCharacterMovement()->MaxFlySpeed, BaseSpeed * 0.25f);
	FindPath(0.05, Goal + FVector(0, 20, 0));
	TestEqual(TEXT("Normal server retarget preserves curve clock"), Move->FollowSpeedState.Elapsed, 0.25f);
	Move->StopFollowMovement();
	TestEqual(TEXT("Stopping restores manual speed"), Partner->GetCharacterMovement()->MaxFlySpeed, BaseSpeed);
	TestEqual(TEXT("Hard stop resets ramp"), Move->FollowSpeedState.TargetSpeed, -1.0f);
	Move->CameraAssistSpeedProfile = FFlightSpeedProfile();
	Move->FollowSpeedState.SetPath(Move->FollowPath);
	// 긴 가속 커브의 초반 저속 구간을 정체로 오판하지 않는지 검사한다.
	Move->CameraAssistSpeedProfile.AccelerationCurve = SpeedCurve;
	Move->CameraAssistSpeedProfile.AccelerationDuration = 2.0f;
	UpdatePath(1.1, Goal);
	TestFalse(TEXT("Slow acceleration does not prematurely discard route"), Move->FollowPath.IsEmpty());
	Move->CameraAssistSpeedProfile = FFlightSpeedProfile();
	// 실제 로그의 마지막 145cm 접근을 재현한다. 커브와 입력 감속을 중복 적용하지 않는다.
	Partner->SetActorLocation(Goal - FVector(145, 0, 0));
	FindPath(10.0, Goal);
	Move->CameraAssistSpeedProfile.DecelerationCurve = SpeedCurve;
	Move->CameraAssistSpeedProfile.DecelerationDistance = 200.0f;
	Partner->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Partner->ConsumeMovementInputVector();
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("Deceleration curve does not multiply distance input slowdown"),
		Partner->ConsumeMovementInputVector().X > 0.99f);
	Move->FollowProgressLocation = Partner->GetActorLocation();
	Move->FollowLastProgressAt = 10.0;
	Partner->SetActorLocation(Goal - FVector(143, 0, 0));
	UpdatePath(12.0, Goal);
	TestFalse(TEXT("Two centimeters of final approach is not a stall"), Move->FollowPath.IsEmpty());
	TestEqual(TEXT("Small final progress refreshes its deadline"), Move->FollowLastProgressAt, 12.0);
	UpdatePath(12.0 + Move->FollowNoProgressTimeout + 0.3, Goal);
	TestTrue(TEXT("A truly stationary final approach still retries"), Move->FollowPath.IsEmpty());
	Move->FollowRetry.Reset();
	Move->FollowLastFailure = EFlightPathStatus::Success;
	Move->CameraAssistSpeedProfile = FFlightSpeedProfile();
	Partner->SetActorLocation(FVector(-300, 0, 0));
	FindPath(0.0, Goal);
	Partner->SetActorLocation(Goal - FVector(50, 0, 0));
	UpdatePath(1.2, Goal);
	TestFalse(TEXT("Arrival wins over stale no-progress timer"), Move->FollowPath.IsEmpty());
	Move->FollowStoredPath(0.016f);
	TestTrue(TEXT("Arrival is holding, not a failed route"), Move->bFollowHolding);
	Partner->SetActorLocation(FVector(-300, 0, 0));
	FindPath(0.0, Goal);
	Move->FollowNextPathCheckAt = 0.0;

	// 선정 이후 생긴 장애물은 저장 경로를 취소하고 지연된 재탐색으로 연결한다.
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();
	UpdatePath(0.1, Goal);
	TestTrue(TEXT("Dynamic obstruction clears the old route"), Move->FollowPath.IsEmpty());
	TestEqual(TEXT("Dynamic obstruction starts one retry"), Move->FollowRetry.Failures, 1);
	UpdatePath(0.2, Goal);
	TestTrue(TEXT("Retry does not run before the delay"), Move->FollowPath.IsEmpty());
	UpdatePath(0.4, Goal);
	TestTrue(TEXT("Pawn-blocking wall is detoured despite ignoring Visibility"), Move->FollowPath.Num() > 2);
	for (int32 Index = 1; Index < Move->FollowPath.Num(); ++Index)
	{
		FFlightPathRequest Segment = Move->MakeFollowPathRequest(Move->FollowPath[Index]);
		Segment.Start = Move->FollowPath[Index - 1];
		TestEqual(TEXT("Every stored segment clears the actual capsule"),
			FlightPathfinding::CheckSegment(*World, *Capsule, Segment), EFlightPathStatus::Success);
	}

	// 예산 실패와 경로 없음은 서로 다른 결과이며, 둘 다 단일 실패로 즉시 종료하지 않는다.
	Move->FollowMaxCollisionChecks = 1;
	TestEqual(TEXT("Low budget reports budget rather than unreachable"),
		FlightPathfinding::FindPath(*World, *Capsule, Move->MakeFollowPathRequest(Goal)).Status,
		EFlightPathStatus::BudgetExceeded);
	Move->FollowMaxCollisionChecks = 20000;
	Move->FailFollowPath(0.0, EFlightPathStatus::BudgetExceeded);
	const FFlightPathRequest BudgetRetry = Move->MakeFollowPathRequest(Goal);
	TestEqual(TEXT("Budget retry changes search ordering"), BudgetRetry.HeuristicScale, 2.0);
	Move->FollowRetry.Failures = 3;
	TestEqual(TEXT("Repeated budget failures keep a valid heuristic"), Move->MakeFollowPathRequest(Goal).HeuristicScale, 2.0);
	TestEqual(TEXT("Budget retry keeps collision limit"), BudgetRetry.MaxCollisionChecks, 20000);
	TestTrue(TEXT("Partner reuses symmetric non-start sweeps"), BudgetRetry.bReuseReverseSegmentChecks);
	Move->FailFollowPath(0.1, EFlightPathStatus::BlockedSegment);
	TestEqual(TEXT("Physical obstruction does not enable budget bias"), Move->MakeFollowPathRequest(Goal).HeuristicScale, 1.25);
	Move->FollowRetry.Reset();
	Move->FollowLastFailure = EFlightPathStatus::Success;
	Box->SetBoxExtent(FVector(30.0, 1000.0, 1000.0));
	World->GetPhysicsScene()->Flush();
	TestEqual(TEXT("Sealed route reports no path within bounds"),
		FlightPathfinding::FindPath(*World, *Capsule, Move->MakeFollowPathRequest(Goal)).Status,
		EFlightPathStatus::NoPathWithinBounds);
	Move->FollowRetry.Reset();
	FindPath(1.0, Goal);
	TestEqual(TEXT("First failure leaves assist active"), Partner->MoveMode, EPartnerMoveMode::CameraAssist);
	UpdatePath(1.3, Goal);
	UpdatePath(1.6, Goal);
	UpdatePath(1.9, Goal);
	TestEqual(TEXT("Exhaustion returns to manual movement"), Partner->MoveMode, EPartnerMoveMode::Normal);
	Move->ApplyCameraAssist();
	TestEqual(TEXT("Held input cannot restart assist"), Partner->MoveMode, EPartnerMoveMode::Normal);
	Move->ReleaseCameraAssistInput();
	Move->ApplyCameraAssist();
	TestEqual(TEXT("Release and press allow a fresh attempt"), Partner->MoveMode, EPartnerMoveMode::CameraAssist);

	// 막힌 추종 오프셋은 제한된 근접 목적지로 보정한다. 장애물 자체를 무시하지 않는다.
	Box->SetBoxExtent(FVector(40.0));
	Wall->SetActorLocation(Goal);
	World->GetPhysicsScene()->Flush();
	Move->FollowGoalSearchRadius = 150.0f;
	const FFlightPathResult Adjusted = FlightPathfinding::FindPath(*World, *Capsule, Move->MakeFollowPathRequest(Goal));
	TestTrue(TEXT("Occupied goal has a validated nearby replacement"),
		Adjusted.Status == EFlightPathStatus::Success && Adjusted.bGoalAdjusted);
	if (!Adjusted.Waypoints.IsEmpty())
	{
		TestTrue(TEXT("Replacement remains within the configured radius"),
			FVector::Distance(Goal, Adjusted.Waypoints.Last()) <= 150.01);
	}
	FindPath(2.0, Goal);
	if (!Move->FollowPath.IsEmpty())
	{
		Partner->SetActorLocation(Move->FollowPath.Last());
		Move->FollowWaypoint = Move->FollowPath.Num() - 1;
		Move->FollowStoredPath(0.016f);
		TestTrue(TEXT("Arrival stops input and velocity"), Partner->GetCharacterMovement()->Velocity.IsNearlyZero());
		UpdatePath(3.0, Goal);
		TestTrue(TEXT("Arrival at adjusted goal does not repeatedly replan"), Move->FollowPath.IsEmpty());
		UpdatePath(3.3, Goal + FVector(200.0, 0.0, 0.0));
		TestFalse(TEXT("Meaningful target movement creates a fresh route"), Move->FollowPath.IsEmpty());
	}
	Move->ResetAutoFollowForOwnerChange();
	TestTrue(TEXT("Owner change clears route and held input"), Move->FollowPath.IsEmpty() && !Move->CameraAssistInput.bHeld);

	// SyncMove도 목표 상대 위치/속도 설정만 다르고 우회와 실패 후 재입력 정책은 같다.
	Partner->SetActorLocation(FVector(-300.0, 0.0, 0.0));
	Box->SetBoxExtent(FVector(30.0, 150.0, 500.0));
	Wall->SetActorLocation(FVector::ZeroVector);
	World->GetPhysicsScene()->Flush();
	Move->SetSyncMove(true);
	TestEqual(TEXT("SyncMove enters its own follow mode"), Partner->MoveMode, EPartnerMoveMode::SyncMove);
	TestEqual(TEXT("SyncMove retains its original acceptance radius"), Move->GetFollowAcceptanceRadius(), 25.0f);
	FindPath(4.0, Goal);
	TestTrue(TEXT("SyncMove uses the same capsule detour"), Move->FollowPath.Num() > 2);
	Move->SyncMoveSpeedProfile.AccelerationCurve = SpeedCurve;
	Move->SyncMoveSpeedProfile.AccelerationDuration = 1.0f;
	Move->FollowStoredPath(0.1f);
	TestTrue(TEXT("SyncMove also owns a profiled speed limit"), Move->bOwnsFollowSpeed);
	Move->StopFollowMovement();
	Move->SyncMoveSpeedProfile = FFlightSpeedProfile();
	Box->SetBoxExtent(FVector(30.0, 1000.0, 1000.0));
	World->GetPhysicsScene()->Flush();
	Move->FollowRetry.Reset();
	FindPath(5.0, Goal);
	UpdatePath(5.3, Goal);
	UpdatePath(5.6, Goal);
	UpdatePath(5.9, Goal);
	TestEqual(TEXT("Blocked SyncMove returns to manual mode"), Partner->MoveMode, EPartnerMoveMode::Normal);
	TestTrue(TEXT("Blocked SyncMove clears velocity"), Partner->GetCharacterMovement()->Velocity.IsNearlyZero());
	Move->SetSyncMove(true);
	TestEqual(TEXT("Held SyncMove cannot automatically restart"), Partner->MoveMode, EPartnerMoveMode::Normal);
	Move->SetSyncMove(false);
	Move->SetSyncMove(true);
	TestEqual(TEXT("Released SyncMove can be enabled again"), Partner->MoveMode, EPartnerMoveMode::SyncMove);
	Move->SetSyncMove(false);
	Move->ApplyCameraAssist();
	const uint32 OldCameraSerial = Move->CameraAssistInput.Serial;
	Move->SetSyncMove(true);
	const TArray<FVector> StaleCameraPath = { FVector(-300.0, 0.0, 0.0), Goal };
	Move->ClientReceiveFollowPath_Implementation(EPartnerMoveMode::CameraAssist, OldCameraSerial, StaleCameraPath, true);
	TestEqual(TEXT("Old CameraAssist stop cannot cancel a newer SyncMove"), Partner->MoveMode, EPartnerMoveMode::SyncMove);
	Move->ReleaseCameraAssistInput();
	TestEqual(TEXT("Releasing the inactive CameraAssist button does not stop SyncMove"),
		Partner->MoveMode, EPartnerMoveMode::SyncMove);
	Move->ResetAutoFollowForOwnerChange();

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FPartnerFollowLifecycleTest,
	"Outlier.Partner.AutoFollow.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FPartnerFollowLifecycleTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Mode : {TEXT("CameraAssist"), TEXT("SyncMove")})
	{
		for (const TCHAR* Scenario : {TEXT("Stop"), TEXT("OwnerChange"), TEXT("LateSuccess"), TEXT("LateStop"), TEXT("ModeSwitch")})
		{
			const FString Name = FString::Printf(TEXT("%s.%s"), Mode, Scenario);
			Names.Add(Name);
			Commands.Add(Name);
		}
	}
}

bool FPartnerFollowLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PartnerFollowLifecycle")));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	UPartnerMovementComponent* Move = Partner ? Partner->FindComponentByClass<UPartnerMovementComponent>() : nullptr;
	if (!TestNotNull(TEXT("Partner movement exists"), Move)) return false;
	Move->RefreshCharacterRefsFromPlayerState();
	const EPartnerMoveMode Mode = Parameters.StartsWith(TEXT("CameraAssist."))
		? EPartnerMoveMode::CameraAssist : EPartnerMoveMode::SyncMove;
	FString ModeName, Scenario;
	Parameters.Split(TEXT("."), &ModeName, &Scenario);
	Move->StartAutoFollow(Mode);
	if (!TestEqual(TEXT("Input enables requested mode"), Partner->MoveMode, Mode)) return false;
	Move->FollowMode = Mode;
	const uint32 OldSerial = Move->GetFollowInput(Mode).Serial;
	const FVector Start = Partner->GetCapsuleComponent()->GetComponentLocation();
	const TArray<FVector> OldPath = {Start, Start + FVector(600, 0, 0)};
	Move->FollowPath = OldPath;
	Move->FollowWaypoint = 1;
	Move->bOwnsFollowSpeed = true;
	Move->FollowSpeedState.TargetSpeed = 100.0f;
	Partner->GetCharacterMovement()->MaxFlySpeed = 100.0f;
	Partner->GetCharacterMovement()->Velocity = FVector(100, 0, 0);
	Partner->AddMovementInput(FVector::ForwardVector, 1.0f, true);
	Move->FollowSearch = FlightPathfinding::BeginSearch(*World, *Partner->GetCapsuleComponent(),
		Move->MakeFollowPathRequest(OldPath.Last()));
	if (!TestTrue(TEXT("A pending search is owned by the component"), Move->FollowSearch.IsValid())) return false;
	const TWeakPtr<FFlightPathSearch> PendingSearch = Move->FollowSearch;

	// 실제 종료 API가 경로/진행 중 탐색/입력/속도를 함께 정리하는지 확인한다.
	if (Scenario == TEXT("OwnerChange")) Move->ResetAutoFollowForOwnerChange();
	else Move->StopAutoFollow();
	TestEqual(TEXT("Stop returns to manual mode"), Partner->MoveMode, EPartnerMoveMode::Normal);
	TestTrue(TEXT("Stop releases pending search, not just its route"), !Move->FollowSearch && !PendingSearch.IsValid());
	TestTrue(TEXT("Stop clears path, input and velocity"), Move->FollowPath.IsEmpty()
		&& Partner->GetPendingMovementInputVector().IsNearlyZero() && Partner->GetCharacterMovement()->Velocity.IsNearlyZero());
	TestFalse(TEXT("Stop releases speed ownership"), Move->bOwnsFollowSpeed);
	TestEqual(TEXT("Stop resets speed curve state"), Move->FollowSpeedState.TargetSpeed, -1.0f);
	const float ManualSpeed = Partner->bIsAccelerate ? Partner->BoostSpeed : Partner->MoveSpeed;
	TestEqual(TEXT("Stop restores manual speed"), Partner->GetCharacterMovement()->MaxFlySpeed, ManualSpeed);

	if (Scenario == TEXT("LateSuccess") || Scenario == TEXT("LateStop") || Scenario == TEXT("ModeSwitch"))
	{
		Move->ReleaseAutoFollow(Mode);
		const EPartnerMoveMode NewMode = Scenario == TEXT("ModeSwitch")
			? (Mode == EPartnerMoveMode::CameraAssist ? EPartnerMoveMode::SyncMove : EPartnerMoveMode::CameraAssist) : Mode;
		Move->StartAutoFollow(NewMode);
		Move->FollowMode = NewMode;
		if (!TestEqual(TEXT("A fresh input enables the next session"), Partner->MoveMode, NewMode)) return false;
		const TArray<FVector> NewPath = {Start, Start + FVector(0, 600, 0)};
		Move->FollowPath = NewPath;
		// RPC 수신 함수의 지연/순서 계약만 검사한다. 실제 네트워크 전송 검증과 구분한다.
		Move->ClientReceiveFollowPath_Implementation(Mode, OldSerial, OldPath, Scenario == TEXT("LateStop"));
		TestEqual(TEXT("Late reply cannot change the active mode"), Partner->MoveMode, NewMode);
		TestTrue(TEXT("Late reply cannot overwrite or clear the new route"), Move->FollowPath == NewPath);
		Move->ServerSetAutoFollowInput_Implementation(Mode, false, OldSerial);
		TestEqual(TEXT("Late release cannot stop the new session"), Partner->MoveMode, NewMode);
	}
	else
	{
		Move->ClientReceiveFollowPath_Implementation(Mode, OldSerial, OldPath, false);
		TestTrue(TEXT("A stopped session cannot be revived by a delayed success"), Move->FollowPath.IsEmpty());
		if (Scenario == TEXT("OwnerChange"))
			TestFalse(TEXT("Owner change releases both input latches"), Move->CameraAssistInput.bHeld || Move->SyncMoveInput.bHeld);
		else
		{
			Move->StartAutoFollow(Mode);
			TestEqual(TEXT("Held input cannot automatically restart"), Partner->MoveMode, EPartnerMoveMode::Normal);
			Move->ReleaseAutoFollow(Mode);
			Move->StartAutoFollow(Mode);
			TestEqual(TEXT("Release and press rearms the session"), Partner->MoveMode, Mode);
		}
	}
	Move->ResetAutoFollowForOwnerChange();
	AddInfo(FString::Printf(TEXT("[FollowLifecycle] Scenario=%s Coverage=LocalRpcDeliveryAndCleanup; real network transport remains separate"), *Parameters));
	return true;
}

#endif
