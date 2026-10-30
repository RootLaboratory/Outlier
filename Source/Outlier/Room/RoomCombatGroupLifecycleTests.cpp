#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NativeGameplayTags.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "UObject/UnrealType.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(LifecycleA, "Room.GroupLifecycle01.1.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(LifecycleB, "Room.GroupLifecycle01.1.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(LifecycleC, "Room.GroupLifecycle01.1.3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(LifecycleOutside, "Room.GroupLifecycle01.2.1");

FRoomCombatPhaseDefinition LifecyclePhase(ERoomCombatPhaseStartPolicy Policy = ERoomCombatPhaseStartPolicy::InitialDetection)
{
	FRoomCombatPhaseDefinition Phase;
	Phase.StartPolicy = Policy;
	Phase.DelayBeforeStartSeconds = 0;
	FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
	if (Policy != ERoomCombatPhaseStartPolicy::InitialDetection)
	{
		Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		FRoomCombatEnemyEntry& Enemy = Wave.Enemies.AddDefaulted_GetRef();
		Enemy.EnemyClass = AEnemyBase::StaticClass();
		Enemy.Count = 1;
	}
	return Phase;
}

struct FGroupLifecycleWorld
{
	UWorld* World;
	UGameInstance* GameInstance;
	URoomCombatSubsystem* Combat;
	UOutlierSaveSubSystem* Saves;
	URoomCombatDefinition* Definition;
	AActor* Requester;
	TMap<FGameplayTag, ARoomVolume*> Rooms;

	FGroupLifecycleWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("GroupLifecycleTest")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		Combat = World->GetSubsystem<URoomCombatSubsystem>();
		Saves = GameInstance->GetSubsystem<UOutlierSaveSubSystem>();
		Definition = NewObject<URoomCombatDefinition>(World);
		Combat->SetCombatDefinitionForTesting(Definition);
		Requester = World->SpawnActor<AActor>();
	}

	~FGroupLifecycleWorld()
	{
		Combat->CombatEventObserverForTesting = nullptr;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	ARoomVolume* AddRoom(FGameplayTag Tag, const TArray<FRoomCombatPhaseDefinition>& Phases = {LifecyclePhase()})
	{
		FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = Tag;
		Entry.CombatPhases = Phases;
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
		Room->SetActorLocation(FVector(Rooms.Num() * 3000.0f, 0, 250));
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(500));
		Rooms.Add(Tag, Room);
		Combat->RegisterRoom(Room, Tag);
		return Room;
	}

	AEnemyBase* AddEnemy(FGameplayTag Tag)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(-10000, 0, 0)), Params);
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
		Combat->RegisterPreplacedEnemy(Enemy);
		return Enemy;
	}

	bool Start(FGameplayTag Tag)
	{
		FRoomCombatPreparationContext Context;
		return Combat->BeginInitialDetectionPreparation(Tag, Requester, Context)
			&& Combat->CompleteInitialDetectionPreparation(Context);
	}

	void Reload(const TMap<FGameplayTag, FOutlierRoomPhaseProgress>& Progress)
	{
		Combat->ResetRuntimeCombatState();
		Saves->RestoreCurrentRoomPhaseProgress(Progress);
		for (const auto& Room : Rooms) Combat->RegisterRoom(Room.Value, Room.Key);
	}

	void Place(APartnerCharacter* Player, FGameplayTag Tag)
	{
		ARoomVolume* Room = Rooms.FindChecked(Tag);
		Player->SetActorLocation(Room->GetActorLocation());
		Player->GetRoomTagComp()->ClearRuntimeRoomAssignment();
		Player->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
		Player->GetCapsuleComponent()->UpdateOverlaps();
		Room->RefreshOverlappingRoomAssignments();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupLifecycleCheckpointTest, "Outlier.Room.GroupLifecycle.CaptureParticipants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupLifecycleCheckpointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupLifecycleWorld Test;
	Test.AddRoom(LifecycleA);
	Test.AddRoom(LifecycleB);
	Test.AddRoom(LifecycleC, {LifecyclePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	Test.AddEnemy(LifecycleA);
	Test.AddEnemy(LifecycleB);
	TestTrue(TEXT("A starts its first phase"), Test.Start(LifecycleA));
	TestTrue(TEXT("B starts independently in the same group"), Test.Start(LifecycleB));
	TMap<FGameplayTag, FOutlierRoomPhaseProgress> Progress;
	Test.Combat->CaptureRoomPhaseProgressForCheckpoint(Progress);
	TestEqual(TEXT("Both actual participants are captured"), Progress.Num(), 2);
	for (FGameplayTag Tag : {LifecycleA.GetTag(), LifecycleB.GetTag()})
	{
		const FOutlierRoomPhaseProgress* Saved = Progress.Find(Tag);
		if (!TestNotNull(TEXT("Child room retains its own checkpoint key"), Saved)) return false;
		TestEqual(TEXT("First phase is preserved rather than omitted"), Saved->NextPhaseIndex, 0);
		TestTrue(TEXT("In-progress phase resumes without a second trigger"), Saved->bResumeCurrentPhase);
		TestTrue(TEXT("Participant retains its own exit-block cause"), Saved->bExitBlockActive);
	}
	TestFalse(TEXT("Event-only nonparticipant is not added to checkpoint"), Progress.Contains(LifecycleC));
	TestTrue(TEXT("Capture does not mutate the live boundary-progress store"), Test.Saves->GetCurrentRoomPhaseProgress().IsEmpty());

	Test.Reload(Progress);
	TestTrue(TEXT("Restored A owns a reservation before pair readiness"), Test.Combat->IsRoomCombatReserved(LifecycleA));
	TestTrue(TEXT("Restored B owns an independent reservation"), Test.Combat->IsRoomCombatReserved(LifecycleB));
	TestFalse(TEXT("Restoration does not recruit C"), Test.Combat->IsRoomCombatReserved(LifecycleC));
	TestTrue(TEXT("Restored participants block their group exterior"), Test.Combat->IsGroupExitBlocked(LifecycleC));
	TestEqual(TEXT("C keeps its external trigger condition"), Test.Combat->GetRoomState(LifecycleC), ERoomCombatState::WaitingForTrigger);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupLifecycleBoundaryTest, "Outlier.Room.GroupLifecycle.IndependentBoundaryRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupLifecycleBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupLifecycleWorld Test;
	FRoomCombatPhaseDefinition External = LifecyclePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger);
	External.DelayBeforeStartSeconds = 0.5f;
	FRoomCombatPhaseDefinition Automatic = LifecyclePhase(ERoomCombatPhaseStartPolicy::Automatic);
	Automatic.DelayBeforeStartSeconds = 0.8f;
	Test.AddRoom(LifecycleA, {LifecyclePhase(), External});
	Test.AddRoom(LifecycleB, {LifecyclePhase(), Automatic});
	Test.AddRoom(LifecycleC, {External});
	AEnemyBase* A = Test.AddEnemy(LifecycleA);
	AEnemyBase* B = Test.AddEnemy(LifecycleB);
	TestTrue(TEXT("A begins"), Test.Start(LifecycleA));
	TestTrue(TEXT("B begins"), Test.Start(LifecycleB));
	Test.Combat->NotifyEnemyDefeated(A);
	Test.Combat->NotifyEnemyDefeated(B);
	TMap<FGameplayTag, FOutlierRoomPhaseProgress> Progress;
	Test.Combat->CaptureRoomPhaseProgressForCheckpoint(Progress);
	for (FGameplayTag Tag : {LifecycleA.GetTag(), LifecycleB.GetTag()})
	{
		const FOutlierRoomPhaseProgress* Saved = Progress.Find(Tag);
		if (!TestNotNull(TEXT("Boundary checkpoint exists per participant"), Saved)) return false;
		TestEqual(TEXT("Completed phase is skipped independently"), Saved->NextPhaseIndex, 1);
		TestFalse(TEXT("Next-phase wait is not saved as an already started phase"), Saved->bResumeCurrentPhase);
	}
	// 등록 순서는 원래 전투 시작 순서와 다를 수 있다. 각 방의 저장 키로 독립 복원한다.
	Test.Reload(Progress);
	TestEqual(TEXT("A restores its next phase"), Test.Combat->GetCurrentCombatPhaseIndex(LifecycleA), 1);
	TestEqual(TEXT("B restores its next phase"), Test.Combat->GetCurrentCombatPhaseIndex(LifecycleB), 1);
	TestEqual(TEXT("External A still requires its own trigger"), Test.Combat->GetRoomState(LifecycleA), ERoomCombatState::WaitingForTrigger);
	TestTrue(TEXT("External A reapplies full phase delay"), Test.Combat->IsPhaseDelayPending(LifecycleA));
	TestTrue(TEXT("Automatic B waits for pair-ready restoration"), Test.Combat->IsRoomCombatReserved(LifecycleB));
	TestFalse(TEXT("C remains a nonparticipant"), Test.Combat->IsRoomCombatReserved(LifecycleC));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupLifecycleLifetimeTest, "Outlier.Room.GroupLifecycle.UnloadAndResetTokens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupLifecycleLifetimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupLifecycleWorld Test;
	ARoomVolume* RoomA = Test.AddRoom(LifecycleA, {LifecyclePhase(), LifecyclePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	ARoomVolume* RoomB = Test.AddRoom(LifecycleB);
	ARoomVolume* RoomC = Test.AddRoom(LifecycleC);
	AEnemyBase* A = Test.AddEnemy(LifecycleA);
	Test.AddEnemy(LifecycleB);
	Test.AddEnemy(LifecycleC);
	TestTrue(TEXT("A begins"), Test.Start(LifecycleA));
	TestTrue(TEXT("B begins"), Test.Start(LifecycleB));
	FRoomCombatReconnectContext Reconnect;
	TestTrue(TEXT("Actual group combat supplies reconnect context"), Test.Combat->GetReconnectContext(Reconnect));
	Test.Combat->NotifyEnemyDefeated(A);
	FRoomCombatTriggerContext OldTrigger;
	TestTrue(TEXT("A captures its next trigger"), Test.Combat->CreateTriggerContext(Test.Requester, LifecycleA, FGameplayTag(), OldTrigger));
	FRoomCombatPreparationContext OldPreparation;
	TestTrue(TEXT("C can prepare without becoming actual combat"), Test.Combat->BeginInitialDetectionPreparation(LifecycleC, Test.Requester, OldPreparation));
	Test.Combat->UnregisterRoom(RoomC);
	TestFalse(TEXT("Unloaded preparation cannot complete"), Test.Combat->CompleteInitialDetectionPreparation(OldPreparation));
	Test.Combat->UnregisterRoom(RoomA);
	TestTrue(TEXT("B remains reserved when representative A unloads"), Test.Combat->IsRoomCombatReserved(LifecycleB));
	TestTrue(TEXT("A room change does not expire the continuing group reconnect context"), Test.Combat->IsReconnectContextCurrent(Reconnect));
	TestFalse(TEXT("Old A trigger cannot start after unload"), Test.Combat->StartTriggeredSequence(Test.Requester, OldTrigger));
	Test.Combat->ResetRuntimeCombatState();
	TestFalse(TEXT("Reset clears all group reservations"), Test.Combat->HasActiveCombatReservation());
	TestFalse(TEXT("Reset clears group exterior blocking"), Test.Combat->IsGroupExitBlocked(LifecycleB));
	TestFalse(TEXT("Reset expires reconnect group lifetime"), Test.Combat->IsReconnectContextCurrent(Reconnect));
	Test.Combat->RegisterRoom(RoomB, LifecycleB);
	Test.AddEnemy(LifecycleB);
	TestTrue(TEXT("The same room tag can start a new group lifetime"), Test.Start(LifecycleB));
	TestFalse(TEXT("Old reconnect context cannot attach to the new lifetime"), Test.Combat->IsReconnectContextCurrent(Reconnect));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupLifecycleReconnectTest, "Outlier.Room.GroupLifecycle.ActualReconnectPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupLifecycleReconnectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupLifecycleWorld Test;
	Test.AddRoom(LifecycleA);
	Test.AddRoom(LifecycleB);
	Test.AddRoom(LifecycleC, {LifecyclePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	Test.AddRoom(LifecycleOutside);
	Test.AddEnemy(LifecycleA);
	TestTrue(TEXT("A begins group combat"), Test.Start(LifecycleA));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	if (!TestNotNull(TEXT("Configured Partner Blueprint exists"), PartnerClass)) return false;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* Player = Test.World->SpawnActor<APartnerCharacter>(PartnerClass, FTransform(FVector(-3000, 0, 250)), Params);
	APartnerCharacter* Anchor = Test.World->SpawnActor<APartnerCharacter>(PartnerClass, FTransform(FVector(-3000, 1000, 250)), Params);
	if (!TestNotNull(TEXT("Reconnecting pawn exists"), Player) || !TestNotNull(TEXT("Anchor pawn exists"), Anchor)) return false;
	// 실제 입장/순간이동의 오버랩을 받으려면 Room과 플레이어의 수명을 시작해야 한다.
	// 전투 집계만 필요한 기본 Enemy에는 에셋이 없으므로 전체 월드 BeginPlay는 호출하지 않는다.
	Test.World->SetBegunPlay(true);
	for (const auto& Room : Test.Rooms) Room.Value->DispatchBeginPlay();
	Player->DispatchBeginPlay();
	Anchor->DispatchBeginPlay();
	FRoomCombatReconnectContext Context;
	TestTrue(TEXT("Group reconnect context exists"), Test.Combat->GetReconnectContext(Context));
	Test.Place(Player, LifecycleB);
	const FVector Before = Player->GetActorLocation();
	TestTrue(TEXT("A safe pawn in a sibling room can reconnect"), Test.Combat->TryPlaceReconnectingPlayer(Player, nullptr, Context));
	TestTrue(TEXT("Sibling pawn is retained without teleport"), Player->GetActorLocation().Equals(Before));

	// 목적지는 대표 전투 방 A가 아니라 남은 플레이어가 실제로 있는 C다.
	// C는 이벤트 전용이므로 합류하더라도 전투 참여나 완료로 바꾸지 않는다.
	Test.Place(Player, LifecycleOutside);
	Test.Place(Anchor, LifecycleC);
	TestTrue(TEXT("Outside reconnecting pawn joins actual sibling anchor room"), Test.Combat->TryPlaceReconnectingPlayer(Player, Anchor, Context));
	TestTrue(TEXT("Reconnected pawn is physically inside C"), Test.Rooms[LifecycleC]->ContainsWorldLocation(Player->GetActorLocation()));
	TestEqual(TEXT("Reconnected pawn tag also becomes C"), Player->GetCurrentRoomTag(), LifecycleC.GetTag());
	TestEqual(TEXT("Reconnect context records actual destination C"), Context.DestinationRoomTag, LifecycleC.GetTag());
	TestTrue(TEXT("Actual destination has a registration token"), Context.DestinationRoomRegistrationId.IsValid());
	TestEqual(TEXT("Event-only anchor room still waits for trigger"), Test.Combat->GetRoomState(LifecycleC), ERoomCombatState::WaitingForTrigger);
	TestFalse(TEXT("Reconnect does not recruit the anchor room"), Test.Combat->IsRoomCombatReserved(LifecycleC));
	const FGuid PreviousGroupId = Context.CombatGroupId;
	const FGuid PreviousDestinationId = Context.DestinationRoomRegistrationId;
	Test.Combat->UnregisterRoom(Test.Rooms[LifecycleC]);
	TestTrue(TEXT("The same destination can register in a new lifetime"), Test.Combat->RegisterRoom(Test.Rooms[LifecycleC], LifecycleC));
	TestTrue(TEXT("Safe placement is revalidated after destination registration changes"), Test.Combat->TryPlaceReconnectingPlayer(Player, Anchor, Context));
	TestEqual(TEXT("Destination reload preserves the ongoing group lifetime"), Context.CombatGroupId, PreviousGroupId);
	TestTrue(TEXT("Destination reload refreshes the ACK registration token"), Context.DestinationRoomRegistrationId.IsValid()
		&& Context.DestinationRoomRegistrationId != PreviousDestinationId);
	TestEqual(TEXT("Reloaded destination still waits for its own event"), Test.Combat->GetRoomState(LifecycleC), ERoomCombatState::WaitingForTrigger);
	FRoomCombatReconnectContext StaleGeneration = Context;
	++StaleGeneration.GameplayGeneration;
	TestFalse(TEXT("Wrong-generation result cannot place a pawn"), Test.Combat->TryPlaceReconnectingPlayer(Player, Anchor, StaleGeneration));
	Test.Combat->UnregisterRoom(Test.Rooms[LifecycleA]);
	FRoomCombatReconnectContext Ended;
	TestFalse(TEXT("Last participant unload ends the active group"), Test.Combat->GetReconnectContext(Ended));
	TestTrue(TEXT("A valid actual destination survives group completion"), Test.Combat->IsReconnectDestinationCurrent(Context));
	Test.Combat->UnregisterRoom(Test.Rooms[LifecycleC]);
	TestFalse(TEXT("An unloaded destination invalidates ACK even without active combat"), Test.Combat->IsReconnectDestinationCurrent(Context));
	Test.Combat->RegisterRoom(Test.Rooms[LifecycleC], LifecycleC);
	TestFalse(TEXT("Same-tag destination reload cannot validate the previous ACK"), Test.Combat->IsReconnectDestinationCurrent(Context));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupLifecycleConflictingRestoreTest, "Outlier.Room.GroupLifecycle.RejectConflictingRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupLifecycleConflictingRestoreTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (bool bOutsideFirst : {false, true})
	{
		FGroupLifecycleWorld Test;
		Test.AddRoom(LifecycleA);
		Test.AddRoom(LifecycleOutside);
		Test.Combat->ResetRuntimeCombatState();
		FOutlierRoomPhaseProgress Saved;
		Saved.NextPhaseIndex = 0;
		Saved.bExitBlockActive = true;
		Saved.bResumeCurrentPhase = true;
		TMap<FGameplayTag, FOutlierRoomPhaseProgress> Progress;
		Progress.Add(LifecycleA, Saved);
		Progress.Add(LifecycleOutside, Saved);
		Test.Saves->RestoreCurrentRoomPhaseProgress(Progress);
		const FGameplayTag First = bOutsideFirst ? LifecycleOutside.GetTag() : LifecycleA.GetTag();
		const FGameplayTag Rejected = bOutsideFirst ? LifecycleA.GetTag() : LifecycleOutside.GetTag();
		TestTrue(TEXT("First restored group can reserve its room"), Test.Combat->RegisterRoom(Test.Rooms[First], First));
		AddExpectedError(TEXT("Conflicting restored combat group"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("A second active parent is rejected before registration"), Test.Combat->RegisterRoom(Test.Rooms[Rejected], Rejected));
		TestFalse(TEXT("Rejected room leaves no partial registration"), Test.Combat->IsRoomRegistered(Rejected));
		TestFalse(TEXT("Rejected room leaves no stray exterior-block cause"), Test.Combat->IsExitBlocked(Rejected));
		TestTrue(TEXT("Original reservation survives the rejection"), Test.Combat->IsRoomCombatReserved(First));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupLifecycleResumeTest, "Outlier.Room.GroupLifecycle.ActualResumeAndReentrantReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupLifecycleResumeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupLifecycleWorld Test;
	Test.AddRoom(LifecycleA);
	Test.AddRoom(LifecycleB, {LifecyclePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	Test.AddRoom(LifecycleC, {LifecyclePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	Test.AddEnemy(LifecycleA);
	TestTrue(TEXT("A begins a placed-enemy phase"), Test.Start(LifecycleA));
	FRoomCombatTriggerContext Trigger;
	TestTrue(TEXT("B captures its real external trigger"), Test.Combat->CreateTriggerContext(Test.Requester, LifecycleB, FGameplayTag(), Trigger));
	TestTrue(TEXT("B accepts the external event before checkpoint"), Test.Combat->StartTriggeredSequence(Test.Requester, Trigger));
	TMap<FGameplayTag, FOutlierRoomPhaseProgress> Progress;
	Test.Combat->CaptureRoomPhaseProgressForCheckpoint(Progress);
	Test.Reload(Progress);
	TestEqual(TEXT("A waits for gameplay readiness after reload"), Test.Combat->GetRoomState(LifecycleA), ERoomCombatState::Dormant);
	TestEqual(TEXT("B does not replay an external event during registration"), Test.Combat->GetRoomState(LifecycleB), ERoomCombatState::Dormant);

	// 기존 DirectPair PIE 준비 판정을 사용한다. 에셋/설정은 저장하지 않고
	// 임시 월드와 테스트 옵션은 조기 반환을 포함한 모든 종료 경로에서 되돌린다.
	AOutlierGameMode* DirectMode = Test.World->GetAuthGameMode<AOutlierGameMode>();
	if (!DirectMode) DirectMode = GetMutableDefault<AOutlierGameMode>();
	FBoolProperty* DirectOption = FindFProperty<FBoolProperty>(AOutlierGameMode::StaticClass(), TEXT("bDirectPairTest"));
	if (!TestNotNull(TEXT("Existing direct-pair option is reflected"), DirectOption)) return false;
	const bool bPreviousDirect = DirectOption->GetPropertyValue_InContainer(DirectMode);
	const EWorldType::Type PreviousType = Test.World->WorldType;
	AWorldSettings* Settings = Test.World->GetWorldSettings();
	const TSubclassOf<AGameModeBase> PreviousModeClass = Settings->DefaultGameMode;
	AGameStateBase* State = Test.World->GetGameState();
	if (!TestNotNull(TEXT("Initialized world has GameState"), State)) return false;
	const TSubclassOf<AGameModeBase> PreviousStateModeClass = State->GameModeClass;
	ON_SCOPE_EXIT
	{
		DirectOption->SetPropertyValue_InContainer(DirectMode, bPreviousDirect);
		Test.World->WorldType = PreviousType;
		Settings->DefaultGameMode = PreviousModeClass;
		State->GameModeClass = PreviousStateModeClass;
	};
	DirectOption->SetPropertyValue_InContainer(DirectMode, true);
	Test.World->WorldType = EWorldType::PIE;
	Settings->DefaultGameMode = AOutlierGameMode::StaticClass();
	State->GameModeClass = AOutlierGameMode::StaticClass();
	TestTrue(TEXT("Transient PIE fixture uses existing direct-pair readiness"), AOutlierGameMode::IsDirectPairTestWorld(Test.World));
	TestTrue(TEXT("Gameplay is genuinely ready through the Arena API"), Test.World->GetSubsystem<UOutlierArenaSubsystem>()->IsGameplayLevelsReady());
	ARoomCombatSpawnPoint* Point = Test.World->SpawnActor<ARoomCombatSpawnPoint>();
	Point->SetForceSpawnLocationFailureForTesting(true);
	Test.Combat->RegisterSpawnPoint(Point, LifecycleB, FGameplayTagContainer(), FGameplayTag());
	int32 StartedA = 0;
	int32 StartedB = 0;
	Test.Combat->CombatEventObserverForTesting = [&](FGameplayTag Tag, ERoomCombatEvent Event, int32)
	{
		if (Event != ERoomCombatEvent::SequenceStarted) return;
		if (Tag == LifecycleA) ++StartedA;
		if (Tag == LifecycleB) ++StartedB;
	};
	Test.Combat->ResumeRestoredAutomaticPhases();
	TestEqual(TEXT("Placed-enemy A waits for its late registration rather than clearing"), Test.Combat->GetRoomState(LifecycleA), ERoomCombatState::Dormant);
	TestEqual(TEXT("Accepted external B resumes without another trigger"), Test.Combat->GetRoomState(LifecycleB), ERoomCombatState::Combat);
	TestEqual(TEXT("B reconstructs only its first-wave request"), Test.Combat->GetPendingSpawnCount(LifecycleB), 1);
	Test.AddEnemy(LifecycleA);
	TestEqual(TEXT("Late placed enemy resumes A through registration"), Test.Combat->GetRoomState(LifecycleA), ERoomCombatState::Combat);
	Test.Combat->ResumeRestoredAutomaticPhases();
	Test.Combat->ResumeRestoredAutomaticPhases();
	TestEqual(TEXT("A resume is emitted once"), StartedA, 1);
	TestEqual(TEXT("B resume is emitted once"), StartedB, 1);
	TestEqual(TEXT("Repeated readiness does not duplicate B's pending roster"), Test.Combat->GetPendingSpawnCount(LifecycleB), 1);
	TestFalse(TEXT("C never acquires a reservation during resume"), Test.Combat->IsRoomCombatReserved(LifecycleC));

	// 시작 통보가 Reset을 요청해도 복사된 다른 방 목록이나 지연 소환이
	// 이전 수명을 다시 살리지 않아야 한다.
	Test.Reload(Progress);
	Test.Combat->RegisterSpawnPoint(Point, LifecycleB, FGameplayTagContainer(), FGameplayTag());
	FRoomCombatReconnectContext BeforeReset;
	TestTrue(TEXT("Pending restored group has a reconnect lifetime"), Test.Combat->GetReconnectContext(BeforeReset));
	bool bResetDuringResume = false;
	Test.Combat->CombatEventObserverForTesting = [&](FGameplayTag, ERoomCombatEvent Event, int32)
	{
		if (Event == ERoomCombatEvent::SequenceStarted)
		{
			bResetDuringResume = true;
			Test.Combat->ResetRuntimeCombatState();
		}
	};
	Test.Combat->ResumeRestoredAutomaticPhases();
	TestTrue(TEXT("Resume actually reached the reentrant Reset boundary"), bResetDuringResume);
	TestFalse(TEXT("Reentrant Reset clears every group reservation"), Test.Combat->HasActiveCombatReservation());
	TestEqual(TEXT("Reentrant Reset discards restored pending spawn work"), Test.Combat->GetPendingSpawnCount(LifecycleB), 0);
	TestFalse(TEXT("Reentrant Reset invalidates the old reconnect lifetime"), Test.Combat->IsReconnectContextCurrent(BeforeReset));
	return true;
}

#endif
