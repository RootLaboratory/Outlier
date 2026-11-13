#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
// 실제 Gun 무기와 Shooter TP 관측을 사용한다. Tree와 자율 이동만 멈춰 양보 인계의 소유권을 검사한다.
struct FF4YieldWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	AEnemyBase* Enemy = nullptr;
	AEnemyBase* Blocker = nullptr;
	AShooterCharacter* Target = nullptr;
	AEnemyAIController* AI = nullptr;
	UEnemyRoomSubsystem* Rooms = nullptr;

	~FF4YieldWorld()
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
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyF4Yield")), GetTransientPackage());
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
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("Production Gun BP loads"), GunClass)
			|| !Test.TestNotNull(TEXT("Production Shooter BP loads"), ShooterClass))
		{
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Target = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(1000, 0, 300), FRotator::ZeroRotator, Spawn);
		Enemy = World->SpawnActor<AEnemyBase>(GunClass, FVector(0, 0, 300), FRotator::ZeroRotator, Spawn);
		Blocker = World->SpawnActor<AEnemyBase>(GunClass, FVector(500, 500, 300), FRotator::ZeroRotator, Spawn);
		Rooms = World->GetSubsystem<UEnemyRoomSubsystem>();
		if (!Test.TestNotNull(TEXT("Actual Shooter spawns"), Target)
			|| !Test.TestNotNull(TEXT("Requesting Gun spawns"), Enemy)
			|| !Test.TestNotNull(TEXT("Cooperative Gun spawns"), Blocker)
			|| !Test.TestNotNull(TEXT("Existing room subsystem exists"), Rooms))
		{
			return false;
		}
		Target->SetActorEnableCollision(false);
		Target->SetCanBeDamaged(false);
		Target->GetCharacterMovement()->DisableMovement();
		Target->GetMesh()->SetComponentTickEnabled(false);
		for (AEnemyBase* Gun : {Enemy, Blocker})
		{
			AEnemyAIController* Controller = Cast<AEnemyAIController>(Gun->GetController());
			if (!Test.TestNotNull(TEXT("Production Gun owns an AI controller"), Controller)
				|| !Test.TestNotNull(TEXT("Production Gun owns a ranged weapon"), Gun->GetCurrentWeapon()))
			{
				return false;
			}
			Gun->EnterCombatFromRoom(Target->GetActorLocation(), false);
			Gun->GetStateTreeComponent()->StopLogic(TEXT("Controlled F4 yield integration"));
			Gun->SetActorTickEnabled(false);
			Gun->GetCharacterMovement()->DisableMovement();
			Gun->SetActorEnableCollision(false);
			Controller->SetEnemyPerceptionEnabled(false);
			Controller->SetActorTickEnabled(false);
			Controller->SetVisibleTargetsForTesting({Target});
			Gun->SetPlayerCurrentlyVisible(true);
			Gun->ObserveTrackedTarget(Target);
			Gun->BeginTargetAttackPreparation(0.0f);
			Gun->EnsureCombatTacticalEpisode(Target, Target->GetActorLocation());
			Rooms->RegisterEnemy(Gun);
		}
		AI = CastChecked<AEnemyAIController>(Enemy->GetController());
		return Test.TestTrue(TEXT("Actual TP bones provide requesting AI observation"), Observe());
	}

	bool Observe(AEnemyBase* Observer = nullptr)
	{
		Observer = Observer ? Observer : Enemy;
		AEnemyAIController* Controller = CastChecked<AEnemyAIController>(Observer->GetController());
		FCanBeSeenFromContext Context;
		Context.IgnoreActor = Observer;
		Context.ObserverLocation = Observer->GetPawnViewLocation();
		FVector Seen;
		int32 Sync = 0;
		int32 Async = 0;
		float Strength = 0.0f;
		Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		Target->GetMesh()->SetComponentTickEnabled(true);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		Target->GetMesh()->SetComponentTickEnabled(false);
		Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		Observer->RefreshCombatAimPoint();
		Controller->SetControlRotation((Observer->GetCombatAimPoint(Target) - Observer->GetCombatShotOrigin()).Rotation());
		FOutlierSightObservation Observation;
		return Target->GetObservedAimPoints(Observer, Observation);
	}

	void PlaceAllyOnMuzzleLine()
	{
		Blocker->SetActorLocation(FMath::Lerp(Enemy->GetCombatShotOrigin(), Enemy->GetCombatAimPoint(Target), 0.5f));
		Blocker->SetActorEnableCollision(true);
		Blocker->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UCapsuleComponent* Capsule = Blocker->GetCapsuleComponent();
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Enemy->UpdateCombatTacticalSightBlock();
	}

	void AddEnvironmentBeforeAlly()
	{
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(20, 200, 300));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Box->SetWorldLocation(FMath::Lerp(Enemy->GetCombatShotOrigin(), Enemy->GetCombatAimPoint(Target), 0.25f));
		Box->RegisterComponent();
	}

	void Advance(float Seconds)
	{
		const int32 Steps = FMath::CeilToInt(Seconds * 60.0f);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_TimeOnly, Seconds / Steps);
		}
	}

	bool ReadContext(uint64* OutSerial = nullptr) const
	{
		FVector Start;
		FVector End;
		uint64 Serial = 0;
		const bool bFound = Blocker->GetCombatYieldContext(Start, End, Serial);
		if (OutSerial)
		{
			*OutSerial = Serial;
		}
		return bFound;
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF4YieldIntegrationTest,
	"Outlier.Enemy.Combat.F4Yield.Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF4YieldIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("NormalReadiness"), TEXT("ActualAllyBlocker"), TEXT("EnvironmentFirst"),
		TEXT("QueryBudget"), TEXT("HiddenRequester"), TEXT("ContextExpiry"), TEXT("BlockerStun"),
		TEXT("BlockerDeath"), TEXT("BlockerPossession"), TEXT("BlockerLease"), TEXT("RequesterController"),
		TEXT("AcceptedMoveOutlivesRequest"), TEXT("OldReleaseKeepsNewMove"), TEXT("FirstEpisodePreservesRequest"),
		TEXT("ExistingGrantDefersYield"), TEXT("DirectStarvation")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyF4YieldIntegrationTest::RunTest(const FString& Parameters)
{
	FF4YieldWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AEnemyBase* Enemy = Fixture.Enemy;
	AEnemyBase* Blocker = Fixture.Blocker;
	ARangedWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (Parameters == TEXT("DirectStarvation"))
	{
		Blocker->SetActorLocation(FMath::Lerp(Enemy->GetCombatShotOrigin(), Enemy->GetCombatAimPoint(Fixture.Target), 0.5f));
		if (!TestTrue(TEXT("Blocking Gun has its own real TP observation"), Fixture.Observe(Blocker)))
		{
			return false;
		}
		Fixture.PlaceAllyOnMuzzleLine();
		if (!TestTrue(TEXT("Blocking Gun is normally attack-ready on its own open line"), Blocker->IsReadyForCombatYieldPriority())
			|| !TestFalse(TEXT("Requester cannot normally attack through the allied capsule"), Enemy->IsReadyForCombatYieldPriority()))
		{
			return false;
		}
		const FVector ProtectedStart = Enemy->GetCombatShotOrigin();
		const FVector ProtectedEnd = Enemy->GetCombatAimPoint(Fixture.Target);
		TestFalse(TEXT("Normally ready blocker wins the first directed competition"),
			Fixture.Rooms->RequestCombatYield(Enemy, Blocker, ProtectedStart, ProtectedEnd));
		FEnemyCombatYieldContext Pending;
		TestFalse(TEXT("Denied directed demand is not exposed as an approved yield request"),
			Fixture.Rooms->GetCombatYieldContext(Blocker, Pending));
		// 같은 사선의 재보고만 수행한다. 정상 공격 가능한 동료의 우선권이 대기 나이를 지우면 안 된다.
		for (int32 Index = 1; Index <= 4; ++Index)
		{
			Fixture.Advance(0.5f);
			Fixture.Observe(Blocker);
			Fixture.Observe();
			TestTrue(TEXT("Blocker retains actual normal attack eligibility during the wait"), Blocker->IsReadyForCombatYieldPriority());
			const bool bApproved = Fixture.Rooms->RequestCombatYield(Enemy, Blocker, ProtectedStart, ProtectedEnd);
			if (Index < 4)
			{
				TestFalse(TEXT("Directed demand remains deferred before two seconds of starvation"), bApproved);
				TestFalse(TEXT("Intermediate denied demand does not become a movement authority"),
					Fixture.Rooms->GetCombatYieldContext(Blocker, Pending));
			}
			else
			{
				TestTrue(TEXT("Same frozen directed demand receives the next opportunity after two seconds"), bApproved);
				TestTrue(TEXT("Approved request is now exposed to the blocker"), Fixture.Rooms->GetCombatYieldContext(Blocker, Pending));
				TestTrue(TEXT("Approved directed request has an identity"), Pending.Serial != 0);
				TestEqual(TEXT("Approved request retains the actual requesting Gun"), Pending.Requester.Get(), Enemy);
				TestTrue(TEXT("Starvation does not substitute a different protected muzzle line"),
					Pending.Start.Equals(ProtectedStart) && Pending.End.Equals(ProtectedEnd));
				const uint64 ApprovedSerial = Pending.Serial;
				TestTrue(TEXT("Re-reporting the approved demand remains valid"),
					Fixture.Rooms->RequestCombatYield(Enemy, Blocker, ProtectedStart, ProtectedEnd));
				Fixture.Rooms->GetCombatYieldContext(Blocker, Pending);
				TestEqual(TEXT("Same approved demand keeps its yield identity"), Pending.Serial, ApprovedSerial);
			}
		}
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("NormalReadiness"))
	{
		TestTrue(TEXT("Aligned observed Gun with ordinary open path has priority eligibility"), Enemy->IsReadyForCombatYieldPriority());
		Fixture.AddEnvironmentBeforeAlly();
		Enemy->NoteTargetAttackAlternativesExhausted();
		Enemy->RefreshCombatAimPoint();
		TestTrue(TEXT("Control environment exception can be armed"), Enemy->PrepareTargetAttackAttempt(0.5f));
		TestFalse(TEXT("An F3 environment exception is not ordinary attack readiness"), Enemy->IsReadyForCombatYieldPriority());
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("EnvironmentFirst"))
	{
		Fixture.AddEnvironmentBeforeAlly();
	}
	// 일반 인계는 아직 조준 중인 동료가 비키는 상황이다. 이미 공격 가능한 동료의 우선권은 DirectStarvation에서 별도로 검사한다.
	CastChecked<AEnemyAIController>(Blocker->GetController())->SetControlRotation(FRotator(0, 180, 0));
	if (Parameters == TEXT("HiddenRequester"))
	{
		Fixture.AI->SetVisibleTargetsForTesting({});
		Enemy->SetPlayerCurrentlyVisible(false);
	}
	if (Parameters == TEXT("FirstEpisodePreservesRequest"))
	{
		// 정지 사격하던 동료는 아직 전술 회차가 없을 수 있다. 요청을 받은 뒤 첫 회차를 만드는 흐름이다.
		Blocker->ResetCombatTacticalEpisode();
		TestEqual(TEXT("Blocker starts without a movement episode"), Blocker->GetCombatTacticalEpisodeSerial(), uint64(0));
	}
	uint64 ExistingGrant = 0;
	if (Parameters == TEXT("ExistingGrantDefersYield"))
	{
		// 양보 요청의 후보 순서는 먼저 확보하되, 동료의 현재 이동 허가를 중단할 권한은 부여하지 않는다.
		Blocker->SetActorEnableCollision(true);
		Blocker->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Enemy->BeginCombatReservationRequest();
		ExistingGrant = Blocker->BeginCombatReservationRequest();
		const FVector Start = Blocker->GetCapsuleComponent()->GetComponentLocation();
		const FVector Destination = Start + FVector(0, 500, 0);
		if (!TestTrue(TEXT("Blocker receives a normal movement grant before the new yield request"),
			Fixture.Rooms->TryReserveCombatDestination(Blocker, ExistingGrant, Destination, {}, {Start, Destination}, 5.0f))
			|| !TestTrue(TEXT("Blocker adopts the existing non-yield movement grant"), Blocker->AdoptCombatReservation(ExistingGrant)))
		{
			return false;
		}
	}
	Fixture.PlaceAllyOnMuzzleLine();
	TestFalse(TEXT("An ally still turning toward its target does not claim attack-ready priority"), Blocker->IsReadyForCombatYieldPriority());
	int32 Checks = Parameters == TEXT("QueryBudget") ? 0 : 16;
	FVector ShotOrigin;
	AEnemyBase* Found = Weapon->FindTargetAttackBlockingAlly(Fixture.Target, Enemy->GetCombatAimPoint(Fixture.Target), Checks, ShotOrigin);
	if (Parameters == TEXT("EnvironmentFirst") || Parameters == TEXT("QueryBudget"))
	{
		TestNull(TEXT("No ally is inferred through an earlier environment hit or without query budget"), Found);
		if (Parameters == TEXT("EnvironmentFirst"))
		{
			TestFalse(TEXT("Environment obstruction does not request an unrelated ally movement"), Fixture.ReadContext());
		}
		TestTrue(TEXT("Blocker detection keeps collision-query budget bounded"), Checks >= 0);
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("HiddenRequester"))
	{
		TestFalse(TEXT("Tracking grace alone does not request movement for a hidden target's shot"), Fixture.ReadContext());
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("ExistingGrantDefersYield"))
	{
		FEnemyCombatYieldContext Pending;
		if (!TestTrue(TEXT("Actual ally obstruction can queue a valid next-boundary yield request"),
			Fixture.Rooms->GetCombatYieldContext(Blocker, Pending)))
		{
			return false;
		}
		TestFalse(TEXT("Pending yield cannot interrupt an already granted ordinary move"), Fixture.ReadContext());
		TestFalse(TEXT("Existing movement grant suppresses yield-driven tactical reselection"), Blocker->NeedsCombatTacticalReposition());
		TestTrue(TEXT("Pending yield does not revoke the existing reservation"),
			Fixture.Rooms->IsCombatReservationCurrent(Blocker, ExistingGrant));
		Blocker->ReleaseCombatReservation(ExistingGrant);
		uint64 AdoptableSerial = 0;
		TestTrue(TEXT("The same pending request becomes available after the grant's safe release"), Fixture.ReadContext(&AdoptableSerial));
		TestEqual(TEXT("Safe movement boundary preserves the queued yield identity"), AdoptableSerial, Pending.Serial);
		TestTrue(TEXT("Released ordinary move allows the next finite yield selection"), Blocker->NeedsCombatTacticalReposition());
		return !HasAnyErrors();
	}
	if (!TestEqual(TEXT("Actual muzzle trace identifies the cooperative Gun"), Found, Blocker)
		|| !TestTrue(TEXT("Existing tactical update reports an actual ally obstruction"), Fixture.ReadContext()))
	{
		return false;
	}
	TestFalse(TEXT("Ally obstruction never grants normal shooting"), Weapon->HasClearTargetAttackPath(Fixture.Target, Enemy->GetCombatAimPoint(Fixture.Target)));
	TestTrue(TEXT("Pending safe yield enters existing tactical reposition selection"), Blocker->NeedsCombatTacticalReposition());
	if (Parameters == TEXT("FirstEpisodePreservesRequest"))
	{
		uint64 BeforeSerial = 0;
		Fixture.ReadContext(&BeforeSerial);
		TestTrue(TEXT("First tactical movement episode starts for the blocker's own target"),
			Blocker->EnsureCombatTacticalEpisode(Blocker->GetTrackedTarget(),
				Blocker->GetCombatNavigationTargetLocation(Blocker->GetTrackedTarget())) != 0);
		uint64 AfterSerial = 0;
		TestTrue(TEXT("Creating the first episode preserves the newly received yield request"), Fixture.ReadContext(&AfterSerial));
		TestEqual(TEXT("First episode does not replace the pending yield identity"), AfterSerial, BeforeSerial);
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("ActualAllyBlocker"))
	{
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("ContextExpiry"))
	{
		Fixture.Advance(1.1f);
	}
	else if (Parameters == TEXT("BlockerStun"))
	{
		Blocker->EnterStun();
	}
	else if (Parameters == TEXT("BlockerDeath"))
	{
		FOutlierDamageRequest Damage;
		Damage.DamageAmount = 100000.0f;
		Damage.DamageTag = OutlierGameplayTags::Damage::Ranged();
		Blocker->ReceiveOutlierDamage(Damage);
		TestTrue(TEXT("Real lethal damage ends blocker lifetime"), Blocker->IsDead());
	}
	else if (Parameters == TEXT("BlockerPossession"))
	{
		Blocker->SetEnemyPossessed(true);
	}
	else if (Parameters == TEXT("BlockerLease"))
	{
		FIntProperty* Lease = FindFProperty<FIntProperty>(AEnemyBase::StaticClass(), TEXT("PoolLeaseSerial"));
		if (!TestNotNull(TEXT("Existing lease identity is available"), Lease))
		{
			return false;
		}
		Lease->SetPropertyValue_InContainer(Blocker, Lease->GetPropertyValue_InContainer(Blocker) + 1);
	}
	else if (Parameters == TEXT("RequesterController"))
	{
		Fixture.AI->UnPossess();
	}
	else if (Parameters == TEXT("AcceptedMoveOutlivesRequest") || Parameters == TEXT("OldReleaseKeepsNewMove"))
	{
		uint64 YieldSerial = 0;
		Fixture.ReadContext(&YieldSerial);
		const FVector Start = Blocker->GetCapsuleComponent()->GetComponentLocation();
		const FVector Destination = Start + FVector(0, 500, 0);
		const uint64 OldRequest = Blocker->BeginCombatReservationRequest();
		if (!TestTrue(TEXT("Actual safe lateral route receives ordinary space reservation"),
			Fixture.Rooms->TryReserveCombatDestination(Blocker, OldRequest, Destination, {}, {Start, Destination}, 5.0f))
			|| !TestTrue(TEXT("Blocker adopts its own reservation"), Blocker->AdoptCombatReservation(OldRequest)))
		{
			return false;
		}
		Blocker->BeginCombatYieldMovement(OldRequest, YieldSerial);
		if (Parameters == TEXT("OldReleaseKeepsNewMove"))
		{
			const uint64 NewRequest = Blocker->BeginCombatReservationRequest();
			if (!TestTrue(TEXT("New request can atomically replace its own destination"),
				Fixture.Rooms->TryReserveCombatDestination(Blocker, NewRequest, Destination, {}, {Start, Destination}, 5.0f))
				|| !TestTrue(TEXT("New request is adopted"), Blocker->AdoptCombatReservation(NewRequest)))
			{
				return false;
			}
			Blocker->BeginCombatYieldMovement(NewRequest, YieldSerial);
			Blocker->ReleaseCombatReservation(OldRequest);
			TestTrue(TEXT("Previous movement Exit cannot clear the new yield path"), Fixture.ReadContext());
			TestTrue(TEXT("Late Exit preserves the new reservation"), Fixture.Rooms->IsCombatReservationCurrent(Blocker, NewRequest));
		}
		else
		{
			// 실이동의 전진만 기존 예약 TTL을 갱신한다. 양보 요청의 1초 수명은 되살리지 않는다.
			for (int32 Index = 1; Index <= 4; ++Index)
			{
				Fixture.Advance(0.3f);
				Blocker->SetActorLocation(Start + FVector(0, Index * 80, 0));
				TestTrue(TEXT("Verified route forward progress retains reservation"), Blocker->RefreshCombatReservation(OldRequest));
			}
			FEnemyCombatYieldContext Expired;
			TestFalse(TEXT("Original unanswered yield request expires independently"), Fixture.Rooms->GetCombatYieldContext(Blocker, Expired));
			TestTrue(TEXT("Accepted finite move survives its source request expiry"), Fixture.ReadContext());
		}
		return !HasAnyErrors();
	}
	TestFalse(TEXT("Expired or invalid owner cannot retain pending yield authority"), Fixture.ReadContext());
	return !HasAnyErrors();
}

#endif
