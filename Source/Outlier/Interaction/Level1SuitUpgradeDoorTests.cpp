#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/InteractableDoor.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
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
#include "TimerManager.h"
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
	// 이 테스트에는 서버 GameMode와 디스크 저장이 없으므로 저장 성공 경계를 직접 설정한다.
	const auto SupplyUpgradeSave = [Door]()
	{
		Door->bUpgradeSaveCommitted = true;
	};
	const auto AdvanceDelay = [World](float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(0.0f);
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	};
	TestFalse(TEXT("Level 1 door starts closed without a motion"), Door->IsDoorOpen());
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
	TestFalse(TEXT("Room tag alone keeps the default door closed"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("One player inside keeps the default door closed"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Partner->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	TestFalse(TEXT("Incomplete pair entry keeps the default door closed"), Door->IsDoorOpen());
	// 캡슐 오버랩은 원점이 Box 경계(500) 안으로 들어오기 전에 발생할 수 있다.
	Shooter->SetActorLocation(FVector(550.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("Capsule overlap alone does not complete entry"), Door->bEntrySealed);
	Shooter->SetActorLocation(FVector(0.0f, 0.0f, 450.0f));
	World->Tick(LEVELTICK_All, 0.11f);
	TestFalse(TEXT("Capsule crossing the Room ceiling does not complete entry"), Door->bEntrySealed);
	Shooter->SetActorLocation(FVector(480.0f, 0.0f, 0.0f));
	World->Tick(LEVELTICK_All, 0.11f);
	TestFalse(TEXT("Capsule still crossing the Room boundary does not complete entry"), Door->bEntrySealed);
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	// 단순 Automation 월드의 반복 Tick은 GFrameCounter를 올리지 않아 타이머를 재실행하지 않는다.
	// 위치가 완전히 들어온 뒤 입장 이벤트를 보내 동일한 서버 입장 판정을 확인한다.
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("Entry event starts closing after both capsules fully enter"), Door->IsDoorOpen());
	TestTrue(TEXT("Default closed door completes the close gate without animation"), Door->bCloseFinished);
	TestFalse(TEXT("Default closed door needs no closing Tick"), Door->IsActorTickEnabled());
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	TestTrue(TEXT("Leaving during close cancels the entry and reopens"), Door->IsDoorOpen());
	// 이후 기존 개방 시작 및 안전 재진입 회귀 검증은 명시적으로 열린 초기 상태를 사용한다.
	Door->bInitiallyOpen = true;
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("Both players reentering starts a fresh close"), Door->IsDoorOpen());
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);

	const uint32 Generation = World->GetSubsystem<UOutlierArenaSubsystem>()->GetGameplayGeneration();
	SupplyUpgradeSave();
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
		TestTrue(TEXT("Entrance checkpoint is available independently of opening"),
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
	TestTrue(TEXT("Ready roster blocks exits immediately after the door opens"), Combat->IsExitBlocked(RoomTag));
	TestEqual(TEXT("Ready roster does not spawn immediately"), Combat->GetAliveEnemyCount(RoomTag), 0);
	if (FBoolProperty* CommittedProperty = FindFProperty<FBoolProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("bCheckpointCommitted")))
	{
		CommittedProperty->SetPropertyValue_InContainer(Checkpoint, true);
	}
	Checkpoint->OnCheckpointCommitted.Broadcast(Checkpoint);
	TestEqual(TEXT("Arrival save does not replay sequence start"), CombatStartCount, 1);
	AdvanceDelay(2.99f);
	TestEqual(TEXT("Enemy spawning waits for the full three seconds"), Combat->GetAliveEnemyCount(RoomTag), 0);
	AdvanceDelay(0.02f);
	TestEqual(TEXT("Successful upgrade save reserves combat before delayed spawning"),
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
	TestTrue(TEXT("Restored sequence blocks exits before delayed spawning"), Combat->IsExitBlocked(RoomTag));
	AdvanceDelay(3.01f);
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

	FOutlierCheckpointSnapshot BeforeEntrance;
	BeforeEntrance.CheckpointId = TEXT("Test.BeforeLevel1Entrance");
	BeforeEntrance.SuitSnapshot.bAcquired = true;
	TestTrue(TEXT("A pre-entrance checkpoint can be selected"),
		Save->CommitCheckpointSnapshotForTesting(BeforeEntrance));
	ShooterPS->SetAcquiredSuit(true);
	PartnerPS->SetAcquiredSuit(true);
	const uint32 SuitGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(SuitGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(SuitGeneration);
	TestTrue(TEXT("Suit ownership alone does not seal the restored entrance"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Partner->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	TestFalse(TEXT("Both suited players entering after a pre-entrance checkpoint close the door"),
		Door->IsDoorOpen());

	// 안전 거절은 봉쇄 성공이나 예약 요청이 아니다. 이전 진행을 초기화한 뒤
	// 플레이어가 감지 범위에 있는 상태로 입장 요청을 만들고, 이탈 후에도 재시도가 없는지 본다.
	APlayerController* SafetyController = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Safety fixture controller exists"), SafetyController))
	{
		CleanupWorld();
		return false;
	}
	Shooter->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SafetyController->Possess(Shooter);
	Door->SafetyRegionLeft->SetRelativeLocation(Shooter->GetActorLocation());
	Door->SafetyRegionLeft->SetBoxExtent(FVector(40.0f));
	FPhysScene* SafetyPhysicsScene = World->GetPhysicsScene();
	if (!TestNotNull(TEXT("Entry safety fixture has a physics scene"), SafetyPhysicsScene))
	{
		CleanupWorld();
		return false;
	}
	// 프레임 없는 테스트 World에서는 물리 등록을 직접 반영한다. 실제 보호 대상이
	// 감지되는 전제부터 확인한 뒤, 닫기 거절과 진행 대기를 검증한다.
	SafetyPhysicsScene->ProcessDeferredCreatePhysicsState();
	SafetyPhysicsScene->Flush();
	if (!TestTrue(TEXT("Entry safety fixture is player controlled"),
		Shooter->GetController() == SafetyController && SafetyController->GetPawn() == Shooter)
		|| !TestTrue(TEXT("Entry safety fixture is physically detected"), Door->HasBlockingPlayer()))
	{
		CleanupWorld();
		return false;
	}
	Save->RestoreCurrentWorldProgress(FOutlierWorldProgressSnapshot());
	Save->ResetRuntimeCheckpointState();
	const uint32 SafetyGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(SafetyGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(SafetyGeneration);
	TestTrue(TEXT("Safety rejection retains the open entrance"), Door->IsDoorOpen());
	TestFalse(TEXT("Safety rejection does not record door progression"),
		Save->HasWorldProgress(EOutlierWorldProgressType::OpenedDoor, Door->DoorId));
	const int32 OpenedBeforeSafety = OpenedCount;
	const int32 CombatBeforeSafety = CombatStartCount;
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	TestFalse(TEXT("Player has cleared the safety region"), Door->HasBlockingPlayer());
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	ShooterPS->OnPlayerCharactersChanged.Broadcast(ShooterPS);
	TestTrue(TEXT("Repeated entry/state events do not retry a rejected close"), Door->IsDoorOpen());
	TestFalse(TEXT("Rejected entry does not begin door motion"), Door->IsActorTickEnabled());
	TestEqual(TEXT("Rejected entry does not emit an opening completion"), OpenedCount, OpenedBeforeSafety);
	TestEqual(TEXT("Rejected entry does not begin combat"), CombatStartCount, CombatBeforeSafety);

	// 몸체가 아직 방 안이면 End/Begin 중복 신호만으로 새 요청을 만들 수 없다.
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("False exit inside the room cannot rearm a rejected close"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(520.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("Capsule still crossing the boundary is not a full exit"), Door->IsDoorOpen());

	// 실제 퇴장 후에도 그 Pawn의 재입장이 필요하다. 다른 페어 멤버의 중복 입장이나
	// 상태 신호는 퇴장 기록을 새 닫기 요청으로 소비하지 않는다.
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	ShooterPS->OnPlayerCharactersChanged.Broadcast(ShooterPS);
	TestTrue(TEXT("Departure alone or another Pawn's duplicate entry never retries"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("A new occupied reentry request is rejected again"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	ShooterPS->OnPlayerCharactersChanged.Broadcast(ShooterPS);
	TestTrue(TEXT("Clearance after rejected reentry does not reuse the consumed request"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("A fresh clear exit/reentry starts closing in the same generation"), Door->IsDoorOpen());
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestFalse(TEXT("Accepted reentry closes and still waits for both UIs"), Door->IsDoorOpen());
	TestFalse(TEXT("Completed reentry close stops motion while waiting for UI"), Door->IsActorTickEnabled());
	TestEqual(TEXT("Reentry closing alone does not complete Level1 opening"), OpenedCount, OpenedBeforeSafety);
	TestEqual(TEXT("Reentry closing alone does not start combat"), CombatStartCount, CombatBeforeSafety);

	// 이번에는 비어 있는 범위에서 봉쇄를 시작한 뒤 몸체만 진입시킨다.
	// 닫힘 끝까지 갈 수 있는 Tick도 안전 검사에서 먼저 중단되어야 한다.
	const uint32 InterruptedGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(InterruptedGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(InterruptedGeneration);
	TestFalse(TEXT("Clear entry starts closing before interruption"), Door->IsDoorOpen());
	static_cast<AActor*>(Door)->Tick(0.4f);
	const FVector BeforeSafetyReversal = Door->DoorMeshLeft->GetRelativeLocation();
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	if (!TestTrue(TEXT("Mid-close entry is physically detected"), Door->HasBlockingPlayer()))
	{
		CleanupWorld();
		return false;
	}
	static_cast<AActor*>(Door)->Tick(0.7f);
	TestTrue(TEXT("Interrupted entry reverses toward open"), Door->IsDoorOpen());
	TestTrue(TEXT("Interrupted entry does not move farther closed"),
		Door->DoorMeshLeft->GetRelativeLocation().Equals(BeforeSafetyReversal));
	TestFalse(TEXT("Safety opening does not record successful Level1 progression"),
		Save->HasWorldProgress(EOutlierWorldProgressType::OpenedDoor, Door->DoorId));
	// 안전 개방 중 퇴장/재입장은 예약하지 않는다. 문이 다 열린 뒤 다른 이벤트가
	// 와도 닫히지 않으며, 새로운 실제 퇴장부터 다시 시작해야 한다.
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("Early reentry does not interrupt safety opening"), Door->IsDoorOpen());
	ShooterPS->ReportStatAllocatorUIOpened(InterruptedGeneration);
	ShooterPS->ReportStatAllocatorUIClosed(InterruptedGeneration);
	PartnerPS->ReportStatAllocatorUIOpened(InterruptedGeneration);
	PartnerPS->ReportStatAllocatorUIClosed(InterruptedGeneration);
	static_cast<AActor*>(Door)->Tick(0.5f);
	TestFalse(TEXT("Safety opening finishes without continued Tick"), Door->IsActorTickEnabled());
	TestEqual(TEXT("Safety opening and completed UI do not complete Level1 entry"), OpenedCount, OpenedBeforeSafety);
	TestEqual(TEXT("Safety opening does not begin combat"), CombatStartCount, CombatBeforeSafety);
	if (FBoolProperty* ActiveProperty = FindFProperty<FBoolProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("bActivationConditionSatisfied")))
	{
		TestTrue(TEXT("Safety opening keeps the respawn checkpoint available"),
			ActiveProperty->GetPropertyValue_InContainer(Checkpoint));
	}
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	ShooterPS->OnPlayerCharactersChanged.Broadcast(ShooterPS);
	Door->OnDoorMotionFinished.Broadcast(Door, true);
	TestTrue(TEXT("Interrupted entry waits instead of automatically retrying"), Door->IsDoorOpen());
	TestFalse(TEXT("Repeated events leave interrupted entry motion stopped"), Door->IsActorTickEnabled());
	TestEqual(TEXT("Stale normal completion cannot complete interrupted entry"), OpenedCount, OpenedBeforeSafety);

	// 이전 Generation에서 밖으로 나간 기록은 다음 Generation의 요청으로 넘어가지 않는다.
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	const uint32 ResetReentryGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(ResetReentryGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(ResetReentryGeneration);
	TestTrue(TEXT("New generation creates its own occupied rejection"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestTrue(TEXT("Previous generation's departure cannot authorize a new close"), Door->IsDoorOpen());

	// 두 명의 퇴장은 독립적이다. 한 명이 안전 개방 중 돌아와도 다른 명의
	// 퇴장 기록은 남아, 완전히 열린 뒤 돌아오는 플레이어가 새 요청을 만들 수 있다.
	const uint32 PairReentryGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(PairReentryGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(PairReentryGeneration);
	TestFalse(TEXT("Pair reentry fixture begins a clear close"), Door->IsDoorOpen());
	static_cast<AActor*>(Door)->Tick(0.4f);
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	static_cast<AActor*>(Door)->Tick(0.7f);
	TestTrue(TEXT("Pair reentry fixture starts safety opening"), Door->IsDoorOpen());
	Shooter->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	Partner->SetActorLocation(FVector(1100.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, false);
	Partner->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	TestTrue(TEXT("Partner's early return leaves safety opening active"), Door->IsDoorOpen());
	static_cast<AActor*>(Door)->Tick(0.5f);
	TestFalse(TEXT("Pair reentry waits until safety opening finishes"), Door->IsActorTickEnabled());
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	TestFalse(TEXT("Shooter's later return retains its independent close eligibility"), Door->IsDoorOpen());
	TestTrue(TEXT("Independent pair reentry starts a new guarded close"), Door->IsActorTickEnabled());
	TestEqual(TEXT("Pair reentry alone does not complete normal opening"), OpenedCount, OpenedBeforeSafety);
	TestEqual(TEXT("Pair reentry alone does not start combat"), CombatStartCount, CombatBeforeSafety);

	// Room 이벤트가 먼저 실행되는 경우에도 즉시 Snap이 안전 반전을 덮어쓰지 않는다.
	const uint32 OverlapGeneration = World->GetSubsystem<UOutlierArenaSubsystem>()->ReserveGameplayGeneration();
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(OverlapGeneration);
	World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReady.Broadcast(OverlapGeneration);
	TestFalse(TEXT("Next test generation begins a clear close"), Door->IsDoorOpen());
	static_cast<AActor*>(Door)->Tick(0.4f);
	const FVector BeforeOverlapReversal = Door->DoorMeshLeft->GetRelativeLocation();
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, false);
	TestTrue(TEXT("Room event prioritizes safety opening before Door Tick"), Door->IsDoorOpen());
	TestTrue(TEXT("Room event reverses at the current leaf position"),
		Door->DoorMeshLeft->GetRelativeLocation().Equals(BeforeOverlapReversal));
	static_cast<AActor*>(Door)->Tick(0.5f);
	TestEqual(TEXT("Room-event safety opening does not emit Level1 completion"), OpenedCount, OpenedBeforeSafety);

	// 일반 문에 기존 개방 기록이 있었다면 취소가 그 기록을 지워서도 안 된다.
	AInteractableDoor* SavedDoor = World->SpawnActor<AInteractableDoor>();
	if (!TestNotNull(TEXT("Previously saved door exists"), SavedDoor))
	{
		CleanupWorld();
		return false;
	}
	SavedDoor->DoorId = TEXT("Test.Safety.PreviouslyOpen");
	SavedDoor->DoorCurve = Curve;
	SavedDoor->bInitiallyOpen = true;
	BeginActor(SavedDoor);
	SavedDoor->SafetyRegionLeft->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
	SavedDoor->SafetyRegionLeft->SetBoxExtent(FVector(40.0f));
	Save->SetWorldProgressState(EOutlierWorldProgressType::OpenedDoor, SavedDoor->DoorId, true);
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	TestTrue(TEXT("Previously saved door accepts a clear close"), SavedDoor->TrySetDoorOpen(false));
	static_cast<AActor*>(SavedDoor)->Tick(0.4f);
	Shooter->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	static_cast<AActor*>(SavedDoor)->Tick(0.7f);
	TestTrue(TEXT("Cancellation restores the door's earlier open progress"),
		Save->HasWorldProgress(EOutlierWorldProgressType::OpenedDoor, SavedDoor->DoorId));
	static_cast<AActor*>(SavedDoor)->Tick(0.5f);

	// 현재 페어가 아닌 Pawn의 실제 출입은 재입장 권한을 만들지 않는다.
	Shooter->SetActorLocation(FVector(350.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Shooter, true);
	APartnerCharacter* UnpairedPartner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(), FTransform(FVector(1800.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("Unpaired reentry fixture exists"), UnpairedPartner))
	{
		CleanupWorld();
		return false;
	}
	Room->OnRoomActorOverlapChanged.Broadcast(UnpairedPartner, false);
	UnpairedPartner->SetActorLocation(FVector(300.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->ProcessDeferredCreatePhysicsState();
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(UnpairedPartner, true);
	TestTrue(TEXT("An unpaired Pawn cannot rearm safety-blocked entry"), Door->IsDoorOpen());

	// Partner만 나갔다가 재입장해도 새 사이클이다. BeginOverlap 시 캡슐이 문턱에
	// 걸쳐 있으면 닫지 않고 기존 입장 평가로 완전히 들어온 시점을 확인한다.
	Partner->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, false);
	Partner->SetActorLocation(FVector(480.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	TestTrue(TEXT("Reentry waits until both capsules fully fit inside the room"), Door->IsDoorOpen());
	Partner->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	SafetyPhysicsScene->Flush();
	PartnerPS->OnPlayerCharactersChanged.Broadcast(PartnerPS);
	TestFalse(TEXT("Partner's fresh reentry restores the normal close sequence"), Door->IsDoorOpen());
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestFalse(TEXT("Fresh reentry close waits for UI completion"), Door->IsDoorOpen());
	ShooterPS->ReportStatAllocatorUIOpened(OverlapGeneration);
	ShooterPS->ReportStatAllocatorUIClosed(OverlapGeneration);
	TestFalse(TEXT("One UI completion still cannot reopen after safety retry"), Door->IsDoorOpen());
	SupplyUpgradeSave();
	PartnerPS->ReportStatAllocatorUIOpened(OverlapGeneration);
	PartnerPS->ReportStatAllocatorUIClosed(OverlapGeneration);
	TestTrue(TEXT("Both UIs request normal opening after a successful reentry close"), Door->IsDoorOpen());
	TestEqual(TEXT("Normal opening request is not Level1 completion"), OpenedCount, OpenedBeforeSafety);
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestEqual(TEXT("Successful retry completes Level1 opening once"), OpenedCount, OpenedBeforeSafety + 1);
	TestTrue(TEXT("Successful retry records normal open progress"),
		Save->HasWorldProgress(EOutlierWorldProgressType::OpenedDoor, Door->DoorId));
	if (FBoolProperty* ActiveProperty = FindFProperty<FBoolProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("bActivationConditionSatisfied")))
	{
		TestTrue(TEXT("Entrance checkpoint remains active after normal opening"),
			ActiveProperty->GetPropertyValue_InContainer(Checkpoint));
	}
	TestEqual(TEXT("Successful opening still waits for checkpoint confirmation"), CombatStartCount, CombatBeforeSafety);
	TestTrue(TEXT("Retry combat room registers"), Combat->RegisterRoom(CombatRoom, RoomTag));
	TestTrue(TEXT("Retry first matching spawn point registers"), Combat->RegisterSpawnPoint(
		SecondPoint, RoomTag, MatchingTags, FGameplayTag()));
	TestTrue(TEXT("Retry second matching spawn point registers"), Combat->RegisterSpawnPoint(
		ThirdPoint, RoomTag, MatchingTags, FGameplayTag()));
	TestEqual(TEXT("Ready retry roster starts the saved sequence immediately"), CombatStartCount, CombatBeforeSafety + 1);
	if (FBoolProperty* CommittedProperty = FindFProperty<FBoolProperty>(
		AOutlierCheckpoint::StaticClass(), TEXT("bCheckpointCommitted")))
	{
		CommittedProperty->SetPropertyValue_InContainer(Checkpoint, true);
	}
	Checkpoint->OnCheckpointCommitted.Broadcast(Checkpoint);
	TestEqual(TEXT("Checkpoint confirmation cannot replay the retry sequence"), CombatStartCount, CombatBeforeSafety + 1);
	AdvanceDelay(3.01f);
	TestEqual(TEXT("Upgrade delay starts combat once after retry"), CombatStartCount, CombatBeforeSafety + 1);
	Checkpoint->OnCheckpointCommitted.Broadcast(Checkpoint);
	Room->OnRoomActorOverlapChanged.Broadcast(Partner, true);
	Door->OnDoorMotionFinished.Broadcast(Door, true);
	TestEqual(TEXT("Duplicate retry completions do not replay Level1 opening"), OpenedCount, OpenedBeforeSafety + 1);
	TestEqual(TEXT("Duplicate retry signals do not restart combat"), CombatStartCount, CombatBeforeSafety + 1);

	CleanupWorld();
	return true;
}

#endif
