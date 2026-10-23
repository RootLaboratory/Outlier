#if WITH_DEV_AUTOMATION_TESTS

#include "CoreGlobals.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "TimerManager.h"

namespace
{
	struct FFirstSpawnTestWorld
	{
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;

		FFirstSpawnTestWorld()
		{
			const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), NAME_None,
				EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
			if (!World)
			{
				GEngine->DestroyWorldContext(World);
				return;
			}
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			GameInstance = NewObject<UGameInstance>(GEngine);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
		}

		~FFirstSpawnTestWorld()
		{
			if (World)
			{
				GEngine->ShutdownWorldNetDriver(World);
				World->DestroyWorld(true);
				GameInstance->Shutdown();
				World->SetPhysicsScene(nullptr);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
			}
		}

		void AdvanceTimers()
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(0.01f);
		}
	};

	URoomCombatDefinition* MakeTurretEncounter(UWorld* World, FGameplayTag RoomTag)
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& Room = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Room.RoomTag = RoomTag;
		FRoomCombatPhaseDefinition& Phase = Room.CombatPhases.AddDefaulted_GetRef();
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::ExternalTrigger;
		FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
		Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		Wave.ExpectedWaveTurretCount = 2;
		return Definition;
	}

	AAutoTurret* SpawnWaveZeroTurret(UWorld* World, FGameplayTag RoomTag, FName Id)
	{
		AAutoTurret* Turret = World->SpawnActorDeferred<AAutoTurret>(
			AAutoTurret::StaticClass(), FTransform::Identity);
		if (Turret)
		{
			Turret->ConfigureWaveRegistrationForTesting(RoomTag, 0, 0, Id);
			Turret->FinishSpawning(FTransform::Identity);
			if (!Turret->HasActorBegunPlay())
			{
				Turret->DispatchBeginPlay();
			}
		}
		return Turret;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSingleExternalEncounterTest,
	"Outlier.Room.FirstSpawn.SingleExternalEncounter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomSingleExternalEncounterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFirstSpawnTestWorld Fixture;
	if (!TestNotNull(TEXT("Test world"), Fixture.World)) { return false; }
	URoomCombatSubsystem* Combat = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
	if (!TestNotNull(TEXT("Combat subsystem"), Combat)) { return false; }
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	URoomCombatDefinition* Definition = MakeTurretEncounter(Fixture.World, RoomTag);
	Combat->SetCombatDefinitionForTesting(Definition);
	FRoomCombatRoomDefinition& Room = Definition->RoomDefinitions[0];
	TestTrue(TEXT("Turret-only external Wave permits pending SpawnPoint readiness"),
		Combat->IsSingleExternalTriggerEncounter(RoomTag));
	const FRoomCombatWaveDefinition ValidWave = Room.CombatPhases[0].Waves[0];
	Room.CombatPhases[0].Waves.Add(ValidWave);
	TestTrue(TEXT("Multiple Waves remain a single retry unit"),
		Combat->IsSingleExternalTriggerEncounter(RoomTag));
	const FRoomCombatPhaseDefinition ValidPhase = Room.CombatPhases[0];
	Room.CombatPhases.Add(ValidPhase);
	TestFalse(TEXT("Multiple phases cannot introduce intermediate saves"),
		Combat->IsSingleExternalTriggerEncounter(RoomTag));
	Room.CombatPhases.SetNum(1);
	Room.CombatPhases[0].StartPolicy = ERoomCombatPhaseStartPolicy::HackTrigger;
	TestFalse(TEXT("Hack triggers do not satisfy the elevator contract"),
		Combat->IsSingleExternalTriggerEncounter(RoomTag));
	Room.CombatPhases[0].StartPolicy = ERoomCombatPhaseStartPolicy::ExternalTrigger;
	Room.CombatPhases[0].Waves[1].ExpectedWaveTurretCount = 0;
	TestFalse(TEXT("Invalid later Wave rejects the entire encounter before closing"),
		Combat->IsSingleExternalTriggerEncounter(RoomTag));
	TestFalse(TEXT("Unknown room cannot satisfy the encounter contract"),
		Combat->IsSingleExternalTriggerEncounter(FGameplayTag()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomActualFirstSpawnTest,
	"Outlier.Room.FirstSpawn.ActualActivationAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomActualFirstSpawnTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFirstSpawnTestWorld Fixture;
	UWorld* World = Fixture.World;
	if (!TestNotNull(TEXT("Test world"), World)) { return false; }
	URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
	UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>();
	if (!TestNotNull(TEXT("Combat subsystem"), Combat)
		|| !TestNotNull(TEXT("Adaptation subsystem"), Adaptation)) { return false; }
	Adaptation->OnWorldBeginPlay(*World);
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	Combat->SetCombatDefinitionForTesting(MakeTurretEncounter(World, RoomTag));
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
	AAutoTurret* First = SpawnWaveZeroTurret(World, RoomTag, TEXT("Turret.FirstSpawn.1"));
	AAutoTurret* Second = SpawnWaveZeroTurret(World, RoomTag, TEXT("Turret.FirstSpawn.2"));
	if (!TestNotNull(TEXT("Room"), Room) || !TestNotNull(TEXT("First turret"), First)
		|| !TestNotNull(TEXT("Second turret"), Second)) { return false; }
	TestTrue(TEXT("Room registers"), Combat->RegisterRoom(Room, RoomTag));
	FRoomCombatTriggerContext Context;
	TestTrue(TEXT("External context captures current registration"),
		Combat->CreateTriggerContext(Room, RoomTag, FGameplayTag(), Context));
	int32 Notifications = 0;
	bool bResetFromListener = false;
	const FDelegateHandle Handle = Combat->OnWaveFirstSpawned.AddLambda(
		[&](FGameplayTag EventRoom, int32 Phase, int32 Wave, int32 Generation, FGuid Registration)
		{
			++Notifications;
			TestEqual(TEXT("Notification identifies exact Room"), EventRoom, RoomTag);
			TestEqual(TEXT("Notification includes first phase"), Phase, 0);
			TestEqual(TEXT("Notification includes Wave zero"), Wave, 0);
			TestEqual(TEXT("Notification preserves gameplay generation"), Generation, Context.GameplayGeneration);
			TestTrue(TEXT("Notification preserves registration token"), Registration == Context.RoomRegistrationId);
			if (bResetFromListener) { Combat->ResetRuntimeCombatState(); }
		});
	TestTrue(TEXT("Sequence starts"), Combat->StartTriggeredSequence(Room, Context));
	TestFalse(TEXT("Queued deployment is not actual spawn"), Combat->HasCurrentWaveSpawned(Context));
	TestEqual(TEXT("Sequence start sends no spawn notification"), Notifications, 0);
	First->NotifyDeploySequenceFinished(First->GetRoomWaveGameplayGeneration(), First->GetRoomWaveActivationSerial());
	TestTrue(TEXT("Successful first activation is immediately queryable"), Combat->HasCurrentWaveSpawned(Context));
	TestEqual(TEXT("Listener waits until activation caller finishes"), Notifications, 0);
	FRoomCombatTriggerContext Stale = Context;
	++Stale.GameplayGeneration;
	TestFalse(TEXT("Wrong generation cannot observe current spawn"), Combat->HasCurrentWaveSpawned(Stale));
	Stale = Context;
	Stale.RoomRegistrationId = FGuid::NewGuid();
	TestFalse(TEXT("Wrong registration cannot observe current spawn"), Combat->HasCurrentWaveSpawned(Stale));
	Second->NotifyDeploySequenceFinished(Second->GetRoomWaveGameplayGeneration(), Second->GetRoomWaveActivationSerial());
	Fixture.AdvanceTimers();
	TestEqual(TEXT("Two successful activations produce one first-spawn notification"), Notifications, 1);
	Second->NotifyDeploySequenceFinished(Second->GetRoomWaveGameplayGeneration(), Second->GetRoomWaveActivationSerial());
	Fixture.AdvanceTimers();
	TestEqual(TEXT("Duplicate deployment does not repeat the notification"), Notifications, 1);

	// 이전 수명의 예약 알림이 새 등록으로 넘어가지 않아야 한다.
	Combat->UnregisterRoom(Room);
	Combat->RegisterRoom(Room, RoomTag);
	Combat->CreateTriggerContext(Room, RoomTag, FGameplayTag(), Context);
	TestTrue(TEXT("Second lifetime starts"), Combat->StartTriggeredSequence(Room, Context));
	First->NotifyDeploySequenceFinished(First->GetRoomWaveGameplayGeneration(), First->GetRoomWaveActivationSerial());
	Combat->UnregisterRoom(Room);
	Combat->RegisterRoom(Room, RoomTag);
	Fixture.AdvanceTimers();
	TestEqual(TEXT("Re-registration discards the queued old-lifetime notification"), Notifications, 1);
	TestFalse(TEXT("Old lifetime query is rejected"), Combat->HasCurrentWaveSpawned(Context));

	// 알림 수신자가 Reset해도 활성화 호출의 집계 참조가 남아 있지 않아야 한다.
	Combat->CreateTriggerContext(Room, RoomTag, FGameplayTag(), Context);
	bResetFromListener = true;
	TestTrue(TEXT("Third lifetime starts"), Combat->StartTriggeredSequence(Room, Context));
	First->NotifyDeploySequenceFinished(First->GetRoomWaveGameplayGeneration(), First->GetRoomWaveActivationSerial());
	Fixture.AdvanceTimers();
	TestEqual(TEXT("New lifetime delivers exactly one notification"), Notifications, 2);
	TestFalse(TEXT("Reentrant Reset safely clears active combat"), Combat->GetActiveCombatRoomTag().IsValid());
	Combat->OnWaveFirstSpawned.Remove(Handle);
	return true;
}

#endif
