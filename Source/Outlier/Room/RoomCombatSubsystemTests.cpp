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
#include "UObject/UnrealType.h"

namespace
{
	FRoomCombatRoomDefinition& AddRoomDefinition(
		URoomCombatDefinition* Definition,
		FGameplayTag RoomTag)
	{
		FRoomCombatRoomDefinition& RoomDefinition =
			Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = RoomTag;
		return RoomDefinition;
	}

	void AddSingleWaveDefinition(URoomCombatDefinition* Definition, FGameplayTag RoomTag)
	{
		FRoomCombatRoomDefinition& RoomDefinition = AddRoomDefinition(Definition, RoomTag);
		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.AddDefaulted();
		RoomDefinition.CombatPhases.Add(Phase);
	}

	void AddStealthThenHackDefinition(URoomCombatDefinition* Definition, FGameplayTag RoomTag)
	{
		FRoomCombatRoomDefinition& RoomDefinition = AddRoomDefinition(Definition, RoomTag);
		FRoomCombatEnemyEntry EnemyEntry;
		EnemyEntry.EnemyClass = AEnemyBase::StaticClass();

		FRoomCombatPhaseDefinition InitialPhase;
		InitialPhase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		InitialPhase.Waves.AddDefaulted();
		FRoomCombatWaveDefinition ReinforcementWave;
		ReinforcementWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		ReinforcementWave.Enemies.Add(EnemyEntry);
		InitialPhase.Waves.Add(ReinforcementWave);
		RoomDefinition.CombatPhases.Add(InitialPhase);

		FRoomCombatPhaseDefinition HackPhase;
		HackPhase.StartPolicy = ERoomCombatPhaseStartPolicy::HackTrigger;
		FRoomCombatWaveDefinition HackWave;
		HackWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		HackWave.Enemies.Add(EnemyEntry);
		HackPhase.Waves.Add(HackWave);
		RoomDefinition.CombatPhases.Add(HackPhase);
	}

	void AddSpawnWaveDefinition(
		URoomCombatDefinition* Definition,
		FGameplayTag RoomTag,
		int32 EnemyCount)
	{
		FRoomCombatRoomDefinition& RoomDefinition = AddRoomDefinition(Definition, RoomTag);
		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.AddDefaulted();

		FRoomCombatWaveDefinition SpawnWave;
		SpawnWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		FRoomCombatEnemyEntry& EnemyEntry = SpawnWave.Enemies.AddDefaulted_GetRef();
		EnemyEntry.EnemyClass = AEnemyBase::StaticClass();
		EnemyEntry.Count = EnemyCount;
		Phase.Waves.Add(SpawnWave);
		RoomDefinition.CombatPhases.Add(Phase);
	}

	void AddWaveProgressDefinition(URoomCombatDefinition* Definition, FGameplayTag RoomTag)
	{
		FRoomCombatRoomDefinition& RoomDefinition = AddRoomDefinition(Definition, RoomTag);
		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;

		FRoomCombatWaveDefinition& PreplacedWave = Phase.Waves.AddDefaulted_GetRef();
		PreplacedWave.NextWaveRemainingRatio = 0.5f;

		FRoomCombatWaveDefinition& ReinforcementWave = Phase.Waves.AddDefaulted_GetRef();
		ReinforcementWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		ReinforcementWave.NextWaveRemainingRatio = 0.5f;
		FRoomCombatEnemyEntry& ReinforcementEntry =
			ReinforcementWave.Enemies.AddDefaulted_GetRef();
		ReinforcementEntry.EnemyClass = AEnemyBase::StaticClass();
		ReinforcementEntry.Count = 2;

		FRoomCombatWaveDefinition& FinalWave = Phase.Waves.AddDefaulted_GetRef();
		FinalWave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		FRoomCombatEnemyEntry& FinalEntry = FinalWave.Enemies.AddDefaulted_GetRef();
		FinalEntry.EnemyClass = AEnemyBase::StaticClass();
		FinalEntry.Count = 1;

		RoomDefinition.CombatPhases.Add(Phase);
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
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
	AddSingleWaveDefinition(Definition, FirstRoomTag);
	AddSingleWaveDefinition(Definition, SecondRoomTag);
	CombatSubsystem->SetCombatDefinitionForTesting(Definition);
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
		CombatSubsystem->RegisterRoom(FirstRoom, FirstRoomTag));
	TestTrue(TEXT("Second Room registers"),
		CombatSubsystem->RegisterRoom(SecondRoom, SecondRoomTag));
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
		FirstRoomTag,
		EligibleSpawnPoints);
	TestEqual(TEXT("Only the active matching SpawnPoint is eligible"),
		EligibleSpawnPoints.Num(), 1);
	TestTrue(TEXT("The eligible SpawnPoint belongs to the active Room"),
		EligibleSpawnPoints.Contains(ActiveSpawnPoint));

	CombatSubsystem->GetEligibleSpawnPoints(
		FirstRoomTag,
		SecondRoomTag,
		EligibleSpawnPoints);
	TestTrue(TEXT("A mismatched required SpawnPoint tag returns no candidates"),
		EligibleSpawnPoints.IsEmpty());
	CombatSubsystem->GetEligibleSpawnPoints(
		SecondRoomTag,
		FGameplayTag(),
		EligibleSpawnPoints);
	TestTrue(TEXT("An inactive combat Room returns no SpawnPoint candidates"),
		EligibleSpawnPoints.IsEmpty());

	CombatSubsystem->SetActivationGroupActive(
		FirstRoomTag,
		SecondRoomTag,
		true);
	CombatSubsystem->GetEligibleSpawnPoints(
		FirstRoomTag,
		FirstRoomTag,
		EligibleSpawnPoints);
	TestEqual(TEXT("Activating a group adds its SpawnPoint to the candidates"),
		EligibleSpawnPoints.Num(), 2);
	CombatSubsystem->GetEligibleSpawnPoints(
		FirstRoomTag,
		FGameplayTag(),
		EligibleSpawnPoints);
	TestEqual(TEXT("An empty required tag accepts every active SpawnPoint"),
		EligibleSpawnPoints.Num(), 2);
	const FGameplayTag ParentSpawnTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Room.Level01")));
	CombatSubsystem->GetEligibleSpawnPoints(
		FirstRoomTag,
		ParentSpawnTag,
		EligibleSpawnPoints);
	TestEqual(TEXT("A parent required tag accepts child SpawnPoint tags"),
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
	AddStealthThenHackDefinition(Definition, StealthRoomTag);
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
		StealthRoomTag));
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
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
	AddSpawnWaveDefinition(Definition, RoomTag, 4);
	CombatSubsystem->SetCombatDefinitionForTesting(Definition);
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
		RoomTag));
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
	TestFalse(TEXT("A request cannot start another combat phase"),
		CombatSubsystem->StartWaveSpawning(RoomTag, 1, 1));
	TestFalse(TEXT("A request cannot use a negative Wave index"),
		CombatSubsystem->StartWaveSpawning(RoomTag, 0, -1));
	TestFalse(TEXT("A request cannot skip past the next Wave"),
		CombatSubsystem->StartWaveSpawning(RoomTag, 0, 2));
	TestTrue(TEXT("The fixed roster wave starts"),
		CombatSubsystem->StartWaveSpawning(RoomTag, 0, 1));
	TestFalse(TEXT("A pending Wave cannot be started twice"),
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
	TestEqual(TEXT("Wave baseline is captured after every request succeeds"),
		CombatSubsystem->GetWaveBaselineEnemyCount(RoomTag), 4);
	TestEqual(TEXT("Every successful pooled Enemy is counted once"),
		CombatSubsystem->GetAliveEnemyCount(RoomTag), 4);
	TestEqual(TEXT("The pool remains capped after reuse"),
		PoolSubsystem->GetTotalCount(AEnemyBase::StaticClass()), 3);

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomCombatWaveProgressRuntimeTest,
	"Outlier.Room.WaveProgressRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatWaveProgressRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Wave progress runtime world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Wave progress runtime world creates an authority game mode"),
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
	if (!TestNotNull(TEXT("Wave progress Room combat subsystem is created"), CombatSubsystem)
		|| !TestNotNull(TEXT("Wave progress Enemy pool subsystem is created"), PoolSubsystem))
	{
		CleanupWorld();
		return false;
	}

	UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& PoolEntry = PoolDefinition->Entries.AddDefaulted_GetRef();
	PoolEntry.EnemyClass = AEnemyBase::StaticClass();
	PoolEntry.PrewarmCount = 3;
	PoolEntry.MaxCount = 3;
	TestTrue(TEXT("Wave progress pool prewarms"), PoolSubsystem->PrewarmPool(PoolDefinition));

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Room.Level01.1")));
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
	AddWaveProgressDefinition(Definition, RoomTag);
	CombatSubsystem->SetCombatDefinitionForTesting(Definition);
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
	ARoomCombatSpawnPoint* SpawnPoint = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(),
		FTransform(FVector(5000.0f, 0.0f, 0.0f)));
	AEnemyBase* FirstPreplaced = SpawnTestEnemy(World, RoomTag);
	AEnemyBase* SecondPreplaced = SpawnTestEnemy(World, RoomTag);
	AEnemyBase* ThirdPreplaced = SpawnTestEnemy(World, RoomTag);
	if (!TestNotNull(TEXT("Wave progress Room is spawned"), Room)
		|| !TestNotNull(TEXT("Wave progress SpawnPoint is spawned"), SpawnPoint)
		|| !TestNotNull(TEXT("First Wave progress preplaced Enemy is spawned"), FirstPreplaced)
		|| !TestNotNull(TEXT("Second Wave progress preplaced Enemy is spawned"), SecondPreplaced)
		|| !TestNotNull(TEXT("Third Wave progress preplaced Enemy is spawned"), ThirdPreplaced))
	{
		CleanupWorld();
		return false;
	}

	FirstPreplaced->SetActorLocation(FVector(-5000.0f, 0.0f, 0.0f));
	SecondPreplaced->SetActorLocation(FVector(-5200.0f, 0.0f, 0.0f));
	ThirdPreplaced->SetActorLocation(FVector(-5400.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Wave progress Room registers"), CombatSubsystem->RegisterRoom(
		Room,
		RoomTag));
	CombatSubsystem->RegisterPreplacedEnemy(FirstPreplaced);
	CombatSubsystem->RegisterPreplacedEnemy(SecondPreplaced);
	CombatSubsystem->RegisterPreplacedEnemy(ThirdPreplaced);
	TestTrue(TEXT("Wave progress SpawnPoint registers"), CombatSubsystem->RegisterSpawnPoint(
		SpawnPoint,
		RoomTag,
		FGameplayTagContainer(),
		FGameplayTag()));
	TestTrue(TEXT("Initial detection starts Wave progress combat"),
		CombatSubsystem->NotifyRoomCombatStarted(RoomTag));
	TestEqual(TEXT("Preplaced Wave captures its starting baseline"),
		CombatSubsystem->GetWaveBaselineEnemyCount(RoomTag), 3);

	SpawnPoint->SetForceSpawnLocationFailureForTesting(true);
	CombatSubsystem->NotifyEnemyDefeated(FirstPreplaced);
	TestEqual(TEXT("A ratio above the threshold keeps the current Wave"),
		CombatSubsystem->GetCurrentWaveIndex(RoomTag), 0);
	CombatSubsystem->NotifyEnemyDefeated(SecondPreplaced);
	TestEqual(TEXT("Falling below the threshold starts the next Wave"),
		CombatSubsystem->GetCurrentWaveIndex(RoomTag), 1);
	TestEqual(TEXT("A Wave with pending spawns has no finalized baseline"),
		CombatSubsystem->GetWaveBaselineEnemyCount(RoomTag), INDEX_NONE);
	TestEqual(TEXT("Failed reinforcement locations remain pending"),
		CombatSubsystem->GetPendingSpawnCount(RoomTag), 2);

	CombatSubsystem->NotifyEnemyDefeated(ThirdPreplaced);
	TestEqual(TEXT("Zero alive Enemies does not finish combat while spawns are pending"),
		CombatSubsystem->GetRoomState(RoomTag), ERoomCombatState::Combat);
	TestEqual(TEXT("Pending requests survive the momentary zero alive count"),
		CombatSubsystem->GetPendingSpawnCount(RoomTag), 2);

	SpawnPoint->SetForceSpawnLocationFailureForTesting(false);
	CombatSubsystem->RetryPendingSpawnsForTesting();
	TestEqual(TEXT("Completing reinforcement spawns clears Pending"),
		CombatSubsystem->GetPendingSpawnCount(RoomTag), 0);
	TestEqual(TEXT("Reinforcement Wave stores a fresh baseline"),
		CombatSubsystem->GetWaveBaselineEnemyCount(RoomTag), 2);

	TArray<AEnemyBase*> ReinforcementEnemies;
	for (TActorIterator<AEnemyBase> EnemyIt(World); EnemyIt; ++EnemyIt)
	{
		if (EnemyIt->IsPoolManaged()
			&& EnemyIt->GetEnemyPoolState() != EEnemyPoolState::Idle)
		{
			ReinforcementEnemies.Add(*EnemyIt);
		}
	}
	if (TestEqual(TEXT("The reinforcement Wave leased two Enemies"),
		ReinforcementEnemies.Num(), 2))
	{
		CombatSubsystem->NotifyEnemyDefeated(ReinforcementEnemies[0]);
	}
	TestEqual(TEXT("The exact ratio boundary starts the final Wave"),
		CombatSubsystem->GetCurrentWaveIndex(RoomTag), 2);
	TestEqual(TEXT("The final Wave baseline includes a previous Wave survivor"),
		CombatSubsystem->GetWaveBaselineEnemyCount(RoomTag), 2);

	TArray<AEnemyBase*> FinalTrackedEnemies;
	for (TActorIterator<AEnemyBase> EnemyIt(World); EnemyIt; ++EnemyIt)
	{
		if (EnemyIt->IsPoolManaged()
			&& EnemyIt->GetEnemyPoolState() != EEnemyPoolState::Idle
			&& !ReinforcementEnemies.IsEmpty()
			&& *EnemyIt != ReinforcementEnemies[0])
		{
			FinalTrackedEnemies.Add(*EnemyIt);
		}
	}
	TestEqual(TEXT("The final Wave tracks the survivor and newly spawned Enemy"),
		FinalTrackedEnemies.Num(), 2);
	for (AEnemyBase* Enemy : FinalTrackedEnemies)
	{
		CombatSubsystem->NotifyEnemyDefeated(Enemy);
	}
	TestEqual(TEXT("The last Wave completes only after every tracked Enemy dies"),
		CombatSubsystem->GetRoomState(RoomTag), ERoomCombatState::Cleared);
	TestEqual(TEXT("Completing the phase clears the Wave baseline"),
		CombatSubsystem->GetWaveBaselineEnemyCount(RoomTag), INDEX_NONE);

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomCombatTriggeredSequenceTest,
	"Outlier.Room.TriggeredSequenceRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatTriggeredSequenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Sequence test world exists"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Sequence world has authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());
	auto CleanupWorld = [World]()
	{
		if (URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>())
		{
			Combat->CombatEventObserverForTesting = nullptr;
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
	UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
	ARoomVolume* OtherRoom = World->SpawnActor<ARoomVolume>();
	AActor* Requester = World->SpawnActor<AActor>();
	ARoomCombatSpawnPoint* Point = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(), FTransform(FVector(5000.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("Combat subsystem"), Combat) || !TestNotNull(TEXT("Pool subsystem"), Pool)
		|| !TestNotNull(TEXT("Room"), Room) || !TestNotNull(TEXT("Other Room"), OtherRoom)
		|| !TestNotNull(TEXT("Requester"), Requester) || !TestNotNull(TEXT("Spawn point"), Point))
	{
		CleanupWorld();
		return false;
	}

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FGameplayTag OtherTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")));
	const FGameplayTag GroupTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.3")));
	// 배치 인스턴스의 RoomTag 설정을 재현해 실제 EndPlay 등록 해제 경로도 검증한다.
	FStructProperty* RoomTagProperty = FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"));
	if (!TestNotNull(TEXT("RoomTag property"), RoomTagProperty))
	{
		CleanupWorld();
		return false;
	}
	*RoomTagProperty->ContainerPtrToValuePtr<FGameplayTag>(Room) = RoomTag;
	*RoomTagProperty->ContainerPtrToValuePtr<FGameplayTag>(OtherRoom) = OtherTag;
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
	FRoomCombatRoomDefinition& RoomDefinition = AddRoomDefinition(Definition, RoomTag);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FRoomCombatPhaseDefinition& Phase = RoomDefinition.CombatPhases.AddDefaulted_GetRef();
		Phase.StartPolicy = Index == 0 ? ERoomCombatPhaseStartPolicy::HackTrigger
			: ERoomCombatPhaseStartPolicy::Automatic;
		FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
		Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		Wave.Enemies.AddDefaulted_GetRef().EnemyClass = AEnemyBase::StaticClass();
	}
	FRoomCombatRoomDefinition OtherRoomDefinition = RoomDefinition;
	OtherRoomDefinition.RoomTag = OtherTag;
	Definition->RoomDefinitions.Add(MoveTemp(OtherRoomDefinition));
	Combat->SetCombatDefinitionForTesting(Definition);
	TestTrue(TEXT("Sequence Room registers"), Combat->RegisterRoom(Room, RoomTag));
	TestTrue(TEXT("Other Room registers"), Combat->RegisterRoom(OtherRoom, OtherTag));
	Point->SetRuntimeActive(false);
	Point->SetForceSpawnLocationFailureForTesting(true);
	TestTrue(TEXT("Inactive group point registers"), Combat->RegisterSpawnPoint(
		Point, RoomTag, FGameplayTagContainer(), GroupTag));

	UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& Entry = PoolDefinition->Entries.AddDefaulted_GetRef();
	Entry.EnemyClass = AEnemyBase::StaticClass();
	Entry.PrewarmCount = 1;
	Entry.MaxCount = 1;
	TestTrue(TEXT("Sequence pool prewarms"), Pool->PrewarmPool(PoolDefinition));
	int32 Starts = 0;
	int32 Phases = 0;
	int32 Clears = 0;
	int32 Cancels = 0;
	Combat->CombatEventObserverForTesting = [&](FGameplayTag EventRoom, ERoomCombatEvent Event, int32 Phase)
	{
		TestEqual(TEXT("Events identify the affected Room"), EventRoom, RoomTag);
		switch (Event)
		{
		case ERoomCombatEvent::SequenceStarted:
			++Starts;
			TestTrue(TEXT("Start observers see blocked exits"), Combat->IsExitBlocked(RoomTag));
			break;
		case ERoomCombatEvent::PhaseCompleted:
			++Phases;
			if (Phase == 0)
			{
				TestTrue(TEXT("Intermediate phase keeps exits blocked"), Combat->IsExitBlocked(RoomTag));
				TestTrue(TEXT("Intermediate phase keeps streaming"), Room->IsCombatStreamingSourceEnabled());
			}
			break;
		case ERoomCombatEvent::RoomCleared:
			++Clears;
			TestFalse(TEXT("Clear observers see open exits"), Combat->IsExitBlocked(RoomTag));
			break;
		case ERoomCombatEvent::Cancelled:
			++Cancels;
			TestFalse(TEXT("Cancel observers see open exits"), Combat->IsExitBlocked(RoomTag));
			break;
		}
	};

	FRoomCombatTriggerContext Context;
	FRoomCombatTriggerContext OtherContext;
	TestFalse(TEXT("Invalid groups cannot create a context"),
		Combat->CreateTriggerContext(Requester, RoomTag, FGameplayTag(), Context));
	TestTrue(TEXT("Hack captures context"), Combat->CreateTriggerContext(Requester, RoomTag, GroupTag, Context));
	TestTrue(TEXT("Context creation does not reserve another Room"),
		Combat->CreateTriggerContext(Requester, OtherTag, GroupTag, OtherContext));
	TestFalse(TEXT("Context creation does not block exits"), Combat->IsExitBlocked(RoomTag));
	FRoomCombatTriggerContext StaleGeneration = Context;
	++StaleGeneration.GameplayGeneration;
	TestFalse(TEXT("Wrong Generation is rejected"), Combat->StartTriggeredSequence(Requester, StaleGeneration));
	TestFalse(TEXT("Another requester cannot use the context"), Combat->StartTriggeredSequence(OtherRoom, Context));
	AActor* ExpiringRequester = World->SpawnActor<AActor>();
	FRoomCombatTriggerContext ExpiredContext;
	if (TestNotNull(TEXT("Temporary requester"), ExpiringRequester))
	{
		Combat->CreateTriggerContext(ExpiringRequester, RoomTag, GroupTag, ExpiredContext);
		ExpiringRequester->Destroy();
		TestFalse(TEXT("Destroyed requester cannot complete a hack"),
			Combat->StartTriggeredSequence(ExpiringRequester, ExpiredContext));
	}
	Definition->RoomDefinitions[0].CombatPhases[1].Waves[0].Enemies[0].Count = 0;
	TestFalse(TEXT("Invalid later roster rejects the entire start"), Combat->StartTriggeredSequence(Requester, Context));
	TestFalse(TEXT("Invalid start does not activate the group"), Point->IsRuntimeActive());
	TestEqual(TEXT("Invalid start leaves the Room waiting"),
		Combat->GetRoomState(RoomTag), ERoomCombatState::WaitingForTrigger);
	TestEqual(TEXT("Invalid start emits no start event"), Starts, 0);
	Definition->RoomDefinitions[0].CombatPhases[1].Waves[0].Enemies[0].Count = 1;
	TestTrue(TEXT("Hack starts sequence"), Combat->StartTriggeredSequence(Requester, Context));
	TestTrue(TEXT("Group point activates"), Point->IsRuntimeActive());
	TestEqual(TEXT("Location failure remains pending"), Combat->GetPendingSpawnCount(RoomTag), 1);
	TestFalse(TEXT("Duplicate success is rejected"), Combat->StartTriggeredSequence(Requester, Context));
	TestFalse(TEXT("Concurrent Room is rejected"), Combat->StartTriggeredSequence(Requester, OtherContext));

	ARoomCombatSpawnPoint* LatePoint = World->SpawnActor<ARoomCombatSpawnPoint>(
		ARoomCombatSpawnPoint::StaticClass(), FTransform(FVector(7000.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("Late point"), LatePoint)) { CleanupWorld(); return false; }
	LatePoint->SetRuntimeActive(false);
	LatePoint->SetForceSpawnLocationFailureForTesting(true);
	Combat->RegisterSpawnPoint(LatePoint, RoomTag, FGameplayTagContainer(), GroupTag);
	TestTrue(TEXT("Late registration inherits active group"), LatePoint->IsRuntimeActive());
	Point->SetForceSpawnLocationFailureForTesting(false);
	LatePoint->SetForceSpawnLocationFailureForTesting(false);
	Combat->RetryPendingSpawnsForTesting();
	TestEqual(TEXT("First phase spawns"), Combat->GetAliveEnemyCount(RoomTag), 1);
	AEnemyBase* Enemy = nullptr;
	for (TActorIterator<AEnemyBase> It(World); It; ++It)
	{
		if (It->IsPoolManaged() && It->GetEnemyPoolState() != EEnemyPoolState::Idle) { Enemy = *It; break; }
	}
	if (!TestNotNull(TEXT("First leased Enemy"), Enemy)) { CleanupWorld(); return false; }
	Combat->NotifyEnemyDefeated(Enemy);
	TestEqual(TEXT("The second phase starts automatically"), Combat->GetCurrentCombatPhaseIndex(RoomTag), 1);
	TestEqual(TEXT("Pool exhaustion in next phase remains pending"), Combat->GetPendingSpawnCount(RoomTag), 1);
	TestTrue(TEXT("The next phase keeps its group active"), Point->IsRuntimeActive());
	TestEqual(TEXT("No intermediate clear event"), Clears, 0);
	Pool->ReturnEnemy(Enemy, Enemy->GetPoolGameplayGeneration(), Enemy->GetPoolLeaseSerial());
	Combat->RetryPendingSpawnsForTesting();
	TestEqual(TEXT("Returned pool Enemy satisfies automatic phase"), Combat->GetAliveEnemyCount(RoomTag), 1);
	Combat->NotifyEnemyDefeated(Enemy);
	Combat->NotifyEnemyDefeated(Enemy);
	TestEqual(TEXT("All phases clear the Room"), Combat->GetRoomState(RoomTag), ERoomCombatState::Cleared);
	TestEqual(TEXT("Exactly one start event"), Starts, 1);
	TestEqual(TEXT("Both phases complete once"), Phases, 2);
	TestEqual(TEXT("Exactly one overall clear event"), Clears, 1);
	TestFalse(TEXT("All group points deactivate"), Point->IsRuntimeActive() || LatePoint->IsRuntimeActive());
	TestFalse(TEXT("Final completion releases streaming"), Room->IsCombatStreamingSourceEnabled());
	Combat->UnregisterSpawnPoint(LatePoint);
	LatePoint->SetRuntimeActive(true);
	Combat->RegisterSpawnPoint(LatePoint, RoomTag, FGameplayTagContainer(), GroupTag);
	TestFalse(TEXT("Registration after Clear retains the disabled group state"), LatePoint->IsRuntimeActive());
	TestFalse(TEXT("Completed context cannot restart"), Combat->StartTriggeredSequence(Requester, Context));
	Pool->ReturnEnemy(Enemy, Enemy->GetPoolGameplayGeneration(), Enemy->GetPoolLeaseSerial());

	Combat->UnregisterRoom(Room);
	Combat->RegisterRoom(Room, RoomTag);
	TestFalse(TEXT("Old context cannot target re-registered Room"), Combat->StartTriggeredSequence(Requester, Context));
	Combat->CreateTriggerContext(Requester, RoomTag, GroupTag, Context);
	Point->SetForceSpawnLocationFailureForTesting(true);
	LatePoint->SetForceSpawnLocationFailureForTesting(true);
	TestTrue(TEXT("New Room lifetime can start"), Combat->StartTriggeredSequence(Requester, Context));
	Combat->UnregisterRoom(Room);
	TestEqual(TEXT("Unregister cancels once"), Cancels, 1);
	TestEqual(TEXT("Unregister does not clear again"), Clears, 1);
	TestEqual(TEXT("Unregister removes pending work"), Combat->GetPendingSpawnCount(RoomTag), 0);
	TestFalse(TEXT("Unregister deactivates group"), Point->IsRuntimeActive());

	Combat->RegisterRoom(Room, RoomTag);
	Combat->CreateTriggerContext(Requester, RoomTag, GroupTag, Context);
	Combat->StartTriggeredSequence(Requester, Context);
	Combat->ResetRuntimeCombatState();
	Combat->ResetRuntimeCombatState();
	TestEqual(TEXT("Reset cancellation is idempotent"), Cancels, 2);
	TestEqual(TEXT("Reset does not count as a clear"), Clears, 1);
	TestFalse(TEXT("Reset deactivates group"), Point->IsRuntimeActive());
	TestEqual(TEXT("Reset removes pending work"), Combat->GetPendingSpawnCount(RoomTag), 0);
	Combat->RegisterRoom(Room, RoomTag);
	TestFalse(TEXT("Reset invalidates previous contexts"), Combat->StartTriggeredSequence(Requester, Context));

	// 실제 BP 델리게이트 수신 중 Reset되는 경우와 같은 재진입을 재현한다.
	int32 ReentrantCancels = 0;
	Combat->CombatEventObserverForTesting = [&](FGameplayTag, ERoomCombatEvent Event, int32)
	{
		if (Event == ERoomCombatEvent::SequenceStarted) { Combat->ResetRuntimeCombatState(); }
		if (Event == ERoomCombatEvent::Cancelled)
		{
			++ReentrantCancels;
			Combat->ResetRuntimeCombatState();
		}
	};
	Combat->CreateTriggerContext(Requester, RoomTag, GroupTag, Context);
	TestTrue(TEXT("Accepted start may be synchronously cancelled by its listener"),
		Combat->StartTriggeredSequence(Requester, Context));
	TestEqual(TEXT("Reentrant reset cancels only once"), ReentrantCancels, 1);
	TestFalse(TEXT("Reentrant reset leaves no active Room"), Combat->GetActiveCombatRoomTag().IsValid());
	TestEqual(TEXT("Reentrant reset does not resume spawning"), Combat->GetPendingSpawnCount(RoomTag), 0);
	TestEqual(TEXT("No pooled Enemy is leased after cancelled start"), Pool->GetLeasedCount(AEnemyBase::StaticClass()), 0);

	Combat->RegisterRoom(Room, RoomTag);
	Point->SetForceSpawnLocationFailureForTesting(false);
	Combat->RegisterSpawnPoint(Point, RoomTag, FGameplayTagContainer(), GroupTag);
	Combat->CombatEventObserverForTesting = [&](FGameplayTag, ERoomCombatEvent Event, int32 Phase)
	{
		if (Event == ERoomCombatEvent::PhaseCompleted && Phase == 0)
		{
			Combat->ResetRuntimeCombatState();
		}
	};
	Combat->CreateTriggerContext(Requester, RoomTag, GroupTag, Context);
	Combat->StartTriggeredSequence(Requester, Context);
	TestEqual(TEXT("Reentrant phase test has one living Enemy"), Combat->GetAliveEnemyCount(RoomTag), 1);
	Combat->NotifyEnemyDefeated(Enemy);
	TestFalse(TEXT("Phase listener reset leaves exits open"), Combat->IsExitBlocked(RoomTag));
	TestFalse(TEXT("Phase listener reset disables group"), Point->IsRuntimeActive());
	TestEqual(TEXT("Phase listener reset cancels the queued automatic phase"), Combat->GetPendingSpawnCount(RoomTag), 0);
	Pool->ReturnEnemy(Enemy, Enemy->GetPoolGameplayGeneration(), Enemy->GetPoolLeaseSerial());
	CleanupWorld();
	return true;
}

#endif
