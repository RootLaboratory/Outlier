#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSlideFireTest,
	"Outlier.Shooter.Slide.Fire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSlideFireTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete Shooter Blueprint"), ShooterClass)) return false;

	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SlideFire"),
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Slide fire test world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AActor* Floor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Floor actor"), Floor)) return false;
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(10000.0, 10000.0, 50.0));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetMobility(EComponentMobility::Static);
	Box->SetWorldLocation(FVector(0.0, 0.0, -50.0));
	Box->RegisterComponent();

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass,
		FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Shooter fixture"), Shooter)) return false;
	APlayerController* Controller = World->SpawnActor<APlayerController>(Spawn);
	if (!TestNotNull(TEXT("Server fire controller"), Controller)) return false;
	Controller->Possess(Shooter);
	Controller->SetAsLocalPlayerController();
	auto* Movement = Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
	auto* Input = Shooter->FindComponentByClass<UShooterMovementComponent>();
	auto* Combat = Shooter->FindComponentByClass<UShooterCombatComponent>();
	if (!TestNotNull(TEXT("Slide simulation"), Movement)
		|| !TestNotNull(TEXT("Movement intent"), Input)
		|| !TestNotNull(TEXT("Combat intent"), Combat)) return false;
	const FBoolProperty* InfiniteAmmoProperty = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
	if (!TestNotNull(TEXT("Finite-ammo fixture setting"), InfiniteAmmoProperty)) return false;
	Movement->bRunPhysicsWithNoController = true;
	Movement->SetMovementMode(MOVE_Walking);
	Shooter->SetActorLocation(FVector(0.0, 0.0, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15));
	Movement->bForceNextFloorCheck = true;
	Movement->PerformMovement(1.0f / 60.0f);
	if (!TestTrue(TEXT("Fixture has a real walkable floor"), Movement->CurrentFloor.IsWalkableFloor())) return false;
	Shooter->SlideDuration = 10.0f;
	Shooter->MinSlideSpeed = 200.0f;
	Movement->Velocity = FVector(400.0, 0.0, 0.0);
	// 로컬 입력은 다음 이동에서 소비된다. Sprint와 Crouch를 실제 입력 순서로 판정한다.
	Input->HandleSprintPressed();
	Shooter->CheckJumpInput(1.0f / 60.0f);
	Input->HandleCrouchPressed();
	Shooter->CheckJumpInput(1.0f / 60.0f);
	if (!TestTrue(TEXT("Gameplay input starts the slide"), Input->IsSliding() && Movement->IsSlideSimulating())) return false;

	// 월드 전체 Tick 없이 발사 타이머만 전진시킨다. 이동/애니메이션의 갱신 시점과 분리해
	// 발사 입력과 ADS 전환 자체가 Slide 속도나 경과 시간을 덮어쓰는지 확인한다.
	auto TickFireTimers = [World](float Delta)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(Delta);
	};
	for (const TCHAR* WeaponPath : {
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"),
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C")})
	{
		UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr, WeaponPath);
		if (!TestNotNull(TEXT("Concrete firearm Blueprint"), WeaponClass)) return false;
		for (const bool bStartAiming : {false, true})
		{
			ARangedWeaponBase* Weapon = World->SpawnActor<ARangedWeaponBase>(WeaponClass, Spawn);
			if (!TestNotNull(TEXT("Firearm fixture"), Weapon)) return false;
			// 실제 장착을 거쳐 서버의 소유자/장착 상태와 캐릭터의 무기 타입을 함께 준비한다.
			Shooter->AFirstPersonCharacter::EquipWeapon(Weapon);
			Shooter->RefreshWeaponMode();
			// 장착 시 DataTable의 무한 탄약 설정이 적용된다. 이 테스트는 탄약 감소로 발사 횟수와
			// 빈 탄창 거절을 측정하므로, 장착이 끝난 테스트 인스턴스만 유한 탄약으로 고정한다.
			AddInfo(FString::Printf(TEXT("[SlideFireTest] Weapon=%s ConfiguredInfiniteAmmo=%d FixtureInfiniteAmmo=0"),
				*GetNameSafe(Weapon), InfiniteAmmoProperty->GetPropertyValue_InContainer(Weapon) ? 1 : 0));
			InfiniteAmmoProperty->SetPropertyValue_InContainer(Weapon, false);
			Combat->ApplyAimInput(bStartAiming);
			if (!TestEqual(TEXT("Slide applies the requested Hip/ADS state"), Combat->IsAiming(), bStartAiming)
				|| !TestEqual(TEXT("Slide keeps the requested Aim intent"), Combat->WantsToAim(), bStartAiming)
				|| !TestTrue(TEXT("Equipped server weapon can fire"), Weapon->CanAttack())) return false;

			const FFloatProperty* IntervalProperty = FindFProperty<FFloatProperty>(WeaponClass, TEXT("AttackInterval"));
			const FBoolProperty* AutomaticProperty = FindFProperty<FBoolProperty>(WeaponClass, TEXT("bIsAutomatic"));
			if (!TestNotNull(TEXT("Existing attack interval property"), IntervalProperty)
				|| !TestNotNull(TEXT("Existing automatic fire property"), AutomaticProperty)) return false;
			const float Interval = IntervalProperty->GetPropertyValue_InContainer(Weapon);
			const bool bAutomatic = AutomaticProperty->GetPropertyValue_InContainer(Weapon);
			if (!TestTrue(TEXT("Fixture uses a positive fire interval"), Interval > KINDA_SMALL_NUMBER)) return false;
			const FVector Velocity = Movement->Velocity;
			const FVector Direction = Movement->GetSlideDirection();
			const float Elapsed = Movement->GetSlideElapsedTime();
			const int32 BeforeFire = Weapon->GetCurrentAmmo();
			Combat->TryStartAttack();
			TestEqual(TEXT("Slide fire consumes exactly one initial round"), Weapon->GetCurrentAmmo(), BeforeFire - 1);
			Combat->TryStartAttack();
			TestEqual(TEXT("Repeated Start does not duplicate the initial shot"), Weapon->GetCurrentAmmo(), BeforeFire - 1);
			TickFireTimers(0.0f);
			TickFireTimers(Interval * 0.5f);
			TestEqual(TEXT("No shot before the existing interval"), Weapon->GetCurrentAmmo(), BeforeFire - 1);

			Combat->ApplyAimInput(!bStartAiming);
			TestEqual(TEXT("Held fire supports both Hip-to-ADS and ADS-to-Hip"), Combat->IsAiming(), !bStartAiming);
			TestEqual(TEXT("Aim transition cannot inject an extra shot"), Weapon->GetCurrentAmmo(), BeforeFire - 1);
			TickFireTimers(Interval * 0.6f);
			TestEqual(TEXT("Automatic repeats while semi-auto keeps one round per press"),
				Weapon->GetCurrentAmmo(), BeforeFire - (bAutomatic ? 2 : 1));
			TestTrue(TEXT("Fire and Aim transitions keep the slide active"), Input->IsSliding() && Movement->IsSlideSimulating());
			TestEqual(TEXT("Fire and Aim transitions preserve the Slide lock"), Shooter->GetActionLock(), EShooterActionLock::Slide);
			TestTrue(TEXT("Fire and Aim transitions preserve actual velocity"), Movement->Velocity.Equals(Velocity));
			TestTrue(TEXT("Fire and Aim transitions preserve slide direction"), Movement->GetSlideDirection().Equals(Direction));
			TestEqual(TEXT("Fire and Aim transitions do not restart slide time"), Movement->GetSlideElapsedTime(), Elapsed);

			Combat->TryStopAttack();
			Combat->ApplyAimInput(false);
			const int32 AfterRelease = Weapon->GetCurrentAmmo();
			TickFireTimers(Interval * 2.0f);
			TestEqual(TEXT("Fire Release stops later shots"), Weapon->GetCurrentAmmo(), AfterRelease);
			TestFalse(TEXT("Fire Release stops weapon attack state"), Weapon->IsAttacking());

			for (const EShooterActionLock Lock : {EShooterActionLock::Reload, EShooterActionLock::Equip})
			{
				Shooter->BeginActionLock(Lock);
				TestFalse(TEXT("Non-Slide action locks still reject fire"), Combat->CanFireInCurrentState());
				Combat->TryStartAttack();
				TestEqual(TEXT("Rejected fire cannot consume ammo"), Weapon->GetCurrentAmmo(), AfterRelease);
				Shooter->EndActionLock(Lock);
				Shooter->BeginActionLock(EShooterActionLock::Slide);
			}
			Weapon->RestoreCheckpointAmmo(0);
			Combat->TryStartAttack();
			TestEqual(TEXT("Empty firearm cannot consume more ammo"), Weapon->GetCurrentAmmo(), 0);
			TestFalse(TEXT("Empty firearm cannot start an attack"), Weapon->IsAttacking());
		}
	}

	// Slide 안에서의 사격만으로는 점프/착지 콜백의 중복 발사나 타이머 취소를 검출할 수 없다.
	// 새 연사 무기를 사용해 직전 무기의 탄약/쿨다운과 경계 검증을 분리한다.
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!TestNotNull(TEXT("Jump boundary rifle class"), RifleClass)) return false;
	ARangedWeaponBase* Rifle = World->SpawnActor<ARangedWeaponBase>(RifleClass, Spawn);
	if (!TestNotNull(TEXT("Jump boundary rifle"), Rifle)) return false;
	Shooter->AFirstPersonCharacter::EquipWeapon(Rifle);
	Shooter->RefreshWeaponMode();
	// 점프/착지 경계도 같은 탄약 측정 조건을 사용한다.
	InfiniteAmmoProperty->SetPropertyValue_InContainer(Rifle, false);
	const FFloatProperty* RifleIntervalProperty = FindFProperty<FFloatProperty>(RifleClass, TEXT("AttackInterval"));
	const FBoolProperty* RifleAutomaticProperty = FindFProperty<FBoolProperty>(RifleClass, TEXT("bIsAutomatic"));
	if (!TestNotNull(TEXT("Jump boundary fire interval"), RifleIntervalProperty)
		|| !TestNotNull(TEXT("Jump boundary automatic setting"), RifleAutomaticProperty)
		|| !TestTrue(TEXT("Jump boundary uses an automatic rifle"), RifleAutomaticProperty->GetPropertyValue_InContainer(Rifle))) return false;
	const float RifleInterval = RifleIntervalProperty->GetPropertyValue_InContainer(Rifle);
	if (!TestTrue(TEXT("Jump boundary fire interval is positive"), RifleInterval > KINDA_SMALL_NUMBER)) return false;
	UCurveFloat* JumpCurve = NewObject<UCurveFloat>(Movement);
	JumpCurve->FloatCurve.AddKey(0.0f, 1.0f);
	JumpCurve->FloatCurve.AddKey(1.0f, 0.0f);
	Movement->SlideJumpDecayCurve = JumpCurve;
	// 점프는 로컬 입력 이력을 소비하므로 Aim도 물리 입력 경로로 기록한다.
	Combat->HandleAimPressed();
	Shooter->CheckJumpInput(0.0f);
	if (!TestTrue(TEXT("Jump boundary starts with Slide ADS"), Combat->IsAiming() && Input->IsSliding())) return false;
	const int32 BeforeJumpFire = Rifle->GetCurrentAmmo();
	Combat->TryStartAttack();
	if (!TestEqual(TEXT("Jump boundary fires its initial round"), Rifle->GetCurrentAmmo(), BeforeJumpFire - 1)) return false;
	TickFireTimers(0.0f);
	Input->DoJumpStart();
	Shooter->CheckJumpInput(0.0f);
	if (!TestTrue(TEXT("Held fire allows a real Slide jump"), Movement->IsFalling() && Shooter->JumpCurrentCount == 1)) return false;
	Input->DoJumpEnd();
	TestFalse(TEXT("Takeoff suspends ADS under the existing airborne policy"), Combat->IsAiming());
	TestTrue(TEXT("Takeoff preserves held Aim for landing"), Combat->WantsToAim());
	TestFalse(TEXT("Firing cancels Sprint and jumping cannot revive it"), Input->WantsToSprint());
	TestEqual(TEXT("Takeoff cannot inject an extra shot"), Rifle->GetCurrentAmmo(), BeforeJumpFire - 1);
	TickFireTimers(RifleInterval * 1.1f);
	TestEqual(TEXT("Held fire keeps its cadence after Slide takeoff"), Rifle->GetCurrentAmmo(), BeforeJumpFire - 2);
	// 착지 콜백만 진행해 이동 시간과 발사 타이머의 경계를 독립적으로 검증한다.
	Movement->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("Landing restores the held Aim"), Combat->IsAiming());
	TestFalse(TEXT("Landing cannot restart the old Slide or Sprint"), Input->IsSliding() || Input->WantsToSprint());
	TestEqual(TEXT("Landing cannot inject an extra shot"), Rifle->GetCurrentAmmo(), BeforeJumpFire - 2);
	TickFireTimers(RifleInterval * 1.1f);
	TestEqual(TEXT("Held fire keeps its cadence after landing"), Rifle->GetCurrentAmmo(), BeforeJumpFire - 3);
	Combat->TryStopAttack();
	Combat->HandleAimReleased();
	Shooter->CheckJumpInput(0.0f);
	const int32 AfterJumpRelease = Rifle->GetCurrentAmmo();
	TickFireTimers(RifleInterval * 2.0f);
	TestEqual(TEXT("Release after landing stops the existing fire timer"), Rifle->GetCurrentAmmo(), AfterJumpRelease);
	TestFalse(TEXT("Release after landing clears the weapon attack state"), Rifle->IsAttacking());

	// 뒤의 근접 정책 검증은 다시 Slide 상태에서 수행한다.
	Movement->Velocity = FVector(400.0, 0.0, 0.0);
	Input->HandleSprintPressed();
	Shooter->CheckJumpInput(0.0f);
	Input->HandleCrouchPressed();
	Shooter->CheckJumpInput(0.0f);
	if (!TestTrue(TEXT("Melee policy fixture reenters Slide after landing"), Input->IsSliding())) return false;
	UClass* MeleeClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
	if (!TestNotNull(TEXT("Concrete melee Blueprint"), MeleeClass)) return false;
	AWeaponBase* Melee = World->SpawnActor<AWeaponBase>(MeleeClass, Spawn);
	if (!TestNotNull(TEXT("Melee fixture"), Melee)) return false;
	Shooter->AFirstPersonCharacter::EquipWeapon(Melee);
	Shooter->RefreshWeaponMode();
	for (const bool bAllowMelee : {false, true})
	{
		Shooter->bAllowMeleeAttackWhileSliding = bAllowMelee;
		TestEqual(TEXT("Melee keeps its existing Slide opt-in policy"), Combat->CanFireInCurrentState(), bAllowMelee);
	}
	Input->StopSlide(ESlideEndReason::ForcedCancel);
	return true;
}

#endif
