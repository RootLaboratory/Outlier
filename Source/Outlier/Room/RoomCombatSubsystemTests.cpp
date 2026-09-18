#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Room/RoomCombatDefinition.h"
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

	CombatSubsystem->NotifyEnemyDefeated(FirstEnemy);
	TestEqual(TEXT("The last defeated Enemy clears a single-Wave Room"),
		CombatSubsystem->GetRoomState(FirstRoomTag), ERoomCombatState::Cleared);
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

#endif
