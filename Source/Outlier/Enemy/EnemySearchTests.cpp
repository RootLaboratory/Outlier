#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyPoolDefinition.h"
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
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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
		// 더 가까운 대상이나 우선순위가 높은 Shooter도 유효한 현재 대상을 교체하지 않는다.
		if (Parameters != TEXT("Priority")) Other->SetActorLocation(FVector(100, 100, 300));
		Rooms->ReportRoomTargetContact(OtherObserver, Other, Other->GetActorLocation());
		Controller->SetCachedVisibleTargetForTesting(Other);
		Receiver->SetPlayerCurrentlyVisible(true);
		TestEqual(TEXT("A new visible/shared candidate cannot steal the current target"),
			Receiver->GetTrackedTarget(), static_cast<AActor*>(Target));
		if (Parameters == TEXT("Priority"))
		{
			Controller->SetCachedVisibleTargetForTesting(nullptr);
			Receiver->SetPlayerCurrentlyVisible(false);
			Receiver->ClearTargetTracking();
			Receiver->RefreshTargetTracking();
			TestEqual(TEXT("Without a current target, Shooter priority beats a closer Partner"),
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

#endif
