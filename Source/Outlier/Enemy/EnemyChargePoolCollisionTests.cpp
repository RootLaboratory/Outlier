#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/SelfDestructDrone.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Explosion/ExplosionComponent.h"
#include "Explosion/ExplosionTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "UObject/UnrealType.h"

namespace
{
struct FDronePoolCollisionWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	UEnemyPoolSubsystem* Pool = nullptr;
	UClass* DroneClass = nullptr;
	ASelfDestructDrone* Drone = nullptr;
	FDronePoolCollisionWorld()
	{
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DronePoolCollision")), GetTransientPackage());
		if (!World) return;
		World->AddToRoot();
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.RunAsDedicated = true;
		Context.SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		URL.Port = 0;
		World->Listen(URL);
		World->BeginPlay();
		Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
		DroneClass = LoadClass<ASelfDestructDrone>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C"));
		if (!Pool || !DroneClass) return;
		UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(World);
		FEnemyPoolEntry& Entry = Definition->Entries.AddDefaulted_GetRef();
		Entry.EnemyClass = DroneClass;
		Entry.PrewarmCount = Entry.MaxCount = 1;
		if (!Pool->PrewarmPool(Definition)) return;
		for (TActorIterator<ASelfDestructDrone> It(World); It; ++It)
		{
			if (It->GetClass() == DroneClass && It->GetEnemyPoolState() == EEnemyPoolState::Idle)
			{
				Drone = *It;
				Drone->SetPoolPresentationAutoCompleteForTesting(false);
				break;
			}
		}
	}
	~FDronePoolCollisionWorld()
	{
		if (!World) return;
		GEngine->ShutdownWorldNetDriver(World);
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		if (Instance) Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	void Tick(float DeltaTime) const
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaTime);
	}
	bool Lease(int32 Generation) const
	{
		FEnemyPoolLeaseContext Context;
		Context.GameplayGeneration = Generation;
		return Pool->LeaseEnemy(DroneClass, FTransform(FVector(0, 0, 1000)), Context) == Drone;
	}
	void ActivateAndIsolateMovement() const
	{
		Drone->CompletePoolSpawnPresentation(Drone->GetPoolGameplayGeneration(), Drone->GetPoolLeaseSerial());
		// Pool state activation, controller creation and the production tree start are
		// real. Stop the tree only for the isolated collision measurement below.
		Drone->GetStateTreeComponent()->StopLogic(TEXT("Pool collision measurement"));
		if (AEnemyAIController* Controller = Cast<AEnemyAIController>(Drone->GetController()))
			Controller->SetEnemyPerceptionEnabled(false);
	}
	bool Return() const
	{
		return Pool->ReturnEnemy(Drone, Drone->GetPoolGameplayGeneration(), Drone->GetPoolLeaseSerial());
	}
	AActor* AddBox(const FVector& Location, const FVector& Extent) const
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Actor;
	}
	void Log(FAutomationTestBase& Test, const TCHAR* Stage, float DeltaTime, float Speed) const
	{
		Test.AddInfo(FString::Printf(TEXT("[DronePoolWall] Stage=%s NetMode=%d Time=%.3f dt=%.4f Speed=%.0f Generation=%d Lease=%d Pool=%s ActorCollision=%d CapsuleCollision=%d Committed=%d Controller=%s Location=%s Velocity=%s"),
			Stage, int32(World->GetNetMode()), World->GetTimeSeconds(), DeltaTime, Speed,
			Drone->GetPoolGameplayGeneration(), Drone->GetPoolLeaseSerial(), *UEnum::GetValueAsString(Drone->GetEnemyPoolState()),
			Drone->GetActorEnableCollision(), int32(Drone->GetCapsuleComponent()->GetCollisionEnabled()),
			Drone->IsSelfDestructCommitted(), *GetNameSafe(Drone->GetController()),
			*Drone->GetActorLocation().ToCompactString(), *Drone->GetVelocity().ToCompactString()));
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargePoolCollisionLifecycleTest,
	"Outlier.Enemy.SelfDestruct.PoolCollisionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargePoolCollisionLifecycleTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	for (float DeltaTime : {1.0f / 60.0f, 1.0f / 15.0f, 0.25f})
	for (float Speed : {750.0f, 3000.0f})
	{
		FDronePoolCollisionWorld Fixture;
		if (!TestNotNull(TEXT("Pool world"), Fixture.World) || !TestNotNull(TEXT("Pooled production BP"), Fixture.Drone)) return false;
		TestTrue(TEXT("Actual dedicated NetDriver exists"), Fixture.World->GetNetMode() == NM_DedicatedServer && Fixture.World->GetNetDriver());
		Fixture.AddBox(FVector(300, 0, 1000), FVector(1.5f, 2000, 2000));
		AActor* Target = Fixture.AddBox(FVector(600, 0, 1000), FVector(1));
		Target->SetActorEnableCollision(false);
		int32 PreviousSerial = 0;
		for (int32 LeaseIndex = 0; LeaseIndex < 3; ++LeaseIndex)
		{
			const int32 Generation = 100 + LeaseIndex;
			if (!TestTrue(TEXT("Lease reuses the same production BP"), Fixture.Lease(Generation))) return false;
			ASelfDestructDrone* Drone = Fixture.Drone;
			Fixture.Log(*this, TEXT("Presentation"), DeltaTime, Speed);
			TestEqual(TEXT("Presentation starts before collision activation"), Drone->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
			TestFalse(TEXT("Presentation disables actor collision"), Drone->GetActorEnableCollision());
			TestNull(TEXT("Presentation has no controller"), Drone->GetController());
			for (int32 Frame = 0; Frame < FMath::CeilToInt(0.5f / DeltaTime); ++Frame)
			{
				Fixture.Tick(DeltaTime);
				TestFalse(TEXT("Inactive presentation never automatically commits a charge"), Drone->IsSelfDestructCommitted());
			}
			if (PreviousSerial)
			{
				Drone->CompletePoolSpawnPresentation(Generation - 1, PreviousSerial);
				TestEqual(TEXT("Stale callback cannot enable the current lease"), Drone->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
				TestFalse(TEXT("Stale callback leaves collision off"), Drone->GetActorEnableCollision());
			}
			Fixture.ActivateAndIsolateMovement();
			TestEqual(TEXT("Current callback activates combat"), Drone->GetEnemyPoolState(), EEnemyPoolState::CombatActive);
			TestTrue(TEXT("Current lease restores actor collision"), Drone->GetActorEnableCollision());
			TestTrue(TEXT("Current lease restores capsule query collision"), Drone->GetCapsuleComponent()->IsQueryCollisionEnabled());
			TestEqual(TEXT("Production WorldStatic response stays Block"), Drone->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);
			if (!TestTrue(TEXT("Active lease commits charge"), Drone->BeginCommittedSelfDestruct(Target, 2, Speed))) return false;
			bool bCrossed = false;
			bool bCollisionLost = false;
			float MaxX = 0;
			for (int32 Frame = 0; Frame < FMath::RoundToInt(2.0f / DeltaTime); ++Frame)
			{
				Drone->UpdateCommittedSelfDestructMovement(DeltaTime, Speed);
				Fixture.Tick(DeltaTime);
				MaxX = FMath::Max(MaxX, float(Drone->GetActorLocation().X));
				bCrossed |= Drone->GetActorLocation().X > 301.5f;
				bCollisionLost |= !Drone->GetActorEnableCollision() || !Drone->GetCapsuleComponent()->IsQueryCollisionEnabled();
			}
			Fixture.Log(*this, TEXT("WallContact"), DeltaTime, Speed);
			TestTrue(TEXT("Scheduled movement physically reaches the wall"), MaxX > 100);
			TestTrue(TEXT("Capsule remains on the near side including its radius"), MaxX <= 298.5f - Drone->GetCapsuleComponent()->GetScaledCapsuleRadius() + 1);
			TestFalse(TEXT("World Tick never crosses the blocking wall"), bCrossed);
			TestFalse(TEXT("Collision stays enabled throughout charge"), bCollisionLost);
			PreviousSerial = Drone->GetPoolLeaseSerial();
			if (LeaseIndex == 1)
			{
				Drone->SimulateDeathPresentationForPoolTesting();
				TestEqual(TEXT("Death presentation disables capsule before reuse"), Drone->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
			}
			if (!TestTrue(TEXT("Return accepts current lease"), Fixture.Return())) return false;
			TestEqual(TEXT("Returned lease is Idle"), Drone->GetEnemyPoolState(), EEnemyPoolState::Idle);
			TestFalse(TEXT("Return clears committed charge"), Drone->IsSelfDestructCommitted());
			TestFalse(TEXT("Return disables collision"), Drone->GetActorEnableCollision());
			Fixture.Tick(DeltaTime);
			TestFalse(TEXT("Idle world tick cannot resume old charge"), Drone->IsSelfDestructCommitted());
			Fixture.Log(*this, TEXT("Returned"), DeltaTime, Speed);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargePoolExplosionRearmTest,
	"Outlier.Enemy.SelfDestruct.PoolExplosionRearm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargePoolExplosionRearmTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FDronePoolCollisionWorld Fixture;
	if (!TestNotNull(TEXT("Pooled production drone"), Fixture.Drone) || !TestTrue(TEXT("First lease"), Fixture.Lease(200))) return false;
	Fixture.ActivateAndIsolateMovement();
	ASelfDestructDrone* Drone = Fixture.Drone;
	UExplosionComponent* Explosion = Drone->GetExplosionComponent();
	if (!TestNotNull(TEXT("Explosion component"), Explosion)) return false;
	Drone->TriggerSelfDestruct();
	TestTrue(TEXT("First actual self-destruction requests an explosion"), Explosion->HasDetonated());
	TestEqual(TEXT("Self-destruction waits for the pool death callback"), Drone->GetEnemyPoolState(), EEnemyPoolState::DeathPresentation);
	const int32 FirstSerial = Drone->GetPoolLeaseSerial();
	Drone->CompletePoolDeathPresentation(200, FirstSerial);
	if (!TestTrue(TEXT("Second lease reuses the same exploded actor"), Fixture.Lease(201))) return false;
	TestEqual(TEXT("Rearming happens before combat activation"), Drone->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	TestFalse(TEXT("Presentation starts with its explosion rearmed"), Explosion->HasDetonated());
	Fixture.ActivateAndIsolateMovement();
	Fixture.Log(*this, TEXT("ExplosionReused"), 0, 0);
	TestFalse(TEXT("A new pool lease must rearm its explosion latch"), Explosion->HasDetonated());
	TestTrue(TEXT("Reused drone must accept a fresh explosion request"), Explosion->DetonateAt(Drone->GetActorLocation()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargePoolExplosionRepeatedDamageTest,
	"Outlier.Enemy.SelfDestruct.PoolExplosionRepeatedDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargePoolExplosionRepeatedDamageTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FDronePoolCollisionWorld Fixture;
	if (!TestNotNull(TEXT("Pooled production drone"), Fixture.Drone)) return false;
	if (!TestTrue(TEXT("Dedicated server owns the fixture"), Fixture.World->GetNetMode() == NM_DedicatedServer)) return false;
	ASelfDestructDrone* Drone = Fixture.Drone;
	UExplosionComponent* Explosion = Drone->GetExplosionComponent();
	if (!TestNotNull(TEXT("Explosion component"), Explosion)) return false;

	// 피해만 관측하는 고정 표적이다. BeginPlay 전에 스탯을 지정해 기본 HP 0에 의한 즉시 사망을 막는다.
	UDataTable* TargetStats = NewObject<UDataTable>(Fixture.World);
	TargetStats->RowStruct = FEnemyStat::StaticStruct();
	FEnemyStat TargetStat;
	TargetStat.Health = 1000;
	const FName TargetStatName(TEXT("PoolRearmTarget"));
	TargetStats->AddRow(TargetStatName, TargetStat);
	FStructProperty* StatProperty = FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("EnemyStatRow"));
	if (!TestNotNull(TEXT("Target stat row property"), StatProperty)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.bDeferConstruction = true;
	const FTransform TargetTransform(FVector(200, 0, 1000));
	AEnemyBase* Target = Fixture.World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), TargetTransform, Spawn);
	if (!TestNotNull(TEXT("Fixed damage target"), Target)) return false;
	Target->AutoPossessAI = EAutoPossessAI::Disabled;
	FDataTableRowHandle* TargetStatHandle = StatProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Target);
	TargetStatHandle->DataTable = TargetStats;
	TargetStatHandle->RowName = TargetStatName;
	Target->FinishSpawning(TargetTransform);
	Target->SetActorTickEnabled(false);
	Target->GetCharacterMovement()->DisableMovement();
	if (!TestFalse(TEXT("Damage target starts alive"), Target->IsDead())) return false;
	TestEqual(TEXT("Target starts with its configured health"), Target->GetCurrentHealth(), 1000.0f);

	UDataTable* Profiles = NewObject<UDataTable>(Fixture.World);
	Profiles->RowStruct = FExplosionProfileRow::StaticStruct();
	FExplosionProfileRow Profile;
	Profile.MaxDamage = 20;
	Profile.OuterRadiusCm = 1000;
	Profile.DamageFalloffExponent = 1;
	Profile.OccludedMultiplier = 1;
	Profile.EnemyImpulseScale = 0;
	Profile.CameraShakeScale = 0;
	const FName ProfileName(TEXT("PoolRearmDamage"));
	Profiles->AddRow(ProfileName, Profile);
	FDataTableRowHandle ProfileHandle;
	ProfileHandle.DataTable = Profiles;
	ProfileHandle.RowName = ProfileName;
	Explosion->SetExplosionProfileRow(ProfileHandle);
	Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	Fixture.World->GetPhysicsScene()->Flush();
	constexpr float ExpectedDamage = 16; // 20 * (1 - 200 / 1000), 고정 거리의 선형 감쇠.

	for (int32 LeaseIndex = 0; LeaseIndex < 3; ++LeaseIndex)
	{
		const int32 Generation = 200 + LeaseIndex;
		if (!TestTrue(TEXT("Each lease reuses the same production actor"), Fixture.Lease(Generation))) return false;
		if (!TestFalse(TEXT("New lease rearms before combat activation"), Explosion->HasDetonated())) return false;
		TestEqual(TEXT("New lease waits for spawn presentation"), Drone->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
		const float HealthBefore = Target->GetCurrentHealth();
		Fixture.Tick(1.0f / 60.0f);
		TestEqual(TEXT("Spawn presentation does not cause explosion damage"), Target->GetCurrentHealth(), HealthBefore);
		Fixture.ActivateAndIsolateMovement();

		// 자폭과 일반 피격 사망 모두 실제 HandleDeath를 거쳐 같은 폭발 컴포넌트를 사용해야 한다.
		if (LeaseIndex == 1)
		{
			FOutlierDamageRequest LethalDamage;
			LethalDamage.DamageAmount = Drone->GetCurrentHealth() + 1;
			LethalDamage.DamageTag = OutlierGameplayTags::Damage::Explosion();
			LethalDamage.DamageCauser = Target;
			LethalDamage.bAllowFriendlyFire = true;
			OutlierDamage::Apply(Drone, LethalDamage);
		}
		else
		{
			Drone->TriggerSelfDestruct();
		}
		if (!TestTrue(TEXT("Actual death requests an explosion on every lease"), Explosion->HasDetonated())) return false;
		TestEqual(TEXT("Actual death waits for its presentation callback"), Drone->GetEnemyPoolState(), EEnemyPoolState::DeathPresentation);
		const float HealthAfter = Target->GetCurrentHealth();
		TestTrue(TEXT("Every lease applies exactly one expected damage amount"), FMath::IsNearlyEqual(HealthBefore - HealthAfter, ExpectedDamage));

		// 사망 재호출과 컴포넌트 직접 요청을 겹쳐도 같은 대여에서 다시 피해를 주면 안 된다.
		Drone->TriggerSelfDestruct();
		TestFalse(TEXT("Same lease rejects a repeated explosion request"), Explosion->DetonateAt(Drone->GetActorLocation()));
		TestEqual(TEXT("Repeated requests do not apply additional damage"), Target->GetCurrentHealth(), HealthAfter);
		Drone->CompletePoolDeathPresentation(Generation, Drone->GetPoolLeaseSerial());
		TestEqual(TEXT("Death presentation returns the actor to Idle"), Drone->GetEnemyPoolState(), EEnemyPoolState::Idle);
		TestTrue(TEXT("Returning to Idle preserves duplicate protection"), Explosion->HasDetonated());
		Fixture.Tick(1.0f / 60.0f);
		TestEqual(TEXT("Idle does not cause explosion damage"), Target->GetCurrentHealth(), HealthAfter);
		AddInfo(FString::Printf(TEXT("[DronePoolExplosion] Lease=%d Generation=%d HealthBefore=%.2f HealthAfter=%.2f ExpectedDamage=%.2f"),
			LeaseIndex, Generation, HealthBefore, HealthAfter, ExpectedDamage));
	}
	return !HasAnyErrors();
}

#endif
