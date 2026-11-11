#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/VECDrone.h"
#include "Components/StateTreeComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterAnimInstance.h"
#include "Weapon/RangedWeaponBase.h"
#include "UObject/UnrealType.h"

namespace
{
// 저장된 Gun BP의 무기·Controller를 사용하고, 감지 입력과 시간만 통제한다.
struct FAttackPreparationWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	AEnemyBase* Enemy = nullptr;
	AEnemyAIController* Controller = nullptr;
	AShooterCharacter* Target = nullptr;

	~FAttackPreparationWorld()
	{
		if (!World) return;
		GEngine->ShutdownWorldNetDriver(World);
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		if (GameInstance) GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("AttackPreparation")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Isolated game world exists"), World)) return false;
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
		UClass* Class = LoadClass<AEnemyBase>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		if (!Test.TestNotNull(TEXT("Saved Gun BP loads"), Class)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(Class, FVector(0, 0, 300), FRotator::ZeroRotator, Spawn);
		if (!Test.TestNotNull(TEXT("Gun drone spawns"), Enemy)) return false;
		Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled preparation test"));
		Controller = Cast<AEnemyAIController>(Enemy->GetController());
		if (!Test.TestNotNull(TEXT("Gun AI controller exists"), Controller)
			|| !Test.TestNotNull(TEXT("Saved Gun weapon exists"), Enemy->GetCurrentWeapon())) return false;
		FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(Enemy->GetCurrentWeapon()->GetClass(), TEXT("bInfiniteAmmo"));
		if (!Test.TestNotNull(TEXT("Real gun ammunition policy is inspectable"), InfiniteAmmo)) return false;
		// 사격 성공은 표시 상태 대신 실제 탄약 감소로 검증한다.
		InfiniteAmmo->SetPropertyValue_InContainer(Enemy->GetCurrentWeapon(), false);
		Controller->SetEnemyPerceptionEnabled(false);
		Enemy->GetCharacterMovement()->DisableMovement();
		Enemy->SetActorEnableCollision(false);
		Target = SpawnTarget(FVector(1000, 0, 300));
		if (!Test.TestNotNull(TEXT("Hostile shooter target spawns"), Target)) return false;
		Enemy->EnterCombatFromRoom(Target->GetActorLocation(), false);
		Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled preparation test"));
		Controller->SetEnemyPerceptionEnabled(false);
		Select(Target);
		return Test.TestTrue(TEXT("Gun uses the scoped preparation policy"), Enemy->UsesTargetAttackPreparation());
	}

	AShooterCharacter* SpawnTarget(const FVector& Location)
	{
		UClass* Class = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Class) return nullptr;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Shooter = World->SpawnActor<AShooterCharacter>(Class, Location, FRotator::ZeroRotator, Spawn);
		if (Shooter)
		{
			Shooter->SetActorEnableCollision(false);
			Shooter->SetCanBeDamaged(false);
			Shooter->GetCharacterMovement()->DisableMovement();
		}
		return Shooter;
	}

	void Select(AShooterCharacter* Shooter)
	{
		Controller->SetCachedVisibleTargetForTesting(Shooter);
		Enemy->SetPlayerCurrentlyVisible(true);
		Enemy->ObserveTrackedTarget(Shooter);
	}

	void AdvanceWorld(float Seconds)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Seconds);
	}

	void ObserveTarget()
	{
		// 캐시 플래그와 별도로 실제 TP 본 관측을 만든다. 발사 검사가 이 단계를 대신 수행하지 않는다.
		FCanBeSeenFromContext Context;
		Context.IgnoreActor = Enemy;
		Context.ObserverLocation = Enemy->GetCombatShotOrigin();
		FVector Seen;
		int32 Sync = 0, Async = 0;
		float Strength = 0.0f;
		Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		Target->GetMesh()->RefreshBoneTransforms();
		Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		Enemy->RefreshCombatAimPoint();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFirstObservationShotGateTest,
	"Outlier.Enemy.Combat.F6.ShooterFirstObservationShotGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFirstObservationShotGateTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AEnemyBase* Enemy = Fixture.Enemy;
	ARangedWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	Enemy->BeginTargetAttackPreparation(0.0f);
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Enemy->UpdateTargetAttackAim(0.1f);
	}
	FOutlierSightObservation Observation;
	TestTrue(TEXT("Cached direct sight alone is present"), Fixture.Controller->IsTargetDirectlyVisible(Fixture.Target));
	TestFalse(TEXT("No actual observer-specific TP snapshot has been produced"), Fixture.Target->GetObservedAimPoints(Enemy, Observation));
	TestTrue(TEXT("Central path is open so obstruction cannot explain the hold"),
		Weapon->HasClearTargetAttackPath(Fixture.Target, Enemy->GetCombatAimPoint(Fixture.Target)));
	const int32 Ammo = Weapon->GetCurrentAmmo();
	TestFalse(TEXT("First selection without a TP observation cannot start the weapon"), Enemy->StartAttackTarget(Fixture.Target));
	TestEqual(TEXT("An unobserved capsule-center fallback consumes no ammunition"), Weapon->GetCurrentAmmo(), Ammo);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAttackPreparationVisibilityTest,
	"Outlier.Enemy.AttackPreparation.VisibilityPauseAndResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAttackPreparationVisibilityTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	auto* Enemy = Fixture.Enemy;
	Enemy->BeginTargetAttackPreparation(0.25f);
	Fixture.Controller->SetCachedVisibleTargetForTesting(nullptr);
	// 복제/캐시 플래그만 남아 있어도 실제 직접 관측 대상이 없으면 전조를 채우지 않는다.
	Enemy->AdvanceTargetAttackPreparation(0.5f);
	TestFalse(TEXT("A visibility flag without an actual directly seen target cannot fill first preparation"), Enemy->IsTargetAttackPreparationComplete());
	Fixture.Select(Fixture.Target);
	Enemy->AdvanceTargetAttackPreparation(0.1f);
	TestTrue(TEXT("Visible exposure leaves the expected first-telegraph remainder"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.15f));
	Fixture.Controller->SetCachedVisibleTargetForTesting(nullptr);
	Enemy->SetPlayerCurrentlyVisible(false);
	Enemy->AdvanceTargetAttackPreparation(1.0f);
	TestFalse(TEXT("Concealment cannot complete an unfinished telegraph"), Enemy->IsTargetAttackPreparationComplete());
	TestTrue(TEXT("Concealment preserves the unfinished remainder"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.15f));
	Fixture.Select(Fixture.Target);
	Enemy->BeginTargetAttackPreparation(0.25f);
	Enemy->AdvanceTargetAttackPreparation(0.14f);
	TestFalse(TEXT("Same-target task reentry neither restarts nor skips preparation"), Enemy->IsTargetAttackPreparationComplete());
	Enemy->AdvanceTargetAttackPreparation(0.02f);
	TestTrue(TEXT("Reacquisition finishes only the remaining exposure"), Enemy->IsTargetAttackPreparationComplete());
	Fixture.Controller->SetCachedVisibleTargetForTesting(nullptr);
	Enemy->SetPlayerCurrentlyVisible(false);
	Enemy->BeginTargetAttackPreparation(0.25f);
	TestTrue(TEXT("A completed preparation remains complete behind cover"), Enemy->IsTargetAttackPreparationComplete());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAttackPreparationRecoveryTest,
	"Outlier.Enemy.AttackPreparation.TargetSwitchRecoveryDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAttackPreparationRecoveryTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	auto* Enemy = Fixture.Enemy;
	Enemy->RememberTargetAttackPreparation();
	Enemy->RememberTargetAttackRecovery(1.0f);
	Fixture.AdvanceWorld(0.2f);
	auto* Second = Fixture.SpawnTarget(FVector(1000, 500, 300));
	auto* Third = Fixture.SpawnTarget(FVector(1000, -500, 300));
	if (!TestNotNull(TEXT("Second target exists"), Second) || !TestNotNull(TEXT("Third target exists"), Third)) return false;
	Fixture.Select(Second);
	Enemy->BeginTargetAttackPreparation(0.25f);
	TestFalse(TEXT("New target requires its own preparation"), Enemy->IsTargetAttackPreparationComplete());
	TestTrue(TEXT("Target selection preserves the running recovery deadline"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackRecoveryRemaining(), 0.8f, 0.01f));
	Enemy->AdvanceTargetAttackPreparation(0.25f);
	Fixture.AdvanceWorld(0.2f);
	Fixture.Select(Third);
	Enemy->BeginTargetAttackPreparation(0.25f);
	TestTrue(TEXT("Repeated target switches cannot restart or cancel recovery"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackRecoveryRemaining(), 0.6f, 0.01f));
	// 대체 대상이 아직 없는 한 프레임도 무기 공통 회복을 지우면 안 된다.
	Third->Destroy();
	Fixture.Controller->SetCachedVisibleTargetForTesting(nullptr);
	Enemy->SetPlayerCurrentlyVisible(false);
	Enemy->RefreshTargetTracking();
	TestTrue(TEXT("Invalidating the old target preserves the weapon recovery deadline"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackRecoveryRemaining(), 0.6f, 0.01f));
	Enemy->ResetTargetAttackContinuity();
	TestEqual(TEXT("Full lifecycle reset clears recovery"), Enemy->GetTargetAttackRecoveryRemaining(), 0.0f);
	TestFalse(TEXT("Full lifecycle reset clears target preparation"), Enemy->IsTargetAttackPreparationComplete());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAttackPreparationAimTest,
	"Outlier.Enemy.AttackPreparation.BoundedAimAndShotGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAttackPreparationAimTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	auto* Enemy = Fixture.Enemy;
	Fixture.Target->SetActorLocation(Enemy->GetCombatShotOrigin() + FVector(0, 1000, 0));
	Fixture.Select(Fixture.Target);
	Fixture.ObserveTarget();
	Fixture.Controller->SetControlRotation(FRotator::ZeroRotator);
	Enemy->SetActorRotation(FRotator::ZeroRotator);
	Enemy->BeginTargetAttackPreparation(0.25f);
	Enemy->AdvanceTargetAttackPreparation(0.25f);
	TestFalse(TEXT("Completed telegraph alone cannot authorize a perpendicular shot"), Enemy->IsTargetAttackAimReady());
	const int32 Ammo = Enemy->GetCurrentWeapon()->GetCurrentAmmo();
	TestFalse(TEXT("Attack start is withheld outside the aiming tolerance"), Enemy->StartAttackTarget(Fixture.Target));
	TestEqual(TEXT("Withheld attack cannot consume ammunition"), Enemy->GetCurrentWeapon()->GetCurrentAmmo(), Ammo);
	Enemy->UpdateTargetAttackAim(0.1f);
	const float Yaw = Fixture.Controller->GetControlRotation().Yaw;
	TestTrue(TEXT("First aiming step moves toward the target without snapping"), Yaw > 0.0f && Yaw <= 24.01f);
	for (int32 Step = 0; Step < 10; ++Step) Enemy->UpdateTargetAttackAim(0.1f);
	TestTrue(TEXT("Continued bounded rotation reaches the firing tolerance"), Enemy->IsTargetAttackAimReady());
	if (!TestTrue(TEXT("Aligned prepared target starts the real weapon"), Enemy->StartAttackTarget(Fixture.Target))) return false;
	const int32 AlignedAmmo = Enemy->GetCurrentWeapon()->GetCurrentAmmo();
	TestTrue(TEXT("The aligned first shot really consumes ammunition"), AlignedAmmo < Ammo);
	// 자동 연사 Timer 사이에 목표가 급히 이동한다. Controller 갱신만 멈춰 오래된 조준으로 탄이 나가는지 확인한다.
	Fixture.Controller->SetActorTickEnabled(false);
	Fixture.Target->SetActorLocation(Enemy->GetCombatShotOrigin() + FVector(-1000, 0, 0));
	Fixture.Select(Fixture.Target);
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Fixture.AdvanceWorld(0.1f);
		Fixture.ObserveTarget();
	}
	Enemy->GetCurrentWeapon()->PerformAttack();
	TestFalse(TEXT("Moved target is outside the per-bullet aiming tolerance"), Enemy->IsTargetAttackAimReady());
	TestEqual(TEXT("Real automatic-fire callbacks hold ammunition while rotation lags"),
		Enemy->GetCurrentWeapon()->GetCurrentAmmo(), AlignedAmmo);
	Enemy->StopCurrentAttack();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyExposedAimShotTest,
	"Outlier.Enemy.Combat.ExposedAim.SelectionShotGateAndCandidateOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyExposedAimShotTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	AEnemyBase* Enemy = Fixture.Enemy;
	AShooterCharacter* Target = Fixture.Target;
	ARangedWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	Fixture.Controller->SetActorTickEnabled(false);
	// 차폐물을 놓은 뒤 애니메이션으로 본이 이동하지 않게 한다. 관측/무기 Trace는 실제 경로를 사용한다.
	Target->GetMesh()->SetComponentTickEnabled(false);
	FCanBeSeenFromContext Context;
	Context.IgnoreActor = Enemy;
	Context.ObserverLocation = Enemy->GetCombatShotOrigin() + FVector(0, 150, 150);
	FVector Seen;
	int32 Sync = 0, Async = 0;
	float Strength = 0;
	const auto Observe = [&]
	{
		Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
		Target->GetMesh()->RefreshBoneTransforms();
		return Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
	};
	Observe();
	FOutlierSightObservation Observation;
	if (!TestTrue(TEXT("Real target exposes registered head to this observer"), Target->GetObservedAimPoints(Enemy, Observation)
		&& Observation.HeadPoints.Num() == 1)) return false;
	const FVector Head = Observation.HeadPoints[0];
	const FVector Origin = Enemy->GetCombatShotOrigin();
	const FVector Center = Target->GetCapsuleComponent()->Bounds.Origin;
	const auto BlockPoint = [&](const FVector& Point)
	{
		AActor* Wall = Fixture.World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(0.2));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetWorldLocation((Origin + Point) * 0.5);
		Box->RegisterComponent();
		return Box;
	};
	TArray<UBoxComponent*> Cover;
	Cover.Add(BlockPoint(Center));
	for (const FVector& Point : Observation.BodyPoints) Cover.Add(BlockPoint(Point));
	for (const FVector& Point : Observation.OtherPoints) Cover.Add(BlockPoint(Point));
	Fixture.AdvanceWorld(0.11f);
	Observe();
	Enemy->RefreshCombatAimPoint();
	TestFalse(TEXT("The central weapon path really is blocked"), Weapon->HasClearTargetAttackPath(Target, Center));
	TestTrue(TEXT("The real head weapon path remains open"), Weapon->HasClearTargetAttackPath(Target, Head));
	TestTrue(TEXT("Selection substitutes the exposed head for the occluded center"), Enemy->GetCombatAimPoint(Target).Equals(Head, 0.1));
	Enemy->UpdateCombatTacticalSightBlock();
	Fixture.AdvanceWorld(0.31f);
	Observe();
	Enemy->RefreshCombatAimPoint();
	Enemy->UpdateCombatTacticalSightBlock();
	TestFalse(TEXT("Central cover with an exposed head never becomes full LOS blockage"), Enemy->NeedsCombatTacticalReposition());
	int32 CandidateChecks = 16;
	TestTrue(TEXT("A tactical candidate accepts the exposed head despite central cover"),
		Enemy->HasCombatAttackPathFrom(Target, Enemy->GetActorLocation(), CandidateChecks));
	TestTrue(TEXT("Candidate checks consume the shared finite trace budget"), CandidateChecks >= 0 && CandidateChecks < 16);
	CandidateChecks = 0;
	TestFalse(TEXT("An exhausted candidate budget never invents a clear path"),
		Enemy->HasCombatAttackPathFrom(Target, Enemy->GetActorLocation(), CandidateChecks));
	Enemy->BeginTargetAttackPreparation(0.0f);
	for (int32 Step = 0; Step < 10; ++Step) Enemy->UpdateTargetAttackAim(0.1f);
	const int32 Ammo = Weapon->GetCurrentAmmo();
	if (!TestTrue(TEXT("The real weapon starts toward the exposed head"), Enemy->StartAttackTarget(Target))) return false;
	TestTrue(TEXT("An authorized exposed-part shot consumes ammunition"), Weapon->GetCurrentAmmo() < Ammo);
	const int32 AfterHeadShot = Weapon->GetCurrentAmmo();
	UBoxComponent* HeadCover = BlockPoint(Head);
	Weapon->PerformAttack();
	TestEqual(TEXT("Cover appearing between callbacks blocks the very next ammunition consumption"), Weapon->GetCurrentAmmo(), AfterHeadShot);
	Enemy->PauseTargetAttack();
	Enemy->RememberTargetAttackRecovery(1.0f);
	const float Recovery = Enemy->GetTargetAttackRecoveryRemaining();
	const FVector OtherOrigin = Enemy->GetActorLocation() + FVector(0, -300, 0);
	CandidateChecks = 16;
	TestTrue(TEXT("Another candidate uses its own origin rather than copying current obstruction"),
		Enemy->HasCombatAttackPathFrom(Target, OtherOrigin, CandidateChecks));
	for (UBoxComponent* Box : Cover) Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadCover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Fixture.AdvanceWorld(0.11f);
		Observe();
		Enemy->RefreshCombatAimPoint();
	}
	TestTrue(TEXT("An open center preserves the already observed and unobstructed head"), Enemy->GetCombatAimPoint(Target).Equals(Head, 0.1));
	TestTrue(TEXT("Changing exposed parts preserves completed preparation"), Enemy->IsTargetAttackPreparationComplete());
	TestTrue(TEXT("Changing exposed parts neither clears nor restarts recovery"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackRecoveryRemaining(), Recovery - 0.33f, 0.02f));
	HeadCover->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Fixture.AdvanceWorld(0.11f);
	Observe();
	Target->GetObservedAimPoints(Enemy, Observation);
	Enemy->RefreshCombatAimPoint();
	const FVector AlternateAim = Enemy->GetCombatAimPoint(Target);
	TestTrue(TEXT("A blocked detected head falls back to a body part actually observed by this AI"),
		Observation.BodyPoints.ContainsByPredicate([&](const FVector& Point) { return Point.Equals(AlternateAim, 0.1); }));
	TestTrue(TEXT("The fallback part has a clear path from the actual weapon origin"), Weapon->HasClearTargetAttackPath(Target, AlternateAim));
	HeadCover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Fixture.AdvanceWorld(0.11f);
	Observe();
	Enemy->RefreshCombatAimPoint();
	TestTrue(TEXT("Reopening the initial head keeps the currently clear observed body part stable"),
		Enemy->GetCombatAimPoint(Target).Equals(AlternateAim, 0.1));
	// 회복과 조준 지연을 먼저 해소한 뒤 관측 수명만 만료시켜 발사 보류 사유를 분리한다.
	for (int32 Step = 0; Step < 20 && Enemy->GetTargetAttackRecoveryRemaining() > 0.0f; ++Step)
		Fixture.AdvanceWorld(0.1f);
	Fixture.Select(Target);
	Enemy->BeginTargetAttackPreparation(0.0f);
	Observe();
	Enemy->RefreshCombatAimPoint();
	for (int32 Step = 0; Step < 10; ++Step) Enemy->UpdateTargetAttackAim(0.1f);
	TestTrue(TEXT("Fresh observed aim is eligible before testing observation expiry"), Enemy->CanAttackTrackedTarget(Target));
	const FVector LastObservedAim = Enemy->GetCombatAimPoint(Target);
	Fixture.AdvanceWorld(0.26f);
	Target->SetActorLocation(Target->GetActorLocation() + FVector(0, 100, 0));
	Enemy->RefreshCombatAimPoint();
	TestTrue(TEXT("Expired observation keeps the last known part instead of following the hidden live capsule"),
		Enemy->GetCombatAimPoint(Target).Equals(LastObservedAim, 0.1));
	TestFalse(TEXT("An expired observation blocks a new shot"), Enemy->CanAttackTrackedTarget(Target));
	CandidateChecks = 16;
	TestFalse(TEXT("An expired observation cannot approve a tactical position through a live capsule fallback"),
		Enemy->HasCombatAttackPathFrom(Target, Enemy->GetActorLocation(), CandidateChecks));
	Enemy->StopCurrentAttack();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatTacticalAttackWaitTest,
	"Outlier.Enemy.Combat.Tactical.OpenPathAttackWaitsDoNotReposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatTacticalAttackWaitTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	AEnemyBase* Enemy = Fixture.Enemy;
	Fixture.Controller->SetActorTickEnabled(false);
	Enemy->SetActorTickEnabled(false);
	Fixture.Target->GetMesh()->SetComponentTickEnabled(false);
	Enemy->BeginTargetAttackPreparation(1.0f);
	Enemy->EnsureCombatTacticalEpisode(Fixture.Target, Fixture.Target->GetActorLocation());
	if (!TestTrue(TEXT("Control fixture has a real open in-range weapon path"),
		Enemy->GetCurrentWeapon()->HasClearTargetAttackPath(Fixture.Target, Enemy->GetCombatAimPoint(Fixture.Target)))) return false;

	// 발사 불가라는 공통 결과 대신 실제 무기 사선을 소비해야 전조/회복/회전 대기가 이동으로 번지지 않는다.
	Enemy->UpdateCombatTacticalSightBlock();
	Fixture.AdvanceWorld(0.4f);
	Enemy->UpdateCombatTacticalSightBlock();
	TestFalse(TEXT("Unfinished preparation still withholds the shot"), Enemy->CanAttackTrackedTarget(Fixture.Target));
	TestFalse(TEXT("Preparation wait cannot become sustained LOS blockage"), Enemy->NeedsCombatTacticalReposition());
	Enemy->RememberTargetAttackPreparation();
	Enemy->RememberTargetAttackRecovery(1.0f);
	Fixture.AdvanceWorld(0.4f);
	Enemy->UpdateCombatTacticalSightBlock();
	TestFalse(TEXT("Running recovery still withholds the shot"), Enemy->CanAttackTrackedTarget(Fixture.Target));
	TestFalse(TEXT("Recovery wait cannot request LOS reposition"), Enemy->NeedsCombatTacticalReposition());
	Fixture.AdvanceWorld(0.7f);
	Fixture.Controller->SetControlRotation(FRotator(0, 90, 0));
	Enemy->SetActorRotation(FRotator(0, 90, 0));
	Enemy->UpdateCombatTacticalSightBlock();
	Fixture.AdvanceWorld(0.4f);
	Enemy->UpdateCombatTacticalSightBlock();
	TestFalse(TEXT("An unaligned weapon still withholds the shot"), Enemy->CanAttackTrackedTarget(Fixture.Target));
	TestFalse(TEXT("Rotation wait with a clear path cannot request LOS reposition"), Enemy->NeedsCombatTacticalReposition());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatTacticalSightBlockTest,
	"Outlier.Enemy.Combat.Tactical.SustainedBlockAndObservationPreserveAttackContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatTacticalSightBlockTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	AEnemyBase* Enemy = Fixture.Enemy;
	Fixture.Controller->SetActorTickEnabled(false);
	Enemy->SetActorTickEnabled(false);
	Fixture.Target->GetMesh()->SetComponentTickEnabled(false);
	Enemy->BeginTargetAttackPreparation(0.25f);
	Enemy->AdvanceTargetAttackPreparation(0.1f);
	Enemy->RememberTargetAttackRecovery(1.5f);
	const uint64 BeforeObservation = Enemy->EnsureCombatTacticalEpisode(Fixture.Target, Fixture.Target->GetActorLocation());
	const float Preparation = Enemy->GetTargetAttackPreparationRemaining();
	const float Recovery = Enemy->GetTargetAttackRecoveryRemaining();
	const FVector Origin = Enemy->GetCombatShotOrigin();
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Physical cover actor exists"), Wall)) return false;
	UBoxComponent* Cover = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Cover);
	Cover->SetBoxExtent(FVector(20, 500, 500));
	Cover->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Cover->SetCollisionObjectType(ECC_WorldStatic);
	Cover->SetCollisionResponseToAllChannels(ECR_Block);
	Cover->SetWorldLocation((Origin + Fixture.Target->GetActorLocation()) * 0.5);
	Cover->RegisterComponent();
	if (!TestFalse(TEXT("Real cover blocks the current weapon path"),
		Enemy->GetCurrentWeapon()->HasClearTargetAttackPath(Fixture.Target, Enemy->GetCombatAimPoint(Fixture.Target)))) return false;

	Enemy->UpdateCombatTacticalSightBlock();
	Fixture.AdvanceWorld(0.14f);
	Enemy->UpdateCombatTacticalSightBlock();
	Fixture.AdvanceWorld(0.14f);
	Enemy->UpdateCombatTacticalSightBlock();
	TestFalse(TEXT("A short 0.28 second obstruction only holds fire"), Enemy->NeedsCombatTacticalReposition());
	Fixture.AdvanceWorld(0.03f);
	Enemy->UpdateCombatTacticalSightBlock();
	TestTrue(TEXT("A sustained full weapon obstruction requests reposition after 0.3 seconds"), Enemy->NeedsCombatTacticalReposition());

	Enemy->EnterCombatObservationWait();
	TestTrue(TEXT("Exhausted tactical flow can enter observation"), Enemy->IsCombatObservationWaiting());
	TestFalse(TEXT("Observation invalidates in-flight results from the former episode"), Enemy->IsCombatTacticalEpisodeCurrent(BeforeObservation));
	TestEqual(TEXT("Observation preserves the known tracked target"), Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
	TestTrue(TEXT("Observation preserves unfinished preparation"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), Preparation));
	TestTrue(TEXT("Observation preserves the original recovery deadline"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackRecoveryRemaining(), Recovery - 0.31f, 0.02f));

	// 재노출은 잔여 준비/회복을 그대로 소비한다. 관찰 대기 자체를 공격 금지 조건으로 추가하지 않는다.
	Cover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Enemy->UpdateCombatTacticalSightBlock();
	TestFalse(TEXT("An open path immediately clears sustained LOS blockage"), Enemy->NeedsCombatTacticalReposition());
	TestTrue(TEXT("Reopening preserves unfinished preparation"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), Preparation));
	TestTrue(TEXT("Reopening preserves recovery rather than restarting it"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackRecoveryRemaining(), Recovery - 0.31f, 0.02f));
	Enemy->AdvanceTargetAttackPreparation(Preparation + 0.01f);
	// World Tick의 큰 DeltaTime 제한에 걸리지 않도록 실제 회복 마감까지 작은 프레임으로 진행한다.
	for (int32 Step = 0; Step < 13; ++Step)
	{
		Fixture.AdvanceWorld(0.1f);
	}
	Fixture.ObserveTarget();
	for (int32 Step = 0; Step < 10; ++Step) Enemy->UpdateTargetAttackAim(0.1f);
	Enemy->EnterCombatObservationWait();
	TestTrue(TEXT("Observation remains active while evaluating a stationary shot"), Enemy->IsCombatObservationWaiting());
	TestTrue(TEXT("Valid stationary fire remains eligible during observation"), Enemy->CanAttackTrackedTarget(Fixture.Target));
	const int32 Ammo = Enemy->GetCurrentWeapon()->GetCurrentAmmo();
	TestTrue(TEXT("The real weapon can fire from observation once its existing gates are met"), Enemy->StartAttackTarget(Fixture.Target));
	TestTrue(TEXT("Stationary observation fire consumes real ammunition"), Enemy->GetCurrentWeapon()->GetCurrentAmmo() < Ammo);
	Enemy->StopCurrentAttack();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyExposedAimLeanPoseTest,
	"Outlier.Enemy.Combat.ExposedAim.LeanEvaluatedThirdPersonBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyExposedAimLeanPoseTest::RunTest(const FString& Parameters)
{
	FAttackPreparationWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	Fixture.Controller->SetActorTickEnabled(false);
	APlayerController* Player = Fixture.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Listen-host pose has a local controller"), Player)) return false;
	Player->Player = NewObject<ULocalPlayer>(GEngine);
	Player->SetAsLocalPlayerController();
	Player->Possess(Fixture.Target);
	Fixture.Target->SetActorEnableCollision(true);
	AActor* FloorActor = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Floor = NewObject<UBoxComponent>(FloorActor);
	FloorActor->SetRootComponent(Floor);
	Floor->SetBoxExtent(FVector(3000, 3000, 10));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->SetWorldLocation(Fixture.Target->GetActorLocation() - FVector(0, 0,
		Fixture.Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10));
	Floor->RegisterComponent();
	Fixture.Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	if (!TestNotNull(TEXT("Production Pistol exists"), PistolClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARangedWeaponBase* Pistol = Fixture.World->SpawnActor<ARangedWeaponBase>(PistolClass, Spawn);
	if (!TestNotNull(TEXT("Real pose weapon spawns"), Pistol)) return false;
	Fixture.Target->AFirstPersonCharacter::EquipWeapon(Pistol);
	Fixture.Target->RefreshWeaponMode();
	const auto TickPose = [&]
	{
		for (int32 Frame = 0; Frame < 90; ++Frame) Fixture.AdvanceWorld(1.0f / 60.0f);
	};
	for (const bool bSuit : {false, true})
	{
		Fixture.Target->SetSuitPresentation(bSuit);
		Fixture.Target->SetLeanTarget(0.0f, false);
		TickPose();
		USkeletalMeshComponent* Mesh = Fixture.Target->GetMesh();
		if (!TestNotNull(TEXT("Production TP animation instance is active"), Cast<UShooterAnimInstance>(Mesh->GetAnimInstance()))) return false;
		const FVector NeutralHead = Mesh->GetSocketLocation(TEXT("head"));
		for (const float Lean : {-1.0f, 1.0f})
		{
			// 입력/충돌 타이머와 실제 TP AnimBP를 실행한다. 본 좌표나 애니메이션 알파를 테스트에서 주입하지 않는다.
			Fixture.Target->SetLeanTarget(Lean, false);
			TickPose();
			const FVector Head = Mesh->GetSocketLocation(TEXT("head"));
			const FVector Delta = Head - NeutralHead;
			AddInfo(FString::Printf(TEXT("[LeanTP] Suit=%d Lean=%.3f Anim=%s Head=%s Neutral=%s Delta=%s View=%s"),
				bSuit, Fixture.Target->GetCurrentLeanAlpha(), *GetNameSafe(Mesh->GetAnimClass()),
				*Head.ToString(), *NeutralHead.ToString(), *Delta.ToString(), *Fixture.Target->GetPawnViewLocation().ToString()));
			TestTrue(TEXT("Real lean timer reaches the requested side"), FMath::IsNearlyEqual(Fixture.Target->GetCurrentLeanAlpha(), Lean, 0.01f));
			TestTrue(TEXT("Evaluated TP head moves toward the same side as the lean view"), Delta.Y * Lean > 1.0f);
			FCanBeSeenFromContext Context;
			Context.IgnoreActor = Fixture.Enemy;
			Context.ObserverLocation = Fixture.Enemy->GetCombatShotOrigin();
			FVector Seen;
			int32 Sync = 0, Async = 0;
			float Strength = 0.0f;
			Fixture.Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
			Fixture.AdvanceWorld(1.0f / 60.0f);
			Fixture.Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
			FOutlierSightObservation Observation;
			if (TestTrue(TEXT("Sight retains a fresh observer-specific TP head"),
				Fixture.Target->GetObservedAimPoints(Fixture.Enemy, Observation) && Observation.HeadPoints.Num() == 1))
			{
				TestTrue(TEXT("Observed head is the actual evaluated TP bone, without a lean proxy offset"),
					Observation.HeadPoints[0].Equals(Mesh->GetSocketLocation(TEXT("head")), 0.01));
				Fixture.Enemy->RefreshCombatAimPoint();
				TestTrue(TEXT("Initial aiming starts at the first TP part actually seen, even while the capsule center is open"),
					Fixture.Enemy->GetCombatAimPoint(Fixture.Target).Equals(Seen, 0.1));
				// 엄폐 끝을 중립 머리와 실제 린 머리 사이에 둔다. FP 카메라나 별도 충돌 구체는 표본으로 쓰지 않는다.
				const FVector Origin = Fixture.Enemy->GetCombatShotOrigin();
				const double WallX = (Origin.X + Head.X) * 0.5;
				const double NeutralY = Origin.Y + (NeutralHead.Y - Origin.Y) * (WallX - Origin.X) / (NeutralHead.X - Origin.X);
				const double LeanY = Origin.Y + (Head.Y - Origin.Y) * (WallX - Origin.X) / (Head.X - Origin.X);
				const double EdgeY = (NeutralY + LeanY) * 0.5;
				AActor* CoverActor = Fixture.World->SpawnActor<AActor>();
				UBoxComponent* Cover = NewObject<UBoxComponent>(CoverActor);
				CoverActor->SetRootComponent(Cover);
				Cover->SetBoxExtent(FVector(0.2, 200, 300));
				Cover->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Cover->SetCollisionObjectType(ECC_WorldStatic);
				Cover->SetCollisionResponseToAllChannels(ECR_Block);
				Cover->SetWorldLocation(FVector(WallX, EdgeY - Lean * 200, Head.Z));
				Cover->RegisterComponent();
				Fixture.AdvanceWorld(0.11f);
				TestTrue(TEXT("Real TP lean head is detected around actual side cover"),
					Fixture.Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr)
					== UAISense_Sight::EVisibilityResult::Visible);
				Fixture.Enemy->RefreshCombatAimPoint();
				ARangedWeaponBase* Gun = Fixture.Enemy->GetCurrentWeapon();
				TestFalse(TEXT("Side cover blocks the capsule center"),
					Gun->HasClearTargetAttackPath(Fixture.Target, Fixture.Target->GetCapsuleComponent()->Bounds.Origin));
				TestTrue(TEXT("Side cover leaves the evaluated TP head weapon path open"),
					Gun->HasClearTargetAttackPath(Fixture.Target, Mesh->GetSocketLocation(TEXT("head"))));
				TestTrue(TEXT("Aim follows the observed TP part around side cover"),
					Fixture.Enemy->GetCombatAimPoint(Fixture.Target).Equals(Seen, 0.1));
				int32 Checks = 16;
				TestTrue(TEXT("Tactical position accepts actual TP lean exposure"),
					Fixture.Enemy->HasCombatAttackPathFrom(Fixture.Target, Fixture.Enemy->GetActorLocation(), Checks));
				Fixture.Enemy->BeginTargetAttackPreparation(0.0f);
				for (int32 Step = 0; Step < 10; ++Step) Fixture.Enemy->UpdateTargetAttackAim(0.1f);
				const int32 Ammo = Gun->GetCurrentAmmo();
				TestTrue(TEXT("A real shot can begin at the exposed TP lean bone"), Fixture.Enemy->StartAttackTarget(Fixture.Target));
				TestTrue(TEXT("A real shot at the TP lean bone consumes ammunition"), Gun->GetCurrentAmmo() < Ammo);
				Fixture.Enemy->PauseTargetAttack();
				Cover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}
	return !HasAnyErrors();
}

#endif
