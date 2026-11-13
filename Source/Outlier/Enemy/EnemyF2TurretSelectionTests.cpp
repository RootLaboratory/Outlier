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
#include "Drone/Partner/HackGameplayTags.h"
#include "Interface/HackableInterface.h"
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

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyTurretCloseRangeFireTest,
	"Outlier.Enemy.Turret.CloseRangeFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyTurretCloseRangeFireTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("Enemy"), TEXT("Hacked"), TEXT("NormalDistance"), TEXT("Wall"),
		TEXT("Tracking"), TEXT("ActualMuzzleDirection")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyTurretCloseRangeFireTest::RunTest(const FString& Parameters)
{
	FF2TurretSelectionWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	AAutoTurret* Turret = Fixture.Turret;
	ARangedWeaponBase* Weapon = Turret->GetCurrentWeapon();
	USceneComponent* HeadPivot = Cast<USceneComponent>(FindFProperty<FObjectPropertyBase>(
		AAutoTurret::StaticClass(), TEXT("TurretHeadPivot"))->GetObjectPropertyValue_InContainer(Turret));
	USkeletalMeshComponent* HeadMesh = Cast<USkeletalMeshComponent>(FindFProperty<FObjectPropertyBase>(
		AAutoTurret::StaticClass(), TEXT("TurretHeadMesh"))->GetObjectPropertyValue_InContainer(Turret));
	if (!TestNotNull(TEXT("Saved turret owns its head rotation pivot"), HeadPivot)
		|| !TestNotNull(TEXT("Saved turret owns its actual muzzle mesh"), HeadMesh)) return false;
	FEnumProperty* Lifecycle = FindFProperty<FEnumProperty>(AAutoTurret::StaticClass(), TEXT("TurretLifecycleState"));
	Lifecycle->GetUnderlyingProperty()->SetIntPropertyValue(Lifecycle->ContainerPtrToValuePtr<void>(Turret),
		uint64(EAutoTurretLifecycleState::Active));
	Turret->EnterCombatFromRoom(Fixture.Shooter->GetActorLocation(), false);
	Fixture.StopAutomaticBehavior();
	Fixture.Select(Fixture.Shooter);
	if (!TestTrue(TEXT("Real Shooter pose supplies the initial observation"), Fixture.ObserveShooter())) return false;
	Turret->RefreshCombatAimPoint();
	const FVector ShooterAimOffset = Turret->GetCombatAimPoint(Fixture.Shooter) - Fixture.Shooter->GetActorLocation();
	const bool bNormalDistance = Parameters == TEXT("NormalDistance");
	const FVector DesiredAim = HeadPivot->GetComponentLocation()
		+ (bNormalDistance ? FVector(600, 800, 0) : FVector(40, 80, 0));
	AActor* Target = Fixture.Shooter;
	if (Parameters == TEXT("Hacked"))
	{
		// 팀은 실제 해킹 성공 효과로 변경하고, 변경된 팀에서 적인 실제 Gun BP를 대상으로 삼는다.
		FHackResultContext Result;
		Result.TargetActor = Turret;
		Result.Result = EHackResult::Success;
		CastChecked<IHackableInterface>(Turret)->HandleHackEffect(HackGameplayTags::Effect::ChangeTeam(), Result);
		UClass* GunClass = LoadClass<AEnemyBase>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Gun = Fixture.World->SpawnActor<AEnemyBase>(GunClass, DesiredAim, FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("Hacked turret has an actual enemy target"), Gun)) return false;
		Gun->GetStateTreeComponent()->StopLogic(TEXT("Controlled close-range target"));
		Gun->SetActorTickEnabled(false);
		Gun->GetCharacterMovement()->DisableMovement();
		Gun->SetActorEnableCollision(false);
		Target = Gun;
		TestTrue(TEXT("Real hack changes the turret team"), Turret->IsHackedToPlayerTeam());
	}
	else
	{
		Fixture.Shooter->SetActorLocation(DesiredAim - ShooterAimOffset);
		if (!TestTrue(TEXT("Near Shooter retains a real observer-specific pose sample"), Fixture.ObserveShooter())) return false;
	}
	Turret->EnterCombatFromRoom(Target->GetActorLocation(), false);
	Fixture.StopAutomaticBehavior();
	Fixture.Select(Target);
	Turret->BeginTargetAttackPreparation(0.0f);
	Turret->RefreshCombatAimPoint();
	if (Target == Fixture.Shooter)
	{
		auto LogSelection = [&](const TCHAR* Stage)
		{
			uint32 SelectedEpoch = 0;
			int32 SelectedBone = INDEX_NONE;
			FVector SelectedPoint;
			const bool bSelected = Turret->GetCombatAimSelectionForTesting(Target, SelectedEpoch, SelectedBone, SelectedPoint);
			FOutlierSightObservation Observation;
			const bool bObserved = Fixture.Shooter->GetObservedAimPoints(Turret, Observation);
			AddInfo(FString::Printf(TEXT("Close selection stage=%s Selected=%d Epoch=%u Bone=%d Observed=%d ObservedEpoch=%u Path=%d CanAttack=%d Block=%s"),
				Stage, bSelected, SelectedEpoch, SelectedBone, bObserved, Observation.Epoch,
				Weapon->HasClearTargetAttackPath(Target, Turret->GetCombatAimPoint(Target)),
				Turret->CanAttackTrackedTarget(Target), Weapon->GetAttackBlockReason()));
		};
		LogSelection(TEXT("BeforeRefreshWindow"));
		// 선택 직후 이동한 포즈는 0.1초 조준 갱신 주기 뒤에 반영한다. 최신 관측/사선 조건은 그대로 검사한다.
		Fixture.AdvanceWorld(0.11f);
		if (!TestTrue(TEXT("Moved Shooter provides a fresh pose after the ordinary aim refresh interval"), Fixture.ObserveShooter())) return false;
		Turret->RefreshCombatAimPoint();
		LogSelection(TEXT("AfterRefreshWindow"));
		uint32 Epoch = 0;
		int32 Bone = INDEX_NONE;
		FVector Point;
		if (!TestTrue(TEXT("Close fixture uses a selected real Shooter bone rather than capsule fallback"),
			Turret->GetCombatAimSelectionForTesting(Target, Epoch, Bone, Point))) return false;
	}
	FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"))
		->SetPropertyValue_InContainer(Weapon, false);
	FindFProperty<FIntProperty>(ARangedWeaponBase::StaticClass(), TEXT("BurstShotCount"))
		->SetPropertyValue_InContainer(Weapon, 0);
	const FVector Aim = Turret->GetCombatAimPoint(Target);
	const float AngularError = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
		(Aim - Turret->GetPawnViewLocation()).GetSafeNormal(), Turret->GetViewRotation().Vector()), -1.0, 1.0)));
	AddInfo(FString::Printf(TEXT("Close fire case=%s pivotDistance=%.2f aimError=%.2f aim=%s view=%s"),
		*Parameters, FVector::Distance(Aim, HeadPivot->GetComponentLocation()), AngularError,
		*Aim.ToCompactString(), *Turret->GetPawnViewLocation().ToCompactString()));
	if (!TestTrue(TEXT("Fixture is deliberately outside the ordinary aim tolerance"),
		AngularError > Turret->GetTurretBehavior().AimToleranceDegrees + 5.0f)) return false;
	const int32 AmmoBefore = Weapon->GetCurrentAmmo();
	if (bNormalDistance)
	{
		TestFalse(TEXT("Normal distance still requires completed angular alignment"), Turret->IsTurretAttackAimReady(Target));
		TestFalse(TEXT("Normal unaligned turret cannot begin target fire"), Turret->StartAttackTarget(Target));
		TestEqual(TEXT("Normal unaligned request consumes no ammunition"), Weapon->GetCurrentAmmo(), AmmoBefore);
		return !HasAnyErrors();
	}
	TestTrue(TEXT("Close target does not wait for exact angular alignment"), Turret->IsTurretAttackAimReady(Target));
	if (Parameters == TEXT("Wall"))
	{
		if (!TestNotNull(TEXT("Actual cover exists between muzzles and target"), Fixture.BlockPath(Aim))) return false;
		TestFalse(TEXT("Close-range aim exception cannot authorize an obstructed muzzle path"), Turret->CanAttackTrackedTarget(Target));
		TestFalse(TEXT("Close-range aim exception cannot begin fire through cover"), Turret->StartAttackTarget(Target));
		TestEqual(TEXT("Rejected covered fire consumes no ammunition"), Weapon->GetCurrentAmmo(), AmmoBefore);
		return !HasAnyErrors();
	}
	AActor* ForwardWall = nullptr;
	if (Parameters == TEXT("ActualMuzzleDirection"))
	{
		// 대상은 옆에 두고 총구 전방에만 벽을 둔다. 조준점으로 꺾지 않은 실제 탄도를 검사한다.
		ForwardWall = Fixture.World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(ForwardWall);
		ForwardWall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(10, 500, 500));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetWorldLocation(Turret->GetPawnViewLocation() + Turret->GetViewRotation().Vector() * 300.0f);
		Box->RegisterComponent();
	}
	const FVector BeforeFireDirection = Turret->GetViewRotation().Vector();
	TestTrue(TEXT("Unaligned close turret starts its actual target weapon"), Turret->StartAttackTarget(Target));
	if (!TestTrue(TEXT("Independent real muzzle group consumes ammunition before exact alignment"), Weapon->GetCurrentAmmo() < AmmoBefore))
		return false;
	if (ForwardWall)
	{
		const FWeaponShotSnapshot& Shot = *FindFProperty<FStructProperty>(ARangedWeaponBase::StaticClass(), TEXT("LastResolvedShot"))
			->ContainerPtrToValuePtr<FWeaponShotSnapshot>(Weapon);
		const FTransform Muzzle = HeadMesh->GetSocketTransform(Shot.MuzzleSocket);
		AddInfo(FString::Printf(TEXT("Actual close shot socketX=%s baseDirection=%s headForward=%s"),
			*Muzzle.GetUnitAxis(EAxis::X).ToCompactString(), *FVector(Shot.BaseDirection).ToCompactString(),
			*BeforeFireDirection.ToCompactString()));
		TestTrue(TEXT("Actual close shot starts at its own muzzle"), FVector(Shot.Start).Equals(Muzzle.GetLocation(), 0.1));
		// 라이플은 기존 ResolveShotTransform의 총구 위치 + 머리 전방 계약을 유지한다. 소켓의 FBX 축으로 교체하지 않는다.
		TestTrue(TEXT("Close exception preserves the existing rifle head/muzzle fire axis"), FVector::DotProduct(
			FVector(Shot.BaseDirection), BeforeFireDirection) > 0.999);
		TestTrue(TEXT("Close shot does not snap its trajectory toward the nearby side target"), FVector::DotProduct(
			FVector(Shot.Direction), (Aim - FVector(Shot.Start)).GetSafeNormal()) < 0.9);
		TestEqual(TEXT("Actual forward projectile hits the wall rather than bending toward the nearby target"), Shot.HitActor.Get(), ForwardWall);
	}
	if (Parameters == TEXT("Tracking"))
	{
		const FRotator BeforeTurn = Turret->GetViewRotation();
		Turret->UpdateTurretAimAtLocation(Aim, 0.1f, true);
		TestFalse(TEXT("Close fire continues turning the head toward its target"), Turret->GetViewRotation().Equals(BeforeTurn, 0.01));
		Fixture.AdvanceWorld(0.2f);
		if (!TestTrue(TEXT("Tracking Shooter obtains a refreshed actual pose"), Fixture.ObserveShooter())) return false;
		Turret->RefreshCombatAimPoint();
		TestTrue(TEXT("Tracking target remains allowed before exact alignment"), Turret->IsTurretAttackAimReady(Target));
		TestTrue(TEXT("Target weapon remains active while the head tracks"), Weapon->IsAttacking());
	}
	Turret->PauseTargetAttack();
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
