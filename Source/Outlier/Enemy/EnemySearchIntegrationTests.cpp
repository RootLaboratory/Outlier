#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyStateTreeSelfDestructTasks.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "StateTree.h"
#include "UObject/UnrealType.h"

namespace
{
struct FEnemySearchIntegrationWorld
{
	const FGameplayTag SearchRoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	ARoomVolume* Room = nullptr;
	URoomCombatDefinition* CombatDefinition = nullptr;

	FEnemySearchIntegrationWorld(bool bStartPlay = false)
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
		if (bStartPlay)
		{
			// 프로젝트 GameMode의 플레이어/스테이지 생성 없이 실제 월드 Tick만 실행한다.
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			World->SetGameMode(URL);
		}
		World->InitializeActorsForPlay(FURL());
		if (bStartPlay) World->BeginPlay();

		// BeginPlay에서 로드하는 프로젝트 방 정의 대신, 테스트에 필요한 방만 주입한다.
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
		// 실제 Tick 테스트는 액터와 서브시스템의 종료 처리를 마친 뒤 월드를 제거한다.
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
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

	void Tick(float DeltaTime) const
	{
		// TimerManager는 같은 엔진 프레임에서 중복 Tick하지 않는다. 테스트가 프레임 경계도 진행한다.
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaTime);
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

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchSelectionResumeIntegrationTest,
	"Outlier.Enemy.Search.Integration.SelectionResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchSelectionResumeIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("AnchorNextCandidate"), TEXT("PointsNextCandidate"), TEXT("RetryLimit"),
		TEXT("CandidateLimit"), TEXT("Disabled"), TEXT("NoRemainingCandidate"), TEXT("RemainingRejected"),
		TEXT("Stale"), TEXT("SightRegained"), TEXT("ControllerLost"), TEXT("SessionEnded"),
		TEXT("Timeout"), TEXT("MissingResult")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemySearchSelectionResumeIntegrationTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	Fixture.SpawnBlockingBox(FVector(0.0, 0.0, 100.0), FVector(40.0, 1600.0, 700.0));
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector(-500.0, 0.0, 100.0));
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("Controller"), Controller)) return false;
	Controller->Possess(Enemy);
	if (!BeginSearch(*this, Enemy, FVector(100.0, 0.0, 100.0))) return false;
	const bool bPoints = Parameters == TEXT("PointsNextCandidate");
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room,
		bPoints ? EEnemySearchQueryMode::Points : EEnemySearchQueryMode::Anchor);
	Data.MaxSearchNodes = 1;
	Data.MaxSearchSelectionRetries = 1;
	Data.MaxPathCandidates = 4;
	EnemySearchTesting::CaptureQuery(Data);
	const FVector First = bPoints ? FVector(650.0, 0.0, 100.0) : FVector(100.0, 0.0, 100.0);
	const FVector Safe = bPoints ? FVector(-650.0, 0.0, 100.0) : FVector(-200.0, 0.0, 100.0);
	TArray<FVector> Candidates = {First, Safe};
	if (Parameters == TEXT("RetryLimit")) Candidates = {First, FVector(200.0, 0.0, 100.0), FVector(300.0, 0.0, 100.0)};
	if (Parameters == TEXT("RemainingRejected")) Candidates = {First, FVector(-900.0, 0.0, 100.0)};
	if (Parameters == TEXT("CandidateLimit")) Data.MaxPathCandidates = 1;
	if (Parameters == TEXT("Disabled")) Data.MaxSearchSelectionRetries = 0;
	if (Parameters == TEXT("NoRemainingCandidate")) Candidates = {First};
	const TSharedPtr<FEnvQueryInstance> Result = MakeShared<FEnvQueryInstance>(MakePointResult(Enemy, Candidates));
	EnemySearchTesting::CompleteQuery(Data, *Result);
	TestFalse(TEXT("Budget exhaustion exposes no unverified destination"), Data.bHasDestination);
	TestEqual(TEXT("First candidate consumes one attempt"), Data.CheckedPathCandidates, 1);
	if (Parameters == TEXT("CandidateLimit") || Parameters == TEXT("Disabled") || Parameters == TEXT("NoRemainingCandidate"))
	{
		TestFalse(TEXT("Bounded selection cannot schedule more work"), Data.bSearchSelectionPending);
		TestEqual(TEXT("Exhaustion is not physical no-path"), Data.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
		return true;
	}
	if (!TestTrue(TEXT("Budget failure defers rather than completes the Task"), Data.bSearchSelectionPending)) return false;
	TestEqual(TEXT("Pending task remains Running"), Data.QueryResult, EEnemyEnvQueryResult::Running);
	Data.DeferredSearchResult = Result;
	TestEqual(TEXT("Interval prevents immediate expensive retry"),
		EnemySearchTesting::ResumeSelection(Data), EStateTreeRunStatus::Running);
	TestEqual(TEXT("Waiting does not recheck first candidate"), Data.CheckedPathCandidates, 1);
	Data.NextSearchSelectionTime = Fixture.World->GetTimeSeconds();
	const bool bStale = Parameters == TEXT("Stale") || Parameters == TEXT("SightRegained")
		|| Parameters == TEXT("ControllerLost") || Parameters == TEXT("SessionEnded");
	if (Parameters == TEXT("Stale")) ++Data.CapturedSearchSerial;
	if (Parameters == TEXT("SightRegained")) Enemy->SetPlayerCurrentlyVisible(true);
	if (Parameters == TEXT("ControllerLost")) Controller->UnPossess();
	if (Parameters == TEXT("SessionEnded")) Enemy->EndSearchEpisode();
	if (Parameters == TEXT("Timeout")) Data.QueryStartedAt = Fixture.World->GetTimeSeconds() - Data.QueryTimeout;
	if (Parameters == TEXT("MissingResult")) Data.DeferredSearchResult.Reset();
	const EStateTreeRunStatus Status = EnemySearchTesting::ResumeSelection(Data);
	if (bStale || Parameters == TEXT("Timeout") || Parameters == TEXT("MissingResult"))
	{
		TestEqual(TEXT("Invalid continuation fails safely"), Status, EStateTreeRunStatus::Failed);
		const EEnemyEnvQueryResult Expected = bStale ? EEnemyEnvQueryResult::Stale
			: Parameters == TEXT("Timeout") ? EEnemyEnvQueryResult::TimedOut : EEnemyEnvQueryResult::Failed;
		TestEqual(TEXT("Cancellation preserves its reason"), Data.QueryResult, Expected);
		TestEqual(TEXT("Cancelled selection performs no extra path checks"), Data.CheckedPathCandidates, 1);
		TestFalse(TEXT("Cancellation releases saved query"), Data.DeferredSearchResult.IsValid());
		TestFalse(TEXT("Cancellation clears pending flag"), Data.bSearchSelectionPending);
		return true;
	}
	TestEqual(TEXT("Continuation does not recheck the first candidate"), Data.CheckedPathCandidates,
		Parameters == TEXT("RemainingRejected") ? 1 : 2);
	TestFalse(TEXT("Final selection releases saved query"), Data.DeferredSearchResult.IsValid());
	if (Parameters == TEXT("RetryLimit") || Parameters == TEXT("RemainingRejected"))
	{
		TestEqual(TEXT("Retry limit terminates even with unchecked candidates"), Status, EStateTreeRunStatus::Failed);
		TestEqual(TEXT("Retry limit retains budget reason"), Data.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
		TestEqual(TEXT("Exactly one continuation was scheduled"), Data.SearchSelectionRetries, 1);
		TestFalse(TEXT("Retry exhaustion leaves no pending work"), Data.bSearchSelectionPending);
	}
	else
	{
		TestEqual(TEXT("Next reachable candidate succeeds"), Status, EStateTreeRunStatus::Succeeded);
		TestEqual(TEXT("Destination is the verified alternate"), Data.Destination, Safe);
		TestTrue(TEXT("Verified destination is exposed"), Data.bHasDestination);
		if (bPoints) TestEqual(TEXT("Only verified point is committed"), Enemy->GetSearchEpisode().Points.Num(), 1);
	}
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

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyChaseIntegrationTest,
	"Outlier.Enemy.SelfDestruct.Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyChaseIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("ChaseSuccess"), TEXT("LiveQuery"), TEXT("SightLost"), TEXT("TargetChanged"), TEXT("TargetMoved"),
		TEXT("Empty"), TEXT("ForeignCandidate"), TEXT("BudgetExceeded"), TEXT("ChargeReady"), TEXT("ChargeBlocked"), TEXT("ChargeHeight"), TEXT("ChargeSightLost")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyChaseIntegrationTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>(
		ASelfDestructDrone::StaticClass(), FTransform(FVector(0, 0, 150)), Spawn);
	if (!TestNotNull(TEXT("Self destruct drone"), Drone)) return false;
	Drone->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.SearchRoomTag);
	Drone->EnterCombatFromRoom(FVector(1000, 0, 150), false);
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Target"), Target)) return false;
	UBoxComponent* TargetRoot = NewObject<UBoxComponent>(Target);
	Target->SetRootComponent(TargetRoot);
	TargetRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TargetRoot->RegisterComponent();
	Target->SetActorLocation(FVector(1000, 0, 150));
	Controller->Possess(Drone);
	// 감지 입력만 고정한다. 후보 생성/결과 적용/캡슐 Sweep/A*는 실제 게임 함수를 실행한다.
	Controller->LastVisibleTarget = Target;
	Drone->SetPlayerCurrentlyVisible(true);
	if (Parameters.StartsWith(TEXT("Charge")))
	{
		Target->SetActorLocation(FVector(400, 0, 150));
		EEnemyChargeEligibility Expected = EEnemyChargeEligibility::Ready;
		if (Parameters == TEXT("ChargeBlocked"))
		{
			Fixture.SpawnBlockingBox(FVector(200, 0, 150), FVector(20, 200, 200));
			Expected = EEnemyChargeEligibility::BlockedPath;
		}
		else if (Parameters == TEXT("ChargeHeight"))
		{
			Target->SetActorLocation(FVector(400, 0, 550));
			Expected = EEnemyChargeEligibility::OutOfRange;
		}
		else if (Parameters == TEXT("ChargeSightLost"))
		{
			Drone->SetPlayerCurrentlyVisible(false);
			Expected = EEnemyChargeEligibility::NoVisibleTarget;
		}
		TestEqual(TEXT("Authoritative charge entry result"), EnemyCharge::Assess(Drone, Controller, Target, 500, 5), Expected);
		return true;
	}
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.Enemy = Drone;
	Data.bValidateChasePath = true;
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	Data.MaxSearchMilliseconds = 1000;
	EnemySearchTesting::CaptureQuery(Data);
	if (Parameters == TEXT("LiveQuery"))
	{
		UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
		if (!TestNotNull(TEXT("Actual EQS manager"), Manager)) return false;
		UEnvQuery* Query = NewObject<UEnvQuery>();
		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
		Query->GetOptionsMutable().Add(Option);
		Option->Generator = NewObject<UEnemyEnvQueryGenerator_Chase>(Option);
		Option->Tests.Add(NewObject<UEnemyEnvQueryTest_FlightDestination>(Option));
		FEnvQueryRequest Request(Query, Drone);
		EnemyChase::SetQuerySnapshot(Request, Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane);
		const TSharedPtr<FEnvQueryResult> Result = Manager->RunInstantQuery(Request, EEnvQueryRunMode::AllMatching);
		if (!TestTrue(TEXT("Live chase query produces candidates"), Result.IsValid() && Result->IsSuccessful() && !Result->Items.IsEmpty())) return false;
		EnemySearchTesting::CompleteQuery(Data, *Result);
		TestEqual(TEXT("Real generator and destination filter feed bounded A star"), Data.QueryResult, EEnemyEnvQueryResult::Success);
		return true;
	}
	TArray<FVector> Candidates;
	EnemyChase::BuildCandidates(Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane, Candidates);
	if (!TestTrue(TEXT("Chase candidates exist"), !Candidates.IsEmpty())) return false;
	EEnemyEnvQueryResult Expected = EEnemyEnvQueryResult::Stale;
	if (Parameters == TEXT("ChaseSuccess")) Expected = EEnemyEnvQueryResult::Success;
	else if (Parameters == TEXT("SightLost")) Drone->SetPlayerCurrentlyVisible(false);
	else if (Parameters == TEXT("TargetChanged")) Controller->LastVisibleTarget.Reset();
	else if (Parameters == TEXT("TargetMoved")) Target->SetActorLocation(FVector(1250, 0, 150));
	else if (Parameters == TEXT("Empty"))
	{
		Candidates.Reset();
		Expected = EEnemyEnvQueryResult::NoResults;
	}
	else if (Parameters == TEXT("ForeignCandidate"))
	{
		Candidates = {FVector(-800, 0, 150)};
		Expected = EEnemyEnvQueryResult::NoReachableDestination;
	}
	else if (Parameters == TEXT("BudgetExceeded"))
	{
		Data.MaxCollisionChecks = 1;
		Expected = EEnemyEnvQueryResult::PathBudgetExceeded;
	}
	else { AddError(TEXT("Unknown chase scenario")); return false; }
	EnemySearchTesting::CompleteQuery(Data, MakePointResult(Drone, Candidates));
	TestEqual(TEXT("Chase completion classification"), Data.QueryResult, Expected);
	TestEqual(TEXT("Only current reachable candidates expose a destination"), Data.bHasDestination, Expected == EEnemyEnvQueryResult::Success);
	TestEqual(TEXT("Query failure does not end combat"), Drone->GetCombatState(), EEnemyCombatState::Combat);
	TestFalse(TEXT("Chase failure cannot force search LocalGuard"), Drone->GetSearchEpisode().bLocalGuard);
	if (Expected == EEnemyEnvQueryResult::Success)
	{
		TestTrue(TEXT("Preferred lane order survives EQS ties"), Data.Destination.Equals(Candidates[0], 1));
		TestEqual(TEXT("Actual A star accepted candidate"), Data.SelectionPathStatus, EFlightPathStatus::Success);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchWorldLifecycleTest,
	"Outlier.Enemy.Search.Integration.WorldLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchWorldLifecycleTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("OpenRoom"), TEXT("Detour"), TEXT("PointDetour"), TEXT("OneCandidate"), TEXT("NoCandidates"),
		TEXT("NewHint"), TEXT("SightRegained")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
		// 기존 Gun 테스트 이름은 유지하고 같은 실제 월드 시나리오를 Melee에도 적용한다.
		const FString MeleeScenario = FString(TEXT("Melee.")) + Scenario;
		Names.Add(MeleeScenario);
		Commands.Add(MeleeScenario);
	}
}

bool FEnemySearchWorldLifecycleTest::RunTest(const FString& Parameters)
{
	const bool bMelee = Parameters.StartsWith(TEXT("Melee."));
	const FString Scenario = bMelee ? Parameters.RightChop(6) : Parameters;
	const bool bPointDetour = Scenario == TEXT("PointDetour");
	const bool bDetour = Scenario == TEXT("Detour") || bPointDetour;
	// 우회 실패는 후보 필터 탈락과 A* 예산 초과를 구분할 수 있도록 실제 질의 로그를 남긴다.
	// 테스트가 끝나면 기존 설정을 복원해 다른 테스트나 에디터의 로그 설정을 바꾸지 않는다.
	IConsoleVariable* QueryDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.EQSDebug"));
	const bool bEnableQueryDebug = bDetour && QueryDebug;
	const int32 PreviousQueryDebug = QueryDebug ? QueryDebug->GetInt() : 0;
	const EConsoleVariableFlags QueryDebugPriority = QueryDebug
		? static_cast<EConsoleVariableFlags>(QueryDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bEnableQueryDebug) QueryDebug->Set(2, QueryDebugPriority);
	IConsoleVariable* FlightDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.FlightDebug"));
	const int32 PreviousFlightDebug = FlightDebug ? FlightDebug->GetInt() : 0;
	const EConsoleVariableFlags FlightDebugPriority = FlightDebug
		? static_cast<EConsoleVariableFlags>(FlightDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bDetour && FlightDebug) FlightDebug->Set(2, FlightDebugPriority);
	IConsoleVariable* SegmentDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Flight.SegmentDebug"));
	const int32 PreviousSegmentDebug = SegmentDebug ? SegmentDebug->GetInt() : 0;
	const EConsoleVariableFlags SegmentPriority = SegmentDebug
		? static_cast<EConsoleVariableFlags>(SegmentDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bDetour && SegmentDebug) SegmentDebug->Set(1, SegmentPriority);
	IConsoleVariable* EndpointDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Flight.EndpointDebug"));
	const int32 PreviousEndpointDebug = EndpointDebug ? EndpointDebug->GetInt() : 0;
	const EConsoleVariableFlags EndpointPriority = EndpointDebug
		? static_cast<EConsoleVariableFlags>(EndpointDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bDetour && EndpointDebug) EndpointDebug->Set(1, EndpointPriority);
	ON_SCOPE_EXIT
	{
		if (bEnableQueryDebug) QueryDebug->Set(PreviousQueryDebug, QueryDebugPriority);
		if (bDetour && FlightDebug) FlightDebug->Set(PreviousFlightDebug, FlightDebugPriority);
		if (bDetour && SegmentDebug) SegmentDebug->Set(PreviousSegmentDebug, SegmentPriority);
		if (bDetour && EndpointDebug) EndpointDebug->Set(PreviousEndpointDebug, EndpointPriority);
	};
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchIntegrationWorld Fixture(true);
	if (!TestTrue(TEXT("Test world begins play"), Fixture.World && Fixture.World->HasBegunPlay())) return false;
	const bool bNoCandidates = Scenario == TEXT("NoCandidates");
	const bool bOneCandidate = Scenario == TEXT("OneCandidate");
	const FVector RoomExtent = bPointDetour ? FVector(550, 350, 110) : bOneCandidate ? FVector(350, 100, 110)
		: bNoCandidates ? FVector(200, 200, 700) : FVector(1600, 1600, 700);
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom(RoomExtent))) return false;
	// 폭과 높이를 제한해 +X 5m 후보만 드론 크기까지 포함해 들어갈 수 있게 한다.
	// 다른 링/방향/높이 후보는 실제 Room/캡슐 검사에서 탈락한다.
	if (bOneCandidate) Fixture.Room->SetActorLocation(FVector(300, 0, 200));
	// +X 방향 후보만 방 안에 남기되, 벽 좌우에는 실제 우회할 수 있는 폭을 확보한다.
	if (bPointDetour) Fixture.Room->SetActorLocation(FVector(500, 0, 200));
	if (!TestNotNull(TEXT("Floor exists"), Fixture.SpawnBlockingBox(
		FVector(0, 0, -150), FVector(1800, 1800, 40)))) return false;
	if (Scenario == TEXT("Detour") && !TestNotNull(TEXT("Detour wall exists"),
		Fixture.SpawnBlockingBox(FVector(0, 0, 200), FVector(40, 180, 300)))) return false;
	if (bPointDetour && !TestNotNull(TEXT("Point detour wall exists"),
		Fixture.SpawnBlockingBox(FVector(250, 0, 200), FVector(40, 80, 300)))) return false;
	// 6.5m 후보는 두 번째 벽 안에 들어가 제외된다. 남은 5m / 8m 지점 사이도 다시 우회해야 한다.
	if (bPointDetour && !TestNotNull(TEXT("Second point detour wall exists"),
		Fixture.SpawnBlockingBox(FVector(650, 0, 200), FVector(40, 80, 300)))) return false;

	// 실제 BP의 공용 Tree / Linked Battle / EQS 설정을 사용하고 Task를 수동 완료하지 않는다.
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, bMelee
		? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
		: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Enemy BP loads"), EnemyClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Start = bOneCandidate || bPointDetour ? FVector(100, 0, 200)
		: bNoCandidates ? FVector(0, 0, 200) : FVector(-600, 0, 200);
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(Start), Spawn);
	if (!TestNotNull(TEXT("Enemy spawns"), Enemy)) return false;
	// Melee 테스트가 잘못 연결된 Gun Tree를 실행하고 통과하지 않도록 실제 Linked Asset도 확인한다.
	const FStructProperty* BattleProperty = FindFProperty<FStructProperty>(EnemyClass, TEXT("BattleStateTreeReference"));
	if (!TestNotNull(TEXT("Battle tree reference exists"), BattleProperty)) return false;
	const FStateTreeReference* BattleReference = BattleProperty->ContainerPtrToValuePtr<FStateTreeReference>(Enemy);
	const FString ExpectedBattlePath = bMelee
		? TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneBattle_Melee.ST_VECDroneBattle_Melee")
		: TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneBattle_Gun.ST_VECDroneBattle_Gun");
	if (!TestTrue(TEXT("BP uses the expected battle asset"), BattleReference && BattleReference->GetStateTree()
		&& BattleReference->GetStateTree()->GetPathName() == ExpectedBattlePath)) return false;
	UStateTreeComponent* Tree = Enemy->GetStateTreeComponent();
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	if (!TestNotNull(TEXT("Actual StateTree component exists"), Tree)
		|| !TestNotNull(TEXT("AI controller exists"), Controller)) return false;
	if (!TestTrue(TEXT("Enemy has a nonzero flight speed"), Enemy->GetRuntimeStat().MoveSpeed > 0)) return false;

	// 감지 입력만 차단한다. 플레이어나 관측자가 없으므로 공유 위치도 자연 갱신되지 않는다.
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.SearchRoomTag);
	Enemy->SetPlayerCurrentlyVisible(false);
	const FVector Center = bOneCandidate || bPointDetour ? FVector(0, 0, 200)
		: bNoCandidates ? FVector(5000, 0, 200) : FVector(600, 0, 200);
	Enemy->EnterCombatFromRoom(Center, false);
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->UpdateLastKnownPlayerLocation(Center);
	Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	if (bPointDetour)
	{
		// 상태 전이를 보기 전에 양방향 격자 경로부터 증명한다. 실패하면 테스트 공간 문제와 실행 문제를 구분한다.
		FFlightPathRequest Request;
		Request.Start = FVector(800, 0, 200);
		Request.Goal = FVector(500, 0, 200);
		Request.SearchBounds = FBox(Request.Start.ComponentMin(Request.Goal), Request.Start.ComponentMax(Request.Goal)).ExpandBy(800);
		Request.bRequireRoom = true;
		Request.Room = Fixture.Room;
		const FFlightPathResult Path = FlightPathfinding::FindPath(*Fixture.World, *Enemy->GetCapsuleComponent(), Request);
		AddInfo(FString::Printf(TEXT("[SearchGeometry] Radius=%.1f HalfHeight=%.1f Cell=%.1f ReversePath=%s Checks=%d"),
			Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(), Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
			Request.CellSize, *UEnum::GetValueAsString(Path.Status), Path.CollisionChecks));
		if (!TestEqual(TEXT("Point fixture has a reverse grid route before tree execution"), Path.Status, EFlightPathStatus::Success)) return false;
	}
	Tree->StopLogic(TEXT("Controlled search lifecycle setup"));
	Tree->StartLogic();

	// 실제 월드가 Timer / EQS callback / StateTree / Movement를 순서대로 진행한다.
	// 프레임마다 Task를 직접 호출하거나 위치를 순간 이동시키면 연결 오류를 놓칠 수 있다.
	constexpr float Step = 1.0f / 30.0f;
	constexpr int32 MaxFrames = 1800;
	bool bSawSearch = false;
	bool bSawAnchorMovement = false;
	bool bSawPointLookAround = false;
	bool bSawFirstPointDetour = false;
	bool bSawSecondPointDetour = false;
	bool bReachedGuard = false;
	FString LastStates;
	uint64 EpisodeSerial = 0;
	uint64 SharedRevision = Enemy->GetSharedTargetRevision();
	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		Fixture.Tick(Step);
		const TArray<FName> States = Tree->GetActiveStateNames();
		const FEnemySearchEpisode& Episode = Enemy->GetSearchEpisode();
		bSawSearch |= Episode.bActive;
		bSawAnchorMovement |= States.Contains(FName(TEXT("MoveAnchor")));
		bSawPointLookAround |= States.Contains(FName(TEXT("PointLookAround")));
		// 지점 이동 중 벽 너비 밖으로 실제 움직여야 한다. 선정 성공이나 제자리 상태 전이만으로 통과하지 않는다.
		if (States.Contains(FName(TEXT("MovePoint"))) && FMath::Abs(Enemy->GetActorLocation().Y) > 80.0)
		{
			bSawFirstPointDetour |= Episode.PointIndex == 0;
			bSawSecondPointDetour |= Episode.PointIndex == 1;
		}
		const FString StateNames = FString::JoinBy(States, TEXT(" > "),
			[](FName Name) { return Name.ToString(); });
		if (StateNames != LastStates)
		{
			AddInfo(FString::Printf(TEXT("[SearchWorld] Scenario=%s Time=%.2f States=%s Location=%s Episode=%llu Selected=%d PointIndex=%d Checked=%d Guard=%d"),
				*Parameters, Fixture.World->GetTimeSeconds(), *StateNames, *Enemy->GetActorLocation().ToCompactString(),
				Episode.Serial, Episode.Points.Num(), Episode.PointIndex, Episode.CheckedPoints.Num(), Episode.bLocalGuard));
			LastStates = StateNames;
		}
		if (Episode.bLocalGuard && States.Contains(FName(TEXT("GuardWait"))))
		{
			bReachedGuard = true;
			EpisodeSerial = Episode.Serial;
			break;
		}
		if (Tree->GetStateTreeRunStatus() != EStateTreeRunStatus::Running || Enemy->IsDead()) break;
	}
	TestTrue(TEXT("Actual tree starts a search episode"), bSawSearch);
	if (!TestTrue(*FString::Printf(TEXT("Search reaches GuardWait within 60 simulated seconds: States=%s Location=%s Checked=%d"),
		*LastStates, *Enemy->GetActorLocation().ToCompactString(), Enemy->GetSearchEpisode().CheckedPoints.Num()), bReachedGuard)) return false;
	TestEqual(TEXT("No spontaneous shared updates"), Enemy->GetSharedTargetRevision(), SharedRevision);
	if (bNoCandidates)
	{
		TestEqual(TEXT("Unreachable center checks no points"), Enemy->GetSearchEpisode().CheckedPoints.Num(), 0);
		TestTrue(TEXT("No-candidate guard stays at spawn"), Enemy->GetActorLocation().Equals(Start, 10));
	}
	else
	{
		if (!bOneCandidate) TestTrue(TEXT("Anchor Fly state actually executes"), bSawAnchorMovement);
		TestTrue(TEXT("Point LookAround state actually executes"), bSawPointLookAround);
		TestEqual(TEXT("All expected points finish before guard"), Enemy->GetSearchEpisode().CheckedPoints.Num(), bOneCandidate ? 1 : 2);
		TestFalse(TEXT("Movement is real, not just a successful query"), Enemy->GetActorLocation().Equals(Start, 50));
		if (bPointDetour)
		{
			TestTrue(TEXT("First point Fly actually moves around the wall"), bSawFirstPointDetour);
			TestTrue(TEXT("Second point Fly actually moves around the wall"), bSawSecondPointDetour);
		}
	}

	// 한 회차가 끝난 후에도 월드를 진행시켜 같은 정보로 수색을 다시 시작하지 않는지 확인한다.
	const FVector GuardLocation = Enemy->GetActorLocation();
	for (int32 Frame = 0; Frame < 150; ++Frame) Fixture.Tick(Step);
	TestEqual(TEXT("No new information keeps the same episode"), Enemy->GetSearchEpisode().Serial, EpisodeSerial);
	TestTrue(TEXT("Guard remains active"), Enemy->GetSearchEpisode().bLocalGuard);
	TestTrue(TEXT("Guard does not move"), Enemy->GetActorLocation().Equals(GuardLocation, 10));

	if (Scenario == TEXT("NewHint"))
	{
		const FVector Hint(0, 500, 200);
		Enemy->ApplySharedTargetContact(Hint, true, SharedRevision + 1);
		for (int32 Frame = 0; Frame < 60 && Enemy->GetSearchEpisode().Serial == EpisodeSerial; ++Frame)
			Fixture.Tick(Step);
		TestTrue(TEXT("Fresh hint restarts the actual tree"), Enemy->GetSearchEpisode().Serial > EpisodeSerial);
		TestTrue(TEXT("Restart uses the new center"), Enemy->GetSearchEpisode().Center.Equals(Hint, 1));
		const uint64 RestartSerial = Enemy->GetSearchEpisode().Serial;
		Enemy->ApplySharedTargetContact(Hint, true, SharedRevision + 1);
		for (int32 Frame = 0; Frame < 30; ++Frame) Fixture.Tick(Step);
		TestEqual(TEXT("Re-delivery of the same revision does not restart"), Enemy->GetSearchEpisode().Serial, RestartSerial);
	}
	else if (Scenario == TEXT("SightRegained"))
	{
		// 네이티브 Shooter는 추상 클래스다. 실제 플레이어 BP로 시야 재발견 입력을 구성한다.
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!TestNotNull(TEXT("Visible target BP loads"), ShooterClass)) return false;
		AShooterCharacter* Target = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass,
			FTransform(Enemy->GetActorLocation() + FVector(1500, 0, 0)), Spawn);
		if (!TestNotNull(TEXT("Visible target fixture exists"), Target)) return false;
		Target->SetActorEnableCollision(false);
		// 시야 플래그만 바꾸면 Sync의 TargetActor가 비어 있다. 실제 Sight 캐시에 자극을 전달한다.
		Controller->EnemyPerceptionComponent->RegisterStimulus(Target, FAIStimulus(
			*GetDefault<UAISense_Sight>(), 1.0f, Target->GetActorLocation(), Enemy->GetActorLocation()));
		Controller->EnemyPerceptionComponent->ProcessStimuli();
		TestEqual(TEXT("Sight cache contains the real target"), Controller->GetPreferredVisibleTarget(), static_cast<AActor*>(Target));
		// 감지 콜백이 수색 회차를 먼저 종료하므로 bActive로 Tick을 중단하면 전이를 확인할 수 없다.
		// 실제 Tree가 다음 프레임에서 전투 상태를 선택할 때까지 기다린다.
		for (int32 Frame = 0; Frame < 60
			&& !Tree->GetActiveStateNames().Contains(FName(TEXT("DirectCombat"))); ++Frame)
			Fixture.Tick(Step);
		AddInfo(FString::Printf(TEXT("[SearchWorld] SightRegained FinalStates=%s SearchActive=%d"),
			*FString::JoinBy(Tree->GetActiveStateNames(), TEXT(" > "),
				[](FName Name) { return Name.ToString(); }), Enemy->GetSearchEpisode().bActive));
		TestFalse(TEXT("Sight regained exits search"), Enemy->GetSearchEpisode().bActive);
		TestTrue(TEXT("Sight regained selects DirectCombat"), Tree->GetActiveStateNames().Contains(FName(TEXT("DirectCombat"))));
	}
	Tree->StopLogic(TEXT("Search lifecycle test complete"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchBudgetIsolationTest,
	"Outlier.Enemy.Search.Integration.BudgetIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchBudgetIsolationTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	FEnemySearchIntegrationWorld Other;
	FlightPathfinding::BeginTestBudget(*Fixture.World, 4096);
	ON_SCOPE_EXIT { FlightPathfinding::EndTestBudget(); };
	TestEqual(TEXT("Test world replaces the asset budget"), FlightPathfinding::GetTestBudget(*Fixture.World, 8192), 4096);
	TestEqual(TEXT("Other worlds preserve asset settings"), FlightPathfinding::GetTestBudget(*Other.World, 8192), 8192);
	FlightPathfinding::RecordTestQuery(Fixture.World, false);
	FlightPathfinding::RecordTestQuery(Fixture.World, true);
	FlightPathfinding::RecordTestQuery(Other.World, false);
	FlightPathfinding::RecordTestQuery(Other.World, true);
	FlightPathfinding::RecordTestQuery(nullptr, true);
	TestEqual(TEXT("Only the measured world's EQS starts count"), FlightPathfinding::GetTestCost().QueriesStarted, int64(1));
	TestEqual(TEXT("Only the measured world's EQS completions count"), FlightPathfinding::GetTestCost().QueriesFinished, int64(1));
	FlightPathfinding::EndTestBudget();
	FlightPathfinding::RecordTestQuery(Fixture.World, false);
	TestEqual(TEXT("No EQS counting after cleanup"), FlightPathfinding::GetTestCost().QueriesStarted, int64(1));
	TestEqual(TEXT("Cleanup restores the normal asset budget"), FlightPathfinding::GetTestBudget(*Fixture.World, 2048), 2048);
	FlightPathfinding::BeginTestBudget(*Fixture.World, 4096);
	TestEqual(TEXT("A new measurement resets EQS counters"), FlightPathfinding::GetTestCost().QueriesStarted, int64(0));
	return true;
}

namespace
{
	TAutoConsoleVariable<int32> CVarSearchLoadEnabled(TEXT("outlier.Enemy.SearchLoadTest"), 0,
		TEXT("Explicitly allow high-memory search stress tests. 0: refuse, 1: allow."));

	struct FSearchLoadSetting
	{
		IConsoleVariable* Variable = nullptr;
		int32 Previous = 0;
		EConsoleVariableFlags Priority = ECVF_SetByCode;
	};

	struct FSearchLoadTick
	{
		int32 Frame = 0;
		double WorldMs = 0;
		FlightPathfinding::FTestCost Cost;
		FString StatesAfterTick;
	};

	struct FSearchLoadRun
	{
		FAutomationTestBase& Test;
		FString Name;
		int32 Count;
		bool bMelee;
		bool bWalls;
		UClass* EnemyClass = nullptr;
		TUniquePtr<FEnemySearchIntegrationWorld> Fixture;
		TArray<AEnemyBase*> Enemies;
		TArray<FSearchLoadSetting> Settings;
		TArray<double> TickTimes;
		TArray<FSearchLoadTick> SlowTicks;
		int32 Frame = 0;
		int32 Columns = 0;
		double Started = FPlatformTime::Seconds();
		double SpawnMs = 0;
		uint64 InitialMemory = FPlatformMemory::GetStats().UsedPhysical;
		uint64 PeakMemory = InitialMemory;
		bool bOwnsBudget = false;

		FSearchLoadRun(FAutomationTestBase& InTest, const FString& InName, int32 InCount, bool InMelee, bool InWalls)
			: Test(InTest), Name(InName), Count(InCount), bMelee(InMelee), bWalls(InWalls) {}

		~FSearchLoadRun()
		{
			if (bOwnsBudget) FlightPathfinding::EndTestBudget();
			// 취소/실패 때도 테스트 월드와 임시 진단 설정이 남지 않는다.
			Fixture.Reset();
			for (const FSearchLoadSetting& Setting : Settings)
				Setting.Variable->Set(Setting.Previous, Setting.Priority);
		}

		void Silence(const TCHAR* NameToDisable)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(NameToDisable))
			{
				FSearchLoadSetting& Setting = Settings.AddDefaulted_GetRef();
				Setting.Variable = Variable;
				Setting.Previous = Variable->GetInt();
				Setting.Priority = static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask);
				Variable->Set(0, Setting.Priority);
			}
		}

		bool Initialize()
		{
			for (const TCHAR* Variable : {TEXT("outlier.Enemy.EQSDebug"), TEXT("outlier.Enemy.FlightDebug"),
				TEXT("outlier.Flight.EndpointDebug"), TEXT("outlier.Flight.SegmentDebug"), TEXT("outlier.Flight.PathCostLog")}) Silence(Variable);
			EnemyClass = LoadClass<AEnemyBase>(nullptr, bMelee
				? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
				: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
			if (!Test.TestNotNull(TEXT("Stress enemy BP loads"), EnemyClass)) return false;
			Fixture = MakeUnique<FEnemySearchIntegrationWorld>(true);
			Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
			if (!Test.TestTrue(TEXT("Stress room registers"), Fixture->CreateRoom(FVector(Columns * 2400 + 2000, Columns * 2400 + 2000, 1000)))) return false;
			Enemies.Reserve(Count);
			FlightPathfinding::BeginTestBudget(*Fixture->World, 4096);
			bOwnsBudget = true;
			Test.AddInfo(FString::Printf(TEXT("[SearchLoad] Begin=%s Count=%d Checks=4096 NodesAndTime=AssetValues Layout=Separated2400cm Authority=Standalone"), *Name, Count));
			Test.TestEqual(TEXT("Test world collision budget is exactly 4096"), FlightPathfinding::GetTestBudget(*Fixture->World, 2048), 4096);
			return true;
		}

		bool SpawnBatch()
		{
			// 메모리 고갈로 에디터를 종료시키는 대신 실패로 남기고 이미 생성한 월드를 정리한다.
			if (FPlatformMemory::GetStats().AvailablePhysical < 1024ull * 1024 * 1024)
			{
				Test.AddError(TEXT("Search load aborted: less than 1 GiB available physical memory."));
				Report();
				return false;
			}
			const double BatchStarted = FPlatformTime::Seconds();
			const int32 End = FMath::Min(Enemies.Num() + 25, Count);
			while (Enemies.Num() < End)
			{
				const int32 Index = Enemies.Num();
				const FVector Start((Index % Columns) * 2400, (Index / Columns) * 2400, 200);
				FActorSpawnParameters Parameters;
				Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AEnemyBase* Enemy = Fixture->World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(Start), Parameters);
				if (!Test.TestNotNull(TEXT("Stress enemy spawns"), Enemy)) return false;
				Enemies.Add(Enemy);
				AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
				UStateTreeComponent* Tree = Enemy->GetStateTreeComponent();
				if (!Test.TestNotNull(TEXT("Stress controller exists"), Controller) || !Test.TestNotNull(TEXT("Stress tree exists"), Tree)) return false;
				Tree->StopLogic(TEXT("Load test setup"));
				Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture->SearchRoomTag);
				Enemy->EnterCombatFromRoom(Start + FVector(600, 0, 0), false);
				Controller->SetEnemyPerceptionEnabled(false);
				Enemy->SetPlayerCurrentlyVisible(false);
				Enemy->UpdateLastKnownPlayerLocation(Start + FVector(600, 0, 0));
				Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				if (bWalls && !Test.TestNotNull(TEXT("Stress obstacle exists"),
					Fixture->SpawnBlockingBox(Start + FVector(300, 0, 0), FVector(40, 180, 300)))) return false;
				Tree->StartLogic();
			}
			SpawnMs += (FPlatformTime::Seconds() - BatchStarted) * 1000;
			return true;
		}

		void RecordTick(double Ms, const FlightPathfinding::FTestCost& Before)
		{
			TickTimes.Add(Ms);
			// 비용 차이는 이 World Tick에 실행된 호출만 나타낸다. 상태 집계/로그는 측정 밖에서 수행한다.
			if (SlowTicks.Num() == 8 && Ms <= SlowTicks.Last().WorldMs) return;
			const FlightPathfinding::FTestCost After = FlightPathfinding::GetTestCost();
			FSearchLoadTick Tick;
			Tick.Frame = Frame;
			Tick.WorldMs = Ms;
			Tick.Cost.Paths = After.Paths - Before.Paths;
			Tick.Cost.Failures = After.Failures - Before.Failures;
			Tick.Cost.Budgets = After.Budgets - Before.Budgets;
			Tick.Cost.Checks = After.Checks - Before.Checks;
			Tick.Cost.Nodes = After.Nodes - Before.Nodes;
			Tick.Cost.TotalMs = After.TotalMs - Before.TotalMs;
			Tick.Cost.QueriesStarted = After.QueriesStarted - Before.QueriesStarted;
			Tick.Cost.QueriesFinished = After.QueriesFinished - Before.QueriesFinished;
			TMap<FName, int32> StateCounts;
			for (const AEnemyBase* Enemy : Enemies)
			{
				const TArray<FName> States = Enemy->GetStateTreeComponent()->GetActiveStateNames();
				++StateCounts.FindOrAdd(States.IsEmpty() ? FName(TEXT("Inactive")) : States.Last());
			}
			TArray<FString> States;
			for (const TPair<FName, int32>& Pair : StateCounts)
				States.Add(FString::Printf(TEXT("%s:%d"), *Pair.Key.ToString(), Pair.Value));
			States.Sort();
			Tick.StatesAfterTick = FString::Join(States, TEXT(","));
			SlowTicks.Add(MoveTemp(Tick));
			SlowTicks.Sort([](const FSearchLoadTick& A, const FSearchLoadTick& B) { return A.WorldMs > B.WorldMs; });
			if (SlowTicks.Num() > 8) SlowTicks.Pop();
		}

		void Report()
		{
			for (const FSearchLoadTick& Tick : SlowTicks)
			{
				// FlightMs는 경로/공간/구간 검사 전체다. 나머지는 EQS 자체, 이동/물리 등을 포함하며 단일 원인으로 해석하지 않는다.
				Test.AddInfo(FString::Printf(TEXT("[SearchLoad] SlowTick=%s Frame=%d SimSeconds=%.3f WorldMs=%.3f FlightMs=%.3f OtherWorldMs=%.3f Paths=%lld Failed=%lld BudgetExceeded=%lld Checks=%lld Nodes=%lld EQSStarted=%lld EQSFinished=%lld StatesAfterTick=%s"),
					*Name, Tick.Frame, (Tick.Frame + 1) / 30.0, Tick.WorldMs, Tick.Cost.TotalMs,
					FMath::Max(0.0, Tick.WorldMs - Tick.Cost.TotalMs), Tick.Cost.Paths, Tick.Cost.Failures,
					Tick.Cost.Budgets, Tick.Cost.Checks, Tick.Cost.Nodes, Tick.Cost.QueriesStarted,
					Tick.Cost.QueriesFinished, *Tick.StatesAfterTick));
			}
			TickTimes.Sort();
			double Total = 0;
			for (double Time : TickTimes) Total += Time;
			int32 Guards = 0;
			int32 Checked = 0;
			for (const AEnemyBase* Enemy : Enemies)
			{
				Guards += Enemy->GetSearchEpisode().bLocalGuard;
				Checked += Enemy->GetSearchEpisode().CheckedPoints.Num();
			}
			const FlightPathfinding::FTestCost Cost = FlightPathfinding::GetTestCost();
			Test.AddInfo(FString::Printf(TEXT("[SearchLoad] End=%s Spawned=%d SpawnMs=%.1f MeasuredFrames=%d TickAvgMs=%.3f TickP95Ms=%.3f TickMaxMs=%.3f ProcessPeakMiB=%.1f ProcessGrowthMiB=%.1f Paths=%lld Failed=%lld BudgetExceeded=%lld Checks=%lld Nodes=%lld CollisionTotalMs=%.3f PeakCallMs=%.3f Guards=%d CheckedPoints=%d"),
				*Name, Enemies.Num(), SpawnMs, TickTimes.Num(), Total / FMath::Max(TickTimes.Num(), 1),
				TickTimes.IsEmpty() ? 0 : TickTimes[FMath::Min(FMath::CeilToInt(TickTimes.Num() * 0.95) - 1, TickTimes.Num() - 1)],
				TickTimes.IsEmpty() ? 0 : TickTimes.Last(), PeakMemory / 1048576.0,
				(static_cast<double>(PeakMemory) - InitialMemory) / 1048576.0,
				Cost.Paths, Cost.Failures, Cost.Budgets, Cost.Checks, Cost.Nodes, Cost.TotalMs, Cost.PeakMs, Guards, Checked));
			Test.TestEqual(TEXT("Full requested population exists"), Enemies.Num(), Count);
			Test.TestTrue(TEXT("Real EQS/Fly path calls were measured"), Cost.Paths > 0);
		}
	};

	class FSearchLoadCommand : public IAutomationLatentCommand
	{
		TSharedPtr<FSearchLoadRun> Run;
	public:
		explicit FSearchLoadCommand(TSharedPtr<FSearchLoadRun> InRun) : Run(MoveTemp(InRun)) {}
		virtual bool Update() override
		{
			// 5000마리 생성도 여러 에디터 프레임으로 나눈다. 측정은 생성 완료 후 실제 월드 Tick만 포함한다.
			if (!Run->Fixture && !Run->Initialize()) return true;
			if (FPlatformTime::Seconds() - Run->Started > 300)
			{
				Run->Test.AddError(TEXT("Search load exceeded 300 wall seconds; aborted, not a passing measurement."));
				Run->Report();
				return true;
			}
			if (Run->Enemies.Num() < Run->Count)
			{
				if (!Run->SpawnBatch()) return true;
				Run->PeakMemory = FMath::Max(Run->PeakMemory, FPlatformMemory::GetStats().UsedPhysical);
				return false;
			}
			TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
			const FlightPathfinding::FTestCost Before = FlightPathfinding::GetTestCost();
			const double Started = FPlatformTime::Seconds();
			Run->Fixture->Tick(1.0f / 30.0f);
			const double Ms = (FPlatformTime::Seconds() - Started) * 1000;
			// 시작 폭주도 숨기지 않도록 모든 Tick을 기록한다. 약 20초의 수색과 Guard 구간을 포함한다.
			Run->RecordTick(Ms, Before);
			Run->PeakMemory = FMath::Max(Run->PeakMemory, FPlatformMemory::GetStats().UsedPhysical);
			if (Run->Frame % 120 == 0)
				Run->Test.AddInfo(FString::Printf(TEXT("[SearchLoad] Progress=%s Frame=%d/600 TickMs=%.3f ProcessMiB=%.1f"),
					*Run->Name, Run->Frame, Ms, Run->PeakMemory / 1048576.0));
			if (Ms > 5000)
			{
				Run->Test.AddError(TEXT("One world Tick exceeded 5000ms; aborted. Reduce population or profile this case alone."));
				Run->Report();
				return true;
			}
			if (++Run->Frame < 600) return false;
			Run->Report();
			return true;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchLoadTest, "Outlier.Enemy.Load.Search",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

void FEnemySearchLoadTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : {TEXT("Gun"), TEXT("Melee")})
		for (const TCHAR* Layout : {TEXT("Open"), TEXT("Walls")})
			for (int32 Count : {500, 1500, 2500, 5000})
			{
				const FString Name = FString::Printf(TEXT("%s.%s.%d"), Type, Layout, Count);
				Names.Add(Name);
				Commands.Add(Name);
			}
}

bool FEnemySearchLoadTest::RunTest(const FString& Parameters)
{
	if (CVarSearchLoadEnabled.GetValueOnGameThread() != 1)
	{
		AddError(TEXT("Explicit opt-in required: outlier.Enemy.SearchLoadTest 1. Run one population at a time, starting with 500."));
		return false;
	}
	TArray<FString> Parts;
	Parameters.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() != 3) return false;
	const int32 Count = FCString::Atoi(*Parts[2]);
	if ((Parts[0] != TEXT("Gun") && Parts[0] != TEXT("Melee"))
		|| (Parts[1] != TEXT("Open") && Parts[1] != TEXT("Walls"))
		|| (Count != 500 && Count != 1500 && Count != 2500 && Count != 5000)) return false;
	TSharedPtr<FSearchLoadRun> Run = MakeShared<FSearchLoadRun>(*this, Parameters, Count,
		Parts[0] == TEXT("Melee"), Parts[1] == TEXT("Walls"));
	ADD_LATENT_AUTOMATION_COMMAND(FSearchLoadCommand(Run));
	return true;
}

#endif
