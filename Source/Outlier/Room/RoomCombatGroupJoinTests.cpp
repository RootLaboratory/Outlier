#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NativeGameplayTags.h"
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
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinA, "Room.GroupJoin01.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinB, "Room.GroupJoin01.1.2");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinC, "Room.GroupJoin01.1.3");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinOutside, "Room.GroupJoin01.2.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinPrefix, "Room.GroupJoin01.10.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinLevel, "Room.GroupJoin02.1.1");

	struct FGroupJoinWorld
	{
		UWorld* World;
		UGameInstance* GameInstance;
		URoomCombatSubsystem* Combat;
		URoomCombatDefinition* Definition;
		AShooterCharacter* Shooter;
		APartnerCharacter* Partner;
		TMap<FGameplayTag, ARoomVolume*> Rooms;
		TArray<AEnemyBase*> Enemies;
		int32 TeleportCalls = 0;

		explicit FGroupJoinWorld(UClass* ShooterClass)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("GroupJoinTest")));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			GameInstance = NewObject<UGameInstance>(GEngine);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
			Combat = World->GetSubsystem<URoomCombatSubsystem>();
			Definition = NewObject<URoomCombatDefinition>(World);
			Combat->SetCombatDefinitionForTesting(Definition);
			AddRoom(JoinA, 0);
			AddRoom(JoinB, 3000, ERoomCombatPhaseStartPolicy::ExternalTrigger);
			AddRoom(JoinC, 6000);
			AddRoom(JoinOutside, 9000);
			AddRoom(JoinPrefix, 12000);
			AddRoom(JoinLevel, 15000);
			for (FGameplayTag Tag : {JoinA.GetTag(), JoinC.GetTag()})
			{
				AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>();
				Enemy->SetActorLocation(FVector(-10000, 0, 0));
				Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
				Combat->RegisterPreplacedEnemy(Enemy);
				Enemies.Add(Enemy);
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass,
				FTransform(FVector(-3000, 0, 150)), Params);
			UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
				TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
			check(PartnerClass);
			Partner = World->SpawnActor<APartnerCharacter>(PartnerClass,
				FTransform(FVector(-3000, 1000, 200)), Params);
			AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
			State->SetShooterCharacter(Shooter);
			State->SetPartnerCharacter(Partner);
			AGameStateBase* GameState = World->GetGameState();
			if (!GameState)
			{
				GameState = World->SpawnActor<AGameStateBase>();
				World->SetGameState(GameState);
			}
			GameState->PlayerArray.AddUnique(State);
			// Room과 두 Pawn의 실제 오버랩 수명만 시작한다. 전투 집계용 기본 Enemy는
			// 스탯/AI 에셋이 없는 fixture이므로 월드 전체 BeginPlay 대상에 넣지 않는다.
			World->SetBegunPlay(true);
			for (const auto& Room : Rooms) Room.Value->DispatchBeginPlay();
			Shooter->DispatchBeginPlay();
			Partner->DispatchBeginPlay();
			Combat->JoinTeleportObserverForTesting = [this] { ++TeleportCalls; };
		}

		~FGroupJoinWorld()
		{
			Combat->JoinTeleportObserverForTesting = nullptr;
			Combat->CombatEventObserverForTesting = nullptr;
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}

		void AddRoom(FGameplayTag Tag, float X,
			ERoomCombatPhaseStartPolicy Policy = ERoomCombatPhaseStartPolicy::InitialDetection)
		{
			FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
			Entry.RoomTag = Tag;
			FRoomCombatPhaseDefinition& Phase = Entry.CombatPhases.AddDefaulted_GetRef();
			Phase.StartPolicy = Policy;
			Phase.DelayBeforeStartSeconds = 0;
			Phase.Waves.AddDefaulted();
			ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
			*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
				->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
			Room->SetActorLocation(FVector(X, 0, 250));
			CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(500));
			Rooms.Add(Tag, Room);
			Combat->RegisterRoom(Room, Tag);
			// Shooter 합류는 실제 지면 Trace를 통과해야 한다. 비행 Partner와 같은 안전 검사 경로를 사용한다.
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(500, 500, 30));
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			Box->RegisterComponent();
			Floor->SetActorLocation(FVector(X, 0, -30));
		}

		void Place(AFirstPersonCharacter* Player, FGameplayTag Tag, FVector Offset = FVector::ZeroVector)
		{
			Player->SetActorLocation(Rooms.FindChecked(Tag)->GetActorLocation() + Offset);
			Player->GetRoomTagComp()->ClearRuntimeRoomAssignment();
			Player->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
			Player->GetCapsuleComponent()->UpdateOverlaps();
			Rooms.FindChecked(Tag)->RefreshOverlappingRoomAssignments();
		}

		void Reset()
		{
			Combat->CombatEventObserverForTesting = nullptr;
			Combat->ResetRuntimeCombatState();
			for (const auto& Room : Rooms) Combat->RegisterRoom(Room.Value, Room.Key);
			for (AEnemyBase* Enemy : Enemies) Combat->RegisterPreplacedEnemy(Enemy);
			TeleportCalls = 0;
			Combat->JoinTeleportObserverForTesting = [this] { ++TeleportCalls; };
		}
	};

	UClass* LoadJoinShooter()
	{
		return LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinInsideTest, "Outlier.Room.GroupJoin.InsideAndOutside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinInsideTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Test(Class);
	for (FGameplayTag PartnerTag : {JoinA.GetTag(), JoinB.GetTag()})
	{
		Test.Place(Test.Shooter, JoinA, FVector(-150, 0, 0));
		Test.Place(Test.Partner, PartnerTag, FVector(150, 0, 0));
		const FVector ShooterBefore = Test.Shooter->GetActorLocation();
		const FVector PartnerBefore = Test.Partner->GetActorLocation();
		TestTrue(TEXT("Same room or sibling pair can start"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
		TestEqual(TEXT("No teleport is invoked inside the group"), Test.TeleportCalls, 0);
		TestTrue(TEXT("Shooter stays in place"), Test.Shooter->GetActorLocation().Equals(ShooterBefore));
		TestTrue(TEXT("Partner stays in place"), Test.Partner->GetActorLocation().Equals(PartnerBefore));
		TestFalse(TEXT("Destination event room is not reserved"), Test.Combat->IsRoomCombatReserved(JoinB));
		TestTrue(TEXT("Repeated detection does not restart combat"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
		TestEqual(TEXT("Repeated detection never teleports the internal pair"), Test.TeleportCalls, 0);
		Test.Reset();
	}
	Test.Place(Test.Shooter, JoinB);
	Test.Place(Test.Partner, JoinC);
	TestTrue(TEXT("Combat room can be empty while both players are in siblings"),
		Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
	TestEqual(TEXT("Two different siblings do not teleport"), Test.TeleportCalls, 0);
	Test.Reset();
	for (FGameplayTag Outside : {JoinOutside.GetTag(), JoinPrefix.GetTag(), JoinLevel.GetTag()})
	{
		Test.Place(Test.Shooter, Outside, FVector(-150, 0, 0));
		Test.Place(Test.Partner, Outside, FVector(150, 0, 0));
		TestFalse(TEXT("Different parent, prefix or level is outside"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
		TestFalse(TEXT("No internal player means no reservation"), Test.Combat->HasActiveCombatReservation());
	}
	// 태그만 내부로 바꿔도 실제 Volume 밖이면 기준 플레이어로 인정하지 않는다.
	Test.Shooter->GetRoomTagComp()->ClearRuntimeRoomAssignment();
	Test.Shooter->GetRoomTagComp()->AssignDefaultRoomTag(JoinB);
	TestFalse(TEXT("Stale internal tag does not count as an internal position"),
		Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
	TestEqual(TEXT("Rejected starts never teleport"), Test.TeleportCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinDestinationTest, "Outlier.Room.GroupJoin.ActualDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinDestinationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Test(Class);
	for (bool bMoveShooter : {false, true})
	{
		Test.Place(Test.Shooter, bMoveShooter ? JoinOutside.GetTag() : JoinB.GetTag());
		Test.Place(Test.Partner, bMoveShooter ? JoinB.GetTag() : JoinOutside.GetTag());
		TestTrue(TEXT("Outside player joins the anchor's actual sibling room"),
			Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
		TestEqual(TEXT("Exactly one nearby teleport"), Test.TeleportCalls, 1);
		AFirstPersonCharacter* Moved = bMoveShooter ? static_cast<AFirstPersonCharacter*>(Test.Shooter)
			: static_cast<AFirstPersonCharacter*>(Test.Partner);
		TestEqual(TEXT("Moved player's current tag is destination B"), Moved->GetCurrentRoomTag(), JoinB.GetTag());
		TestTrue(TEXT("Moved player is physically inside B"), Test.Rooms[JoinB]->ContainsWorldLocation(Moved->GetActorLocation()));
		TestFalse(TEXT("Moved player is not forced into source A"), Test.Rooms[JoinA]->ContainsWorldLocation(Moved->GetActorLocation()));
		TestEqual(TEXT("Only A starts combat"), Test.Combat->GetRoomState(JoinA), ERoomCombatState::Combat);
		TestEqual(TEXT("B still waits for its external event"), Test.Combat->GetRoomState(JoinB), ERoomCombatState::WaitingForTrigger);
		TestFalse(TEXT("B is not a participant"), Test.Combat->IsRoomCombatReserved(JoinB));
		Test.Reset();
	}
	// 수평 후보가 모두 방 밖인 좁은 목적지를 만든다. 공중 fallback은 B에만 등록해
	// A의 출입구가 아니라 실제 합류 방의 지점을 사용하는지 확인한다.
	Test.Place(Test.Shooter, JoinB);
	Test.Place(Test.Partner, JoinOutside);
	const float Width = FMath::Max(Test.Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius(),
		Test.Partner->GetCapsuleComponent()->GetScaledCapsuleRadius()) + 10.0f;
	CastChecked<UBoxComponent>(Test.Rooms[JoinB]->GetRootComponent())->SetBoxExtent(FVector(Width, Width, 500));
	ARoomCombatBarrier* Fallback = Test.World->SpawnActor<ARoomCombatBarrier>();
	*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("RoomTag"))
		->ContainerPtrToValuePtr<FGameplayTag>(Fallback) = JoinB;
	Fallback->SetActorLocation(Test.Rooms[JoinB]->GetActorLocation() + FVector(1000, 0, 0));
	const float Height = Test.Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		+ Test.Partner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.0f;
	*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("JoinFallbackLocalOffset"))
		->ContainerPtrToValuePtr<FVector>(Fallback) = FVector(-1000, 0, Height);
	Test.Combat->RegisterBarrier(Fallback);
	TestTrue(TEXT("Sibling destination uses its own safe fallback"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
	TestTrue(TEXT("Partner arrives at B's configured fallback"),
		Test.Partner->GetActorLocation().Equals(Fallback->GetJoinFallbackLocation(), 0.1f));
	TestEqual(TEXT("Only the valid fallback invokes teleport"), Test.TeleportCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinRecoveryTest, "Outlier.Room.GroupJoin.RecoveryAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	IConsoleVariable* Retry = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.RoomCombat.InitialDetectionRetrySeconds"));
	if (!TestNotNull(TEXT("Retry interval setting"), Retry)) return false;
	const float PreviousRetry = Retry->GetFloat();
	Retry->Set(1.0f, ECVF_SetByCode);
	ON_SCOPE_EXIT { Retry->Set(PreviousRetry, ECVF_SetByCode); };
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Test(Class);
	Test.Place(Test.Shooter, JoinB);
	Test.Place(Test.Partner, JoinOutside);
	TestTrue(TEXT("C is already in combat"), Test.Combat->NotifyRoomCombatStarted(JoinC));
	UBoxComponent* Box = CastChecked<UBoxComponent>(Test.Rooms[JoinB]->GetRootComponent());
	Box->SetBoxExtent(FVector(20));
	const FVector Before = Test.Partner->GetActorLocation();
	TestFalse(TEXT("Unsafe destination rejects join"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
	TestEqual(TEXT("A leaves Preparing"), Test.Combat->GetRoomState(JoinA), ERoomCombatState::Dormant);
	TestFalse(TEXT("Only A reservation is released"), Test.Combat->IsRoomCombatReserved(JoinA));
	TestTrue(TEXT("C reservation survives A failure"), Test.Combat->IsRoomCombatReserved(JoinC));
	TestFalse(TEXT("A barrier remains open"), Test.Combat->IsExitBlocked(JoinA));
	TestTrue(TEXT("Failure never forces movement"), Test.Partner->GetActorLocation().Equals(Before));
	Box->SetBoxExtent(FVector(500));
	TestFalse(TEXT("Immediate new detection is throttled"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
	for (int32 Index = 0; Index < 11; ++Index) Test.World->Tick(LEVELTICK_All, 0.1f);
	TestEqual(TEXT("Time alone does not retry"), Test.Combat->GetRoomState(JoinA), ERoomCombatState::Dormant);
	TestTrue(TEXT("Fresh detection after cooldown succeeds"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
	TestTrue(TEXT("C remains active after A joins"), Test.Combat->IsRoomCombatReserved(JoinC));
	Test.Reset();

	// 실제 이동 직전 또는 시작 이벤트 중 목적지 등록 수명이 바뀌는 두 경계를 검증한다.
	for (bool bBeforeTeleport : {true, false})
	{
		Test.Place(Test.Shooter, JoinB);
		Test.Place(Test.Partner, bBeforeTeleport ? JoinOutside.GetTag() : JoinC.GetTag());
		bool bReloaded = false;
		auto ReloadDestination = [&Test, &bReloaded]
		{
			bReloaded = true;
			ARoomVolume* Destination = Test.Rooms[JoinB];
			Test.Combat->UnregisterRoom(Destination);
			Test.Combat->RegisterRoom(Destination, JoinB);
		};
		if (bBeforeTeleport) Test.Combat->JoinTeleportObserverForTesting = ReloadDestination;
		else Test.Combat->CombatEventObserverForTesting = [&](FGameplayTag Tag, ERoomCombatEvent Event, int32)
		{
			if (Tag == JoinA && Event == ERoomCombatEvent::SequenceStarted) ReloadDestination();
		};
		TestFalse(TEXT("Old destination lifetime cannot confirm combat"), Test.Combat->TryStartInitialDetectionForPlayers(JoinA));
		TestTrue(TEXT("Destination lifetime actually changed at the tested boundary"), bReloaded);
		TestEqual(TEXT("Stale join releases preparation"), Test.Combat->GetRoomState(JoinA), ERoomCombatState::Dormant);
		TestFalse(TEXT("Stale join releases A reservation"), Test.Combat->IsRoomCombatReserved(JoinA));
		TestFalse(TEXT("Stale join rolls back its barrier"), Test.Combat->IsExitBlocked(JoinA));
		Test.Reset();
	}
	return true;
}

#endif
