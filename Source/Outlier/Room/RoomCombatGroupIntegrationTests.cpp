#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "OutlierArenaSettings.h"
#include "OutlierPlayerState.h"
#include "Room/RoomCombatBarrier.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(IntegrationA, "Room.GroupIntegration01.1.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(IntegrationB, "Room.GroupIntegration01.1.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(IntegrationEvent, "Room.GroupIntegration01.1.3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(IntegrationOutside, "Room.GroupIntegration01.2.1");

struct FGroupIntegrationWorld
{
	UWorld* World;
	UGameInstance* GameInstance;
	URoomCombatSubsystem* Combat;
	AShooterCharacter* Shooter;
	APartnerCharacter* Partner;
	TMap<FGameplayTag, ARoomVolume*> Rooms;
	TMap<FGameplayTag, AEnemyBase*> Enemies;
	TSoftObjectPtr<UWorld> SavedArena;
	int32 TeleportCalls = 0;

	FGroupIntegrationWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RoomGroupIntegrationTest")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		SavedArena = GetDefault<UOutlierArenaSettings>()->ArenaLevel;
		GetMutableDefault<UOutlierArenaSettings>()->ArenaLevel = World;
		Combat = World->GetSubsystem<URoomCombatSubsystem>();
		Combat->JoinTeleportObserverForTesting = [this] { ++TeleportCalls; };
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		Combat->SetCombatDefinitionForTesting(Definition);
		int32 RoomIndex = 0;
		for (FGameplayTag Tag : {IntegrationA.GetTag(), IntegrationB.GetTag(),
			IntegrationEvent.GetTag(), IntegrationOutside.GetTag()})
		{
			auto& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
			Entry.RoomTag = Tag;
			auto& Phase = Entry.CombatPhases.AddDefaulted_GetRef();
			Phase.StartPolicy = Tag == IntegrationEvent
				? ERoomCombatPhaseStartPolicy::ExternalTrigger : ERoomCombatPhaseStartPolicy::InitialDetection;
			Phase.Waves.AddDefaulted();
			if (Tag == IntegrationEvent)
			{
				// 실행 가능한 이벤트 차수여야 감지 우회 차단을 검증할 수 있다.
				Phase.Waves[0].SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
				auto& EnemyEntry = Phase.Waves[0].Enemies.AddDefaulted_GetRef();
				EnemyEntry.EnemyClass = AEnemyBase::StaticClass();
			}
			ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
			*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
				->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
			Room->SetActorLocation(FVector(RoomIndex++ * 3000, 0, 250));
			CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(500));
			Rooms.Add(Tag, Room);
			Combat->RegisterRoom(Room, Tag);
			AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>();
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
			Enemy->SetActorLocation(Room->GetActorLocation());
			// 실제 BeginPlay로 배치 적을 등록한다. 테스트 전용 기본 스탯만 메모리에 구성한다.
			UDataTable* Stats = NewObject<UDataTable>(Enemy);
			Stats->RowStruct = FEnemyStat::StaticStruct();
			FEnemyStat Stat;
			Stat.Health = 100;
			Stats->AddRow(TEXT("Integration"), Stat);
			auto* Row = FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("EnemyStatRow"))
				->ContainerPtrToValuePtr<FDataTableRowHandle>(Enemy);
			Row->DataTable = Stats;
			Row->RowName = TEXT("Integration");
			Enemies.Add(Tag, Enemy);
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
			TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
		check(ShooterClass && PartnerClass);
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FTransform(FVector(-3000, 0, 250)), Params);
		Partner = World->SpawnActor<APartnerCharacter>(PartnerClass, FTransform(FVector(-3000, 1000, 250)), Params);
		AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
		State->SetShooterCharacter(Shooter);
		State->SetPartnerCharacter(Partner);
		if (!World->GetGameState()) World->SetGameState(World->SpawnActor<AGameStateBase>());
		World->GetGameState()->PlayerArray.AddUnique(State);
		World->SetBegunPlay(true);
		for (const auto& Room : Rooms) Room.Value->DispatchBeginPlay();
		for (const auto& Enemy : Enemies) Enemy.Value->DispatchBeginPlay();
		Shooter->DispatchBeginPlay();
		Partner->DispatchBeginPlay();
		Place(Shooter, IntegrationA, FVector(150, 0, 0));
		Place(Partner, IntegrationB, FVector(150, 0, 0));
	}

	~FGroupIntegrationWorld()
	{
		Combat->JoinTeleportObserverForTesting = nullptr;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		GetMutableDefault<UOutlierArenaSettings>()->ArenaLevel = SavedArena;
	}

	void Place(ACharacter* Actor, FGameplayTag Tag, FVector Offset = FVector::ZeroVector)
	{
		Actor->SetActorLocation(Rooms.FindChecked(Tag)->GetActorLocation() + Offset);
		Actor->GetCapsuleComponent()->UpdateOverlaps();
		Rooms.FindChecked(Tag)->RefreshOverlappingRoomAssignments();
	}

	ARoomCombatBarrier* AddBarrier(FGameplayTag Tag, FGameplayTag Connected = FGameplayTag())
	{
		// 차단막이 전투 중 플레이어/적과 겹쳐 경로 검증 자체를 방해하지 않도록 멀리 둔다.
		const FTransform Transform(FVector(20000, 0, 250));
		ARoomCombatBarrier* Barrier = World->SpawnActorDeferred<ARoomCombatBarrier>(ARoomCombatBarrier::StaticClass(), Transform);
		*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Barrier) = Tag;
		*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("ConnectedRoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Barrier) = Connected;
		Barrier->FinishSpawning(Transform);
		return Barrier;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomCombatGroupPursuitIntegrationTest,
	"Outlier.Room.GroupIntegration.PursuitOwnershipAndBarrierCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatGroupPursuitIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupIntegrationWorld Test;
	AEnemyBase* Pursuer = Test.Enemies[IntegrationA];
	ARoomCombatBarrier* Exterior = Test.AddBarrier(IntegrationEvent);
	ARoomCombatBarrier* Internal = Test.AddBarrier(IntegrationA, IntegrationB);
	const FVector ShooterLocation = Test.Shooter->GetActorLocation();
	const FVector PartnerLocation = Test.Partner->GetActorLocation();
	Pursuer->EnterAlertFromPerception(PartnerLocation);
	if (!TestTrue(TEXT("Sibling detection commits the source room"), Pursuer->CommitAlertToCombat())) return false;
	TestEqual(TEXT("Shooter already in the group is not teleported"), Test.Shooter->GetActorLocation(), ShooterLocation);
	TestEqual(TEXT("Partner in a different child room is not teleported"), Test.Partner->GetActorLocation(), PartnerLocation);
	TestEqual(TEXT("Players already in the group never invoke join teleport"), Test.TeleportCalls, 0);
	TestEqual(TEXT("Unvisited sibling remains dormant"), Test.Combat->GetRoomState(IntegrationB), ERoomCombatState::Dormant);
	TestTrue(TEXT("An event-only sibling exterior blocks for the participating group"), Exterior->IsBlocked());
	TestFalse(TEXT("Internal passage remains without a battle barrier"), Internal->IsBlocked());

	// 같은 전투의 실제 진입 이벤트를 통해 B를 참여시킨다. 기본 소속 A는 끝까지 유지한다.
	Test.Place(Pursuer, IntegrationB);
	TestEqual(TEXT("Physical pursuit arrival starts B"), Test.Combat->GetRoomState(IntegrationB), ERoomCombatState::Combat);
	TestTrue(TEXT("Destination AI joins through the room combat event"),
		Test.World->GetSubsystem<UEnemyRoomSubsystem>()->IsRoomInCombat(IntegrationB));
	TestEqual(TEXT("B roster includes only its own placed enemy"), Test.Combat->GetAliveEnemyCount(IntegrationB), 1);
	TestEqual(TEXT("Pursuer retains A ownership after entering B"), Pursuer->GetDefaultRoomTag(), IntegrationA.GetTag());
	Test.Place(Pursuer, IntegrationEvent);
	TestEqual(TEXT("Arrival does not consume an event-only sibling's trigger"),
		Test.Combat->GetRoomState(IntegrationEvent), ERoomCombatState::WaitingForTrigger);
	TestFalse(TEXT("The event-only room is not a participant"), Test.Combat->IsRoomCombatReserved(IntegrationEvent));
	Test.Place(Pursuer, IntegrationB);

	FRoomCombatPreparationContext Outside;
	TestFalse(TEXT("Another group cannot prepare while A and B participate"),
		Test.Combat->BeginInitialDetectionPreparation(IntegrationOutside, Test.Shooter, Outside));
	Test.Combat->NotifyEnemyDefeated(Pursuer);
	TestEqual(TEXT("A enemy death in B independently clears A"), Test.Combat->GetRoomState(IntegrationA), ERoomCombatState::Cleared);
	TestEqual(TEXT("A death never decrements B's roster"), Test.Combat->GetAliveEnemyCount(IntegrationB), 1);
	TestEqual(TEXT("B continues after A clears"), Test.Combat->GetRoomState(IntegrationB), ERoomCombatState::Combat);
	TestTrue(TEXT("Exterior remains blocked until B completes"), Exterior->IsBlocked());
	TestFalse(TEXT("Internal barrier remains absent after A completes"), Internal->IsBlocked());
	Test.Combat->NotifyEnemyDefeated(Test.Enemies[IntegrationB]);
	TestEqual(TEXT("B completes independently"), Test.Combat->GetRoomState(IntegrationB), ERoomCombatState::Cleared);
	TestFalse(TEXT("Final participant completion releases the whole group exterior"), Exterior->IsBlocked());
	TestFalse(TEXT("Final participant completion releases group ownership"), Test.Combat->HasActiveCombatReservation());
	TestEqual(TEXT("Unparticipating event room still waits for its own event"),
		Test.Combat->GetRoomState(IntegrationEvent), ERoomCombatState::WaitingForTrigger);
	TestTrue(TEXT("Another group can prepare after all actual participants complete"),
		Test.Combat->BeginInitialDetectionPreparation(IntegrationOutside, Test.Shooter, Outside));
	return true;
}

#endif
