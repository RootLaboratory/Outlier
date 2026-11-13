#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Room/RoomCombatSubsystem.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/LaserWeapon.h"
#include "Damage/OutlierDamageReceiver.h"
#include "GameplayTags/OutlierGameplayTags.h"

namespace
{
// 감지 본과 무기·충돌은 실제 BP 경로를 사용한다. 자동 Tree만 멈춰 실패 조건과 시간 경계를 제어한다.
struct FF3AttemptWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	AEnemyBase* Enemy = nullptr;
	AEnemyAIController* AI = nullptr;
	AShooterCharacter* Target = nullptr;
	UClass* ShooterClass = nullptr;
	UBoxComponent* Wall = nullptr;

	~FF3AttemptWorld()
	{
		if (!World)
		{
			return;
		}
		GEngine->ShutdownWorldNetDriver(World);
		if (World->HasBegunPlay())
		{
			World->EndPlay(EEndPlayReason::Quit);
		}
		World->DestroyWorld(true);
		if (GameInstance)
		{
			GameInstance->Shutdown();
		}
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyF3Attempt")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Isolated server world exists"), World))
		{
			return false;
		}
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();
		UClass* GunClass = LoadClass<AEnemyBase>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("Production Gun BP loads"), GunClass)
			|| !Test.TestNotNull(TEXT("Production Shooter BP loads"), ShooterClass))
		{
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(GunClass, FVector(0, 0, 300), FRotator::ZeroRotator, Spawn);
		Target = SpawnTarget(FVector(1000, 0, 300));
		if (!Test.TestNotNull(TEXT("Production Gun spawns"), Enemy)
			|| !Test.TestNotNull(TEXT("Production Shooter spawns"), Target))
		{
			return false;
		}
		AI = Cast<AEnemyAIController>(Enemy->GetController());
		if (!Test.TestNotNull(TEXT("Gun owns a controller"), AI)
			|| !Test.TestNotNull(TEXT("Gun owns its real weapon"), Enemy->GetCurrentWeapon()))
		{
			return false;
		}
		Enemy->EnterCombatFromRoom(Target->GetActorLocation(), false);
		Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled F3 attempt regression"));
		AI->SetEnemyPerceptionEnabled(false);
		AI->SetActorTickEnabled(false);
		Enemy->SetActorTickEnabled(false);
		Enemy->SetActorEnableCollision(false);
		Enemy->GetCharacterMovement()->DisableMovement();
		Select(Target);
		if (!Test.TestTrue(TEXT("Shooter supplies actual observer-specific TP bones"), Observe(Target)))
		{
			return false;
		}
		Enemy->RefreshCombatAimPoint();
		Align();
		ARangedWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
		FFloatProperty* Interval = FindFProperty<FFloatProperty>(ARangedWeaponBase::StaticClass(), TEXT("AttackInterval"));
		FIntProperty* BurstCount = FindFProperty<FIntProperty>(ARangedWeaponBase::StaticClass(), TEXT("BurstShotCount"));
		if (!Test.TestNotNull(TEXT("Real weapon ammo policy exists"), InfiniteAmmo)
			|| !Test.TestNotNull(TEXT("Real weapon cadence exists"), Interval)
			|| !Test.TestNotNull(TEXT("Real weapon burst policy exists"), BurstCount))
		{
			return false;
		}
		InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
		Interval->SetPropertyValue_InContainer(Weapon, 0.05f);
		BurstCount->SetPropertyValue_InContainer(Weapon, 0);
		return true;
	}

	AShooterCharacter* SpawnTarget(const FVector& Location)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterCharacter* Result = World->SpawnActor<AShooterCharacter>(ShooterClass, Location, FRotator::ZeroRotator, Spawn);
		if (Result)
		{
			Result->SetActorEnableCollision(false);
			Result->SetCanBeDamaged(false);
			Result->GetCharacterMovement()->DisableMovement();
			Result->GetMesh()->SetComponentTickEnabled(false);
		}
		return Result;
	}

	void Select(AActor* Selected)
	{
		AI->SetVisibleTargetsForTesting({Selected});
		Enemy->SetPlayerCurrentlyVisible(true);
		Enemy->ObserveTrackedTarget(Selected);
		Enemy->BeginTargetAttackPreparation(0.0f);
		Enemy->EnsureCombatTacticalEpisode(Selected, Selected->GetActorLocation());
	}

	bool Observe(AShooterCharacter* Observed)
	{
		FCanBeSeenFromContext Context;
		Context.IgnoreActor = Enemy;
		Context.ObserverLocation = Enemy->GetPawnViewLocation();
		FVector Seen;
		int32 Sync = 0;
		int32 Async = 0;
		float Strength = 0.0f;
		Observed->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		Observed->GetMesh()->SetComponentTickEnabled(true);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		Observed->GetMesh()->SetComponentTickEnabled(false);
		Observed->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		FOutlierSightObservation Observation;
		return Observed->GetObservedAimPoints(Enemy, Observation);
	}

	void Align()
	{
		AI->SetControlRotation((Enemy->GetCombatAimPoint(Enemy->GetTrackedTarget()) - Enemy->GetCombatShotOrigin()).Rotation());
	}

	UBoxComponent* AddBlocker(AActor* Owner, const FVector& Location, bool bEnvironment)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
		Owner->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(20, 180, 300));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(bEnvironment ? ECC_WorldStatic : ECC_Pawn);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Box->SetWorldLocation(Location);
		Box->RegisterComponent();
		return Box;
	}

	void BlockMuzzle()
	{
		// Visibility는 통과시켜 실제 자기 Sight를 유지하고, 실제 탄환 PhysicsBody 경로만 차단한다.
		Wall = AddBlocker(World->SpawnActor<AActor>(),
			FMath::Lerp(Enemy->GetCombatShotOrigin(), Enemy->GetCombatAimPoint(Target), 0.5f), true);
	}

	void Advance(float Seconds, bool bRefreshObservation = false)
	{
		const double EndAt = World->GetTimeSeconds() + Seconds;
		while (World->GetTimeSeconds() < EndAt - KINDA_SMALL_NUMBER)
		{
			const float Step = FMath::Min(static_cast<float>(EndAt - World->GetTimeSeconds()), 0.025f);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			if (bRefreshObservation && World->GetTimeSeconds() + 1.0 / 60.0 <= EndAt)
			{
				Observe(Target);
				Enemy->RefreshCombatAimPoint();
				Align();
			}
		}
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF3AttemptTest,
	"Outlier.Enemy.Combat.F3Attempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF3AttemptTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("RequiresPhysicalExhaustion"), TEXT("NormalGateUnchanged"), TEXT("EnvironmentPath"),
		TEXT("QueryBudget"), TEXT("AllyInFront"), TEXT("AllyBehindWall"), TEXT("ActualShotAndDeadline"),
		TEXT("ShortBurstDeadline"), TEXT("HiddenBlocksNewShot"), TEXT("FinishedDoesNotRecharge"),
		TEXT("TargetRoundTrip"), TEXT("PreparationExpiryDoesNotRecharge"), TEXT("OpenShotDoesNotRecharge"),
		TEXT("SmallJitterDoesNotRecharge"), TEXT("ObservationExpiry"), TEXT("FailureTTLDoesNotRecharge"),
		TEXT("RelatedObstacleChange"), TEXT("UnrelatedObstacleChange"), TEXT("DifferentSightLine"), TEXT("ClearedExhaustion"),
		TEXT("LaserExcluded"), TEXT("PossessedExcluded"), TEXT("DeathStopsShots"), TEXT("StunDoesNotRecharge"),
		TEXT("PoolLeaseContract"), TEXT("ControllerContract"), TEXT("WeaponContract")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyF3AttemptTest::RunTest(const FString& Parameters)
{
	FF3AttemptWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AEnemyBase* Enemy = Fixture.Enemy;
	ARangedWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	AShooterCharacter* Target = Fixture.Target;
	Fixture.BlockMuzzle();
	Enemy->RefreshCombatAimPoint();
	Fixture.Align();
	const FVector Aim = Enemy->GetCombatAimPoint(Target);
	const FVector Origin = Enemy->GetCombatShotOrigin();
	if (!TestNotNull(TEXT("Physical muzzle wall exists"), Fixture.Wall)
		|| !TestFalse(TEXT("Control ordinary muzzle path is physically blocked"), Weapon->HasClearTargetAttackPath(Target, Aim)))
	{
		return false;
	}
	if (Parameters == TEXT("EnvironmentPath"))
	{
		int32 Budget = 16;
		TestTrue(TEXT("Environment-only path can be evaluated without removing real wall"),
			Weapon->HasTargetAttemptPathFrom(Target, Aim, Origin, Budget));
		TestTrue(TEXT("Per-muzzle evaluation respects sixteen query bound"), Budget >= 0 && Budget < 16);
		TestFalse(TEXT("Attempt assessment never changes ordinary wall collision"), Weapon->HasClearTargetAttackPath(Target, Aim));
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("QueryBudget"))
	{
		int32 Budget = 0;
		TestFalse(TEXT("Unexamined path cannot grant environment exception"),
			Weapon->HasTargetAttemptPathFrom(Target, Aim, Origin, Budget));
		TestEqual(TEXT("Exhausted evaluation cannot consume negative budget"), Budget, 0);
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("AllyInFront") || Parameters == TEXT("AllyBehindWall"))
	{
		AEnemyBase* Ally = Fixture.World->SpawnActor<AEnemyBase>();
		if (!TestNotNull(TEXT("Actual allied AI actor exists"), Ally))
		{
			return false;
		}
		Ally->SetActorTickEnabled(false);
		Ally->SetActorEnableCollision(true);
		Fixture.AddBlocker(Ally, FMath::Lerp(Origin, Aim,
			Parameters == TEXT("AllyInFront") ? 0.25f : 0.75f), false);
		int32 Budget = 16;
		TestFalse(TEXT("Fellow AI blocks exception even behind environmental cover"),
			Weapon->HasTargetAttemptPathFrom(Target, Aim, Origin, Budget));
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("RequiresPhysicalExhaustion") || Parameters == TEXT("NormalGateUnchanged"))
	{
		TestFalse(TEXT("Blocked ordinary attack cannot fire before movement alternatives are exhausted"), Enemy->CanAttackTrackedTarget(Target));
		TestFalse(TEXT("Missing physical exhaustion evidence cannot arm attempt"), Enemy->PrepareTargetAttackAttempt(1.0f));
		TestFalse(TEXT("No exception attack was created"), Enemy->IsTargetAttemptActive());
		return !HasAnyErrors();
	}
	Enemy->NoteTargetAttackAlternativesExhausted();
	if (Parameters == TEXT("PossessedExcluded"))
	{
		FBoolProperty* Possessed = FindFProperty<FBoolProperty>(AEnemyBase::StaticClass(), TEXT("bIsPossessed"));
		if (!TestNotNull(TEXT("Existing possession policy flag exists"), Possessed))
		{
			return false;
		}
		Possessed->SetPropertyValue_InContainer(Enemy, true);
		TestFalse(TEXT("Player possession never inherits AI environment exception"), Enemy->PrepareTargetAttackAttempt(1.0f));
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("LaserExcluded"))
	{
		ALaserWeapon* Laser = Fixture.World->SpawnActor<ALaserWeapon>();
		FObjectPropertyBase* WeaponProperty = FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"));
		if (!TestNotNull(TEXT("Dedicated laser exists"), Laser) || !TestNotNull(TEXT("Weapon ownership property exists"), WeaponProperty))
		{
			return false;
		}
		Laser->OnEquipped(Enemy);
		WeaponProperty->SetObjectPropertyValue_InContainer(Enemy, Laser);
		TestFalse(TEXT("Dedicated laser remains outside ordinary bullet exception"), Enemy->PrepareTargetAttackAttempt(1.0f));
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("ClearedExhaustion"))
	{
		Enemy->ClearTargetAttackAlternativesExhausted();
		TestFalse(TEXT("A newly validated movement alternative revokes obsolete exhaustion evidence"), Enemy->PrepareTargetAttackAttempt(1.0f));
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("ObservationExpiry"))
	{
		Fixture.Advance(1.0f);
		TestFalse(TEXT("Movement exhaustion cannot substitute for expired TP observation"), Enemy->PrepareTargetAttackAttempt(1.0f));
		return !HasAnyErrors();
	}
	const float Duration = Parameters == TEXT("ShortBurstDeadline") ? 0.1f : 1.0f;
	if (!TestTrue(TEXT("Real own observation and physical exhaustion arm one bounded attempt"), Enemy->PrepareTargetAttackAttempt(Duration))
		|| !TestTrue(TEXT("Armed attempt preserves all other attack gates"), Enemy->CanAttackTrackedTarget(Target)))
	{
		return false;
	}
	const int32 BeforeAmmo = Weapon->GetCurrentAmmo();
	if (!TestTrue(TEXT("Prepared aligned Gun starts actual weapon attack"), Enemy->StartAttackTarget(Target))
		|| !TestTrue(TEXT("Environment attempt consumes normal ammunition"), Weapon->GetCurrentAmmo() < BeforeAmmo))
	{
		return false;
	}
	FStructProperty* ResolvedShotProperty = FindFProperty<FStructProperty>(ARangedWeaponBase::StaticClass(), TEXT("LastResolvedShot"));
	if (!TestNotNull(TEXT("Actual server shot snapshot is available"), ResolvedShotProperty))
	{
		return false;
	}
	const FWeaponShotSnapshot* ResolvedShot = ResolvedShotProperty->ContainerPtrToValuePtr<FWeaponShotSnapshot>(Weapon);
	TestTrue(TEXT("Attempt produces an actual server hit"), ResolvedShot->ShotId > 0 && ResolvedShot->bHasImpact);
	TestEqual(TEXT("Real environment wall terminates the actual bullet"), ResolvedShot->HitActor.Get(), Fixture.Wall->GetOwner());
	if (Parameters == TEXT("HiddenBlocksNewShot") || Parameters == TEXT("DeathStopsShots"))
	{
		if (Parameters == TEXT("DeathStopsShots"))
		{
			FOutlierDamageRequest Request;
			Request.DamageAmount = 100000.0f;
			Request.DamageTag = OutlierGameplayTags::Damage::Ranged();
			Enemy->ReceiveOutlierDamage(Request);
			TestTrue(TEXT("Real lethal damage ends the attacker lifetime"), Enemy->IsDead());
		}
		else
		{
			Fixture.AI->SetVisibleTargetsForTesting({});
			Enemy->SetPlayerCurrentlyVisible(false);
		}
		const int32 HiddenAmmo = Weapon->GetCurrentAmmo();
		Fixture.Advance(0.15f);
		TestEqual(TEXT("Auto-fire timer cannot fire into hidden target"), Weapon->GetCurrentAmmo(), HiddenAmmo);
		TestFalse(TEXT("Tracking memory is not a shot permit"), Enemy->CanAttackTrackedTarget(Target));
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("ActualShotAndDeadline") || Parameters == TEXT("ShortBurstDeadline"))
	{
		// Task를 Tick하지 않고 자기 관측만 갱신한다. 자동 연사 콜백도 Pawn의 절대 종료 기한을 지켜야 한다.
		Fixture.Advance(Parameters == TEXT("ShortBurstDeadline") ? 0.12f : 0.52f, true);
		TestTrue(TEXT("Elapsed attempt reaches original finite deadline"), Enemy->HasTargetAttackAttemptExpired());
		const int32 AtDeadline = Weapon->GetCurrentAmmo();
		Fixture.Advance(0.15f, true);
		TestEqual(TEXT("Weapon callback cannot issue new rounds after deadline"), Weapon->GetCurrentAmmo(), AtDeadline);
		TestFalse(TEXT("Finite exception does not remove wall collision"), Weapon->HasClearTargetAttackPath(Target, Aim));
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("OpenShotDoesNotRecharge"))
	{
		// 진행 중 사선이 열리면 같은 버스트를 일반 사격으로 이어가되 사용 기록은 남긴다.
		Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fixture.Advance(0.1f, true);
		Fixture.Observe(Target);
		Enemy->RefreshCombatAimPoint();
		Fixture.Align();
		TestFalse(TEXT("Open line hands the active burst back to ordinary shooting"), Enemy->PrepareTargetAttackAttempt(0.4f));
		TestFalse(TEXT("Normal shooting no longer carries the attempt deadline"), Enemy->IsTargetAttemptActive());
		TestTrue(TEXT("Normal attack gates accept the now open target"), Enemy->CanAttackTrackedTarget(Target));
		Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	Enemy->FinishTargetAttackAttempt();
	Enemy->PauseTargetAttack();
	if (Parameters == TEXT("TargetRoundTrip"))
	{
		AShooterCharacter* Other = Fixture.SpawnTarget(FVector(1000, 500, 300));
		if (!TestNotNull(TEXT("Second actual target exists"), Other))
		{
			return false;
		}
		Fixture.Select(Other);
		Fixture.Select(Target);
	}
	else if (Parameters == TEXT("PreparationExpiryDoesNotRecharge"))
	{
		Fixture.Advance(3.2f);
		Fixture.Select(Target);
	}
	else if (Parameters == TEXT("FailureTTLDoesNotRecharge"))
	{
		// 기존 전술 실패 캐시의 30초 TTL과 시도 사용 기록의 수명을 분리한다.
		Fixture.Advance(30.2f);
		Fixture.Select(Target);
	}
	else if (Parameters == TEXT("OpenShotDoesNotRecharge"))
	{
		Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fixture.Advance(0.2f);
		Fixture.Observe(Target);
		Enemy->RefreshCombatAimPoint();
		Fixture.Align();
		TestTrue(TEXT("Unblocked control path reopens ordinary shooting"), Weapon->HasClearTargetAttackPath(Target, Enemy->GetCombatAimPoint(Target)));
		Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	else if (Parameters == TEXT("SmallJitterDoesNotRecharge"))
	{
		Enemy->SetActorLocation(Enemy->GetActorLocation() + FVector(0, 1, 0));
	}
	else if (Parameters == TEXT("StunDoesNotRecharge"))
	{
		Enemy->EnterStun();
		TestFalse(TEXT("Stun revokes active fire approval"), Enemy->CanAttackTrackedTarget(Target));
		Enemy->RestoreStateAfterStun();
		Fixture.Select(Target);
	}
	else if (Parameters == TEXT("PoolLeaseContract"))
	{
		FIntProperty* Lease = FindFProperty<FIntProperty>(AEnemyBase::StaticClass(), TEXT("PoolLeaseSerial"));
		if (!TestNotNull(TEXT("Existing pool identity exists"), Lease))
		{
			return false;
		}
		Lease->SetPropertyValue_InContainer(Enemy, Lease->GetPropertyValue_InContainer(Enemy) + 1);
		Fixture.Select(Target);
	}
	else if (Parameters == TEXT("ControllerContract"))
	{
		AEnemyAIController* NewController = Fixture.World->SpawnActor<AEnemyAIController>();
		if (!TestNotNull(TEXT("New controlling AI session exists"), NewController))
		{
			return false;
		}
		NewController->Possess(Enemy);
		Fixture.AI = NewController;
		Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled F3 new controller"));
		NewController->SetEnemyPerceptionEnabled(false);
		NewController->SetActorTickEnabled(false);
		Fixture.Select(Target);
	}
	else if (Parameters == TEXT("WeaponContract"))
	{
		ARangedWeaponBase* Replacement = Fixture.World->SpawnActor<ARangedWeaponBase>(Weapon->GetClass());
		FObjectPropertyBase* WeaponProperty = FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"));
		if (!TestNotNull(TEXT("Replacement ordinary weapon exists"), Replacement)
			|| !TestNotNull(TEXT("Existing weapon ownership property exists"), WeaponProperty))
		{
			return false;
		}
		Replacement->OnEquipped(Enemy);
		WeaponProperty->SetObjectPropertyValue_InContainer(Enemy, Replacement);
		Fixture.Select(Target);
	}
	else if (Parameters == TEXT("DifferentSightLine"))
	{
		// 같은 벽의 다른 쪽을 향하는 충분한 발사 원점·각도·접촉점 변화다.
		Enemy->SetActorLocation(Enemy->GetActorLocation() + FVector(0, 350, 0));
		Fixture.Wall->SetBoxExtent(FVector(20, 600, 300));
	}
	else if (Parameters == TEXT("RelatedObstacleChange") || Parameters == TEXT("UnrelatedObstacleChange"))
	{
		URoomCombatSubsystem* Rooms = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
		if (!TestNotNull(TEXT("Movement obstacle event source exists"), Rooms))
		{
			return false;
		}
		const FVector Change = Parameters == TEXT("RelatedObstacleChange")
			? Fixture.Wall->GetComponentLocation() : FVector(10000, 10000, 10000);
		Rooms->OnMovementObstacleOpened.Broadcast(FBox::BuildAABB(Change, FVector(50)));
	}
	Fixture.Advance(0.2f);
	Fixture.Observe(Target);
	Enemy->RefreshCombatAimPoint();
	Fixture.Align();
	Enemy->NoteTargetAttackAlternativesExhausted();
	if (Parameters == TEXT("RelatedObstacleChange") || Parameters == TEXT("DifferentSightLine")
		|| Parameters == TEXT("PoolLeaseContract") || Parameters == TEXT("ControllerContract") || Parameters == TEXT("WeaponContract"))
	{
		TestTrue(TEXT("A relevant obstacle or genuinely different firing line permits a new bounded evaluation"), Enemy->PrepareTargetAttackAttempt(1.0f));
	}
	else
	{
		TestFalse(TEXT("Same physical failure has only one attempt despite lifecycle or preparation changes"), Enemy->PrepareTargetAttackAttempt(1.0f));
	}
	return !HasAnyErrors();
}

#endif
