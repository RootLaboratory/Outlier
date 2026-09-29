#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Curves/CurveFloat.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Interaction/InteractableDoor.h"
#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "Interaction/Level1SuitUpgradeDoor.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Save/OutlierCheckpoint.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevel1SuitUpgradeDoorTest,
	"Outlier.Interaction.Level1SuitUpgradeDoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevel1SuitUpgradeDoorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Level 1 door test world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(GameInstance);
	GameInstance->Init();
	World->InitializeActorsForPlay(FURL());
	const auto CleanupWorld = [World, GameInstance]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	const auto BeginActor = [](AActor* Actor)
	{
		if (Actor && !Actor->HasActorBegunPlay())
		{
			Actor->DispatchBeginPlay();
		}
	};

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01")));
	const FGameplayTag EntryRoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
	FRoomCombatRoomDefinition& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
	RoomDefinition.RoomTag = RoomTag;
	FRoomCombatPhaseDefinition& Phase = RoomDefinition.CombatPhases.AddDefaulted_GetRef();
	Phase.StartPolicy = ERoomCombatPhaseStartPolicy::ExternalTrigger;
	Phase.ExpectedStartSpawnPointCount = 2;
	FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
	Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
	Wave.RequiredSpawnPointTag = RoomTag;
	Wave.Enemies.AddDefaulted_GetRef().EnemyClass = AEnemyBase::StaticClass();
	Combat->SetCombatDefinitionForTesting(Definition);
	int32 CombatStartCount = 0;
	Combat->CombatEventObserverForTesting = [&CombatStartCount](
		FGameplayTag, ERoomCombatEvent Event, int32)
	{
		if (Event == ERoomCombatEvent::SequenceStarted)
		{
			++CombatStartCount;
		}
	};
	UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& PoolEntry = PoolDefinition->Entries.AddDefaulted_GetRef();
	PoolEntry.EnemyClass = AEnemyBase::StaticClass();
	PoolEntry.PrewarmCount = 1;
	PoolEntry.MaxCount = 1;
	TestTrue(TEXT("First Wave Enemy pool is ready"),
		World->GetSubsystem<UEnemyPoolSubsystem>()->PrewarmPool(PoolDefinition));
	ARoomVolume* Room = World->SpawnActorDeferred<ARoomVolume>(
		ARoomVolume::StaticClass(), FTransform::Identity);
	UCurveFloat* Curve = NewObject<UCurveFloat>(GetTransientPackage());
	Curve->FloatCurve.AddKey(0.0f, 0.0f);
	Curve->FloatCurve.AddKey(1.0f, 1.0f);
	if (!TestNotNull(TEXT("Room is spawned"), Room))
	{
		CleanupWorld();
		return false;
	}
	if (FStructProperty* RoomTagProperty = FindFProperty<FStructProperty>(
		ARoomVolume::StaticClass(), TEXT("RoomTag")))
	{
		*RoomTagProperty->ContainerPtrToValuePtr<FGameplayTag>(Room) = EntryRoomTag;
	}
	CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(500.0f));
	Room->FinishSpawning(FTransform::Identity);
	BeginActor(Room);
	// 입장 Volume은 전투 Definition에 등록하지 않는다. 전투 Volume은 별도 태그와 위치를 가진다.
	ARoomVolume* CombatRoom = World->SpawnActorDeferred<ARoomVolume>(
		ARoomVolume::StaticClass(), FTransform(FVector(3000.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("Combat Room is spawned"), CombatRoom))
	{
		CleanupWorld();
		return false;
	}
	if (FStructProperty* RoomTagProperty = FindFProperty<FStructProperty>(
		ARoomVolume::StaticClass(), TEXT("RoomTag")))
	{
		*RoomTagProperty->ContainerPtrToValuePtr<FGameplayTag>(CombatRoom) = RoomTag;
	}
	CastChecked<UBoxComponent>(CombatRoom->GetRootComponent())->SetBoxExtent(FVector(500.0f));
	CombatRoom->FinishSpawning(FTransform(FVector(3000.0f, 0.0f, 0.0f)));
	BeginActor(CombatRoom);
	TestEqual(TEXT("ExternalTrigger room waits for the door"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::WaitingForTrigger);
	AOutlierCheckpoint* Checkpoint = World->SpawnActorDeferred<AOutlierCheckpoint>(
		AOutlierCheckpoint::StaticClass(), FTransform(FVector(1500.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("Entrance checkpoint is spawned"), Checkpoint))
	{
		CleanupWorld();
		return false;
	}
	if (FNameProperty* IdProperty = FindFProperty<FNameProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("CheckpointId")))
	{
		*IdProperty->ContainerPtrToValuePtr<FName>(Checkpoint) = TEXT("Test.Level1.Entrance");
	}
	Checkpoint->FinishSpawning(FTransform(FVector(1500.0f, 0.0f, 0.0f)));
	BeginActor(Checkpoint);
	ALevel1SuitUpgradeDoor* Door = World->SpawnActorDeferred<ALevel1SuitUpgradeDoor>(
		ALevel1SuitUpgradeDoor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Level 1 door is spawned"), Door))
	{
		CleanupWorld();
		return false;
	}
	Door->DoorCurve = Curve;
	Door->DoorId = TEXT("Test.Level1Door.Door");
	Door->TargetRoomVolume = Room;
	Door->CombatRoomVolume = CombatRoom;
	Door->EntranceCheckpoint = Checkpoint;
	Door->FinishSpawning(FTransform::Identity);
	BeginActor(Door);
	TestTrue(TEXT("Level 1 door starts open without a motion"), Door->IsDoorOpen());
	int32 OpenedCount = 0;
	Door->OnLevel1DoorOpened.AddLambda([&OpenedCount](AActor*, uint32) { ++OpenedCount; });

	AOutlierPlayerState* ShooterPS = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerPS = World->SpawnActor<AOutlierPlayerState>();
	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(
			ShooterClass, FTransform(FVector(1000.0f, 0.0f, 0.0f)))
		: nullptr;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(), FTransform(FVector(1100.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("Shooter Blueprint class"), ShooterClass)
		|| !TestNotNull(TEXT("Shooter state"), ShooterPS)
		|| !TestNotNull(TEXT("Partner state"), PartnerPS)
		|| !TestNotNull(TEXT("Shooter character"), Shooter)
		|| !TestNotNull(TEXT("Partner character"), Partner))
	{
		CleanupWorld();
		return false;
	}
	ShooterPS->SetPairId(1);
	PartnerPS->SetPairId(1);
	ShooterPS->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PartnerPS->SetPlayerRole(EOutlierPlayerRole::Partner);
	ShooterPS->SetShooterCharacter(Shooter);
	PartnerPS->SetPartnerCharacter(Partner);
	Shooter->GetRoomTagComp()->AssignDefaultRoomTag(EntryRoomTag);
	Partner->GetRoomTagComp()->AssignDefaultRoomTag(EntryRoomTag);
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("Room tag alone does not close the door"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("One player inside keeps the door open"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Partner->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	TestTrue(TEXT("First player leaving before the second enters keeps the door open"), Door->IsDoorOpen());
	// 캡슐 오버랩은 원점이 Box 경계(500) 안으로 들어오기 전에 발생할 수 있다.
	Shooter->SetActorLocation(FVector(550.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("Capsule overlap alone does not seal the room"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(0.0f, 0.0f, 450.0f));
	World->Tick(LEVELTICK_All, 0.11f);
	TestTrue(TEXT("Capsule crossing the Room ceiling does not start closing"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(480.0f, 0.0f, 0.0f));
	World->Tick(LEVELTICK_All, 0.11f);
	TestTrue(TEXT("Capsule still crossing the Room boundary does not start closing"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	// 단순 Automation 월드의 반복 Tick은 GFrameCounter를 올리지 않아 타이머를 재실행하지 않는다.
	// 위치가 완전히 들어온 뒤 입장 이벤트를 보내 동일한 서버 입장 판정을 확인한다.
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("Entry event starts closing after both capsules fully enter"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	TestTrue(TEXT("Leaving during close cancels the entry and reopens"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("Both players reentering starts a fresh close"), Door->IsDoorOpen());
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);

	const uint32 Generation = World->GetSubsystem<UOutlierArenaSubsystem>()->GetGameplayGeneration();
	ShooterPS->SetAcquiredSuit(true);
	PartnerPS->SetAcquiredSuit(true);
	ShooterPS->ReportStatAllocatorUIOpened(Generation);
	ShooterPS->ReportStatAllocatorUIClosed(Generation);
	TestFalse(TEXT("One completed UI does not reopen"), Door->IsDoorOpen());
	PartnerPS->ReportStatAllocatorUIOpened(Generation);
	PartnerPS->ReportStatAllocatorUIClosed(Generation);
	TestTrue(TEXT("Shooter UI completion is recorded"),
		ShooterPS->IsStatAllocatorUICompletedForGeneration(Generation));
	TestTrue(TEXT("Partner UI completion is recorded"),
		PartnerPS->IsStatAllocatorUICompletedForGeneration(Generation));
	TestFalse(TEXT("Both UIs wait for close animation"), Door->IsDoorOpen());
	TestEqual(TEXT("Opening has not completed"), OpenedCount, 0);
	TestTrue(TEXT("Door ticks while closing"), Door->IsActorTickEnabled());
	Partner->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestTrue(TEXT("Close completion rechecks location even without an overlap event"), Door->IsDoorOpen());
	TestEqual(TEXT("Cancelled close does not broadcast opening"), OpenedCount, 0);
	Partner->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	TestFalse(TEXT("Reentry closes the door after a missed overlap"), Door->IsDoorOpen());
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestTrue(TEXT("Door reopens after close animation"), Door->IsDoorOpen());
	TestEqual(TEXT("Opening request is not completion"), OpenedCount, 0);
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestEqual(TEXT("Server opening completes once"), OpenedCount, 1);
	if (FBoolProperty* ActiveProperty = FindFProperty<FBoolProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("bActivationConditionSatisfied")))
	{
		TestTrue(TEXT("Open door activates entrance checkpoint"),
			ActiveProperty->GetPropertyValue_InContainer(Checkpoint));
	}
	TestEqual(TEXT("Door waits for the configured SpawnPoint roster"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::WaitingForTrigger);
	ARoomCombatSpawnPoint* FirstPoint = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(), FTransform(FVector(300.0f, 0.0f, 0.0f)));
	ARoomCombatSpawnPoint* SecondPoint = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(), FTransform(FVector(-300.0f, 0.0f, 0.0f)));
	ARoomCombatSpawnPoint* ThirdPoint = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(), FTransform(FVector(0.0f, 300.0f, 0.0f)));
	if (!TestNotNull(TEXT("First SpawnPoint"), FirstPoint)
		|| !TestNotNull(TEXT("Second SpawnPoint"), SecondPoint)
		|| !TestNotNull(TEXT("Third SpawnPoint"), ThirdPoint))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Unmatched SpawnPoint registers"), Combat->RegisterSpawnPoint(
		FirstPoint, RoomTag, FGameplayTagContainer(), FGameplayTag()));
	TestEqual(TEXT("An untagged point does not count toward the roster"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::WaitingForTrigger);
	FGameplayTagContainer MatchingTags;
	MatchingTags.AddTag(RoomTag);
	TestTrue(TEXT("First matching SpawnPoint registers"), Combat->RegisterSpawnPoint(
		SecondPoint, RoomTag, MatchingTags, FGameplayTag()));
	TestEqual(TEXT("One of two matching points cannot start combat"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::WaitingForTrigger);
	TestTrue(TEXT("Second matching SpawnPoint registers"), Combat->RegisterSpawnPoint(
		ThirdPoint, RoomTag, MatchingTags, FGameplayTag()));
	TestEqual(TEXT("Ready roster still waits for entrance save"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::WaitingForTrigger);
	if (FBoolProperty* CommittedProperty = FindFProperty<FBoolProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("bCheckpointCommitted")))
	{
		CommittedProperty->SetPropertyValue_InContainer(Checkpoint, true);
	}
	Checkpoint->OnCheckpointCommitted.Broadcast(Checkpoint);
	TestEqual(TEXT("Successful entrance save starts combat"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::Combat);
	TestEqual(TEXT("Door starts the sequence once"), CombatStartCount, 1);
	UOutlierSaveSubSystem* Save = GameInstance->GetSubsystem<UOutlierSaveSubSystem>();
	TestTrue(TEXT("Open door progress is recorded for restore"),
		Save->HasWorldProgress(EOutlierWorldProgressType::OpenedDoor, Door->DoorId));
	Door->OnDoorMotionFinished.Broadcast(Door, true);
	TestEqual(TEXT("Duplicate completion is ignored"), OpenedCount, 1);
	TestEqual(TEXT("Duplicate completion does not restart combat"), CombatStartCount, 1);
	const uint32 NextGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(NextGeneration);
	Door->OnDoorMotionFinished.Broadcast(Door, true);
	TestEqual(TEXT("Old door completion does not advance a new generation"), OpenedCount, 1);
	TestEqual(TEXT("Old door completion does not restart combat"), CombatStartCount, 1);
	TestTrue(TEXT("The persistent door still has its saved open state"), Door->IsDoorOpen());
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(Generation);
	TestEqual(TEXT("Old ready signal cannot restore the new generation"), CombatStartCount, 1);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(NextGeneration);
	TestTrue(TEXT("Restored open door does not replay its animation"), Door->IsDoorOpen());
	TestEqual(TEXT("Restore does not emit another opening completion"), OpenedCount, 1);
	TestEqual(TEXT("Restored door waits for its Room"), CombatStartCount, 1);
	TestTrue(TEXT("Reloaded Room registers"), Combat->RegisterRoom(CombatRoom, RoomTag));
	TestEqual(TEXT("Restored door waits for its SpawnPoints"), CombatStartCount, 1);
	TestTrue(TEXT("Reloaded first matching point registers"), Combat->RegisterSpawnPoint(
		SecondPoint, RoomTag, MatchingTags, FGameplayTag()));
	TestEqual(TEXT("One restored point is not enough"), CombatStartCount, 1);
	TestTrue(TEXT("Reloaded second matching point registers"), Combat->RegisterSpawnPoint(
		ThirdPoint, RoomTag, MatchingTags, FGameplayTag()));
	TestEqual(TEXT("Restored open door resumes combat once"), CombatStartCount, 2);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(NextGeneration);
	TestEqual(TEXT("Duplicate ready signal does not restart combat"), CombatStartCount, 2);

	TestTrue(TEXT("The encounter completion is recorded"),
		Save->RecordCompletedEncounter(RoomTag.GetTagName()));
	const uint32 ClearedGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(ClearedGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(ClearedGeneration);
	TestTrue(TEXT("Cleared encounter keeps the door open"), Door->IsDoorOpen());
	TestTrue(TEXT("Cleared Room registers"), Combat->RegisterRoom(CombatRoom, RoomTag));
	TestEqual(TEXT("Completed encounter does not respawn Wave 1"), CombatStartCount, 2);
	TestEqual(TEXT("Completed Room restores as cleared"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::Cleared);

	// 초기 스냅샷에는 이 문의 개방·전투 완료 기록이 없다. 페어를 밖으로 옮겨 초기 상태를 확인한다.
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Partner->SetActorLocation(FVector(1100.0f, 0.0f, 0.0f));
	Save->RestoreCurrentWorldProgress(FOutlierWorldProgressSnapshot());
	Save->ResetRuntimeCheckpointState();
	ShooterPS->SetAcquiredSuit(false);
	PartnerPS->SetAcquiredSuit(false);
	if (FBoolProperty* CommittedProperty = FindFProperty<FBoolProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("bCheckpointCommitted")))
	{
		CommittedProperty->SetPropertyValue_InContainer(Checkpoint, false);
	}
	const uint32 InitialGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(InitialGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(InitialGeneration);
	TestTrue(TEXT("Initial snapshot restores the open entrance"), Door->IsDoorOpen());
	TestEqual(TEXT("Initial snapshot does not start combat"), CombatStartCount, 2);
	TestTrue(TEXT("Initial Room registers"), Combat->RegisterRoom(CombatRoom, RoomTag));
	TestEqual(TEXT("Initial Room waits for its external trigger"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::WaitingForTrigger);

	CleanupWorld();
	return true;
}

#endif
