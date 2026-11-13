#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAIController.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"
#include "Weapon/LaserWeapon.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
struct FF2TurretSelectionWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	AAutoTurret* Turret = nullptr;
	AEnemyAIController* AI = nullptr;
	AShooterCharacter* Shooter = nullptr;
	APartnerCharacter* Partner = nullptr;

	~FF2TurretSelectionWorld()
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
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("F2TurretSelection")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Isolated turret selection world exists"), World))
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
		UClass* TurretClass = LoadClass<AAutoTurret>(nullptr,
			TEXT("/Game/Blueprints/Enemy/Turret/BP_RifleTurret.BP_RifleTurret_C"));
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
			TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
		if (!Test.TestNotNull(TEXT("Saved rifle turret BP loads"), TurretClass)
			|| !Test.TestNotNull(TEXT("Saved Shooter BP loads"), ShooterClass)
			|| !Test.TestNotNull(TEXT("Saved Partner BP loads"), PartnerClass))
		{
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& Room = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Room.RoomTag = RoomTag;
		FRoomCombatPhaseDefinition& Phase = Room.CombatPhases.AddDefaulted_GetRef();
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::ExternalTrigger;
		FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
		Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		Wave.ExpectedWaveTurretCount = 1;
		URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
		if (!Test.TestNotNull(TEXT("Turret fixture has a combat registry"), Combat))
		{
			return false;
		}
		Combat->SetCombatDefinitionForTesting(Definition);
		const FTransform TurretTransform(FRotator::ZeroRotator, FVector(0, 0, 400));
		Turret = World->SpawnActorDeferred<AAutoTurret>(TurretClass, TurretTransform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test.TestNotNull(TEXT("Production turret deferred spawn succeeds"), Turret))
		{
			return false;
		}
		// 저장 BP도 BeginPlay 이전에 실제 배치 계약을 갖춰야 한다. 등록 오류를 예상 로그로 숨기지 않는다.
		Turret->ConfigureWaveRegistrationForTesting(RoomTag, 0, 0, TEXT("Enemy.F2.Selection.Turret"));
		Turret->FinishSpawning(TurretTransform);
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(1000, 0, 400), FRotator::ZeroRotator, Spawn);
		Partner = World->SpawnActor<APartnerCharacter>(PartnerClass, FVector(1000, 1000, 400), FRotator::ZeroRotator, Spawn);
		if (!Test.TestNotNull(TEXT("Saved rifle turret spawns"), Turret)
			|| !Test.TestNotNull(TEXT("Saved Shooter spawns"), Shooter)
			|| !Test.TestNotNull(TEXT("Saved Partner spawns"), Partner))
		{
			return false;
		}
		AI = Cast<AEnemyAIController>(Turret->GetController());
		if (!AI)
		{
			AI = World->SpawnActor<AEnemyAIController>();
			if (AI)
			{
				AI->Possess(Turret);
			}
		}
		if (!Test.TestNotNull(TEXT("Turret controller exists"), AI)
			|| !Test.TestNotNull(TEXT("Production turret weapon exists"), Turret->GetCurrentWeapon()))
		{
			return false;
		}
		StopAutomaticBehavior();
		for (ACharacter* Character : {static_cast<ACharacter*>(Turret), static_cast<ACharacter*>(Shooter), static_cast<ACharacter*>(Partner)})
		{
			Character->SetActorEnableCollision(false);
			Character->GetCharacterMovement()->DisableMovement();
			Character->GetMesh()->SetComponentTickEnabled(false);
		}
		return true;
	}

	void StopAutomaticBehavior()
	{
		Turret->GetStateTreeComponent()->StopLogic(TEXT("Controlled F2 turret selection"));
		Turret->SetActorTickEnabled(false);
		AI->SetEnemyPerceptionEnabled(false);
		AI->SetActorTickEnabled(false);
	}

	bool ObserveShooter()
	{
		FCanBeSeenFromContext Context;
		Context.IgnoreActor = Turret;
		Context.ObserverLocation = Turret->GetPawnViewLocation();
		FVector Seen;
		int32 Sync = 0;
		int32 Async = 0;
		float Strength = 0.0f;
		Shooter->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		// 최초 조회의 포즈 요청을 실제 메시 Tick으로 완료한다. 본 좌표나 관측 결과를 주입하지 않는다.
		Shooter->GetMesh()->SetComponentTickEnabled(true);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		Shooter->GetMesh()->SetComponentTickEnabled(false);
		Shooter->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		FOutlierSightObservation Observation;
		return Shooter->GetObservedAimPoints(Turret, Observation);
	}

	void Select(AActor* Target)
	{
		AI->SetVisibleTargetsForTesting({Target});
		Turret->SetPlayerCurrentlyVisible(true);
		Turret->ObserveTrackedTarget(Target);
		Turret->RefreshTargetTrackingAtSelectionBoundary();
	}

	void AdvanceWorld(float Seconds)
	{
		float Remaining = Seconds;
		while (Remaining > KINDA_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Remaining, 0.05f);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			Remaining -= Step;
		}
	}

	UBoxComponent* BlockPath(const FVector& Target, double HalfWidth = 130.0)
	{
		AActor* Wall = World->SpawnActor<AActor>();
		if (!Wall)
		{
			return nullptr;
		}
		UBoxComponent* Cover = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Cover);
		Cover->SetBoxExtent(FVector(20, HalfWidth, 600));
		Cover->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Cover->SetCollisionObjectType(ECC_WorldStatic);
		Cover->SetCollisionResponseToAllChannels(ECR_Block);
		Cover->SetWorldLocation((Turret->GetPawnViewLocation() + Target) * 0.5);
		Cover->RegisterComponent();
		return Cover;
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF2TurretSelectionTest,
	"Outlier.Enemy.Turret.F2Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF2TurretSelectionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("BlockedShooterOpenPartner"), TEXT("KeepAttackablePartner"),
		TEXT("BothBlockedKeepCurrent"), TEXT("CooldownIsNotObstruction"), TEXT("OutOfRangeCurrent"),
		TEXT("UnreachablePitchCurrent"), TEXT("DirectCandidatesOnly"), TEXT("CandidateQueryIsReadOnly"),
		TEXT("CommittedLaserUnchanged"), TEXT("CadencePreservedOnTargetCooldown"), TEXT("TargetSwitchPreservesCadence")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyF2TurretSelectionTest::RunTest(const FString& Parameters)
{
	FF2TurretSelectionWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AAutoTurret* Turret = Fixture.Turret;
	AEnemyAIController* AI = Fixture.AI;
	AShooterCharacter* Shooter = Fixture.Shooter;
	APartnerCharacter* Partner = Fixture.Partner;
	// 격리 월드에는 Wave 등록이 없다. 기존 테스트처럼 활성 경계만 제공하고 실제 BP/무기/총구는 유지한다.
	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
	Turret->EnterCombatFromRoom(Shooter->GetActorLocation(), false);
	Fixture.StopAutomaticBehavior();
	if (!TestTrue(TEXT("Turret uses ordinary projectile policy"), Turret->UsesNormalTurretAttackPolicy()))
	{
		return false;
	}
	Fixture.Select(Shooter);
	if (!TestTrue(TEXT("Shooter has a fresh observer-specific TP sight sample"), Fixture.ObserveShooter()))
	{
		return false;
	}
	FVector AimPoint;
	if (!TestTrue(TEXT("Control Shooter has a real open turret muzzle path"), Turret->TryGetAttackableVisibleTargetAim(Shooter, AimPoint)))
	{
		return false;
	}

	if (Parameters == TEXT("KeepAttackablePartner") || Parameters == TEXT("CooldownIsNotObstruction"))
	{
		Fixture.Select(Partner);
		if (!TestTrue(TEXT("Other visible Shooter has a current TP observation after selection change"), Fixture.ObserveShooter()))
		{
			return false;
		}
		AI->SetVisibleTargetsForTesting({Shooter, Partner});
		if (Parameters == TEXT("CooldownIsNotObstruction"))
		{
			FFloatProperty* Duration = FindFProperty<FFloatProperty>(ARangedWeaponBase::StaticClass(), TEXT("PostBurstCooldownSeconds"));
			if (!TestNotNull(TEXT("Existing weapon post-burst policy exists"), Duration))
			{
				return false;
			}
			Duration->SetPropertyValue_InContainer(Turret->GetCurrentWeapon(), 2.0f);
			Turret->GetCurrentWeapon()->ForcePostBurstCooldown();
			TestFalse(TEXT("Control weapon is genuinely cooling down"), Turret->GetCurrentWeapon()->CanAttack());
		}
		TestEqual(TEXT("Current attackable Partner outranks a newly seen higher-kind Shooter"),
			AI->GetPreferredAttackableTurretTarget(Partner), static_cast<AActor*>(Partner));
		Turret->RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("Tracking retains the current attackable Partner"), Turret->GetTrackedTarget(), static_cast<AActor*>(Partner));
	}
	else if (Parameters == TEXT("BlockedShooterOpenPartner") || Parameters == TEXT("BothBlockedKeepCurrent")
		|| Parameters == TEXT("CandidateQueryIsReadOnly"))
	{
		AI->SetVisibleTargetsForTesting({Shooter, Partner});
		if (!TestNotNull(TEXT("Real Shooter cover exists"), Fixture.BlockPath(Shooter->GetActorLocation())))
		{
			return false;
		}
		TestFalse(TEXT("Real muzzle geometry blocks every observed Shooter part"),
			Turret->TryGetAttackableVisibleTargetAim(Shooter, AimPoint));
		if (Parameters == TEXT("BothBlockedKeepCurrent"))
		{
			if (!TestNotNull(TEXT("Real Partner cover exists"), Fixture.BlockPath(Partner->GetActorLocation())))
			{
				return false;
			}
			TestFalse(TEXT("Partner is physically blocked too"), Turret->TryGetAttackableVisibleTargetAim(Partner, AimPoint));
			TestNull(TEXT("Blocked candidates do not invent an attackable target"), AI->GetPreferredAttackableTurretTarget(Shooter));
			Turret->RefreshTargetTrackingAtSelectionBoundary();
			TestEqual(TEXT("Both blocked retains valid current observation"), Turret->GetTrackedTarget(), static_cast<AActor*>(Shooter));
		}
		else
		{
			TestTrue(TEXT("Partner actual muzzle path remains open"), Turret->TryGetAttackableVisibleTargetAim(Partner, AimPoint));
			Turret->RememberTargetAttackRecovery(2.0f);
			const FVector PreviousAim = Turret->GetCombatAimPoint(Shooter);
			TestEqual(TEXT("Attackable selection overrides blocked Shooter kind priority"),
				AI->GetPreferredAttackableTurretTarget(Shooter), static_cast<AActor*>(Partner));
			TestEqual(TEXT("Candidate query does not replace tracked target"), Turret->GetTrackedTarget(), static_cast<AActor*>(Shooter));
			TestTrue(TEXT("Candidate query does not overwrite selected aim"), Turret->GetCombatAimPoint(Shooter).Equals(PreviousAim));
			TestTrue(TEXT("Candidate query does not alter recovery deadline"), FMath::IsNearlyEqual(Turret->GetTargetAttackRecoveryRemaining(), 2.0f));
			if (Parameters != TEXT("CandidateQueryIsReadOnly"))
			{
				Turret->RefreshTargetTrackingAtSelectionBoundary();
				TestEqual(TEXT("Tracking adopts the actually attackable Partner"), Turret->GetTrackedTarget(), static_cast<AActor*>(Partner));
				TestTrue(TEXT("Target handoff preserves common recovery"), FMath::IsNearlyEqual(Turret->GetTargetAttackRecoveryRemaining(), 2.0f));
			}
		}
	}
	else if (Parameters == TEXT("OutOfRangeCurrent") || Parameters == TEXT("UnreachablePitchCurrent"))
	{
		if (Parameters == TEXT("OutOfRangeCurrent"))
		{
			Shooter->SetActorLocation(Turret->GetPawnViewLocation() + FVector(Turret->GetCurrentWeapon()->GetEffectiveRange() + 2000, 0, 0));
		}
		else
		{
			Shooter->SetActorLocation(Turret->GetPawnViewLocation() + FVector(100, 0, -2000));
		}
		if (!TestTrue(TEXT("Moved Shooter obtains current TP observation"), Fixture.ObserveShooter()))
		{
			return false;
		}
		AI->SetVisibleTargetsForTesting({Shooter, Partner});
		TestFalse(TEXT("Physical range or raw pitch excludes current Shooter"), Turret->TryGetAttackableVisibleTargetAim(Shooter, AimPoint));
		TestEqual(TEXT("A reachable in-range Partner wins"), AI->GetPreferredAttackableTurretTarget(Shooter), static_cast<AActor*>(Partner));
		Turret->RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("Selection adopts the reachable alternative"), Turret->GetTrackedTarget(), static_cast<AActor*>(Partner));
	}
	else if (Parameters == TEXT("DirectCandidatesOnly"))
	{
		AI->SetVisibleTargetsForTesting({Partner});
		TestFalse(TEXT("Remembered Shooter cannot become a physical attack candidate without own direct sight"),
			Turret->TryGetAttackableVisibleTargetAim(Shooter, AimPoint));
		TestEqual(TEXT("Only the direct Partner can win candidate selection"),
			AI->GetPreferredAttackableTurretTarget(Shooter), static_cast<AActor*>(Partner));
	}
	else if (Parameters == TEXT("CommittedLaserUnchanged"))
	{
		ALaserWeapon* Laser = Fixture.World->SpawnActor<ALaserWeapon>();
		FObjectPropertyBase* WeaponProperty = FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"));
		if (!TestNotNull(TEXT("Dedicated laser spawns"), Laser) || !TestNotNull(TEXT("Existing weapon property exists"), WeaponProperty))
		{
			return false;
		}
		Laser->OnEquipped(Turret);
		WeaponProperty->SetObjectPropertyValue_InContainer(Turret, Laser);
		Turret->SetAttackPhase(EEnemyAttackPhase::Telegraph);
		AI->SetVisibleTargetsForTesting({Partner});
		TestFalse(TEXT("Laser remains outside ordinary turret policy"), Turret->UsesNormalTurretAttackPolicy());
		TestNull(TEXT("Ordinary attackable-candidate query cannot alter laser policy"), AI->GetPreferredAttackableTurretTarget(Shooter));
		Turret->RefreshTargetTracking();
		TestEqual(TEXT("Committed laser preserves its original target"), Turret->GetTrackedTarget(), static_cast<AActor*>(Shooter));
	}
	else if (Parameters == TEXT("CadencePreservedOnTargetCooldown") || Parameters == TEXT("TargetSwitchPreservesCadence"))
	{
		ARangedWeaponBase* Weapon = Turret->GetCurrentWeapon();
		FFloatProperty* Interval = FindFProperty<FFloatProperty>(ARangedWeaponBase::StaticClass(), TEXT("AttackInterval"));
		FFloatProperty* Cooldown = FindFProperty<FFloatProperty>(ARangedWeaponBase::StaticClass(), TEXT("PostBurstCooldownSeconds"));
		FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
		FIntProperty* BurstCount = FindFProperty<FIntProperty>(ARangedWeaponBase::StaticClass(), TEXT("BurstShotCount"));
		if (!TestNotNull(TEXT("Weapon interval exists"), Interval) || !TestNotNull(TEXT("Weapon cooldown exists"), Cooldown)
			|| !TestNotNull(TEXT("Weapon ammo policy exists"), InfiniteAmmo) || !TestNotNull(TEXT("Weapon burst policy exists"), BurstCount))
		{
			return false;
		}
		Interval->SetPropertyValue_InContainer(Weapon, 0.8f);
		Cooldown->SetPropertyValue_InContainer(Weapon, 0.1f);
		InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
		BurstCount->SetPropertyValue_InContainer(Weapon, 0);
		Turret->BeginTargetAttackPreparation(0.0f);
		Turret->RefreshCombatAimPoint();
		for (int32 Step = 0; Step < 20; ++Step)
		{
			Turret->UpdateTurretAimAtLocation(Turret->GetCombatAimPoint(Shooter), 0.1f, true);
		}
		const int32 BeforeShot = Weapon->GetCurrentAmmo();
		const double FirstShotTime = Fixture.World->GetTimeSeconds();
		if (!TestTrue(TEXT("Prepared and aligned real turret starts fire"), Turret->StartAttackTarget(Shooter))
			|| !TestTrue(TEXT("Cadence control consumes actual ammunition"), Weapon->GetCurrentAmmo() < BeforeShot))
		{
			return false;
		}
		const int32 AfterShot = Weapon->GetCurrentAmmo();
		Fixture.AdvanceWorld(0.02f);
		if (Parameters == TEXT("TargetSwitchPreservesCadence"))
		{
			// 실제 대상 인계가 Task Tick/Exit 이전에도 부분 버스트를 동일하게 정리해야 한다.
			Fixture.Select(Partner);
			TestEqual(TEXT("Actual target selection hands off to Partner"), Turret->GetTrackedTarget(), static_cast<AActor*>(Partner));
			TestTrue(TEXT("Target boundary starts partial-burst recovery without waiting for task exit"), Weapon->IsOnPostBurstCooldown());
		}
		else
		{
			Turret->PauseTargetAttack();
			Weapon->ForceTargetAttackPostBurstCooldown();
		}
		// 총구 두 개의 동시 발사는 두 탄 간격을 사용한다. 실제 소비량으로 원래 그룹 기한을 계산한다.
		const double ExpectedGroupReadyAt = FirstShotTime + 0.8 * (BeforeShot - AfterShot);
		const double PreservedReadyAt = Fixture.World->GetTimeSeconds() + Weapon->GetAttackRetryDelay();
		TestTrue(TEXT("Target interruption preserves at least the actual fired muzzle-group interval"),
			PreservedReadyAt >= ExpectedGroupReadyAt - 0.02);
		Fixture.AdvanceWorld(0.06f);
		Weapon->ForceTargetAttackPostBurstCooldown();
		Fixture.AdvanceWorld(0.05f);
		TestFalse(TEXT("Repeated recovery requests cannot move the original recovery deadline"), Weapon->IsOnPostBurstCooldown());
		Fixture.AdvanceWorld(0.09f);
		TestFalse(TEXT("Shorter target-change recovery has finished"), Weapon->IsOnPostBurstCooldown());
		TestFalse(TEXT("Finishing recovery cannot erase the longer original shot interval"), Weapon->CanAttack());
		TestEqual(TEXT("Target-change recovery cannot inject extra rounds"), Weapon->GetCurrentAmmo(), AfterShot);
		Fixture.AdvanceWorld(static_cast<float>(FMath::Max(0.0, PreservedReadyAt - Fixture.World->GetTimeSeconds()) + 0.05));
		TestTrue(TEXT("Original shot interval eventually permits firing again"), Weapon->CanAttack());
	}
	return !HasAnyErrors();
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF3TurretAttemptTest,
	"Outlier.Enemy.Turret.F3Attempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF3TurretAttemptTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("BlockedOwnSight"), TEXT("OpenPartnerFirst"), TEXT("FiniteShots"), TEXT("RoundTripKeepsUse")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyF3TurretAttemptTest::RunTest(const FString& Parameters)
{
	FF2TurretSelectionWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AAutoTurret* Turret = Fixture.Turret;
	ARangedWeaponBase* Weapon = Turret->GetCurrentWeapon();
	FEnumProperty* Lifecycle = FindFProperty<FEnumProperty>(AAutoTurret::StaticClass(), TEXT("TurretLifecycleState"));
	if (!TestNotNull(TEXT("Existing turret activation state exists"), Lifecycle))
	{
		return false;
	}
	Lifecycle->GetUnderlyingProperty()->SetIntPropertyValue(Lifecycle->ContainerPtrToValuePtr<void>(Turret), uint64(EAutoTurretLifecycleState::Active));
	Turret->EnterCombatFromRoom(Fixture.Shooter->GetActorLocation(), false);
	Fixture.StopAutomaticBehavior();
	Fixture.Select(Fixture.Shooter);
	if (!TestTrue(TEXT("Turret has its own real TP observation"), Fixture.ObserveShooter()))
	{
		return false;
	}
	Turret->RefreshCombatAimPoint();
	UBoxComponent* Wall = Fixture.BlockPath(Turret->GetCombatAimPoint(Fixture.Shooter));
	if (!TestNotNull(TEXT("Real cover blocks independent muzzle paths"), Wall))
	{
		return false;
	}
	// 시야와 총구 차단을 분리한다. 관측은 유효하지만 모든 실제 총구가 환경에 막힌 조건이다.
	Wall->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Turret->BeginTargetAttackPreparation(0.0f);
	for (int32 Step = 0; Step < 20; ++Step)
	{
		Turret->UpdateTurretAimAtLocation(Turret->GetCombatAimPoint(Fixture.Shooter), 0.1f, true);
	}
	if (Parameters == TEXT("OpenPartnerFirst"))
	{
		Fixture.AI->SetVisibleTargetsForTesting({Fixture.Shooter, Fixture.Partner});
		TestFalse(TEXT("Other open directly visible candidate prevents a blocked-target attempt"), Turret->PrepareTargetAttackAttempt(0.5f));
		TestEqual(TEXT("Normal selection prefers open Partner"), Fixture.AI->GetPreferredAttackableTurretTarget(Fixture.Shooter), static_cast<AActor*>(Fixture.Partner));
		return !HasAnyErrors();
	}
	FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"))->SetPropertyValue_InContainer(Weapon, false);
	FTargetAttemptPathEvidence Evidence;
	int32 NormalChecks = 16;
	AddInfo(FString::Printf(TEXT("F3Turret Direct=%d Tracked=%s Normal=%d Candidate=%d Budget=%d AttemptPath=%d Blocker=%s"),
		Fixture.AI->IsTargetDirectlyVisible(Fixture.Shooter), *GetNameSafe(Turret->GetTrackedTarget()),
		Weapon->HasClearTargetAttackPath(Fixture.Shooter, Turret->GetCombatAimPoint(Fixture.Shooter)),
		Turret->HasCombatAttackPathFrom(Fixture.Shooter, Turret->GetActorLocation(), NormalChecks), NormalChecks,
		Weapon->HasTargetAttemptPath(Fixture.Shooter, Turret->GetCombatAimPoint(Fixture.Shooter), &Evidence), *GetNameSafe(Evidence.Blocker.Get())));
	TestFalse(TEXT("Fixed turret does not start a movement EQS episode"), Turret->GetCombatTacticalEpisodeSerial() != 0);
	if (!TestTrue(TEXT("Fixed turret can evaluate its blocked current target without movement EQS"), Turret->PrepareTargetAttackAttempt(0.5f)))
	{
		return false;
	}
	const int32 Ammo = Weapon->GetCurrentAmmo();
	TestTrue(TEXT("Actual prepared turret starts limited fire"), Turret->StartAttackTarget(Fixture.Shooter));
	TestTrue(TEXT("Independent muzzle group consumes normal ammunition"), Weapon->GetCurrentAmmo() < Ammo);
	if (Parameters == TEXT("FiniteShots"))
	{
		for (int32 Step = 0; Step < 14; ++Step)
		{
			Fixture.AdvanceWorld(0.05f);
			Fixture.ObserveShooter();
			Turret->RefreshCombatAimPoint();
		}
		TestTrue(TEXT("Turret uses same finite absolute attempt deadline"), Turret->HasTargetAttackAttemptExpired());
		const int32 FinishedAmmo = Weapon->GetCurrentAmmo();
		Fixture.AdvanceWorld(0.2f);
		TestEqual(TEXT("No late callback can shoot another group"), Weapon->GetCurrentAmmo(), FinishedAmmo);
	}
	Turret->FinishTargetAttackAttempt();
	Turret->PauseTargetAttack();
	if (Parameters == TEXT("RoundTripKeepsUse"))
	{
		Fixture.Select(Fixture.Partner);
		Fixture.Select(Fixture.Shooter);
		Fixture.ObserveShooter();
		Turret->RefreshCombatAimPoint();
	}
	TestFalse(TEXT("Same turret obstruction cannot recharge an already used attempt"), Turret->PrepareTargetAttackAttempt(0.5f));
	return !HasAnyErrors();
}

#endif
