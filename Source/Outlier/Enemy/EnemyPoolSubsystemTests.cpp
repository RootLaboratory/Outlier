#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Room/RoomTagComponent.h"

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
	TestTrue(TEXT("Reinforcement joins the active room combat immediately"), FirstLease->IsInCombat());
	TestTrue(TEXT("Reinforcement receives the active room target immediately"),
		FirstLease->HasSharedTargetContact());
	TestEqual(TEXT("Reinforcement receives the shared target location"),
		FirstLease->GetSharedTargetLocation(), SharedTargetLocation);
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
	// 마지막 직접 관측자가 시야를 잃은 사이 증원돼도 전투에는 바로 합류한다.
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
	TestTrue(TEXT("Reinforcement joins combat without active sight sharing"),
		SecondLease->IsInCombat());
	TestFalse(TEXT("Lost sight is not restored as shared contact"),
		SecondLease->HasSharedTargetContact());
	TestEqual(TEXT("Reinforcement investigates the last known target location"),
		SecondLease->GetLastKnownPlayerLocation(), SharedTargetLocation);

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
