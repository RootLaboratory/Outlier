#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomTagComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
struct FEnemyAttackContinuityWorld
{
	UWorld* World = nullptr;
	AEnemyBase* Enemy = nullptr;
	AEnemyAIController* Controller = nullptr;
	AShooterCharacter* Target = nullptr;
	ARangedWeaponBase* Weapon = nullptr;
	UClass* TargetClass = nullptr;
	static constexpr float AttackInterval = 0.2f;

	~FEnemyAttackContinuityWorld()
	{
		if (!World) return;
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		if (!Test.TestNotNull(TEXT("Engine exists"), GEngine)) return false;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyAttackContinuity")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Authority world exists"), World)) return false;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->CreateAISystem();
		TargetClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr,
			TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
		if (!Test.TestNotNull(TEXT("Concrete player class exists"), TargetClass)
			|| !Test.TestNotNull(TEXT("Concrete firearm class exists"), WeaponClass)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(0, 0, 400)), Spawn);
		Controller = World->SpawnActor<AEnemyAIController>();
		Target = SpawnTarget(FVector(500, 0, 400));
		Weapon = World->SpawnActor<ARangedWeaponBase>(WeaponClass, Spawn);
		if (!Test.TestNotNull(TEXT("Enemy exists"), Enemy) || !Test.TestNotNull(TEXT("Controller exists"), Controller)
			|| !Test.TestNotNull(TEXT("Real player exists"), Target) || !Test.TestNotNull(TEXT("Weapon exists"), Weapon)) return false;
		Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
		Controller->Possess(Enemy);
		Enemy->EnterCombatFromRoom(Target->GetActorLocation(), false);
		const_cast<FEnemyStat&>(Enemy->GetRuntimeStat()).TargetTrackingGraceSeconds = 0.25f;
		Weapon->OnEquipped(Enemy);

		FObjectPropertyBase* CurrentWeapon = FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"));
		FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
		FBoolProperty* Automatic = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bIsAutomatic"));
		FFloatProperty* Interval = FindFProperty<FFloatProperty>(WeaponClass, TEXT("AttackInterval"));
		FFloatProperty* Range = FindFProperty<FFloatProperty>(WeaponClass, TEXT("EffectiveRange"));
		FFloatProperty* Recoil = FindFProperty<FFloatProperty>(WeaponClass, TEXT("RecoilMultiplier"));
		FIntProperty* Burst = FindFProperty<FIntProperty>(WeaponClass, TEXT("BurstShotCount"));
		if (!Test.TestNotNull(TEXT("Enemy weapon property exists"), CurrentWeapon)
			|| !Test.TestNotNull(TEXT("Finite ammo property exists"), InfiniteAmmo)
			|| !Test.TestNotNull(TEXT("Automatic fire property exists"), Automatic)
			|| !Test.TestNotNull(TEXT("Interval property exists"), Interval)
			|| !Test.TestNotNull(TEXT("Range property exists"), Range)
			|| !Test.TestNotNull(TEXT("Recoil property exists"), Recoil)
			|| !Test.TestNotNull(TEXT("Burst property exists"), Burst)) return false;
		// 장착 시 적용된 DT 값 이후 인스턴스만 고정한다. 실제 탄약 소비를 발사 횟수로 측정한다.
		CurrentWeapon->SetObjectPropertyValue_InContainer(Enemy, Weapon);
		InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
		Automatic->SetPropertyValue_InContainer(Weapon, true);
		Interval->SetPropertyValue_InContainer(Weapon, AttackInterval);
		Range->SetPropertyValue_InContainer(Weapon, 1000.0f);
		Recoil->SetPropertyValue_InContainer(Weapon, 0.0f);
		Burst->SetPropertyValue_InContainer(Weapon, 0);
		Sight(Target);
		return Test.TestTrue(TEXT("Equipped weapon can fire"), Weapon->CanAttack())
			&& Test.TestTrue(TEXT("Initial live target has a clear attack path"), Enemy->CanAttackTrackedTarget(Target));
	}

	AShooterCharacter* SpawnTarget(const FVector& Location)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterCharacter* Player = World->SpawnActor<AShooterCharacter>(TargetClass, FTransform(Location), Spawn);
		if (Player)
		{
			Player->GetCharacterMovement()->SetComponentTickEnabled(false);
			// 사망/피해로 추적 조건이 변하지 않도록 실제 플레이어의 충돌만 끈다.
			Player->SetActorEnableCollision(false);
		}
		return Player;
	}

	void Sight(AActor* Player)
	{
		Controller->SetCachedVisibleTargetForTesting(Player);
		Enemy->ObserveTrackedTarget(Player);
		Enemy->SetPlayerCurrentlyVisible(true);
	}

	void LoseSight()
	{
		Controller->SetCachedVisibleTargetForTesting(nullptr);
		Enemy->SetPlayerCurrentlyVisible(false);
	}

	void Advance(float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(0.0f);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds * 120.0f));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			// 감지 Actor는 실행하지 않고 유예 시각과 실제 발사 Timer를 같은 시간축에서 진행한다.
			World->Tick(LEVELTICK_TimeOnly, Seconds / Steps);
			World->GetTimerManager().Tick(World->GetDeltaSeconds());
			Enemy->RefreshTargetTracking();
		}
	}

	UBoxComponent* AddWall(ECollisionChannel BlockedChannel)
	{
		AActor* Wall = World->SpawnActor<AActor>();
		if (!Wall) return nullptr;
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(10, 200, 300));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(BlockedChannel, ECR_Block);
		Box->RegisterComponent();
		Box->SetWorldLocation(FVector(250, 0, 400));
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Box;
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyAttackContinuityTest,
	"Outlier.Enemy.Combat.Continuity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyAttackContinuityTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("GraceFire"), TEXT("PhysicsWall"), TEXT("VisibilityWall"), TEXT("Range"),
		TEXT("TargetIdentity"), TEXT("Expiry"), TEXT("ReacquireGrace"), TEXT("ReacquireSearch"),
		TEXT("DifferentTarget"), TEXT("Reset"), TEXT("StunReset"), TEXT("ControllerReset"), TEXT("PauseCadence"),
		TEXT("SharedFire"), TEXT("SharedWall"), TEXT("SharedRange"), TEXT("SharedReacquire"),
		TEXT("SharedExpiry"), TEXT("SharedStun")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyAttackContinuityTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyAttackContinuityWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	AEnemyBase* Enemy = Fixture.Enemy;
	ARangedWeaponBase* Weapon = Fixture.Weapon;
	const int32 InitialAmmo = Weapon->GetCurrentAmmo();
	if (Parameters.StartsWith(TEXT("Shared")))
	{
		UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
		AEnemyBase* Observer = Fixture.World->SpawnActor<AEnemyBase>();
		AShooterCharacter* SharedTarget = Fixture.SpawnTarget(FVector(500, 100, 400));
		if (!TestNotNull(TEXT("Room subsystem exists"), Rooms) || !TestNotNull(TEXT("Direct observer exists"), Observer)
			|| !TestNotNull(TEXT("Never-seen shared target exists"), SharedTarget)) return false;
		const FGameplayTag Room = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Room);
		Observer->GetRoomTagComp()->AssignDefaultRoomTag(Room);
		Fixture.LoseSight();
		Enemy->ClearTargetTracking();
		Rooms->SetActiveRoomTargetForTesting(Room, SharedTarget->GetActorLocation(), false);
		Rooms->ReportRoomTargetContact(Observer, SharedTarget, SharedTarget->GetActorLocation());
		Enemy->RefreshTargetTracking();
		TestFalse(TEXT("Shared attacks preserve actual lost sight"), Enemy->IsPlayerCurrentlyVisible());
		if (!TestEqual(TEXT("The new shared player is the tracked identity"), Enemy->GetTrackedTarget(), static_cast<AActor*>(SharedTarget))) return false;
		if (Parameters == TEXT("SharedRange"))
		{
			SharedTarget->SetActorLocation(FVector(500, 100, 1600));
			TestFalse(TEXT("Shared information cannot bypass weapon range"), Enemy->StartAttackTarget(SharedTarget));
			TestEqual(TEXT("Rejected shared range consumes no ammunition"), Weapon->GetCurrentAmmo(), InitialAmmo);
			return true;
		}
		if (!TestTrue(TEXT("Shared-only target can start real fire before arrival"), Enemy->StartAttackTarget(SharedTarget))) return false;
		TestEqual(TEXT("Shared attack consumes a real first round"), Weapon->GetCurrentAmmo(), InitialAmmo - 1);
		if (Parameters == TEXT("SharedWall"))
		{
			UBoxComponent* Wall = Fixture.AddWall(ECC_Visibility);
			if (!TestNotNull(TEXT("A real blocking wall exists"), Wall)) return false;
			Fixture.Advance(0.21f);
			TestEqual(TEXT("Shared live tracking cannot fire through a wall"), Weapon->GetCurrentAmmo(), InitialAmmo - 1);
			Enemy->PauseTargetAttack();
			Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			TestTrue(TEXT("A clear shared path resumes after the existing cooldown"), Enemy->StartAttackTarget(SharedTarget));
			TestEqual(TEXT("Path reopening fires without an additional preparation delay"), Weapon->GetCurrentAmmo(), InitialAmmo - 2);
			Fixture.Advance(0.05f);
			Enemy->PauseTargetAttack();
			TestFalse(TEXT("Reopening the path cannot shorten the next real shot interval"), Enemy->StartAttackTarget(SharedTarget));
			Fixture.Advance(0.16f);
			TestTrue(TEXT("The next real shot interval expires normally after reopening"), Enemy->StartAttackTarget(SharedTarget));
			TestEqual(TEXT("Reopening and pausing preserve exact ammunition consumption"), Weapon->GetCurrentAmmo(), InitialAmmo - 3);
		}
		else if (Parameters == TEXT("SharedReacquire"))
		{
			Fixture.Advance(0.05f);
			Fixture.Controller->SetCachedVisibleTargetForTesting(SharedTarget);
			Enemy->SetPlayerCurrentlyVisible(true);
			Fixture.LoseSight();
			TestTrue(TEXT("Shared/direct/shared handoff retains attack ownership"), Enemy->HasActiveTargetAttack());
			TestTrue(TEXT("Same-target handoff retains the weapon timer"), Weapon->IsAttacking());
			TestEqual(TEXT("Handoff cannot inject an initial round"), Weapon->GetCurrentAmmo(), InitialAmmo - 1);
			Fixture.Advance(0.16f);
			TestEqual(TEXT("Handoff preserves the original shot interval"), Weapon->GetCurrentAmmo(), InitialAmmo - 2);
		}
		else if (Parameters == TEXT("SharedExpiry") || Parameters == TEXT("SharedStun"))
		{
			if (Parameters == TEXT("SharedStun")) Enemy->EnterStun();
			else Rooms->RemoveRoomTargetObserver(Observer);
			Fixture.Advance(0.3f);
			TestFalse(TEXT("Expiry or stun ends shared attack eligibility"), Enemy->CanAttackTrackedTarget(SharedTarget));
			TestFalse(TEXT("Expiry or stun releases attack ownership"), Enemy->HasActiveTargetAttack());
			const int32 StoppedAmmo = Weapon->GetCurrentAmmo();
			Fixture.Advance(0.3f);
			TestEqual(TEXT("No stale firing callback survives shared interruption"), Weapon->GetCurrentAmmo(), StoppedAmmo);
		}
		else
		{
			Fixture.Advance(0.3f);
			TestTrue(TEXT("A live observer keeps tracking past the local grace duration"), Enemy->HasTrackableTarget());
			TestEqual(TEXT("Live shared tracking continues automatic fire"), Weapon->GetCurrentAmmo(), InitialAmmo - 2);
		}
	}
	else if (Parameters == TEXT("GraceFire"))
	{
		Fixture.LoseSight();
		TestFalse(TEXT("Grace preserves truthful lost sight"), Enemy->IsPlayerCurrentlyVisible());
		TestTrue(TEXT("Grace with an open path permits attack"), Enemy->CanAttackTrackedTarget(Fixture.Target));
		TestTrue(TEXT("Grace starts real fire before a movement destination is reached"), Enemy->StartAttackTarget(Fixture.Target));
		TestEqual(TEXT("Grace fire consumes one initial round"), Weapon->GetCurrentAmmo(), InitialAmmo - 1);
		Fixture.Advance(0.21f);
		TestEqual(TEXT("Automatic fire continues inside tracking grace"), Weapon->GetCurrentAmmo(), InitialAmmo - 2);
	}
	else if (Parameters == TEXT("PhysicsWall") || Parameters == TEXT("VisibilityWall"))
	{
		if (!TestTrue(TEXT("Attack starts before the wall is introduced"), Enemy->StartAttackTarget(Fixture.Target))) return false;
		Fixture.LoseSight();
		UBoxComponent* Wall = Fixture.AddWall(Parameters == TEXT("PhysicsWall") ? ECC_PhysicsBody : ECC_Visibility);
		if (!TestNotNull(TEXT("Channel-specific wall exists"), Wall)) return false;
		TestFalse(TEXT("A wall on either actual attack channel rejects the tracked target"), Enemy->CanAttackTrackedTarget(Fixture.Target));
		Fixture.Advance(0.21f);
		TestEqual(TEXT("An already-running automatic timer cannot shoot through the wall"), Weapon->GetCurrentAmmo(), InitialAmmo - 1);
		Enemy->PauseTargetAttack();
		Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TestTrue(TEXT("An opened path restores eligibility without new sight"), Enemy->CanAttackTrackedTarget(Fixture.Target));
	}
	else if (Parameters == TEXT("Range"))
	{
		Fixture.LoseSight();
		Fixture.Target->SetActorLocation(FVector(500, 0, 1600));
		TestFalse(TEXT("Horizontal proximity cannot bypass the actual 3D weapon range"), Enemy->CanAttackTrackedTarget(Fixture.Target));
		TestFalse(TEXT("Out-of-range target cannot start firing"), Enemy->StartAttackTarget(Fixture.Target));
		TestEqual(TEXT("Rejected range does not consume ammo"), Weapon->GetCurrentAmmo(), InitialAmmo);
	}
	else if (Parameters == TEXT("Expiry"))
	{
		if (!TestTrue(TEXT("Attack starts before expiry"), Enemy->StartAttackTarget(Fixture.Target))) return false;
		Fixture.LoseSight();
		Fixture.Advance(0.3f);
		TestFalse(TEXT("Expired grace rejects the previously valid player"), Enemy->CanAttackTrackedTarget(Fixture.Target));
		const int32 AmmoAtExpiry = Weapon->GetCurrentAmmo();
		Fixture.Advance(0.3f);
		TestEqual(TEXT("No automatic rounds follow tracking expiry"), Weapon->GetCurrentAmmo(), AmmoAtExpiry);
		TestFalse(TEXT("An explicit former target cannot restart after expiry"), Enemy->StartAttackTarget(Fixture.Target));
	}
	else if (Parameters == TEXT("TargetIdentity"))
	{
		AShooterCharacter* Other = Fixture.SpawnTarget(FVector(500, 100, 400));
		if (!TestNotNull(TEXT("A second real player exists"), Other)) return false;
		TestFalse(TEXT("A valid player outside the tracked identity cannot be attacked"), Enemy->CanAttackTrackedTarget(Other));
		TestFalse(TEXT("An explicit alternate player cannot bypass tracking"), Enemy->StartAttackTarget(Other));
		TestEqual(TEXT("Rejected identity does not consume ammo"), Weapon->GetCurrentAmmo(), InitialAmmo);
		Fixture.Sight(Other);
		TestFalse(TEXT("A former target becomes ineligible after target replacement"), Enemy->CanAttackTrackedTarget(Fixture.Target));
		TestTrue(TEXT("The newly tracked player becomes eligible"), Enemy->CanAttackTrackedTarget(Other));
	}
	else if (Parameters == TEXT("ReacquireGrace"))
	{
		if (!TestTrue(TEXT("Attack starts before grace rediscovery"), Enemy->StartAttackTarget(Fixture.Target))) return false;
		Fixture.LoseSight();
		Fixture.Advance(0.05f);
		Fixture.Sight(Fixture.Target);
		TestTrue(TEXT("Same-target rediscovery retains the running attack"), Enemy->HasActiveTargetAttack());
		TestTrue(TEXT("Same-target rediscovery retains weapon fire"), Weapon->IsAttacking());
		TestEqual(TEXT("Rediscovery cannot inject an initial shot"), Weapon->GetCurrentAmmo(), InitialAmmo - 1);
		Fixture.Advance(0.16f);
		TestEqual(TEXT("Rediscovery preserves the existing fire interval"), Weapon->GetCurrentAmmo(), InitialAmmo - 2);
	}
	else if (Parameters == TEXT("ReacquireSearch") || Parameters == TEXT("DifferentTarget") || Parameters == TEXT("Reset"))
	{
		Enemy->RememberTargetAttackPreparation();
		Fixture.LoseSight();
		Fixture.Advance(0.3f);
		TestNull(TEXT("Rediscovery scenario first expires live tracking"), Enemy->GetTrackedTarget());
		TestTrue(TEXT("Expiry permits entering a real search episode"), Enemy->BeginSearchEpisode());
		AActor* Rediscovered = Fixture.Target;
		if (Parameters == TEXT("DifferentTarget"))
		{
			Rediscovered = Fixture.SpawnTarget(FVector(500, 100, 400));
			if (!TestNotNull(TEXT("A different real player exists"), Rediscovered)) return false;
		}
		else if (Parameters == TEXT("Reset")) Enemy->ResetTargetAttackContinuity();
		Fixture.Sight(Rediscovered);
		TestEqual(TEXT("Only the same prepared target can skip repeated preparation after search"),
			Enemy->ConsumeTargetAttackResume(), Parameters == TEXT("ReacquireSearch"));
		TestFalse(TEXT("Preparation resume is consumed once"), Enemy->ConsumeTargetAttackResume());
	}
	else if (Parameters == TEXT("StunReset") || Parameters == TEXT("ControllerReset"))
	{
		if (!TestTrue(TEXT("Lifecycle scenario starts a prepared real attack"), Enemy->StartAttackTarget(Fixture.Target))) return false;
		Fixture.LoseSight();
		if (Parameters == TEXT("StunReset")) Enemy->EnterStun();
		else Fixture.Controller->UnPossess();
		TestFalse(TEXT("Lifecycle interruption releases active attack ownership"), Enemy->HasActiveTargetAttack());
		TestFalse(TEXT("Lifecycle interruption stops the actual weapon"), Weapon->IsAttacking());

		// 같은 대상/무기/Controller를 복구해도 이전 전투 준비를 재사용하면 안 된다.
		if (Parameters == TEXT("StunReset")) Enemy->RestoreStateAfterStun();
		else Fixture.Controller->Possess(Enemy);
		Fixture.Sight(Fixture.Target);
		TestTrue(TEXT("Restored lifecycle has a valid current target again"), Enemy->CanAttackTrackedTarget(Fixture.Target));
		TestFalse(TEXT("Actual stun or unpossess clears remembered preparation"), Enemy->ConsumeTargetAttackResume());
	}
	else if (Parameters == TEXT("PauseCadence"))
	{
		if (!TestTrue(TEXT("Cadence starts a real first shot"), Enemy->StartAttackTarget(Fixture.Target))) return false;
		Fixture.Advance(0.05f);
		Enemy->PauseTargetAttack();
		TestFalse(TEXT("Pause releases attack ownership"), Enemy->HasActiveTargetAttack());
		TestFalse(TEXT("Pause stops the firing timer"), Weapon->IsAttacking());
		TestFalse(TEXT("Immediate retry cannot reset the remaining fire interval"), Enemy->StartAttackTarget(Fixture.Target));
		Fixture.Advance(0.1f);
		TestFalse(TEXT("Retry before the original interval still waits"), Enemy->StartAttackTarget(Fixture.Target));
		TestEqual(TEXT("Pause and retries cannot inject extra rounds"), Weapon->GetCurrentAmmo(), InitialAmmo - 1);
		Fixture.Advance(0.06f);
		TestTrue(TEXT("Retry after the original interval resumes immediately"), Enemy->StartAttackTarget(Fixture.Target));
		TestEqual(TEXT("Resume consumes exactly one round"), Weapon->GetCurrentAmmo(), InitialAmmo - 2);
	}
	else
	{
		AddError(FString::Printf(TEXT("Unknown continuity scenario: %s"), *Parameters));
	}
	return true;
}

#endif
