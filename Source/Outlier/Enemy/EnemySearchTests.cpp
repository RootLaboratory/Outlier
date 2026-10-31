#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Room/RoomTagComponent.h"
#include "TimerManager.h"

namespace
{
// 기존 Room/Pool 테스트와 동일한 서버 권한 World를 사용하고 실패 경로에서도 정리한다.
struct FEnemySearchTestWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;

	FEnemySearchTestWorld()
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
	TestFalse(TEXT("More than two search points are rejected"), Enemy->SetSearchPoints({First, Second, Hint}));
	TestTrue(TEXT("Two points are stored once before movement"), Enemy->SetSearchPoints({First, Second}));
	TestFalse(TEXT("Selected points cannot be replenished in the episode"), Enemy->SetSearchPoints({Hint}));
	TestTrue(TEXT("First LookAround completes one point"), Enemy->CompleteSearchPoint());
	TestFalse(TEXT("Second point remains before LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);
	TestTrue(TEXT("Second LookAround completes the episode"), Enemy->CompleteSearchPoint());
	TestTrue(TEXT("Two confirmations enter LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);
	TestFalse(TEXT("Completed episode cannot confirm a third point"), Enemy->CompleteSearchPoint());
	Enemy->BeginSearchEpisode();
	Enemy->SetSearchPoints({First});
	Enemy->CompleteSearchPoint();
	TestTrue(TEXT("One point also finishes without refill"), Enemy->GetSearchEpisode().bLocalGuard);

	Enemy->SetPlayerCurrentlyVisible(true);
	TestFalse(TEXT("Direct sight ends the search episode"), Enemy->GetSearchEpisode().bActive);
	Enemy->ApplySharedTargetContact(Hint, true, 12);
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
	FEnemySearchTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FGameplayTag OtherRoom = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")));
	AEnemyBase* JoiningEnemy = Fixture.SpawnEnemy(Room, FVector::ZeroVector, false);
	AEnemyBase* OtherEnemy = Fixture.SpawnEnemy(OtherRoom, FVector(1000, 0, 0), false);
	AEnemyBase* Target = Fixture.SpawnEnemy(OtherRoom, FVector(300, 200, 350), false);
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms)
		|| !Controller || !JoiningEnemy || !OtherEnemy || !Target) return false;
	const FVector SeenLocation = Target->GetActorLocation();
	const FAIStimulus Seen(*GetDefault<UAISense_Sight>(), 1.0f, SeenLocation, FVector::ZeroVector);
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);

	// 실제 Sight 콜백의 전투 합류를 검사한다. 비전투 방은 기존 Alert 절차를 유지해야 한다.
	Controller->HandleSightStimulus(JoiningEnemy, Target, Seen);
	TestEqual(TEXT("Successful sight joins active room immediately"),
		JoiningEnemy->GetCombatState(), EEnemyCombatState::Combat);
	TestTrue(TEXT("Sight retains direct visibility"), JoiningEnemy->IsPlayerCurrentlyVisible());
	TestTrue(TEXT("Sight remembers the observed location"),
		JoiningEnemy->GetLastKnownPlayerLocation().Equals(SeenLocation));
	Controller->HandleSightStimulus(OtherEnemy, Target, Seen);
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
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FGameplayTag OtherRoom = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")));
	AEnemyBase* JoiningEnemy = Fixture.SpawnEnemy(Room, FVector::ZeroVector, false);
	AEnemyBase* AlertEnemy = Fixture.SpawnEnemy(Room, FVector(1000, 0, 0), false);
	AEnemyBase* OtherEnemy = Fixture.SpawnEnemy(OtherRoom, FVector(2000, 0, 0), false);
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms)
		|| !Controller || !JoiningEnemy || !AlertEnemy || !OtherEnemy) return false;
	const FVector HeardLocation(300, 200, 350);
	const FAIStimulus Heard(*GetDefault<UAISense_Hearing>(), 1.0f, HeardLocation, FVector::ZeroVector);
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);

	// 공유 접촉과 직접 시야가 없어도 전투 중인 자신의 방으로 즉시 합류한다.
	Controller->HandleHearingStimulus(JoiningEnemy, nullptr, FAIStimulus());
	TestEqual(TEXT("Failed hearing does not join combat"), JoiningEnemy->GetCombatState(), EEnemyCombatState::NonCombat);
	Controller->HandleHearingStimulus(JoiningEnemy, nullptr, Heard);
	TestEqual(TEXT("Successful hearing joins active room immediately"), JoiningEnemy->GetCombatState(), EEnemyCombatState::Combat);
	TestFalse(TEXT("Hearing does not invent direct sight"), JoiningEnemy->IsPlayerCurrentlyVisible());
	TestFalse(TEXT("Joining does not require or publish shared contact"), JoiningEnemy->HasSharedTargetContact());
	TestTrue(TEXT("Joining remembers the heard location"), JoiningEnemy->GetLastKnownPlayerLocation().Equals(HeardLocation));

	AlertEnemy->EnterAlertFromPerception(FVector::ZeroVector);
	Controller->HandleHearingStimulus(AlertEnemy, nullptr, Heard);
	TestEqual(TEXT("Alert enemy also joins active room on hearing"), AlertEnemy->GetCombatState(), EEnemyCombatState::Combat);
	Controller->HandleHearingStimulus(OtherEnemy, nullptr, Heard);
	TestEqual(TEXT("Another room's combat does not bypass Alert"), OtherEnemy->GetCombatState(), EEnemyCombatState::Alert);
	TestFalse(TEXT("Hearing in inactive room does not start room combat"), Rooms->IsRoomInCombat(OtherRoom));

	// 반복 청각은 현재 수색 회차를 건드리지 않고, Stun도 강제로 해제하지 않는다.
	TestTrue(TEXT("Combat participant can begin hidden-target search"), JoiningEnemy->BeginSearchEpisode());
	const uint64 SearchSerial = JoiningEnemy->GetSearchEpisode().Serial;
	Controller->HandleHearingStimulus(JoiningEnemy, nullptr, Heard);
	TestTrue(TEXT("Repeated hearing preserves active search"), JoiningEnemy->GetSearchEpisode().bActive);
	TestEqual(TEXT("Repeated hearing does not restart search"), JoiningEnemy->GetSearchEpisode().Serial, SearchSerial);
	AlertEnemy->EnterStun();
	Controller->HandleHearingStimulus(AlertEnemy, nullptr, Heard);
	TestEqual(TEXT("Combat join preserves active stun"), AlertEnemy->GetCombatState(), EEnemyCombatState::Stun);
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
	for (const TCHAR* Scenario : {TEXT("Points0"), TEXT("Points1"), TEXT("Points2"),
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
	const bool bFreshOnEntry = Parameters == TEXT("FreshHint");
	if (bFreshOnEntry) Enemy->ApplySharedTargetContact(Hint, true, 10);
	TestEqual(TEXT("Search parent stays running for child transitions"),
		EnemySearchTesting::EnterSession(Session), EStateTreeRunStatus::Running);
	TestEqual(TEXT("Only a fresh shared hint skips the loss wait"), Session.bSkipLossWait, bFreshOnEntry);
	const uint64 Serial = Enemy->GetSearchEpisode().Serial;

	// 0/1/2 지점은 동일한 진행 Task를 반복 호출한다. 이동 전 조회로 확인 횟수가 늘지 않아야 한다.
	if (Parameters.StartsWith(TEXT("Points")))
	{
		const int32 Count = Parameters == TEXT("Points2") ? 2 : Parameters == TEXT("Points1") ? 1 : 0;
		Progress.Action = EEnemySearchProgress::AnchorArrived;
		TestEqual(TEXT("Anchor arrival succeeds"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
		TArray<FVector> Points;
		if (Count > 0) Points.Add(First);
		if (Count > 1) Points.Add(Second);
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
		for (int32 Tick = 0; Tick < 5; ++Tick) EnemySearchTesting::TickSession(Session);
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
				Enemy->SetPlayerCurrentlyVisible(false);
				EnemySearchTesting::EnterSession(Session);
			}
			if (Phase == 1) Enemy->SetSearchPoints({First, Second});
			if (Phase == 2) Enemy->EnterSearchLocalGuard();
			Enemy->SetPlayerCurrentlyVisible(true);
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

#endif
