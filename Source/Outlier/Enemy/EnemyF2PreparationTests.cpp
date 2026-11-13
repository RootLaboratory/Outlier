#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Components/StateTreeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
// 저장된 Gun BP를 사용하되 자동 감지와 Tree 진행만 멈춘다. 전조와 만료 시계는 실제 Pawn API를 통과한다.
struct FF2PreparationWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	AEnemyBase* Enemy = nullptr;
	AEnemyAIController* Controller = nullptr;
	AShooterCharacter* FirstTarget = nullptr;
	UClass* ShooterClass = nullptr;

	~FF2PreparationWorld()
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
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyF2Preparation")), GetTransientPackage());
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
		if (!Test.TestNotNull(TEXT("Real Gun spawns"), Enemy))
		{
			return false;
		}
		Controller = Cast<AEnemyAIController>(Enemy->GetController());
		if (!Test.TestNotNull(TEXT("Gun has its production AI controller"), Controller)
			|| !Test.TestNotNull(TEXT("Gun has its production weapon"), Enemy->GetCurrentWeapon()))
		{
			return false;
		}
		FirstTarget = SpawnTarget(0);
		if (!Test.TestNotNull(TEXT("First hostile Shooter spawns"), FirstTarget))
		{
			return false;
		}
		Enemy->EnterCombatFromRoom(FirstTarget->GetActorLocation(), false);
		StopAutomaticBehavior();
		Enemy->SetActorEnableCollision(false);
		Enemy->GetCharacterMovement()->DisableMovement();
		Select(FirstTarget);
		return Test.TestTrue(TEXT("Real Gun uses target preparation"), Enemy->UsesTargetAttackPreparation());
	}

	void StopAutomaticBehavior()
	{
		Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled F2 preparation regression"));
		Controller->SetEnemyPerceptionEnabled(false);
		Controller->SetActorTickEnabled(false);
		Enemy->SetActorTickEnabled(false);
	}

	AShooterCharacter* SpawnTarget(int32 Index)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterCharacter* Target = World->SpawnActor<AShooterCharacter>(ShooterClass,
			FVector(1000, Index * 300, 300), FRotator::ZeroRotator, Spawn);
		if (Target)
		{
			Target->SetActorEnableCollision(false);
			Target->SetCanBeDamaged(false);
			Target->GetCharacterMovement()->DisableMovement();
			Target->GetMesh()->SetComponentTickEnabled(false);
		}
		return Target;
	}

	void Select(AActor* Target)
	{
		Controller->SetCachedVisibleTargetForTesting(Target);
		Enemy->SetPlayerCurrentlyVisible(true);
		Enemy->ObserveTrackedTarget(Target);
		Enemy->BeginTargetAttackPreparation(1.0f);
	}

	void AdvanceWorld(float Seconds)
	{
		// 큰 한 프레임은 엔진 DeltaTime 제한에 걸린다. 만료 경계는 실제 World 시간의 작은 프레임으로 검사한다.
		float Remaining = Seconds;
		while (Remaining > KINDA_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Remaining, 0.05f);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			Remaining -= Step;
		}
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF2PreparationHistoryTest,
	"Outlier.Enemy.AttackPreparation.F2History",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF2PreparationHistoryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Case : {TEXT("PartialRestore"), TEXT("CompletedRestore"), TEXT("BeforeExpiry"),
		TEXT("Expired"), TEXT("HiddenPause"), TEXT("FourTargetBound"), TEXT("DuplicateDoesNotRenew"),
		TEXT("RecoveryDeadline"), TEXT("WeaponContract"), TEXT("ControllerContract"),
		TEXT("PoolGeneration"), TEXT("PoolLease"), TEXT("TargetPoolLease"), TEXT("TargetHostility"), TEXT("FullReset")})
	{
		OutBeautifiedNames.Add(Case);
		OutTestCommands.Add(Case);
	}
}

bool FEnemyF2PreparationHistoryTest::RunTest(const FString& Parameters)
{
	FF2PreparationWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AEnemyBase* Enemy = Fixture.Enemy;
	AShooterCharacter* A = Fixture.FirstTarget;
	AShooterCharacter* B = Fixture.SpawnTarget(1);
	if (!TestNotNull(TEXT("Second target spawns"), B))
	{
		return false;
	}
	Enemy->AdvanceTargetAttackPreparation(0.35f);
	TestTrue(TEXT("A starts with a real partial preparation"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.65f));
	if (Parameters == TEXT("CompletedRestore"))
	{
		Enemy->AdvanceTargetAttackPreparation(0.65f);
		TestTrue(TEXT("A completes its own preparation"), Enemy->IsTargetAttackPreparationComplete());
	}
	if (Parameters == TEXT("RecoveryDeadline"))
	{
		Enemy->RememberTargetAttackRecovery(2.0f);
	}
	Fixture.Select(B);
	TestFalse(TEXT("B never inherits A preparation approval"), Enemy->IsTargetAttackPreparationComplete());
	TestTrue(TEXT("B starts with its own full preparation"),
		FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	Enemy->AdvanceTargetAttackPreparation(0.2f);

	if (Parameters == TEXT("PartialRestore") || Parameters == TEXT("CompletedRestore"))
	{
		Fixture.AdvanceWorld(0.2f);
		Fixture.Select(A);
		if (Parameters == TEXT("CompletedRestore"))
		{
			TestTrue(TEXT("A restores its own completed preparation"), Enemy->IsTargetAttackPreparationComplete());
		}
		else
		{
			TestTrue(TEXT("A restores its own remainder rather than B progress"),
				FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.65f));
			Enemy->AdvanceTargetAttackPreparation(0.64f);
			TestFalse(TEXT("Restoration cannot skip A remaining exposure"), Enemy->IsTargetAttackPreparationComplete());
			Enemy->AdvanceTargetAttackPreparation(0.02f);
			TestTrue(TEXT("Only A remaining exposure completes A preparation"), Enemy->IsTargetAttackPreparationComplete());
		}
	}
	else if (Parameters == TEXT("BeforeExpiry") || Parameters == TEXT("Expired"))
	{
		Fixture.AdvanceWorld(Parameters == TEXT("BeforeExpiry") ? 2.95f : 3.05f);
		Fixture.Select(A);
		const float Expected = Parameters == TEXT("BeforeExpiry") ? 0.65f : 1.0f;
		TestTrue(TEXT("Three-second deselection lifetime controls restoration"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), Expected));
	}
	else if (Parameters == TEXT("HiddenPause"))
	{
		// Observe만 한 입력도 실제 시야 갱신 경계를 거쳐야 접촉 소실 유예가 시작된다.
		Enemy->RefreshTargetTracking();
		Fixture.Controller->SetCachedVisibleTargetForTesting(nullptr);
		Enemy->SetPlayerCurrentlyVisible(false);
		Enemy->AdvanceTargetAttackPreparation(5.0f);
		TestTrue(TEXT("A hidden selected target cannot advance its partial preparation"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.8f));
		Fixture.AdvanceWorld(0.5f);
		Fixture.Select(A);
		TestTrue(TEXT("Deselected A does not gain hidden or B exposure time"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.65f));
	}
	else if (Parameters == TEXT("FourTargetBound"))
	{
		AShooterCharacter* C = Fixture.SpawnTarget(2);
		AShooterCharacter* D = Fixture.SpawnTarget(3);
		AShooterCharacter* E = Fixture.SpawnTarget(4);
		if (!TestNotNull(TEXT("Third target spawns"), C) || !TestNotNull(TEXT("Fourth target spawns"), D)
			|| !TestNotNull(TEXT("Fifth target spawns"), E))
		{
			return false;
		}
		Fixture.AdvanceWorld(0.05f);
		Fixture.Select(C);
		Enemy->AdvanceTargetAttackPreparation(0.3f);
		Fixture.AdvanceWorld(0.05f);
		Fixture.Select(D);
		Enemy->AdvanceTargetAttackPreparation(0.4f);
		Fixture.AdvanceWorld(0.05f);
		Fixture.Select(B);
		TestTrue(TEXT("Four total targets retain B own remainder"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.8f));
		Fixture.AdvanceWorld(0.05f);
		Fixture.Select(E);
		Fixture.Select(A);
		TestTrue(TEXT("A fifth target evicts oldest inactive A within the time limit"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	}
	else if (Parameters == TEXT("DuplicateDoesNotRenew"))
	{
		Fixture.AdvanceWorld(2.0f);
		for (int32 Repeat = 0; Repeat < 5; ++Repeat)
		{
			Fixture.Select(B);
			Enemy->GetTargetAttackPreparationRemaining();
			Enemy->IsTargetAttackPreparationComplete();
		}
		TestTrue(TEXT("Repeated B input leaves B own partial progress intact"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.8f));
		Fixture.AdvanceWorld(1.1f);
		Fixture.Select(A);
		TestTrue(TEXT("Repeated selected-target input never extends deselected A lifetime"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	}
	else if (Parameters == TEXT("RecoveryDeadline"))
	{
		Fixture.AdvanceWorld(0.3f);
		Fixture.Select(A);
		Enemy->RememberTargetAttackRecovery(2.0f);
		Fixture.AdvanceWorld(0.2f);
		Fixture.Select(B);
		TestTrue(TEXT("Cache restoration neither cancels nor extends common recovery"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackRecoveryRemaining(), 1.5f, 0.02f));
		TestTrue(TEXT("Recovery is independent of each target own preparation"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 0.8f));
	}
	else if (Parameters == TEXT("WeaponContract"))
	{
		FObjectPropertyBase* WeaponProperty = FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"));
		ARangedWeaponBase* OriginalWeapon = Enemy->GetCurrentWeapon();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARangedWeaponBase* Replacement = Fixture.World->SpawnActor<ARangedWeaponBase>(OriginalWeapon->GetClass(), Spawn);
		if (!TestNotNull(TEXT("Existing equipped weapon property exists"), WeaponProperty)
			|| !TestNotNull(TEXT("Replacement real weapon spawns"), Replacement))
		{
			return false;
		}
		Replacement->OnEquipped(Enemy);
		WeaponProperty->SetObjectPropertyValue_InContainer(Enemy, Replacement);
		Fixture.Select(B);
		WeaponProperty->SetObjectPropertyValue_InContainer(Enemy, OriginalWeapon);
		Fixture.Select(A);
		TestTrue(TEXT("Restoring a former weapon cannot resurrect its pre-change preparation"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	}
	else if (Parameters == TEXT("ControllerContract"))
	{
		Fixture.Controller->UnPossess();
		Fixture.Controller->Possess(Enemy);
		Fixture.StopAutomaticBehavior();
		Fixture.Select(A);
		TestTrue(TEXT("Restoring AI control cannot resurrect a previous control session preparation"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	}
	else if (Parameters == TEXT("PoolGeneration") || Parameters == TEXT("PoolLease"))
	{
		const FName IdentityName = Parameters == TEXT("PoolGeneration") ? TEXT("PoolGameplayGeneration") : TEXT("PoolLeaseSerial");
		FIntProperty* Identity = FindFProperty<FIntProperty>(AEnemyBase::StaticClass(), IdentityName);
		if (!TestNotNull(TEXT("Existing pool generation or lease identity exists"), Identity))
		{
			return false;
		}
		// 계약 세대만 바꿔 동일 Actor/무기/Controller를 유지한다. 캐시 내부 값은 테스트에서 조작하지 않는다.
		const int32 Original = Identity->GetPropertyValue_InContainer(Enemy);
		Identity->SetPropertyValue_InContainer(Enemy, Original + 1);
		Fixture.Select(B);
		Fixture.Select(A);
		TestTrue(TEXT("A new owner generation or lease never inherits former preparation"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	}
	else if (Parameters == TEXT("TargetPoolLease") || Parameters == TEXT("TargetHostility"))
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* EnemyTarget = Fixture.World->SpawnActor<AEnemyBase>(Enemy->GetClass(),
			FVector(1300, 500, 300), FRotator::ZeroRotator, Spawn);
		FBoolProperty* Possessed = FindFProperty<FBoolProperty>(AEnemyBase::StaticClass(), TEXT("bIsPossessed"));
		FIntProperty* Lease = FindFProperty<FIntProperty>(AEnemyBase::StaticClass(), TEXT("PoolLeaseSerial"));
		if (!TestNotNull(TEXT("Concrete hostile player-controlled enemy target spawns"), EnemyTarget)
			|| !TestNotNull(TEXT("Existing target team state exists"), Possessed)
			|| !TestNotNull(TEXT("Existing target lease state exists"), Lease))
		{
			return false;
		}
		EnemyTarget->GetStateTreeComponent()->StopLogic(TEXT("Controlled target identity"));
		EnemyTarget->SetActorTickEnabled(false);
		EnemyTarget->SetActorEnableCollision(false);
		EnemyTarget->GetCharacterMovement()->DisableMovement();
		if (AEnemyAIController* TargetController = Cast<AEnemyAIController>(EnemyTarget->GetController()))
		{
			TargetController->SetEnemyPerceptionEnabled(false);
			TargetController->SetActorTickEnabled(false);
		}
		// 실제 팀 조회가 소비하는 기존 상태만 제공한다. 캐시 키나 준비 결과를 직접 주입하지 않는다.
		Possessed->SetPropertyValue_InContainer(EnemyTarget, true);
		Fixture.Select(EnemyTarget);
		if (!TestEqual(TEXT("Hostile enemy target is actually selected"), Enemy->GetTrackedTarget(), static_cast<AActor*>(EnemyTarget)))
		{
			return false;
		}
		Enemy->AdvanceTargetAttackPreparation(0.45f);
		Fixture.Select(B);
		if (Parameters == TEXT("TargetPoolLease"))
		{
			Lease->SetPropertyValue_InContainer(EnemyTarget, EnemyTarget->GetPoolLeaseSerial() + 1);
		}
		else
		{
			Possessed->SetPropertyValue_InContainer(EnemyTarget, false);
			TestFalse(TEXT("Target team change genuinely removes hostility"), Fixture.Controller->IsValidTrackingTarget(EnemyTarget));
		}
		// 새 정상 입력에서 무효 이력을 폐기한 뒤 같은 Actor가 다시 적대 상태가 되어도 되살리지 않는다.
		Fixture.Select(A);
		Possessed->SetPropertyValue_InContainer(EnemyTarget, true);
		Fixture.Select(EnemyTarget);
		TestTrue(TEXT("Target lease or hostility change discards that target old preparation"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	}
	else if (Parameters == TEXT("FullReset"))
	{
		Enemy->ResetTargetAttackContinuity();
		Fixture.Select(A);
		TestTrue(TEXT("Full lifecycle reset discards inactive history as well as active state"),
			FMath::IsNearlyEqual(Enemy->GetTargetAttackPreparationRemaining(), 1.0f));
	}
	return !HasAnyErrors();
}

#endif
