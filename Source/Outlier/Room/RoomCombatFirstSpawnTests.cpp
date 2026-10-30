#if WITH_DEV_AUTOMATION_TESTS

#include "CoreGlobals.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Curves/CurveFloat.h"
#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "Misc/AutomationTest.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

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
			GameInstance->OnWorldChanged(nullptr, World);
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

	Room.CombatPhases[0] = ValidPhase;
	TestTrue(TEXT("Elevator still accepts a single phase"), Combat->IsElevatorEncounter(RoomTag));
	for (int32 Index = 1; Index < 3; ++Index)
	{
		FRoomCombatPhaseDefinition Next = ValidPhase;
		Next.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		Room.CombatPhases.Add(Next);
		TestTrue(TEXT("Elevator accepts automatic successors"), Combat->IsElevatorEncounter(RoomTag));
	}
	for (ERoomCombatPhaseStartPolicy Policy : {ERoomCombatPhaseStartPolicy::ExternalTrigger,
		ERoomCombatPhaseStartPolicy::HackTrigger, ERoomCombatPhaseStartPolicy::InitialDetection})
	{
		Room.CombatPhases[2].StartPolicy = Policy;
		TestFalse(TEXT("Later non-automatic phase is rejected before closing"), Combat->IsElevatorEncounter(RoomTag));
	}
	Room.CombatPhases[2].StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
	Room.CombatPhases[2].Waves.Reset();
	TestFalse(TEXT("Empty later phase is rejected"), Combat->IsElevatorEncounter(RoomTag));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorMultiPhaseCombatTest,
	"Outlier.Elevator.Combat.MultiPhaseLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorMultiPhaseCombatTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 실제 터렛 활성화/사망 집계로 차수를 진행한다. 완료 이벤트를 만들어 정상 진행을 대신하지 않는다.
	for (int32 PhaseCount : {1, 2, 3})
	{
		FFirstSpawnTestWorld Fixture;
		UWorld* World = Fixture.World;
		if (!TestNotNull(TEXT("Multi-phase world"), World)) return false;
		URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
		UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>();
		AElevatorShaftController* Controller = World->SpawnActor<AElevatorShaftController>();
		AElevatorShaftSection* Section = World->SpawnActor<AElevatorShaftSection>();
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		if (!TestNotNull(TEXT("Combat"), Combat) || !TestNotNull(TEXT("Adaptation"), Adaptation)
			|| !TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Section"), Section)
			|| !TestNotNull(TEXT("Room"), Room)) return false;
		Adaptation->OnWorldBeginPlay(*World);
		Controller->Combat = Combat;
		Controller->Motion.ExecutionId = FGuid::NewGuid();
		Controller->CombatRoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		Controller->EntranceSection = Section;
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Section);
		Mesh->SetExtendedBounds(FBoxSphereBounds(FBox(FVector(-1, -1, 0), FVector(1, 1, 1450))));
		FObjectProperty* HeightMesh = FindFProperty<FObjectProperty>(AElevatorShaftSection::StaticClass(), TEXT("HeightReferenceMesh"));
		if (!TestNotNull(TEXT("Height reference property"), HeightMesh)) return false;
		HeightMesh->SetObjectPropertyValue_InContainer(Section, Mesh);
		for (bool bAcceleration : {true, false})
		{
			UCurveFloat* Curve = NewObject<UCurveFloat>(Controller);
			for (int32 Index = 0; Index <= 1; ++Index)
			{
				const FKeyHandle Handle = Curve->FloatCurve.AddKey(Index, bAcceleration ? Index : 1 - Index);
				Curve->FloatCurve.SetKeyInterpMode(Handle, RCIM_Cubic);
				FRichCurveKey& Key = Curve->FloatCurve.GetKey(Handle);
				Key.TangentMode = RCTM_User;
				Key.ArriveTangent = Key.LeaveTangent = 0.f;
			}
			if (bAcceleration) Controller->AccelerationCurve = Curve;
			else Controller->DecelerationCurve = Curve;
		}
		FString Error;
		Controller->bProfilesReady = Controller->BuildMotionProfiles(Error);
		if (!TestTrue(TEXT("Motion profiles"), Controller->bProfilesReady)) return false;
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& Encounter = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Encounter.RoomTag = Controller->CombatRoomTag;
		TArray<AAutoTurret*> Turrets;
		for (int32 Phase = 0; Phase < PhaseCount; ++Phase)
		{
			FRoomCombatPhaseDefinition& Entry = Encounter.CombatPhases.AddDefaulted_GetRef();
			Entry.StartPolicy = Phase == 0 ? ERoomCombatPhaseStartPolicy::ExternalTrigger : ERoomCombatPhaseStartPolicy::Automatic;
			Entry.DelayBeforeStartSeconds = Phase == 1 ? 0.2f : 0.f;
			for (int32 Wave = 0; Wave < 2; ++Wave)
			{
				FRoomCombatWaveDefinition& Roster = Entry.Waves.AddDefaulted_GetRef();
				Roster.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
				Roster.ExpectedWaveTurretCount = 1;
				AAutoTurret* Turret = World->SpawnActorDeferred<AAutoTurret>(AAutoTurret::StaticClass(), FTransform::Identity);
				if (!TestNotNull(TEXT("Phase turret"), Turret)) return false;
				Turret->ConfigureWaveRegistrationForTesting(Encounter.RoomTag, Phase, Wave,
					FName(*FString::Printf(TEXT("Turret.Elevator.%d.%d"), Phase, Wave)));
				Turret->FinishSpawning(FTransform::Identity);
				if (!Turret->HasActorBegunPlay()) Turret->DispatchBeginPlay();
				Turrets.Add(Turret);
			}
		}
		Combat->SetCombatDefinitionForTesting(Definition);
		if (!TestTrue(TEXT("Room registers"), Combat->RegisterRoom(Room, Encounter.RoomTag))) return false;
		Combat->OnCombatLifecycleEvent.AddUObject(Controller, &AElevatorShaftController::OnCombatEvent);
		Combat->OnWaveFirstSpawned.AddUObject(Controller, &AElevatorShaftController::OnFirstSpawn);
		TestTrue(TEXT("Capture starting context"), Combat->CreateTriggerContext(Controller, Encounter.RoomTag,
			FGameplayTag(), Controller->CombatContext));
		Controller->Motion.Generation = Controller->CombatContext.GameplayGeneration;
		Controller->StartCombat();
		TestEqual(TEXT("Requests alone do not move walls"), Controller->GetShaftState(), EElevatorShaftState::AwaitingSpawn);
		double StartTime = 0;
		double Distance = 0;
		uint8 Segments = 0;
		for (int32 Index = 0; Index < Turrets.Num(); ++Index)
		{
			AAutoTurret* Turret = Turrets[Index];
			Turret->NotifyDeploySequenceFinished(Turret->GetRoomWaveGameplayGeneration(), Turret->GetRoomWaveActivationSerial());
			Fixture.AdvanceTimers();
			if (Index == 0)
			{
				// TimerManager 진행과 서버 시계는 별개다. 경계가 아닌 운행 중 위치를 명시한다.
				Controller->Motion.StartServerTime -= 1.0;
				Controller->UpdateMotionAudio();
				StartTime = Controller->Motion.StartServerTime;
				Distance = Controller->CurrentDistance();
				Segments = Controller->PresentedSegments;
				FRoomCombatLifecycleEvent Fake{Encounter.RoomTag, ERoomCombatEvent::RoomCleared, 0,
					Controller->CombatContext.GameplayGeneration, Controller->CombatContext.RoomRegistrationId, Controller->Motion.ExecutionId};
				Controller->OnCombatEvent(Fake);
				TestEqual(TEXT("Uncleared Room rejects fabricated clear"), Controller->GetShaftState(), EElevatorShaftState::RisingCombat);
			}
			TestEqual(TEXT("Later spawns do not restart the clock"), Controller->Motion.StartServerTime, StartTime);
			TestEqual(TEXT("Later spawns retain integrated distance"), Controller->CurrentDistance(), Distance);
			TestEqual(TEXT("Later spawns do not replay motion segments"), Controller->PresentedSegments, Segments);
			Combat->NotifyEnemyDefeated(Turret);
			if (Index + 1 < Turrets.Num())
			{
				TestEqual(TEXT("Intermediate Wave/Phase keeps rising"), Controller->GetShaftState(), EElevatorShaftState::RisingCombat);
				if (Combat->IsPhaseDelayPending(Encounter.RoomTag))
				{
					TestTrue(TEXT("Phase delay keeps checkpoint hold"), Combat->IsCombatPhaseCheckpointDeferred(
						Encounter.RoomTag, Controller->CombatContext.GameplayGeneration));
					++GFrameCounter;
					World->GetTimerManager().Tick(0.f);
					++GFrameCounter;
					World->GetTimerManager().Tick(0.25f);
				}
			}
		}
		TestEqual(TEXT("Final phase clears the Room"), Combat->GetRoomState(Encounter.RoomTag), ERoomCombatState::Cleared);
		TestEqual(TEXT("Final phase reserves one finishing section"), Controller->GetShaftState(), EElevatorShaftState::FinishingCurrentSection);
		TestFalse(TEXT("Final completion releases checkpoint hold"), Combat->IsCombatPhaseCheckpointDeferred(
			Encounter.RoomTag, Controller->CombatContext.GameplayGeneration));
		const double Boundary = Controller->Motion.FinishDistance;
		FRoomCombatLifecycleEvent Duplicate{Encounter.RoomTag, ERoomCombatEvent::RoomCleared, PhaseCount - 1,
			Controller->CombatContext.GameplayGeneration, Controller->CombatContext.RoomRegistrationId, Controller->Motion.ExecutionId};
		Controller->OnCombatEvent(Duplicate);
		TestEqual(TEXT("Duplicate clear does not extend finishing distance"), Controller->Motion.FinishDistance, Boundary);
		Combat->OnCombatLifecycleEvent.RemoveAll(Controller);
		Combat->OnWaveFirstSpawned.RemoveAll(Controller);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorMultiPhaseCancellationTest,
	"Outlier.Elevator.Combat.MultiPhaseCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorMultiPhaseCancellationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FFirstSpawnTestWorld Fixture;
	UWorld* World = Fixture.World;
	if (!TestNotNull(TEXT("Cancellation world"), World)) return false;
	URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
	UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>();
	AElevatorShaftController* Controller = World->SpawnActor<AElevatorShaftController>();
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
	if (!TestNotNull(TEXT("Combat"), Combat) || !TestNotNull(TEXT("Adaptation"), Adaptation)
		|| !TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Room"), Room)) return false;
	Adaptation->OnWorldBeginPlay(*World);
	Controller->Combat = Combat;
	Controller->Motion.ExecutionId = FGuid::NewGuid();
	Controller->CombatRoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	URoomCombatDefinition* Definition = MakeTurretEncounter(World, Controller->CombatRoomTag);
	FRoomCombatPhaseDefinition Automatic = Definition->RoomDefinitions[0].CombatPhases[0];
	Automatic.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
	Automatic.DelayBeforeStartSeconds = 0.2f;
	Definition->RoomDefinitions[0].CombatPhases.Add(Automatic);
	Combat->SetCombatDefinitionForTesting(Definition);
	AAutoTurret* First = SpawnWaveZeroTurret(World, Controller->CombatRoomTag, TEXT("Turret.Cancel.0"));
	AAutoTurret* Second = SpawnWaveZeroTurret(World, Controller->CombatRoomTag, TEXT("Turret.Cancel.1"));
	AAutoTurret* Later = World->SpawnActorDeferred<AAutoTurret>(AAutoTurret::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("First"), First) || !TestNotNull(TEXT("Second"), Second)
		|| !TestNotNull(TEXT("Later"), Later)) return false;
	Later->ConfigureWaveRegistrationForTesting(Controller->CombatRoomTag, 1, 0, TEXT("Turret.Cancel.Later"));
	Later->FinishSpawning(FTransform::Identity);
	if (!Later->HasActorBegunPlay()) Later->DispatchBeginPlay();
	// 두 번째 차수도 명세와 같은 수의 터렛을 갖추되, 취소 전에는 죽이지 않는다.
	Definition->RoomDefinitions[0].CombatPhases[1].Waves[0].ExpectedWaveTurretCount = 1;
	Combat->OnCombatLifecycleEvent.AddUObject(Controller, &AElevatorShaftController::OnCombatEvent);
	FRoomCombatLifecycleEvent PreviousCancel;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		TestTrue(TEXT("Retry registers a fresh Room"), Combat->RegisterRoom(Room, Controller->CombatRoomTag));
		TestTrue(TEXT("Retry captures fresh context"), Combat->CreateTriggerContext(Controller, Controller->CombatRoomTag,
			FGameplayTag(), Controller->CombatContext));
		Controller->Motion.Generation = Controller->CombatContext.GameplayGeneration;
		Controller->StartCombat();
		const FGuid Execution = Controller->Motion.ExecutionId;
		const FRoomCombatTriggerContext Context = Controller->CombatContext;
		FRoomCombatLifecycleEvent Cancel{Controller->CombatRoomTag, ERoomCombatEvent::Cancelled, 1,
			Context.GameplayGeneration, Context.RoomRegistrationId, Execution};
		for (int32 InvalidCase = 0; InvalidCase < 4; ++InvalidCase)
		{
			FRoomCombatLifecycleEvent Invalid = Cancel;
			if (InvalidCase == 0) Invalid.RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.2"));
			if (InvalidCase == 1) ++Invalid.GameplayGeneration;
			if (InvalidCase == 2) Invalid.RoomRegistrationId = FGuid::NewGuid();
			if (InvalidCase == 3) Invalid.ExecutionId = FGuid::NewGuid();
			Controller->OnCombatEvent(Invalid);
			TestEqual(TEXT("Invalid cancellation does not change current execution"), Controller->Motion.ExecutionId, Execution);
		}
		if (Attempt > 0)
		{
			Controller->OnCombatEvent(PreviousCancel);
			TestEqual(TEXT("Previous registration cannot cancel retry"), Controller->Motion.ExecutionId, Execution);
			Combat->ReleaseEncounterCheckpointHold(Controller->CombatRoomTag, PreviousCancel.ExecutionId);
			TestTrue(TEXT("Old release cannot remove retry hold"), Combat->IsEncounterContextCurrent(Context, Execution));
		}
		First->NotifyDeploySequenceFinished(First->GetRoomWaveGameplayGeneration(), First->GetRoomWaveActivationSerial());
		Second->NotifyDeploySequenceFinished(Second->GetRoomWaveGameplayGeneration(), Second->GetRoomWaveActivationSerial());
		Combat->NotifyEnemyDefeated(First);
		Combat->NotifyEnemyDefeated(Second);
		TestEqual(TEXT("Cancellation occurs after first phase"), Combat->GetCurrentCombatPhaseIndex(Controller->CombatRoomTag), 1);
		TestTrue(TEXT("Automatic successor has a real delay"), Combat->IsPhaseDelayPending(Controller->CombatRoomTag));
		if (Attempt > 0)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(0.f);
			++GFrameCounter;
			World->GetTimerManager().Tick(0.25f);
			Later->NotifyDeploySequenceFinished(Later->GetRoomWaveGameplayGeneration(), Later->GetRoomWaveActivationSerial());
			TestFalse(TEXT("Later phase is now active"), Combat->IsPhaseDelayPending(Controller->CombatRoomTag));
		}
		if (Attempt == 2) Combat->ResetRuntimeCombatState();
		else Combat->UnregisterRoom(Room);
		TestEqual(TEXT("Later-phase cancellation resets Controller"), Controller->GetShaftState(), EElevatorShaftState::WaitingForPair);
		TestTrue(TEXT("Cancellation invalidates old execution"), Controller->Motion.ExecutionId != Execution);
		TestFalse(TEXT("Cancellation releases checkpoint deferral"), Combat->IsCombatPhaseCheckpointDeferred(
			Controller->CombatRoomTag, Context.GameplayGeneration));
		TestFalse(TEXT("Cancellation clears phase delay"), Combat->IsPhaseDelayPending(Controller->CombatRoomTag));
		Fixture.AdvanceTimers();
		TestFalse(TEXT("Old delay cannot restart removed Room"), Combat->IsRoomRegistered(Controller->CombatRoomTag));
		PreviousCancel = Cancel;
	}
	Combat->OnCombatLifecycleEvent.RemoveAll(Controller);
	return true;
}

#endif
