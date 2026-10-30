#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyAIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
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

#endif
