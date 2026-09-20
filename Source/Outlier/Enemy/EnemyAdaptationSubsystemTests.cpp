#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyAdaptationDefinition.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTags/OutlierGameplayTags.h"
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
	// GameplayReady는 프로젝트 설정의 Definition을 다시 읽으므로 런타임 수명 검증 직전에
	// 자동화 전용 Definition을 주입한다.
	Adaptation->SetDefinitionForTesting(NewObject<UEnemyAdaptationDefinition>(World));

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
	FEnemyAdaptationUpdateResult DefeatResult;
	TestTrue(TEXT("Current pooled lease can report a defeat"),
		Adaptation->ReportEnemyDefeat(
			PooledEnemy,
			LeaseContext.GameplayGeneration,
			LeaseSerial,
			EEnemyFinalKillCategory::Gun,
			DefeatResult));
	TestTrue(TEXT("Pooled Enemy returns to the pool"),
		Pool->ReturnEnemy(PooledEnemy, LeaseContext.GameplayGeneration, LeaseSerial));
	TestFalse(TEXT("Returned pooled Enemy is unregistered"),
		Adaptation->IsEnemyRegistered(PooledEnemy));

	PooledEnemy = Pool->LeaseEnemy(AEnemyBase::StaticClass(), FTransform::Identity, LeaseContext);
	if (!TestNotNull(TEXT("Pooled Enemy can be leased again"), PooledEnemy))
	{
		CleanupWorld();
		return false;
	}
	const int32 NewLeaseSerial = PooledEnemy->GetPoolLeaseSerial();
	PooledEnemy->CompletePoolSpawnPresentation(
		LeaseContext.GameplayGeneration,
		NewLeaseSerial);
	TestFalse(TEXT("Previous pooled lease defeat is rejected after reuse"),
		Adaptation->ReportEnemyDefeat(
			PooledEnemy,
			LeaseContext.GameplayGeneration,
			LeaseSerial,
			EEnemyFinalKillCategory::Gun,
			DefeatResult));
	TestFalse(TEXT("Previous gameplay generation is rejected after reuse"),
		Adaptation->ReportEnemyDefeat(
			PooledEnemy,
			LeaseContext.GameplayGeneration - 1,
			NewLeaseSerial,
			EEnemyFinalKillCategory::Gun,
			DefeatResult));
	TestTrue(TEXT("Current reused lease can report a defeat"),
		Adaptation->ReportEnemyDefeat(
			PooledEnemy,
			LeaseContext.GameplayGeneration,
			NewLeaseSerial,
			EEnemyFinalKillCategory::Gun,
			DefeatResult));

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
	TestEqual(TEXT("Stack 0 resolves to Normal"),
		Definition->ResolveState(0), EEnemyAdaptationState::Normal);
	TestEqual(TEXT("Stack 6 resolves to Normal"),
		Definition->ResolveState(6), EEnemyAdaptationState::Normal);
	TestEqual(TEXT("Stack 7 resolves to ShieldPreview"),
		Definition->ResolveState(7), EEnemyAdaptationState::ShieldPreview);
	TestEqual(TEXT("Stack 8 resolves to ResistanceLevel1"),
		Definition->ResolveState(8), EEnemyAdaptationState::ResistanceLevel1);
	TestEqual(TEXT("Stack 9 resolves to ResistanceLevel2"),
		Definition->ResolveState(9), EEnemyAdaptationState::ResistanceLevel2);
	TestEqual(TEXT("Stack 10 resolves to ResistanceMax"),
		Definition->ResolveState(10), EEnemyAdaptationState::ResistanceMax);
	TestEqual(TEXT("Stack clamps below zero"), Definition->ClampStack(-1), 0);
	TestEqual(TEXT("Stack clamps above max"), Definition->ClampStack(11), 10);
	TestEqual(TEXT("Preview does not reduce gun damage"),
		Definition->ResolveGunDamageMultiplier(7), 1.0f);
	TestEqual(TEXT("Resistance level 1 resolves gun multiplier"),
		Definition->ResolveGunDamageMultiplier(8), 0.9f);
	TestEqual(TEXT("Resistance level 2 resolves gun multiplier"),
		Definition->ResolveGunDamageMultiplier(9), 0.8f);
	TestEqual(TEXT("Resistance max resolves gun multiplier"),
		Definition->ResolveGunDamageMultiplier(10), 0.5f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAdaptationStackStateTest,
	"Outlier.Enemy.Adaptation.StackState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAdaptationStackStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Enemy adaptation stack world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy adaptation stack world creates an authority game mode"),
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
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!TestNotNull(TEXT("Enemy adaptation subsystem is created"), Adaptation)
		|| !TestNotNull(TEXT("Arena subsystem is created"), Arena))
	{
		CleanupWorld();
		return false;
	}

	Adaptation->OnWorldBeginPlay(*World);
	UEnemyAdaptationDefinition* Definition = NewObject<UEnemyAdaptationDefinition>(World);
	Adaptation->SetDefinitionForTesting(Definition);
	const FGameplayTag TargetTag = OutlierGameplayTags::Enemy::Adaptation::Target();
	auto SpawnEnemy = [World, Adaptation, TargetTag](bool bAdaptationTarget)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(
			AEnemyBase::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (Enemy && !bAdaptationTarget)
		{
			Enemy->RemoveEnemyTraitForTesting(TargetTag);
		}
		if (Enemy)
		{
			Adaptation->RegisterEnemy(Enemy);
		}
		return Enemy;
	};
	auto Report = [Adaptation](
		AEnemyBase* Enemy,
		EEnemyFinalKillCategory Category,
		FEnemyAdaptationUpdateResult& Result)
	{
		return Adaptation->ReportEnemyDefeat(Enemy, 0, 0, Category, Result);
	};

	FEnemyAdaptationUpdateResult Result;
	AEnemyBase* GunEnemy = SpawnEnemy(true);
	TestNotNull(TEXT("Gun kill target is spawned"), GunEnemy);
	TestTrue(TEXT("Stack can be restored before processing kills"),
		Adaptation->SetGunAdaptationStack(6));
	TestTrue(TEXT("Gun kill is accepted"),
		Report(GunEnemy, EEnemyFinalKillCategory::Gun, Result));
	TestEqual(TEXT("Gun kill increments the shared stack"), Result.CurrentStack, 7);
	TestEqual(TEXT("Gun kill enters shield preview"),
		Result.CurrentState, EEnemyAdaptationState::ShieldPreview);
	TestFalse(TEXT("Duplicate defeat report is rejected"),
		Report(GunEnemy, EEnemyFinalKillCategory::Gun, Result));
	TestEqual(TEXT("Duplicate report does not change the stack"),
		Adaptation->GetCurrentGunAdaptationStack(), 7);

	AEnemyBase* ExcludedEnemy = SpawnEnemy(false);
	TestFalse(TEXT("Enemy without the adaptation target tag is excluded"),
		Report(ExcludedEnemy, EEnemyFinalKillCategory::Gun, Result));
	TestEqual(TEXT("Excluded Enemy does not change the stack"),
		Adaptation->GetCurrentGunAdaptationStack(), 7);

	AEnemyBase* PreviewBreakEnemy = SpawnEnemy(true);
	TestTrue(TEXT("Preview NonGun kill is accepted"),
		Report(PreviewBreakEnemy, EEnemyFinalKillCategory::NonGun, Result));
	TestEqual(TEXT("Stack 7 NonGun kill decrements to 5"), Result.CurrentStack, 5);
	TestFalse(TEXT("Preview NonGun kill does not trigger adaptation break"),
		Result.bAdaptationBroken);

	const TArray<TPair<int32, float>> BreakCases = {
		TPair<int32, float>(8, 0.5f),
		TPair<int32, float>(9, 0.75f),
		TPair<int32, float>(10, 1.0f)
	};
	for (const TPair<int32, float>& BreakCase : BreakCases)
	{
		AEnemyBase* BreakEnemy = SpawnEnemy(true);
		TestTrue(TEXT("Resistance stack can be restored"),
			Adaptation->SetGunAdaptationStack(BreakCase.Key));
		TestTrue(TEXT("Resistance NonGun kill is accepted"),
			Report(BreakEnemy, EEnemyFinalKillCategory::NonGun, Result));
		TestEqual(TEXT("Resistance break resets the stack"), Result.CurrentStack, 0);
		TestTrue(TEXT("Resistance NonGun kill triggers adaptation break"),
			Result.bAdaptationBroken);
		TestEqual(TEXT("Resistance break resolves the configured stun duration"),
			Result.BreakStunSeconds, BreakCase.Value);
	}

	AEnemyBase* IgnoreEnemy = SpawnEnemy(true);
	TestTrue(TEXT("Stack can be restored for Ignore test"),
		Adaptation->SetGunAdaptationStack(4));
	TestTrue(TEXT("Ignore final category is consumed"),
		Report(IgnoreEnemy, EEnemyFinalKillCategory::Ignore, Result));
	TestEqual(TEXT("Ignore does not change the stack"), Result.CurrentStack, 4);
	TestFalse(TEXT("Ignore report is still consumed only once"),
		Report(IgnoreEnemy, EEnemyFinalKillCategory::Gun, Result));

	AEnemyBase* StaleEnemy = SpawnEnemy(true);
	TestFalse(TEXT("Stale generation is rejected"),
		Adaptation->ReportEnemyDefeat(
			StaleEnemy, 1, 0, EEnemyFinalKillCategory::Gun, Result));
	TestFalse(TEXT("Stale lease serial is rejected"),
		Adaptation->ReportEnemyDefeat(
			StaleEnemy, 0, 1, EEnemyFinalKillCategory::Gun, Result));
	TestTrue(TEXT("Current unmanaged lifecycle remains reportable after stale callbacks"),
		Report(StaleEnemy, EEnemyFinalKillCategory::Gun, Result));

	AEnemyBase* LowStackEnemy = SpawnEnemy(true);
	TestTrue(TEXT("Low stack can be restored for decrement clamp test"),
		Adaptation->SetGunAdaptationStack(1));
	TestTrue(TEXT("Low stack NonGun kill is accepted"),
		Report(LowStackEnemy, EEnemyFinalKillCategory::NonGun, Result));
	TestEqual(TEXT("NonGun decrement clamps at zero"), Result.CurrentStack, 0);

	AEnemyBase* MaxStackEnemy = SpawnEnemy(true);
	TestTrue(TEXT("Stack clamps to the configured maximum"),
		Adaptation->SetGunAdaptationStack(100));
	TestEqual(TEXT("Restored stack is clamped"),
		Adaptation->GetCurrentGunAdaptationStack(), 10);
	TestTrue(TEXT("Gun kill at max stack is accepted"),
		Report(MaxStackEnemy, EEnemyFinalKillCategory::Gun, Result));
	TestEqual(TEXT("Gun increment clamps at the configured maximum"),
		Result.CurrentStack, 10);
	Arena->OnArenaGameplayReloadStarted.Broadcast(42);
	TestEqual(TEXT("Gameplay reload preserves the shared stack"),
		Adaptation->GetCurrentGunAdaptationStack(), 10);
	TestEqual(TEXT("Gameplay reload clears only active registrations"),
		Adaptation->GetRegisteredEnemyCount(), 0);
	Arena->OnArenaReleased.Broadcast();
	TestEqual(TEXT("Arena release resets the shared stack"),
		Adaptation->GetCurrentGunAdaptationStack(), 0);
	TestEqual(TEXT("Arena release resets the resolved state"),
		Adaptation->GetCurrentAdaptationState(), EEnemyAdaptationState::Normal);

	CleanupWorld();
	return true;
}

#endif
