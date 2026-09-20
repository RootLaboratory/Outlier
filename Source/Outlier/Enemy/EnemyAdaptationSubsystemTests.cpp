#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyAdaptationDefinition.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomTagComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAdaptationRuntimeRegistrationTest,
	"Outlier.Enemy.Adaptation.RuntimeRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAdaptationRuntimeRegistrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Enemy adaptation runtime world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy adaptation runtime world creates an authority game mode"),
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

	UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>();
	UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!TestNotNull(TEXT("Enemy adaptation subsystem is created"), Adaptation)
		|| !TestNotNull(TEXT("Enemy pool subsystem is created"), Pool)
		|| !TestNotNull(TEXT("Arena subsystem is created"), Arena))
	{
		CleanupWorld();
		return false;
	}

	// 자동화 월드는 실제 Arena 표시 이벤트가 없으므로 서버 BeginPlay 상태를 명시적으로 연다.
	Adaptation->OnWorldBeginPlay(*World);

	AEnemyBase* RoomlessEnemy = World->SpawnActor<AEnemyBase>();
	AEnemyBase* RoomEnemy = World->SpawnActor<AEnemyBase>();
	if (!TestNotNull(TEXT("Roomless preplaced Enemy is spawned"), RoomlessEnemy)
		|| !TestNotNull(TEXT("Room-tagged preplaced Enemy is spawned"), RoomEnemy))
	{
		CleanupWorld();
		return false;
	}

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	RoomEnemy->GetRoomTagComp()->AssignDefaultRoomTag(RoomTag);
	TestTrue(TEXT("Adaptation registry accepts server registrations"),
		Adaptation->IsAcceptingRegistrationsForTesting());
	TestTrue(TEXT("Roomless preplaced Enemy has authority"), RoomlessEnemy->HasAuthority());
	TestTrue(TEXT("Roomless preplaced Enemy can register"),
		Adaptation->RegisterEnemy(RoomlessEnemy));
	TestTrue(TEXT("Room-tagged preplaced Enemy can register"),
		Adaptation->RegisterEnemy(RoomEnemy));
	TestTrue(TEXT("Roomless preplaced Enemy is registered"),
		Adaptation->IsEnemyRegistered(RoomlessEnemy));
	TestTrue(TEXT("Room-tagged preplaced Enemy is registered"),
		Adaptation->IsEnemyRegistered(RoomEnemy));
	TestEqual(TEXT("Both preplaced Enemies share the arena registry"),
		Adaptation->GetRegisteredEnemyCount(), 2);

	TestTrue(TEXT("Duplicate registration is accepted"), Adaptation->RegisterEnemy(RoomlessEnemy));
	TestEqual(TEXT("Duplicate registration does not duplicate the Enemy"),
		Adaptation->GetRegisteredEnemyCount(), 2);
	Adaptation->UnregisterEnemy(RoomlessEnemy);
	Adaptation->UnregisterEnemy(RoomlessEnemy);
	TestEqual(TEXT("Repeated unregister is safe"), Adaptation->GetRegisteredEnemyCount(), 1);

	Arena->OnArenaGameplayReloadStarted.Broadcast(42);
	TestEqual(TEXT("Reload start clears active Enemy registrations"),
		Adaptation->GetRegisteredEnemyCount(), 0);
	TestFalse(TEXT("Reloading rejects a late registration"),
		Adaptation->RegisterEnemy(RoomlessEnemy));
	Arena->OnArenaGameplayGCReady.Broadcast(42);
	TestTrue(TEXT("GC verification opens registration for the new Data Layer"),
		Adaptation->RegisterEnemy(RoomlessEnemy));
	Adaptation->UnregisterEnemy(RoomlessEnemy);
	Arena->OnArenaGameplayReady.Broadcast(42);
	TestTrue(TEXT("Gameplay ready accepts the new generation"),
		Adaptation->RegisterEnemy(RoomlessEnemy));

	UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& PoolEntry = PoolDefinition->Entries.AddDefaulted_GetRef();
	PoolEntry.EnemyClass = AEnemyBase::StaticClass();
	PoolEntry.PrewarmCount = 1;
	PoolEntry.MaxCount = 1;
	TestTrue(TEXT("Enemy pool prewarms for adaptation registration"),
		Pool->PrewarmPool(PoolDefinition));

	FEnemyPoolLeaseContext LeaseContext;
	LeaseContext.GameplayGeneration = 42;
	AEnemyBase* PooledEnemy = nullptr;
	for (TActorIterator<AEnemyBase> It(World); It; ++It)
	{
		if (It->IsPoolManaged())
		{
			PooledEnemy = *It;
			break;
		}
	}
	if (!TestNotNull(TEXT("Prewarmed Enemy exists"), PooledEnemy))
	{
		CleanupWorld();
		return false;
	}

	PooledEnemy->SetPoolPresentationAutoCompleteForTesting(false);
	PooledEnemy = Pool->LeaseEnemy(AEnemyBase::StaticClass(), FTransform::Identity, LeaseContext);
	if (!TestNotNull(TEXT("Pooled Enemy can be leased"), PooledEnemy))
	{
		CleanupWorld();
		return false;
	}
	TestEqual(TEXT("Pooled Enemy waits in spawn presentation"),
		PooledEnemy->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	TestFalse(TEXT("Spawn presentation is not registered as active"),
		Adaptation->IsEnemyRegistered(PooledEnemy));

	const int32 LeaseSerial = PooledEnemy->GetPoolLeaseSerial();
	PooledEnemy->CompletePoolSpawnPresentation(LeaseContext.GameplayGeneration, LeaseSerial);
	TestTrue(TEXT("Combat-active pooled Enemy is registered"),
		Adaptation->IsEnemyRegistered(PooledEnemy));
	TestTrue(TEXT("Pooled Enemy returns to the pool"),
		Pool->ReturnEnemy(PooledEnemy, LeaseContext.GameplayGeneration, LeaseSerial));
	TestFalse(TEXT("Returned pooled Enemy is unregistered"),
		Adaptation->IsEnemyRegistered(PooledEnemy));

	Adaptation->ResetActiveEnemies();
	TestEqual(TEXT("Explicit reset clears all active registrations"),
		Adaptation->GetRegisteredEnemyCount(), 0);

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAdaptationDefinitionValidationTest,
	"Outlier.Enemy.Adaptation.DefinitionValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAdaptationDefinitionValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEnemyAdaptationDefinition* Definition = NewObject<UEnemyAdaptationDefinition>();
	{
		FDataValidationContext Context;
		TestEqual(TEXT("Default adaptation settings are valid"),
			Definition->IsDataValid(Context), EDataValidationResult::Valid);
	}

	Definition->ResistanceLevel1Threshold = Definition->ShieldPreviewThreshold;
	{
		FDataValidationContext Context;
		TestEqual(TEXT("Unordered adaptation thresholds are rejected"),
			Definition->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	Definition->ResistanceLevel1Threshold = 8;

	Definition->ResistanceMaxGunDamageMultiplier = 1.1f;
	{
		FDataValidationContext Context;
		TestEqual(TEXT("Gun damage multipliers above one are rejected"),
			Definition->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	Definition->ResistanceMaxGunDamageMultiplier = 0.5f;

	Definition->ResistanceLevel1BreakStunSeconds = -0.1f;
	{
		FDataValidationContext Context;
		TestEqual(TEXT("Negative break stun durations are rejected"),
			Definition->IsDataValid(Context), EDataValidationResult::Invalid);
	}

	return true;
}

#endif
