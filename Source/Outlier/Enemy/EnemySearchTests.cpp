#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/SelfDestructDrone.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomCombatSubsystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/Anim/AnimNotify_MakeNoise.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(NoiseCompletionRoomTag, "Room.NoiseCompletionTest01.1.1");

// 기존 Room/Pool 테스트와 동일한 서버 권한 World를 사용하고 실패 경로에서도 정리한다.
struct FEnemySearchTestWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;

	explicit FEnemySearchTestWorld(bool bCreateAISystem = false)
	{
		const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!World) return;
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		World->AddToRoot();
		Context.SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		// 실제 감지 파이프라인을 사용하는 테스트만 AI System을 생성한다.
		// Game 타입 임시 월드에는 기본 생성되지 않으므로 Controller 등록보다 먼저 준비한다.
		if (bCreateAISystem) World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
	}

	~FEnemySearchTestWorld()
	{
		if (!World) return;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	AEnemyBase* SpawnEnemy(FGameplayTag RoomTag, const FVector& Location = FVector::ZeroVector,
		bool bStartInCombat = true) const
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(Location), Parameters);
		if (Enemy)
		{
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(RoomTag);
			if (bStartInCombat) Enemy->EnterCombatFromRoom(FVector::ZeroVector, false);
		}
		return Enemy;
	}

	void AdvanceTimers(float Seconds) const
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	}

	AShooterCharacter* SpawnPlayer(const FVector& Location = FVector::ZeroVector) const
	{
		// ShooterCharacter는 추상 클래스다. 기존 통합 테스트와 같은 실제 BP를 사용한다.
		UClass* PlayerClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!PlayerClass) return nullptr;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AShooterCharacter>(PlayerClass, FTransform(Location), Spawn);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatAimPointTest,
	"Outlier.Enemy.Combat.AimPoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatAimPointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag(), FVector::ZeroVector, false);
	ACharacter* Target = Fixture.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Enemy exists"), Enemy)
		|| !TestNotNull(TEXT("Character target exists"), Target)) return false;

	// 시점과 몸체 중심을 분리해 기존 시점 조준으로 되돌아가는 회귀를 검출한다.
	Target->BaseEyeHeight = 80.0f;
	Target->GetCapsuleComponent()->SetCapsuleSize(20.0f, 30.0f);
	Target->SetActorLocation(FVector(500.0f, 200.0f, 300.0f));
	const FVector InitialCenter = Target->GetCapsuleComponent()->Bounds.Origin;
	TestFalse(TEXT("View location differs from the small capsule center"),
		Target->GetPawnViewLocation().Equals(InitialCenter));
	TestTrue(TEXT("Character aim uses capsule center instead of view height"),
		Enemy->GetCombatAimPoint(Target).Equals(InitialCenter));

	Target->SetActorLocation(FVector(750.0f, -100.0f, 450.0f));
	const FVector MovedCenter = Target->GetCapsuleComponent()->Bounds.Origin;
	TestFalse(TEXT("Target capsule moved"), MovedCenter.Equals(InitialCenter));
	TestTrue(TEXT("Tracking query follows the current capsule center"),
		Enemy->GetCombatAimPoint(Target).Equals(MovedCenter));

	APawn* PawnTarget = Fixture.World->SpawnActor<APawn>();
	AActor* ActorTarget = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Non-character pawn exists"), PawnTarget)
		|| !TestNotNull(TEXT("Non-pawn actor exists"), ActorTarget)) return false;
	PawnTarget->BaseEyeHeight = 47.0f;
	TestTrue(TEXT("Pawn without a character capsule keeps its view-location fallback"),
		Enemy->GetCombatAimPoint(PawnTarget).Equals(PawnTarget->GetPawnViewLocation()));
	TestTrue(TEXT("Non-pawn actor keeps its actor-location fallback"),
		Enemy->GetCombatAimPoint(ActorTarget).Equals(ActorTarget->GetActorLocation()));
	TestTrue(TEXT("Null target keeps the zero-vector fallback"),
		Enemy->GetCombatAimPoint(nullptr).Equals(FVector::ZeroVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchEpisodeTest,
	"Outlier.Enemy.Search.Episode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchEpisodeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Enemy = Fixture.SpawnEnemy(Room);
	if (!TestNotNull(TEXT("Enemy exists"), Enemy)) return false;
	TestTrue(TEXT("Zero world coordinate is valid remembered information"), Enemy->BeginSearchEpisode());
	Enemy->SetSearchPoints({});
	TestTrue(TEXT("Zero selected points enters LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);

	const FVector Hint(300, 200, 350);
	Enemy->ApplySharedTargetContact(Hint, true, 10);
	TestTrue(TEXT("New shared revision is fresh"), Enemy->HasFreshSharedTargetLocation());
	TestTrue(TEXT("New hint begins an episode"), Enemy->BeginSearchEpisode());
	TestTrue(TEXT("Episode pins shared center"), Enemy->GetSearchEpisode().Center.Equals(Hint));
	Enemy->CompleteSearchAnchor();
	TestFalse(TEXT("Anchor arrival consumes only its hint"), Enemy->HasFreshSharedTargetLocation());
	Enemy->ApplySharedTargetContact(Hint, true, 10);
	TestFalse(TEXT("Repeated revision cannot restart pursuit"), Enemy->HasFreshSharedTargetLocation());
	Enemy->ApplySharedTargetContact(Hint, true, 11);
	TestTrue(TEXT("Later real sight at same coordinate is new information"), Enemy->HasFreshSharedTargetLocation());
	Enemy->CompleteSearchAnchor();
	TestTrue(TEXT("Old episode cannot consume a newer hint"), Enemy->HasFreshSharedTargetLocation());
	Enemy->BeginSearchEpisode();
	Enemy->CompleteSearchAnchor();
	const FVector First(800, 200, 350);
	const FVector Second(300, 800, 350);
	TestFalse(TEXT("More than four search points are rejected"), Enemy->SetSearchPoints({First, Second, Hint, -First, -Second}));
	TestTrue(TEXT("Two available points are stored once before movement"), Enemy->SetSearchPoints({First, Second}));
	TestFalse(TEXT("Selected points cannot be replenished in the episode"), Enemy->SetSearchPoints({Hint}));
	TestTrue(TEXT("First LookAround completes one point"), Enemy->CompleteSearchPoint());
	TestFalse(TEXT("Second point remains before LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);
	TestTrue(TEXT("Second LookAround completes the episode"), Enemy->CompleteSearchPoint());
	TestTrue(TEXT("Two confirmations enter LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);
	TestFalse(TEXT("Completed episode cannot confirm a third point"), Enemy->CompleteSearchPoint());
	Enemy->ApplySharedTargetContact(Hint + FVector(1000.0, 0.0, 0.0), true, 12);
	Enemy->BeginSearchEpisode();
	Enemy->SetSearchPoints({First});
	Enemy->CompleteSearchPoint();
	TestTrue(TEXT("One point also finishes without refill"), Enemy->GetSearchEpisode().bLocalGuard);

	// 직접 시야는 bool만으로 성립하지 않는다. 추적 갱신이 검증할 실제 대상과 Controller 캐시를 준비한다.
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Target = Fixture.SpawnPlayer(Hint);
	if (!Controller || !TestNotNull(TEXT("Direct rediscovery has a valid player"), Target)) return false;
	Controller->Possess(Enemy);
	Controller->SetCachedVisibleTargetForTesting(Target);
	Enemy->SetPlayerCurrentlyVisible(true);
	TestFalse(TEXT("Direct sight ends the search episode"), Enemy->GetSearchEpisode().bActive);
	Enemy->ApplySharedTargetContact(Hint, true, 13);
	TestFalse(TEXT("Sharing while directly seeing is already confirmed"), Enemy->HasFreshSharedTargetLocation());
	TestFalse(TEXT("Visible enemy cannot begin hidden-target search"), Enemy->BeginSearchEpisode());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchReservationTest,
	"Outlier.Enemy.Search.Reservations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchReservationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FGameplayTag OtherRoom = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")));
	AEnemyBase* First = Fixture.SpawnEnemy(Room);
	AEnemyBase* Second = Fixture.SpawnEnemy(Room, FVector(2000, 0, 0));
	AEnemyBase* Other = Fixture.SpawnEnemy(OtherRoom, FVector(4000, 0, 0));
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms) || !First || !Second || !Other) return false;
	First->BeginSearchEpisode();
	Second->BeginSearchEpisode();
	Other->BeginSearchEpisode();
	const FVector Point(600, 0, 300);
	TestTrue(TEXT("First drone reserves search destination"), Rooms->ReserveSearchDestinations(First, {Point}, 200));
	TestTrue(TEXT("Another same-room drone detects occupied destination"), Rooms->IsSearchDestinationReserved(Second, Point, 200));
	TestFalse(TEXT("Reservation rejects duplicate destination"), Rooms->ReserveSearchDestinations(Second, {Point}, 200));
	TestFalse(TEXT("Other rooms do not share reservations"), Rooms->IsSearchDestinationReserved(Other, Point, 200));
	First->EndSearchEpisode();
	TestFalse(TEXT("Search exit releases reservation"), Rooms->IsSearchDestinationReserved(Second, Point, 200));
	First->BeginSearchEpisode();
	Rooms->ReserveSearchDestinations(First, {Point}, 200);
	First->ReleaseSearchDestinations();
	TestFalse(TEXT("Lifecycle cleanup releases new search destinations"), Rooms->IsSearchDestinationReserved(Second, Point, 200));
	TestTrue(TEXT("Reservation cleanup alone preserves the search episode"), First->GetSearchEpisode().bActive);
	Rooms->ReserveSearchDestinations(Second, {Point}, 200);
	Second->EnterSearchLocalGuard();
	First->BeginSearchEpisode();
	TestFalse(TEXT("LocalGuard releases its reservation"), Rooms->IsSearchDestinationReserved(First, Point, 200));
	Rooms->ReserveSearchDestinations(First, {Point}, 200);
	Rooms->UnregisterEnemy(First);
	TestFalse(TEXT("Unregistration releases search destinations"), Rooms->IsSearchDestinationReserved(Second, Point, 200));
	Rooms->ReserveSearchDestinations(First, {Point}, 200);
	Rooms->NotifyRoomCombatEnded(Room);
	TestFalse(TEXT("Room combat ending releases search destinations"), Rooms->IsSearchDestinationReserved(Second, Point, 200));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchEpisodeBudgetTest,
	"Outlier.Enemy.Search.EpisodeBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchEpisodeBudgetTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Deadline"), TEXT("Reentry"), TEXT("SamePositionHint"),
		TEXT("MeaningfulHint"), TEXT("CluePitch")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemySearchEpisodeBudgetTest::RunTest(const FString& Parameters)
{
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World))
	{
		return false;
	}
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Enemy = Fixture.SpawnEnemy(Room);
	if (!TestNotNull(TEXT("Enemy exists"), Enemy))
	{
		return false;
	}
	const FVector Center(500.0, 0.0, 2000.0);
	Enemy->ApplySharedTargetContact(Center, true, 10);
	FEnemySearchSessionTaskInstanceData Session;
	Session.Enemy = Enemy;
	TestEqual(TEXT("Session starts"), EnemySearchTesting::EnterSession(Session), EStateTreeRunStatus::Running);
	const FEnemySearchEpisode Snapshot = Enemy->GetSearchEpisode();
	TestEqual(TEXT("General search limit is four points"), Enemy->GetSearchPointLimit(), 4);
	TestEqual(TEXT("General search includes anchor approach in its twelve seconds"),
		Snapshot.EndsAt - Snapshot.StartedAt, 12.0);

	if (Parameters == TEXT("Deadline"))
	{
		TestTrue(TEXT("Unfinished point exists before the deadline"), Enemy->SetSearchPoints({Center + FVector(500.0, 0.0, 0.0)}));
		for (int32 Step = 0; Step < 383; ++Step)
		{
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		}
		EnemySearchTesting::TickSession(Session);
		TestFalse(TEXT("Search remains available immediately before its deadline"), Session.bLocalGuard);
		Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		EnemySearchTesting::RefreshSessionTransitions(Session);
		TestTrue(TEXT("Deadline interrupts even an unfinished anchor approach"), Session.bLocalGuard);
		TestFalse(TEXT("Late look completion cannot confirm an expired point"), Enemy->CompleteSearchPoint());
		TestTrue(TEXT("Deadline cannot be restarted by parent reentry"), Enemy->BeginSearchEpisode());
		TestEqual(TEXT("Expired episode keeps its deadline"), Enemy->GetSearchEpisode().EndsAt, Snapshot.EndsAt);
		TestTrue(TEXT("Reentered expired episode remains LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);
	}
	else if (Parameters == TEXT("Reentry"))
	{
		const TArray<FVector> Points = {Center + FVector(500.0, 0.0, 0.0), Center + FVector(0.0, 500.0, 0.0),
			Center + FVector(-500.0, 0.0, 0.0), Center + FVector(0.0, -500.0, 0.0)};
		TestTrue(TEXT("Four points commit"), Enemy->SetSearchPoints(Points));
		UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
		TestTrue(TEXT("All four selected points can reserve their actual shared space"),
			Rooms && Rooms->ReserveSearchDestinations(Enemy, Points, 200.0f));
		TestTrue(TEXT("First confirmation consumes one point"), Enemy->CompleteSearchPoint());
		Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		EnemySearchTesting::EnterSession(Session);
		TestEqual(TEXT("Active parent reentry preserves serial"), Enemy->GetSearchEpisode().Serial, Snapshot.Serial);
		EnemySearchTesting::ExitSession(Session);
		EnemySearchTesting::EnterSession(Session);
		TestEqual(TEXT("Exit and reentry preserve absolute deadline"), Enemy->GetSearchEpisode().EndsAt, Snapshot.EndsAt);
		TestEqual(TEXT("Exit and reentry preserve consumed points"), Enemy->GetSearchEpisode().PointIndex, 1);
		TestEqual(TEXT("Confirmed region remains excluded"), Enemy->GetSearchEpisode().CheckedPoints.Num(), 1);
		TestFalse(TEXT("Same evidence cannot refill points after reentry"), Enemy->SetSearchPoints(Points));
	}
	else if (Parameters == TEXT("SamePositionHint") || Parameters == TEXT("MeaningfulHint"))
	{
		const bool bMeaningful = Parameters == TEXT("MeaningfulHint");
		const FVector Hint = Center + FVector(bMeaningful ? 201.0 : 200.0, 0.0, 0.0);
		UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
		AEnemyBase* Other = Fixture.SpawnEnemy(Room, FVector(-2000.0, 0.0, 0.0));
		TestTrue(TEXT("Current search reserves its selected space"),
			Rooms && Rooms->ReserveSearchDestinations(Enemy, {Center}, 200.0f));
		Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		Enemy->ApplySharedTargetContact(Hint, true, 11);
		TestEqual(TEXT("Only a changed search goal releases its current spatial reservation"),
			Rooms->IsSearchDestinationReserved(Other, Center, 200.0f), !bMeaningful);
		EnemySearchTesting::RefreshSessionTransitions(Session);
		TestEqual(TEXT("Only a meaningful changed location requests restart"), Session.bShouldRestart, bMeaningful);
		if (bMeaningful)
		{
			FEnemySearchProgressTaskInstanceData Progress;
			Progress.Enemy = Enemy;
			Progress.Action = EEnemySearchProgress::RestartWithHint;
			TestEqual(TEXT("Meaningful hint restarts through the progress task"),
				EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
			TestTrue(TEXT("New evidence gets its own deadline"), Enemy->GetSearchEpisode().EndsAt > Snapshot.EndsAt);
		}
		else
		{
			EnemySearchTesting::EnterSession(Session);
			TestEqual(TEXT("Same-position report does not replace the frozen clue"), Enemy->GetSearchEpisode().Center, Center);
			TestEqual(TEXT("Same-position report does not reset budget"), Enemy->GetSearchEpisode().EndsAt, Snapshot.EndsAt);
		}
	}
	else if (Parameters == TEXT("CluePitch"))
	{
		float Pitch = 0.0f;
		TestTrue(TEXT("Active search has a clue-based pitch"), Enemy->GetSearchLookPitch(Pitch));
		TestEqual(TEXT("High clue is limited to forty-five degrees"), Pitch, 45.0f);
		Enemy->UpdateLastKnownPlayerLocation(FVector(500.0, 0.0, -2000.0));
		Enemy->GetSearchLookPitch(Pitch);
		TestEqual(TEXT("Later hidden position updates cannot redirect the fixed clue"), Pitch, 45.0f);
		Enemy->SetActorLocation(FVector(0.0, 0.0, 4000.0));
		Enemy->GetSearchLookPitch(Pitch);
		TestEqual(TEXT("Low clue is limited to minus forty-five degrees"), Pitch, -45.0f);
		Enemy->EndSearchEpisode();
		TestFalse(TEXT("Ordinary guard without a search clue does not acquire pitch"), Enemy->GetSearchLookPitch(Pitch));
	}
	else
	{
		AddError(FString::Printf(TEXT("Unknown budget scenario: %s"), *Parameters));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchSharedTargetStabilityTest,
	"Outlier.Enemy.Search.SharedTargetStability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchSharedTargetStabilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* FirstObserver = Fixture.SpawnEnemy(Room);
	AEnemyBase* SecondObserver = Fixture.SpawnEnemy(Room, FVector(2000, 0, 0));
	AActor* FirstTarget = Fixture.World->SpawnActor<AActor>();
	AActor* SecondTarget = Fixture.World->SpawnActor<AActor>();
	if (!Rooms || !FirstObserver || !SecondObserver || !FirstTarget || !SecondTarget) return false;
	const FVector FirstLocation(300, 200, 350);
	const FVector SecondLocation(1800, 1600, 350);
	Rooms->ReportRoomTargetContact(FirstObserver, FirstTarget, FirstLocation);
	const uint64 InitialRevision = Rooms->GetSharedRevisionForTesting(Room);
	TestTrue(TEXT("First contact publishes a revision"), InitialRevision > 0);

	// 같은 우선순위의 서로 다른 타깃 보고가 공유 위치를 번갈아 바꾸지 않아야 한다.
	for (int32 Report = 0; Report < 5; ++Report)
	{
		Rooms->ReportRoomTargetContact(SecondObserver, SecondTarget, SecondLocation);
		Rooms->ReportRoomTargetContact(FirstObserver, FirstTarget, FirstLocation);
	}
	TestEqual(TEXT("Alternating targets preserve the observed shared target"),
		Rooms->GetSharedRevisionForTesting(Room), InitialRevision);
	Rooms->ReportRoomTargetContact(FirstObserver, FirstTarget, FirstLocation + FVector(100, 0, 0));
	const uint64 MovedRevision = Rooms->GetSharedRevisionForTesting(Room);
	TestTrue(TEXT("Same target movement still publishes new information"), MovedRevision > InitialRevision);

	// 공유 타깃의 관측이 끊기면 다른 타깃으로 전환할 수 있으며, 남은 관측자는 핑을 차단한다.
	Rooms->RemoveRoomTargetObserver(FirstObserver);
	Rooms->TriggerForcedShareForTesting(Room);
	TestFalse(TEXT("Other target observer still prevents the all-unseen ping"), Rooms->IsForcedShareConsumedForTesting(Room));
	Rooms->ReportRoomTargetContact(SecondObserver, SecondTarget, SecondLocation);
	const uint64 SwitchedRevision = Rooms->GetSharedRevisionForTesting(Room);
	TestTrue(TEXT("Unobserved target can be replaced"), SwitchedRevision > MovedRevision);
	Rooms->ReportRoomTargetContact(FirstObserver, FirstTarget, FirstLocation);
	TestEqual(TEXT("Rediscovery cannot steal an equally ranked observed target"),
		Rooms->GetSharedRevisionForTesting(Room), SwitchedRevision);
	AEnemyBase* HigherPriorityTarget = Fixture.SpawnEnemy(Room, FVector(6000, 0, 0));
	if (!TestNotNull(TEXT("Higher-priority target exists"), HigherPriorityTarget)) return false;
	Rooms->ReportRoomTargetContact(FirstObserver, HigherPriorityTarget, HigherPriorityTarget->GetActorLocation());
	const uint64 PriorityRevision = Rooms->GetSharedRevisionForTesting(Room);
	TestTrue(TEXT("Higher priority replaces an observed target"), PriorityRevision > SwitchedRevision);
	Rooms->ReportRoomTargetContact(SecondObserver, SecondTarget, SecondLocation);
	TestEqual(TEXT("Lower-priority report cannot switch the target back"),
		Rooms->GetSharedRevisionForTesting(Room), PriorityRevision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchForcedSharingTest,
	"Outlier.Enemy.Search.ForcedSharing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchForcedSharingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Observer = Fixture.SpawnEnemy(Room);
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms) || !Observer || !Target) return false;
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	TestEqual(TEXT("Forced sharing uses three minutes"), Rooms->GetForcedShareDelayForTesting(), 180.0f);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	const uint64 SeenRevision = Rooms->GetSharedRevisionForTesting(Room);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	TestEqual(TEXT("Unchanged report does not invent new information"), Rooms->GetSharedRevisionForTesting(Room), SeenRevision);
	Rooms->TriggerForcedShareForTesting(Room);
	TestFalse(TEXT("Any direct observer prevents forced ping"), Rooms->IsForcedShareConsumedForTesting(Room));
	Rooms->RemoveRoomTargetObserver(Observer);
	Rooms->TriggerForcedShareForTesting(Room);
	TestTrue(TEXT("All-unseen episode sends a single forced ping"), Rooms->IsForcedShareConsumedForTesting(Room));
	const uint64 PingRevision = Rooms->GetSharedRevisionForTesting(Room);
	Rooms->TriggerForcedShareForTesting(Room);
	TestEqual(TEXT("Remaining unseen cannot send a second ping"), Rooms->GetSharedRevisionForTesting(Room), PingRevision);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	TestFalse(TEXT("Rediscovery resets ping consumption"), Rooms->IsForcedShareConsumedForTesting(Room));
	TestTrue(TEXT("Same-coordinate rediscovery receives a fresh revision"), Rooms->GetSharedRevisionForTesting(Room) > PingRevision);
	Rooms->RemoveRoomTargetObserver(Observer);
	Rooms->TriggerForcedShareForTesting(Room);
	TestTrue(TEXT("Next all-unseen episode can send a new ping"), Rooms->IsForcedShareConsumedForTesting(Room));
	Rooms->NotifyRoomCombatEnded(Room);
	Rooms->TriggerForcedShareForTesting(Room);
	TestEqual(TEXT("Ended room cannot retain or publish target information"), Rooms->GetSharedRevisionForTesting(Room), uint64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightCombatJoinTest,
	"Outlier.Enemy.Perception.SightCombatJoin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightCombatJoinTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture(true);
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	if (!TestNotNull(TEXT("Perception system exists before listener creation"),
		UAIPerceptionSystem::GetCurrent(Fixture.World))) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AEnemyAIController* OtherController = Fixture.World->SpawnActor<AEnemyAIController>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FGameplayTag OtherRoom = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")));
	AEnemyBase* JoiningEnemy = Fixture.SpawnEnemy(Room, FVector::ZeroVector, false);
	AEnemyBase* OtherEnemy = Fixture.SpawnEnemy(OtherRoom, FVector(1000, 0, 0), false);
	AShooterCharacter* Target = Fixture.SpawnPlayer(FVector(300, 200, 350));
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms)
		|| !TestNotNull(TEXT("Player BP spawned"), Target)
		|| !Controller || !OtherController || !JoiningEnemy || !OtherEnemy) return false;
	const FVector SeenLocation = Target->GetActorLocation();
	const FAIStimulus Seen(*GetDefault<UAISense_Sight>(), 1.0f, SeenLocation, FVector::ZeroVector);
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	Controller->Possess(JoiningEnemy);
	OtherController->Possess(OtherEnemy);
	// RegisterStimulus는 Sense ID를 배열 인덱스로 사용한다. 초기화 실패는 크래시 대신 테스트 실패로 남긴다.
	if (!TestTrue(TEXT("Sight stimulus has a registered sense"), Seen.Type.IsValid())
		|| !TestTrue(TEXT("Joining listener is registered"), Controller->EnemyPerceptionComponent->GetListenerId().IsValid())
		|| !TestTrue(TEXT("Other listener is registered"), OtherController->EnemyPerceptionComponent->GetListenerId().IsValid())) return false;

	// Sight 캐시에 자극을 넣은 뒤 콜백을 처리한다. 직접 호출만 하면 실제 감지 목록은 비어 있다.
	Controller->EnemyPerceptionComponent->RegisterStimulus(Target, Seen);
	Controller->EnemyPerceptionComponent->ProcessStimuli();
	TestEqual(TEXT("Successful sight joins active room immediately"),
		JoiningEnemy->GetCombatState(), EEnemyCombatState::Combat);
	TestTrue(TEXT("Sight retains direct visibility"), JoiningEnemy->IsPlayerCurrentlyVisible());
	TestTrue(TEXT("Sight remembers the observed location"),
		JoiningEnemy->GetLastKnownPlayerLocation().Equals(SeenLocation));
	OtherController->EnemyPerceptionComponent->RegisterStimulus(Target, Seen);
	OtherController->EnemyPerceptionComponent->ProcessStimuli();
	TestEqual(TEXT("Sight in another inactive room still enters Alert"),
		OtherEnemy->GetCombatState(), EEnemyCombatState::Alert);
	TestFalse(TEXT("Sight does not immediately start the inactive room's combat"),
		Rooms->IsRoomInCombat(OtherRoom));

	JoiningEnemy->EnterStun();
	Controller->HandleSightStimulus(JoiningEnemy, Target, Seen);
	TestEqual(TEXT("Sight combat join preserves active stun"),
		JoiningEnemy->GetCombatState(), EEnemyCombatState::Stun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHearingCombatJoinTest,
	"Outlier.Enemy.Perception.HearingCombatJoin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHearingCombatJoinTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AEnemyAIController* AlertController = Fixture.World->SpawnActor<AEnemyAIController>();
	AEnemyAIController* OtherController = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* NoiseSource = Fixture.SpawnPlayer();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FGameplayTag OtherRoom = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")));
	AEnemyBase* JoiningEnemy = Fixture.SpawnEnemy(Room, FVector::ZeroVector, false);
	AEnemyBase* AlertEnemy = Fixture.SpawnEnemy(Room, FVector(1000, 0, 0), false);
	AEnemyBase* OtherEnemy = Fixture.SpawnEnemy(OtherRoom, FVector(2000, 0, 0), false);
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms)
		|| !TestNotNull(TEXT("Noise source BP spawned"), NoiseSource)
		|| !Controller || !AlertController || !OtherController || !JoiningEnemy || !AlertEnemy || !OtherEnemy) return false;
	const FVector HeardLocation(300, 200, 350);
	const FAIStimulus Heard(*GetDefault<UAISense_Hearing>(), 1.0f, HeardLocation, FVector::ZeroVector);
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);

	// 공유 접촉과 직접 시야가 없어도 전투 중인 자신의 방으로 즉시 합류한다.
	Controller->Possess(JoiningEnemy);
	AlertController->Possess(AlertEnemy);
	OtherController->Possess(OtherEnemy);
	Controller->HandleHearingStimulus(JoiningEnemy, nullptr, FAIStimulus());
	TestEqual(TEXT("Failed hearing does not join combat"), JoiningEnemy->GetCombatState(), EEnemyCombatState::NonCombat);
	Controller->HandleHearingStimulus(JoiningEnemy, NoiseSource, Heard);
	TestEqual(TEXT("Successful hearing joins active room immediately"), JoiningEnemy->GetCombatState(), EEnemyCombatState::Combat);
	TestFalse(TEXT("Hearing does not invent direct sight"), JoiningEnemy->IsPlayerCurrentlyVisible());
	TestFalse(TEXT("Joining does not require or publish shared contact"), JoiningEnemy->HasSharedTargetContact());
	TestTrue(TEXT("Joining remembers the heard location"), JoiningEnemy->GetLastKnownPlayerLocation().Equals(HeardLocation));

	AlertEnemy->EnterAlertFromPerception(FVector::ZeroVector);
	AlertController->HandleHearingStimulus(AlertEnemy, NoiseSource, Heard);
	TestEqual(TEXT("Alert enemy also joins active room on hearing"), AlertEnemy->GetCombatState(), EEnemyCombatState::Combat);
	OtherController->HandleHearingStimulus(OtherEnemy, NoiseSource, Heard);
	TestEqual(TEXT("Another room's combat does not bypass Alert"), OtherEnemy->GetCombatState(), EEnemyCombatState::Alert);
	TestFalse(TEXT("Hearing in inactive room does not start room combat"), Rooms->IsRoomInCombat(OtherRoom));

	// 반복 청각은 현재 수색 회차를 건드리지 않고, Stun도 강제로 해제하지 않는다.
	TestTrue(TEXT("Combat participant can begin hidden-target search"), JoiningEnemy->BeginSearchEpisode());
	const uint64 SearchSerial = JoiningEnemy->GetSearchEpisode().Serial;
	Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
	Controller->HandleHearingStimulus(JoiningEnemy, NoiseSource, Heard);
	TestTrue(TEXT("Repeated hearing preserves active search"), JoiningEnemy->GetSearchEpisode().bActive);
	TestEqual(TEXT("Repeated hearing does not restart search"), JoiningEnemy->GetSearchEpisode().Serial, SearchSerial);
	AlertEnemy->EnterStun();
	AlertController->HandleHearingStimulus(AlertEnemy, NoiseSource, Heard);
	TestEqual(TEXT("Combat join preserves active stun"), AlertEnemy->GetCombatState(), EEnemyCombatState::Stun);
	AEnemyBase* StunnedEnemy = Fixture.SpawnEnemy(Room, FVector(0, 5000, 0), false);
	AEnemyAIController* StunnedController = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!StunnedEnemy || !StunnedController) return false;
	StunnedController->Possess(StunnedEnemy);
	StunnedEnemy->EnterStun();
	StunnedController->HandleHearingStimulus(StunnedEnemy, NoiseSource, Heard);
	TestEqual(TEXT("Hearing never ends stun early"), StunnedEnemy->GetCombatState(), EEnemyCombatState::Stun);
	StunnedEnemy->RestoreStateAfterStun();
	TestEqual(TEXT("Hearing promotes the suspended noncombat state to room combat"), StunnedEnemy->GetCombatState(), EEnemyCombatState::Combat);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHearingMemoryTest,
	"Outlier.Enemy.Perception.HearingMemory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHearingMemoryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag(), FVector::ZeroVector, false);
	AEnemyBase* Ally = Fixture.SpawnEnemy(FGameplayTag(), FVector(2000, 0, 0), false);
	AShooterCharacter* Source = Fixture.SpawnPlayer();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Noise source BP spawned"), Source) || !Enemy || !Ally || !Controller) return false;
	Controller->Possess(Enemy);
	for (int32 Step = 0; Step < 32; ++Step) Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
	const double Now = Fixture.World->GetTimeSeconds();
	const FVector HeardLocation(100, 200, 300);
	FEnemyHeardNoise Noise;
	TestFalse(TEXT("No noise initially"), Enemy->TryGetHeardNoise(Noise));
	TestFalse(TEXT("Enemy sound is rejected"), Enemy->RecordHeardNoise(Ally, HeardLocation, Now));
	TestFalse(TEXT("Future timestamp is rejected"), Enemy->RecordHeardNoise(Source, HeardLocation, Now + 1.0));
	TestTrue(TEXT("Fresh player noise is recorded"), Enemy->RecordHeardNoise(Source, HeardLocation, Now - 0.5));
	TestFalse(TEXT("Out-of-order noise in the same cluster is rejected"), Enemy->RecordHeardNoise(Source, HeardLocation, Now - 0.75));
	TestFalse(TEXT("Duplicate does not extend lifetime"), Enemy->RecordHeardNoise(Source, HeardLocation, Now - 0.5));
	TestTrue(TEXT("Fresh noise can be queried"), Enemy->TryGetHeardNoise(Noise));
	TestTrue(TEXT("Location is an occurrence snapshot"), Noise.Location.Equals(HeardLocation));
	TestEqual(TEXT("Timestamp is preserved"), Noise.OccurredAt, Now - 0.5);
	Source->SetActorLocation(FVector(1000, 0, 0));
	TestTrue(TEXT("Moving source does not move heard location"), Enemy->TryGetHeardNoise(Noise) && Noise.Location.Equals(HeardLocation));
	TestFalse(TEXT("Noise does not grant direct sight"), Enemy->IsPlayerCurrentlyVisible());
	TestFalse(TEXT("Noise does not grant actual-position tracking"), Enemy->HasTrackableTarget());
	TestFalse(TEXT("Noise is not shared to another enemy"), Ally->TryGetHeardNoise(Noise));

	// 소리 만료는 World 시각으로 판정한다. 타이머만 진행하면 이 경계를 검증할 수 없다.
	for (int32 Step = 0; Step < 80; ++Step) Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
	TestFalse(TEXT("Noise expires exactly at three seconds"), Enemy->TryGetHeardNoise(Noise));
	TestTrue(TEXT("Failed query clears output"), Noise.OccurredAt < 0.0 && !Noise.Source.IsValid());
	TestFalse(TEXT("Expired input cannot start a new lifetime"), Enemy->RecordHeardNoise(Source, HeardLocation, Now - 0.5));
	const double Later = Fixture.World->GetTimeSeconds();
	TestTrue(TEXT("Same-position newer noise is accepted"), Enemy->RecordHeardNoise(Source, HeardLocation, Later));
	Controller->UnPossess();
	Controller->Possess(Enemy);
	TestFalse(TEXT("Controller replacement clears noise"), Enemy->TryGetHeardNoise(Noise));

	FAIStimulus Heard(*GetDefault<UAISense_Hearing>(), 1.0f, HeardLocation, FVector::ZeroVector);
	Heard.SetStimulusAge(1.0f);
	Controller->HandleHearingStimulus(Enemy, Source, Heard);
	TestTrue(TEXT("Hearing callback records a player event"), Enemy->TryGetHeardNoise(Noise));
	TestEqual(TEXT("Hearing age is not reset on receipt"), Noise.OccurredAt, Later - 1.0);
	TestEqual(TEXT("Legacy hearing still enters Alert"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
	Enemy->ClearHeardNoise();
	Enemy->HeardNoiseLifetime = 0.5f;
	TestFalse(TEXT("Lifetime override applies to incoming noise"), Enemy->RecordHeardNoise(Source, HeardLocation, Later - 0.5));
	TestTrue(TEXT("Noise inside the configured lifetime is accepted"), Enemy->RecordHeardNoise(Source, HeardLocation, Later - 0.25));
	Source->Destroy();
	TestFalse(TEXT("Destroyed source cannot expose a usable noise"), Enemy->TryGetHeardNoise(Noise));
	AShooterCharacter* ReusedSource = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Another player exists for the pool scenario"), ReusedSource)) return false;
	TestTrue(TEXT("Pool scenario starts with fresh noise"), Enemy->RecordHeardNoise(ReusedSource, HeardLocation, Later));
	Enemy->PrepareForPoolSpawn(Fixture.World->GetSubsystem<UEnemyPoolSubsystem>());
	TestFalse(TEXT("Pool reuse clears heard noise"), Enemy->TryGetHeardNoise(Noise));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyPerceptionGoalTest,
	"Outlier.Enemy.Perception.Goal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyPerceptionGoalTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("DirectSight"), TEXT("SharedDistance"), TEXT("Hysteresis"),
		TEXT("SharedExpiry"), TEXT("RetainExpiredNoise"), TEXT("NewerThanGrace"), TEXT("OlderThanGrace"),
		TEXT("AlternativeLiveTarget"), TEXT("Lifecycle"), TEXT("CommittedCharge") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyPerceptionGoalTest::RunTest(const FString& Parameters)
{
	FEnemySearchTestWorld Fixture(true);
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	AEnemyBase* Observer = Fixture.SpawnEnemy(Room, FVector(3000, 0, 300));
	AEnemyBase* Receiver = Fixture.SpawnEnemy(Room, FVector(0, 0, 300));
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Target = Fixture.SpawnPlayer(FVector(1000, 0, 300));
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(2000, 100, 300));
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms) || !Observer || !Receiver || !Controller
		|| !TestNotNull(TEXT("Shared player BP exists"), Target) || !TestNotNull(TEXT("Noise source BP exists"), Source)) return false;
	Controller->Possess(Receiver);
	Receiver->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	auto Advance = [&](float Seconds)
	{
		for (int32 Step = 0; Step < FMath::RoundToInt(Seconds * 32.0f); ++Step)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
		}
	};
	auto ExpectGoal = [&](const TCHAR* Message, EEnemyPerceptionGoalType Type)
	{
		TestEqual(Message, Receiver->GetPerceptionGoal().Type, Type);
	};
	auto Hear = [&](float Distance, double Age = 0.0)
	{
		return Receiver->RecordHeardNoise(Source, FVector(Distance, 0, 300), Fixture.World->GetTimeSeconds() - Age);
	};
	Advance(1.0f);
	Rooms->SetActiveRoomTargetForTesting(Room, Target->GetActorLocation(), false);
	Rooms->ReportRoomTargetContact(Observer, Target, Target->GetActorLocation());
	Receiver->RefreshTargetTracking();
	if (!TestEqual(TEXT("Baseline retains shared target"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target))) return false;
	ExpectGoal(TEXT("Shared observation starts combat intent"), EEnemyPerceptionGoalType::Combat);

	if (Parameters == TEXT("DirectSight"))
	{
		Controller->SetCachedVisibleTargetForTesting(Source);
		Receiver->SetPlayerCurrentlyVisible(true);
		TestEqual(TEXT("Direct sight adopts the visible player before processing sound"),
			Receiver->GetTrackedTarget(), static_cast<AActor*>(Source));
		TestTrue(TEXT("Near noise is stored"), Hear(100));
		ExpectGoal(TEXT("Any direct player sight blocks noise"), EEnemyPerceptionGoalType::Combat);
		TestEqual(TEXT("Goal uses visible player"), Receiver->GetPerceptionGoal().Target.Get(), static_cast<AActor*>(Source));
		TestEqual(TEXT("Sound cannot redirect the adopted visible target to the hidden shared player"),
			Receiver->GetTrackedTarget(), static_cast<AActor*>(Source));
	}
	else if (Parameters == TEXT("OlderThanGrace") || Parameters == TEXT("NewerThanGrace"))
	{
		Rooms->RemoveRoomTargetObserver(Observer);
		Advance(0.5f);
		TestTrue(TEXT("Noise accepted during tracking grace"), Hear(100, Parameters == TEXT("OlderThanGrace") ? 0.75 : 0.0));
		ExpectGoal(TEXT("Only newer information overrides grace"), Parameters == TEXT("OlderThanGrace")
			? EEnemyPerceptionGoalType::Combat : EEnemyPerceptionGoalType::InvestigateNoise);
		TestEqual(TEXT("Sound never replaces live-position tracking knowledge"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
	}
	else if (Parameters == TEXT("AlternativeLiveTarget"))
	{
		AEnemyBase* OtherObserver = Fixture.SpawnEnemy(Room, FVector(4000, 0, 300));
		if (!TestNotNull(TEXT("Other observer exists"), OtherObserver)) return false;
		Rooms->RemoveRoomTargetObserver(Observer);
		Rooms->ReportRoomTargetContact(OtherObserver, Source, Source->GetActorLocation());
		FEnemyRoomTargetObservationSnapshot Live;
		TestTrue(TEXT("Live-only selection skips the retained grace target"), Rooms->TrySelectRoomTarget(Receiver, Live, true));
		TestEqual(TEXT("Alternative live source selected"), Live.TargetActor.Get(), static_cast<AActor*>(Source));
	}
	else if (Parameters == TEXT("CommittedCharge"))
	{
		ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>();
		AEnemyAIController* DroneController = Fixture.World->SpawnActor<AEnemyAIController>();
		if (!TestNotNull(TEXT("Drone exists"), Drone) || !DroneController) return false;
		DroneController->Possess(Drone);
		if (!TestTrue(TEXT("Charge commits"), Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 10.0f, 100.0f, 1000.0f))) return false;
		TestTrue(TEXT("Charge can hear"), Drone->RecordHeardNoise(Source, FVector(100, 0, 300), Fixture.World->GetTimeSeconds()));
		TestEqual(TEXT("Noise cannot replace committed intent"), Drone->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::CommittedAction);
		TestTrue(TEXT("Charge remains committed"), Drone->IsSelfDestructCommitted());
	}
	else
	{
		TestTrue(TEXT("Near sound accepted despite simultaneous live report"), Hear(100));
		ExpectGoal(TEXT("Near sound wins over live sharing"), EEnemyPerceptionGoalType::InvestigateNoise);
		TestNull(TEXT("Investigation does not expose an attack target"), Receiver->GetPerceptionGoal().Target.Get());
		TestEqual(TEXT("Existing tracking knowledge is preserved"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
		if (Parameters == TEXT("SharedDistance"))
		{
			// 설정 변경도 다음 평가에 반영하며, 거리 차이 경계와 0의 동거리 규칙을 검증한다.
			Receiver->NoiseGoalSwitchDistance = 1000.0f;
			Receiver->RefreshPerceptionGoal();
			ExpectGoal(TEXT("Larger margin retains the chosen noise"), EEnemyPerceptionGoalType::InvestigateNoise);
			Advance(0.25f);
			Receiver->ClearHeardNoise();
			TestTrue(TEXT("Sound exactly one margin farther is accepted"), Hear(2000));
			ExpectGoal(TEXT("Exact configured margin switches to sharing"), EEnemyPerceptionGoalType::Combat);
			Receiver->NoiseGoalSwitchDistance = 0.0f;
			Advance(0.25f);
			Receiver->ClearHeardNoise();
			TestTrue(TEXT("Equal-distance sound is accepted"), Hear(1000));
			ExpectGoal(TEXT("Zero margin ties retain combat"), EEnemyPerceptionGoalType::Combat);
			Advance(0.25f);
			TestTrue(TEXT("Closer sound is accepted with zero margin"), Hear(999));
			ExpectGoal(TEXT("Zero margin still switches for a strict advantage"), EEnemyPerceptionGoalType::InvestigateNoise);
		}
		else if (Parameters == TEXT("Hysteresis"))
		{
			Advance(0.25f);
			Receiver->SetActorLocation(FVector(600, 0, 300));
			Receiver->RefreshPerceptionGoal();
			ExpectGoal(TEXT("Small distance reversal retains investigation"), EEnemyPerceptionGoalType::InvestigateNoise);
			Advance(0.25f);
			Receiver->SetActorLocation(FVector(800, 0, 300));
			Receiver->RefreshPerceptionGoal();
			ExpectGoal(TEXT("Significantly closer shared target restores combat"), EEnemyPerceptionGoalType::Combat);
			Advance(0.25f);
			Receiver->SetActorLocation(FVector(0, 0, 300));
			Receiver->ClearHeardNoise();
			TestTrue(TEXT("Small sound advantage accepted"), Hear(900));
			ExpectGoal(TEXT("Small advantage retains combat"), EEnemyPerceptionGoalType::Combat);
		}
		else if (Parameters == TEXT("Lifecycle"))
		{
			Controller->UnPossess();
			ExpectGoal(TEXT("Unpossess clears adopted goal"), EEnemyPerceptionGoalType::None);
			Controller->Possess(Receiver);
			Receiver->RefreshPerceptionGoal();
			ExpectGoal(TEXT("Old noise cannot resume after controller reset"), EEnemyPerceptionGoalType::Search);
		}
		else
		{
			if (Parameters == TEXT("RetainExpiredNoise"))
			{
				FVector Destination;
				if (!TestTrue(TEXT("Only an adopted snapshot survives expiry"), Receiver->BeginNoiseInvestigation(Destination))) return false;
				Rooms->RemoveRoomTargetObserver(Observer);
			}
			Advance(3.0f);
			FEnemyHeardNoise ExpiredNoise;
			TestFalse(TEXT("Noise is invalid at the exact lifetime boundary"), Receiver->TryGetHeardNoise(ExpiredNoise));
			// TimerManager는 예약 시각을 초과한 틱에 콜백을 실행한다.
			// 3초 경계의 정보 만료와 다음 틱의 자동 의도 갱신을 별도로 검증한다.
			Advance(1.0f / 32.0f);
			ExpectGoal(TEXT("Expiry returns to live sharing or retains adopted snapshot"), Parameters == TEXT("SharedExpiry")
				? EEnemyPerceptionGoalType::Combat : EEnemyPerceptionGoalType::InvestigateNoise);
			if (Parameters == TEXT("RetainExpiredNoise"))
			{
				Source->SetActorLocation(FVector(5000, 0, 300));
				Receiver->RefreshPerceptionGoal();
				TestTrue(TEXT("Adopted goal never follows the sound source"), Receiver->GetPerceptionGoal().Location.Equals(FVector(100, 0, 300)));
			}
		}
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseInvestigationTest,
	"Outlier.Enemy.Perception.Investigation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseInvestigationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("FixedSnapshot"), TEXT("DestinationThrottle"), TEXT("SearchWithSharedTarget"),
		TEXT("NewNoiseDuringSearch"), TEXT("CompletionConsumesNoise"), TEXT("DirectReacquire"),
		TEXT("ControllerReset"), TEXT("ReachableSearchCenter"), TEXT("ExpiredPendingDestination") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseInvestigationTest::RunTest(const FString& Parameters)
{
	FEnemySearchTestWorld Fixture(true);
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyBase* Observer = Fixture.SpawnEnemy(Room, FVector(3000, 0, 300));
	AEnemyBase* Receiver = Fixture.SpawnEnemy(Room, FVector(0, 0, 300));
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Target = Fixture.SpawnPlayer(FVector(1000, 0, 300));
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(2000, 100, 300));
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms) || !Observer || !Receiver || !Controller
		|| !TestNotNull(TEXT("Shared player BP exists"), Target) || !TestNotNull(TEXT("Noise source BP exists"), Source)) return false;
	Controller->Possess(Receiver);
	Receiver->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	auto Advance = [&](float Seconds)
	{
		for (int32 Step = 0; Step < FMath::RoundToInt(Seconds * 32.0f); ++Step)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
		}
	};
	auto Hear = [&](const FVector& Location)
	{
		return Receiver->RecordHeardNoise(Source, Location, Fixture.World->GetTimeSeconds());
	};
	Advance(1.0f);
	Rooms->SetActiveRoomTargetForTesting(Room, Target->GetActorLocation(), false);
	Rooms->ReportRoomTargetContact(Observer, Target, Target->GetActorLocation());
	Receiver->RefreshTargetTracking();
	if (!TestEqual(TEXT("Baseline has real shared tracking"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target))) return false;
	const bool bTestsRetarget = Parameters == TEXT("DestinationThrottle") || Parameters == TEXT("NewNoiseDuringSearch")
		|| Parameters == TEXT("ExpiredPendingDestination");
	const FVector FirstNoise(bTestsRetarget ? 800 : 100, 0, 300);
	if (!TestTrue(TEXT("Nearby player sound is accepted"), Hear(FirstNoise))) return false;
	FVector Destination = FVector::ZeroVector;
	if (!TestTrue(TEXT("Selected noise starts an investigation"), Receiver->BeginNoiseInvestigation(Destination))) return false;
	const uint64 FirstSerial = Receiver->GetNoiseInvestigationSerial();
	TestTrue(TEXT("Investigation owns a current movement episode"), Receiver->IsNoiseInvestigationCurrent(FirstSerial));
	TestTrue(TEXT("Movement starts at the heard snapshot"), Destination.Equals(FirstNoise));
	TestEqual(TEXT("Investigating does not erase shared knowledge"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));

	if (Parameters == TEXT("FixedSnapshot"))
	{
		Source->SetActorLocation(FVector(5000, 500, 300));
		Advance(0.5f);
		Receiver->RefreshPerceptionGoal();
		TestFalse(TEXT("A moving source cannot retarget investigation without another sound"), Receiver->TryUpdateNoiseInvestigationDestination(Destination));
		TestTrue(TEXT("Selected goal remains the occurrence location"), Receiver->GetPerceptionGoal().Location.Equals(FirstNoise));
		TestTrue(TEXT("Investigation never grants direct visibility"), !Receiver->IsPlayerCurrentlyVisible());
	}
	else if (Parameters == TEXT("DestinationThrottle"))
	{
		Receiver->NoiseDestinationChangeDistance = 100.0f;
		Advance(0.5f);
		const FVector SecondNoise(600, 0, 300);
		TestTrue(TEXT("Latest sound is remembered before the movement interval"), Hear(SecondNoise));
		TestFalse(TEXT("A meaningful shift cannot bypass the minimum interval"), Receiver->TryUpdateNoiseInvestigationDestination(Destination));
		Advance(0.5f);
		Receiver->RefreshPerceptionGoal();
		TestTrue(TEXT("Pending shift applies at the interval boundary"), Receiver->TryUpdateNoiseInvestigationDestination(Destination));
		TestTrue(TEXT("Destination uses the newest eligible snapshot"), Destination.Equals(SecondNoise));
		Advance(0.5f);
		TestTrue(TEXT("New occurrence at the same position is remembered"), Hear(SecondNoise));
		TestFalse(TEXT("Same position does not request another path"), Receiver->TryUpdateNoiseInvestigationDestination(Destination));
		TestEqual(TEXT("Same-position sound does not replace the movement episode"), Receiver->GetNoiseInvestigationSerial(), FirstSerial);
		Advance(0.5f);
		// 거리 이득 조건을 분리해 좌표 차이 100cm 제한 자체를 검증한다.
		Receiver->NoiseCandidateSwitchDistance = 0.0f;
		TestTrue(TEXT("Small closer position shift is remembered"), Hear(SecondNoise - FVector(99, 0, 0)));
		TestFalse(TEXT("Sub-threshold displacement does not request another path"), Receiver->TryUpdateNoiseInvestigationDestination(Destination));
	}
	else if (Parameters == TEXT("ExpiredPendingDestination"))
	{
		Rooms->RemoveRoomTargetObserver(Observer);
		Receiver->NoiseDestinationUpdateInterval = 5.0f;
		Advance(0.5f);
		TestTrue(TEXT("A newer sound waits for the destination interval"), Hear(FVector(600, 0, 300)));
		TestFalse(TEXT("Pending sound cannot bypass the interval"), Receiver->TryUpdateNoiseInvestigationDestination(Destination));
		Advance(5.0f);
		Receiver->RefreshPerceptionGoal();
		TestFalse(TEXT("Expired unadopted sound cannot become a destination"), Receiver->TryUpdateNoiseInvestigationDestination(Destination));
		TestTrue(TEXT("Only the already adopted destination survives expiry"), Destination.Equals(FirstNoise));
		TestTrue(TEXT("Published intent returns to the adopted snapshot"), Receiver->GetPerceptionGoal().Location.Equals(FirstNoise));
	}
	else if (Parameters == TEXT("DirectReacquire") || Parameters == TEXT("ControllerReset"))
	{
		if (Parameters == TEXT("DirectReacquire"))
		{
			Controller->SetCachedVisibleTargetForTesting(Target);
			Receiver->SetPlayerCurrentlyVisible(true);
			TestEqual(TEXT("Direct reacquisition restores combat intent"), Receiver->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::Combat);
		}
		else Controller->UnPossess();
		TestFalse(TEXT("Higher-priority perception or controller reset cancels investigation"), Receiver->IsNoiseInvestigationActive());
		TestFalse(TEXT("Cancelled movement serial is no longer current"), Receiver->IsNoiseInvestigationCurrent(FirstSerial));
		TestFalse(TEXT("Cancelled search reservations are released"), Receiver->GetSearchEpisode().bActive);
		if (Parameters == TEXT("DirectReacquire"))
		{
			Controller->SetCachedVisibleTargetForTesting(nullptr);
			Receiver->SetPlayerCurrentlyVisible(false);
			Rooms->RemoveRoomTargetObserver(Observer);
		}
		else Controller->Possess(Receiver);
		Advance(0.5f);
		TestTrue(TEXT("A later sound remains eligible"), Hear(FVector(200, 0, 300)));
		if (!TestTrue(TEXT("A new investigation can start"), Receiver->BeginNoiseInvestigation(Destination))) return false;
		const uint64 NewSerial = Receiver->GetNoiseInvestigationSerial();
		TestTrue(TEXT("A replacement episode has a new serial"), NewSerial != FirstSerial);
		TestFalse(TEXT("Old movement completion cannot start replacement search"), Receiver->BeginNoiseInvestigationSearch(FirstSerial, Receiver->GetActorLocation()));
		TestTrue(TEXT("Rejected old completion preserves new movement"), Receiver->IsNoiseInvestigationCurrent(NewSerial));
	}
	else
	{
		if (Parameters == TEXT("ReachableSearchCenter"))
		{
			TestFalse(TEXT("Unreached sound-side point cannot become a fallback center"),
				Receiver->BeginNoiseInvestigationSearch(FirstSerial, FirstNoise));
			TestTrue(TEXT("Rejected fallback preserves active movement"), Receiver->IsNoiseInvestigationCurrent(FirstSerial));
		}
		const FVector ReachedLocation = Receiver->GetActorLocation();
		if (!TestTrue(TEXT("Physically reached location starts local search"), Receiver->BeginNoiseInvestigationSearch(FirstSerial, ReachedLocation))) return false;
		TestTrue(TEXT("Investigation changes to its search phase"), Receiver->IsNoiseSearchActive());
		TestEqual(TEXT("Arrival publishes search intent"), Receiver->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::Search);
		if (!Receiver->GetSearchEpisode().bActive)
			TestTrue(TEXT("Noise search can start while shared tracking remains valid"), Receiver->BeginSearchEpisode());
		TestTrue(TEXT("Search owns a fixed reached center"), Receiver->GetSearchEpisode().Center.Equals(ReachedLocation));
		TestEqual(TEXT("Noise search does not destroy a valid shared target"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
		if (Parameters == TEXT("NewNoiseDuringSearch"))
		{
			Advance(1.0f);
			// 수색 중 이동한 위치에서 기존 중심보다 2m 가까운 새 소리를 듣는다.
			Receiver->SetActorLocation(FVector(-400, 0, 300));
			const FVector NewLocation(-200, 0, 300);
			TestTrue(TEXT("A newer nearby sound arrives during local search"), Hear(NewLocation));
			TestEqual(TEXT("New sound requests investigation movement"), Receiver->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::InvestigateNoise);
			if (!TestTrue(TEXT("New goal can start movement from the search phase"), Receiver->BeginNoiseInvestigation(Destination))) return false;
			TestTrue(TEXT("Restarted movement uses the new heard location"), Destination.Equals(NewLocation));
			TestFalse(TEXT("Restart releases the previous search episode"), Receiver->GetSearchEpisode().bActive);
			TestFalse(TEXT("Previous movement cannot complete after retarget"), Receiver->BeginNoiseInvestigationSearch(FirstSerial, Receiver->GetActorLocation()));
		}
		else if (Parameters == TEXT("CompletionConsumesNoise"))
		{
			Receiver->EndNoiseInvestigation();
			Receiver->RefreshPerceptionGoal();
			TestFalse(TEXT("Completed investigation is inactive"), Receiver->IsNoiseInvestigationActive());
			TestFalse(TEXT("Completion releases local search reservations"), Receiver->GetSearchEpisode().bActive);
			TestFalse(TEXT("Consumed sound cannot start another investigation"), Receiver->BeginNoiseInvestigation(Destination));
			Advance(0.5f);
			TestTrue(TEXT("A later occurrence at the old position is fresh evidence"), Hear(FirstNoise));
			TestTrue(TEXT("New evidence can start an investigation after completion"), Receiver->BeginNoiseInvestigation(Destination));
		}
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyInvestigationEvidenceTest,
	"Outlier.Enemy.Perception.InvestigationEvidence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyInvestigationEvidenceTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("DirectionUse"), TEXT("NewEvidenceSurvives"), TEXT("FixedActiveSnapshot"),
		TEXT("VisualHold"), TEXT("VisualRetarget"), TEXT("Reset"), TEXT("ExpiredDirection"), TEXT("SightCallback")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyInvestigationEvidenceTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchTestWorld Fixture(true);
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag());
	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(3000, 0, 0));
	AShooterCharacter* Other = Fixture.SpawnPlayer(FVector(4000, 0, 0));
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("Controller"), AI)
		|| !TestNotNull(TEXT("Source"), Source) || !TestNotNull(TEXT("Other source"), Other)) return false;
	AI->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	Other->GetCharacterMovement()->SetComponentTickEnabled(false);
	const auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
		}
	};
	Advance(32);
	const FVector First(1000, 0, 0);
	if (!TestTrue(TEXT("Original event accepted"), Enemy->RecordHeardNoise(Source, First, Fixture.World->GetTimeSeconds()))) return false;
	FEnemyHeardNoise Original;
	if (!TestTrue(TEXT("Original event available"), Enemy->TryGetHeardNoise(Original))) return false;
	TestTrue(TEXT("Accepted event has identity"), Original.Serial != 0);
	if (Parameters == TEXT("DirectionUse"))
	{
		TestTrue(TEXT("One direction use accepted"), Enemy->MarkNoiseDirectionUsed(Original.Serial));
		TestFalse(TEXT("Same direction use cannot repeat"), Enemy->MarkNoiseDirectionUsed(Original.Serial));
		FEnemyHeardNoise Direction;
		TestFalse(TEXT("Used event is absent from direction candidates"), Enemy->TryGetDirectionConfirmationNoise(Direction));
		FVector Destination;
		TestTrue(TEXT("Direction use does not consume location investigation"), Enemy->BeginNoiseInvestigation(Destination));
		TestEqual(TEXT("Investigation adopts the same event"), Enemy->GetActiveNoiseInvestigationClue().Serial, Original.Serial);
		Enemy->EndNoiseInvestigation();
		TestFalse(TEXT("Completed location cannot repeat"), Enemy->BeginNoiseInvestigation(Destination));
		return true;
	}
	if (Parameters == TEXT("ExpiredDirection"))
	{
		Advance(96);
		TestFalse(TEXT("Expired event cannot start direction use"), Enemy->MarkNoiseDirectionUsed(Original.Serial));
		FEnemyHeardNoise Direction;
		return TestFalse(TEXT("Expired direction candidate excluded"), Enemy->TryGetDirectionConfirmationNoise(Direction));
	}
	FVector Destination;
	if (!TestTrue(TEXT("Investigation begins"), Enemy->BeginNoiseInvestigation(Destination))) return false;
	if (Parameters == TEXT("NewEvidenceSurvives"))
	{
		Advance(8);
		TestTrue(TEXT("New event arrives while destination is held"), Enemy->RecordHeardNoise(Other, FVector(2000, 0, 0), Fixture.World->GetTimeSeconds()));
		Enemy->EndNoiseInvestigation();
		FEnemyHeardNoise Remaining;
		TestTrue(TEXT("Old completion preserves newer evidence"), Enemy->TryGetHeardNoise(Remaining));
		TestTrue(TEXT("Remaining event has separate identity"), Remaining.Serial != Original.Serial);
		TestTrue(TEXT("New candidate starts next investigation"), Enemy->BeginNoiseInvestigation(Destination));
		return TestTrue(TEXT("Next destination is the new event"), Destination.Equals(FVector(2000, 0, 0)));
	}
	if (Parameters == TEXT("FixedActiveSnapshot"))
	{
		Advance(8);
		TestTrue(TEXT("Nearby event replaces candidate slot"), Enemy->RecordHeardNoise(Source, First + FVector(50, 0, 0), Fixture.World->GetTimeSeconds()));
		const FEnemyHeardNoise& Active = Enemy->GetActiveNoiseInvestigationClue();
		TestEqual(TEXT("Active event identity stays fixed"), Active.Serial, Original.Serial);
		TestTrue(TEXT("Active occurrence coordinate stays fixed"), Active.Location.Equals(First));
		TestTrue(TEXT("Reached center can start search"), Enemy->BeginNoiseInvestigationSearch(Enemy->GetNoiseInvestigationSerial(), Enemy->GetActorLocation()));
		TestTrue(TEXT("Reached center does not overwrite occurrence"), Enemy->GetActiveNoiseInvestigationClue().Location.Equals(First));
		Advance(96);
		return TestTrue(TEXT("Active investigation survives source candidate expiry"), Enemy->IsNoiseInvestigationActive());
	}
	if (Parameters == TEXT("Reset"))
	{
		Enemy->RecordInvestigationSight(Source, First, Fixture.World->GetTimeSeconds());
		Enemy->ClearHeardNoise();
		TestFalse(TEXT("Reset removes visual hold"), Enemy->IsVisualInvestigationHoldActive());
		TestEqual(TEXT("Reset removes visual adoption"), Enemy->GetVisualInvestigation().AdoptedAt, -1.0);
		Advance(1);
		TestTrue(TEXT("New lease accepts evidence"), Enemy->RecordHeardNoise(Source, First, Fixture.World->GetTimeSeconds()));
		FEnemyHeardNoise Fresh;
		Enemy->TryGetHeardNoise(Fresh);
		TestTrue(TEXT("Serial never returns to old lease"), Fresh.Serial > Original.Serial);
		TestFalse(TEXT("Old confirmation callback cannot mark fresh slot"), Enemy->MarkNoiseDirectionUsed(Original.Serial));
		TestTrue(TEXT("New lease starts an investigation"), Enemy->BeginNoiseInvestigation(Destination));
		TestTrue(TEXT("Visual state exists before Controller loss"), Enemy->RecordInvestigationSight(Source, First, Fixture.World->GetTimeSeconds()));
		AI->UnPossess();
		TestFalse(TEXT("Controller loss clears active investigation"), Enemy->IsNoiseInvestigationActive());
		TestFalse(TEXT("Controller loss clears sound candidates"), Enemy->TryGetHeardNoise(Fresh));
		return TestEqual(TEXT("Controller loss clears candidate and visual state"), Enemy->GetVisualInvestigation().AdoptedAt, -1.0);
	}
	if (Parameters == TEXT("SightCallback"))
	{
		UAIPerceptionComponent* Perception = AI->GetPerceptionComponent();
		FAIStimulus Sight(*GetDefault<UAISense_Sight>(), 1.0f, Source->GetActorLocation(), Enemy->GetActorLocation());
		Perception->RegisterStimulus(Source, Sight);
		Perception->ProcessStimuli();
		return TestEqual(TEXT("Actual sight callback records the selected target"), Enemy->GetVisualInvestigation().Target.Get(), static_cast<AActor*>(Source));
	}
	const double AdoptedAt = Fixture.World->GetTimeSeconds();
	if (!TestTrue(TEXT("Visual goal adopted during investigation"), Enemy->RecordInvestigationSight(Source, First, AdoptedAt))) return false;
	if (Parameters == TEXT("VisualRetarget"))
	{
		Advance(8);
		TestTrue(TEXT("Different target at same coordinate adopted"), Enemy->RecordInvestigationSight(Other, First, Fixture.World->GetTimeSeconds()));
		TestTrue(TEXT("Target switch updates adoption time"), Enemy->GetVisualInvestigation().AdoptedAt > AdoptedAt);
		const double ChangedAt = Enemy->GetVisualInvestigation().AdoptedAt;
		Advance(8);
		TestTrue(TEXT("Meaningful coordinate change adopted"), Enemy->RecordInvestigationSight(Other, First + FVector(100, 0, 0), Fixture.World->GetTimeSeconds()));
		TestTrue(TEXT("Coordinate switch updates adoption time"), Enemy->GetVisualInvestigation().AdoptedAt > ChangedAt);
		return TestFalse(TEXT("Older callback cannot restore previous goal"), Enemy->RecordInvestigationSight(Source, First, AdoptedAt));
	}
	Advance(16);
	TestTrue(TEXT("Same target nearby observation accepted"), Enemy->RecordInvestigationSight(Source, First + FVector(50, 0, 0), Fixture.World->GetTimeSeconds()));
	TestEqual(TEXT("Reobservation leaves original adoption clock"), Enemy->GetVisualInvestigation().AdoptedAt, AdoptedAt);
	TestTrue(TEXT("Observation clock advances independently"), Enemy->GetVisualInvestigation().LastObservedAt > AdoptedAt);
	TestTrue(TEXT("Original one second is still active"), Enemy->IsVisualInvestigationHoldActive());
	Enemy->EndNoiseInvestigation(false);
	Advance(8);
	TestTrue(TEXT("Newer noise is retained during visual hold"), Enemy->RecordHeardNoise(Other, FVector(500, 0, 0), Fixture.World->GetTimeSeconds()));
	TestTrue(TEXT("Newer noise cannot take goal before visual deadline"), Enemy->GetPerceptionGoal().Type != EEnemyPerceptionGoalType::InvestigateNoise);
	Advance(8);
	TestFalse(TEXT("Hold ends at original one second boundary"), Enemy->IsVisualInvestigationHoldActive());
	// TimerManager는 만료 시각을 지난 다음 갱신에서 콜백을 실행할 수 있다. 유지 조건과 예약 실행을 구분한다.
	Advance(1);
	TestEqual(TEXT("Existing perception timer publishes waiting noise at hold end"), Enemy->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::InvestigateNoise);
	Enemy->VisualInvestigationHoldDuration = 2.0f;
	TestTrue(TEXT("Hold duration is configurable"), Enemy->IsVisualInvestigationHoldActive());
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseCandidateTest,
	"Outlier.Enemy.Perception.NoiseCandidates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseCandidateTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("Nearest"), TEXT("HoldBoundary"), TEXT("HoldTimer"), TEXT("EqualQueued"),
		TEXT("Farther"), TEXT("InsufficientBenefit"), TEXT("ExpiredPending"), TEXT("UnadoptedExpiry"),
		TEXT("ObservationFilter"), TEXT("Capacity"), TEXT("ControllerReset") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseCandidateTest::RunTest(const FString& Parameters)
{
	FEnemySearchTestWorld Fixture(true);
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag());
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(10000, 0, 0));
	AShooterCharacter* OtherSource = Fixture.SpawnPlayer(FVector(12000, 0, 0));
	if (!Enemy || !Controller || !Source || !OtherSource) return false;
	Controller->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	OtherSource->GetCharacterMovement()->SetComponentTickEnabled(false);
	auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
		}
	};
	auto Hear = [&](const FVector& Location)
	{
		return Enemy->RecordHeardNoise(Source, Location, Fixture.World->GetTimeSeconds());
	};
	Advance(128);
	const double Now = Fixture.World->GetTimeSeconds();
	const FVector Original(800, 0, 0);
	const double InitialAge = Parameters == TEXT("ExpiredPending") ? 2.5 : 0.0;
	if (!TestTrue(TEXT("Initial candidate is accepted"), Enemy->RecordHeardNoise(Source, Original, Now - InitialAge))) return false;

	if (Parameters == TEXT("Nearest"))
	{
		const FVector Near(300, 0, 0);
		TestTrue(TEXT("Older sound from another source remains eligible"), Enemy->RecordHeardNoise(OtherSource, Near, Now - 0.25));
		Advance(1);
		TestTrue(TEXT("Newest distant sound remains a candidate"), Hear(FVector(1000, 0, 0)));
		FVector Destination;
		if (!TestTrue(TEXT("Nearest sound starts movement"), Enemy->BeginNoiseInvestigation(Destination))) return false;
		TestTrue(TEXT("Distance wins over occurrence order"), Destination.Equals(Near));
		Enemy->EndNoiseInvestigation();
		TestFalse(TEXT("Completion consumes unselected old candidates too"), Enemy->BeginNoiseInvestigation(Destination));
		return true;
	}
	if (Parameters == TEXT("UnadoptedExpiry"))
	{
		Advance(97);
		Enemy->RefreshPerceptionGoal();
		FVector Destination;
		TestFalse(TEXT("Expired queued intent cannot start movement"), Enemy->BeginNoiseInvestigation(Destination));
		return true;
	}
	if (Parameters == TEXT("ObservationFilter"))
	{
		Controller->SetCachedVisibleTargetForTesting(Source);
		Enemy->SetPlayerCurrentlyVisible(true);
		Enemy->RefreshPerceptionGoal();
		Controller->SetCachedVisibleTargetForTesting(nullptr);
		Enemy->SetPlayerCurrentlyVisible(false);
		Advance(1);
		TestTrue(TEXT("Post-observation farther sound is accepted"), Hear(FVector(1200, 0, 0)));
		TestTrue(TEXT("Ineligible close sound cannot hide newer evidence"), Enemy->GetPerceptionGoal().Location.Equals(FVector(1200, 0, 0)));
		return true;
	}
	if (Parameters == TEXT("Capacity"))
	{
		for (int32 Index = 0; Index < 10; ++Index)
		{
			Advance(1);
			Hear(FVector(1100 + Index * 200, 0, 0));
		}
		Advance(1);
		TestTrue(TEXT("A close candidate replaces a full buffer entry"), Hear(FVector(200, 0, 0)));
		FVector Destination;
		if (!TestTrue(TEXT("Bounded candidate buffer remains usable"), Enemy->BeginNoiseInvestigation(Destination))) return false;
		TestTrue(TEXT("Overflow preserves the nearest evidence"), Destination.Equals(FVector(200, 0, 0)));
		return true;
	}

	FVector Destination;
	if (!TestTrue(TEXT("Original destination is adopted"), Enemy->BeginNoiseInvestigation(Destination))) return false;
	const uint64 Serial = Enemy->GetNoiseInvestigationSerial();
	Advance(1);
	FVector Candidate(600, 0, 0);
	if (Parameters == TEXT("EqualQueued")) Candidate = FVector(-800, 0, 0);
	if (Parameters == TEXT("Farther")) Candidate = FVector(1000, 0, 0);
	if (Parameters == TEXT("InsufficientBenefit")) Candidate = FVector(700, 0, 0);
	if (Parameters == TEXT("ExpiredPending"))
		TestTrue(TEXT("Aged candidate waits without extending its lifetime"), Enemy->RecordHeardNoise(Source, Candidate, Now - 2.25));
	else TestTrue(TEXT("New sound is recorded during hold"), Hear(Candidate));
	TestFalse(TEXT("Receipt cannot bypass the one-second hold"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
	TestTrue(TEXT("Pending sound cannot rotate the published goal"), Enemy->GetPerceptionGoal().Location.Equals(Original));
	if (Parameters == TEXT("ControllerReset"))
	{
		Controller->UnPossess();
		Controller->Possess(Enemy);
		Advance(33);
		FEnemyHeardNoise Noise;
		TestFalse(TEXT("Controller reset clears all candidates"), Enemy->TryGetHeardNoise(Noise));
		TestFalse(TEXT("Old hold timer cannot restore old ownership"), Enemy->IsNoiseInvestigationCurrent(Serial));
		return true;
	}
	if (Parameters == TEXT("EqualQueued"))
	{
		Advance(1);
		Candidate = FVector(0, 800, 0);
		TestTrue(TEXT("Another equal-distance sound joins the queue"), Hear(Candidate));
		Advance(29);
	}
	else Advance(30);
	TestFalse(TEXT("Destination remains fixed immediately before one second"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
	Advance(1);
	// TimerManager는 경계를 초과한 다음 틱에 실행한다. 이 사례는 수동 Refresh 없이 예약 자체를 검증한다.
	if (Parameters == TEXT("HoldTimer")) Advance(1);
	else Enemy->RefreshPerceptionGoal();
	const bool bShouldSwitch = Parameters == TEXT("HoldBoundary") || Parameters == TEXT("HoldTimer") || Parameters == TEXT("EqualQueued");
	TestEqual(TEXT("Only equal or sufficiently closer valid evidence can replace the destination"),
		Enemy->TryUpdateNoiseInvestigationDestination(Destination), bShouldSwitch);
	TestTrue(TEXT("Exactly one final candidate is adopted"), Destination.Equals(bShouldSwitch ? Candidate : Original));
	TestFalse(TEXT("Queued intermediate sounds cannot cause another immediate switch"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseTransitionPolicyTest,
	"Outlier.Enemy.Perception.NoiseTransitionPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseTransitionPolicyTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("MoveEqualInside"), TEXT("MoveEqualBoundary"), TEXT("MoveEqualOutside"),
		TEXT("MoveBenefitInside"), TEXT("MoveBenefitBoundary"), TEXT("MovePositionInside"), TEXT("MovePositionBoundary"),
		TEXT("SearchFar"), TEXT("SearchPositionInside"), TEXT("SearchPositionBoundary"), TEXT("SearchOlder"),
		TEXT("SearchExpired"), TEXT("SearchHoldBefore"), TEXT("SearchHoldBoundary"), TEXT("SearchHoldAfter"),
		TEXT("ReentryKeepsClock"), TEXT("NearestBeforeNewest"), TEXT("EqualNewest"), TEXT("VisualMoveDistance"),
		TEXT("CombinedHold"), TEXT("ConfigurableTolerance"), TEXT("CompletionRace")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseTransitionPolicyTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchTestWorld Fixture(true);
	AEnemyBase* Enemy = Fixture.SpawnEnemy(NoiseCompletionRoomTag.GetTag());
	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(10000, 0, 0));
	AShooterCharacter* Other = Fixture.SpawnPlayer(FVector(12000, 0, 0));
	if (!Enemy || !AI || !Source || !Other) return false;
	AI->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	Other->GetCharacterMovement()->SetComponentTickEnabled(false);
	const auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
		}
	};
	Advance(128);
	const FVector Original(800, 0, 0);
	const double OriginalAt = Fixture.World->GetTimeSeconds();
	if (!TestTrue(TEXT("Original evidence recorded"), Enemy->RecordHeardNoise(Source, Original, OriginalAt))) return false;
	FVector Destination;
	if (!TestTrue(TEXT("Original goal adopted"), Enemy->BeginNoiseInvestigation(Destination))) return false;
	const uint64 Episode = Enemy->GetNoiseInvestigationSerial();
	const bool bSearch = Parameters.StartsWith(TEXT("Search")) || Parameters == TEXT("CompletionRace");
	if (bSearch && !TestTrue(TEXT("Actual reached center starts search"), Enemy->BeginNoiseInvestigationSearch(Episode, Enemy->GetActorLocation()))) return false;
	if (Parameters == TEXT("ReentryKeepsClock"))
	{
		Advance(16);
		TestTrue(TEXT("Same movement can reenter"), Enemy->BeginNoiseInvestigation(Destination));
		TestEqual(TEXT("Reentry does not issue a new episode"), Enemy->GetNoiseInvestigationSerial(), Episode);
		Advance(16);
		Enemy->RecordHeardNoise(Other, FVector(500, 0, 0), Fixture.World->GetTimeSeconds());
		return TestTrue(TEXT("Reentry did not extend the original deadline"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
	}
	if (Parameters == TEXT("CombinedHold") || Parameters == TEXT("VisualMoveDistance"))
	{
		TestTrue(TEXT("Visual location recorded"), Enemy->RecordInvestigationSight(Source, Original, OriginalAt));
		Enemy->EndNoiseInvestigation(false);
		Advance(1);
		const FVector Candidate = Parameters == TEXT("CombinedHold") ? FVector(500, 0, 0) : FVector(1400, 0, 0);
		Enemy->RecordHeardNoise(Other, Candidate, Fixture.World->GetTimeSeconds());
		TestFalse(TEXT("Visual deadline blocks new movement"), Enemy->BeginNoiseInvestigation(Destination));
		Advance(31);
		Enemy->RefreshPerceptionGoal();
		const bool bExpected = Parameters == TEXT("CombinedHold");
		return TestEqual(TEXT("Visual hold is not added twice and movement still needs distance eligibility"), Enemy->BeginNoiseInvestigation(Destination), bExpected);
	}
	Advance(1);
	FVector Candidate(400, 0, 0);
	bool bExpected = true;
	if (Parameters == TEXT("MoveEqualInside")) Candidate = FVector(-809.9, 0, 0);
	if (Parameters == TEXT("MoveEqualBoundary")) Candidate = FVector(-810, 0, 0);
	if (Parameters == TEXT("MoveEqualOutside")) { Candidate = FVector(-810.1, 0, 0); bExpected = false; }
	if (Parameters == TEXT("MoveBenefitInside")) { Candidate = FVector(600.1, 0, 0); bExpected = false; }
	if (Parameters == TEXT("MoveBenefitBoundary")) Candidate = FVector(600, 0, 0);
	if (Parameters == TEXT("MovePositionInside")) { Candidate = Original + FVector(0, 99.9, 0); bExpected = false; }
	if (Parameters == TEXT("MovePositionBoundary")) Candidate = Original + FVector(0, 100, 0);
	if (Parameters == TEXT("SearchPositionInside")) { Candidate = FVector(99.9, 0, 0); bExpected = false; }
	if (Parameters == TEXT("SearchPositionBoundary")) Candidate = FVector(100, 0, 0);
	if (Parameters == TEXT("ConfigurableTolerance")) { Enemy->NoiseEqualDistanceTolerance = 20; Candidate = FVector(-815, 0, 0); }
	const double OccurredAt = Parameters == TEXT("SearchOlder") ? OriginalAt - 0.25 : Fixture.World->GetTimeSeconds();
	if (Parameters == TEXT("SearchOlder")) bExpected = false;
	TestTrue(TEXT("Candidate accepted without adopting it"), Enemy->RecordHeardNoise(Other, Candidate, OccurredAt));
	if (Parameters == TEXT("NearestBeforeNewest") || Parameters == TEXT("EqualNewest"))
	{
		Candidate = FVector(-405, 0, 0);
		if (Parameters == TEXT("EqualNewest")) Candidate = FVector(-400, 0, 0);
		Advance(1);
		TestTrue(TEXT("Second candidate is newer"), Enemy->RecordHeardNoise(Source, Candidate, Fixture.World->GetTimeSeconds()));
		if (Parameters == TEXT("NearestBeforeNewest")) Candidate = FVector(400, 0, 0);
		Advance(30);
	}
	else if (Parameters == TEXT("SearchExpired")) { Advance(96); bExpected = false; }
	else if (Parameters == TEXT("SearchHoldBefore")) { Advance(30); bExpected = false; }
	else if (Parameters == TEXT("SearchHoldAfter")) Advance(32);
	else if (Parameters == TEXT("CompletionRace")) Advance(160);
	else Advance(31);
	Enemy->RefreshPerceptionGoal();
	const uint64 SelectedSerial = Enemy->GetPerceptionGoal().NoiseClueSerial;
	if (Parameters == TEXT("CompletionRace"))
	{
		// 테스트 후보는 완료 직전에 새로 발생해야 한다. 오래된 후보의 수명을 늘리지 않는다.
		Enemy->RecordHeardNoise(Other, Candidate, Fixture.World->GetTimeSeconds());
		TestFalse(TEXT("Old completion yields to the new movement intent"), Enemy->CompleteNoiseInvestigation());
		TestEqual(TEXT("Completion race does not briefly return home"), Enemy->GetCombatState(), EEnemyCombatState::Combat);
	}
	const bool bAdopted = bSearch ? Enemy->BeginNoiseInvestigation(Destination) : Enemy->TryUpdateNoiseInvestigationDestination(Destination);
	TestEqual(TEXT("Stage and boundary conditions decide adoption"), bAdopted, bExpected);
	if (bAdopted)
	{
		TestTrue(TEXT("The selected occurrence becomes the actual destination"), Destination.Equals(Candidate, 0.01));
		if (Parameters != TEXT("CompletionRace")) TestEqual(TEXT("Goal identity is preserved through adoption"), Enemy->GetActiveNoiseInvestigationClue().Serial, SelectedSerial);
		TestFalse(TEXT("Another immediate destination change is blocked"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
		if (bSearch) TestFalse(TEXT("Old reached callback cannot overwrite replacement"), Enemy->BeginNoiseInvestigationSearch(Episode, Enemy->GetActorLocation()));
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseCompletionTest,
	"Outlier.Enemy.Perception.Investigation.Completion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseCompletionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("AlertReturn"), TEXT("CombatReturn"), TEXT("ActiveRoomGuard"), TEXT("NewNoise"), TEXT("ReacquiredTarget") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseCompletionTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchTestWorld Fixture(true);
	if (!TestNotNull(TEXT("Completion world exists"), Fixture.World)) return false;
	const FGameplayTag Room = NoiseCompletionRoomTag.GetTag();
	if (!TestTrue(TEXT("Completion uses a child Room with return-home semantics"), URoomTagComponent::IsValidChildRoomTag(Room))) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(Room, FVector::ZeroVector, Parameters != TEXT("AlertReturn"));
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(10000, 0, 0));
	if (!Enemy || !Controller || !TestNotNull(TEXT("Completion noise source exists"), Source)) return false;
	Controller->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	if (Parameters == TEXT("AlertReturn")) Enemy->EnterAlertFromPerception(FVector(100, 0, 0));
	if (Parameters == TEXT("ActiveRoomGuard"))
		Fixture.World->GetSubsystem<UEnemyRoomSubsystem>()->SetActiveRoomTargetForTesting(Room, FVector(100, 0, 0), false);
	// World의 최대 DeltaTime 제한을 피하면서 목적지 갱신 간격까지 실제 World 시간을 진행한다.
	auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		}
	};
	Advance(32);
	if (!TestTrue(TEXT("Completion starts from a player noise"), Enemy->RecordHeardNoise(Source, FVector(100, 0, 0), Fixture.World->GetTimeSeconds()))) return false;
	FVector Destination;
	if (!TestTrue(TEXT("Completion owns an investigation"), Enemy->BeginNoiseInvestigation(Destination))
		|| !TestTrue(TEXT("Completion begins reached-position search"), Enemy->BeginNoiseInvestigationSearch(
			Enemy->GetNoiseInvestigationSerial(), Enemy->GetActorLocation()))) return false;
	if (Parameters == TEXT("NewNoise"))
	{
		Advance(32);
		Enemy->SetActorLocation(FVector(-400, 0, 0));
		TestTrue(TEXT("Newer closer evidence arrives before completion"), Enemy->RecordHeardNoise(Source, FVector(-200, 0, 0), Fixture.World->GetTimeSeconds()));
		TestFalse(TEXT("Old completion cannot consume the new movement intent"), Enemy->CompleteNoiseInvestigation());
		TestEqual(TEXT("New evidence still requests movement"), Enemy->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::InvestigateNoise);
		return true;
	}
	if (Parameters == TEXT("ReacquiredTarget"))
	{
		Controller->SetCachedVisibleTargetForTesting(Source);
		Enemy->SetPlayerCurrentlyVisible(true);
		TestFalse(TEXT("Old completion cannot return a reacquired combatant home"), Enemy->CompleteNoiseInvestigation());
		TestEqual(TEXT("Reacquisition retains combat state"), Enemy->GetCombatState(), EEnemyCombatState::Combat);
		return true;
	}
	// 실제 기본 수색은 5초다. Alert의 최소 경계 4초 안에 완료를 강제해 귀환을 기대하지 않는다.
	if (Parameters == TEXT("AlertReturn")) Advance(160);
	FEnemySearchProgressTaskInstanceData Finish;
	Finish.Enemy = Enemy;
	Finish.Action = EEnemySearchProgress::FinishNoiseInvestigation;
	TestEqual(TEXT("Production progress task resolves completed investigation"), EnemySearchTesting::Progress(Finish), EStateTreeRunStatus::Succeeded);
	TestFalse(TEXT("Completion releases investigation ownership"), Enemy->IsNoiseInvestigationActive());
	TestFalse(TEXT("Completion releases local search reservations"), Enemy->GetSearchEpisode().bActive);
	// 도착 Room을 만들지 않은 fixture다. 유효한 소속 방이 미등록이면 귀환 상태로 등록 복구를 기다린다.
	TestEqual(TEXT("Only active Room combat prevents return"), Enemy->GetCombatState(),
		Parameters == TEXT("ActiveRoomGuard") ? EEnemyCombatState::Combat : EEnemyCombatState::Returning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHearingNotifyTest,
	"Outlier.Enemy.Perception.HearingNotify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHearingNotifyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture(true);
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Enemy = Fixture.SpawnEnemy(Room);
	AShooterCharacter* Source = Fixture.SpawnPlayer();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(Fixture.World);
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	if (!TestNotNull(TEXT("Noise source BP spawned"), Source) || !Enemy || !Controller || !TestNotNull(TEXT("Perception system exists"), Perception)
		|| !TestNotNull(TEXT("Room subsystem exists"), Rooms)) return false;
	Source->GetRoomTagComp()->AssignDefaultRoomTag(Room);
	Source->SetActorLocation(FVector(100, 0, 0));
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	Controller->Possess(Enemy);
	if (!TestTrue(TEXT("Hearing sense is registered"), UAISense::GetSenseID<UAISense_Hearing>().IsValid())
		|| !TestTrue(TEXT("Hearing listener is registered"), Controller->EnemyPerceptionComponent->GetListenerId().IsValid())) return false;
	Controller->EnemyPerceptionComponent->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	// native 테스트 Pawn에는 DataTable이 없다. 실제 청각 파이프라인의 거리만 명시한다.
	Controller->HearingConfig->HearingRange = 1000.0f;
	Controller->EnemyPerceptionComponent->ConfigureSense(*Controller->HearingConfig);
	Controller->EnemyPerceptionComponent->RequestStimuliListenerUpdate();
	TestEqual(TEXT("Emitter is in the active combat room"), Source->GetCurrentRoomTag(), Room);
	UAnimNotify_MakeNoise* Notify = NewObject<UAnimNotify_MakeNoise>();
	Notify->Loudness = 1.0f;
	Notify->Notify(Source->GetMesh(), nullptr, FAnimNotifyEventReference());
	// 콜백을 직접 부르지 않고 Notify → Hearing Sense → Perception 콜백까지 진행한다.
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		Perception->Tick(1.0f / 32.0f);
	}
	FEnemyHeardNoise Noise;
	TestTrue(TEXT("Notify is heard even while the room is in combat"), Enemy->TryGetHeardNoise(Noise));
	TestTrue(TEXT("Notify keeps the sound's occurrence location"), Noise.Location.Equals(Source->GetActorLocation()));
	TestFalse(TEXT("Hearing does not invent sight"), Enemy->IsPlayerCurrentlyVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchPingTimerTest,
	"Outlier.Enemy.Search.PingTimer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchPingTimerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Observer = Fixture.SpawnEnemy(Room);
	AEnemyBase* OtherObserver = Fixture.SpawnEnemy(Room, FVector(2000.0, 0.0, 0.0));
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms) || !Observer || !OtherObserver || !Target) return false;
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	// 실제 예약 타이머를 진행하되 3분을 실시간으로 기다리지 않는다.
	// TimerManager의 프레임 중복 방지용 카운터는 테스트 종료 시 원래 값으로 복구한다.
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	Rooms->ReportRoomTargetContact(OtherObserver, Target, FVector::ZeroVector);
	Rooms->RemoveRoomTargetObserver(Observer);
	Fixture.AdvanceTimers(181.0f);
	TestFalse(TEXT("One remaining direct observer prevents the timer"), Rooms->IsForcedShareConsumedForTesting(Room));
	Rooms->RemoveRoomTargetObserver(OtherObserver);
	Fixture.AdvanceTimers(0.0f);
	Fixture.AdvanceTimers(179.0f);
	TestFalse(TEXT("No ping before 180 seconds"), Rooms->IsForcedShareConsumedForTesting(Room));
	Fixture.AdvanceTimers(1.1f);
	TestTrue(TEXT("Actual scheduled timer sends ping after 180 seconds"), Rooms->IsForcedShareConsumedForTesting(Room));
	const uint64 Revision = Rooms->GetSharedRevisionForTesting(Room);
	Fixture.AdvanceTimers(360.0f);
	TestEqual(TEXT("Continued unseen time cannot send another ping"), Rooms->GetSharedRevisionForTesting(Room), Revision);

	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	Rooms->RemoveRoomTargetObserver(Observer);
	Fixture.AdvanceTimers(0.0f);
	Fixture.AdvanceTimers(100.0f);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	const uint64 RediscoveredRevision = Rooms->GetSharedRevisionForTesting(Room);
	Fixture.AdvanceTimers(100.0f);
	TestEqual(TEXT("Rediscovery cancels the old pending timer"), Rooms->GetSharedRevisionForTesting(Room), RediscoveredRevision);
	TestFalse(TEXT("Visible target does not consume a ping"), Rooms->IsForcedShareConsumedForTesting(Room));
	Rooms->RemoveRoomTargetObserver(Observer);
	Fixture.AdvanceTimers(0.0f);
	Fixture.AdvanceTimers(180.1f);
	TestTrue(TEXT("Next unseen period starts a full fresh timer"), Rooms->IsForcedShareConsumedForTesting(Room));
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	Rooms->RemoveRoomTargetObserver(Observer);
	Rooms->NotifyRoomCombatEnded(Room);
	Fixture.AdvanceTimers(0.0f);
	Fixture.AdvanceTimers(180.1f);
	TestEqual(TEXT("Room ending cancels pending timer and contact"), Rooms->GetSharedRevisionForTesting(Room), uint64(0));
	AddInfo(TEXT("[SearchScenario] PingTimer: virtual time; real TimerManager callbacks"));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchScenarioTest,
	"Outlier.Enemy.Search.Scenarios", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchScenarioTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Points0"), TEXT("Points1"), TEXT("Points2"), TEXT("Points4"),
		TEXT("FrozenCenter"), TEXT("FreshHint"), TEXT("DuplicateHint"),
		TEXT("LocalGuardRestart"), TEXT("SameFrameRestart"), TEXT("ConsecutiveHints"),
		TEXT("MovementFailure"), TEXT("Rediscovery"), TEXT("Interrupted"), TEXT("Possessed")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemySearchScenarioTest::RunTest(const FString& Parameters)
{
	AddInfo(FString::Printf(TEXT("[SearchScenario] Begin Scenario=%s Layer=TaskLogic"), *Parameters));
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Enemy = Fixture.SpawnEnemy(Room, FVector(0, 0, 350));
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	if (!TestNotNull(TEXT("Enemy exists"), Enemy) || !TestNotNull(TEXT("Room subsystem exists"), Rooms)) return false;
	FEnemySearchSessionTaskInstanceData Session;
	Session.Enemy = Enemy;
	FEnemySearchProgressTaskInstanceData Progress;
	Progress.Enemy = Enemy;
	const FVector First(600, 0, 350);
	const FVector Second(0, 600, 350);
	const FVector Hint(300, 200, 350);
	AEnemyAIController* RediscoveryController = nullptr;
	AShooterCharacter* RediscoveryTarget = nullptr;
	if (Parameters == TEXT("Rediscovery"))
	{
		RediscoveryController = Fixture.World->SpawnActor<AEnemyAIController>();
		RediscoveryTarget = Fixture.SpawnPlayer(Hint);
		if (!TestNotNull(TEXT("Rediscovery controller exists"), RediscoveryController)
			|| !TestNotNull(TEXT("Rediscovery uses a real player target"), RediscoveryTarget))
		{
			return false;
		}
		RediscoveryController->Possess(Enemy);
		// 각 단계의 재발견을 독립적으로 검증한다. 이 테스트에서는 추적 유예를 생략한다.
		const_cast<FEnemyStat&>(Enemy->GetRuntimeStat()).TargetTrackingGraceSeconds = 0.0f;
	}
	const bool bFreshOnEntry = Parameters == TEXT("FreshHint");
	if (bFreshOnEntry)
	{
		Enemy->ApplySharedTargetContact(-Hint, true, 10);
	}
	TestEqual(TEXT("Search parent stays running for child transitions"),
		EnemySearchTesting::EnterSession(Session), EStateTreeRunStatus::Running);
	TestEqual(TEXT("Only a fresh shared hint skips the loss wait"), Session.bSkipLossWait, bFreshOnEntry);
	const uint64 Serial = Enemy->GetSearchEpisode().Serial;

	// 0/1/2/4 지점은 동일한 진행 Task를 반복 호출한다. 이동 전 조회로 확인 횟수가 늘지 않아야 한다.
	if (Parameters.StartsWith(TEXT("Points")))
	{
		const int32 Count = Parameters == TEXT("Points4") ? 4
			: Parameters == TEXT("Points2") ? 2 : Parameters == TEXT("Points1") ? 1 : 0;
		Progress.Action = EEnemySearchProgress::AnchorArrived;
		TestEqual(TEXT("Anchor arrival succeeds"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
		TArray<FVector> Points;
		if (Count > 0)
		{
			Points.Add(First);
		}
		if (Count > 1)
		{
			Points.Add(Second);
		}
		if (Count > 2)
		{
			Points.Add(FVector(-600.0, 0.0, 350.0));
			Points.Add(FVector(0.0, -600.0, 350.0));
		}
		TestTrue(TEXT("Prevalidated points are stored once"), Enemy->SetSearchPoints(Points));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Progress.Action = EEnemySearchProgress::SelectCurrentPoint;
			TestEqual(TEXT("Selecting a current point succeeds"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
			TestEqual(TEXT("Destination output advances to the correct point"), Progress.Destination, Points[Index]);
			TestEqual(TEXT("Selection alone does not confirm arrival or LookAround"), Enemy->GetSearchEpisode().CheckedPoints.Num(), Index);
			EnemySearchTesting::Progress(Progress);
			TestEqual(TEXT("Repeated selection does not consume the point"), Progress.PointIndex, Index);
			Progress.Action = EEnemySearchProgress::PointChecked;
			TestEqual(TEXT("Confirmation after movement and LookAround succeeds"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
			TestEqual(TEXT("Exactly one point is confirmed"), Enemy->GetSearchEpisode().CheckedPoints.Num(), Index + 1);
		}
		Progress.Action = EEnemySearchProgress::SelectCurrentPoint;
		TestEqual(TEXT("Exhausted selection takes the failure-to-LocalGuard branch"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Failed);
		TestTrue(TEXT("All selected points lead to LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);
		TestFalse(TEXT("LocalGuard cannot replenish candidates"), Enemy->SetSearchPoints({First}));
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			EnemySearchTesting::TickSession(Session);
		}
		TestTrue(TEXT("LocalGuard remains active without new information"), Session.bLocalGuard);
		TestEqual(TEXT("Guard repetition cannot create extra confirmations"), Enemy->GetSearchEpisode().CheckedPoints.Num(), Count);
	}
	else if (Parameters == TEXT("FrozenCenter"))
	{
		const FVector Center = Session.SearchCenter;
		Enemy->UpdateLastKnownPlayerLocation(Hint);
		EnemySearchTesting::TickSession(Session);
		TestEqual(TEXT("Remembered position updates cannot move the frozen center"), Session.SearchCenter, Center);
		TestFalse(TEXT("Remembered position alone cannot request restart"), Session.bShouldRestart);
	}
	else if (Parameters == TEXT("FreshHint") || Parameters == TEXT("DuplicateHint")
		|| Parameters == TEXT("LocalGuardRestart") || Parameters == TEXT("SameFrameRestart")
		|| Parameters == TEXT("ConsecutiveHints"))
	{
		if (Parameters == TEXT("LocalGuardRestart"))
		{
			Progress.Action = EEnemySearchProgress::LocalGuard;
			EnemySearchTesting::Progress(Progress);
		}
		Enemy->ApplySharedTargetContact(Hint, true, 11);
		EnemySearchTesting::TickSession(Session);
		TestTrue(TEXT("A new revision requests restart even while guarding"), Session.bShouldRestart);
		Progress.Action = EEnemySearchProgress::RestartWithHint;
		TestEqual(TEXT("Restart child replaces the episode"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
		TestTrue(TEXT("Restart produces a new serial"), Enemy->GetSearchEpisode().Serial > Serial);
		TestEqual(TEXT("Restart pins the new shared center"), Enemy->GetSearchEpisode().Center, Hint);
		// 부모 Tick 없이 전이를 재평가한다. 이전에는 true가 남아 중복 RestartHint 실패로 이어졌다.
		TestTrue(TEXT("Parent tick output still contains the previous restart request"), Session.bShouldRestart);
		const FEnemySearchSessionTask SessionTask;
		TestTrue(TEXT("Session refresh participates in transition evaluation"), SessionTask.bShouldAffectTransitions);
		EnemySearchTesting::RefreshSessionTransitions(Session);
		TestFalse(TEXT("Restart request clears after consuming the revision into a new episode"), Session.bShouldRestart);
		TestFalse(TEXT("Restart does not enter LocalGuard"), Session.bLocalGuard);
		const uint64 RestartSerial = Enemy->GetSearchEpisode().Serial;
		EnemySearchTesting::RefreshSessionTransitions(Session);
		TestFalse(TEXT("Repeated same-frame evaluation cannot request another restart"), Session.bShouldRestart);
		TestEqual(TEXT("Transition refresh preserves the new episode"), Enemy->GetSearchEpisode().Serial, RestartSerial);
		if (Parameters == TEXT("ConsecutiveHints"))
		{
			const FVector NextHint(500, 400, 350);
			Enemy->ApplySharedTargetContact(NextHint, true, 12);
			EnemySearchTesting::RefreshSessionTransitions(Session);
			TestTrue(TEXT("A genuinely newer hint can restart in the same frame"), Session.bShouldRestart);
			TestEqual(TEXT("Next hint restarts successfully"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
			EnemySearchTesting::RefreshSessionTransitions(Session);
			TestFalse(TEXT("Next restart request is cleared before transitions"), Session.bShouldRestart);
			TestEqual(TEXT("Next hint becomes the new center"), Session.SearchCenter, NextHint);
		}
		if (Parameters == TEXT("DuplicateHint"))
		{
			Enemy->ApplySharedTargetContact(Hint, true, 11);
			EnemySearchTesting::TickSession(Session);
			TestFalse(TEXT("Duplicate revision does not request restart"), Session.bShouldRestart);
			TestEqual(TEXT("Forced duplicate restart fails"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Failed);
			TestEqual(TEXT("Duplicate information preserves serial"), Enemy->GetSearchEpisode().Serial, RestartSerial);
		}
	}
	else if (Parameters == TEXT("MovementFailure"))
	{
		Enemy->SetSearchPoints({First, Second});
		TestTrue(TEXT("A selected destination can be reserved"), Rooms->ReserveSearchDestinations(Enemy, {First, Second}, 200));
		// Fly 실패 전이가 호출하는 실제 LocalGuard 동작: 확인으로 간주하거나 두 번째 지점으로 건너뛰지 않는다.
		Progress.Action = EEnemySearchProgress::LocalGuard;
		TestEqual(TEXT("Movement failure enters LocalGuard"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
		TestEqual(TEXT("Failed movement is not a completed search point"), Enemy->GetSearchEpisode().CheckedPoints.Num(), 0);
		TestFalse(TEXT("Failure cannot refill selected candidates"), Enemy->SetSearchPoints({Hint}));
		AEnemyBase* Other = Fixture.SpawnEnemy(Room, FVector(2000, 0, 350));
		if (!TestNotNull(TEXT("Other drone exists"), Other)) return false;
		Other->BeginSearchEpisode();
		TestFalse(TEXT("Failed episode releases its reservation"), Rooms->IsSearchDestinationReserved(Other, First, 200));
	}
	else if (Parameters == TEXT("Rediscovery"))
	{
		// 마지막 위치 접근/지점 이동/LocalGuard 각각에서 발견해도 자식은 더 진행하지 않는다.
		for (int32 Phase = 0; Phase < 3; ++Phase)
		{
			if (Phase > 0)
			{
				RediscoveryController->SetCachedVisibleTargetForTesting(nullptr);
				Enemy->SetPlayerCurrentlyVisible(false);
				Enemy->RefreshTargetTracking();
				TestEqual(TEXT("Sight loss starts the next search phase"),
					EnemySearchTesting::EnterSession(Session), EStateTreeRunStatus::Running);
			}
			if (Phase == 1)
			{
				Enemy->SetSearchPoints({First, Second});
			}
			if (Phase == 2)
			{
				Enemy->EnterSearchLocalGuard();
			}
			TestTrue(TEXT("Each phase has an active search before reacquisition"), Enemy->GetSearchEpisode().bActive);
			// 가시 bool만으로는 직접 시야가 성립하지 않는다. 실제 관측 대상과 Controller 캐시를 함께 갱신한다.
			RediscoveryController->SetCachedVisibleTargetForTesting(RediscoveryTarget);
			Enemy->ObserveTrackedTarget(RediscoveryTarget);
			Enemy->SetPlayerCurrentlyVisible(true);
			TestTrue(TEXT("Reacquisition has validated direct sight"), Enemy->IsPlayerCurrentlyVisible());
			TestFalse(TEXT("Rediscovery cancels the active search episode"), Enemy->GetSearchEpisode().bActive);
			TestEqual(TEXT("Parent remains running so the sight transition has priority"), EnemySearchTesting::TickSession(Session), EStateTreeRunStatus::Running);
			Progress.Action = EEnemySearchProgress::SelectCurrentPoint;
			TestEqual(TEXT("No hidden-target child can continue after rediscovery"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Failed);
		}
	}
	else if (Parameters == TEXT("Interrupted") || Parameters == TEXT("Possessed"))
	{
		Enemy->SetSearchPoints({First});
		Rooms->ReserveSearchDestinations(Enemy, {First}, 200);
		if (Parameters == TEXT("Possessed")) Enemy->SetEnemyPossessed(true);
		else Enemy->EnterStun();
		TestEqual(TEXT("Stun/possession interrupts the search parent"), EnemySearchTesting::TickSession(Session), EStateTreeRunStatus::Failed);
		EnemySearchTesting::ExitSession(Session);
		TestFalse(TEXT("Parent exit clears the search episode"), Enemy->GetSearchEpisode().bActive);
		AEnemyBase* Other = Fixture.SpawnEnemy(Room, FVector(2000, 0, 350));
		if (!TestNotNull(TEXT("Other drone exists"), Other)) return false;
		Other->BeginSearchEpisode();
		TestFalse(TEXT("Interrupt releases the reservation"), Rooms->IsSearchDestinationReserved(Other, First, 200));
	}
	else
	{
		AddError(FString::Printf(TEXT("Unknown search scenario: %s"), *Parameters));
	}
	EnemySearchTesting::ExitSession(Session);
	TestFalse(TEXT("Scenario teardown ends the episode"), Enemy->GetSearchEpisode().bActive);
	AddInfo(FString::Printf(TEXT("[SearchScenario] End Scenario=%s; assertions determine PASS/FAIL"), *Parameters));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyTrackingGraceTest,
	"Outlier.Enemy.Tracking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyTrackingGraceTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Expiry"), TEXT("Reacquire"), TEXT("SharedContact"),
		TEXT("ZeroDuration"), TEXT("DestroyedTarget"), TEXT("ControllerChanged"), TEXT("Stun"),
		TEXT("VisibleUnavailable"), TEXT("GraceUnavailable"), TEXT("VisibleDead"), TEXT("GraceDead"),
		TEXT("DeathCleanup"), TEXT("PoolReuse"), TEXT("StunVisible")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyTrackingGraceTest::RunTest(const FString& Parameters)
{
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	Fixture.World->CreateAISystem();
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag());
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	UClass* TargetClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Enemy exists"), Enemy) || !TestNotNull(TEXT("Controller exists"), Controller)
		|| !TestNotNull(TEXT("Concrete player class exists"), TargetClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Target = Fixture.World->SpawnActor<AShooterCharacter>(TargetClass,
		FTransform(FVector(500, 100, 200)), Spawn);
	if (!TestNotNull(TEXT("Player target exists"), Target)) return false;
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	Controller->Possess(Enemy);
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Enemy->GetRuntimeStat());
	TestEqual(TEXT("Tracking defaults to three seconds"), Stat.TargetTrackingGraceSeconds, 3.0f);
	if (Parameters == TEXT("ZeroDuration")) Stat.TargetTrackingGraceSeconds = 0.0f;
	auto Sight = [&](bool bVisible)
	{
		Controller->SetCachedVisibleTargetForTesting(bVisible ? Target : nullptr);
		if (bVisible) Enemy->ObserveTrackedTarget(Target);
		Enemy->SetPlayerCurrentlyVisible(bVisible);
	};
	auto Advance = [&](float Seconds)
	{
		// 최초 Tick은 대기 타이머를 활성화한다. 이후 월드 시각과 타이머를 함께 진행해야
		// 고정 만료 시각과 만료 콜백을 같은 시간축에서 검증할 수 있다.
		Fixture.AdvanceTimers(0.0f);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds * 60.0f));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			// TimeOnly는 감지/이동 Actor를 실행하지 않으며 TimerManager도 별도로 갱신해야 한다.
			Fixture.World->Tick(LEVELTICK_TimeOnly, Seconds / Steps);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
			Enemy->RefreshTargetTracking();
		}
	};
	Sight(true);
	if (!TestEqual(TEXT("Direct sight chooses the player"), Enemy->GetTrackedTarget(), static_cast<AActor*>(Target))) return false;
	if (Parameters == TEXT("StunVisible"))
	{
		Enemy->EnterStun();
		Enemy->RefreshTargetTracking();
		TestTrue(TEXT("Stun preserves actual sight"), Enemy->IsPlayerCurrentlyVisible());
		TestFalse(TEXT("Stun suspends tracking"), Enemy->HasTrackableTarget());
		Enemy->RestoreStateAfterStun();
		Enemy->RefreshTargetTracking();
		TestEqual(TEXT("Recovery reuses valid direct sight without a new perception callback"), Enemy->GetTrackedTarget(), static_cast<AActor*>(Target));
		return true;
	}
	if (Parameters == TEXT("VisibleUnavailable") || Parameters == TEXT("VisibleDead"))
	{
		Target->GetAbilitySystemComponent()->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(
			Parameters == TEXT("VisibleDead") ? TEXT("State.Dead") : TEXT("State.Stealthed")));
		Enemy->RefreshTargetTracking();
		TestFalse(TEXT("Unavailable visible player is not tracked"), Enemy->HasTrackableTarget());
		TestFalse(TEXT("Unavailable visible player clears direct visibility"), Enemy->IsPlayerCurrentlyVisible());
		return true;
	}
	Sight(false);
	TestFalse(TEXT("Grace never fakes direct visibility"), Enemy->IsPlayerCurrentlyVisible());
	if (Parameters == TEXT("ZeroDuration"))
	{
		TestFalse(TEXT("Zero grace ends tracking immediately"), Enemy->HasTrackableTarget());
		TestTrue(TEXT("Zero grace skips an additional search wait"), Enemy->ShouldSkipTrackingLossWait());
		return true;
	}
	TestEqual(TEXT("Sight loss retains the same player"), Enemy->GetTrackedTarget(), static_cast<AActor*>(Target));
	TestFalse(TEXT("Search cannot start during grace"), Enemy->BeginSearchEpisode());
	if (Parameters == TEXT("GraceUnavailable") || Parameters == TEXT("GraceDead"))
	{
		Target->GetAbilitySystemComponent()->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(
			Parameters == TEXT("GraceDead") ? TEXT("State.Dead") : TEXT("State.Stealthed")));
		Enemy->RefreshTargetTracking();
		TestFalse(TEXT("Grace cannot bypass target stealth"), Enemy->HasTrackableTarget());
		return true;
	}
	if (Parameters == TEXT("PoolReuse"))
	{
		Enemy->SetPoolPresentationAutoCompleteForTesting(false);
		Enemy->PrepareForPoolSpawn(Fixture.World->GetSubsystem<UEnemyPoolSubsystem>());
		FEnemyPoolLeaseContext Lease;
		Lease.GameplayGeneration = 1;
		Lease.RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		if (!TestTrue(TEXT("New pool lease starts"), Enemy->BeginPoolLease(Lease, FTransform(FVector(2000, 0, 300)), 1))) return false;
		Advance(4.0f);
		TestFalse(TEXT("New lease cannot retain the previous tracked player"), Enemy->HasTrackableTarget());
		TestFalse(TEXT("Old expiry cannot request search in the new lease"), Enemy->ShouldSkipTrackingLossWait());
		return true;
	}
	if (Parameters == TEXT("DeathCleanup"))
	{
		Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
		Advance(4.0f);
		TestFalse(TEXT("Death clears tracking"), Enemy->HasTrackableTarget());
		TestFalse(TEXT("Old tracking timer cannot request search after death"), Enemy->ShouldSkipTrackingLossWait());
		return true;
	}
	Target->SetActorLocation(FVector(700, -200, 400));
	Advance(1.0f);
	TestTrue(TEXT("Grace follows the current three-dimensional position"),
		Enemy->GetLastKnownPlayerLocation().Equals(Target->GetActorLocation()));
	if (Parameters == TEXT("ControllerChanged"))
	{
		Controller->UnPossess();
		Advance(4.0f);
		TestFalse(TEXT("Unpossess clears tracking"), Enemy->HasTrackableTarget());
		TestFalse(TEXT("Old timer cannot request search after unpossess"), Enemy->ShouldSkipTrackingLossWait());
		return true;
	}
	if (Parameters == TEXT("Stun"))
	{
		Enemy->EnterStun();
		Advance(4.0f);
		TestFalse(TEXT("Stun clears tracking"), Enemy->HasTrackableTarget());
		TestFalse(TEXT("Old tracking timer cannot request search during stun"), Enemy->ShouldSkipTrackingLossWait());
		return true;
	}
	if (Parameters == TEXT("DestroyedTarget"))
	{
		const FVector LastValid = Enemy->GetLastKnownPlayerLocation();
		Target->Destroy();
		Enemy->RefreshTargetTracking();
		TestFalse(TEXT("Destroyed player cannot remain tracked"), Enemy->HasTrackableTarget());
		TestTrue(TEXT("Invalidation preserves the last valid position"), Enemy->GetLastKnownPlayerLocation().Equals(LastValid));
		return true;
	}
	if (Parameters == TEXT("Reacquire"))
	{
		Sight(true);
		Advance(3.0f);
		TestTrue(TEXT("Old expiry cannot end reacquired sight"), Enemy->HasTrackableTarget());
		Sight(false);
		Advance(2.9f);
		TestTrue(TEXT("A new sight loss receives a fresh grace period"), Enemy->HasTrackableTarget());
	}
	else
	{
		if (Parameters == TEXT("SharedContact"))
		{
			Enemy->ApplySharedTargetContact(FVector(-900, 800, 100), true, 100);
			TestFalse(TEXT("Shared information during tracking is already consumed"), Enemy->HasFreshSharedTargetLocation());
			TestTrue(TEXT("Shared information cannot replace direct tracking"),
				Enemy->GetLastKnownPlayerLocation().Equals(Target->GetActorLocation()));
		}
		Advance(1.9f);
		TestTrue(TEXT("Tracking remains valid before expiry"), Enemy->HasTrackableTarget());
	}
	Target->SetActorLocation(FVector(800, -300, 450));
	Advance(0.2f);
	TestFalse(TEXT("The fixed deadline ends tracking"), Enemy->HasTrackableTarget());
	const FVector Frozen = Enemy->GetLastKnownPlayerLocation();
	TestTrue(TEXT("Expiry captures the last tracked position"), Frozen.Equals(Target->GetActorLocation()));
	Target->SetActorLocation(FVector(1200, 900, 600));
	Advance(1.0f);
	TestTrue(TEXT("Hidden player movement after expiry is not observed"), Enemy->GetLastKnownPlayerLocation().Equals(Frozen));
	FEnemySearchSessionTaskInstanceData Session;
	Session.Enemy = Enemy;
	TestEqual(TEXT("Expiry can enter search"), EnemySearchTesting::EnterSession(Session), EStateTreeRunStatus::Running);
	TestTrue(TEXT("Expiry bypasses LossWait"), Session.bSkipLossWait);
	TestTrue(TEXT("Search uses the frozen tracking center"), Enemy->GetSearchEpisode().Center.Equals(Frozen));
	EnemySearchTesting::ExitSession(Session);
	return true;
}

// 공유 대상 선택/공격과 분리해 관측 데이터의 출처와 시간 경계만 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomObservationTargetsTest,
	"Outlier.Enemy.RoomObservation.Targets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomObservationTargetsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* First = Fixture.SpawnEnemy(Room);
	AEnemyBase* Second = Fixture.SpawnEnemy(Room, FVector(2000, 0, 0));
	AEnemyBase* Receiver = Fixture.SpawnEnemy(Room, FVector(4000, 0, 0));
	AActor* TargetA = Fixture.World->SpawnActor<AActor>();
	AActor* TargetB = Fixture.World->SpawnActor<AActor>();
	if (!Rooms || !First || !Second || !Receiver || !TargetA || !TargetB) return false;
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	Rooms->ReportRoomTargetContact(First, TargetA, FVector(100, 0, 0));
	Rooms->ReportRoomTargetContact(Second, TargetB, FVector(200, 0, 0));
	FEnemyRoomTargetObservationSnapshot SnapshotA, SnapshotB;
	if (!TestTrue(TEXT("First target retained"), Rooms->TryGetRoomTargetObservation(Receiver, TargetA, SnapshotA))
		|| !TestTrue(TEXT("Second target retained"), Rooms->TryGetRoomTargetObservation(Receiver, TargetB, SnapshotB))) return false;
	TestTrue(TEXT("Both targets independently observed"), SnapshotA.bDirectlyObserved && SnapshotB.bDirectlyObserved);
	TestTrue(TEXT("First location preserved"), SnapshotA.LastObservedLocation.Equals(FVector(100, 0, 0)));
	TestTrue(TEXT("Second location preserved"), SnapshotB.LastObservedLocation.Equals(FVector(200, 0, 0)));
	const uint64 Revision = Rooms->GetSharedRevisionForTesting(Room);
	// 좌표 공유의 50cm 필터를 통과하지 않아도 정지한 대상의 직접 관측은 최신이어야 한다.
	++GFrameCounter;
	Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f);
	Rooms->ReportRoomTargetContact(First, TargetA, FVector(110, 0, 0));
	TestEqual(TEXT("Small movement does not republish legacy coordinates"), Rooms->GetSharedRevisionForTesting(Room), Revision);
	TestTrue(TEXT("Updated observation available"), Rooms->TryGetRoomTargetObservation(Receiver, TargetA, SnapshotA));
	TestEqual(TEXT("Direct report uses server time"), SnapshotA.LastObservedAt, static_cast<double>(Fixture.World->GetTimeSeconds()));
	TestTrue(TEXT("Observation retains exact reported location"), SnapshotA.LastObservedLocation.Equals(FVector(110, 0, 0)));
	Rooms->RemoveRoomTargetObserver(First);
	TestTrue(TEXT("First target enters grace"), Rooms->TryGetRoomTargetObservation(Receiver, TargetA, SnapshotA));
	TestFalse(TEXT("Other target observer does not keep first target directly observed"), SnapshotA.bDirectlyObserved);
	TestTrue(TEXT("Other target remains directly observed"), Rooms->TryGetRoomTargetObservation(Receiver, TargetB, SnapshotB) && SnapshotB.bDirectlyObserved);
	Rooms->NotifyTargetActorRemoved(TargetA);
	TestFalse(TEXT("Removed representative target unavailable"), Rooms->TryGetRoomTargetObservation(Receiver, TargetA, SnapshotA));
	TestTrue(TEXT("Removing representative preserves other target record"), Rooms->TryGetRoomTargetObservation(Receiver, TargetB, SnapshotB));
	Rooms->NotifyRoomCombatEnded(Room);
	TestFalse(TEXT("Combat end clears records"), Rooms->TryGetRoomTargetObservation(Receiver, TargetB, SnapshotB));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomObservationExpiryTest,
	"Outlier.Enemy.RoomObservation.Expiry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomObservationExpiryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Observer = Fixture.SpawnEnemy(Room);
	AEnemyBase* OtherObserver = Fixture.SpawnEnemy(Room, FVector(2000, 0, 0));
	AEnemyBase* Receiver = Fixture.SpawnEnemy(Room, FVector(4000, 0, 0));
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!Rooms || !Observer || !OtherObserver || !Receiver || !Target) return false;
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	FEnemyRoomTargetObservationSnapshot Snapshot;
	auto Advance = [&](float Seconds)
	{
		// 큰 DeltaSeconds는 WorldSettings에서 제한된다. 이진수로 정확한 간격을 사용해
		// 월드 시각을 실제로 진행하고 3초 만료 경계도 오차 없이 검증한다.
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds * 32.0f));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, Seconds / Steps);
		}
	};
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	Advance(1.0f);
	Rooms->ReportRoomTargetContact(OtherObserver, Target, FVector::ZeroVector);
	Rooms->RemoveRoomTargetObserver(Observer);
	TestTrue(TEXT("Observer handoff remains direct"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot) && Snapshot.bDirectlyObserved);
	const double LastObserved = Snapshot.LastObservedAt;
	Advance(1.0f);
	Rooms->RemoveRoomTargetObserver(OtherObserver);
	TestTrue(TEXT("Delayed removal retains remaining grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestEqual(TEXT("Removal does not refresh observation time"), Snapshot.LastObservedAt, LastObserved);
	TestEqual(TEXT("Grace anchored to last direct report"), Snapshot.TrackingExpiresAt, LastObserved + 3.0);
	const double Deadline = Snapshot.TrackingExpiresAt;
	Target->SetActorLocation(FVector(900, 0, 0));
	Rooms->TriggerForcedShareForTesting(Room);
	TestTrue(TEXT("Legacy forced ping executed"), Rooms->IsForcedShareConsumedForTesting(Room));
	for (int32 Read = 0; Read < 3; ++Read)
	{
		TestTrue(TEXT("Repeated read still within original grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
		TestEqual(TEXT("Read and forced ping preserve deadline"), Snapshot.TrackingExpiresAt, Deadline);
		TestTrue(TEXT("Forced ping does not overwrite observed location"), Snapshot.LastObservedLocation.IsZero());
	}
	Advance(2.0f);
	TestFalse(TEXT("Exactly three seconds since last observation expires"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestFalse(TEXT("Failed query clears target output"), Snapshot.TargetActor.IsValid());
	TestEqual(TEXT("Failed query clears deadline output"), Snapshot.TrackingExpiresAt, 0.0);
	Rooms->ReportRoomTargetContact(Observer, Target, Target->GetActorLocation());
	TestTrue(TEXT("Genuine rediscovery renews observation"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestTrue(TEXT("Rediscovery advances time"), Snapshot.LastObservedAt > LastObserved);
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Receiver->GetRuntimeStat());
	Stat.TargetTrackingGraceSeconds = 0.0f;
	TestTrue(TEXT("Zero grace still permits active direct observation"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	Rooms->RemoveRoomTargetObserver(Observer);
	TestFalse(TEXT("Zero grace expires immediately after loss"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	Stat.TargetTrackingGraceSeconds = 5.0f;
	TestTrue(TEXT("Receiver configuration controls grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestEqual(TEXT("Configured duration uses same observation time"), Snapshot.TrackingExpiresAt, Snapshot.LastObservedAt + 5.0);
	Rooms->ResetRuntimeCombatState();
	TestFalse(TEXT("Runtime reset clears all records"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomObservationLifecycleTest,
	"Outlier.Enemy.RoomObservation.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomObservationLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	AEnemyBase* Observer = Fixture.SpawnEnemy(Room);
	AEnemyBase* Receiver = Fixture.SpawnEnemy(Room, FVector(2000, 0, 0));
	AEnemyBase* Outsider = Fixture.SpawnEnemy(FGameplayTag(), FVector(4000, 0, 0));
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!Rooms || !Observer || !Receiver || !Outsider || !Target) return false;
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	FEnemyRoomTargetObservationSnapshot Snapshot;
	TestFalse(TEXT("Different room cannot read observation"), Rooms->TryGetRoomTargetObservation(Outsider, Target, Snapshot));
	Rooms->UnregisterEnemy(Observer);
	TestTrue(TEXT("Unregister leaves bounded grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestFalse(TEXT("Unregistered observer no longer direct"), Snapshot.bDirectlyObserved);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	Observer->GetRoomTagComp()->AssignDefaultRoomTag(FGameplayTag());
	TestTrue(TEXT("Room departure leaves bounded grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestFalse(TEXT("Observer from different room is removed"), Snapshot.bDirectlyObserved);
	Observer->GetRoomTagComp()->AssignDefaultRoomTag(Room);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	Observer->EnterStun();
	TestTrue(TEXT("Stunned observer leaves bounded grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestFalse(TEXT("Stunned observer is not a direct source"), Snapshot.bDirectlyObserved);
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	TestTrue(TEXT("Stun rejects new direct report"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot) && !Snapshot.bDirectlyObserved);
	Observer->RestoreStateAfterStun();
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	Observer->SetEnemyPossessed(true);
	TestTrue(TEXT("Possessed observer leaves bounded grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	TestFalse(TEXT("Possessed observer is not a direct source"), Snapshot.bDirectlyObserved);
	// 사망/은신으로 무효화된 기록은 상태가 풀려도 새 직접 보고 없이 되살아나지 않는다.
	AEnemyBase* TaggedTarget = Fixture.SpawnEnemy(Room, FVector(6000, 0, 0));
	if (!TaggedTarget || !TaggedTarget->GetAbilitySystemComponent()) return false;
	Observer->SetEnemyPossessed(false);
	for (const TCHAR* TagName : {TEXT("State.Dead"), TEXT("State.Stealthed")})
	{
		Rooms->ReportRoomTargetContact(Observer, TaggedTarget, TaggedTarget->GetActorLocation());
		TestTrue(TEXT("Fresh target observation available"), Rooms->TryGetRoomTargetObservation(Receiver, TaggedTarget, Snapshot));
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName));
		TaggedTarget->GetAbilitySystemComponent()->AddLooseGameplayTag(Tag);
		TestFalse(TEXT("Unavailable target immediately rejected"), Rooms->TryGetRoomTargetObservation(Receiver, TaggedTarget, Snapshot));
		TaggedTarget->GetAbilitySystemComponent()->RemoveLooseGameplayTag(Tag);
		TestFalse(TEXT("Availability alone cannot restore discarded observation"), Rooms->TryGetRoomTargetObservation(Receiver, TaggedTarget, Snapshot));
	}
	Target->Destroy();
	TestFalse(TEXT("Destroyed target immediately unavailable"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomObservationPoolReuseTest,
	"Outlier.Enemy.RoomObservation.PoolReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomObservationPoolReuseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	UEnemyPoolSubsystem* Pool = Fixture.World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!Rooms || !Pool) return false;
	UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(Fixture.World);
	FEnemyPoolEntry& Entry = Definition->Entries.AddDefaulted_GetRef();
	Entry.EnemyClass = AEnemyBase::StaticClass();
	Entry.PrewarmCount = 1;
	Entry.MaxCount = 1;
	if (!TestTrue(TEXT("Observation pool prewarms"), Pool->PrewarmPool(Definition))) return false;
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	AEnemyBase* Receiver = Fixture.SpawnEnemy(Room, FVector(3000, 0, 0));
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	FEnemyPoolLeaseContext Context;
	Context.RoomTag = Room;
	Context.GameplayGeneration = 10;
	AEnemyBase* Observer = Pool->LeaseEnemy(AEnemyBase::StaticClass(), FTransform::Identity, Context);
	if (!Observer || !Receiver || !Target) return false;
	Rooms->ReportRoomTargetContact(Observer, Target, FVector::ZeroVector);
	FEnemyRoomTargetObservationSnapshot Snapshot;
	TestTrue(TEXT("Current lease directly observed"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot) && Snapshot.bDirectlyObserved);
	if (!TestTrue(TEXT("First lease returned"), Pool->ReturnEnemy(Observer,
		Observer->GetPoolGameplayGeneration(), Observer->GetPoolLeaseSerial()))) return false;
	TestTrue(TEXT("Return retains only bounded grace"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot) && !Snapshot.bDirectlyObserved);
	Context.GameplayGeneration = 11;
	AEnemyBase* Reused = Pool->LeaseEnemy(AEnemyBase::StaticClass(), FTransform::Identity, Context);
	if (!TestNotNull(TEXT("Second lease exists"), Reused)) return false;
	TestEqual(TEXT("Same Actor reused"), Reused, Observer);
	TestTrue(TEXT("New lease cannot inherit direct observation"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot) && !Snapshot.bDirectlyObserved);
	Rooms->ReportRoomTargetContact(Reused, Target, FVector::ZeroVector);
	TestTrue(TEXT("New lease requires a genuine report"), Rooms->TryGetRoomTargetObservation(Receiver, Target, Snapshot) && Snapshot.bDirectlyObserved);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyRoomTrackingTest,
	"Outlier.Enemy.RoomTracking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyRoomTrackingTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Acquire"), TEXT("LiveObserver"), TEXT("Expiry"), TEXT("LateAcquire"), TEXT("ZeroGrace"),
		TEXT("KeepCurrent"), TEXT("Priority"), TEXT("Alternative"), TEXT("DirectTransition"), TEXT("RoomEnded"), TEXT("RoomChanged")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyRoomTrackingTest::RunTest(const FString& Parameters)
{
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	Fixture.World->CreateAISystem();
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	AEnemyBase* Observer = Fixture.SpawnEnemy(Room, FVector(3000, 0, 300));
	AEnemyBase* OtherObserver = Fixture.SpawnEnemy(Room, FVector(4000, 0, 300));
	AEnemyBase* Receiver = Fixture.SpawnEnemy(Room, FVector(0, 0, 300));
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	UClass* TargetClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!Rooms || !Observer || !OtherObserver || !Receiver || !Controller || !TargetClass) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Target = Fixture.World->SpawnActor<ACharacter>(
		Parameters == TEXT("Priority") ? APartnerCharacter::StaticClass() : TargetClass,
		FTransform(FVector(500, 0, 300)), Spawn);
	AShooterCharacter* Other = Fixture.World->SpawnActor<AShooterCharacter>(TargetClass,
		FTransform(FVector(700, 100, 300)), Spawn);
	if (!TestNotNull(TEXT("Shared player exists"), Target) || !TestNotNull(TEXT("Alternative player exists"), Other)) return false;
	Controller->Possess(Receiver);
	if (Parameters == TEXT("ZeroGrace"))
		const_cast<FEnemyStat&>(Receiver->GetRuntimeStat()).TargetTrackingGraceSeconds = 0.0f;
	Receiver->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	Other->GetCharacterMovement()->SetComponentTickEnabled(false);
	Rooms->SetActiveRoomTargetForTesting(Room, Target->GetActorLocation(), false);
	Rooms->ReportRoomTargetContact(Observer, Target, Target->GetActorLocation());
	if (Parameters == TEXT("LateAcquire"))
	{
		Rooms->RemoveRoomTargetObserver(Observer);
		// 수신자가 늦게 합류해도 마지막 직접 보고 이후 남은 1초만 사용해야 한다.
		for (int32 Step = 0; Step < 64; ++Step)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		}
	}
	Receiver->RefreshTargetTracking();
	if (!TestEqual(TEXT("Never-seen shared player becomes the live target"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target))) return false;
	TestFalse(TEXT("Sharing does not fabricate direct sight"), Receiver->IsPlayerCurrentlyVisible());
	TestFalse(TEXT("Shared live tracking blocks search"), Receiver->BeginSearchEpisode());
	auto Advance = [&](float Seconds)
	{
		Fixture.AdvanceTimers(0.0f);
		const int32 Steps = FMath::RoundToInt(Seconds * 32.0f);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
			Receiver->RefreshTargetTracking();
		}
	};
	if (Parameters == TEXT("ZeroGrace") || Parameters == TEXT("LateAcquire"))
	{
		Rooms->RemoveRoomTargetObserver(Observer);
		if (Parameters == TEXT("LateAcquire")) Advance(1.0f);
		else Receiver->RefreshTargetTracking();
		TestNull(TEXT("Zero or partially consumed grace cannot restart on receipt"), Receiver->GetTrackedTarget());
		TestTrue(TEXT("Expired shared tracking skips another search wait"), Receiver->ShouldSkipTrackingLossWait());
	}
	else if (Parameters == TEXT("Acquire") || Parameters == TEXT("LiveObserver"))
	{
		Target->SetActorLocation(FVector(600, -100, 200));
		Advance(Parameters == TEXT("LiveObserver") ? 4.0f : 0.25f);
		TestEqual(TEXT("Live observation remains trackable"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
		TestTrue(TEXT("Tracking uses actual 3D position rather than the old reported coordinate"),
			Receiver->GetLastKnownPlayerLocation().Equals(Target->GetActorLocation()));
	}
	else if (Parameters == TEXT("KeepCurrent") || Parameters == TEXT("Alternative") || Parameters == TEXT("Priority"))
	{
		// 후보 수신은 즉시 교체가 아니다. 이후의 안전한 선택 경계에서 유지/거리/우선순위를 적용한다.
		if (Parameters != TEXT("Priority")) Other->SetActorLocation(FVector(100, 100, 300));
		Rooms->ReportRoomTargetContact(OtherObserver, Other, Other->GetActorLocation());
		Controller->SetCachedVisibleTargetForTesting(Other);
		Receiver->SetPlayerCurrentlyVisible(true);
		TestEqual(TEXT("A new visible/shared candidate cannot steal the current target"),
			Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
		if (Parameters == TEXT("Priority"))
		{
			Receiver->RefreshTargetTrackingAtSelectionBoundary();
			TestEqual(TEXT("At a safe boundary, Shooter priority replaces a still-valid closer Partner"),
				Receiver->GetTrackedTarget(), static_cast<AActor*>(Other));
		}
		if (Parameters == TEXT("KeepCurrent"))
		{
			Advance(1.0f);
			Receiver->RefreshTargetTrackingAtSelectionBoundary();
			TestEqual(TEXT("After the hold, the better shared candidate can replace a still-valid target"),
				Receiver->GetTrackedTarget(), static_cast<AActor*>(Other));
		}
		if (Parameters == TEXT("Alternative"))
		{
			Rooms->RemoveRoomTargetObserver(Observer);
			Advance(3.0f);
			TestEqual(TEXT("Expiry chooses the remaining valid target before search"),
				Receiver->GetTrackedTarget(), static_cast<AActor*>(Other));
			TestFalse(TEXT("Alternative target avoids a search-expiry transition"), Receiver->ShouldSkipTrackingLossWait());
		}
	}
	else if (Parameters == TEXT("DirectTransition"))
	{
		Controller->SetCachedVisibleTargetForTesting(Target);
		Receiver->SetPlayerCurrentlyVisible(true);
		Rooms->RemoveRoomTargetObserver(Observer);
		Advance(4.0f);
		TestEqual(TEXT("Own direct sight survives the old shared deadline"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
		Rooms->ReportRoomTargetContact(Observer, Target, Target->GetActorLocation());
		Controller->SetCachedVisibleTargetForTesting(nullptr);
		Receiver->SetPlayerCurrentlyVisible(false);
		Advance(4.0f);
		TestEqual(TEXT("Direct-to-shared transition keeps the same target"), Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
		TestFalse(TEXT("Shared continuation keeps actual lost sight"), Receiver->IsPlayerCurrentlyVisible());
	}
	else if (Parameters == TEXT("RoomEnded") || Parameters == TEXT("RoomChanged"))
	{
		if (Parameters == TEXT("RoomEnded")) Rooms->NotifyRoomCombatEnded(Room);
		else Receiver->GetRoomTagComp()->AssignDefaultRoomTag(FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.2")));
		TestNull(TEXT("Old Room permission is rejected even before the next tracking refresh"), Receiver->GetTrackedTarget());
		const FVector LastValid = Receiver->GetLastKnownPlayerLocation();
		Target->SetActorLocation(FVector(900, 200, 100));
		Receiver->RefreshTargetTracking();
		TestNull(TEXT("Old Room observation cannot become local grace"), Receiver->GetTrackedTarget());
		TestTrue(TEXT("Room invalidation freezes the last valid position"), Receiver->GetLastKnownPlayerLocation().Equals(LastValid));
	}
	else
	{
		Rooms->RemoveRoomTargetObserver(Observer);
		Advance(2.0f);
		// 180초 강제 핑과 좌표 수신은 대상의 직접 관측 시각을 갱신할 수 없다.
		Rooms->TriggerForcedShareForTesting(Room);
		Receiver->ApplySharedTargetContact(FVector(900, 0, 300), true, 10000);
		Target->SetActorLocation(FVector(600, 0, 200));
		Advance(0.5f);
		Target->SetActorLocation(FVector(900, 0, 200));
		Advance(0.46875f);
		const FVector LastValid = Receiver->GetLastKnownPlayerLocation();
		Target->SetActorLocation(FVector(1000, 0, 100));
		// 마지막 유효 갱신 뒤 정확히 만료 경계까지 진행해 만료 콜백의 좌표 누출도 검사한다.
		Advance(1.0f / 32.0f);
		TestNull(TEXT("Received or relayed information cannot extend the original deadline"), Receiver->GetTrackedTarget());
		const FVector Frozen = Receiver->GetLastKnownPlayerLocation();
		TestFalse(TEXT("No stale observation is re-adopted"), Receiver->HasTrackableTarget());
		TestTrue(TEXT("Expiry skips the additional loss wait"), Receiver->ShouldSkipTrackingLossWait());
		TestTrue(TEXT("Expiry preserves the final valid sample without reading a post-deadline position"), Frozen.Equals(LastValid));
		Target->SetActorLocation(FVector(1200, 0, 100));
		Advance(1.0f);
		TestTrue(TEXT("Movement after expiry stays hidden"), Receiver->GetLastKnownPlayerLocation().Equals(Frozen));
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseDirectionConfirmationTest,
	"Outlier.Enemy.Perception.NoiseDirection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseDirectionConfirmationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Timeout"), TEXT("Hold"), TEXT("LateReached"), TEXT("EarlyEndInterval"),
		TEXT("HoldingPastInterval"), TEXT("YawBoundary"), TEXT("YawOutside"), TEXT("PitchBoundary"), TEXT("PitchOutside"),
		TEXT("MergeKeepsSnapshot"), TEXT("CooldownMerge"), TEXT("NewEventAfterWindow"), TEXT("DelayedOldEvent"),
		TEXT("VerticalSame"), TEXT("VerticalOpposite"), TEXT("VerticalUndefined"), TEXT("CurrentAnchor"),
		TEXT("ZeroDirection"), TEXT("ExpiredActive"), TEXT("NearestQueued"), TEXT("GoalPreempts"),
		TEXT("SightPreempts"), TEXT("HitPreempts"), TEXT("StunPreempts"), TEXT("ControllerReset"),
		TEXT("StaleCallback"), TEXT("ArrivalKeepsClock"), TEXT("SearchEnd"), TEXT("LocationStillEligible"), TEXT("Configurable")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseDirectionConfirmationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchTestWorld Fixture(true);
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag());
	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(5000, 0, 0));
	AShooterCharacter* Other = Fixture.SpawnPlayer(FVector(6000, 0, 0));
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("AI"), AI)
		|| !TestNotNull(TEXT("Source"), Source) || !TestNotNull(TEXT("Other"), Other)) return false;
	AI->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	Other->GetCharacterMovement()->SetComponentTickEnabled(false);
	const auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
		}
	};
	const auto Emit = [&](const FVector& Point, AActor* Actor = nullptr)
	{
		return Enemy->RecordHeardNoise(Actor ? Actor : Other, Point, Fixture.World->GetTimeSeconds());
	};
	Advance(32);
	FVector Destination;
	if (!TestTrue(TEXT("Original investigation clue accepted"), Emit(FVector(1000, 0, 0), Source))
		|| !TestTrue(TEXT("Original investigation adopted"), Enemy->BeginNoiseInvestigation(Destination))) return false;
	TestFalse(TEXT("Current movement clue is not an extra look"), Enemy->TryBeginNoiseDirectionConfirmation());
	Advance(1);
	FVector Reference = FRotator(0, 60, 0).Vector() * 2000.0;
	if (Parameters.StartsWith(TEXT("Vertical"))) Reference = FVector(0, 0, 2000);
	if (Parameters == TEXT("ZeroDirection")) Reference = Enemy->GetActorLocation();
	if (Parameters == TEXT("ExpiredActive")) Enemy->HeardNoiseLifetime = 0.125f;
	if (Parameters == TEXT("Configurable"))
	{
		Enemy->NoiseDirectionConfirmationInterval = 0.25f;
		Enemy->NoiseDirectionConfirmationMaxRotationTime = 0.125f;
		Enemy->NoiseDirectionConfirmationHold = 0.0625f;
		Enemy->NoiseDirectionMergeYaw = 5.0f;
		Enemy->NoiseDirectionMergePitch = 5.0f;
	}
	if (!TestTrue(TEXT("Secondary sound accepted"), Emit(Reference))) return false;
	if (Parameters == TEXT("ZeroDirection"))
	{
		TestFalse(TEXT("Coincident source does not invent a direction"), Enemy->TryBeginNoiseDirectionConfirmation());
		Advance(32);
		Enemy->RefreshPerceptionGoal();
		return TestEqual(TEXT("Coincident clue can still become a movement goal"), Enemy->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::InvestigateNoise);
	}
	if (!TestTrue(TEXT("Secondary sound starts one confirmation"), Enemy->TryBeginNoiseDirectionConfirmation())) return false;
	const FEnemyNoiseDirectionConfirmation Initial = Enemy->GetNoiseDirectionConfirmation();
	TestTrue(TEXT("Starts rotating"), Initial.Phase == EEnemyNoiseConfirmationPhase::Rotating);
	TestTrue(TEXT("Captures moving policy"), Initial.bStartedWhileMoving);
	TestFalse(TEXT("Active response cannot overlap"), Enemy->TryBeginNoiseDirectionConfirmation());
	const auto HasCandidate = [&]
	{
		FEnemyHeardNoise Candidate;
		return Enemy->TryGetDirectionConfirmationNoise(Candidate);
	};
	if (Parameters == TEXT("Timeout"))
	{
		Advance(31);
		TestTrue(TEXT("Rotates before deadline"), Enemy->IsNoiseDirectionConfirmationActive());
		Advance(2);
		TestFalse(TEXT("Deadline timer ends without adding hold"), Enemy->IsNoiseDirectionConfirmationActive());
		return TestFalse(TEXT("Timed out event does not replay"), Enemy->TryBeginNoiseDirectionConfirmation());
	}
	if (Parameters == TEXT("Hold") || Parameters == TEXT("Configurable"))
	{
		TestTrue(TEXT("Reached notification accepted"), Enemy->NotifyNoiseDirectionReached(Initial.Serial));
		TestTrue(TEXT("Reached enters hold"), Enemy->GetNoiseDirectionConfirmation().Phase == EEnemyNoiseConfirmationPhase::Holding);
		const double Deadline = Enemy->GetNoiseDirectionConfirmation().PhaseEndsAt;
		TestFalse(TEXT("Duplicate reach cannot extend hold"), Enemy->NotifyNoiseDirectionReached(Initial.Serial));
		TestEqual(TEXT("Hold deadline remains fixed"), Enemy->GetNoiseDirectionConfirmation().PhaseEndsAt, Deadline);
		Advance(Parameters == TEXT("Hold") ? 11 : 3);
		return TestFalse(TEXT("Real timer ends reached hold"), Enemy->IsNoiseDirectionConfirmationActive());
	}
	if (Parameters == TEXT("LateReached"))
	{
		// 타이머 콜백이 아직 실행되지 않은 정확한 경계에서도 도달 성공으로 바꾸지 않는다.
		for (int32 Frame = 0; Frame < 32; ++Frame) Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		TestFalse(TEXT("Late reach rejected at deadline"), Enemy->NotifyNoiseDirectionReached(Initial.Serial));
		return TestFalse(TEXT("Late reach adds no hold"), Enemy->IsNoiseDirectionConfirmationActive());
	}
	if (Parameters == TEXT("EarlyEndInterval") || Parameters == TEXT("HoldingPastInterval"))
	{
		Advance(Parameters == TEXT("EarlyEndInterval") ? 1 : 29);
		TestTrue(TEXT("Another direction queues"), Emit(FVector(0, -2500, 0), Source));
		if (Parameters == TEXT("EarlyEndInterval"))
		{
			Enemy->EndNoiseDirectionConfirmation(Initial.Serial);
			Advance(30);
			TestFalse(TEXT("Early finish waits for original start interval"), Enemy->TryBeginNoiseDirectionConfirmation());
			Advance(1);
			return TestTrue(TEXT("Next starts exactly at interval"), Enemy->TryBeginNoiseDirectionConfirmation());
		}
		Enemy->NotifyNoiseDirectionReached(Initial.Serial);
		Advance(4);
		TestFalse(TEXT("One second does not interrupt hold"), Enemy->TryBeginNoiseDirectionConfirmation());
		Advance(7);
		return TestTrue(TEXT("Queued direction starts after hold also ends"), Enemy->TryBeginNoiseDirectionConfirmation());
	}
	if (Parameters == TEXT("YawBoundary") || Parameters == TEXT("YawOutside")
		|| Parameters == TEXT("PitchBoundary") || Parameters == TEXT("PitchOutside"))
	{
		const bool bYaw = Parameters.StartsWith(TEXT("Yaw"));
		const bool bOutside = Parameters.EndsWith(TEXT("Outside"));
		const double Yaw = 60.0 + (bYaw ? (bOutside ? 30.01 : 30.0) : 0.0);
		const double Pitch = bYaw ? 0.0 : (bOutside ? 20.01 : 20.0);
		Advance(1);
		Emit(FRotator(Pitch, Yaw, 0).Vector() * 2500.0, Source);
		return TestEqual(TEXT("Both angular thresholds include only their boundary"), HasCandidate(), bOutside);
	}
	if (Parameters == TEXT("VerticalSame") || Parameters == TEXT("VerticalOpposite") || Parameters == TEXT("VerticalUndefined"))
	{
		Advance(1);
		const FVector Point = Parameters == TEXT("VerticalSame") ? FVector(0, 0, 2500)
			: Parameters == TEXT("VerticalOpposite") ? FVector(0, 0, -2500) : FVector(1, 0, 2500);
		Emit(Point, Source);
		return TestEqual(TEXT("Undefined yaw merges only same vertical hemisphere"), HasCandidate(), Parameters != TEXT("VerticalSame"));
	}
	if (Parameters == TEXT("CurrentAnchor"))
	{
		// 두 사건은 현재 위치에서 거의 같은 방향이지만 시작 당시 벡터와는 크게 다르다.
		Enemy->SetActorLocation(Reference - FVector(0, 0, 1000));
		Advance(1);
		Emit(Reference + FVector(0, 0, 500), Source);
		return TestFalse(TEXT("Both directions use the same current anchor"), HasCandidate());
	}
	if (Parameters == TEXT("MergeKeepsSnapshot") || Parameters == TEXT("CooldownMerge")
		|| Parameters == TEXT("NewEventAfterWindow") || Parameters == TEXT("DelayedOldEvent"))
	{
		if (Parameters != TEXT("MergeKeepsSnapshot")) Enemy->EndNoiseDirectionConfirmation(Initial.Serial);
		Advance(Parameters == TEXT("NewEventAfterWindow") || Parameters == TEXT("DelayedOldEvent") ? 33 : 4);
		const FVector NearbyDirection = FRotator(0, 80, 0).Vector() * 2500.0;
		const double EventAt = Parameters == TEXT("DelayedOldEvent") ? Initial.StartedAt + 0.25 : Fixture.World->GetTimeSeconds();
		Enemy->RecordHeardNoise(Source, NearbyDirection, EventAt);
		TestTrue(TEXT("Merge never shifts adopted reference"), Enemy->GetNoiseDirectionConfirmation().Clue.Location.Equals(Reference));
		TestEqual(TEXT("Merge never restarts response clock"), Enemy->GetNoiseDirectionConfirmation().StartedAt, Initial.StartedAt);
		TestEqual(TEXT("Merge never extends phase deadline"), Enemy->GetNoiseDirectionConfirmation().PhaseEndsAt, Initial.PhaseEndsAt);
		return TestEqual(TEXT("Only actually new events after merge window reactivate"), HasCandidate(), Parameters == TEXT("NewEventAfterWindow"));
	}
	if (Parameters == TEXT("ExpiredActive"))
	{
		Advance(5);
		TestFalse(TEXT("Original clue expiry ends active response"), Enemy->IsNoiseDirectionConfirmationActive());
		TestFalse(TEXT("Expired snapshot cannot enter hold"), Enemy->NotifyNoiseDirectionReached(Initial.Serial));
		Advance(11);
		TestFalse(TEXT("Hold still ends normally"), Enemy->IsNoiseDirectionConfirmationActive());
		return TestFalse(TEXT("Expired event cannot start again"), Enemy->TryBeginNoiseDirectionConfirmation());
	}
	if (Parameters == TEXT("NearestQueued"))
	{
		Advance(1);
		const FVector Near(0, -2400, 0);
		Emit(Near, Source);
		Advance(1);
		Emit(FVector(-3000, 0, 0));
		Advance(31);
		TestTrue(TEXT("One waiting response selected after timeout"), Enemy->TryBeginNoiseDirectionConfirmation());
		return TestTrue(TEXT("Nearest queued wins over newest"), Enemy->GetNoiseDirectionConfirmation().Clue.Location.Equals(Near));
	}
	if (Parameters == TEXT("GoalPreempts") || Parameters == TEXT("LocationStillEligible"))
	{
		if (Parameters == TEXT("GoalPreempts"))
		{
			Enemy->NoiseDestinationUpdateInterval = 0.125f;
			Advance(8);
			TestTrue(TEXT("New closer destination accepted"), Emit(FVector(0, -400, 0), Source));
		}
		else
		{
			Enemy->NoiseCandidateSwitchDistance = 0.0f;
			Enemy->SetActorLocation(Reference);
			Advance(33);
			Enemy->RefreshPerceptionGoal();
		}
		TestFalse(TEXT("Movement intent preempts short look"), Enemy->IsNoiseDirectionConfirmationActive());
		TestTrue(TEXT("Selected movement can be adopted"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
		if (Parameters == TEXT("LocationStillEligible")) TestTrue(TEXT("Direction-used event remains a location clue"), Destination.Equals(Reference));
		return TestFalse(TEXT("Adopted destination does not add a short look"), Enemy->TryBeginNoiseDirectionConfirmation());
	}
	if (Parameters == TEXT("SightPreempts")) Enemy->OnDirectSightForHitResponse(Source->GetActorLocation());
	else if (Parameters == TEXT("HitPreempts"))
	{
		FEnemyHitClue Clue;
		TestTrue(TEXT("Hit clue created"), Enemy->CreateHitClue(FVector(-500, 0, 0), Fixture.World->GetTimeSeconds(), 3.0, Clue));
		TestTrue(TEXT("Hit immediately preempts hearing"), Enemy->RecordHitClue(Clue));
	}
	else if (Parameters == TEXT("StunPreempts")) Enemy->EnterStun();
	else if (Parameters == TEXT("ControllerReset")) AI->UnPossess();
	else if (Parameters == TEXT("ArrivalKeepsClock") || Parameters == TEXT("SearchEnd"))
	{
		TestTrue(TEXT("Arrival starts search"), Enemy->BeginNoiseInvestigationSearch(Enemy->GetNoiseInvestigationSerial(), Enemy->GetActorLocation()));
		TestEqual(TEXT("Arrival keeps original response identity"), Enemy->GetNoiseDirectionConfirmation().Serial, Initial.Serial);
		TestEqual(TEXT("Arrival keeps original rotation deadline"), Enemy->GetNoiseDirectionConfirmation().PhaseEndsAt, Initial.PhaseEndsAt);
		TestTrue(TEXT("Arrival keeps moving angle policy"), Enemy->GetNoiseDirectionConfirmation().bStartedWhileMoving);
		if (Parameters == TEXT("ArrivalKeepsClock")) return TestTrue(TEXT("Arrival does not cancel active moving response"), Enemy->IsNoiseDirectionConfirmationActive());
		Enemy->EndSearchEpisode();
	}
	else if (Parameters == TEXT("StaleCallback"))
	{
		Enemy->ClearHeardNoise();
		Advance(1);
		Emit(FVector(1000, 0, 0), Source);
		Enemy->BeginNoiseInvestigation(Destination);
		Advance(1);
		Emit(FVector(0, -2500, 0));
		TestTrue(TEXT("New lifetime starts a response"), Enemy->TryBeginNoiseDirectionConfirmation());
		TestTrue(TEXT("Reset does not reuse response serial"), Enemy->GetNoiseDirectionConfirmation().Serial > Initial.Serial);
		TestFalse(TEXT("Old reach cannot hold new response"), Enemy->NotifyNoiseDirectionReached(Initial.Serial));
		TestFalse(TEXT("Old completion cannot end new response"), Enemy->EndNoiseDirectionConfirmation(Initial.Serial));
		return TestTrue(TEXT("New response survives stale callbacks"), Enemy->IsNoiseDirectionConfirmationActive());
	}
	TestFalse(TEXT("Upper action or lifetime end releases response"), Enemy->IsNoiseDirectionConfirmationActive());
	TestFalse(TEXT("Old reach callback rejected"), Enemy->NotifyNoiseDirectionReached(Initial.Serial));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseFacingTest,
	"Outlier.Enemy.Perception.NoiseFacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseFacingTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("MovingClamp"), TEXT("PathTurn"), TEXT("ArrivalFreeze"), TEXT("StationaryRear"),
		TEXT("VerticalYaw"), TEXT("DownPitch"), TEXT("ControllerEntry"), TEXT("DirectSight"), TEXT("PartialSight"),
		TEXT("Reobservation"), TEXT("ProlongedSight"), TEXT("VisualToNoise"), TEXT("Stun"), TEXT("Destination"), TEXT("ConfigurableLimit")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseFacingTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchTestWorld Fixture(true);
	const bool bPartial = Parameters == TEXT("PartialSight") || Parameters == TEXT("Reobservation")
		|| Parameters == TEXT("ProlongedSight") || Parameters == TEXT("VisualToNoise");
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag(), FVector::ZeroVector, !bPartial);
	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(5000, 0, 0));
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("Controller"), AI) || !TestNotNull(TEXT("Source"), Source)) return false;
	AI->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	const auto Advance = [&](int32 Frames, bool bRotate = false)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
			if (bRotate) AI->UpdateControlRotation(1.0f / 32.0f);
		}
	};
	const auto Sight = [&](bool bVisible)
	{
		FAIStimulus Stimulus(*GetDefault<UAISense_Sight>(), 1.0f, Source->GetActorLocation(), Enemy->GetActorLocation());
		if (!bVisible) Stimulus.MarkNoLongerSensed();
		AI->GetPerceptionComponent()->RegisterStimulus(Source, Stimulus);
		AI->GetPerceptionComponent()->ProcessStimuli();
	};
	Advance(32);
	if (bPartial) Enemy->EnterAlertFromPerception(FVector(1000, 0, 0));
	FVector Destination;
	if (!TestTrue(TEXT("Initial sound accepted"), Enemy->RecordHeardNoise(Source, FVector(1000, 0, 0), Fixture.World->GetTimeSeconds()))
		|| !TestTrue(TEXT("Initial destination adopted"), Enemy->BeginNoiseInvestigation(Destination))) return false;
	const uint64 Investigation = Enemy->GetNoiseInvestigationSerial();
	Enemy->UpdateNoiseInvestigationTravelDirection(Investigation, FVector::ForwardVector, 360.0f);
	if (Parameters == TEXT("StationaryRear"))
		TestTrue(TEXT("Stationary search entered"), Enemy->BeginNoiseInvestigationSearch(Investigation, Enemy->GetActorLocation()));
	if (Parameters == TEXT("ConfigurableLimit")) Enemy->NoiseDirectionMovingHalfAngle = 25.0f;
	if (Parameters == TEXT("VerticalYaw"))
	{
		AI->SetControlRotation(FRotator(0, 35, 0));
		Enemy->SetActorRotation(FRotator(0, 35, 0));
	}
	Advance(1);
	const FVector Point = Parameters == TEXT("VerticalYaw") ? FVector(0, 0, 2000)
		: Parameters == TEXT("DownPitch") ? FVector(0, 2000, -2000) : FVector(-2000, 0, 0);
	TestTrue(TEXT("Secondary noise accepted"), Enemy->RecordHeardNoise(Source, Point, Fixture.World->GetTimeSeconds()));
	if (Parameters == TEXT("ControllerEntry"))
	{
		AI->UpdateControlRotation(1.0f / 32.0f);
		TestTrue(TEXT("Existing controller update starts confirmation"), Enemy->IsNoiseDirectionConfirmationActive());
		return TestTrue(TEXT("Controller preserves owned rotation instead of default focus"), AI->GetControlRotation().Yaw > 0.0);
	}
	if (!TestTrue(TEXT("Facing starts"), Enemy->TryBeginNoiseDirectionConfirmation())) return false;
	const uint64 Confirmation = Enemy->GetNoiseDirectionConfirmation().Serial;
	TestTrue(TEXT("Server publishes presentation ownership"), Enemy->IsNoiseDirectionFacing());
	if (Parameters == TEXT("DirectSight") || bPartial)
	{
		Sight(true);
		TestFalse(TEXT("Direct sight cancels old sound investigation immediately"), Enemy->IsNoiseInvestigationActive());
		TestFalse(TEXT("Direct sight releases facing presentation"), Enemy->IsNoiseDirectionFacing());
		TestFalse(TEXT("Old completion no longer owns investigation"), Enemy->IsLocationInvestigationCurrent(Investigation));
		TestFalse(TEXT("Old reached callback rejected"), Enemy->NotifyNoiseDirectionReached(Confirmation));
		if (!bPartial) return TestEqual(TEXT("Combat retains observed target"), Enemy->GetTrackedTarget(), static_cast<AActor*>(Source));
		TestEqual(TEXT("Brief sight preserves initial detection policy"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
		if (Parameters == TEXT("ProlongedSight"))
		{
			const_cast<FEnemyStat&>(Enemy->GetRuntimeStat()).AwarenessFillDuration = 20.0f;
			Advance(224, true);
		}
		Sight(false);
		Advance(24, true);
		if (!TestTrue(TEXT("Hidden last observed point has an investigation"), Enemy->HasHitResponse())) return false;
		const uint64 VisualSerial = Enemy->GetLocationInvestigationSerial();
		const double AdoptedAt = Enemy->GetVisualInvestigation().AdoptedAt;
		if (Parameters == TEXT("Reobservation"))
		{
			Sight(true);
			TestEqual(TEXT("Same target and position retain visual adoption clock"), Enemy->GetVisualInvestigation().AdoptedAt, AdoptedAt);
			return TestEqual(TEXT("Same observation retains investigation episode"), Enemy->GetLocationInvestigationSerial(), VisualSerial);
		}
		if (Parameters == TEXT("VisualToNoise"))
		{
			Advance(16, true);
			TestTrue(TEXT("Later closer sound accepted"), Enemy->RecordHeardNoise(Source, FVector(0, 500, 0), Fixture.World->GetTimeSeconds()));
			TestFalse(TEXT("Eligible sound ends observed investigation"), Enemy->HasHitResponse());
			TestTrue(TEXT("Later sound can start movement after visual hold"), Enemy->BeginNoiseInvestigation(Destination));
			return TestTrue(TEXT("New investigation uses new sound point"), Destination.Equals(FVector(0, 500, 0)));
		}
		return TestTrue(TEXT("Observed point is the real last sight location"), Enemy->GetPerceptionGoal().Location.Equals(Source->GetActorLocation()));
	}
	if (Parameters == TEXT("Stun"))
	{
		Enemy->EnterStun();
		return TestFalse(TEXT("Stun clears replicated presentation ownership"), Enemy->IsNoiseDirectionFacing());
	}
	if (Parameters == TEXT("Destination"))
	{
		Enemy->NoiseDestinationUpdateInterval = 0.125f;
		Advance(6, true);
		TestTrue(TEXT("New movement evidence accepted"), Enemy->RecordHeardNoise(Source, FVector(0, -400, 0), Fixture.World->GetTimeSeconds()));
		TestFalse(TEXT("Movement goal preempts presentation before timeout"), Enemy->IsNoiseDirectionFacing());
		return TestTrue(TEXT("New movement intent remains available"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
	}
	Advance(8, true);
	if (Parameters == TEXT("PathTurn"))
	{
		const double Before = Enemy->GetActorRotation().Yaw;
		Enemy->UpdateNoiseInvestigationTravelDirection(Investigation, FVector::RightVector, 360.0f);
		Advance(1, true);
		TestTrue(TEXT("Changed path does not snap body to new clamp"), FMath::Abs(FMath::FindDeltaAngleDegrees(Before, Enemy->GetActorRotation().Yaw)) <= 11.3);
		Advance(12, true);
		return TestTrue(TEXT("Changed path updates allowed target sector"), Enemy->GetActorRotation().Yaw > 60.0 && Enemy->GetActorRotation().Yaw <= 150.01);
	}
	if (Parameters == TEXT("ArrivalFreeze"))
	{
		const double StartedAt = Enemy->GetNoiseDirectionConfirmation().StartedAt;
		TestTrue(TEXT("Arrival transitions to search"), Enemy->BeginNoiseInvestigationSearch(Investigation, Enemy->GetActorLocation()));
		Enemy->UpdateNoiseInvestigationTravelDirection(Investigation, FVector::RightVector, 360.0f);
		Advance(12, true);
		TestEqual(TEXT("Arrival does not restart response"), Enemy->GetNoiseDirectionConfirmation().StartedAt, StartedAt);
		return TestTrue(TEXT("Arrival retains last moving constraint"), Enemy->GetActorRotation().Yaw <= 60.01);
	}
	Advance(12, true);
	TestTrue(TEXT("Collision capsule stays upright"), FMath::Abs(Enemy->GetActorRotation().Pitch) < 0.01);
	if (Parameters == TEXT("StationaryRear")) return TestTrue(TEXT("Stationary body may turn beyond moving sector"), Enemy->GetActorRotation().Yaw > 100.0);
	if (Parameters == TEXT("VerticalYaw"))
	{
		TestTrue(TEXT("Vertical source keeps stable yaw"), FMath::Abs(AI->GetControlRotation().Yaw - 35.0) < 0.01);
		return TestTrue(TEXT("Vertical source uses allowed pitch"), AI->GetControlRotation().Pitch > 60.0 && AI->GetControlRotation().Pitch <= 89.01);
	}
	if (Parameters == TEXT("DownPitch")) return TestTrue(TEXT("Downward sound pitch remains full angle"), AI->GetControlRotation().GetNormalized().Pitch < -40.0);
	const double Limit = Parameters == TEXT("ConfigurableLimit") ? 25.0 : 60.0;
	TestTrue(TEXT("Moving body respects configured sector"), Enemy->GetActorRotation().Yaw <= Limit + 0.01);
	TestTrue(TEXT("Moving body actually confirms the allowed boundary"), Enemy->GetActorRotation().Yaw >= Limit - 2.0);
	return TestTrue(TEXT("Server sight direction follows controller yaw"), Enemy->GetViewRotation().Equals(AI->GetControlRotation(), 0.01));
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseBoundaryTest,
	"Outlier.Enemy.Perception.NoiseBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseBoundaryTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("NearInside"), TEXT("NearBoundary"), TEXT("NearOutside"), TEXT("FastPass"),
		TEXT("Miss"), TEXT("VerticalMiss"), TEXT("Corner"), TEXT("ZeroLength"), TEXT("StaleProgress"),
		TEXT("FailedRepeat"), TEXT("DoorOnly"), TEXT("ExpiredAfterOpen"), TEXT("RelevantOpenRetry"),
		TEXT("FailedNearRetry"), TEXT("UnrelatedOpen"), TEXT("SignalBeforeFailure"), TEXT("DifferentGoal"),
		TEXT("ResetStale"), TEXT("SearchRadius"), TEXT("StaleFailure"), TEXT("WrongFailureDestination")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseBoundaryTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchTestWorld Fixture(true);
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag());
	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(5000, 0, 0));
	URoomCombatSubsystem* Rooms = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("Controller"), AI)
		|| !TestNotNull(TEXT("Sound source"), Source) || !TestNotNull(TEXT("Room subsystem"), Rooms)) return false;
	AI->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	const auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
		}
	};
	const auto Emit = [&](const FVector& Point) { return Enemy->RecordHeardNoise(Source, Point, Fixture.World->GetTimeSeconds()); };
	Advance(32);
	const FVector Original(1000, 0, 0);
	FVector Destination;
	if (!TestTrue(TEXT("Original sound accepted"), Emit(Original))
		|| !TestTrue(TEXT("Original investigation adopted"), Enemy->BeginNoiseInvestigation(Destination))) return false;
	const uint64 Investigation = Enemy->GetNoiseInvestigationSerial();
	const uint64 OriginalClue = Enemy->GetActiveNoiseInvestigationClue().Serial;
	Advance(1);
	if (Parameters == TEXT("SearchRadius"))
	{
		// 도달 반경이 위치 병합 반경보다 커도 제자리 이동/수색을 반복하지 않는다.
		Enemy->UpdateNoiseInvestigationProgress(Investigation, FVector::ZeroVector, Enemy->GetActorLocation(), 200.0f);
		if (!TestTrue(TEXT("Existing investigation enters search"), Enemy->BeginNoiseInvestigationSearch(Investigation, Enemy->GetActorLocation()))) return false;
		const uint64 Search = Enemy->GetSearchEpisode().Serial;
		Advance(40);
		TestTrue(TEXT("Distinct event inside arrival radius is stored"), Emit(FVector(0, 150, 0)));
		TestEqual(TEXT("Already reached sound keeps search intent"), Enemy->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::Search);
		TestFalse(TEXT("Already reached sound cannot restart movement"), Enemy->BeginNoiseInvestigation(Destination));
		return TestEqual(TEXT("Close sound preserves search episode"), Enemy->GetSearchEpisode().Serial, Search);
	}
	const bool bProgress = Parameters.StartsWith(TEXT("Near")) || Parameters == TEXT("FastPass")
		|| Parameters == TEXT("Miss") || Parameters == TEXT("VerticalMiss") || Parameters == TEXT("Corner")
		|| Parameters == TEXT("ZeroLength") || Parameters == TEXT("StaleProgress");
	if (bProgress)
	{
		const FVector Point(0, 1500, 0);
		if (!TestTrue(TEXT("Secondary clue stored"), Emit(Point))
			|| !TestTrue(TEXT("Confirmation begins"), Enemy->TryBeginNoiseDirectionConfirmation())) return false;
		const uint64 Confirmation = Enemy->GetNoiseDirectionConfirmation().Serial;
		FVector Previous(-200, 1500, 0), Current(200, 1500, 0);
		if (Parameters.StartsWith(TEXT("Near")))
		{
			const double Distance = Parameters == TEXT("NearInside") ? 49.9 : Parameters == TEXT("NearBoundary") ? 50.0 : 50.1;
			Current = Point + FVector(-Distance, 0, 0);
			Previous = Current + FVector(0, -10, 0);
		}
		else if (Parameters == TEXT("Miss")) { Previous.Y += 100; Current.Y += 100; }
		else if (Parameters == TEXT("VerticalMiss")) { Previous.Z += 200; Current.Z += 200; }
		else if (Parameters == TEXT("Corner"))
		{
			Previous = FVector(200, 1300, 0); Current = FVector(200, 1700, 0);
			Enemy->UpdateNoiseInvestigationTravelDirection(Investigation, FVector::RightVector, 180.0f);
		}
		else if (Parameters == TEXT("ZeroLength")) Previous = Current = FVector::ZeroVector;
		Enemy->SetActorLocation(Current);
		Enemy->UpdateNoiseInvestigationProgress(Parameters == TEXT("StaleProgress") ? Investigation + 1 : Investigation, Previous, Current, 50.0f);
		const bool bShouldEnd = Parameters == TEXT("NearInside") || Parameters == TEXT("NearBoundary") || Parameters == TEXT("FastPass");
		TestEqual(TEXT("Only proximity or actual passing ends confirmation"), Enemy->IsNoiseDirectionConfirmationActive(), !bShouldEnd);
		TestTrue(TEXT("Movement investigation remains active"), Enemy->IsNoiseInvestigationActive());
		TestFalse(TEXT("Passing cannot fabricate search arrival"), Enemy->IsNoiseSearchActive());
		TestEqual(TEXT("Investigation identity unchanged"), Enemy->GetNoiseInvestigationSerial(), Investigation);
		TestEqual(TEXT("Original movement clue retained"), Enemy->GetActiveNoiseInvestigationClue().Serial, OriginalClue);
		TestTrue(TEXT("Movement destination retained"), Enemy->GetPerceptionGoal().Location.Equals(Original));
		if (bShouldEnd)
		{
			TestFalse(TEXT("Old reached callback rejected"), Enemy->NotifyNoiseDirectionReached(Confirmation));
			return TestFalse(TEXT("Passed event cannot repeat"), Enemy->TryBeginNoiseDirectionConfirmation());
		}
		return true;
	}
	const FBox Bounds(FVector(-200, -200, -200), FVector(1200, 200, 200));
	const FBox Relevant(FVector(480, -20, -20), FVector(520, 20, 20));
	const FBox Unrelated(FVector(5000, 5000, -20), FVector(5100, 5100, 20));
	if (Parameters == TEXT("SignalBeforeFailure")) Rooms->NotifyMovementObstacleOpened(Relevant);
	if (Parameters == TEXT("FailedNearRetry"))
	{
		Enemy->SetActorLocation(FVector(950, 0, 0));
		Enemy->UpdateNoiseInvestigationProgress(Investigation, FVector::ZeroVector, Enemy->GetActorLocation(), 10.0f);
	}
	if (Parameters == TEXT("StaleFailure") || Parameters == TEXT("WrongFailureDestination"))
	{
		TestFalse(TEXT("Stale movement cannot poison failure record"), Enemy->RecordNoiseInvestigationFailure(
			Parameters == TEXT("StaleFailure") ? Investigation + 1 : Investigation,
			Parameters == TEXT("WrongFailureDestination") ? Original + FVector(300, 0, 0) : Original, Bounds));
		Enemy->BeginNoiseInvestigationSearch(Investigation, Enemy->GetActorLocation());
		Advance(40); Emit(Original + FVector(25, 0, 0));
		return TestEqual(TEXT("Rejected failure cannot suppress next goal"), Enemy->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::InvestigateNoise);
	}
	if (!TestTrue(TEXT("Current movement records failure"), Enemy->RecordNoiseInvestigationFailure(Investigation, Original, Bounds))
		|| !TestTrue(TEXT("Failure searches reached position"), Enemy->BeginNoiseInvestigationSearch(Investigation, Enemy->GetActorLocation()))) return false;
	const uint64 Search = Enemy->GetSearchEpisode().Serial;
	Advance(40);
	if (Parameters == TEXT("DoorOnly"))
	{
		Rooms->NotifyMovementObstacleOpened(Relevant);
		TestEqual(TEXT("Door alone keeps search episode"), Enemy->GetSearchEpisode().Serial, Search);
		Enemy->EndNoiseInvestigation(); Rooms->NotifyMovementObstacleOpened(Relevant);
		return TestFalse(TEXT("Door alone cannot restore finished clue"), Enemy->BeginNoiseInvestigation(Destination));
	}
	if (Parameters == TEXT("ExpiredAfterOpen"))
	{
		Emit(Original + FVector(25, 0, 0)); Advance(128);
		Rooms->NotifyMovementObstacleOpened(Relevant); Enemy->EndNoiseInvestigation();
		return TestFalse(TEXT("Opening cannot renew expired candidate"), Enemy->BeginNoiseInvestigation(Destination));
	}
	if (Parameters == TEXT("ResetStale"))
	{
		Enemy->ClearHeardNoise(); Rooms->NotifyMovementObstacleOpened(Relevant); Advance(1); Emit(Original);
		TestTrue(TEXT("New lifetime may investigate original point"), Enemy->BeginNoiseInvestigation(Destination));
		return TestFalse(TEXT("Old failure callback cannot affect new lifetime"), Enemy->RecordNoiseInvestigationFailure(Investigation, Original, Bounds));
	}
	if (Parameters == TEXT("RelevantOpenRetry") || Parameters == TEXT("FailedNearRetry")) Rooms->NotifyMovementObstacleOpened(Relevant);
	else if (Parameters == TEXT("UnrelatedOpen")) Rooms->NotifyMovementObstacleOpened(Unrelated);
	const FVector Next = Parameters == TEXT("DifferentGoal") ? FVector(0, 1800, 0) : Original + FVector(25, 0, 0);
	Advance(1); TestTrue(TEXT("Fresh sound stored"), Emit(Next));
	const bool bCanRetry = Parameters == TEXT("RelevantOpenRetry") || Parameters == TEXT("FailedNearRetry") || Parameters == TEXT("DifferentGoal");
	TestEqual(TEXT("Only qualified retry or different goal publishes movement"), Enemy->GetPerceptionGoal().Type == EEnemyPerceptionGoalType::InvestigateNoise, bCanRetry);
	if (bCanRetry)
	{
		TestTrue(TEXT("Qualified goal adopted"), Enemy->BeginNoiseInvestigation(Destination));
		TestTrue(TEXT("New movement uses fresh point"), Destination.Equals(Next));
		return TestTrue(TEXT("New adoption advances episode"), Enemy->GetNoiseInvestigationSerial() > Investigation);
	}
	TestEqual(TEXT("Suppressed repeat keeps search episode"), Enemy->GetSearchEpisode().Serial, Search);
	TestFalse(TEXT("Failed goal cannot restart without permission"), Enemy->BeginNoiseInvestigation(Destination));
	return TestTrue(TEXT("Suppressed location remains eligible for direction confirmation"), Enemy->TryBeginNoiseDirectionConfirmation());
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF5NoiseContinuationTest,
	"Outlier.Enemy.F5.NoiseContinuation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF5NoiseContinuationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Arrival"), TEXT("Waypoint"), TEXT("StillBehind"), TEXT("Expired"),
		TEXT("StaleBoundary"), TEXT("SightPreempts"), TEXT("NewDestination"), TEXT("OriginalDeadline"), TEXT("ObservedArrival")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyF5NoiseContinuationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchTestWorld Fixture(true);
	const bool bObservedArrival = Parameters == TEXT("ObservedArrival");
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FGameplayTag(), FVector::ZeroVector, !bObservedArrival);
	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	AShooterCharacter* Source = Fixture.SpawnPlayer(FVector(5000, 0, 0));
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("AI"), AI) || !TestNotNull(TEXT("Source"), Source))
	{
		return false;
	}
	AI->Possess(Enemy);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	// Isolate direction confirmation from independently approved destination replacement.
	Enemy->NoiseDestinationUpdateInterval = 10.0f;
	const auto Advance = [&](int32 Frames, bool bRotate = true)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
			Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
			if (bRotate)
			{
				AI->UpdateControlRotation(1.0f / 32.0f);
			}
		}
	};
	Advance(32, false);
	if (bObservedArrival)
	{
		Enemy->EnterAlertFromPerception(FVector(1000, 0, 0));
	}
	FVector Destination;
	if (!TestTrue(TEXT("Original noise accepted"), Enemy->RecordHeardNoise(Source, FVector(1000, 0, 0), Fixture.World->GetTimeSeconds()))
		|| !TestTrue(TEXT("Original investigation adopted"), Enemy->BeginNoiseInvestigation(Destination)))
	{
		return false;
	}
	const uint64 OriginalNoiseSerial = Enemy->GetLocationInvestigationSerial();
	if (bObservedArrival)
	{
		Enemy->VisualInvestigationHoldDuration = 10.0f;
		TestTrue(TEXT("Brief actual observation recorded"), Enemy->RecordInvestigationSight(Source, Destination, Fixture.World->GetTimeSeconds()));
		Enemy->OnDirectSightForHitResponse(Destination);
		Advance(24);
		if (!TestTrue(TEXT("Last observed investigation begins moving"), Enemy->BeginLocationInvestigation(Destination)))
		{
			return false;
		}
		TestTrue(TEXT("Visual investigation replaces original noise identity"), Enemy->GetLocationInvestigationSerial() != OriginalNoiseSerial);
	}
	const uint64 Investigation = Enemy->GetLocationInvestigationSerial();
	Enemy->UpdateNoiseInvestigationTravelDirection(Investigation, FVector::ForwardVector, 360.0f);
	Advance(1, false);
	if (Parameters == TEXT("OriginalDeadline"))
	{
		Enemy->HeardNoiseLifetime = 1.2f;
	}
	const double OccurredAt = Fixture.World->GetTimeSeconds();
	if (!TestTrue(TEXT("Farther rear noise accepted"), Enemy->RecordHeardNoise(Source, FVector(-2000, 0, 0), OccurredAt))
		|| !TestTrue(TEXT("Limited look starts"), Enemy->TryBeginNoiseDirectionConfirmation()))
	{
		return false;
	}
	const uint64 Initial = Enemy->GetNoiseDirectionConfirmation().Serial;
	Advance(20);
	TestTrue(TEXT("Rear look remains within moving sector"), FMath::Abs(Enemy->GetActorRotation().Yaw) <= 60.01);
	TestFalse(TEXT("Limited reaction does not claim full source confirmation"), Enemy->GetNoiseDirectionConfirmation().bReachedSourceDirection);
	TestTrue(TEXT("Original destination retained"), Enemy->BeginLocationInvestigation(Destination) && Destination.Equals(FVector(1000, 0, 0)));
	Enemy->EndNoiseDirectionConfirmation(Initial);
	Advance(16, false);
	if (bObservedArrival)
	{
		Enemy->NotifyNoiseInvestigationNaturalBoundary(OriginalNoiseSerial, true);
		AI->UpdateControlRotation(1.0f / 32.0f);
		TestFalse(TEXT("Old noise arrival cannot resume visual investigation remainder"), Enemy->IsNoiseDirectionConfirmationActive());
	}
	if (Parameters == TEXT("Expired"))
	{
		Advance(64, false);
	}
	if (Parameters == TEXT("NewDestination"))
	{
		Enemy->NoiseDestinationUpdateInterval = 0.0f;
		TestTrue(TEXT("New closer event accepted"), Enemy->RecordHeardNoise(Source, FVector(0, 500, 0), Fixture.World->GetTimeSeconds()));
		TestTrue(TEXT("New destination adopted"), Enemy->TryUpdateNoiseInvestigationDestination(Destination));
	}
	if (Parameters == TEXT("SightPreempts"))
	{
		Enemy->OnDirectSightForHitResponse(Source->GetActorLocation());
	}
	if (Parameters == TEXT("Waypoint"))
	{
		Enemy->UpdateNoiseInvestigationTravelDirection(Investigation, -FVector::ForwardVector, 360.0f);
	}
	if (Parameters == TEXT("Arrival") || Parameters == TEXT("OriginalDeadline") || bObservedArrival)
	{
		TestTrue(TEXT("Natural arrival enters stationary search"), Enemy->BeginLocationInvestigationSearch(Investigation, Enemy->GetActorLocation()));
	}
	else
	{
		Enemy->NotifyNoiseInvestigationNaturalBoundary(Parameters == TEXT("StaleBoundary") ? Investigation + 1 : Investigation,
			Parameters != TEXT("Waypoint") && Parameters != TEXT("StillBehind"));
	}
	AI->UpdateControlRotation(1.0f / 32.0f);
	const bool bExpectedContinuation = Parameters == TEXT("Arrival") || Parameters == TEXT("Waypoint") || Parameters == TEXT("OriginalDeadline") || bObservedArrival;
	TestEqual(TEXT("Only eligible natural boundary resumes the original event"), Enemy->IsNoiseDirectionConfirmationActive(), bExpectedContinuation);
	if (!bExpectedContinuation)
	{
		return true;
	}
	const uint64 Continued = Enemy->GetNoiseDirectionConfirmation().Serial;
	TestTrue(TEXT("Continuation owns a new callback identity"), Continued > Initial);
	TestFalse(TEXT("Old completion cannot cancel continuation"), Enemy->EndNoiseDirectionConfirmation(Initial));
	TestEqual(TEXT("Continuation keeps original occurrence time"), Enemy->GetNoiseDirectionConfirmation().Clue.OccurredAt, OccurredAt);
	if (Parameters == TEXT("OriginalDeadline"))
	{
		Advance(4);
		return TestFalse(TEXT("Continuation ends at original TTL without fresh budget"), Enemy->IsNoiseDirectionConfirmationActive());
	}
	Advance(24);
	TestTrue(TEXT("Remaining source direction becomes visibly reachable"), FMath::Abs(Enemy->GetActorRotation().Yaw) > 150.0);
	Enemy->EndNoiseDirectionConfirmation(Continued);
	Advance(12, false);
	Enemy->NotifyNoiseInvestigationNaturalBoundary(Investigation, true);
	return TestFalse(TEXT("Same rear remainder cannot replay at another boundary"), Enemy->TryBeginNoiseDirectionConfirmation());
}

#endif
