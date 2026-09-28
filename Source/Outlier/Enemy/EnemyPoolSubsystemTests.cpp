#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Room/RoomTagComponent.h"
#include "StateTree.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyPoolRuntimeTest,
	"Outlier.Enemy.PoolRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPoolRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Enemy pool runtime world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy pool runtime world creates an authority game mode"),
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

	UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!TestNotNull(TEXT("Enemy pool subsystem is created"), Pool))
	{
		CleanupWorld();
		return false;
	}

	UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& Entry = Definition->Entries.AddDefaulted_GetRef();
	Entry.EnemyClass = AEnemyBase::StaticClass();
	Entry.PrewarmCount = 1;
	Entry.MaxCount = 2;
	TestTrue(TEXT("Configured pool prewarms"), Pool->PrewarmPool(Definition));
	TestEqual(TEXT("Prewarm creates one Enemy"), Pool->GetTotalCount(AEnemyBase::StaticClass()), 1);
	TestEqual(TEXT("Prewarmed Enemy is idle"), Pool->GetIdleCount(AEnemyBase::StaticClass()), 1);

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FVector SharedTargetLocation(700.0, 800.0, 900.0);
	UEnemyRoomSubsystem* RoomSubsystem = World->GetSubsystem<UEnemyRoomSubsystem>();
	if (TestNotNull(TEXT("Enemy room subsystem is created"), RoomSubsystem))
	{
		RoomSubsystem->SetActiveRoomTargetForTesting(RoomTag, SharedTargetLocation);
		TestTrue(TEXT("Room target fixture marks the room in combat"),
			RoomSubsystem->IsRoomInCombat(RoomTag));
	}
	FEnemyPoolLeaseContext FirstContext;
	FirstContext.RoomTag = RoomTag;
	FirstContext.CombatPhaseIndex = 1;
	FirstContext.WaveIndex = 2;
	FirstContext.GameplayGeneration = 10;
	AEnemyBase* FirstLease = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(),
		FTransform(FVector(100.0, 200.0, 300.0)),
		FirstContext);
	if (!TestNotNull(TEXT("Prewarmed Enemy can be leased"), FirstLease))
	{
		CleanupWorld();
		return false;
	}

	TestEqual(TEXT("Default presentation enters combat"),
		FirstLease->GetEnemyPoolState(), EEnemyPoolState::CombatActive);
	TestTrue(TEXT("Combat-active Enemy accepts damage"), FirstLease->CanBeDamaged());
	TestTrue(TEXT("Combat-active Enemy enables collision"), FirstLease->GetActorEnableCollision());
	TestEqual(TEXT("Lease applies runtime RoomTag"), FirstLease->GetDefaultRoomTag(), RoomTag);
	TestEqual(TEXT("Reinforcement starts in NonCombat even in an active room"),
		FirstLease->GetCombatState(), EEnemyCombatState::NonCombat);
	TestFalse(TEXT("Reinforcement does not inherit room target before perception"),
		FirstLease->HasSharedTargetContact());
	const ECollisionEnabled::Type CapsuleCollision = FirstLease->GetCapsuleComponent()->GetCollisionEnabled();
	const ECollisionEnabled::Type MeshCollision = FirstLease->GetMesh()->GetCollisionEnabled();
	const ECollisionEnabled::Type CoreCollision = FirstLease->GetCoreHitboxComponent()->GetCollisionEnabled();
	const bool bMeshVisible = FirstLease->GetMesh()->IsVisible();
	const bool bMeshHiddenInGame = FirstLease->GetMesh()->bHiddenInGame;
	const bool bCoreVisible = FirstLease->GetCoreHitboxComponent()->IsVisible();
	FirstLease->SimulateDeathPresentationForPoolTesting();
	TestFalse(TEXT("Death presentation hides the source mesh"), FirstLease->GetMesh()->IsVisible());
	TestEqual(TEXT("Death presentation disables the capsule"),
		FirstLease->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	const int32 FirstLeaseSerial = FirstLease->GetPoolLeaseSerial();
	TestTrue(TEXT("Active Enemy returns to the pool"),
		Pool->ReturnEnemy(FirstLease, FirstContext.GameplayGeneration, FirstLeaseSerial));
	TestEqual(TEXT("Returned Enemy is idle"), FirstLease->GetEnemyPoolState(), EEnemyPoolState::Idle);
	TestTrue(TEXT("Returned Enemy is hidden"), FirstLease->IsHidden());
	TestFalse(TEXT("Returned Enemy cannot be damaged"), FirstLease->CanBeDamaged());
	TestFalse(TEXT("Returned Enemy collision is disabled"), FirstLease->GetActorEnableCollision());
	TestFalse(TEXT("Returned Enemy clears RoomTag"), FirstLease->GetDefaultRoomTag().IsValid());

	FirstLease->SetPoolPresentationAutoCompleteForTesting(false);
	// 마지막 직접 관측자가 시야를 잃은 뒤 재사용해도 방의 마지막 위치로 선행 경계하지 않는다.
	if (RoomSubsystem)
	{
		RoomSubsystem->SetActiveRoomTargetForTesting(RoomTag, SharedTargetLocation, false);
	}
	FEnemyPoolLeaseContext SecondContext = FirstContext;
	SecondContext.GameplayGeneration = 11;
	SecondContext.CombatPhaseIndex = 3;
	SecondContext.WaveIndex = 4;
	AEnemyBase* SecondLease = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(),
		FTransform(FVector(400.0, 500.0, 600.0)),
		SecondContext);
	TestTrue(TEXT("The same Actor is reused"), SecondLease == FirstLease);
	TestEqual(TEXT("Enemy waits for the explicit spawn callback"),
		SecondLease->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	TestEqual(TEXT("Spawn presentation does not enter Alert before activation"),
		SecondLease->GetCombatState(), EEnemyCombatState::NonCombat);
	TestFalse(TEXT("Spawn presentation blocks damage"), SecondLease->CanBeDamaged());
	TestFalse(TEXT("Spawn presentation blocks collision"), SecondLease->GetActorEnableCollision());
	TestFalse(TEXT("Previous Dead state is not retained"), SecondLease->IsDead());
	// 연출 대기 중에는 Actor 충돌이 꺼져 GetCollisionEnabled가 NoCollision을 반환한다.
	// 컴포넌트 충돌 복원은 현재 대여가 전투 상태로 활성화된 뒤 확인한다.
	TestEqual(TEXT("Reused Enemy restores source mesh visibility"),
		SecondLease->GetMesh()->IsVisible(), bMeshVisible);
	TestEqual(TEXT("Reused Enemy restores child visibility"),
		SecondLease->GetCoreHitboxComponent()->IsVisible(), bCoreVisible);
	TestEqual(TEXT("Reused Enemy restores source mesh hidden state"),
		static_cast<bool>(SecondLease->GetMesh()->bHiddenInGame), bMeshHiddenInGame);

	const int32 SecondLeaseSerial = SecondLease->GetPoolLeaseSerial();
	TestFalse(TEXT("An old lease cannot return a reused Actor"),
		Pool->ReturnEnemy(SecondLease, FirstContext.GameplayGeneration, FirstLeaseSerial));
	TestEqual(TEXT("Rejected return keeps the current lease registered"),
		Pool->GetLeasedCount(AEnemyBase::StaticClass()), 1);
	SecondLease->CompletePoolSpawnPresentation(FirstContext.GameplayGeneration, FirstLeaseSerial);
	TestEqual(TEXT("A stale spawn callback cannot activate a new lease"),
		SecondLease->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	SecondLease->CompletePoolSpawnPresentation(SecondContext.GameplayGeneration, SecondLeaseSerial);
	TestEqual(TEXT("The current callback activates combat"),
		SecondLease->GetEnemyPoolState(), EEnemyPoolState::CombatActive);
	TestEqual(TEXT("Reused Enemy restores capsule collision"),
		SecondLease->GetCapsuleComponent()->GetCollisionEnabled(), CapsuleCollision);
	TestEqual(TEXT("Reused Enemy restores mesh collision"),
		SecondLease->GetMesh()->GetCollisionEnabled(), MeshCollision);
	TestEqual(TEXT("Reused Enemy restores core collision"),
		SecondLease->GetCoreHitboxComponent()->GetCollisionEnabled(), CoreCollision);
	TestEqual(TEXT("Reused reinforcement also starts in NonCombat"),
		SecondLease->GetCombatState(), EEnemyCombatState::NonCombat);
	TestFalse(TEXT("Lost sight is not restored as shared contact"),
		SecondLease->HasSharedTargetContact());
	TestEqual(TEXT("Room history does not pre-alert a reused reinforcement"),
		SecondLease->GetLastKnownPlayerLocation(), FVector::ZeroVector);

	UOutlierAbilitySystemComponent* ASC = SecondLease->GetOutlierAbilitySystemComponent();
	if (TestNotNull(TEXT("Pooled Enemy has an ASC"), ASC))
	{
		TestTrue(TEXT("The death fixture applies the Dead GAS state"),
			ASC->ApplyDeadStateToSelf());
		SecondLease->BeginDeathForPoolTesting();
		TestEqual(TEXT("Pool death waits in presentation state"),
			SecondLease->GetEnemyPoolState(), EEnemyPoolState::DeathPresentation);
		SecondLease->CompletePoolDeathPresentation(
			SecondContext.GameplayGeneration,
			SecondLeaseSerial);
		TestEqual(TEXT("Death presentation returns instead of destroying"),
			SecondLease->GetEnemyPoolState(), EEnemyPoolState::Idle);
		TestFalse(TEXT("Returned pooled Enemy is not destroyed"), SecondLease->IsActorBeingDestroyed());
	}

	FirstLease->SetPoolPresentationAutoCompleteForTesting(true);
	AEnemyBase* MaxLeaseA = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(), FTransform::Identity, FirstContext);
	AEnemyBase* MaxLeaseB = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(), FTransform::Identity, FirstContext);
	AEnemyBase* ExhaustedLease = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(), FTransform::Identity, FirstContext);
	TestNotNull(TEXT("Pool reuses the returned Enemy"), MaxLeaseA);
	TestNotNull(TEXT("Pool expands up to MaxCount"), MaxLeaseB);
	TestNull(TEXT("Pool refuses to exceed MaxCount"), ExhaustedLease);
	TestEqual(TEXT("Pool total stops at MaxCount"), Pool->GetTotalCount(AEnemyBase::StaticClass()), 2);
	TestTrue(TEXT("Prewarming during combat does not require new Actors"), Pool->PrewarmPool(Definition));
	TestEqual(TEXT("Prewarm includes leased Actors in its target count"),
		Pool->GetTotalCount(AEnemyBase::StaticClass()), 2);
	if (MaxLeaseA)
	{
		TestFalse(TEXT("A repeated lease clears the previous Dead state"), MaxLeaseA->IsDead());
		MaxLeaseA->CompletePoolDeathPresentation(
			SecondContext.GameplayGeneration,
			SecondLeaseSerial);
		TestEqual(TEXT("A stale death callback cannot return a newer lease"),
			MaxLeaseA->GetEnemyPoolState(), EEnemyPoolState::CombatActive);
	}

	Pool->DestroyPool();
	TestEqual(TEXT("DestroyPool clears all buckets"), Pool->GetTotalCount(AEnemyBase::StaticClass()), 0);
	TestTrue(TEXT("DestroyPool invalidates the old lease"),
		!MaxLeaseA || MaxLeaseA->IsActorBeingDestroyed());

	UEnemyPoolDefinition* TurretDefinition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& TurretEntry = TurretDefinition->Entries.AddDefaulted_GetRef();
	TurretEntry.EnemyClass = AAutoTurret::StaticClass();
	TurretEntry.PrewarmCount = 1;
	TurretEntry.MaxCount = 1;
	AddExpectedError(TEXT("Prewarm rejected because AutoTurret is placed-only"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Runtime prewarm rejects placed-only AutoTurrets"),
		Pool->PrewarmPool(TurretDefinition));
	TestEqual(TEXT("Rejected AutoTurret prewarm creates no Actors"),
		Pool->GetTotalCount(AAutoTurret::StaticClass()), 0);

	// 실제 사격형 BP의 StateTree가 방 전투 중에도 NonBattle에서 시작하는지 확인한다.
	// Alive 아래 전투 상태 선택이 실패해도 Death가 일반 fallback이 되면 안 된다.
	const UStateTree* GunCommonTree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDrone_Common.ST_VECDrone_Common"));
	if (TestNotNull(TEXT("Gun common StateTree loads"), GunCommonTree))
	{
		bool bFoundDeath = false;
		for (const FCompactStateTreeState& State : GunCommonTree->GetStates())
		{
			if (State.Name == FName(TEXT("Death")))
			{
				bFoundDeath = true;
				TestTrue(TEXT("Death requires the died event, not a failed combat selection"),
					State.RequiredEventToEnter.Tag == FGameplayTag::RequestGameplayTag(
						TEXT("Enemy.Event.Died")));
			}
		}
		TestTrue(TEXT("Gun common StateTree contains Death"), bFoundDeath);
	}
	const UStateTree* GunBattleTree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneBattle_Gun.ST_VECDroneBattle_Gun"));
	if (TestNotNull(TEXT("Gun battle StateTree loads"), GunBattleTree))
	{
		bool bFoundSearch = false;
		for (const FCompactStateTreeState& State : GunBattleTree->GetStates())
		{
			if (State.Name == FName(TEXT("LostContactSearch")))
			{
				bFoundSearch = true;
				TestEqual(TEXT("Search remains the fallback after other combat choices fail"),
					State.EnterConditionsNum, static_cast<uint8>(0));
			}
		}
		TestTrue(TEXT("Gun battle StateTree contains LostContactSearch"), bFoundSearch);
	}

	UClass* GunClass = LoadClass<AEnemyBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (TestNotNull(TEXT("Gun reinforcement BP loads"), GunClass))
	{
		UEnemyPoolDefinition* GunDefinition = NewObject<UEnemyPoolDefinition>(World);
		FEnemyPoolEntry& GunEntry = GunDefinition->Entries.AddDefaulted_GetRef();
		GunEntry.EnemyClass = GunClass;
		GunEntry.PrewarmCount = 1;
		GunEntry.MaxCount = 1;
		if (TestTrue(TEXT("Gun reinforcement prewarms"), Pool->PrewarmPool(GunDefinition)))
		{
			AEnemyBase* GunLease = Pool->LeaseEnemy(GunClass,
				FTransform(FVector(100.0, 200.0, 300.0)), FirstContext);
			if (TestNotNull(TEXT("Gun reinforcement leases"), GunLease))
			{
				if (GunLease->GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
				{
					GunLease->CompletePoolSpawnPresentation(
						FirstContext.GameplayGeneration, GunLease->GetPoolLeaseSerial());
				}
				UStateTreeComponent* GunStateTree = GunLease->GetStateTreeComponent();
				if (TestNotNull(TEXT("Gun reinforcement has a StateTree"), GunStateTree))
				{
					const TArray<FName> ActiveStates = GunStateTree->GetActiveStateNames();
					TestEqual(TEXT("Gun reinforcement starts its StateTree"),
						GunStateTree->GetStateTreeRunStatus(), EStateTreeRunStatus::Running);
					TestTrue(TEXT("Gun reinforcement selects NonBattle immediately"),
						ActiveStates.Contains(FName(TEXT("NonBattle"))));
					TestFalse(TEXT("Gun reinforcement is not alerted by room history"),
						ActiveStates.Contains(FName(TEXT("Alert"))));
				}
				TestTrue(TEXT("Gun reinforcement returns before a shared-target lease"),
					Pool->ReturnEnemy(GunLease, FirstContext.GameplayGeneration,
						GunLease->GetPoolLeaseSerial()));
				RoomSubsystem->SetActiveRoomTargetForTesting(RoomTag, SharedTargetLocation, true);
				AEnemyBase* SharedGunLease = Pool->LeaseEnemy(GunClass,
					FTransform(FVector(100.0, 200.0, 300.0)), FirstContext);
				if (TestNotNull(TEXT("Gun reinforcement leases with a shared target"), SharedGunLease))
				{
					if (SharedGunLease->GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
					{
						SharedGunLease->CompletePoolSpawnPresentation(
							FirstContext.GameplayGeneration, SharedGunLease->GetPoolLeaseSerial());
					}
					TestFalse(TEXT("Shared target does not pre-alert the gun reinforcement"),
						SharedGunLease->HasSharedTargetContact());
					if (UStateTreeComponent* SharedStateTree = SharedGunLease->GetStateTreeComponent())
					{
						const TArray<FName> SharedInitialStates = SharedStateTree->GetActiveStateNames();
						TestTrue(TEXT("Shared-target gun also starts in NonBattle"),
							SharedInitialStates.Contains(FName(TEXT("NonBattle"))));
						// 실제 감지가 일어난 뒤에만 경계와 공유 추적을 시작한다.
						SharedGunLease->EnterAlertFromPerception(SharedTargetLocation);
						SharedGunLease->ApplySharedTargetContact(SharedTargetLocation, true);
						for (int32 TickIndex = 0; TickIndex < 4
							&& SharedGunLease->GetCombatState() != EEnemyCombatState::Combat; ++TickIndex)
						{
							SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						}
						TestEqual(TEXT("Detected gun commits to combat after entering Alert"),
							SharedGunLease->GetCombatState(), EEnemyCombatState::Combat);
						// 별도 월드 틱이 없는 fixture에서 다음 틱 예약 이벤트를 직접 전달한다.
						SharedGunLease->SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(
							TEXT("Enemy.Event.Combat.Entered")));
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						const TArray<FName> BattleStates = SharedStateTree->GetActiveStateNames();
						TestTrue(TEXT("Shared-target gun transitions into Battle"),
							BattleStates.Contains(FName(TEXT("Battle"))));
						TestTrue(TEXT("Shared-target gun pursues the room target"),
							BattleStates.Contains(FName(TEXT("SharedPursuit"))));
						TestFalse(TEXT("Shared-target gun never returns to NonBattle"),
							BattleStates.Contains(FName(TEXT("NonBattle"))));

						// Perception의 시야 플래그가 TargetActor 갱신보다 앞서면 Battle의
						// 직접 교전/공유 추적 조건 사이에 한 프레임 공백이 생길 수 있다.
						SharedGunLease->SetPlayerCurrentlyVisible(true);
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						TestFalse(TEXT("Living gun does not fall into Death during a target-selection gap"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Death"))));
						TestTrue(TEXT("Gun keeps shared pursuit without a visible target Actor"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("SharedPursuit"))));
						SharedGunLease->SetPlayerCurrentlyVisible(false);
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);

						// 사망이 아닌 표적 공유 소실은 전투 수색으로 이어져야 한다.
						SharedGunLease->ClearSharedTargetContact();
						for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
						{
							World->GetTimerManager().Tick(0.016f);
							SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						}
						TestFalse(TEXT("Living gun never selects Death after losing a shared target"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Death"))));
						TestTrue(TEXT("Living gun stays in Battle after losing a shared target"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Battle"))));

						// 공유 표적 소실과 가시성 신호가 엇갈린 프레임에서 트리가
						// 다시 선택되더라도 사망 분기로 빠지지 않아야 한다.
						SharedGunLease->SetPlayerCurrentlyVisible(true);
						SharedStateTree->StopLogic(TEXT("Target-selection gap regression"));
						SharedStateTree->StartLogic();
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						TestEqual(TEXT("Gun StateTree keeps running with visibility but no target Actor"),
							SharedStateTree->GetStateTreeRunStatus(), EStateTreeRunStatus::Running);
						TestFalse(TEXT("Living gun does not select Death after target-selection restart"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Death"))));
						TestTrue(TEXT("Gun searches when the visible target Actor is missing"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("LostContactSearch"))));
					}
				}
			}
		}
		Pool->DestroyPool();
	}

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyPoolDefinitionValidationTest,
	"Outlier.Enemy.PoolDefinition.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPoolDefinitionValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>();
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("An empty pool definition is valid"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Valid);
	}

	FEnemyPoolEntry& ValidEntry = Definition->Entries.AddDefaulted_GetRef();
	ValidEntry.EnemyClass = AEnemyBase::StaticClass();
	ValidEntry.PrewarmCount = 2;
	ValidEntry.MaxCount = 3;
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("A configured pool entry is valid"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Valid);
	}

	FEnemyPoolEntry& DuplicateEntry = Definition->Entries.AddDefaulted_GetRef();
	DuplicateEntry.EnemyClass = AEnemyBase::StaticClass();
	DuplicateEntry.PrewarmCount = 4;
	DuplicateEntry.MaxCount = 3;
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("Duplicate classes and PrewarmCount above MaxCount are rejected"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Invalid);
	}

	Definition->Entries.Reset();
	FEnemyPoolEntry& TurretEntry = Definition->Entries.AddDefaulted_GetRef();
	TurretEntry.EnemyClass = AAutoTurret::StaticClass();
	TurretEntry.PrewarmCount = 1;
	TurretEntry.MaxCount = 1;
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("A placed-only AutoTurret class is rejected"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Invalid);
	}

	Definition->Entries.Reset();
	Definition->Entries.AddDefaulted();
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("A missing EnemyClass is rejected"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Invalid);
	}

	return true;
}

#endif
