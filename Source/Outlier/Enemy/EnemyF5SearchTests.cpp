#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(F5SearchHomeTag, "Room.F5SearchTest01.1.1");

struct FEnemyF5SearchWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	AEnemyBase* Enemy = nullptr;
	AEnemyAIController* AI = nullptr;
	UEnemyRoomSubsystem* Rooms = nullptr;
	FEnemySearchSessionTaskInstanceData Session;

	explicit FEnemyF5SearchWorld(bool bCreateAISystem = false)
	{
		if (!GEngine)
		{
			return;
		}
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("F5SearchTest")));
		if (!World)
		{
			return;
		}
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		if (bCreateAISystem)
		{
			World->CreateAISystem();
		}
		World->InitializeActorsForPlay(FURL());
		Rooms = World->GetSubsystem<UEnemyRoomSubsystem>();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform::Identity, Spawn);
		if (Enemy)
		{
			AI = World->SpawnActor<AEnemyAIController>();
			if (AI)
			{
				// 소리의 자격 검사와 수색 소유권이 처음부터 같은 Controller를 사용한다.
				AI->Possess(Enemy);
			}
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(F5SearchHomeTag);
			Enemy->EnterCombatFromRoom(FVector(500, 0, 200), false);
			Session.Enemy = Enemy;
		}
	}

	~FEnemyF5SearchWorld()
	{
		if (!World)
		{
			return;
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	void Advance(int32 Frames) const
	{
		// 1/32초를 사용해 3초 바로 전과 정확한 경계를 같은 실제 월드 시계로 검증한다.
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
		}
	}

	AShooterCharacter* SpawnNoiseSource() const
	{
		UClass* Class = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Class)
		{
			return nullptr;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AShooterCharacter>(Class, FTransform(FVector(5000, 0, 0)), Spawn);
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF5GeneralSearchTest,
	"Outlier.Enemy.Search.F5.GeneralSearch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF5GeneralSearchTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("Deadline"), TEXT("Reentry"), TEXT("DuplicateHint"),
		TEXT("NewHint"), TEXT("StaleSerial"), TEXT("SightPreemption"), TEXT("NoisePreemption"),
		TEXT("HitPreemption"), TEXT("SameRoomReturn"), TEXT("Suppression"), TEXT("NoiseSearchExclusion"),
		TEXT("ReturnSharedPreemption"), TEXT("ReturnDuplicateShared") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyF5GeneralSearchTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyF5SearchWorld Fixture(Parameters == TEXT("SightPreemption"));
	AEnemyBase* Enemy = Fixture.Enemy;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)
		|| !TestNotNull(TEXT("Enemy exists"), Enemy)
		|| !TestNotNull(TEXT("Stable investigation controller exists"), Fixture.AI)
		|| !TestNotNull(TEXT("Room perception subsystem exists"), Fixture.Rooms))
	{
		return false;
	}
	TestTrue(TEXT("Test exercises authority gameplay"), Enemy->HasAuthority());
	TestEqual(TEXT("General guard default is three seconds"), Enemy->GetRuntimeStat().SearchGuardSeconds, 3.0f);
	const bool bReturnShared = Parameters == TEXT("ReturnSharedPreemption") || Parameters == TEXT("ReturnDuplicateShared");
	if (bReturnShared)
	{
		// 실제로 소비한 공유 사건을 남겨, 귀환 중의 중복과 새로운 관측 위치를 구분한다.
		Enemy->ApplySharedTargetContact(Enemy->GetLastKnownPlayerLocation(), true, 10);
	}
	TestEqual(TEXT("General session starts"), EnemySearchTesting::EnterSession(Fixture.Session), EStateTreeRunStatus::Running);
	TestTrue(TEXT("Exhausted points enter local guard"), Enemy->SetSearchPoints({}));
	const FEnemySearchEpisode Initial = Enemy->GetSearchEpisode();
	TestTrue(TEXT("Guard has an absolute three-second deadline"),
		FMath::IsNearlyEqual(Initial.GuardEndsAt - Fixture.World->GetTimeSeconds(), 3.0));
	TestFalse(TEXT("Guard cannot complete immediately"), Enemy->CanCompleteGeneralSearch(Initial.Serial));

	if (Parameters == TEXT("Reentry") || Parameters == TEXT("DuplicateHint"))
	{
		Fixture.Advance(32);
		Enemy->EnterSearchLocalGuard();
		TestEqual(TEXT("Repeated guard entry preserves the deadline"), Enemy->GetSearchEpisode().GuardEndsAt, Initial.GuardEndsAt);
		if (Parameters == TEXT("Reentry"))
		{
			EnemySearchTesting::ExitSession(Fixture.Session);
			TestEqual(TEXT("Session can reenter with the same evidence"),
				EnemySearchTesting::EnterSession(Fixture.Session), EStateTreeRunStatus::Running);
			TestTrue(TEXT("New owner receives a new episode serial"), Enemy->GetSearchEpisode().Serial > Initial.Serial);
			TestFalse(TEXT("Previous owner cannot complete a reentered episode"), Enemy->TryCompleteGeneralSearch(Initial.Serial));
		}
		else
		{
			Enemy->ApplySharedTargetContact(Initial.Center, true, 10);
			TestTrue(TEXT("Same-position update reuses the episode"), Enemy->BeginSearchEpisode());
			EnemySearchTesting::RefreshSessionTransitions(Fixture.Session);
			TestFalse(TEXT("Duplicate position does not request another search"), Fixture.Session.bShouldRestart);
		}
		TestEqual(TEXT("Reentry or duplicate information cannot restart guard time"), Enemy->GetSearchEpisode().GuardEndsAt, Initial.GuardEndsAt);
		Fixture.Advance(64);
		TestTrue(TEXT("Original guard deadline still completes"), Enemy->CanCompleteGeneralSearch(Enemy->GetSearchEpisode().Serial));
		return true;
	}

	Fixture.Advance(95);
	EnemySearchTesting::RefreshSessionTransitions(Fixture.Session);
	TestFalse(TEXT("Guard remains active immediately before three seconds"), Fixture.Session.bShouldReturnHome);
	Fixture.Advance(1);
	EnemySearchTesting::RefreshSessionTransitions(Fixture.Session);
	TestTrue(TEXT("Guard publishes return at exactly three seconds"), Fixture.Session.bShouldReturnHome);

	if (Parameters == TEXT("NewHint"))
	{
		Enemy->ApplySharedTargetContact(Initial.Center + FVector(400, 0, 0), true, 10);
		EnemySearchTesting::RefreshSessionTransitions(Fixture.Session);
		TestTrue(TEXT("New meaningful shared clue requests a search restart"), Fixture.Session.bShouldRestart);
		TestFalse(TEXT("New clue clears the previous return output before transitions"), Fixture.Session.bShouldReturnHome);
		TestFalse(TEXT("Old guard completion cannot override new information"), Enemy->TryCompleteGeneralSearch(Initial.Serial));
		TestEqual(TEXT("New clue keeps the enemy in combat"), Enemy->GetCombatState(), EEnemyCombatState::Combat);
		return true;
	}
	if (Parameters == TEXT("StaleSerial"))
	{
		TestFalse(TEXT("Wrong serial cannot finish a guard"), Enemy->TryCompleteGeneralSearch(Initial.Serial + 1));
		TestTrue(TEXT("Wrong serial leaves the current guard eligible"), Enemy->CanCompleteGeneralSearch(Initial.Serial));
		return true;
	}
	if (Parameters == TEXT("SightPreemption"))
	{
		AShooterCharacter* Target = Fixture.SpawnNoiseSource();
		UAIPerceptionComponent* Perception = Fixture.AI->GetPerceptionComponent();
		if (!TestNotNull(TEXT("Direct sight has an actual player target"), Target)
			|| !TestNotNull(TEXT("Direct sight has a registered perception listener"), Perception))
		{
			return false;
		}
		// bool만 켜면 추적 갱신이 대상 없는 시야를 해제한다. 실제 Sight 입력으로 캐시와 조사 중단을 함께 검증한다.
		const FAIStimulus Seen(*GetDefault<UAISense_Sight>(), 1.0f, Target->GetActorLocation(), Enemy->GetActorLocation());
		Perception->RegisterStimulus(Target, Seen);
		Perception->ProcessStimuli();
		TestTrue(TEXT("Actual sight stimulus establishes a directly visible player"),
			Enemy->IsPlayerCurrentlyVisible() && Fixture.AI->IsTargetDirectlyVisible(Target));
		EnemySearchTesting::RefreshSessionTransitions(Fixture.Session);
		TestFalse(TEXT("Direct sight clears the previous return output"), Fixture.Session.bShouldReturnHome);
		TestFalse(TEXT("Direct sight prevents general-search completion"), Enemy->CanCompleteGeneralSearch(Initial.Serial));
		return true;
	}
	if (Parameters == TEXT("Suppression"))
	{
		Enemy->SetEnemyPossessed(true);
		TestFalse(TEXT("Control transfer prevents an old guard completion"), Enemy->TryCompleteGeneralSearch(Initial.Serial));
		return true;
	}
	if (Parameters == TEXT("HitPreemption"))
	{
		FEnemyHitClue Clue;
		TestTrue(TEXT("Fresh hit clue is created"), Enemy->CreateHitClue(FVector(-500, 0, 0), Fixture.World->GetTimeSeconds(), 6.0, Clue));
		TestTrue(TEXT("Fresh hit clue is recorded"), Enemy->RecordHitClue(Clue));
		TestFalse(TEXT("Unconsumed hit wins even before its response update"), Enemy->TryCompleteGeneralSearch(Initial.Serial));
		Enemy->TickHitResponse(0.0f);
		TestTrue(TEXT("Valid hit starts its own response"), Enemy->HasHitResponse());
		TestFalse(TEXT("Old general-search completion cannot replace a hit response"), Enemy->TryCompleteGeneralSearch(Initial.Serial));
		return true;
	}
	if (Parameters == TEXT("NoisePreemption") || Parameters == TEXT("NoiseSearchExclusion"))
	{
		AShooterCharacter* Source = Fixture.SpawnNoiseSource();
		if (!TestNotNull(TEXT("Actual player noise source exists"), Source))
		{
			return false;
		}
		TestTrue(TEXT("A fresh player sound is accepted"), Enemy->RecordHeardNoise(Source, FVector(100, 0, 0), Fixture.World->GetTimeSeconds()));
		EnemySearchTesting::RefreshSessionTransitions(Fixture.Session);
		TestTrue(TEXT("Adoptable noise takes priority over return"), Fixture.Session.bShouldInvestigateNoise);
		TestFalse(TEXT("Noise clears stale return output"), Fixture.Session.bShouldReturnHome);
		TestFalse(TEXT("Old completion cannot override noise intent"), Enemy->TryCompleteGeneralSearch(Initial.Serial));
		if (Parameters == TEXT("NoiseSearchExclusion"))
		{
			FVector Destination;
			TestTrue(TEXT("Noise movement adopts its own investigation"), Enemy->BeginNoiseInvestigation(Destination));
			TestTrue(TEXT("Noise arrival enters a dedicated search at the actual position"),
				Enemy->BeginNoiseInvestigationSearch(Enemy->GetNoiseInvestigationSerial(), Enemy->GetActorLocation()));
			const FEnemySearchEpisode NoiseEpisode = Enemy->GetSearchEpisode();
			TestTrue(TEXT("Noise search retains its separate investigation identity"), NoiseEpisode.InvestigationSerial != 0);
			Enemy->EnterSearchLocalGuard();
			Fixture.Advance(96);
			TestFalse(TEXT("Three-second general guard cannot complete a dedicated noise search"),
				Enemy->CanCompleteGeneralSearch(NoiseEpisode.Serial));
			TestTrue(TEXT("Dedicated noise search retains its five-second budget"),
				FMath::IsNearlyEqual(NoiseEpisode.EndsAt - NoiseEpisode.StartedAt, 5.0));
		}
		return true;
	}
	if (Parameters == TEXT("SameRoomReturn") || bReturnShared)
	{
		URoomCombatSubsystem* Combat = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(Fixture.World);
		FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = F5SearchHomeTag;
		Entry.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
		Combat->SetCombatDefinitionForTesting(Definition);
		ARoomVolume* Home = Fixture.World->SpawnActor<ARoomVolume>();
		if (!TestNotNull(TEXT("Home room exists"), Home))
		{
			return false;
		}
		CastChecked<UBoxComponent>(Home->GetRootComponent())->SetBoxExtent(FVector(2000));
		Combat->RegisterRoom(Home, F5SearchHomeTag);
		TestTrue(TEXT("Enemy is already inside its home room"), Enemy->IsInsideDefaultRoom());
		Fixture.Rooms->SetActiveRoomTargetForTesting(F5SearchHomeTag, Initial.Center, false);
		TestTrue(TEXT("Room combat is active before individual return"), Fixture.Rooms->IsRoomInCombat(F5SearchHomeTag));
	}
	// 정상 종료는 실제 Task 경로로 실행한다. 부모 출력만 검사해 귀환 연결 누락을 숨기지 않는다.
	FEnemySearchProgressTaskInstanceData Finish;
	Finish.Enemy = Enemy;
	Finish.Action = EEnemySearchProgress::FinishGeneralSearch;
	TestEqual(TEXT("Completion task commits the return"), EnemySearchTesting::Progress(Finish), EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("Guard completion enters Returning even within the same home room"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	TestFalse(TEXT("Committed guard cannot complete again"), Enemy->TryCompleteGeneralSearch(Initial.Serial));
	if (Parameters == TEXT("SameRoomReturn") || bReturnShared)
	{
		TestTrue(TEXT("An individual return cannot end room combat"), Fixture.Rooms->IsRoomInCombat(F5SearchHomeTag));
	}
	if (bReturnShared)
	{
		FVector Destination;
		if (!TestTrue(TEXT("Return selects an actual home request"), Enemy->TrySelectReturnHomeDestination(Destination)))
		{
			return false;
		}
		const uint64 ReturnSerial = Enemy->GetReturnHomeRequestSerial();
		const int32 ReturnRequests = Enemy->GetReturnHomeRequestCount();
		TestTrue(TEXT("Selected return owns a current request"), Enemy->IsReturnHomeRequestCurrent(ReturnSerial));
		Enemy->ApplySharedTargetContact(Initial.Center, true, 10);
		Enemy->ApplySharedTargetContact(Initial.Center, true, 11);
		Enemy->ApplySharedTargetContact(Initial.Center + FVector(400, 0, 0), true, 9);
		TestEqual(TEXT("Repeated and same-position shared evidence preserves Returning"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
		TestEqual(TEXT("Ignored evidence cannot restart return requests"), Enemy->GetReturnHomeRequestCount(), ReturnRequests);
		TestTrue(TEXT("Ignored evidence preserves the owned return request"), Enemy->IsReturnHomeRequestCurrent(ReturnSerial));
		if (Parameters == TEXT("ReturnSharedPreemption"))
		{
			const FVector NewLocation = Initial.Center + FVector(400, 0, 0);
			Enemy->ApplySharedTargetContact(NewLocation, true, 12);
			TestEqual(TEXT("A meaningfully changed fresh shared clue interrupts return"), Enemy->GetCombatState(), EEnemyCombatState::Combat);
			TestTrue(TEXT("New shared observation remains available for investigation"), Enemy->HasFreshSharedTargetLocation());
			TestTrue(TEXT("Investigation uses the new fixed observation location"), Enemy->GetSharedTargetLocation().Equals(NewLocation));
			TestFalse(TEXT("Old return result cannot apply after new investigation"), Enemy->IsReturnHomeRequestCurrent(ReturnSerial));
			TestTrue(TEXT("Shared interruption leaves room combat active"), Fixture.Rooms->IsRoomInCombat(F5SearchHomeTag));
		}
	}
	return true;
}

#endif
