#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"

namespace
{
	URoomCombatDefinition* MakeSingleWaveDefinition()
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();
		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.AddDefaulted();
		Definition->CombatPhases.Add(Phase);
		return Definition;
	}

	URoomCombatDefinition* MakeStealthThenHackDefinition()
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();
		FRoomCombatEnemyEntry EnemyEntry;
		EnemyEntry.EnemyClass = AEnemyBase::StaticClass();

		FRoomCombatPhaseDefinition InitialPhase;
		InitialPhase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		InitialPhase.Waves.AddDefaulted();
		FRoomCombatWaveDefinition ReinforcementWave;
		ReinforcementWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		ReinforcementWave.Enemies.Add(EnemyEntry);
		InitialPhase.Waves.Add(ReinforcementWave);
		Definition->CombatPhases.Add(InitialPhase);

		FRoomCombatPhaseDefinition HackPhase;
		HackPhase.StartPolicy = ERoomCombatPhaseStartPolicy::HackTrigger;
		FRoomCombatWaveDefinition HackWave;
		HackWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		HackWave.Enemies.Add(EnemyEntry);
		HackPhase.Waves.Add(HackWave);
		Definition->CombatPhases.Add(HackPhase);
		return Definition;
	}

	URoomCombatDefinition* MakeSpawnWaveDefinition(int32 EnemyCount)
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();
		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.AddDefaulted();

		FRoomCombatWaveDefinition SpawnWave;
		SpawnWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		FRoomCombatEnemyEntry& EnemyEntry = SpawnWave.Enemies.AddDefaulted_GetRef();
		EnemyEntry.EnemyClass = AEnemyBase::StaticClass();
		EnemyEntry.Count = EnemyCount;
		Phase.Waves.Add(SpawnWave);
		Definition->CombatPhases.Add(Phase);
		return Definition;
	}

	AEnemyBase* SpawnTestEnemy(UWorld* World, FGameplayTag RoomTag)
	{
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(
			AEnemyBase::StaticClass(),
			FTransform::Identity,
			FActorSpawnParameters());
		if (Enemy && Enemy->GetRoomTagComp())
		{
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(RoomTag);
		}
		return Enemy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomCombatWeightedSpawnPointTest,
	"Outlier.Room.WeightedSpawnPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatWeightedSpawnPointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<float> Weights = { 2.0f, 1.0f };
	TArray<int32> AssignedCounts = { 0, 0 };
	for (int32 RequestIndex = 0; RequestIndex < 6; ++RequestIndex)
	{
		const int32 SelectedIndex = RoomCombat::SelectWeightedSpawnPoint(
			Weights,
			AssignedCounts,
			RequestIndex);
		if (TestTrue(TEXT("A weighted candidate is selected"),
			AssignedCounts.IsValidIndex(SelectedIndex)))
		{
			++AssignedCounts[SelectedIndex];
		}
	}

	TestEqual(TEXT("Weight 2 receives four of six assignments"), AssignedCounts[0], 4);
	TestEqual(TEXT("Weight 1 receives two of six assignments"), AssignedCounts[1], 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomCombatSubsystemRuntimeTest,
	"Outlier.Room.CombatRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatSubsystemRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Room combat runtime world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	auto CleanupWorld = [World]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	URoomCombatSubsystem* CombatSubsystem = World->GetSubsystem<URoomCombatSubsystem>();
	if (!TestNotNull(TEXT("Room combat subsystem is created"), CombatSubsystem))
	{
		CleanupWorld();
		return false;
	}

	const FGameplayTag FirstRoomTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Room.Level01.1")));
	const FGameplayTag SecondRoomTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Room.Level01.2")));
	ARoomVolume* FirstRoom = World->SpawnActor<ARoomVolume>();
	ARoomVolume* SecondRoom = World->SpawnActor<ARoomVolume>();
	AEnemyBase* FirstEnemy = SpawnTestEnemy(World, FirstRoomTag);
	AEnemyBase* SecondEnemy = SpawnTestEnemy(World, SecondRoomTag);
	if (!TestNotNull(TEXT("First RoomVolume is spawned"), FirstRoom)
		|| !TestNotNull(TEXT("Second RoomVolume is spawned"), SecondRoom)
		|| !TestNotNull(TEXT("First preplaced Enemy is spawned"), FirstEnemy)
		|| !TestNotNull(TEXT("Second preplaced Enemy is spawned"), SecondEnemy))
	{
		CleanupWorld();
		return false;
	}

	TestTrue(TEXT("First Room registers"),
		CombatSubsystem->RegisterRoom(FirstRoom, FirstRoomTag, MakeSingleWaveDefinition()));
	TestTrue(TEXT("Second Room registers"),
		CombatSubsystem->RegisterRoom(SecondRoom, SecondRoomTag, MakeSingleWaveDefinition()));
	TestTrue(TEXT("A registered Room can be queried"),
		CombatSubsystem->IsRoomRegistered(FirstRoomTag));
	CombatSubsystem->RegisterPreplacedEnemy(FirstEnemy);
	CombatSubsystem->RegisterPreplacedEnemy(SecondEnemy);
	TestEqual(TEXT("A preplaced Enemy is counted"),
		CombatSubsystem->GetAliveEnemyCount(FirstRoomTag), 1);

	TestTrue(TEXT("Initial detection starts the first Room"),
		CombatSubsystem->NotifyRoomCombatStarted(FirstRoomTag));
	TestEqual(TEXT("The first Room enters Combat"),
		CombatSubsystem->GetRoomState(FirstRoomTag), ERoomCombatState::Combat);
	TestTrue(TEXT("Repeated alerts for the active Room are accepted"),
		CombatSubsystem->NotifyRoomCombatStarted(FirstRoomTag));
	TestFalse(TEXT("Another Room cannot start while combat is active"),
		CombatSubsystem->NotifyRoomCombatStarted(SecondRoomTag));
	TestTrue(TEXT("Active Room enables its combat streaming source"),
		FirstRoom->IsCombatStreamingSourceEnabled());
	TestFalse(TEXT("Inactive Room keeps its combat streaming source disabled"),
		SecondRoom->IsCombatStreamingSourceEnabled());

	ARoomCombatSpawnPoint* ActiveSpawnPoint =
		World->SpawnActor<ARoomCombatSpawnPoint>();
	ARoomCombatSpawnPoint* OtherRoomSpawnPoint =
		World->SpawnActor<ARoomCombatSpawnPoint>();
	ARoomCombatSpawnPoint* GroupSpawnPoint =
		World->SpawnActor<ARoomCombatSpawnPoint>();
	if (!TestNotNull(TEXT("Active SpawnPoint is spawned"), ActiveSpawnPoint)
		|| !TestNotNull(TEXT("Other Room SpawnPoint is spawned"), OtherRoomSpawnPoint)
		|| !TestNotNull(TEXT("Activation group SpawnPoint is spawned"), GroupSpawnPoint))
	{
		CleanupWorld();
		return false;
	}

	FGameplayTagContainer FirstRoomSpawnTags;
	FirstRoomSpawnTags.AddTag(FirstRoomTag);
	FGameplayTagContainer SecondRoomSpawnTags;
	SecondRoomSpawnTags.AddTag(SecondRoomTag);
	GroupSpawnPoint->SetRuntimeActive(false);

	TestTrue(TEXT("Active SpawnPoint registers"), CombatSubsystem->RegisterSpawnPoint(
		ActiveSpawnPoint,
		FirstRoomTag,
		FirstRoomSpawnTags,
		FGameplayTag()));
	TestTrue(TEXT("Other Room SpawnPoint registers"), CombatSubsystem->RegisterSpawnPoint(
		OtherRoomSpawnPoint,
		SecondRoomTag,
		SecondRoomSpawnTags,
		FGameplayTag()));
	TestTrue(TEXT("Activation group SpawnPoint registers"), CombatSubsystem->RegisterSpawnPoint(
		GroupSpawnPoint,
		FirstRoomTag,
		FirstRoomSpawnTags,
		SecondRoomTag));
	TestTrue(TEXT("Duplicate registration is idempotent"), CombatSubsystem->RegisterSpawnPoint(
		ActiveSpawnPoint,
		FirstRoomTag,
		FirstRoomSpawnTags,
		FGameplayTag()));
	TestEqual(TEXT("Duplicate registration does not add another entry"),
		CombatSubsystem->GetRegisteredSpawnPointCount(FirstRoomTag), 2);

	TArray<ARoomCombatSpawnPoint*> EligibleSpawnPoints;
	CombatSubsystem->GetEligibleSpawnPoints(
		FirstRoomTag,
		FGameplayTagQuery::MakeQuery_MatchTag(FirstRoomTag),
		EligibleSpawnPoints);
	TestEqual(TEXT("Only the active matching SpawnPoint is eligible"),
		EligibleSpawnPoints.Num(), 1);
	TestTrue(TEXT("The eligible SpawnPoint belongs to the active Room"),
		EligibleSpawnPoints.Contains(ActiveSpawnPoint));

	CombatSubsystem->GetEligibleSpawnPoints(
		FirstRoomTag,
		FGameplayTagQuery::MakeQuery_MatchTag(SecondRoomTag),
		EligibleSpawnPoints);
	TestTrue(TEXT("A mismatched SpawnPoint query returns no candidates"),
		EligibleSpawnPoints.IsEmpty());
	CombatSubsystem->GetEligibleSpawnPoints(
		SecondRoomTag,
		FGameplayTagQuery(),
		EligibleSpawnPoints);
	TestTrue(TEXT("An inactive combat Room returns no SpawnPoint candidates"),
		EligibleSpawnPoints.IsEmpty());

	CombatSubsystem->SetActivationGroupActive(
		FirstRoomTag,
		SecondRoomTag,
		true);
	CombatSubsystem->GetEligibleSpawnPoints(
		FirstRoomTag,
		FGameplayTagQuery::MakeQuery_MatchTag(FirstRoomTag),
		EligibleSpawnPoints);
	TestEqual(TEXT("Activating a group adds its SpawnPoint to the candidates"),
		EligibleSpawnPoints.Num(), 2);

	GroupSpawnPoint->Destroy();
	TestEqual(TEXT("SpawnPoint EndPlay unregisters its entry"),
		CombatSubsystem->GetRegisteredSpawnPointCount(FirstRoomTag), 1);

	CombatSubsystem->NotifyEnemyDefeated(FirstEnemy);
	TestEqual(TEXT("The last defeated Enemy clears a single-Wave Room"),
		CombatSubsystem->GetRoomState(FirstRoomTag), ERoomCombatState::Cleared);
	TestFalse(TEXT("Completing the phase disables its combat streaming source"),
		FirstRoom->IsCombatStreamingSourceEnabled());
	TestFalse(TEXT("Ordinary unregistration is not treated as a defeat"),
		CombatSubsystem->GetRoomState(SecondRoomTag) == ERoomCombatState::Cleared);
	CombatSubsystem->UnregisterEnemy(SecondEnemy);
	TestEqual(TEXT("EndPlay-style unregistration leaves the Room dormant"),
		CombatSubsystem->GetRoomState(SecondRoomTag), ERoomCombatState::Dormant);

	const FGameplayTag StealthRoomTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Room.Level01.3")));
	ARoomVolume* StealthRoom = World->SpawnActor<ARoomVolume>();
	AEnemyBase* StealthEnemy = SpawnTestEnemy(World, StealthRoomTag);
	if (!TestNotNull(TEXT("Stealth RoomVolume is spawned"), StealthRoom)
		|| !TestNotNull(TEXT("Stealth preplaced Enemy is spawned"), StealthEnemy))
	{
		CleanupWorld();
		return false;
	}

	TestTrue(TEXT("Stealth Room registers"), CombatSubsystem->RegisterRoom(
		StealthRoom,
		StealthRoomTag,
		MakeStealthThenHackDefinition()));
	CombatSubsystem->RegisterPreplacedEnemy(StealthEnemy);
	CombatSubsystem->NotifyEnemyDefeated(StealthEnemy);
	TestEqual(TEXT("Undetected elimination cancels the current phase reinforcements"),
		CombatSubsystem->GetRoomState(StealthRoomTag),
		ERoomCombatState::WaitingForTrigger);
	TestEqual(TEXT("Undetected elimination advances only to the next phase"),
		CombatSubsystem->GetCurrentCombatPhaseIndex(StealthRoomTag), 1);

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomCombatWaveSpawnRuntimeTest,
	"Outlier.Room.WaveSpawnRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatWaveSpawnRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Wave spawn runtime world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Wave spawn runtime world creates an authority game mode"),
		World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	auto CleanupWorld = [World]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	URoomCombatSubsystem* CombatSubsystem = World->GetSubsystem<URoomCombatSubsystem>();
	UEnemyPoolSubsystem* PoolSubsystem = World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!TestNotNull(TEXT("Room combat subsystem is created"), CombatSubsystem)
		|| !TestNotNull(TEXT("Enemy pool subsystem is created"), PoolSubsystem))
	{
		CleanupWorld();
		return false;
	}

	UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& PoolEntry = PoolDefinition->Entries.AddDefaulted_GetRef();
	PoolEntry.EnemyClass = AEnemyBase::StaticClass();
	PoolEntry.PrewarmCount = 3;
	PoolEntry.MaxCount = 3;
	TestTrue(TEXT("Wave spawn pool prewarms"), PoolSubsystem->PrewarmPool(PoolDefinition));

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Room.Level01.1")));
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
	AEnemyBase* PreplacedEnemy = SpawnTestEnemy(World, RoomTag);
	ARoomCombatSpawnPoint* FirstSpawnPoint = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(),
		FTransform(FVector(2000.0f, 0.0f, 0.0f)));
	ARoomCombatSpawnPoint* SecondSpawnPoint = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(),
		FTransform(FVector(-2000.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("Wave spawn Room is spawned"), Room)
		|| !TestNotNull(TEXT("Wave spawn preplaced Enemy is spawned"), PreplacedEnemy)
		|| !TestNotNull(TEXT("First wave SpawnPoint is spawned"), FirstSpawnPoint)
		|| !TestNotNull(TEXT("Second wave SpawnPoint is spawned"), SecondSpawnPoint))
	{
		CleanupWorld();
		return false;
	}

	TestTrue(TEXT("Wave spawn Room registers"), CombatSubsystem->RegisterRoom(
		Room,
		RoomTag,
		MakeSpawnWaveDefinition(4)));
	CombatSubsystem->RegisterPreplacedEnemy(PreplacedEnemy);
	TestTrue(TEXT("First wave SpawnPoint registers"), CombatSubsystem->RegisterSpawnPoint(
		FirstSpawnPoint,
		RoomTag,
		FGameplayTagContainer(),
		FGameplayTag()));
	TestTrue(TEXT("Second wave SpawnPoint registers"), CombatSubsystem->RegisterSpawnPoint(
		SecondSpawnPoint,
		RoomTag,
		FGameplayTagContainer(),
		FGameplayTag()));
	TestTrue(TEXT("Initial detection starts the spawn test Room"),
		CombatSubsystem->NotifyRoomCombatStarted(RoomTag));

	SecondSpawnPoint->SetForceSpawnLocationFailureForTesting(true);
	TestTrue(TEXT("The fixed roster wave starts"),
		CombatSubsystem->StartWaveSpawning(RoomTag, 0, 1));
	TestEqual(TEXT("Equal weights assign two requests to the first SpawnPoint"),
		CombatSubsystem->GetAssignedSpawnCount(RoomTag, FirstSpawnPoint), 2);
	TestEqual(TEXT("Equal weights assign two requests to the second SpawnPoint"),
		CombatSubsystem->GetAssignedSpawnCount(RoomTag, SecondSpawnPoint), 2);
	TestEqual(TEXT("Location failures remain pending"),
		CombatSubsystem->GetPendingSpawnCount(RoomTag), 2);
	TestEqual(TEXT("Only successful leases enter the alive count"),
		CombatSubsystem->GetAliveEnemyCount(RoomTag), 3);

	SecondSpawnPoint->SetForceSpawnLocationFailureForTesting(false);
	CombatSubsystem->RetryPendingSpawnsForTesting();
	TestEqual(TEXT("Pool exhaustion leaves one request pending"),
		CombatSubsystem->GetPendingSpawnCount(RoomTag), 1);
	TestEqual(TEXT("Pool expands only to its configured maximum"),
		PoolSubsystem->GetLeasedCount(AEnemyBase::StaticClass()), 3);

	AEnemyBase* ReturnedEnemy = nullptr;
	for (TActorIterator<AEnemyBase> EnemyIt(World); EnemyIt; ++EnemyIt)
	{
		if (EnemyIt->IsPoolManaged()
			&& EnemyIt->GetEnemyPoolState() != EEnemyPoolState::Idle)
		{
			ReturnedEnemy = *EnemyIt;
			break;
		}
	}
	if (TestNotNull(TEXT("A leased Enemy is available for return"), ReturnedEnemy))
	{
		TestTrue(TEXT("A leased Enemy returns to the pool"), PoolSubsystem->ReturnEnemy(
			ReturnedEnemy,
			ReturnedEnemy->GetPoolGameplayGeneration(),
			ReturnedEnemy->GetPoolLeaseSerial()));
	}
	CombatSubsystem->RetryPendingSpawnsForTesting();
	TestEqual(TEXT("A returned Enemy satisfies the final pending request"),
		CombatSubsystem->GetPendingSpawnCount(RoomTag), 0);
	TestEqual(TEXT("Every successful pooled Enemy is counted once"),
		CombatSubsystem->GetAliveEnemyCount(RoomTag), 4);
	TestEqual(TEXT("The pool remains capped after reuse"),
		PoolSubsystem->GetTotalCount(AEnemyBase::StaticClass()), 3);

	CleanupWorld();
	return true;
}

#endif
