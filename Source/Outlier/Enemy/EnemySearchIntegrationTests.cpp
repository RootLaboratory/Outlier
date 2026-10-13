#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"

namespace
{
struct FEnemySearchIntegrationWorld
{
	const FGameplayTag SearchRoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	ARoomVolume* Room = nullptr;
	URoomCombatDefinition* CombatDefinition = nullptr;

	FEnemySearchIntegrationWorld()
	{
		if (!GEngine) return;

		const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemySearchIntegration"));
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!World) return;

		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		World->AddToRoot();
		Context.SetCurrentWorld(World);

		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		// 임시 Game 월드는 AI System을 기본 생성하지 않는다. 실제 EQS Manager를 준비한 뒤 액터를 초기화한다.
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());

		CombatDefinition = NewObject<URoomCombatDefinition>(GetTransientPackage());
		FRoomCombatRoomDefinition& RoomDefinition = CombatDefinition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = SearchRoomTag;
		RoomDefinition.CombatPhases.AddDefaulted();

		if (URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>())
		{
			Combat->SetCombatDefinitionForTesting(CombatDefinition);
		}
	}

	~FEnemySearchIntegrationWorld()
	{
		if (!World) return;

		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		if (GameInstance) GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool CreateRoom(const FVector& Extent = FVector(1600.0, 1600.0, 700.0))
	{
		if (!World) return false;
		Room = World->SpawnActor<ARoomVolume>(ARoomVolume::StaticClass(), FTransform::Identity);
		if (!Room) return false;

		if (UBoxComponent* Box = Cast<UBoxComponent>(Room->GetRootComponent()))
		{
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Overlap);
		}

		URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
		return Combat && Combat->RegisterRoom(Room, SearchRoomTag);
	}

	AEnemyBase* SpawnEnemy(const FVector& Location = FVector::ZeroVector) const
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World ? World->SpawnActor<AEnemyBase>(
			AEnemyBase::StaticClass(), FTransform(Location), Parameters) : nullptr;
		if (!Enemy) return nullptr;

		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(SearchRoomTag);
		Enemy->EnterCombatFromRoom(FVector::ZeroVector, false);
		Enemy->UpdateLastKnownPlayerLocation(FVector::ZeroVector);
		return Enemy;
	}

	AActor* SpawnBlockingBox(const FVector& Location, const FVector& Extent) const
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location)) : nullptr;
		if (!Actor) return nullptr;

		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Actor;
	}
};

FEnvQueryInstance MakePointResult(UObject* Owner, const TArray<FVector>& Points)
{
	FEnvQueryInstance Result;
	Result.Owner = Owner;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	for (const FVector& Point : Points)
	{
		Result.AddItemData<UEnvQueryItemType_Point>(Point);
	}
	return Result;
}

FEnemyRunEnvQueryTaskInstanceData MakeSearchData(
	AEnemyBase* Enemy, ARoomVolume* Room, EEnemySearchQueryMode Mode)
{
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.Enemy = Enemy;
	Data.SearchMode = Mode;
	Data.bRequireVisibleTarget = false;
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	Data.CachedRooms = Enemy ? Enemy->GetWorld()->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	Data.CapturedRoomTag = Enemy->GetRoomTagComp()->GetDefaultRoomTag();
	Data.CapturedRoom = Room;
	Data.CapturedCombatState = EEnemyCombatState::Combat;
	Data.CapturedGeneration = Enemy ? Enemy->GetPoolGameplayGeneration() : 0;
	Data.CapturedLease = Enemy ? Enemy->GetPoolLeaseSerial() : 0;
	Data.QueryTimeout = 60.0f;
	Data.QueryStartedAt = Enemy->GetWorld()->GetTimeSeconds();
	Data.PathCellSize = 100.0f;
	Data.SearchPadding = 900.0f;
	Data.MaxPathCandidates = 16;
	Data.MaxSearchNodes = 65536;
	Data.MaxCollisionChecks = 262144;
	Data.MaxSearchMilliseconds = 10000.0f;
	Data.SearchPointSeparation = 200.0f;
	if (Enemy)
	{
		const FEnemySearchEpisode& Episode = Enemy->GetSearchEpisode();
		Data.CapturedSearchSerial = Episode.Serial;
		Data.CapturedSharedRevision = Episode.SharedRevision;
		Data.CapturedOrbitCenter = Episode.Center;
		Data.CapturedSearchHeight = Enemy->GetActorLocation().Z;
	}
	return Data;
}

bool BeginSearch(FAutomationTestBase& Test, AEnemyBase* Enemy, const FVector& Center)
{
	if (!Test.TestNotNull(TEXT("Enemy exists"), Enemy)) return false;
	Enemy->UpdateLastKnownPlayerLocation(Center);
	Test.TestTrue(TEXT("Search episode begins"), Enemy->BeginSearchEpisode());
	return Enemy->GetSearchEpisode().bActive;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchAnchorCorrectionIntegrationTest,
	"Outlier.Enemy.Search.Integration.AnchorCorrection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchAnchorCorrectionIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector(-300.0, 0.0, 300.0));
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;

	// 목격 XY에 가장 가까운 보정 후보가 점수순 앞 후보보다 우선되어야 한다.
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Anchor);
	const FEnvQueryInstance Result = MakePointResult(Enemy,
		{FVector(350.0, 0.0, 300.0), FVector(0.0, 0.0, 300.0), FVector(100.0, 0.0, 300.0)});

	TestTrue(TEXT("Anchor selection succeeds through real selector"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	TestEqual(TEXT("Anchor selects remembered XY first"), Data.Destination, FVector(0.0, 0.0, 300.0));
	TestTrue(TEXT("Anchor result is exposed as destination"), Data.Destination.Equals(FVector(0.0, 0.0, 300.0)));

	// 마지막 목격 위치를 실제 콜리전으로 막으면 주변 접근 가능한 후보로 보정한다.
	// 100cm 후보도 박스 내부에 넣는다. 60cm 박스에서는 기본 캡슐이 해당 후보에 안전하게 들어갈 수 있다.
	Fixture.SpawnBlockingBox(FVector(0.0, 0.0, 300.0), FVector(120.0));
	Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Anchor);
	TestTrue(TEXT("Blocked remembered position selects nearby correction"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	TestEqual(TEXT("Correction selects the closest capsule-safe candidate"), Data.Destination, FVector(350.0, 0.0, 300.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchCandidateCountIntegrationTest,
	"Outlier.Enemy.Search.Integration.CandidateCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchCandidateCountIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;

	// 후보 0개: 범위 밖 후보만 있으면 LocalGuard로 갈 수 있는 실패를 남긴다.
	AEnemyBase* ZeroEnemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, ZeroEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData ZeroData = MakeSearchData(ZeroEnemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance ZeroResult = MakePointResult(ZeroEnemy, {FVector(1300.0, 0.0, 0.0)});
	TestFalse(TEXT("Out-of-range candidates are not selected"),
		EnemySearchTesting::SelectDestinations(ZeroData, ZeroResult));
	TestEqual(TEXT("Zero candidates report no reachable destination"),
		ZeroData.QueryResult, EEnemyEnvQueryResult::NoReachableDestination);
	TestTrue(TEXT("Zero candidates leave no selected points"), ZeroData.SelectedSearchPoints.IsEmpty());
	TestTrue(TEXT("Empty point commit enters LocalGuard"), ZeroEnemy->SetSearchPoints({}));
	TestTrue(TEXT("Zero-candidate episode is LocalGuard"), ZeroEnemy->GetSearchEpisode().bLocalGuard);
	ZeroEnemy->SetActorEnableCollision(false);

	// 후보 1개: 두 번째 후보가 너무 가까우면 보충하지 않고 한 지점만 사용한다.
	AEnemyBase* OneEnemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, OneEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData OneData = MakeSearchData(OneEnemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FVector FirstPoint(550.0, 0.0, 0.0);
	const FEnvQueryInstance OneResult = MakePointResult(OneEnemy, {FirstPoint, FVector(620.0, 0.0, 0.0)});
	TestTrue(TEXT("One reachable point is accepted"),
		EnemySearchTesting::SelectDestinations(OneData, OneResult));
	TestEqual(TEXT("Only one separated point is selected"), OneData.SelectedSearchPoints.Num(), 1);
	TestEqual(TEXT("One-point destination is the first point"), OneData.Destination, FirstPoint);
	OneEnemy->SetActorEnableCollision(false);

	// 후보 2개: 두 번째 이동은 첫 지점에서 출발하는 실제 경로로 검증된다.
	AEnemyBase* TwoEnemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, TwoEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData TwoData = MakeSearchData(TwoEnemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FVector SecondPoint(0.0, 650.0, 0.0);
	const FEnvQueryInstance TwoResult = MakePointResult(TwoEnemy, {FirstPoint, SecondPoint});
	TestTrue(TEXT("Two reachable points are accepted"),
		EnemySearchTesting::SelectDestinations(TwoData, TwoResult));
	if (!TestEqual(TEXT("Two points are preselected"), TwoData.SelectedSearchPoints.Num(), 2)) return false;
	TestTrue(TEXT("First selected point is preserved"), TwoData.SelectedSearchPoints[0].Equals(FirstPoint));
	TestTrue(TEXT("Second selected point is preserved"), TwoData.SelectedSearchPoints[1].Equals(SecondPoint));

	TestTrue(TEXT("Selected points can be committed to the episode"),
		TwoEnemy->SetSearchPoints(TwoData.SelectedSearchPoints));
	FEnemySearchProgressTaskInstanceData Progress;
	Progress.Enemy = TwoEnemy;
	Progress.Action = EEnemySearchProgress::SelectCurrentPoint;
	TestEqual(TEXT("Progress exposes first point"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("Current point index starts at zero"), Progress.PointIndex, 0);
	TestTrue(TEXT("Current point is first selected destination"), Progress.Destination.Equals(FirstPoint));
	Progress.Action = EEnemySearchProgress::PointChecked;
	TestEqual(TEXT("First point completion succeeds"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("Progress advances to second point"), Progress.PointIndex, 1);
	TestTrue(TEXT("Second point remains available"), Progress.bHasPoint && Progress.Destination.Equals(SecondPoint));
	Progress.Action = EEnemySearchProgress::PointChecked;
	TestEqual(TEXT("Second point completion enters LocalGuard"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("Episode reaches LocalGuard after two points"), TwoEnemy->GetSearchEpisode().bLocalGuard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchObstacleBoundaryBudgetIntegrationTest,
	"Outlier.Enemy.Search.Integration.ObstacleBoundaryBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchObstacleBoundaryBudgetIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom(FVector(900.0, 900.0, 500.0)))) return false;

	// 이동을 시작하기 전에 막힌 후보를 제외하고 다음 후보의 경로를 검증한다.
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector(-500.0, -500.0, 100.0));
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;
	Fixture.SpawnBlockingBox(FVector(600.0, 0.0, 100.0), FVector(70.0));
	const FVector First(500.0, -500.0, 100.0);
	const FVector BlockedSecond(600.0, 0.0, 100.0);
	const FVector ReachableSecond(0.0, 650.0, 100.0);
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance Result = MakePointResult(Enemy, {First, BlockedSecond, ReachableSecond});
	TestTrue(TEXT("Selection uses next candidate after a blocked second leg"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	TestEqual(TEXT("Two points selected despite dynamic obstacle"), Data.SelectedSearchPoints.Num(), 2);
	TestTrue(TEXT("Blocked second leg is skipped"), !Data.SelectedSearchPoints.Contains(BlockedSecond));
	TestTrue(TEXT("Reachable second leg is selected"), Data.SelectedSearchPoints.Contains(ReachableSecond));

	// 방 밖 후보는 실제 RoomVolume 경계 검사에서 탈락한다.
	FEnemySearchIntegrationWorld BoundaryFixture;
	if (!TestTrue(TEXT("Boundary room registers"), BoundaryFixture.CreateRoom(FVector(600.0, 900.0, 500.0)))) return false;
	AEnemyBase* BoundaryEnemy = BoundaryFixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, BoundaryEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData BoundaryData = MakeSearchData(BoundaryEnemy, BoundaryFixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance BoundaryResult = MakePointResult(BoundaryEnemy, {FVector(0.0, 650.0, 0.0), FVector(700.0, 0.0, 0.0)});
	TestTrue(TEXT("Inside-room candidate succeeds"),
		EnemySearchTesting::SelectDestinations(BoundaryData, BoundaryResult));
	TestEqual(TEXT("Outside boundary candidate is not added"), BoundaryData.SelectedSearchPoints.Num(), 1);

	// 같은 막힘도 충분한 예산이면 NoPath, 지나치게 작은 예산이면 BudgetExceeded로 구분한다.
	FEnemySearchIntegrationWorld SealedFixture;
	if (!TestTrue(TEXT("Sealed room registers"), SealedFixture.CreateRoom(FVector(900.0, 900.0, 500.0)))) return false;
	SealedFixture.SpawnBlockingBox(FVector(0.0, 0.0, 100.0), FVector(40.0, 900.0, 500.0));
	AEnemyBase* NoPathEnemy = SealedFixture.SpawnEnemy(FVector(-500.0, 0.0, 100.0));
	if (!BeginSearch(*this, NoPathEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData NoPathData = MakeSearchData(NoPathEnemy, SealedFixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance NoPathResult = MakePointResult(NoPathEnemy, {FVector(650.0, 0.0, 100.0)});
	TestFalse(TEXT("Sealed wall prevents route"),
		EnemySearchTesting::SelectDestinations(NoPathData, NoPathResult));
	TestEqual(TEXT("Physical no-path is not a budget failure"),
		NoPathData.QueryResult, EEnemyEnvQueryResult::NoReachableDestination);
	TestEqual(TEXT("Physical failure records no path status"),
		NoPathData.SelectionPathStatus, EFlightPathStatus::NoPathWithinBounds);

	AEnemyBase* BudgetEnemy = SealedFixture.SpawnEnemy(FVector(-500.0, 300.0, 100.0));
	if (!BeginSearch(*this, BudgetEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData BudgetData = MakeSearchData(BudgetEnemy, SealedFixture.Room, EEnemySearchQueryMode::Points);
	BudgetData.MaxSearchNodes = 1;
	const FEnvQueryInstance BudgetResult = MakePointResult(BudgetEnemy, {FVector(650.0, 300.0, 100.0)});
	TestFalse(TEXT("Tiny search budget fails distinctly"),
		EnemySearchTesting::SelectDestinations(BudgetData, BudgetResult));
	TestEqual(TEXT("Budget failure is explicit"),
		BudgetData.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
	TestEqual(TEXT("Budget status is preserved"),
		BudgetData.SelectionPathStatus, EFlightPathStatus::BudgetExceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchReservationIntegrationTest,
	"Outlier.Enemy.Search.Integration.ReservationRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchReservationIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyBase* First = Fixture.SpawnEnemy(FVector::ZeroVector);
	AEnemyBase* Second = Fixture.SpawnEnemy(FVector(0.0, 300.0, 0.0));
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms)
		|| !BeginSearch(*this, First, FVector::ZeroVector)
		|| !BeginSearch(*this, Second, FVector::ZeroVector))
	{
		return false;
	}

	const FVector Reserved(550.0, 0.0, 0.0);
	const FVector Alternative(0.0, 650.0, 0.0);
	TestTrue(TEXT("First enemy reserves a search point"),
		Rooms->ReserveSearchDestinations(First, {Reserved}, 200.0f));

	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Second, Fixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance Result = MakePointResult(Second, {Reserved, Alternative});
	TestTrue(TEXT("Second enemy finds non-reserved alternative"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	if (!TestEqual(TEXT("Reservation leaves one usable point"), Data.SelectedSearchPoints.Num(), 1)) return false;
	TestEqual(TEXT("Reserved point is rejected"), Data.SelectedSearchPoints[0], Alternative);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchAssetQueryIntegrationTest,
	"Outlier.Enemy.Search.Integration.AssetQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchAssetQueryIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;

	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
	if (!TestNotNull(TEXT("EQS manager exists"), Manager)) return false;

	// 실제 에셋 템플릿을 동기 실행해 Generator/Test 연결이 살아 있는지 확인한다.
	UEnvQuery* AnchorQuery = LoadObject<UEnvQuery>(
		nullptr, TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_Drone_SearchAnchor.EQS_Drone_SearchAnchor"));
	UEnvQuery* PointsQuery = LoadObject<UEnvQuery>(
		nullptr, TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_SearchPoints.EQS_SearchPoints"));
	if (!TestNotNull(TEXT("SearchAnchor EQS asset loads"), AnchorQuery)
		|| !TestNotNull(TEXT("SearchPoints EQS asset loads"), PointsQuery))
	{
		return false;
	}

	FEnvQueryRequest AnchorRequest(AnchorQuery, Enemy);
	EnemySearch::SetQuerySnapshot(AnchorRequest, Enemy->GetSearchEpisode().Center, Enemy->GetActorLocation().Z);
	const TSharedPtr<FEnvQueryResult> AnchorResult = Manager->RunInstantQuery(AnchorRequest, EEnvQueryRunMode::AllMatching);
	TestTrue(TEXT("Anchor query finishes successfully"), AnchorResult.IsValid() && AnchorResult->IsSuccessful());
	TestTrue(TEXT("Anchor query returns point-compatible results"),
		AnchorResult.IsValid() && AnchorResult->ItemType
		&& AnchorResult->ItemType->IsChildOf(UEnvQueryItemType_Point::StaticClass()));
	TestTrue(TEXT("Anchor query produces at least one candidate"),
		AnchorResult.IsValid() && AnchorResult->Items.Num() > 0);

	FEnvQueryRequest PointsRequest(PointsQuery, Enemy);
	EnemySearch::SetQuerySnapshot(PointsRequest, Enemy->GetSearchEpisode().Center, Enemy->GetActorLocation().Z);
	const TSharedPtr<FEnvQueryResult> PointsResult = Manager->RunInstantQuery(PointsRequest, EEnvQueryRunMode::AllMatching);
	TestTrue(TEXT("Search points query finishes successfully"), PointsResult.IsValid() && PointsResult->IsSuccessful());
	TestTrue(TEXT("Search points query returns point-compatible results"),
		PointsResult.IsValid() && PointsResult->ItemType
		&& PointsResult->ItemType->IsChildOf(UEnvQueryItemType_Point::StaticClass()));
	TestTrue(TEXT("Search points query produces at least one candidate"),
		PointsResult.IsValid() && PointsResult->Items.Num() > 0);
	if (AnchorResult.IsValid() && AnchorResult->IsSuccessful())
	{
		FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Anchor);
		TestTrue(TEXT("Real anchor EQS result passes actual path selection"),
			EnemySearchTesting::SelectDestinations(Data, *AnchorResult));
	}
	if (PointsResult.IsValid() && PointsResult->IsSuccessful())
	{
		FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Points);
		TestTrue(TEXT("Real points EQS result passes actual path selection"),
			EnemySearchTesting::SelectDestinations(Data, *PointsResult));
		TestTrue(TEXT("Real EQS selects one or two reachable search points"),
			Data.SelectedSearchPoints.Num() >= 1 && Data.SelectedSearchPoints.Num() <= 2);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchQueryCompletionTest,
	"Outlier.Enemy.Search.Integration.QueryCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchQueryCompletionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Success"), TEXT("FreshHint"), TEXT("NewEpisode"), TEXT("Rediscovery"),
		TEXT("LocalGuard"), TEXT("Stun"), TEXT("Possessed"), TEXT("LeaseChanged"), TEXT("TimedOut"), TEXT("Empty")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemySearchQueryCompletionTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller)) return false;
	Controller->Possess(Enemy);
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Points);
	EnemySearchTesting::CaptureQuery(Data);
	const FVector Goal(550.0, 0.0, 0.0);
	const FEnvQueryInstance Result = MakePointResult(Enemy, {Goal});
	EEnemyEnvQueryResult Expected = EEnemyEnvQueryResult::Stale;
	if (Parameters == TEXT("Success")) Expected = EEnemyEnvQueryResult::Success;
	else if (Parameters == TEXT("FreshHint")) Enemy->ApplySharedTargetContact(FVector(100.0, 200.0, 0.0), true, 10);
	else if (Parameters == TEXT("NewEpisode")) Enemy->BeginSearchEpisode();
	else if (Parameters == TEXT("Rediscovery")) Enemy->SetPlayerCurrentlyVisible(true);
	else if (Parameters == TEXT("LocalGuard")) Enemy->EnterSearchLocalGuard();
	else if (Parameters == TEXT("Stun")) Enemy->EnterStun();
	else if (Parameters == TEXT("Possessed")) Enemy->SetEnemyPossessed(true);
	else if (Parameters == TEXT("LeaseChanged")) ++Data.CapturedLease;
	else if (Parameters == TEXT("TimedOut"))
	{
		Data.QueryStartedAt -= Data.QueryTimeout + 1.0;
		Expected = EEnemyEnvQueryResult::TimedOut;
	}
	else if (Parameters == TEXT("Empty")) Expected = EEnemyEnvQueryResult::NoResults;
	else
	{
		AddError(FString::Printf(TEXT("Unknown query completion scenario %s"), *Parameters));
		return false;
	}
	if (Parameters == TEXT("Empty")) EnemySearchTesting::CompleteQuery(Data, MakePointResult(Enemy, {}));
	else EnemySearchTesting::CompleteQuery(Data, Result);
	TestEqual(TEXT("Real result application classifies completion correctly"), Data.QueryResult, Expected);
	TestEqual(TEXT("Only a successful current query exposes a destination"), Data.bHasDestination, Expected == EEnemyEnvQueryResult::Success);
	TestEqual(TEXT("Only a successful result commits search points"), Enemy->GetSearchEpisode().bPointsSelected, Expected == EEnemyEnvQueryResult::Success);
	if (Expected == EEnemyEnvQueryResult::Success)
	{
		TestEqual(TEXT("Committed destination matches query result"), Data.Destination, Goal);
		TestTrue(TEXT("Successful query commits exactly one search point"), Enemy->GetSearchEpisode().Points.Num() == 1);
	}
	Enemy->EndSearchEpisode();
	AddInfo(FString::Printf(TEXT("[SearchScenario] QueryCompletion=%s Result=%s"),
		*Parameters, *UEnum::GetValueAsString(Data.QueryResult)));
	return true;
}

#endif
