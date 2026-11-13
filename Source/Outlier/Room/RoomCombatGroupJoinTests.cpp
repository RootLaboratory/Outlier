#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NativeGameplayTags.h"
#include "OutlierPlayerState.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Room/RoomCombatBarrier.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UnrealType.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinA, "Room.GroupJoin01.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinB, "Room.GroupJoin01.1.2");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinC, "Room.GroupJoin01.1.3");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinOutside, "Room.GroupJoin01.2.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinPrefix, "Room.GroupJoin01.10.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinLevel, "Room.GroupJoin02.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinUnregistered, "Room.GroupJoin01.1.99");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinLegacy, "Room.GroupJoinLegacy.3");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(JoinLegacyOther, "Room.GroupJoinLegacy.4");

	struct FGroupJoinWorld
	{
		UWorld* World;
		UGameInstance* GameInstance;
		URoomCombatSubsystem* Combat;
		URoomCombatDefinition* Definition;
		AShooterCharacter* Shooter;
		APartnerCharacter* Partner;
		AOutlierPlayerState* PairState;
		TMap<FGameplayTag, ARoomVolume*> Rooms;
		TMap<FGameplayTag, ARoomCombatBarrier*> Barriers;
		TMap<FGameplayTag, UBoxComponent*> Floors;
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
			World->CreateAISystem();
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
			PairState = State;
			PairState->SetAcquiredSuit(true);
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
			for (const auto& Entry : Rooms) AddBarrier(Entry.Key);
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
			Floors.Add(Tag, Box);
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

		UBoxComponent* AddBlocker(const FVector& Location, const FVector& Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			Box->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Box;
		}

		void AddFallback(FGameplayTag Tag, const FVector& Location)
		{
			ARoomCombatBarrier* Barrier = World->SpawnActor<ARoomCombatBarrier>();
			*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("RoomTag"))
				->ContainerPtrToValuePtr<FGameplayTag>(Barrier) = Tag;
			// 차단막 자체는 후보 영역 밖에 두고, 실제 맵처럼 로컬 오프셋으로 안전 지점을 지정한다.
			Barrier->SetActorLocation(Location + FVector(2000, 0, 0));
			*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("JoinFallbackLocalOffset"))
				->ContainerPtrToValuePtr<FVector>(Barrier) = FVector(-2000, 0, 0);
			Combat->RegisterBarrier(Barrier);
		}

		void AddBarrier(FGameplayTag Tag)
		{
			ARoomCombatBarrier* Barrier = World->SpawnActor<ARoomCombatBarrier>();
			*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("RoomTag"))
				->ContainerPtrToValuePtr<FGameplayTag>(Barrier) = Tag;
			// 기본 fixture는 차단막 존재 조건만 갖춘다. 안전 fallback이 필요한 사례에서만
			// AddFallback으로 방 내부 위치를 별도로 설정한다.
			Barrier->SetActorLocation(FVector(0, 0, -10000));
			Combat->RegisterBarrier(Barrier);
			Barriers.Add(Tag, Barrier);
		}

		void PlaceJoinPair(bool bMoveShooter, AFirstPersonCharacter*& Anchor, AFirstPersonCharacter*& Moving)
		{
			Anchor = bMoveShooter ? static_cast<AFirstPersonCharacter*>(Partner) : Shooter;
			Moving = bMoveShooter ? static_cast<AFirstPersonCharacter*>(Shooter) : Partner;
			Place(Anchor, JoinB);
			Place(Moving, JoinOutside);
			Anchor->SetActorRotation(FRotator::ZeroRotator);
			// 두 역할 모두 바닥과 겹치지 않는 높이에서 시작한다. Shooter의 합류 후보만
			// 실제 지면 Trace로 높이가 보정되므로 장애물도 그 최종 높이에 배치해야 한다.
			const float Height = FMath::Max(Anchor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
				Moving->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) + 1.0f;
			FVector Location = Anchor->GetActorLocation();
			Location.Z = Height;
			Anchor->SetActorLocation(Location);
		}

		void AdvanceFrames(int32 Frames)
		{
			for (int32 Index = 0; Index < Frames; ++Index)
			{
				// 동기 테스트의 World Tick 반복은 엔진 프레임 번호를 올리지 않는다.
				// TimerManager의 같은 프레임 중복 실행 방지를 넘겨 GAS 만료 시간도 진행시킨다.
				++GFrameCounter;
				World->Tick(LEVELTICK_All, 0.1f);
			}
		}

		void AdvanceRetryInterval()
		{
			IConsoleVariable* Retry = IConsoleManager::Get().FindConsoleVariable(
				TEXT("outlier.RoomCombat.InitialDetectionRetrySeconds"));
			const float Duration = FMath::Max(Retry->GetFloat(), 0.0f) + 0.2f;
			AdvanceFrames(FMath::CeilToInt(Duration / 0.1f));
		}

		void CompleteQueries()
		{
			// 목적지 응답만 진행한다. 이동/효과 수명/재시도 간격은 이 대기로 바꾸지 않는다.
			for (int32 Index = 0; Index < 100; ++Index)
				UEnvQueryManager::GetCurrent(World)->Tick(0.01f);
		}

		bool StartAndCompleteJoin(FGameplayTag RoomTag)
		{
			const bool bAlreadyStarted = Combat->TryStartInitialDetectionForPlayers(RoomTag);
			CompleteQueries();
			return bAlreadyStarted || Combat->GetRoomState(RoomTag) == ERoomCombatState::Combat;
		}
	};

	UClass* LoadJoinShooter()
	{
		return LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	}

	void CheckJoinReleased(FAutomationTestBase& Test, const FGroupJoinWorld& Fixture)
	{
		Test.TestFalse(TEXT("Failed join does not protect Shooter"), Fixture.Shooter->GetOutlierAbilitySystemComponent()
			->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
		Test.TestFalse(TEXT("Failed join does not protect Partner"), Fixture.Partner->GetOutlierAbilitySystemComponent()
			->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
		Test.TestEqual(TEXT("Failed join leaves Preparing"), Fixture.Combat->GetRoomState(JoinA), ERoomCombatState::Dormant);
		Test.TestFalse(TEXT("Failed join releases its reservation"), Fixture.Combat->IsRoomCombatReserved(JoinA));
		Test.TestFalse(TEXT("Failed join does not close the exit"), Fixture.Combat->IsExitBlocked(JoinA));
	}

	bool ContainsJoinCapsule(const ARoomVolume* Room, const AFirstPersonCharacter* Player)
	{
		const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float Height = Capsule->GetScaledCapsuleHalfHeight();
		for (const FVector& Offset : {FVector(Radius, 0, 0), FVector(-Radius, 0, 0),
			FVector(0, Radius, 0), FVector(0, -Radius, 0), FVector(0, 0, Height), FVector(0, 0, -Height)})
		{
			if (!Room->ContainsWorldLocation(Player->GetActorLocation() + Offset)) return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinPendingTest, "Outlier.Room.GroupJoin.EQS.PendingCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinPendingTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Fixture(Class);
	AFirstPersonCharacter* Anchor;
	AFirstPersonCharacter* Moving;
	Fixture.PlaceJoinPair(false, Anchor, Moving);
	const FVector Before = Moving->GetActorLocation();
	TestFalse(TEXT("A pending EQS request is not completed combat"), Fixture.Combat->TryStartInitialDetectionForPlayers(JoinA));
	TestEqual(TEXT("Pending request holds preparation"), Fixture.Combat->GetRoomState(JoinA), ERoomCombatState::Preparing);
	TestTrue(TEXT("Pending request does not move the player"), Moving->GetActorLocation().Equals(Before));
	TestFalse(TEXT("Pending request never closes the barrier"), Fixture.Combat->IsExitBlocked(JoinA));
	TestFalse(TEXT("Pending request does not grant protection"), Fixture.Partner->GetOutlierAbilitySystemComponent()
		->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestFalse(TEXT("Duplicate detection cannot finish a pending request"), Fixture.Combat->TryStartInitialDetectionForPlayers(JoinA));
	FRoomCombatPreparationContext Context;
	TestTrue(TEXT("Repeated begin returns the active preparation token"),
		Fixture.Combat->BeginInitialDetectionPreparation(JoinA, Anchor, Context));
	TestFalse(TEXT("Public completion cannot bypass pending destination validation"),
		Fixture.Combat->CompleteInitialDetectionPreparation(Context));
	TestEqual(TEXT("Rejected manual completion keeps the query preparing"),
		Fixture.Combat->GetRoomState(JoinA), ERoomCombatState::Preparing);
	Fixture.CompleteQueries();
	TestEqual(TEXT("Query completion starts combat"), Fixture.Combat->GetRoomState(JoinA), ERoomCombatState::Combat);
	TestEqual(TEXT("Duplicate detection still makes only one teleport"), Fixture.TeleportCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinPendingLifetimeTest, "Outlier.Room.GroupJoin.EQS.PendingLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinPendingLifetimeTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	for (int32 Scenario = 0; Scenario < 8; ++Scenario)
	{
		AddInfo(FString::Printf(TEXT("Pending lifetime scenario %d"), Scenario));
		FGroupJoinWorld Fixture(Class);
		AFirstPersonCharacter* Anchor;
		AFirstPersonCharacter* Moving;
		Fixture.PlaceJoinPair(false, Anchor, Moving);
		APlayerController* Controller = nullptr;
		if (Scenario == 7)
		{
			// 원래 Controller가 null인 문맥과, 실제 Controller가 대기 중 파괴된 문맥을 구분한다.
			Controller = Fixture.World->SpawnActor<APlayerController>();
			Controller->Possess(Moving);
		}
		const FVector Before = Moving->GetActorLocation();
		TestFalse(TEXT("Async join starts pending"), Fixture.Combat->TryStartInitialDetectionForPlayers(JoinA));
		TestEqual(TEXT("Invalidation occurs while preparing"), Fixture.Combat->GetRoomState(JoinA), ERoomCombatState::Preparing);
		switch (Scenario)
		{
		case 0: Fixture.Combat->ResetRuntimeCombatState(); break;
		case 1: Fixture.Place(Anchor, JoinOutside, FVector(0, 300, 0)); break;
		case 2: Fixture.PairState->SetPartnerCharacter(nullptr); break;
		case 3:
			Fixture.Combat->UnregisterRoom(Fixture.Rooms[JoinB]);
			Fixture.Combat->RegisterRoom(Fixture.Rooms[JoinB], JoinB);
			break;
		case 4: Fixture.Partner->GetOutlierAbilitySystemComponent()->AddLooseGameplayTag(OutlierGameplayTags::State::Rebooting()); break;
		case 5:
		{
			// EQS 응답을 처리하지 않은 채 준비 제한만 만료시킨다.
			TGuardValue<uint64> RestoreFrame(GFrameCounter, GFrameCounter);
			for (int32 Index = 0; Index < 70; ++Index)
			{
				++GFrameCounter;
				Fixture.World->GetTimerManager().Tick(0.1f);
			}
			break;
		}
		case 6:
		{
			FRoomCombatPreparationContext Context;
			TestTrue(TEXT("Existing preparation exposes the same cancellation token"),
				Fixture.Combat->BeginInitialDetectionPreparation(JoinA, Anchor, Context));
			TestTrue(TEXT("Explicit cancellation succeeds"), Fixture.Combat->CancelInitialDetectionPreparation(Context));
			break;
		}
		case 7: Controller->Destroy(); break;
		}
		Fixture.CompleteQueries();
		CheckJoinReleased(*this, Fixture);
		TestTrue(TEXT("Invalidated query never moves the player"), Moving->GetActorLocation().Equals(Before));
		TestEqual(TEXT("Invalidated query never teleports"), Fixture.TeleportCalls, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinInsideTest, "Outlier.Room.GroupJoin.InsideAndOutside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinInsideTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Test(Class);
	Test.Place(Test.Shooter, JoinA, FVector(-150, 0, 0));
	Test.Place(Test.Partner, JoinA, FVector(150, 0, 0));
	const FVector ShooterBefore = Test.Shooter->GetActorLocation();
	const FVector PartnerBefore = Test.Partner->GetActorLocation();
	TestTrue(TEXT("Same-room pair can start"), Test.StartAndCompleteJoin(JoinA));
	TestEqual(TEXT("Same-room pair never teleports"), Test.TeleportCalls, 0);
	TestFalse(TEXT("Internal Shooter receives no protection"), Test.Shooter->GetOutlierAbilitySystemComponent()
		->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestFalse(TEXT("Internal Partner receives no protection"), Test.Partner->GetOutlierAbilitySystemComponent()
		->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestTrue(TEXT("Shooter stays in place"), Test.Shooter->GetActorLocation().Equals(ShooterBefore));
	TestTrue(TEXT("Partner stays in place"), Test.Partner->GetActorLocation().Equals(PartnerBefore));
	TestFalse(TEXT("Destination event room is not reserved"), Test.Combat->IsRoomCombatReserved(JoinB));
	TestTrue(TEXT("Repeated detection does not restart combat"), Test.StartAndCompleteJoin(JoinA));
	TestEqual(TEXT("Repeated detection never teleports the internal pair"), Test.TeleportCalls, 0);
	Test.Reset();
	Test.Place(Test.Shooter, JoinB);
	Test.Place(Test.Partner, JoinC);
	TestTrue(TEXT("Combat room can be empty while both players are in siblings"),
		Test.StartAndCompleteJoin(JoinA));
	TestEqual(TEXT("Different siblings require a real join"), Test.TeleportCalls, 1);
	TestEqual(TEXT("Partner joins the anchor's exact room"), Test.Partner->GetCurrentRoomTag(), JoinB.GetTag());
	Test.Reset();
	for (FGameplayTag Outside : {JoinOutside.GetTag(), JoinPrefix.GetTag(), JoinLevel.GetTag()})
	{
		Test.Place(Test.Shooter, Outside, FVector(-150, 0, 0));
		Test.Place(Test.Partner, Outside, FVector(150, 0, 0));
		TestFalse(TEXT("Different parent, prefix or level is outside"), Test.StartAndCompleteJoin(JoinA));
		TestFalse(TEXT("No internal player means no reservation"), Test.Combat->HasActiveCombatReservation());
	}
	// 태그만 내부로 바꿔도 실제 Volume 밖이면 기준 플레이어로 인정하지 않는다.
	Test.Shooter->GetRoomTagComp()->ClearRuntimeRoomAssignment();
	Test.Shooter->GetRoomTagComp()->AssignDefaultRoomTag(JoinB);
	TestFalse(TEXT("Stale internal tag does not count as an internal position"),
		Test.StartAndCompleteJoin(JoinA));
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
			Test.StartAndCompleteJoin(JoinA));
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
	TestTrue(TEXT("Sibling destination uses its own safe fallback"), Test.StartAndCompleteJoin(JoinA));
	TestTrue(TEXT("Partner arrives at B's configured fallback"),
		Test.Partner->GetActorLocation().Equals(Fallback->GetJoinFallbackLocation(), 0.1f));
	TestEqual(TEXT("Only the valid fallback invokes teleport"), Test.TeleportCalls, 1);
	TestTrue(TEXT("Successful fallback protects the moved Partner"), Test.Partner->GetOutlierAbilitySystemComponent()
		->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinConditionsTest, "Outlier.Room.GroupJoin.SuitBarrierAndExactRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinConditionsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	for (bool bSuitAcquired : {false, true})
	{
		for (bool bHasBarrier : {false, true})
		{
			for (FGameplayTag OtherRoom : {JoinA.GetTag(), JoinC.GetTag(), JoinOutside.GetTag()})
			{
				for (bool bMoveShooter : {false, true})
				{
					AddInfo(FString::Printf(TEXT("Suit=%d Barrier=%d OtherRoom=%s MoveShooter=%d"),
						bSuitAcquired, bHasBarrier, *OtherRoom.ToString(), bMoveShooter));
					FGroupJoinWorld Fixture(Class);
					Fixture.PairState->SetAcquiredSuit(bSuitAcquired);
					if (!bHasBarrier) Fixture.Combat->UnregisterBarrier(Fixture.Barriers[JoinA]);
					AFirstPersonCharacter* Anchor = bMoveShooter
						? static_cast<AFirstPersonCharacter*>(Fixture.Partner) : Fixture.Shooter;
					AFirstPersonCharacter* Other = bMoveShooter
						? static_cast<AFirstPersonCharacter*>(Fixture.Shooter) : Fixture.Partner;
					Fixture.Place(Anchor, JoinA, FVector(-150, 0, 0));
					Fixture.Place(Other, OtherRoom, FVector(150, 0, 0));
					const FVector AnchorBefore = Anchor->GetActorLocation();
					const FVector OtherBefore = Other->GetActorLocation();
					const bool bShouldMove = bSuitAcquired && bHasBarrier && OtherRoom != JoinA;
					TestFalse(TEXT("Barrier starts with collision disabled"), Fixture.Barriers[JoinA]->IsBlocked());
					TestTrue(TEXT("Skipping movement never stalls valid initial combat"),
						Fixture.StartAndCompleteJoin(JoinA));
					TestEqual(TEXT("Only all three conditions permit teleport"), Fixture.TeleportCalls, bShouldMove ? 1 : 0);
					TestTrue(TEXT("Anchor remains in place"), Anchor->GetActorLocation().Equals(AnchorBefore));
					UOutlierAbilitySystemComponent* OtherASC = bMoveShooter
						? Fixture.Shooter->GetOutlierAbilitySystemComponent() : Fixture.Partner->GetOutlierAbilitySystemComponent();
					TestEqual(TEXT("Only a moved player receives join immunity"),
						OtherASC->HasMatchingGameplayTag(
							OutlierGameplayTags::State::DamageImmune()), bShouldMove);
					if (bShouldMove)
					{
						TestEqual(TEXT("Moving player reaches the exact requested room"), Other->GetCurrentRoomTag(), JoinA.GetTag());
					}
					else
					{
						TestTrue(TEXT("Skipped player keeps the original position"), Other->GetActorLocation().Equals(OtherBefore));
					}
				}
			}
		}
	}
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
	TestFalse(TEXT("Unsafe destination rejects join"), Test.StartAndCompleteJoin(JoinA));
	TestEqual(TEXT("A leaves Preparing"), Test.Combat->GetRoomState(JoinA), ERoomCombatState::Dormant);
	TestFalse(TEXT("Only A reservation is released"), Test.Combat->IsRoomCombatReserved(JoinA));
	TestTrue(TEXT("C reservation survives A failure"), Test.Combat->IsRoomCombatReserved(JoinC));
	TestFalse(TEXT("A barrier remains open"), Test.Combat->IsExitBlocked(JoinA));
	TestTrue(TEXT("Failure never forces movement"), Test.Partner->GetActorLocation().Equals(Before));
	Box->SetBoxExtent(FVector(500));
	TestFalse(TEXT("Immediate new detection is throttled"), Test.StartAndCompleteJoin(JoinA));
	for (int32 Index = 0; Index < 11; ++Index) Test.World->Tick(LEVELTICK_All, 0.1f);
	TestEqual(TEXT("Time alone does not retry"), Test.Combat->GetRoomState(JoinA), ERoomCombatState::Dormant);
	TestTrue(TEXT("Fresh detection after cooldown succeeds"), Test.StartAndCompleteJoin(JoinA));
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
		TestFalse(TEXT("Old destination lifetime cannot confirm combat"), Test.StartAndCompleteJoin(JoinA));
		TestTrue(TEXT("Destination lifetime actually changed at the tested boundary"), bReloaded);
		TestEqual(TEXT("Stale join releases preparation"), Test.Combat->GetRoomState(JoinA), ERoomCombatState::Dormant);
		TestFalse(TEXT("Stale join releases A reservation"), Test.Combat->IsRoomCombatReserved(JoinA));
		TestFalse(TEXT("Stale join rolls back its barrier"), Test.Combat->IsExitBlocked(JoinA));
		Test.Reset();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinNarrowSpaceTest, "Outlier.Room.GroupJoin.Scenarios.NarrowSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinNarrowSpaceTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	for (bool bMoveShooter : {false, true})
	{
		for (bool bBlockFallback : {false, true})
		{
			FGroupJoinWorld Fixture(Class);
			AFirstPersonCharacter* Anchor;
			AFirstPersonCharacter* Moving;
			Fixture.PlaceJoinPair(bMoveShooter, Anchor, Moving);
			const FVector Before = Moving->GetActorLocation();
			const float Height = Moving->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FVector Fallback;
			if (bMoveShooter)
			{
				// 3m 탐색 안에는 바닥이 없다. 지정 출입구 아래에만 실제 착지 발판을 둔다.
				Fixture.Floors[JoinB]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Fallback = FVector(3370, 0, Height + 2);
				Fixture.AddBlocker(FVector(3370, 0, -10), FVector(50, 60, 10));
			}
			else
			{
				// 비행은 같은 높이의 좁은 방 안에서 서로 분리될 수 없다. 지정 상층 공간만 안전하다.
				const float Width = FMath::Max(Anchor->GetCapsuleComponent()->GetScaledCapsuleRadius(),
					Moving->GetCapsuleComponent()->GetScaledCapsuleRadius()) + 10;
				CastChecked<UBoxComponent>(Fixture.Rooms[JoinB]->GetRootComponent())->SetBoxExtent(FVector(Width, Width, 500));
				Fallback = Anchor->GetActorLocation() + FVector(0, 0,
					Anchor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + Height + 20);
			}
			Fixture.AddFallback(JoinB, Fallback);
			if (bBlockFallback) Fixture.AddBlocker(Fallback, FVector(10, 10, 1000));
			TestEqual(TEXT("Designated fallback still requires a safe role-specific destination"),
				Fixture.StartAndCompleteJoin(JoinA), !bBlockFallback);
			if (!bBlockFallback)
			{
				TestTrue(TEXT("Valid fallback outside the nearby candidates is used"), Moving->GetActorLocation().Equals(Fallback, 0.1));
				TestTrue(TEXT("Fallback contains the whole capsule"), ContainsJoinCapsule(Fixture.Rooms[JoinB], Moving));
				TestEqual(TEXT("Fallback moves once"), Fixture.TeleportCalls, 1);
			}
			else
			{
				CheckJoinReleased(*this, Fixture);
				TestTrue(TEXT("Rejected fallback preserves the previous location"), Moving->GetActorLocation().Equals(Before));
				TestEqual(TEXT("Rejected fallback never teleports"), Fixture.TeleportCalls, 0);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinCrouchTest, "Outlier.Room.GroupJoin.EQS.CrouchedLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinCrouchTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Fixture(Class);
	AFirstPersonCharacter* Anchor;
	AFirstPersonCharacter* Moving;
	Fixture.PlaceJoinPair(true, Anchor, Moving);
	const float StandingHeight = Moving->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float CrouchedHeight = Moving->GetCharacterMovement()->GetCrouchedHalfHeight()
		* Moving->GetCapsuleComponent()->GetShapeScale();
	const float CeilingBottom = StandingHeight + CrouchedHeight;
	FVector AnchorLocation = Anchor->GetActorLocation();
	AnchorLocation.Z = FMath::Min(Anchor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2, CeilingBottom - 2);
	Anchor->SetActorLocation(AnchorLocation);
	Fixture.AddBlocker(FVector(3000, 0, CeilingBottom + 10), FVector(500, 500, 10));
	TestTrue(TEXT("Low ceiling admits crouched ground landing"), Fixture.StartAndCompleteJoin(JoinA));
	TestTrue(TEXT("Shooter lands with the crouched capsule"), Fixture.Shooter->bIsCrouched);
	UShooterMovementComponent* Intent = Fixture.Shooter->FindComponentByClass<UShooterMovementComponent>();
	if (!TestNotNull(TEXT("Shooter movement input"), Intent)) return false;
	TestFalse(TEXT("Automatic crouch preserves the original standing input intent"), Intent->WantsToCrouch());
	TestTrue(TEXT("Crouched landing still lies fully in the destination"), ContainsJoinCapsule(Fixture.Rooms[JoinB], Moving));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinBoundaryTest, "Outlier.Room.GroupJoin.Scenarios.BoundaryPositions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	enum class EBoundary { Corner, DoorSill, CapsuleOutside, SharedFace, AnchorLeaves };
	const TCHAR* CaseNames[] = {TEXT("Corner"), TEXT("DoorSill"), TEXT("CapsuleOutside"), TEXT("SharedFace"), TEXT("AnchorLeaves")};
	for (bool bMoveShooter : {false, true})
	{
		for (EBoundary Boundary : {EBoundary::Corner, EBoundary::DoorSill, EBoundary::CapsuleOutside,
			EBoundary::SharedFace, EBoundary::AnchorLeaves})
		{
			AddInfo(FString::Printf(TEXT("Boundary: Moving=%s Case=%s"),
				bMoveShooter ? TEXT("Shooter") : TEXT("Partner"), CaseNames[static_cast<int32>(Boundary)]));
			FGroupJoinWorld Fixture(Class);
			AFirstPersonCharacter* Anchor;
			AFirstPersonCharacter* Moving;
			Fixture.PlaceJoinPair(bMoveShooter, Anchor, Moving);
			ARoomVolume* Room = Fixture.Rooms[JoinB];
			UBoxComponent* Box = CastChecked<UBoxComponent>(Room->GetRootComponent());
			const FVector Before = Moving->GetActorLocation();
			bool bReachedTeleport = false;
			if (Boundary == EBoundary::Corner)
			{
				Anchor->SetActorLocation(Anchor->GetActorLocation() + FVector(480, 480, 0));
			}
			else if (Boundary == EBoundary::DoorSill)
			{
				// 주변 후보 아래의 실제 바닥을 60cm 높인다. Shooter는 높이가 보정되고
				// 비행 Partner는 지면 검사를 추가하지 않은 채 안전한 공중 위치를 사용한다.
				Fixture.AddBlocker(FVector(3000, 0, 30), FVector(400, 400, 30));
				Anchor->SetActorLocation(Anchor->GetActorLocation() + FVector(0, 0, 60));
			}
			else if (Boundary == EBoundary::CapsuleOutside)
			{
				Box->SetBoxExtent(FVector(Moving->GetCapsuleComponent()->GetScaledCapsuleRadius() - 1, 500, 500));
				TestTrue(TEXT("Anchor center remains inside the narrow boundary"), Room->ContainsWorldLocation(Anchor->GetActorLocation()));
				TestFalse(TEXT("Center-only placement cannot contain the moving capsule"),
					Room->ContainsWorldLocation(Anchor->GetActorLocation() + FVector(Moving->GetCapsuleComponent()->GetScaledCapsuleRadius(), 0, 0)));
			}
			else if (Boundary == EBoundary::SharedFace)
			{
				Fixture.Rooms[JoinC]->SetActorLocation(FVector(4000, 0, 250));
				Anchor->SetActorLocation(Anchor->GetActorLocation() + FVector(500, 0, 0));
				Anchor->GetCapsuleComponent()->UpdateOverlaps();
				// 면만 맞닿아도 캡슐은 양쪽 방에 걸친다. B 입장을 마지막으로 통지해
				// 경계의 기준 플레이어 주변에서 B 안쪽 안전 위치를 찾는지 검증한다.
				Anchor->GetRoomTagComp()->EnterRoom(Room);
				TestTrue(TEXT("Shared face is physically in both rooms"),
					Room->ContainsWorldLocation(Anchor->GetActorLocation())
					&& Fixture.Rooms[JoinC]->ContainsWorldLocation(Anchor->GetActorLocation()));
			}
			else
			{
				Fixture.Combat->JoinTeleportObserverForTesting = [&]
				{
					bReachedTeleport = true;
					Fixture.Place(Anchor, JoinOutside, FVector(0, 200, 0));
				};
			}
			const bool bExpectedSuccess = Boundary != EBoundary::CapsuleOutside && Boundary != EBoundary::AnchorLeaves;
			TestEqual(TEXT("Boundary result respects capsule and anchor validity"),
				Fixture.StartAndCompleteJoin(JoinA), bExpectedSuccess);
			Fixture.Combat->JoinTeleportObserverForTesting = [&Fixture] { ++Fixture.TeleportCalls; };
			if (bExpectedSuccess)
			{
				TestTrue(TEXT("Boundary join arrives with its whole capsule inside B"), ContainsJoinCapsule(Room, Moving));
				TestEqual(TEXT("Final room tag agrees with the destination"), Moving->GetCurrentRoomTag(), JoinB.GetTag());
				if (Boundary == EBoundary::DoorSill && bMoveShooter)
				{
					TestTrue(TEXT("Shooter is placed on top of the raised sill"), FMath::IsNearlyEqual(
						Moving->GetActorLocation().Z - Moving->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 62.0, 0.1));
				}
			}
			else
			{
				CheckJoinReleased(*this, Fixture);
				TestTrue(TEXT("Rejected boundary does not move the outside player"), Moving->GetActorLocation().Equals(Before));
				if (Boundary == EBoundary::AnchorLeaves) TestTrue(TEXT("Anchor leaves at the actual teleport boundary"), bReachedTeleport);
				Box->SetBoxExtent(FVector(500));
				Fixture.Place(Anchor, JoinB);
				TestFalse(TEXT("Boundary failure is throttled"), Fixture.StartAndCompleteJoin(JoinA));
				Fixture.AdvanceRetryInterval();
				CheckJoinReleased(*this, Fixture);
				TestTrue(TEXT("New detection recovers after the boundary is repaired"), Fixture.StartAndCompleteJoin(JoinA));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinTagScenarioTest, "Outlier.Room.GroupJoin.Scenarios.RoomTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinTagScenarioTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	for (bool bMoveShooter : {false, true})
	{
		for (FGameplayTag Tag : {JoinA.GetTag(), JoinOutside.GetTag(), JoinPrefix.GetTag(), JoinLevel.GetTag(),
			JoinB.GetTag().RequestDirectParent(), JoinLegacy.GetTag(), JoinUnregistered.GetTag(), FGameplayTag()})
		{
			AddInfo(FString::Printf(TEXT("Stale tag: Moving=%s Tag=%s"),
				bMoveShooter ? TEXT("Shooter") : TEXT("Partner"), *Tag.ToString()));
			FGroupJoinWorld Fixture(Class);
			AFirstPersonCharacter* Anchor;
			AFirstPersonCharacter* Moving;
			Fixture.PlaceJoinPair(bMoveShooter, Anchor, Moving);
			const FVector Before = Moving->GetActorLocation();
			// 실제 위치는 B 내부에 두고, 지연이나 잘못된 설정으로 현재 태그만 다른 상태를 만든다.
			// 같은 그룹의 A 태그여도 실제 A 영역에 없으면 내부 플레이어로 인정하지 않는다.
			Anchor->GetRoomTagComp()->ClearRuntimeRoomAssignment();
			Anchor->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
			TestEqual(TEXT("Test exercises the intended mismatched tag"), Anchor->GetCurrentRoomTag(), Tag);
			TestFalse(TEXT("Tag without matching registered physical room cannot anchor a join"),
				Fixture.StartAndCompleteJoin(JoinA));
			CheckJoinReleased(*this, Fixture);
			TestEqual(TEXT("Invalid anchor never attempts teleport"), Fixture.TeleportCalls, 0);
			TestTrue(TEXT("Invalid tag does not move the outside player"), Moving->GetActorLocation().Equals(Before));
			Fixture.Rooms[JoinB]->RefreshOverlappingRoomAssignments();
			TestEqual(TEXT("Actual room refresh repairs the tag"), Anchor->GetCurrentRoomTag(), JoinB.GetTag());
			TestTrue(TEXT("Correct sibling tag and physical position allow joining"),
				Fixture.StartAndCompleteJoin(JoinA));
		}

		// 기존 세 단계 태그는 같은 방만 인정한다. 공통 접두사로 형제 관계를
		// 추측하지 않으므로 다른 기존 방의 플레이어는 실제 합류 이동이 필요하다.
		FGroupJoinWorld Fixture(Class);
		Fixture.AddRoom(JoinLegacy, 18000);
		Fixture.AddRoom(JoinLegacyOther, 21000);
		Fixture.AddBarrier(JoinLegacy);
		AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>();
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(JoinLegacy);
		Fixture.Combat->RegisterPreplacedEnemy(Enemy);
		Fixture.Enemies.Add(Enemy);
		Fixture.Place(Fixture.Shooter, JoinLegacy, FVector(-150, 0, 0));
		Fixture.Place(Fixture.Partner, JoinLegacy, FVector(150, 0, 0));
		TestTrue(TEXT("Legacy exact-room pair can start without a group"), Fixture.StartAndCompleteJoin(JoinLegacy));
		TestEqual(TEXT("Legacy same-room pair stays in place"), Fixture.TeleportCalls, 0);
		Fixture.Reset();
		AFirstPersonCharacter* Moving = bMoveShooter ? static_cast<AFirstPersonCharacter*>(Fixture.Shooter) : Fixture.Partner;
		Fixture.Place(Moving, JoinLegacyOther);
		TestTrue(TEXT("Different legacy room requires a real join"), Fixture.StartAndCompleteJoin(JoinLegacy));
		TestEqual(TEXT("Legacy prefix does not suppress teleport"), Fixture.TeleportCalls, 1);
		TestEqual(TEXT("Legacy join returns to the exact room"), Moving->GetCurrentRoomTag(), JoinLegacy.GetTag());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinOverlapScenarioTest, "Outlier.Room.GroupJoin.Scenarios.OverlappingVolumes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinOverlapScenarioTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	for (bool bSameGroup : {true, false})
	{
		for (bool bFullOverlap : {false, true})
		{
			for (bool bDestinationLast : {false, true})
			{
				AddInfo(FString::Printf(TEXT("Overlapping pair: SameGroup=%d Full=%d BLast=%d"),
					bSameGroup, bFullOverlap, bDestinationLast));
				FGroupJoinWorld Fixture(Class);
				ARoomVolume* B = Fixture.Rooms[JoinB];
				ARoomVolume* Other = Fixture.Rooms[bSameGroup ? JoinC.GetTag() : JoinOutside.GetTag()];
				Other->SetActorLocation(B->GetActorLocation() + FVector(bFullOverlap ? 0 : 250, 0, 0));
				Fixture.Place(Fixture.Shooter, JoinB, FVector(0, -150, 0));
				Fixture.Place(Fixture.Partner, JoinB, FVector(0, 150, 0));
				for (AFirstPersonCharacter* Player : {static_cast<AFirstPersonCharacter*>(Fixture.Shooter),
					static_cast<AFirstPersonCharacter*>(Fixture.Partner)})
				{
					TestTrue(TEXT("Player really overlaps both physical volumes"),
						B->IsOverlappingActor(Player) && Other->IsOverlappingActor(Player));
					// 실제로 겹친 두 Volume의 통지 순서만 반대로 바꾼다. 엔진 열거 순서에
					// 기대지 않고 마지막 입장이 현재 방을 결정하는 기존 동작을 드러낸다.
					Player->GetRoomTagComp()->EnterRoom(bDestinationLast ? Other : B);
					Player->GetRoomTagComp()->EnterRoom(bDestinationLast ? B : Other);
					TestEqual(TEXT("Last entry selects the current tag"), Player->GetCurrentRoomTag(),
						bDestinationLast ? JoinB.GetTag() : Other->GetRoomTag());
				}
				TestEqual(TEXT("Overlapping tags determine whether the pair belongs to the combat group"),
					Fixture.StartAndCompleteJoin(JoinA), bSameGroup || bDestinationLast);
				TestEqual(TEXT("An already-inside pair is never teleported to resolve overlap"), Fixture.TeleportCalls, 0);
				if (!bSameGroup && !bDestinationLast) CheckJoinReleased(*this, Fixture);
			}
		}

		for (bool bMoveShooter : {false, true})
		{
			AddInfo(FString::Printf(TEXT("Post-teleport overlap: SameGroup=%d Moving=%s"),
				bSameGroup, bMoveShooter ? TEXT("Shooter") : TEXT("Partner")));
			FGroupJoinWorld Fixture(Class);
			ARoomVolume* B = Fixture.Rooms[JoinB];
			ARoomVolume* Other = Fixture.Rooms[bSameGroup ? JoinC.GetTag() : JoinOutside.GetTag()];
			const FVector OriginalOtherLocation = Other->GetActorLocation();
			AFirstPersonCharacter* Anchor;
			AFirstPersonCharacter* Moving;
			Fixture.PlaceJoinPair(bMoveShooter, Anchor, Moving);
			// 이동 대상은 겹침 영역 밖에 둔다. 완전히 겹친 Other의 입장을 마지막으로
			// 통지해 이동 후 방 확인에서 실패하는 순서를 Shooter/Partner 모두 재현한다.
			Moving->SetActorLocation(FVector(-3000, 0, 250));
			Moving->GetRoomTagComp()->ClearRuntimeRoomAssignment();
			Other->SetActorLocation(B->GetActorLocation());
			Anchor->GetRoomTagComp()->EnterRoom(B);
			const FDelegateHandle Handle = Moving->GetRoomTagComp()->OnCurrentRoomTagChanged.AddLambda(
				[Moving, Other](FGameplayTag, FGameplayTag Current)
				{
					if (Current == JoinB && Other->ContainsWorldLocation(Moving->GetActorLocation()))
					{
						Moving->GetRoomTagComp()->EnterRoom(Other);
					}
				});
			const bool bStarted = Fixture.StartAndCompleteJoin(JoinA);
			Moving->GetRoomTagComp()->OnCurrentRoomTagChanged.Remove(Handle);
			TestFalse(TEXT("A different post-move tag cannot confirm the exact destination"), bStarted);
			TestEqual(TEXT("Failure occurs after one movement attempt"), Fixture.TeleportCalls, 1);
			TestTrue(TEXT("Player physically arrived inside B"), B->ContainsWorldLocation(Moving->GetActorLocation()));
			TestEqual(TEXT("Overlap assigned the other room tag"), Moving->GetCurrentRoomTag(), Other->GetRoomTag());
			CheckJoinReleased(*this, Fixture);
			Other->SetActorLocation(OriginalOtherLocation);
			Moving->GetCapsuleComponent()->UpdateOverlaps();
			B->RefreshOverlappingRoomAssignments();
			TestFalse(TEXT("Removing overlap does not bypass cooldown"), Fixture.StartAndCompleteJoin(JoinA));
			Fixture.AdvanceRetryInterval();
			CheckJoinReleased(*this, Fixture);
			TestTrue(TEXT("Fresh detection succeeds after overlap is removed"), Fixture.StartAndCompleteJoin(JoinA));
			TestEqual(TEXT("Recovery does not teleport the already-arrived pair again"), Fixture.TeleportCalls, 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinProtectionTest, "Outlier.Room.GroupJoin.Protection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinProtectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Fixture(Class);
	const FGameplayTag Immune = OutlierGameplayTags::State::DamageImmune();
	for (bool bMoveShooter : {false, true})
	{
		for (float Duration : {0.0f, 0.8f, 2.0f})
		{
			Fixture.Definition->ForcedJoinInvulnerabilitySeconds = Duration;
			AFirstPersonCharacter* Anchor;
			AFirstPersonCharacter* Moving;
			Fixture.PlaceJoinPair(bMoveShooter, Anchor, Moving);
			UOutlierAbilitySystemComponent* MovedASC = bMoveShooter
				? Fixture.Shooter->GetOutlierAbilitySystemComponent() : Fixture.Partner->GetOutlierAbilitySystemComponent();
			UOutlierAbilitySystemComponent* AnchorASC = bMoveShooter
				? Fixture.Partner->GetOutlierAbilitySystemComponent() : Fixture.Shooter->GetOutlierAbilitySystemComponent();
			bool bSawStart = false;
			Fixture.Combat->CombatEventObserverForTesting = [&](FGameplayTag, ERoomCombatEvent Event, int32)
			{
				if (Event != ERoomCombatEvent::SequenceStarted) return;
				bSawStart = true;
				TestEqual(TEXT("Protection is decided before combat starts"), MovedASC->HasMatchingGameplayTag(Immune), Duration > 0);
				TestFalse(TEXT("Anchor is never protected"), AnchorASC->HasMatchingGameplayTag(Immune));
			};
			TestTrue(TEXT("Join starts combat"), Fixture.StartAndCompleteJoin(JoinA));
			TestTrue(TEXT("Combat start was observed"), bSawStart);
			Fixture.Combat->CombatEventObserverForTesting = nullptr;
			TestEqual(TEXT("Exactly one teleport"), Fixture.TeleportCalls, 1);
			if (Duration > 0)
			{
				TestFalse(TEXT("Protection rejects gameplay damage"), MovedASC->ApplyDamageToSelf(
					1.0f, nullptr, Anchor, OutlierGameplayTags::Damage::Weapon()));
				Fixture.AdvanceFrames(4);
				TestTrue(TEXT("Protection remains before expiry"), MovedASC->HasMatchingGameplayTag(Immune));
				Fixture.Definition->ForcedJoinInvulnerabilitySeconds = 30.0f;
				TestTrue(TEXT("Repeated detection is accepted without another join"),
					Fixture.StartAndCompleteJoin(JoinA));
				Fixture.AdvanceFrames(FMath::CeilToInt(Duration * 10.0f) - 2);
				TestFalse(TEXT("Duplicate detection and edited settings do not extend protection"),
					MovedASC->HasMatchingGameplayTag(Immune));
				TestTrue(TEXT("Damage resumes after expiry"), MovedASC->ApplyDamageToSelf(
					1.0f, nullptr, Anchor, OutlierGameplayTags::Damage::Weapon()));
			}
			else
			{
				TestFalse(TEXT("Zero duration adds no immunity"), MovedASC->HasMatchingGameplayTag(Immune));
			}
			TestEqual(TEXT("Repeated detection never moves again"), Fixture.TeleportCalls, 1);
			Fixture.Reset();
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinProtectionLifecycleTest, "Outlier.Room.GroupJoin.ProtectionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinProtectionLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	const FGameplayTag Immune = OutlierGameplayTags::State::DamageImmune();
	// 각 정리 사유를 새 월드에서 검사해 사망/Pawn 교체가 다음 시나리오에 남지 않게 한다.
	for (int32 Scenario = 0; Scenario < 9; ++Scenario)
	{
		FGroupJoinWorld Fixture(Class);
		AFirstPersonCharacter* Anchor;
		AFirstPersonCharacter* Moving;
		Fixture.PlaceJoinPair(false, Anchor, Moving);
		UOutlierAbilitySystemComponent* ASC = Fixture.Partner->GetOutlierAbilitySystemComponent();
		const FActiveGameplayEffectHandle OtherImmunity = ASC->ApplyDamageImmuneStateToSelf();
		TestTrue(TEXT("Join with unrelated protection succeeds"), Fixture.StartAndCompleteJoin(JoinA));
		switch (Scenario)
		{
		case 0: Fixture.Combat->ResetRuntimeCombatState(); break;
		case 1: Fixture.Combat->UnregisterRoom(Fixture.Rooms[JoinA]); break;
		case 2: Fixture.Combat->UnregisterRoom(Fixture.Rooms[JoinB]); break;
		case 3: ASC->ApplyDeadStateToSelf(); break;
		case 4:
		{
			AOutlierPlayerState* State = CastChecked<AOutlierPlayerState>(Fixture.World->GetGameState()->PlayerArray.Last());
			State->SetPartnerCharacter(nullptr);
			break;
		}
		case 5: static_cast<APawn*>(Moving)->UnPossessed(); break;
		case 6:
			// 마지막 실제 적 사망으로 최초 발각 차수를 완료한다.
			Fixture.Combat->NotifyEnemyDefeated(Fixture.Enemies[0]);
			break;
		case 7:
			Fixture.AdvanceFrames(24);
			break;
		case 8: Moving->Destroy(); break;
		}
		TestNotNull(TEXT("Cleanup preserves the unrelated effect"), ASC->GetActiveGameplayEffect(OtherImmunity));
		ASC->RemoveActiveEffectFromSelf(OtherImmunity);
		TestFalse(TEXT("Only the unrelated immunity remained after cleanup"), ASC->HasMatchingGameplayTag(Immune));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupJoinProtectionReentryTest, "Outlier.Room.GroupJoin.ProtectionReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupJoinProtectionReentryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	const FGameplayTag Immune = OutlierGameplayTags::State::DamageImmune();
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		FGroupJoinWorld Fixture(Class);
		AFirstPersonCharacter* Anchor;
		AFirstPersonCharacter* Moving;
		Fixture.PlaceJoinPair(false, Anchor, Moving);
		UOutlierAbilitySystemComponent* ASC = Fixture.Partner->GetOutlierAbilitySystemComponent();
		FDelegateHandle Handle;
		if (Scenario < 2)
		{
			// 아직 효과 핸들을 돌려받기 전의 태그 알림에서도 시도가 무효화될 수 있다.
			Handle = ASC->RegisterGameplayTagEvent(Immune, EGameplayTagEventType::NewOrRemoved)
				.AddLambda([&Fixture, Scenario](FGameplayTag, int32 Count)
				{
					if (Count == 0) return;
					if (Scenario == 0) Fixture.Combat->ResetRuntimeCombatState();
					else CastChecked<AOutlierPlayerState>(Fixture.World->GetGameState()->PlayerArray.Last())
						->SetPartnerCharacter(nullptr);
				});
		}
		else
		{
			Fixture.Combat->CombatEventObserverForTesting = [&Fixture](FGameplayTag, ERoomCombatEvent Event, int32)
			{
				if (Event == ERoomCombatEvent::SequenceStarted) Fixture.Combat->ResetRuntimeCombatState();
			};
		}
		TestFalse(TEXT("Invalidated protection cannot commit combat"),
			Fixture.StartAndCompleteJoin(JoinA));
		ASC->RegisterGameplayTagEvent(Immune, EGameplayTagEventType::NewOrRemoved).Remove(Handle);
		Fixture.Combat->CombatEventObserverForTesting = nullptr;
		TestFalse(TEXT("Interrupted application leaves no immunity"), ASC->HasMatchingGameplayTag(Immune));
		TestFalse(TEXT("Interrupted application leaves no barrier"), Fixture.Combat->IsExitBlocked(JoinA));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomBarrierSafetyPairTest, "Outlier.Room.BarrierSafety.PairSeparationAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomBarrierSafetyPairTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		FGroupJoinWorld Fixture(Class);
		Fixture.World->SpawnActor<APlayerController>()->Possess(Fixture.Shooter);
		Fixture.World->SpawnActor<APlayerController>()->Possess(Fixture.Partner);
		Fixture.Place(Fixture.Shooter, JoinA, FVector(-450, -150, -50));
		Fixture.Place(Fixture.Partner, JoinA, FVector(-450, 150, -50));
		ARoomCombatBarrier* Barrier = Fixture.Barriers.FindChecked(JoinA);
		Barrier->SetActorLocation(FVector(-450, 0, 200));
		CastChecked<UBoxComponent>(Barrier->GetRootComponent())->SetBoxExtent(FVector(25, 400, 300));
		const FVector ShooterBefore = Fixture.Shooter->GetActorLocation();
		const FVector PartnerBefore = Fixture.Partner->GetActorLocation();
		if (Scenario == 2) Fixture.Floors.FindChecked(JoinA)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		bool bCompleted = false;
		bool bSafe = false;
		int32 ReadyNotifications = 0;
		const FDelegateHandle ReadyHandle = Fixture.Combat->OnRoomStartReadinessChanged.AddLambda(
			[&ReadyNotifications](FGameplayTag Tag) { if (Tag == JoinA) ++ReadyNotifications; });
		TestFalse(TEXT("Occupied barrier starts asynchronous preparation"), Fixture.Combat->PrepareBarrierClosure(JoinA,
			[] { return true; }, [&bCompleted, &bSafe](bool bResult) { bCompleted = true; bSafe = bResult; }));
		TestFalse(TEXT("Collision stays disabled while candidates are pending"), Barrier->IsBlocked());
		if (Scenario == 1) Fixture.Combat->ResetRuntimeCombatState();
		Fixture.CompleteQueries();
		TestTrue(TEXT("Preparation reaches a bounded result"), bCompleted);
		TestEqual(TEXT("Only the valid grounded pair succeeds"), bSafe, Scenario == 0);
		if (Scenario == 0)
		{
			for (const AFirstPersonCharacter* Player : {
				static_cast<AFirstPersonCharacter*>(Fixture.Shooter), static_cast<AFirstPersonCharacter*>(Fixture.Partner)})
			{
				const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
				TestFalse(TEXT("Both final capsules clear the future barrier"), Barrier->OverlapsJoinCapsule(
					Player->GetActorLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()));
				TestTrue(TEXT("Both final capsules fit inside the intended room"), ContainsJoinCapsule(Fixture.Rooms.FindChecked(JoinA), Player));
			}
			TestEqual(TEXT("Successful asynchronous preparation wakes waiting trigger callers once"), ReadyNotifications, 1);
		}
		else
		{
			TestTrue(TEXT("Cancelled or groundless preparation leaves Shooter in place"), Fixture.Shooter->GetActorLocation().Equals(ShooterBefore));
			TestTrue(TEXT("Cancelled or groundless preparation leaves Partner in place"), Fixture.Partner->GetActorLocation().Equals(PartnerBefore));
			TestEqual(TEXT("Failure cannot wake a successful trigger retry"), ReadyNotifications, 0);
		}
		Fixture.Combat->OnRoomStartReadinessChanged.Remove(ReadyHandle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomBarrierSafetyLateRegistrationTest, "Outlier.Room.BarrierSafety.ConsecutiveLateRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomBarrierSafetyLateRegistrationTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadJoinShooter();
	if (!TestNotNull(TEXT("Concrete Shooter"), Class)) return false;
	FGroupJoinWorld Fixture(Class);
	Fixture.World->SpawnActor<APlayerController>()->Possess(Fixture.Shooter);
	Fixture.World->SpawnActor<APlayerController>()->Possess(Fixture.Partner);
	Fixture.Place(Fixture.Shooter, JoinA, FVector(-450, -150, -50));
	Fixture.Place(Fixture.Partner, JoinA, FVector(-450, 150, -50));
	FRoomCombatPreparationContext Preparation;
	TestTrue(TEXT("Existing combat prepares before late barriers load"),
		Fixture.Combat->BeginInitialDetectionPreparation(JoinA, Fixture.Shooter, Preparation));
	TestTrue(TEXT("Existing combat starts with clear registered exits"),
		Fixture.Combat->CompleteInitialDetectionPreparation(Preparation));
	auto AddLateBarrier = [&Fixture](float Y)
	{
		const FTransform Transform(FVector(-450, Y, 200));
		ARoomCombatBarrier* Barrier = Fixture.World->SpawnActorDeferred<ARoomCombatBarrier>(ARoomCombatBarrier::StaticClass(), Transform);
		*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Barrier) = JoinA;
		CastChecked<UBoxComponent>(Barrier->GetRootComponent())->SetBoxExtent(FVector(25, 90, 300));
		Barrier->FinishSpawning(Transform);
		if (!Barrier->HasActorBegunPlay()) Barrier->DispatchBeginPlay();
		return Barrier;
	};
	ARoomCombatBarrier* First = AddLateBarrier(-150);
	TestFalse(TEXT("First late barrier waits for safe separation"), First->IsBlocked());
	// 두 번째 등록은 첫 쿼리를 취소하고 변경된 차단막 집합으로 다시 준비한다.
	ARoomCombatBarrier* Second = AddLateBarrier(150);
	TestFalse(TEXT("Second late barrier also stays open while pending"), Second->IsBlocked());
	// 무관한 비전투 그룹의 WP 등록은 이 방의 준비를 취소해서는 안 된다.
	ARoomCombatBarrier* Unrelated = Fixture.World->SpawnActorDeferred<ARoomCombatBarrier>(
		ARoomCombatBarrier::StaticClass(), FTransform(FVector(9000, 0, 200)));
	*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("RoomTag"))
		->ContainerPtrToValuePtr<FGameplayTag>(Unrelated) = JoinOutside;
	Unrelated->FinishSpawning(FTransform(FVector(9000, 0, 200)));
	if (!Unrelated->HasActorBegunPlay()) Unrelated->DispatchBeginPlay();
	Fixture.CompleteQueries();
	TestEqual(TEXT("Late safety never stops the existing combat"), Fixture.Combat->GetRoomState(JoinA), ERoomCombatState::Combat);
	TestTrue(TEXT("Successful shared preparation closes the first deferred barrier too"), First->IsBlocked());
	TestTrue(TEXT("Successful shared preparation closes the latest barrier"), Second->IsBlocked());
	TestFalse(TEXT("Unrelated inactive group remains open"), Unrelated->IsBlocked());
	for (ARoomCombatBarrier* Barrier : {First, Second})
	{
		for (const AFirstPersonCharacter* Player : {
			static_cast<AFirstPersonCharacter*>(Fixture.Shooter), static_cast<AFirstPersonCharacter*>(Fixture.Partner)})
		{
			const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
			TestFalse(TEXT("Neither closed late barrier overlaps either player"), Barrier->OverlapsJoinCapsule(
				Player->GetActorLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()));
		}
	}
	// 관련 차단막의 언로드도 준비 집합을 바꾼다. 기존 전투는 유지한 채
	// 남아 있는 출입구가 변경된 집합으로 다시 안전 분리/닫힘을 마쳐야 한다.
	Fixture.Place(Fixture.Shooter, JoinA, FVector(-450, -150, -50));
	Fixture.Place(Fixture.Partner, JoinA, FVector(-450, 150, -50));
	First->RefreshFromRoomCombat();
	TestFalse(TEXT("Reentered active barrier opens while preparing again"), First->IsBlocked());
	Second->Destroy();
	Fixture.CompleteQueries();
	TestEqual(TEXT("Related unload preserves the existing combat"), Fixture.Combat->GetRoomState(JoinA), ERoomCombatState::Combat);
	TestTrue(TEXT("Related unload resumes safety for the remaining barrier"), First->IsBlocked());
	const UCapsuleComponent* Capsule = Fixture.Shooter->GetCapsuleComponent();
	TestFalse(TEXT("Remaining closed barrier clears the reentered Shooter"), First->OverlapsJoinCapsule(
		Fixture.Shooter->GetActorLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()));
	return true;
}

#endif
