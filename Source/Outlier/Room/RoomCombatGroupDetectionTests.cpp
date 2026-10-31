#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "OutlierArenaSettings.h"
#include "OutlierPlayerState.h"
#include "Perception/AISense_Sight.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PursuitA, "Room.GroupPursuit01.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PursuitB, "Room.GroupPursuit01.1.2");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PursuitC, "Room.GroupPursuit01.1.3");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PursuitOutside, "Room.GroupPursuit01.2.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PursuitPrefix, "Room.GroupPursuit01.10.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PursuitLevel, "Room.GroupPursuit02.1.1");

	struct FPursuitWorld
	{
		UWorld* World;
		UGameInstance* GameInstance;
		URoomCombatSubsystem* Combat;
		UEnemyRoomSubsystem* EnemyRooms;
		URoomCombatDefinition* Definition;
		AShooterCharacter* Shooter;
		APartnerCharacter* Partner;
		AEnemyBase* Pursuer;
		TMap<FGameplayTag, ARoomVolume*> Rooms;
		TMap<FGameplayTag, AEnemyBase*> Enemies;
		TSoftObjectPtr<UWorld> SavedArena;

		FPursuitWorld(ERoomCombatPhaseStartPolicy DestinationPolicy = ERoomCombatPhaseStartPolicy::InitialDetection,
			bool bDestinationEnemy = true)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RoomPursuitTest")));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			GameInstance = NewObject<UGameInstance>(GEngine);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
			// 실제 AI 전파 경로의 Arena 로드 검사를 사용한다. 설정은 테스트 수명 안에서만 바꾼다.
			UOutlierArenaSettings* Settings = GetMutableDefault<UOutlierArenaSettings>();
			SavedArena = Settings->ArenaLevel;
			Settings->ArenaLevel = World;
			Combat = World->GetSubsystem<URoomCombatSubsystem>();
			EnemyRooms = World->GetSubsystem<UEnemyRoomSubsystem>();
			Definition = NewObject<URoomCombatDefinition>(World);
			Combat->SetCombatDefinitionForTesting(Definition);
			int32 Index = 0;
			for (FGameplayTag Tag : {PursuitA.GetTag(), PursuitB.GetTag(), PursuitC.GetTag(),
				PursuitOutside.GetTag(), PursuitPrefix.GetTag(), PursuitLevel.GetTag()})
			{
				FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
				Entry.RoomTag = Tag;
				FRoomCombatPhaseDefinition& Phase = Entry.CombatPhases.AddDefaulted_GetRef();
				Phase.StartPolicy = Tag == PursuitB ? DestinationPolicy : ERoomCombatPhaseStartPolicy::InitialDetection;
				Phase.DelayBeforeStartSeconds = 0;
				Phase.Waves.AddDefaulted();
				ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
				*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
					->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
				Room->SetActorLocation(FVector(Index++ * 3000, 0, 250));
				CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(500));
				Rooms.Add(Tag, Room);
				Combat->RegisterRoom(Room, Tag);
				if (Tag == PursuitB && !bDestinationEnemy) continue;
				AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>();
				Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
				Enemy->SetActorLocation(Room->GetActorLocation());
				// 기본 C++ Enemy에도 실제 BeginPlay/사망 이벤트를 사용하도록 유효한 스탯을 준다.
				UDataTable* Stats = NewObject<UDataTable>(Enemy);
				Stats->RowStruct = FEnemyStat::StaticStruct();
				FEnemyStat Stat;
				Stat.Health = 100;
				Stats->AddRow(TEXT("Pursuit"), Stat);
				FDataTableRowHandle* Row = FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("EnemyStatRow"))
					->ContainerPtrToValuePtr<FDataTableRowHandle>(Enemy);
				Row->DataTable = Stats;
				Row->RowName = TEXT("Pursuit");
				Enemies.Add(Tag, Enemy);
			}
			Pursuer = Enemies[PursuitA];
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
			Place(Shooter, PursuitB, FVector(-150, 0, 0));
			Place(Partner, PursuitB, FVector(150, 0, 0));
		}

		~FPursuitWorld()
		{
			Combat->CombatEventObserverForTesting = nullptr;
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

		void StartSource()
		{
			Pursuer->EnterAlertFromPerception(Shooter->GetActorLocation());
			Pursuer->CommitAlertToCombat();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupPursuitDetectionTest, "Outlier.Room.GroupDetection.DetectionAndArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupPursuitDetectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPursuitWorld Test;
	AEnemyAIController* Controller = Test.World->SpawnActor<AEnemyAIController>();
	Controller->Possess(Test.Pursuer);
	TestTrue(TEXT("Sibling player remains a valid sight target"), Controller->IsValidDetectionTarget(Test.Shooter));
	const FAIStimulus Seen(*GetDefault<UAISense_Sight>(), 1.0f,
		Test.Shooter->GetActorLocation(), Test.Pursuer->GetActorLocation());
	Controller->HandleSightStimulus(Test.Pursuer, Test.Shooter, Seen);
	TestEqual(TEXT("Sight follows the existing Alert confirmation"), Test.Pursuer->GetCombatState(), EEnemyCombatState::Alert);
	TestTrue(TEXT("Alert commits with both players in sibling B"), Test.Pursuer->CommitAlertToCombat());
	TestEqual(TEXT("Source A starts"), Test.Combat->GetRoomState(PursuitA), ERoomCombatState::Combat);
	TestEqual(TEXT("B is dormant before actual arrival"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::Dormant);
	TestFalse(TEXT("Tag event outside the volume cannot start B"), Test.Combat->TryStartCombatFromPursuit(Test.Pursuer, PursuitB));
	Test.Pursuer->GetRoomTagComp()->EnterRoom(Test.Rooms[PursuitB]);
	TestEqual(TEXT("A stale B tag without physical arrival cannot spread"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::Dormant);
	Test.Pursuer->GetRoomTagComp()->LeaveRoom(Test.Rooms[PursuitB]);
	const float Radius = Test.Pursuer->GetCapsuleComponent()->GetScaledCapsuleRadius();
	Test.Place(Test.Pursuer, PursuitB, FVector(-500 - Radius * 0.5f, 0, 0));
	TestFalse(TEXT("Capsule can enter before its center"), Test.Rooms[PursuitB]->ContainsWorldLocation(Test.Pursuer->GetActorLocation()));
	TestTrue(TEXT("The entering capsule really overlaps B"), Test.Rooms[PursuitB]->IsOverlappingActor(Test.Pursuer));
	TestEqual(TEXT("Actual overlap starts B"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::Combat);
	TestTrue(TEXT("B participates in the same group"), Test.Combat->IsRoomCombatReserved(PursuitB));
	TestTrue(TEXT("B AI combat is synchronized"), Test.EnemyRooms->IsRoomInCombat(PursuitB));
	TestEqual(TEXT("B preplaced enemy joins combat"), Test.Enemies[PursuitB]->GetCombatState(), EEnemyCombatState::Combat);
	TestEqual(TEXT("Unvisited C stays dormant"), Test.Combat->GetRoomState(PursuitC), ERoomCombatState::Dormant);
	TestFalse(TEXT("Duplicate arrival does not start another sequence"), Test.Combat->TryStartCombatFromPursuit(Test.Pursuer, PursuitB));
	TestEqual(TEXT("Pursuer keeps source ownership"), Test.Pursuer->GetDefaultRoomTag(), PursuitA.GetTag());
	TestEqual(TEXT("B counts only its own enemy"), Test.Combat->GetAliveEnemyCount(PursuitB), 1);
	Test.Combat->NotifyEnemyDefeated(Test.Pursuer);
	TestEqual(TEXT("Death in B removes A's enemy"), Test.Combat->GetAliveEnemyCount(PursuitA), 0);
	TestEqual(TEXT("Death in B never decrements B"), Test.Combat->GetAliveEnemyCount(PursuitB), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupPursuitPolicyTest, "Outlier.Room.GroupDetection.StartPolicies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupPursuitPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (ERoomCombatPhaseStartPolicy Policy : {ERoomCombatPhaseStartPolicy::ExternalTrigger,
		ERoomCombatPhaseStartPolicy::HackTrigger, ERoomCombatPhaseStartPolicy::InitialDetection})
	{
		FPursuitWorld Test(Policy, Policy != ERoomCombatPhaseStartPolicy::InitialDetection);
		const ERoomCombatState Before = Test.Combat->GetRoomState(PursuitB);
		Test.StartSource();
		TestEqual(TEXT("Source is combat before pursuit"), Test.Pursuer->GetCombatState(), EEnemyCombatState::Combat);
		Test.Place(Test.Pursuer, PursuitB);
		TestEqual(TEXT("Event waiting or empty preplaced room is unchanged"), Test.Combat->GetRoomState(PursuitB), Before);
		TestFalse(TEXT("Ineligible B is not reserved"), Test.Combat->IsRoomCombatReserved(PursuitB));
		TestFalse(TEXT("Ineligible B AI does not enter combat"), Test.EnemyRooms->IsRoomInCombat(PursuitB));
		TestTrue(TEXT("Failure preserves A reservation"), Test.Combat->IsRoomCombatReserved(PursuitA));
	}
	// 배치 전투를 끝낸 같은 Room도 후속 문/해킹 차수는 별도 이벤트를 기다려야 한다.
	for (ERoomCombatPhaseStartPolicy Policy : {ERoomCombatPhaseStartPolicy::ExternalTrigger,
		ERoomCombatPhaseStartPolicy::HackTrigger})
	{
		FPursuitWorld Test;
		FRoomCombatPhaseDefinition& Next = Test.Definition->RoomDefinitions[1].CombatPhases.AddDefaulted_GetRef();
		Next.StartPolicy = Policy;
		Next.DelayBeforeStartSeconds = 0;
		Next.Waves.AddDefaulted();
		AEnemyBase* DestinationEnemy = Test.Enemies[PursuitB];
		DestinationEnemy->EnterAlertFromPerception(Test.Shooter->GetActorLocation());
		TestTrue(TEXT("B can initially start its own preplaced combat"), DestinationEnemy->CommitAlertToCombat());
		Test.Combat->NotifyEnemyDefeated(DestinationEnemy);
		TestEqual(TEXT("B advances to its event phase"), Test.Combat->GetCurrentCombatPhaseIndex(PursuitB), 1);
		TestEqual(TEXT("B waits for the next trigger"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::WaitingForTrigger);
		Test.StartSource();
		Test.Place(Test.Pursuer, PursuitB);
		TestEqual(TEXT("Arrival cannot bypass a later event phase"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::WaitingForTrigger);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGroupPursuitLifetimeTest, "Outlier.Room.GroupDetection.EligibilityAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGroupPursuitLifetimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	{
		FPursuitWorld Test;
		Test.Place(Test.Pursuer, PursuitB);
		TestEqual(TEXT("Noncombat arrival does not spread"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::Dormant);
		Test.Place(Test.Pursuer, PursuitA);
		Test.StartSource();
		Test.Pursuer->SetEnemyPossessed(true);
		Test.Place(Test.Pursuer, PursuitB);
		TestEqual(TEXT("Player-controlled enemy cannot spread AI combat"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::Dormant);
		Test.Pursuer->SetEnemyPossessed(false);
		Test.Place(Test.Pursuer, PursuitA);
		for (FGameplayTag Tag : {PursuitOutside.GetTag(), PursuitPrefix.GetTag(), PursuitLevel.GetTag()})
		{
			Test.Place(Test.Pursuer, Tag);
			TestEqual(TEXT("Other group/1 vs 10/level cannot spread"), Test.Combat->GetRoomState(Tag), ERoomCombatState::Dormant);
		}
		Test.Pursuer->GetAbilitySystemComponent()->AddLooseGameplayTag(OutlierGameplayTags::State::Dead());
		Test.Place(Test.Pursuer, PursuitB);
		TestEqual(TEXT("Dead enemy cannot spread"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::Dormant);
	}
	for (int32 Invalidation = 0; Invalidation < 3; ++Invalidation)
	{
		FPursuitWorld Test;
		Test.StartSource();
		bool bInvalidated = false;
		Test.Combat->CombatEventObserverForTesting = [&](FGameplayTag Tag, ERoomCombatEvent Event, int32)
		{
			if (Tag == PursuitB && Event == ERoomCombatEvent::SequenceStarted)
			{
				bInvalidated = true;
				if (Invalidation == 2)
				{
					Test.Combat->ResetRuntimeCombatState();
				}
				else
				{
					const FGameplayTag InvalidatedTag = Invalidation == 0 ? PursuitA.GetTag() : PursuitB.GetTag();
					Test.Combat->UnregisterRoom(Test.Rooms[InvalidatedTag]);
					Test.Combat->RegisterRoom(Test.Rooms[InvalidatedTag], InvalidatedTag);
				}
			}
		};
		Test.Place(Test.Pursuer, PursuitB);
		TestTrue(TEXT("Start callback really invalidates source registration"), bInvalidated);
		TestEqual(TEXT("Stale pursuit cannot commit B"), Test.Combat->GetRoomState(PursuitB), ERoomCombatState::Dormant);
		TestFalse(TEXT("Failed attempt releases only B"), Test.Combat->IsRoomCombatReserved(PursuitB));
		TestFalse(TEXT("Failed attempt restores B barrier state"), Test.Combat->IsExitBlocked(PursuitB));
		TestFalse(TEXT("Stale pursuit never propagates B AI combat"), Test.EnemyRooms->IsRoomInCombat(PursuitB));
	}
	return true;
}

#endif
