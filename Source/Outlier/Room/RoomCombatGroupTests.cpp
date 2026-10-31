#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "UObject/UnrealType.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RoomA, "Room.GroupRuntime01.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RoomB, "Room.GroupRuntime01.1.2");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RoomC, "Room.GroupRuntime01.1.3");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(OtherGroup, "Room.GroupRuntime01.2.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PrefixGroup, "Room.GroupRuntime01.10.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(OtherLevel, "Room.GroupRuntime02.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HackGroup, "RoomGroupTest.Activation");

	struct FGroupCombatWorld
	{
		UWorld* World;
		UGameInstance* GameInstance;
		URoomCombatSubsystem* Combat;
		URoomCombatDefinition* Definition;

		FGroupCombatWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RoomGroupTest")));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			GameInstance = NewObject<UGameInstance>(GEngine);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
			Combat = World->GetSubsystem<URoomCombatSubsystem>();
			Definition = NewObject<URoomCombatDefinition>(World);
			Combat->SetCombatDefinitionForTesting(Definition);
		}

		~FGroupCombatWorld()
		{
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}

		ARoomVolume* AddRoom(FGameplayTag Tag, const TArray<FRoomCombatPhaseDefinition>& Phases)
		{
			FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
			Entry.RoomTag = Tag;
			Entry.CombatPhases = Phases;
			ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
			FStructProperty* Property = FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"));
			*Property->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
			Combat->RegisterRoom(Room, Tag);
			return Room;
		}

		AEnemyBase* AddEnemy(FGameplayTag Tag)
		{
			AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>();
			Enemy->SetActorLocation(FVector(-10000, 0, 0));
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
			Combat->RegisterPreplacedEnemy(Enemy);
			return Enemy;
		}

		ARoomCombatSpawnPoint* AddPoint(FGameplayTag Tag, float X)
		{
			ARoomCombatSpawnPoint* Point = World->SpawnActor<ARoomCombatSpawnPoint>(
				ARoomCombatSpawnPoint::StaticClass(), FTransform(FVector(X, 0, 0)));
			Combat->RegisterSpawnPoint(Point, Tag, FGameplayTagContainer(), FGameplayTag());
			return Point;
		}

		AEnemyBase* FindSpawnedEnemy(FGameplayTag Tag) const
		{
			for (TActorIterator<AEnemyBase> It(World); It; ++It)
			{
				if (It->IsPoolManaged() && It->GetEnemyPoolState() != EEnemyPoolState::Idle
					&& It->GetRoomTagComp()->GetDefaultRoomTag() == Tag) return *It;
			}
			return nullptr;
		}
	};

	FRoomCombatPhaseDefinition MakePhase(ERoomCombatPhaseStartPolicy Policy)
	{
		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = Policy;
		Phase.DelayBeforeStartSeconds = 0.0f;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomCombatGroupProgressTest,
	"Outlier.Room.Group.IndependentProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatGroupProgressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupCombatWorld TestWorld;
	URoomCombatSubsystem* Combat = TestWorld.Combat;
	UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(TestWorld.World);
	FEnemyPoolEntry& PoolEntry = PoolDefinition->Entries.AddDefaulted_GetRef();
	PoolEntry.EnemyClass = AEnemyBase::StaticClass();
	PoolEntry.PrewarmCount = 8;
	PoolEntry.MaxCount = 8;
	if (!TestTrue(TEXT("Pool prewarms"), TestWorld.World->GetSubsystem<UEnemyPoolSubsystem>()->PrewarmPool(PoolDefinition))) return false;

	FRoomCombatPhaseDefinition Detection = MakePhase(ERoomCombatPhaseStartPolicy::InitialDetection);
	Detection.Waves.Add(MakePhase(ERoomCombatPhaseStartPolicy::Automatic).Waves[0]);
	TestWorld.AddRoom(RoomA, {Detection});
	TestWorld.AddRoom(RoomB, {MakePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger), MakePhase(ERoomCombatPhaseStartPolicy::Automatic)});
	TestWorld.AddRoom(RoomC, {MakePhase(ERoomCombatPhaseStartPolicy::HackTrigger)});
	TestWorld.AddRoom(OtherGroup, {MakePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	TestWorld.AddRoom(PrefixGroup, {MakePhase(ERoomCombatPhaseStartPolicy::InitialDetection)});
	TestWorld.AddRoom(OtherLevel, {MakePhase(ERoomCombatPhaseStartPolicy::InitialDetection)});
	AEnemyBase* EnemyA = TestWorld.AddEnemy(RoomA);
	TestWorld.AddEnemy(PrefixGroup);
	TestWorld.AddEnemy(OtherLevel);
	ARoomCombatSpawnPoint* PointA = TestWorld.AddPoint(RoomA, 5000);
	ARoomCombatSpawnPoint* PointB = TestWorld.AddPoint(RoomB, 10000);
	PointA->SetForceSpawnLocationFailureForTesting(true);
	PointB->SetForceSpawnLocationFailureForTesting(true);
	AActor* Requester = TestWorld.World->SpawnActor<AActor>();
	FRoomCombatPreparationContext Preparation;
	TestTrue(TEXT("A prepares"), Combat->BeginInitialDetectionPreparation(RoomA, Requester, Preparation));
	TestTrue(TEXT("A confirms combat"), Combat->CompleteInitialDetectionPreparation(Preparation));
	TestEqual(TEXT("Exact parent owns the active group"), Combat->GetActiveCombatGroupTag(), RoomA.GetTag().RequestDirectParent());
	Combat->NotifyEnemyDefeated(EnemyA);
	TestEqual(TEXT("A reinforcement remains pending"), Combat->GetPendingSpawnCount(RoomA), 1);
	FRoomCombatTriggerContext TriggerB, Outside;
	TestTrue(TEXT("Sibling B can create an external context"), Combat->CreateTriggerContext(Requester, RoomB, FGameplayTag(), TriggerB));
	TestTrue(TEXT("B starts while A has pending work"), Combat->StartTriggeredSequence(Requester, TriggerB));
	TestEqual(TEXT("B has its own pending request"), Combat->GetPendingSpawnCount(RoomB), 1);
	TestFalse(TEXT("Other group external trigger is blocked"), Combat->CreateTriggerContext(Requester, OtherGroup, FGameplayTag(), Outside));
	TestFalse(TEXT("Group 10 cannot detect-start in group 1"), Combat->NotifyRoomCombatStarted(PrefixGroup));
	TestFalse(TEXT("Another level cannot detect-start"), Combat->NotifyRoomCombatStarted(OtherLevel));
	TestFalse(TEXT("Untriggered C is not a participant"), Combat->IsRoomCombatReserved(RoomC));
	FRoomCombatTriggerContext Hack;
	TestFalse(TEXT("Sibling reservation does not bypass the hack activation condition"),
		Combat->CreateTriggerContext(Requester, RoomC, FGameplayTag(), Hack));
	TestTrue(TEXT("An eligible sibling hack context can be created"),
		Combat->CreateTriggerContext(Requester, RoomC, HackGroup, Hack));
	TestFalse(TEXT("Creating a context alone does not reserve C"), Combat->IsRoomCombatReserved(RoomC));

	// B만 위치 실패를 해제한다. A의 Pending이 B의 기준값 확정/차수 전환을 막으면 안 된다.
	PointB->SetForceSpawnLocationFailureForTesting(false);
	Combat->RetryPendingSpawnsForTesting();
	TestEqual(TEXT("A remains pending"), Combat->GetPendingSpawnCount(RoomA), 1);
	TestEqual(TEXT("B finishes its pending spawn"), Combat->GetPendingSpawnCount(RoomB), 0);
	TestEqual(TEXT("B finalizes its own baseline"), Combat->GetWaveBaselineEnemyCount(RoomB), 1);
	TArray<ARoomCombatSpawnPoint*> Eligible;
	Combat->GetEligibleSpawnPoints(RoomB, FGameplayTag(), Eligible);
	TestTrue(TEXT("B only uses B spawn points"), Eligible.Num() == 1 && Eligible[0] == PointB);
	AEnemyBase* SpawnedB = TestWorld.FindSpawnedEnemy(RoomB);
	if (!TestNotNull(TEXT("B leased its own enemy"), SpawnedB)) return false;
	PointB->SetForceSpawnLocationFailureForTesting(true);
	Combat->NotifyEnemyDefeated(SpawnedB);
	SpawnedB->Destroy();
	TestEqual(TEXT("B advances phase without A completing"), Combat->GetCurrentCombatPhaseIndex(RoomB), 1);
	TestEqual(TEXT("A phase stays unchanged"), Combat->GetCurrentCombatPhaseIndex(RoomA), 0);
	PointB->SetForceSpawnLocationFailureForTesting(false);
	Combat->RetryPendingSpawnsForTesting();
	SpawnedB = TestWorld.FindSpawnedEnemy(RoomB);
	if (!TestNotNull(TEXT("B automatic phase spawned"), SpawnedB)) return false;
	Combat->NotifyEnemyDefeated(SpawnedB);
	TestEqual(TEXT("B clears independently"), Combat->GetRoomState(RoomB), ERoomCombatState::Cleared);
	TestTrue(TEXT("A still owns the group"), Combat->IsRoomCombatReserved(RoomA));
	TestFalse(TEXT("Other group remains blocked after B clears"), Combat->CreateTriggerContext(Requester, OtherGroup, FGameplayTag(), Outside));
	PointA->SetForceSpawnLocationFailureForTesting(false);
	Combat->RetryPendingSpawnsForTesting();
	TestEqual(TEXT("A retry continues after B clears"), Combat->GetPendingSpawnCount(RoomA), 0);
	AEnemyBase* SpawnedA = TestWorld.FindSpawnedEnemy(RoomA);
	if (!TestNotNull(TEXT("A reinforcement spawned"), SpawnedA)) return false;
	Combat->NotifyEnemyDefeated(SpawnedA);
	TestFalse(TEXT("All participants complete releases the group"), Combat->HasActiveCombatReservation());
	TestEqual(TEXT("Unparticipating C still waits for its event"), Combat->GetRoomState(RoomC), ERoomCombatState::WaitingForTrigger);
	TestTrue(TEXT("Other group is allowed after all participants complete"), Combat->CreateTriggerContext(Requester, OtherGroup, FGameplayTag(), Outside));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomCombatGroupReservationTest,
	"Outlier.Room.Group.ReservationLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatGroupReservationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupCombatWorld TestWorld;
	URoomCombatSubsystem* Combat = TestWorld.Combat;
	const FRoomCombatPhaseDefinition Detection = MakePhase(ERoomCombatPhaseStartPolicy::InitialDetection);
	const FRoomCombatPhaseDefinition External = MakePhase(ERoomCombatPhaseStartPolicy::ExternalTrigger);
	TestWorld.AddRoom(RoomA, {Detection, External});
	ARoomVolume* VolumeB = TestWorld.AddRoom(RoomB, {Detection});
	TestWorld.AddRoom(OtherGroup, {External});
	const FGameplayTag Legacy = RoomA.GetTag().RequestDirectParent();
	TestWorld.AddRoom(Legacy, {Detection});
	AEnemyBase* EnemyA = TestWorld.AddEnemy(RoomA);
	TestWorld.AddEnemy(RoomB);
	TestWorld.AddEnemy(Legacy);
	AActor* Anchor = TestWorld.World->SpawnActor<AActor>();
	FRoomCombatPreparationContext A, B;
	TestTrue(TEXT("A reserves the group"), Combat->BeginInitialDetectionPreparation(RoomA, Anchor, A));
	TestTrue(TEXT("A starts"), Combat->CompleteInitialDetectionPreparation(A));
	TestTrue(TEXT("B can prepare beside A"), Combat->BeginInitialDetectionPreparation(RoomB, Anchor, B));
	TestTrue(TEXT("B can fail without cancelling A"), Combat->CancelInitialDetectionPreparation(B));
	TestTrue(TEXT("A reservation survives sibling failure"), Combat->IsRoomCombatReserved(RoomA));
	TestFalse(TEXT("B releases only its own reservation"), Combat->IsRoomCombatReserved(RoomB));
	TestFalse(TEXT("Legacy parent name is not treated as a new sibling"), Combat->NotifyRoomCombatStarted(Legacy));
	Combat->NotifyEnemyDefeated(EnemyA);
	TestEqual(TEXT("A waits for its remaining external phase"), Combat->GetRoomState(RoomA), ERoomCombatState::WaitingForTrigger);
	TestTrue(TEXT("Waiting participant keeps the group"), Combat->IsRoomCombatReserved(RoomA));
	FRoomCombatTriggerContext Outside, Next;
	TestFalse(TEXT("Waiting participant blocks another group"), Combat->CreateTriggerContext(Anchor, OtherGroup, FGameplayTag(), Outside));
	TestTrue(TEXT("The same participant can trigger its next phase"), Combat->CreateTriggerContext(Anchor, RoomA, FGameplayTag(), Next));
	Combat->UnregisterRoom(VolumeB);
	TestFalse(TEXT("Old B completion remains invalid"), Combat->CompleteInitialDetectionPreparation(B));
	// 재등록한 B를 실제 전투까지 진행한 뒤 언로드해도 A의 대기 점유는 유지한다.
	Combat->RegisterRoom(VolumeB, RoomB);
	FRoomCombatPreparationContext ReloadedB;
	TestTrue(TEXT("Reloaded B prepares beside waiting A"), Combat->BeginInitialDetectionPreparation(RoomB, Anchor, ReloadedB));
	TestTrue(TEXT("Reloaded B enters combat"), Combat->CompleteInitialDetectionPreparation(ReloadedB));
	Combat->UnregisterRoom(VolumeB);
	TestTrue(TEXT("Unloading B preserves waiting A"), Combat->IsRoomCombatReserved(RoomA));
	TestFalse(TEXT("Unloading B releases B only"), Combat->IsRoomCombatReserved(RoomB));
	Combat->ResetRuntimeCombatState();
	TestFalse(TEXT("Reset releases every reservation"), Combat->HasActiveCombatReservation());
	TestFalse(TEXT("Reset invalidates the group key"), Combat->GetActiveCombatGroupTag().IsValid());
	TestFalse(TEXT("Old A context cannot finish after Reset"), Combat->CompleteInitialDetectionPreparation(A));
	return true;
}

#endif
