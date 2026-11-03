#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyAIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "UObject/UnrealType.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(GroupMovementHome, "Room.GroupMovementTest.1.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(GroupMovementSibling, "Room.GroupMovementTest.1.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(GroupMovementOther, "Room.GroupMovementTest.2.1");

struct FGroupMovementWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	URoomCombatSubsystem* Rooms = nullptr;
	URoomCombatDefinition* Definition = nullptr;
	ARoomVolume* Home = nullptr;
	ARoomVolume* Sibling = nullptr;
	AEnemyBase* Enemy = nullptr;
	AEnemyAIController* Controller = nullptr;

	FGroupMovementWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("GroupMovement")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
		Rooms = World->GetSubsystem<URoomCombatSubsystem>();
		Definition = NewObject<URoomCombatDefinition>(World);
		Rooms->SetCombatDefinitionForTesting(Definition);
		Home = AddRoom(GroupMovementHome, FVector(-300, 0, 0));
		Sibling = AddRoom(GroupMovementSibling, FVector(300, 0, 0));
		AddRoom(GroupMovementOther, FVector(1200, 0, 0));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(-250, 0, 0)), Params);
		Enemy->GetCapsuleComponent()->SetCapsuleSize(10, 20);
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(GroupMovementHome);
		Controller = World->SpawnActor<AEnemyAIController>();
		Controller->Possess(Enemy);
	}

	~FGroupMovementWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	ARoomVolume* AddRoom(FGameplayTag Tag, FVector Location)
	{
		FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = Tag;
		Entry.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
		Room->SetActorLocation(Location);
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(300));
		Rooms->RegisterRoom(Room, Tag);
		return Room;
	}

	void SetState(EEnemyCombatState State)
	{
		// 전투 시작 정책과 분리해 실제 이동 Task의 상태별 공간 계약을 검사한다.
		*FindFProperty<FEnumProperty>(AEnemyBase::StaticClass(), TEXT("CombatState"))
			->ContainerPtrToValuePtr<EEnemyCombatState>(Enemy) = State;
		Enemy->SetNextFlightPathRequestTime(0.0);
	}

	FEnemyFlyToLocationTaskInstanceData Flight(FVector Goal)
	{
		FEnemyFlyToLocationTaskInstanceData Data;
		Data.Enemy = Enemy;
		Data.CachedController = Controller;
		Data.CachedCapsule = Enemy->GetCapsuleComponent();
		Data.CachedRoomSubsystem = Rooms;
		Data.CachedRoom = Home;
		Data.PathRoomTag = GroupMovementHome;
		Data.Destination = Data.LastDestination = Goal;
		Data.bOwnsMovement = true;
		Data.bUsePathfinding = true;
		Data.GoalSearchRadius = 0;
		Data.PathClearance = 0;
		Data.MaxSearchMilliseconds = 1000;
		Data.bRotateTowardDestination = false;
		return Data;
	}
};

bool CheckFlightPathReady(FAutomationTestBase& Test, const FEnemyFlyToLocationTaskInstanceData& Data)
{
	// Running은 경로 준비 완료뿐 아니라 재탐색 대기도 뜻한다. 배열을 읽기 전에 둘을 구분한다.
	return Test.TestTrue(FString::Printf(TEXT("Flight path ready: Status=%s Waiting=%d Points=%d SpeedLimits=%d"),
		*UEnum::GetValueAsString(Data.PathStatus), Data.bWaitingForPath, Data.PathWaypoints.Num(), Data.WaypointSpeedLimits.Num()),
		!Data.bWaitingForPath && Data.PathStatus == EFlightPathStatus::Success
		&& Data.PathWaypoints.IsValidIndex(1) && Data.WaypointSpeedLimits.IsValidIndex(1));
}

FEnvQueryInstance GroupPointResult(AEnemyBase* Enemy, FVector Point)
{
	FEnvQueryInstance Result;
	Result.Owner = Enemy;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	Result.AddItemData<UEnvQueryItemType_Point>(Point);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGroupFlightBoundaryTest, "Outlier.Enemy.GroupMovement.FlightBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGroupFlightBoundaryTest::RunTest(const FString& Parameters)
{
	FGroupMovementWorld Fixture;
	const FVector Goal(250, 0, 0);
	for (EEnemyCombatState State : {EEnemyCombatState::Combat, EEnemyCombatState::Returning})
	{
		Fixture.SetState(State);
		auto Data = Fixture.Flight(Goal);
		TestEqual(TEXT("Actual Fly path starts across touching sibling volumes"),
			EnemyFlightTesting::StartPath(Data), EStateTreeRunStatus::Running);
		TestEqual(TEXT("Sibling route passes collision and full-segment bounds"), Data.PathStatus, EFlightPathStatus::Success);
		TestTrue(TEXT("Direct flight fallback uses the same group space"), EnemyFlightTesting::CheckDirectBoundary(Data, 0.01f));
		Fixture.Enemy->GetCharacterMovement()->Velocity = FVector(100000, 0, 0);
		TestFalse(TEXT("Inertia cannot leave registered group space"), EnemyFlightTesting::CheckDirectBoundary(Data, 0.1f));
		Fixture.Enemy->GetCharacterMovement()->StopMovementImmediately();
		Fixture.SetState(EEnemyCombatState::NonCombat);
		TestEqual(TEXT("State change invalidates existing path before another input"),
			EnemyFlightTesting::TickPath(Data, 0.01f), EStateTreeRunStatus::Failed);
	}
	Fixture.SetState(EEnemyCombatState::NonCombat);
	auto Patrol = Fixture.Flight(Goal);
	EnemyFlightTesting::StartPath(Patrol);
	TestFalse(TEXT("Noncombat cannot obtain a sibling path"), Patrol.PathStatus == EFlightPathStatus::Success);
	Fixture.SetState(EEnemyCombatState::Combat);
	auto Other = Fixture.Flight(FVector(1200, 0, 0));
	EnemyFlightTesting::StartPath(Other);
	TestFalse(TEXT("Other parent destination cannot obtain a path"), Other.PathStatus == EFlightPathStatus::Success);
	Fixture.SetState(EEnemyCombatState::Combat);
	auto Stale = Fixture.Flight(Goal);
	EnemyFlightTesting::StartPath(Stale);
	Fixture.Rooms->UnregisterRoom(Fixture.Sibling);
	TestEqual(TEXT("Unloaded sibling invalidates captured path"),
		EnemyFlightTesting::TickPath(Stale, 0.01f), EStateTreeRunStatus::Failed);
	TestTrue(TEXT("Invalidation clears movement input"), Fixture.Enemy->GetPendingMovementInputVector().IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGroupQueryBoundaryTest, "Outlier.Enemy.GroupMovement.QueryBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGroupQueryBoundaryTest::RunTest(const FString& Parameters)
{
	FGroupMovementWorld Fixture;
	const auto Result = GroupPointResult(Fixture.Enemy, FVector(250, 0, 0));
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.Enemy = Fixture.Enemy;
	Data.bRequireVisibleTarget = false;
	Data.Clearance = 0;
	Fixture.SetState(EEnemyCombatState::Combat);
	EnemySearchTesting::CaptureQuery(Data);
	EnemySearchTesting::CompleteQuery(Data, Result);
	TestTrue(TEXT("Combat EQS accepts sibling destination through actual final capsule check"), Data.bHasDestination);
	Fixture.SetState(EEnemyCombatState::NonCombat);
	EnemySearchTesting::CaptureQuery(Data);
	EnemySearchTesting::CompleteQuery(Data, Result);
	TestFalse(TEXT("Noncombat EQS rejects sibling destination"), Data.bHasDestination);
	Fixture.SetState(EEnemyCombatState::Combat);
	EnemySearchTesting::CaptureQuery(Data);
	Fixture.Rooms->UnregisterRoom(Fixture.Sibling);
	EnemySearchTesting::CompleteQuery(Data, Result);
	TestEqual(TEXT("EQS completion discards captured registry before publishing a destination"), Data.QueryResult, EEnemyEnvQueryResult::Stale);
	TestFalse(TEXT("Stale EQS exposes no destination"), Data.bHasDestination);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGroupReturnMovementTest, "Outlier.Enemy.GroupMovement.ActualReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGroupReturnMovementTest::RunTest(const FString& Parameters)
{
	FGroupMovementWorld Fixture;
	Fixture.Enemy->SetActorLocation(FVector(250, 0, 0));
	Fixture.SetState(EEnemyCombatState::Returning);
	FEnemyStat* Stat = FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("RuntimeStat"))
		->ContainerPtrToValuePtr<FEnemyStat>(Fixture.Enemy);
	Stat->MoveSpeed = 200;
	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Flying);
	Movement->bRunPhysicsWithNoController = true;
	auto Data = Fixture.Flight(FVector::ZeroVector);
	Data.bReturnHome = true;
	Data.AcceptanceRadius = 1;
	if (!TestEqual(TEXT("Return resolves home destination and starts actual route"),
		EnemyFlightTesting::StartPath(Data), EStateTreeRunStatus::Running)) return false;
	TestFalse(TEXT("Return cannot complete at sibling start"), Fixture.Enemy->CompleteReturnHome());
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	bool bSawIntermediateMovement = false;
	constexpr float Step = 1.0f / 60.0f;
	for (int32 Frame = 0; Frame < 600 && Status == EStateTreeRunStatus::Running; ++Frame)
	{
		const FVector Before = Fixture.Enemy->GetActorLocation();
		Status = EnemyFlightTesting::TickPath(Data, Step);
		// 위치를 직접 갱신하지 않는다. 실제 CharacterMovement가 Task 입력과 캡슐 충돌을 처리한다.
		Movement->TickComponent(Step, LEVELTICK_All, nullptr);
		const FVector After = Fixture.Enemy->GetActorLocation();
		bSawIntermediateMovement |= After.X > 25 && After.X < 225;
		if (!TestTrue(TEXT("Return has no teleport-sized step"), FVector::Distance(Before, After) <= 10.0)) break;
	}
	TestTrue(TEXT("Real movement passes intermediate sibling positions"), bSawIntermediateMovement);
	TestEqual(TEXT("Actual Fly reaches home"), Status, EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("Full capsule actually returned inside home"), Fixture.Enemy->IsInsideDefaultRoom());
	TestTrue(TEXT("Only physical home arrival completes return"), Fixture.Enemy->CompleteReturnHome());
	TestEqual(TEXT("Arrival restores noncombat state"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::NonCombat);
	TestEqual(TEXT("Movement retains original combat ownership"), Fixture.Enemy->GetDefaultRoomTag(), FGameplayTag(GroupMovementHome));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightCornerContinuityTest, "Outlier.Enemy.Flight.CornerContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightCornerContinuityTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("RightAngle"), TEXT("LowFPS"), TEXT("ShortSegments"), TEXT("Reversal"), TEXT("WithCurves")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyFlightCornerContinuityTest::RunTest(const FString& Parameters)
{
	FGroupMovementWorld Fixture;
	FEnemyStat* Stat = FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("RuntimeStat"))
		->ContainerPtrToValuePtr<FEnemyStat>(Fixture.Enemy);
	Stat->MoveSpeed = 250;
	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Flying);
	Movement->MaxAcceleration = 500;
	Movement->BrakingDecelerationFlying = 500;
	auto Data = Fixture.Flight(FVector(-100, 150, 0));
	Data.bUseInitialPath = true;
	Data.AcceptanceRadius = 5;
	Data.PlannedWaypoints = {FVector(-250, 0, 0), FVector(-100, 0, 0), Data.Destination};
	if (Parameters == TEXT("ShortSegments"))
	{
		// NonCombat 경로는 Home 내부만 허용한다. X=0 경계까지 가면 캡슐 일부가 이웃 방에 걸친다.
		Data.Destination = Data.LastDestination = FVector(-50, 20, 0);
		Data.PlannedWaypoints = {FVector(-250, 0, 0), FVector(-100, 0, 0), FVector(-100, 20, 0), Data.Destination};
	}
	else if (Parameters == TEXT("Reversal"))
	{
		Data.Destination = Data.LastDestination = FVector(-230, 0, 0);
		Data.PlannedWaypoints.Last() = Data.Destination;
	}
	else if (Parameters == TEXT("WithCurves"))
	{
		UCurveFloat* Curve = NewObject<UCurveFloat>();
		Curve->FloatCurve.AddKey(0, 0);
		Curve->FloatCurve.AddKey(1, 1);
		Data.SpeedProfile.AccelerationCurve = Curve;
		Data.SpeedProfile.DecelerationCurve = Curve;
	}
	if (!TestEqual(TEXT("Validated corner path starts"), EnemyFlightTesting::StartPath(Data), EStateTreeRunStatus::Running)) return false;
	if (!CheckFlightPathReady(*this, Data)) return false;
	TestTrue(TEXT("Path prepares a finite corner speed"), FMath::IsFinite(Data.WaypointSpeedLimits[1]));
	const float Step = Parameters == TEXT("LowFPS") ? 1.0f / 15.0f : 1.0f / 60.0f;
	Movement->Velocity = FVector(250, 0, 0);
	bool bBrakedBeforeCorner = false;
	bool bCrossedWithMomentum = false;
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	for (int32 Tick = 0; Tick < 1200 && Status == EStateTreeRunStatus::Running; ++Tick)
	{
		const int32 BeforeIndex = Data.WaypointIndex;
		Status = EnemyFlightTesting::TickPath(Data, Step);
		if (Data.WaypointIndex == 1 && Fixture.Enemy->GetActorLocation().X < -120)
			bBrakedBeforeCorner |= Movement->Velocity.Size() < 200;
		if (BeforeIndex == 1 && Data.WaypointIndex == 2)
			bCrossedWithMomentum = !Movement->Velocity.IsNearlyZero();
		Movement->TickComponent(Step, LEVELTICK_All, nullptr);
		if (!TestTrue(TEXT("Corner stays within the permitted room"), Fixture.Enemy->IsInsideDefaultRoom())) break;
	}
	TestTrue(TEXT("Physical speed decreases before reaching the corner"), bBrakedBeforeCorner);
	TestTrue(TEXT("Passing the corner does not clear velocity"), bCrossedWithMomentum);
	TestEqual(TEXT("Physical movement reaches final destination"), Status, EStateTreeRunStatus::Succeeded);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightWaypointCaptureTest, "Outlier.Enemy.Flight.WaypointCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightWaypointCaptureTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Rate : {TEXT("15"), TEXT("30"), TEXT("60"), TEXT("120")})
	{
		Names.Add(Rate);
		Commands.Add(Rate);
	}
}

bool FEnemyFlightWaypointCaptureTest::RunTest(const FString& Parameters)
{
	FGroupMovementWorld Fixture;
	FEnemyStat* Stat = FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("RuntimeStat"))
		->ContainerPtrToValuePtr<FEnemyStat>(Fixture.Enemy);
	Stat->MoveSpeed = 400;
	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Flying);
	Movement->MaxAcceleration = 2048;
	Movement->BrakingDecelerationFlying = 2048;
	auto Data = Fixture.Flight(FVector(-100, -100, 0));
	Data.bUseInitialPath = true;
	Data.AcceptanceRadius = 10;
	Data.MoveTimeout = 10;
	Data.PlannedWaypoints = {FVector(-250, 0, 0), FVector(-200, 0, 0),
		FVector(-150, -50, 0), Data.Destination};
	if (!TestEqual(TEXT("Capture path starts"), EnemyFlightTesting::StartPath(Data), EStateTreeRunStatus::Running)
		|| !CheckFlightPathReady(*this, Data)) return false;

	// 목표 방향과 직교하는 실제 관성으로 시작한다. 방향만 입력하면 작은 경유점 주위를 돌 수 있다.
	// Task와 CharacterMovement를 함께 실행해 벡터 계산만 맞는 가짜 회귀 검증을 피한다.
	Movement->Velocity = FVector(0, 400, 0);
	const float Step = 1.0f / FCString::Atof(*Parameters);
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	bool bRepathed = false;
	for (int32 Tick = 0; Tick < FMath::CeilToInt(10.0f / Step) && Status == EStateTreeRunStatus::Running; ++Tick)
	{
		Status = EnemyFlightTesting::TickPath(Data, Step);
		bRepathed |= Data.bWaitingForPath || Data.bRepathEpisode;
		Movement->TickComponent(Step, LEVELTICK_All, nullptr);
		if (!TestTrue(TEXT("Capture stays inside its room"), Fixture.Enemy->IsInsideDefaultRoom())) return false;
	}
	TestFalse(TEXT("Turning inertia alone does not trigger a path retry"), bRepathed);
	TestEqual(TEXT("Physical movement captures small waypoints before timeout"), Status, EStateTreeRunStatus::Succeeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightRepathContinuityTest, "Outlier.Enemy.Flight.RepathContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightRepathContinuityTest::RunTest(const FString& Parameters)
{
	FGroupMovementWorld Fixture;
	FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("RuntimeStat"))
		->ContainerPtrToValuePtr<FEnemyStat>(Fixture.Enemy)->MoveSpeed = 200;
	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Flying);
	auto Data = Fixture.Flight(FVector(-50, 0, 0));
	if (!TestEqual(TEXT("Original route starts"), EnemyFlightTesting::StartPath(Data), EStateTreeRunStatus::Running)) return false;
	if (!CheckFlightPathReady(*this, Data)) return false;
	Movement->Velocity = FVector(100, 0, 0);
	Fixture.Enemy->SetNextFlightPathRequestTime(Fixture.World->GetTimeSeconds() + 0.4);
	Data.Destination = FVector(-25, 100, 0);
	TestEqual(TEXT("Throttled retarget keeps running"), EnemyFlightTesting::TickPath(Data, 0.01f), EStateTreeRunStatus::Running);
	TestTrue(TEXT("Retarget keeps a validated path while waiting"), Data.bWaitingForPath && !Data.PathWaypoints.IsEmpty());
	TestTrue(TEXT("Request interval does not hard-stop physical velocity"), !Movement->Velocity.IsNearlyZero());
	const float Deadline = Data.RetryElapsed;
	Fixture.Enemy->SetActorLocation(FVector(-100, 0, 0));
	EnemyFlightTesting::TickPath(Data, 0.01f);
	TestTrue(TEXT("Progress on old route cannot reset retry deadline"), Data.bRepathEpisode && Data.RetryElapsed > Deadline);
	Fixture.Enemy->SetActorLocation(FVector(-50, 0, 0));
	TestEqual(TEXT("Old endpoint is not new destination success"), EnemyFlightTesting::TickPath(Data, 0.01f), EStateTreeRunStatus::Running);
	TestTrue(TEXT("Exhausted old route stops safely while retry remains pending"),
		Data.bWaitingForPath && Data.PathWaypoints.IsEmpty() && Movement->Velocity.IsNearlyZero());
	Data.RetryElapsed = Data.RepathTimeout;
	TestEqual(TEXT("Retry deadline still ends pending movement"), EnemyFlightTesting::TickPath(Data, 0.01f), EStateTreeRunStatus::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightRepathBlockedTest, "Outlier.Enemy.Flight.RepathBlocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightRepathBlockedTest::RunTest(const FString& Parameters)
{
	FGroupMovementWorld Fixture;
	FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("RuntimeStat"))
		->ContainerPtrToValuePtr<FEnemyStat>(Fixture.Enemy)->MoveSpeed = 200;
	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Flying);
	auto Data = Fixture.Flight(FVector(-50, 0, 0));
	if (!TestEqual(TEXT("Original route starts before wall exists"), EnemyFlightTesting::StartPath(Data), EStateTreeRunStatus::Running)) return false;
	if (!CheckFlightPathReady(*this, Data)) return false;
	Movement->Velocity = FVector(100, 0, 0);
	Fixture.Enemy->SetNextFlightPathRequestTime(Fixture.World->GetTimeSeconds() + 0.4);
	Data.Destination = FVector(-25, 100, 0);
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(10, 100, 100));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Wall->SetActorLocation(FVector(-125, 0, 0));
	TestEqual(TEXT("Blocked old route waits instead of moving through wall"), EnemyFlightTesting::TickPath(Data, 0.01f), EStateTreeRunStatus::Running);
	TestTrue(TEXT("Actual blockage clears old route and velocity"),
		Data.bWaitingForPath && Data.PathWaypoints.IsEmpty() && Movement->Velocity.IsNearlyZero());
	return true;
}

#endif
