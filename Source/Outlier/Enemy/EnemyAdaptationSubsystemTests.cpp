#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyAdaptationDefinition.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GAS/OutlierAbilitySystemComponent.h"
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
	TestTrue(TEXT("Runtime stack can enter resistance before a pooled Enemy activates"),
		Adaptation->SetGunAdaptationStack(8));

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
	TestEqual(TEXT("Newly activated pooled Enemy receives the current resistance state"),
		PooledEnemy->GetAdaptationState(), EEnemyAdaptationState::ResistanceLevel1);
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
	TestEqual(TEXT("Returned pooled Enemy clears its shield presentation state"),
		PooledEnemy->GetAdaptationState(), EEnemyAdaptationState::Normal);

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
	TestEqual(TEXT("Reused pooled Enemy receives the state reached by the previous kill"),
		PooledEnemy->GetAdaptationState(), EEnemyAdaptationState::ResistanceLevel2);
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
	TestEqual(TEXT("Registered pooled Enemy follows later stack changes"),
		PooledEnemy->GetAdaptationState(), EEnemyAdaptationState::ResistanceMax);

	Adaptation->ResetActiveEnemies();
	TestEqual(TEXT("Explicit reset clears all active registrations"),
		Adaptation->GetRegisteredEnemyCount(), 0);
	TestEqual(TEXT("Explicit reset clears the Enemy presentation state"),
		PooledEnemy->GetAdaptationState(), EEnemyAdaptationState::Normal);

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
	TestEqual(TEXT("Shield break damage is disabled by default"),
		Definition->ShieldBreakDamage, 0.0f);
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
	Definition->ResistanceLevel1BreakStunSeconds = 0.5f;

	Definition->ShieldBreakDamage = -1.0f;
	{
		FDataValidationContext Context;
		TestEqual(TEXT("Negative shield break damage is rejected"),
			Definition->IsDataValid(Context), EDataValidationResult::Invalid);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAdaptationBreakFieldTest,
	"Outlier.Enemy.Adaptation.BreakField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAdaptationBreakFieldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Enemy adaptation break world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy adaptation break world creates an authority game mode"),
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
	if (!TestNotNull(TEXT("Enemy adaptation subsystem is created"), Adaptation))
	{
		CleanupWorld();
		return false;
	}

	Adaptation->OnWorldBeginPlay(*World);
	UEnemyAdaptationDefinition* Definition = NewObject<UEnemyAdaptationDefinition>(World);
	Definition->ShieldBreakDamage = 12.0f;
	Adaptation->SetDefinitionForTesting(Definition);
	const FGameplayTag TargetTag = OutlierGameplayTags::Enemy::Adaptation::Target();
	auto SpawnEnemy = [World, Adaptation, TargetTag](
		bool bAdaptationTarget,
		bool bEnterCombat,
		bool bPossessed)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(
			AEnemyBase::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (!Enemy)
		{
			return Enemy;
		}

		if (!bAdaptationTarget)
		{
			Enemy->RemoveEnemyTraitForTesting(TargetTag);
		}
		Enemy->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
		if (bEnterCombat)
		{
			Enemy->EnterCombat(FVector::ZeroVector);
		}
		if (bPossessed)
		{
			Enemy->SetEnemyPossessed(true);
		}
		Adaptation->RegisterEnemy(Enemy);
		return Enemy;
	};
	auto HasStun = [](const AEnemyBase* Enemy)
	{
		return Enemy
			&& Enemy->GetOutlierAbilitySystemComponent()->HasMatchingGameplayTag(
				OutlierGameplayTags::State::Stunned());
	};
	auto ReportNonGunBreak = [Adaptation](
		AEnemyBase* SourceEnemy,
		FEnemyAdaptationUpdateResult& OutResult)
	{
		return Adaptation->ReportEnemyDefeat(
			SourceEnemy,
			0,
			0,
			EEnemyFinalKillCategory::NonGun,
			OutResult);
	};

	AEnemyBase* BreakSource = SpawnEnemy(true, false, false);
	AEnemyBase* AdaptationCombatEnemy = SpawnEnemy(true, true, false);
	AEnemyBase* GeneralCombatEnemy = SpawnEnemy(false, true, false);
	AEnemyBase* NonCombatEnemy = SpawnEnemy(true, false, false);
	AEnemyBase* PossessedCombatEnemy = SpawnEnemy(true, true, true);
	if (!TestNotNull(TEXT("Break source is spawned"), BreakSource)
		|| !TestNotNull(TEXT("Adaptation combat target is spawned"), AdaptationCombatEnemy)
		|| !TestNotNull(TEXT("General combat target is spawned"), GeneralCombatEnemy)
		|| !TestNotNull(TEXT("Non-combat target is spawned"), NonCombatEnemy)
		|| !TestNotNull(TEXT("Possessed combat target is spawned"), PossessedCombatEnemy))
	{
		CleanupWorld();
		return false;
	}

	FEnemyAdaptationUpdateResult Result;
	TestTrue(TEXT("Stack enters resistance for field break"),
		Adaptation->SetGunAdaptationStack(8));
	TestTrue(TEXT("NonGun defeat triggers the field break"),
		ReportNonGunBreak(BreakSource, Result));
	TestTrue(TEXT("Field break is reported"), Result.bAdaptationBroken);
	TestEqual(TEXT("Field break resets the shared stack"), Result.CurrentStack, 0);
	TestEqual(TEXT("Combat adaptation target receives break damage"),
		AdaptationCombatEnemy->GetCurrentHealth(), 88.0f);
	TestTrue(TEXT("Combat adaptation target receives break stun"),
		HasStun(AdaptationCombatEnemy));
	TestEqual(TEXT("Combat target without adaptation trait receives break damage"),
		GeneralCombatEnemy->GetCurrentHealth(), 88.0f);
	TestTrue(TEXT("Combat target without adaptation trait receives break stun"),
		HasStun(GeneralCombatEnemy));
	TestEqual(TEXT("Non-combat target is excluded from break damage"),
		NonCombatEnemy->GetCurrentHealth(), 100.0f);
	TestFalse(TEXT("Non-combat target is excluded from break stun"),
		HasStun(NonCombatEnemy));
	TestEqual(TEXT("Player-team possessed target is excluded from break damage"),
		PossessedCombatEnemy->GetCurrentHealth(), 100.0f);
	TestFalse(TEXT("Player-team possessed target is excluded from break stun"),
		HasStun(PossessedCombatEnemy));

	Adaptation->ResetActiveEnemies();
	Definition->ShieldBreakDamage = 0.0f;
	AEnemyBase* ZeroDamageSource = SpawnEnemy(true, false, false);
	AEnemyBase* ZeroDamageTarget = SpawnEnemy(true, true, false);
	if (!TestNotNull(TEXT("Zero-damage break source is spawned"), ZeroDamageSource)
		|| !TestNotNull(TEXT("Zero-damage break target is spawned"), ZeroDamageTarget))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Stack enters level 2 resistance for zero-damage break"),
		Adaptation->SetGunAdaptationStack(9));
	TestTrue(TEXT("Zero-damage field break is accepted"),
		ReportNonGunBreak(ZeroDamageSource, Result));
	TestEqual(TEXT("Zero-damage break preserves target health"),
		ZeroDamageTarget->GetCurrentHealth(), 100.0f);
	TestTrue(TEXT("Zero-damage break still applies stun"), HasStun(ZeroDamageTarget));

	Adaptation->ResetActiveEnemies();
	AEnemyBase* LethalBreakSource = SpawnEnemy(true, false, false);
	AEnemyBase* LethalBreakTarget = SpawnEnemy(true, true, false);
	if (!TestNotNull(TEXT("Lethal break source is spawned"), LethalBreakSource)
		|| !TestNotNull(TEXT("Lethal break target is spawned"), LethalBreakTarget))
	{
		CleanupWorld();
		return false;
	}
	// Enemy의 최종 체력은 스폰 시 Stat Row 초기화 결과를 따른다. 고정 피해량 대신
	// 현재 체력을 사용해 이 구간이 항상 파열 피해의 사망 경로를 검증하도록 한다.
	Definition->ShieldBreakDamage = LethalBreakTarget->GetCurrentHealth();
	TestTrue(TEXT("Stack enters max resistance for lethal field break"),
		Adaptation->SetGunAdaptationStack(10));
	TestTrue(TEXT("Lethal field break is accepted"),
		ReportNonGunBreak(LethalBreakSource, Result));
	TestEqual(TEXT("Lethal break damage cannot change the reset stack"),
		Adaptation->GetCurrentGunAdaptationStack(), 0);
	TestEqual(TEXT("Lethal break damage clamps target health to zero"),
		LethalBreakTarget->GetCurrentHealth(), 0.0f);
	TestTrue(TEXT("Lethal break target enters death"),
		!IsValid(LethalBreakTarget) || LethalBreakTarget->IsDead());

	CleanupWorld();
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
	Definition->ShieldBreakDamage = 12.0f;
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
	AEnemyBase* DamageEnemy = SpawnEnemy(true);
	if (!TestNotNull(TEXT("Adaptation damage target is spawned"), DamageEnemy))
	{
		CleanupWorld();
		return false;
	}
	UOutlierAbilitySystemComponent* DamageAbilitySystem =
		DamageEnemy->GetOutlierAbilitySystemComponent();
	if (!TestNotNull(TEXT("Adaptation damage target has an ASC"), DamageAbilitySystem))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Adaptation damage target initializes vitality"),
		DamageAbilitySystem->InitializeVitalityToSelf(100.0f));
	TestTrue(TEXT("Resistance stack can be restored for damage multiplier test"),
		Adaptation->SetGunAdaptationStack(8));
	TestEqual(TEXT("Registered Enemy receives resistance level 1 presentation"),
		DamageEnemy->GetAdaptationState(), EEnemyAdaptationState::ResistanceLevel1);
	FOutlierDamageRequest GunDamageRequest;
	GunDamageRequest.DamageAmount = 20.0f;
	GunDamageRequest.DamageTag = OutlierGameplayTags::Damage::Weapon();
	GunDamageRequest.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Gun;
	TestTrue(TEXT("Gun damage uses the current resistance multiplier"),
		FMath::IsNearlyEqual(DamageEnemy->ReceiveOutlierDamage(GunDamageRequest), 18.0f));

	FOutlierDamageRequest NonGunDamageRequest = GunDamageRequest;
	NonGunDamageRequest.DamageAmount = 10.0f;
	NonGunDamageRequest.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::NonGun;
	TestTrue(TEXT("NonGun damage bypasses gun resistance"),
		FMath::IsNearlyEqual(DamageEnemy->ReceiveOutlierDamage(NonGunDamageRequest), 10.0f));
	TestEqual(TEXT("NonGun hit alone does not break resistance"),
		Adaptation->GetCurrentGunAdaptationStack(), 8);

	FOutlierDamageRequest PistolDamageRequest = NonGunDamageRequest;
	PistolDamageRequest.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Pistol;
	TestTrue(TEXT("Pistol damage bypasses gun resistance"),
		FMath::IsNearlyEqual(DamageEnemy->ReceiveOutlierDamage(PistolDamageRequest), 10.0f));
	TestEqual(TEXT("Pistol hit immediately breaks active resistance"),
		Adaptation->GetCurrentGunAdaptationStack(), 0);
	TestEqual(TEXT("Pistol break clears the registered Enemy presentation"),
		DamageEnemy->GetAdaptationState(), EEnemyAdaptationState::Normal);

	AEnemyBase* GunDamageKillEnemy = SpawnEnemy(true);
	if (!TestNotNull(TEXT("Gun kill target is spawned"), GunDamageKillEnemy))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Stack can be restored for Enemy death-path Gun kill"),
		Adaptation->SetGunAdaptationStack(6));
	GunDamageKillEnemy->BeginDeathForAdaptationTesting(
		EOutlierAdaptationDamageCategory::Gun);
	TestEqual(TEXT("Enemy death-path Gun kill increments the shared stack"),
		Adaptation->GetCurrentGunAdaptationStack(), 7);

	AEnemyBase* PreviewPistolKillEnemy = SpawnEnemy(true);
	if (!TestNotNull(TEXT("Preview pistol kill target is spawned"), PreviewPistolKillEnemy))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Stack can be restored for Enemy death-path preview pistol kill"),
		Adaptation->SetGunAdaptationStack(7));
	PreviewPistolKillEnemy->BeginDeathForAdaptationTesting(
		EOutlierAdaptationDamageCategory::Pistol);
	TestEqual(TEXT("Enemy death-path preview pistol kill decrements stack 7 to 5"),
		Adaptation->GetCurrentGunAdaptationStack(), 5);

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
	if (TestNotNull(TEXT("Enemy without the adaptation target tag is spawned"), ExcludedEnemy))
	{
		TestEqual(TEXT("Enemy without the adaptation target tag keeps no shield presentation"),
			ExcludedEnemy->GetAdaptationState(), EEnemyAdaptationState::Normal);
	}
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
		TestEqual(TEXT("Resistance break exposes the configured break damage"),
			Result.BreakDamage, Definition->ShieldBreakDamage);
	}

	AEnemyBase* PreviewPistolEnemy = SpawnEnemy(true);
	TestTrue(TEXT("Preview stack can be restored for pistol test"),
		Adaptation->SetGunAdaptationStack(7));
	TestFalse(TEXT("Pistol hit does not break the preview shield"),
		Adaptation->ReportPistolHit(PreviewPistolEnemy, 0, 0, false, Result));
	TestEqual(TEXT("Preview pistol hit keeps stack 7"),
		Adaptation->GetCurrentGunAdaptationStack(), 7);
	TestTrue(TEXT("Preview pistol kill follows the normal NonGun rule"),
		Report(PreviewPistolEnemy, EEnemyFinalKillCategory::NonGun, Result));
	TestEqual(TEXT("Preview pistol kill decrements stack 7 to 5"),
		Result.CurrentStack, 5);

	for (const TPair<int32, float>& BreakCase : BreakCases)
	{
		AEnemyBase* PistolBreakEnemy = SpawnEnemy(true);
		TestTrue(TEXT("Resistance stack can be restored for pistol hit"),
			Adaptation->SetGunAdaptationStack(BreakCase.Key));
		TestTrue(TEXT("Pistol hit immediately breaks active resistance"),
			Adaptation->ReportPistolHit(PistolBreakEnemy, 0, 0, false, Result));
		TestEqual(TEXT("Pistol break resets the stack"), Result.CurrentStack, 0);
		TestTrue(TEXT("Pistol hit reports an adaptation break"),
			Result.bAdaptationBroken);
		TestEqual(TEXT("Pistol break resolves the configured stun duration"),
			Result.BreakStunSeconds, BreakCase.Value);
		TestEqual(TEXT("Pistol break exposes the configured break damage"),
			Result.BreakDamage, Definition->ShieldBreakDamage);
		TestTrue(TEXT("Nonlethal pistol break keeps the Enemy defeat report available"),
			Report(PistolBreakEnemy, EEnemyFinalKillCategory::Gun, Result));
	}

	AEnemyBase* LethalPistolEnemy = SpawnEnemy(true);
	TestTrue(TEXT("Resistance stack can be restored for lethal pistol hit"),
		Adaptation->SetGunAdaptationStack(8));
	TestTrue(TEXT("Lethal pistol hit breaks resistance and consumes the defeat"),
		Adaptation->ReportPistolHit(LethalPistolEnemy, 0, 0, true, Result));
	TestFalse(TEXT("Lethal pistol hit cannot report a second NonGun defeat"),
		Report(LethalPistolEnemy, EEnemyFinalKillCategory::NonGun, Result));

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
	AEnemyBase* LateRegisteredEnemy = SpawnEnemy(true);
	if (TestNotNull(TEXT("Late adaptation target is spawned"), LateRegisteredEnemy))
	{
		TestEqual(TEXT("Late registered Enemy immediately receives the current max state"),
			LateRegisteredEnemy->GetAdaptationState(), EEnemyAdaptationState::ResistanceMax);
	}
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
