#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(ReturnHomeTag, "Room.ReturnTest01.1.1");

struct FRoomReturnWorld
{
	UWorld* World;
	UGameInstance* GameInstance;
	URoomCombatSubsystem* Combat;
	ARoomVolume* Home;
	AEnemyBase* Enemy;

	FRoomReturnWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RoomReturnTest")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		Combat = World->GetSubsystem<URoomCombatSubsystem>();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = ReturnHomeTag;
		Entry.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
		Combat->SetCombatDefinitionForTesting(Definition);
		Home = World->SpawnActor<ARoomVolume>();
		CastChecked<UBoxComponent>(Home->GetRootComponent())->SetBoxExtent(FVector(400));
		Combat->RegisterRoom(Home, ReturnHomeTag);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(800, 0, 0)), Params);
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(ReturnHomeTag);
	}

	~FRoomReturnWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool StartReturn() const
	{
		Enemy->EnterAlertFromPerception(FVector(900, 0, 0));
		return Enemy->CommitAlertToNonCombat();
	}

	void Advance(float Seconds) const
	{
		// UWorld는 큰 단일 DeltaTime을 제한한다. 작은 프레임으로 실제 게임 시계를 전진시킨다.
		float Remaining = Seconds;
		while (Remaining > UE_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Remaining, 0.1f);
			++GFrameCounter;
			World->Tick(LEVELTICK_TimeOnly, Step);
			Remaining -= Step;
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnArrivalTest, "Outlier.Enemy.RoomReturn.ActualHomeArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnArrivalTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	AEnemyBase* Enemy = Fixture.Enemy;
	TestTrue(TEXT("Alert clear is accepted outside home"), Fixture.StartReturn());
	TestEqual(TEXT("Outside home enters Returning"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	TestEqual(TEXT("Fallback current tag is still home"), Enemy->GetCurrentRoomTag(), ReturnHomeTag.GetTag());
	TestFalse(TEXT("Home tag alone cannot complete return"), Enemy->CompleteReturnHome());
	FVector Destination;
	TestTrue(TEXT("Return has an actual home destination"), Enemy->GetReturnHomeDestination(Destination));
	TestEqual(TEXT("Destination selection never teleports"), Enemy->GetActorLocation(), FVector(800, 0, 0));
	Enemy->SetActorLocation(FVector(399, 0, 0));
	TestFalse(TEXT("Capsule crossing home boundary is not arrival"), Enemy->CompleteReturnHome());
	Enemy->SetActorLocation(Destination);
	TestTrue(TEXT("Actual capsule arrival completes return"), Enemy->CompleteReturnHome());
	TestEqual(TEXT("Patrol resumes only after arrival"), Enemy->GetCombatState(), EEnemyCombatState::NonCombat);
	TestFalse(TEXT("Completion cannot repeat"), Enemy->CompleteReturnHome());
	TestEqual(TEXT("Movement never changes original ownership"), Enemy->GetDefaultRoomTag(), ReturnHomeTag.GetTag());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnInterruptionTest, "Outlier.Enemy.RoomReturn.InterruptionAndMissingHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnInterruptionTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	AEnemyBase* Enemy = Fixture.Enemy;
	TestTrue(TEXT("Start return"), Fixture.StartReturn());
	FVector InitialDestination;
	TestTrue(TEXT("Initial flight is selected before interruption"), Enemy->TrySelectReturnHomeDestination(InitialDestination));
	const uint64 PreStunSerial = Enemy->GetReturnHomeRequestSerial();
	Enemy->EnterStun();
	Fixture.Advance(4.0f);
	Enemy->RestoreStateAfterStun();
	TestEqual(TEXT("Stun restores unfinished return"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	TestFalse(TEXT("Stun restore never revives previous flight serial"), Enemy->IsReturnHomeRequestCurrent(PreStunSerial));
	TestEqual(TEXT("Stun retains previous request budget"), Enemy->GetReturnHomeRequestCount(), 1);
	TestFalse(TEXT("Resumed selection retains retry hold"), Enemy->TrySelectReturnHomeDestination(InitialDestination));
	Fixture.Advance(1.01f);
	TestTrue(TEXT("Stun duration does not consume failure recovery time"), Enemy->TrySelectReturnHomeDestination(InitialDestination));
	Enemy->EnterAlertFromPerception(FVector(1000, 0, 0));
	TestEqual(TEXT("Rediscovery interrupts return through existing alert flow"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
	TestFalse(TEXT("Old arrival cannot clear a fresh alert"), Enemy->CompleteReturnHome());
	TestTrue(TEXT("Alert timeout starts another return"), Enemy->CommitAlertToNonCombat());
	Enemy->EnterStun();
	Enemy->EnterAlertFromPerception(FVector(1100, 0, 0));
	Enemy->RestoreStateAfterStun();
	TestEqual(TEXT("Rediscovery during stun supersedes return"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
	Fixture.Combat->UnregisterRoom(Fixture.Home);
	TestTrue(TEXT("Missing home does not block alert clear"), Enemy->CommitAlertToNonCombat());
	TestEqual(TEXT("Missing home waits in Returning"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	FVector Destination;
	TestFalse(TEXT("Missing home has no unsafe fallback destination"), Enemy->GetReturnHomeDestination(Destination));
	Enemy->SetActorLocation(FVector::ZeroVector);
	TestFalse(TEXT("Missing home cannot complete return"), Enemy->CompleteReturnHome());
	Fixture.Combat->RegisterRoom(Fixture.Home, ReturnHomeTag);
	TestTrue(TEXT("Registration recovery restores destination"), Enemy->GetReturnHomeDestination(Destination));
	Enemy->SetEnemyPossessed(true);
	TestFalse(TEXT("Possession suppresses stale return completion"), Enemy->CompleteReturnHome());
	TestFalse(TEXT("Possession suppresses destination selection"), Enemy->GetReturnHomeDestination(Destination));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnRecoveryTest, "Outlier.Enemy.RoomReturn.BoundedRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnRecoveryTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	AEnemyBase* Enemy = Fixture.Enemy;
	TestTrue(TEXT("Start return"), Fixture.StartReturn());
	FVector Destination;
	TestTrue(TEXT("First request is immediate"), Enemy->TrySelectReturnHomeDestination(Destination));
	const uint64 FirstSerial = Enemy->GetReturnHomeRequestSerial();
	TestTrue(TEXT("Current return owns its flight"), Enemy->IsReturnHomeRequestCurrent(FirstSerial));
	Fixture.Advance(4.0f);
	TestTrue(TEXT("Progressing travel is outside failure recovery deadline"), Enemy->IsReturnHomeRequestCurrent(FirstSerial));
	for (int32 Attempt = 2; Attempt <= 3; ++Attempt)
	{
		TestFalse(TEXT("Failed flight reentry holds before retry"), Enemy->TrySelectReturnHomeDestination(Destination));
		TestFalse(TEXT("Failed flight serial becomes stale"), Enemy->IsReturnHomeRequestCurrent(FirstSerial));
		Fixture.Advance(1.01f);
		TestTrue(TEXT("Due retry obtains destination"), Enemy->TrySelectReturnHomeDestination(Destination));
		TestEqual(TEXT("Reentry retains request count"), Enemy->GetReturnHomeRequestCount(), Attempt);
	}
	TestFalse(TEXT("Third failed flight enters safe wait"), Enemy->TrySelectReturnHomeDestination(Destination));
	Fixture.Advance(2.0f);
	TestFalse(TEXT("Exhaustion does not replenish with time"), Enemy->TrySelectReturnHomeDestination(Destination));
	TestEqual(TEXT("Exhausted request count stays bounded"), Enemy->GetReturnHomeRequestCount(), 3);
	TestEqual(TEXT("Safe wait retains Returning"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	TestTrue(TEXT("Exhaustion enters explicit return guard"), Enemy->IsReturnHomeGuarding());
	TestTrue(TEXT("Fallback candidate work remains finite"), Enemy->GetReturnHomeCandidateChecks() <= 8);
	Fixture.Combat->OnRoomStartReadinessChanged.Broadcast(ReturnHomeTag);
	TestFalse(TEXT("Duplicate room readiness does not restart"), Enemy->TrySelectReturnHomeDestination(Destination));
	Fixture.Combat->NotifyMovementObstacleOpened(FBox(FVector(5000), FVector(5100)));
	TestFalse(TEXT("Unrelated obstacle opening does not restart"), Enemy->TrySelectReturnHomeDestination(Destination));
	const FBox RelevantOpening(FVector(350, -50, -50), FVector(450, 50, 50));
	Fixture.Combat->NotifyMovementObstacleOpened(RelevantOpening);
	TestTrue(TEXT("Relevant opening starts a new recovery episode"), Enemy->TrySelectReturnHomeDestination(Destination));
	TestFalse(TEXT("Meaningful recovery exits return guard"), Enemy->IsReturnHomeGuarding());
	TestEqual(TEXT("New meaningful condition replenishes count once"), Enemy->GetReturnHomeRequestCount(), 1);
	TestFalse(TEXT("New failed flight holds"), Enemy->TrySelectReturnHomeDestination(Destination));
	Fixture.Combat->NotifyMovementObstacleOpened(RelevantOpening);
	TestEqual(TEXT("Duplicate opening cannot reset current failure count"), Enemy->GetReturnHomeRequestCount(), 1);
	TestFalse(TEXT("Duplicate opening cannot bypass retry interval"), Enemy->TrySelectReturnHomeDestination(Destination));
	Enemy->EnterAlertFromPerception(FVector(900, 0, 0));
	TestFalse(TEXT("Rediscovery invalidates all old return flight results"), Enemy->IsReturnHomeRequestCurrent(FirstSerial));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnDeadlineTest, "Outlier.Enemy.RoomReturn.MissingHomeDeadlineAndRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnDeadlineTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	Fixture.Combat->UnregisterRoom(Fixture.Home);
	TestTrue(TEXT("Missing home starts Returning"), Fixture.StartReturn());
	FVector Destination;
	TestFalse(TEXT("Missing home counts first failed selection immediately"), Fixture.Enemy->TrySelectReturnHomeDestination(Destination));
	TestEqual(TEXT("Missing home uses one request"), Fixture.Enemy->GetReturnHomeRequestCount(), 1);
	Fixture.Advance(3.01f);
	TestFalse(TEXT("Recovery deadline prevents overdue retries"), Fixture.Enemy->TrySelectReturnHomeDestination(Destination));
	TestEqual(TEXT("Deadline exhaustion does not issue additional requests"), Fixture.Enemy->GetReturnHomeRequestCount(), 1);
	TestTrue(TEXT("Missing home deadline guards current position"), Fixture.Enemy->IsReturnHomeGuarding());
	Fixture.Combat->RegisterRoom(Fixture.Home, ReturnHomeTag);
	TestTrue(TEXT("Actual home registration restarts exhausted return"), Fixture.Enemy->TrySelectReturnHomeDestination(Destination));
	TestEqual(TEXT("Registration starts one new request"), Fixture.Enemy->GetReturnHomeRequestCount(), 1);
	TestEqual(TEXT("Registration recovery never teleports"), Fixture.Enemy->GetActorLocation(), FVector(800, 0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnFallbackTest, "Outlier.Enemy.RoomReturn.FallbackCacheAndGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnFallbackTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	AEnemyBase* Enemy = Fixture.Enemy;
	TestTrue(TEXT("Start return"), Fixture.StartReturn());
	FVector Exact;
	TestTrue(TEXT("First request selects exact home"), Enemy->TrySelectReturnHomeDestination(Exact));
	FVector Selected;
	TestTrue(TEXT("Active request exposes owned destination"), Enemy->GetSelectedReturnHomeDestination(Selected));
	TestEqual(TEXT("Exact destination is cached"), Selected, Exact);
	TestFalse(TEXT("Failed flight waits before fallback"), Enemy->TrySelectReturnHomeDestination(Selected));
	Fixture.Advance(1.01f);
	TestTrue(TEXT("Remaining request selects nearby clear candidate"), Enemy->TrySelectReturnHomeDestination(Selected));
	TestTrue(TEXT("Fallback is distinct and within 200cm"), !Selected.Equals(Exact, 1.0f)
		&& FVector::Distance(Selected, Exact) <= 200.01f);
	FVector Cached;
	TestTrue(TEXT("Fly consumes selected fallback cache"), Enemy->GetSelectedReturnHomeDestination(Cached));
	TestEqual(TEXT("Cache does not recompute exact home"), Cached, Selected);
	Enemy->SetActorLocation(Exact);
	TestFalse(TEXT("Home room membership alone cannot complete cached flight"), Enemy->CompleteReturnHome());
	Enemy->SetActorLocation(Selected);
	TestFalse(TEXT("Fallback arrival cannot complete ordinary home return"), Enemy->CompleteReturnHome());
	TestTrue(TEXT("Fallback arrival enters guard"), Enemy->IsReturnHomeGuarding());
	TestFalse(TEXT("Repeated guard completion cannot start patrol"), Enemy->CompleteReturnHome());
	TestEqual(TEXT("Guard remains Returning rather than patrol"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	TestFalse(TEXT("Guard invalidates old flight cache"), Enemy->GetSelectedReturnHomeDestination(Cached));
	const int32 Requests = Enemy->GetReturnHomeRequestCount();
	Fixture.Advance(5.0f);
	TestFalse(TEXT("Guard never starts another request with time"), Enemy->TrySelectReturnHomeDestination(Cached));
	TestEqual(TEXT("Guard retains request budget"), Enemy->GetReturnHomeRequestCount(), Requests);
	Enemy->EnterStun();
	Enemy->RestoreStateAfterStun();
	TestTrue(TEXT("Stun retains terminal guard"), Enemy->IsReturnHomeGuarding());
	Enemy->EnterAlertFromPerception(FVector(900, 0, 0));
	TestFalse(TEXT("Fresh alert clears guard"), Enemy->IsReturnHomeGuarding());
	TestFalse(TEXT("Fresh alert rejects stale arrival"), Enemy->CompleteReturnHome());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnBlockedFallbackTest, "Outlier.Enemy.RoomReturn.BlockedFallbackGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnBlockedFallbackTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	AEnemyBase* Enemy = Fixture.Enemy;
	TestTrue(TEXT("Start return"), Fixture.StartReturn());
	FVector Destination;
	TestTrue(TEXT("Exact selection precedes failed movement"), Enemy->TrySelectReturnHomeDestination(Destination));
	AActor* Obstacle = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Obstacle);
	Obstacle->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(400.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	TestFalse(TEXT("Failed movement preserves retry interval"), Enemy->TrySelectReturnHomeDestination(Destination));
	Fixture.Advance(1.01f);
	TestFalse(TEXT("Blocked fallback candidates terminate selection"), Enemy->TrySelectReturnHomeDestination(Destination));
	TestEqual(TEXT("At most eight endpoint candidates are examined"), Enemy->GetReturnHomeCandidateChecks(), 8);
	TestEqual(TEXT("Blocked endpoints spend only remaining selection request"), Enemy->GetReturnHomeRequestCount(), 2);
	TestTrue(TEXT("No safe candidate enters explicit current-position guard"), Enemy->IsReturnHomeGuarding());
	TestEqual(TEXT("Failed fallback never teleports"), Enemy->GetActorLocation(), FVector(800, 0, 0));
	Fixture.Advance(5.0f);
	TestFalse(TEXT("Guard does not restart candidate search"), Enemy->TrySelectReturnHomeDestination(Destination));
	TestEqual(TEXT("Endpoint work remains eight"), Enemy->GetReturnHomeCandidateChecks(), 8);
	return true;
}

#endif
