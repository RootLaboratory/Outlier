#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterEquipFireTest,
	"Outlier.Shooter.Equip.Fire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipFireTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	const auto* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
	const auto* Automatic = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bIsAutomatic"));
	const auto* MagazineSize = FindFProperty<FIntProperty>(ARangedWeaponBase::StaticClass(), TEXT("MagazineSize"));
	const auto* AttackInterval = FindFProperty<FFloatProperty>(ARangedWeaponBase::StaticClass(), TEXT("AttackInterval"));
	if (!TestNotNull(TEXT("Shooter Blueprint"), ShooterClass)
		|| !TestNotNull(TEXT("Rifle Blueprint"), RifleClass)
		|| !TestNotNull(TEXT("Pistol Blueprint"), PistolClass)
		|| !TestNotNull(TEXT("Finite ammo property"), InfiniteAmmo)
		|| !TestNotNull(TEXT("Automatic weapon property"), Automatic)
		|| !TestNotNull(TEXT("Magazine size property"), MagazineSize)
		|| !TestNotNull(TEXT("Attack interval property"), AttackInterval))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EquipFire"), EUniqueObjectNameOptions::GloballyUnique),
		GetTransientPackage());
	if (!TestNotNull(TEXT("Equip fire test world"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	TArray<APlayerController*> LocalControllers;
	ON_SCOPE_EXIT
	{
		for (APlayerController* Controller : LocalControllers)
		{
			Controller->Player = nullptr;
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	auto TickTimers = [World](float Delta)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(Delta);
	};
	auto FinishServerTimers = [&]()
	{
		// 첫 Tick에서 아직 정규 Tick을 받지 않은 타이머를 활성화한다.
		TickTimers(0.0f);
		TickTimers(3.0f);
	};

	struct FFixture
	{
		AShooterCharacter* Shooter = nullptr;
		UShooterInventoryComponent* Inventory = nullptr;
		ARangedWeaponBase* Rifle = nullptr;
		ARangedWeaponBase* Pistol = nullptr;
	};
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 NextFixture = 0;
	auto Setup = [&](FFixture& Fixture, bool bLocalOwner, bool bAutomaticRifle = true)
	{
		const FVector Location(0.0, 10000.0 * NextFixture++, 200.0);
		Fixture.Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Location, FRotator::ZeroRotator, Spawn);
		Fixture.Rifle = World->SpawnActor<ARangedWeaponBase>(RifleClass, Location, FRotator::ZeroRotator, Spawn);
		Fixture.Pistol = World->SpawnActor<ARangedWeaponBase>(PistolClass, Location, FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("Fixture Shooter"), Fixture.Shooter)
			|| !TestNotNull(TEXT("Fixture Rifle"), Fixture.Rifle)
			|| !TestNotNull(TEXT("Fixture Pistol"), Fixture.Pistol))
		{
			return false;
		}
		Fixture.Inventory = Fixture.Shooter->GetInventoryComponent();
		Fixture.Inventory->BeginPlay();
		Fixture.Shooter->FirstPersonSwitchLowerDuration = 0.10f;
		Fixture.Shooter->FirstPersonSwitchRaiseDuration = 0.10f;
		Fixture.Inventory->HandleEquipWeapon(Fixture.Rifle);
		FinishServerTimers();
		Fixture.Inventory->HandleEquipWeapon(Fixture.Pistol);
		FinishServerTimers();
		if (!TestEqual(TEXT("Fixture begins with the pistol equipped"), Fixture.Shooter->GetCurrentWeapon(),
			static_cast<AWeaponBase*>(Fixture.Pistol))
			|| !TestEqual(TEXT("Fixture finishes its initial Equip"), Fixture.Shooter->GetActionLock(), EShooterActionLock::None))
		{
			return false;
		}
		for (ARangedWeaponBase* Weapon : {Fixture.Rifle, Fixture.Pistol})
		{
			InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
			MagazineSize->SetPropertyValue_InContainer(Weapon, 12);
			Weapon->RestoreCheckpointAmmo(12);
		}
		Automatic->SetPropertyValue_InContainer(Fixture.Rifle, bAutomaticRifle);
		Automatic->SetPropertyValue_InContainer(Fixture.Pistol, true);

		// 원격 서버 사례도 실제 탄약을 소비하려면 총기의 시점 계산에 필요한 Controller가 있어야 한다.
		APlayerController* Controller = World->SpawnActor<APlayerController>(Spawn);
		if (!TestNotNull(TEXT("Owning controller"), Controller))
		{
			return false;
		}
		Controller->Possess(Fixture.Shooter);
		if (bLocalOwner)
		{
			LocalControllers.Add(Controller);
			Controller->Player = NewObject<ULocalPlayer>(GEngine);
			Controller->SetAsLocalPlayerController();
			return TestTrue(TEXT("Fixture uses local owner input"), Fixture.Shooter->IsLocallyControlled());
		}
		return TestFalse(TEXT("Remote authority does not have a local presentation"), Fixture.Shooter->IsLocallyControlled());
	};

	enum class EPressPhase
	{
		AfterCompletion,
		Lower,
		WaitingForWeapon,
		Raise,
		TapDuringLower,
		InputResetDuringLower
	};
	for (EPressPhase PressPhase : {
		EPressPhase::AfterCompletion, EPressPhase::Lower, EPressPhase::WaitingForWeapon,
		EPressPhase::Raise, EPressPhase::TapDuringLower, EPressPhase::InputResetDuringLower})
	{
		FFixture Fixture;
		if (!Setup(Fixture, true))
		{
			return false;
		}
		AShooterCharacter* Shooter = Fixture.Shooter;
		ARangedWeaponBase* Rifle = Fixture.Rifle;
		ARangedWeaponBase* Pistol = Fixture.Pistol;
		const int32 InitialRifleAmmo = Rifle->GetCurrentAmmo();

		Shooter->TryStartAttack();
		if (!TestTrue(TEXT("Source weapon fires before switching"), Pistol->IsAttacking()))
		{
			return false;
		}
		const int32 PistolAmmoAfterStart = Pistol->GetCurrentAmmo();
		Fixture.Inventory->SelectWeaponByIndex(static_cast<int32>(EWeaponSlot::Primary));
		TestTrue(TEXT("Slot selection opens Equip"), Shooter->IsWeaponEquipInputBlocked());
		TestFalse(TEXT("Accepting the switch stops the old weapon immediately"), Pistol->IsAttacking());
		if (PressPhase != EPressPhase::AfterCompletion)
		{
			Shooter->HandleAttackInputReleased();
		}
		if (PressPhase == EPressPhase::Lower || PressPhase == EPressPhase::TapDuringLower
			|| PressPhase == EPressPhase::InputResetDuringLower)
		{
			Shooter->TryStartAttack();
		}
		if (PressPhase == EPressPhase::TapDuringLower)
		{
			Shooter->HandleAttackInputReleased();
			Shooter->TryStartAttack();
			Shooter->HandleAttackInputReleased();
		}
		if (PressPhase == EPressPhase::InputResetDuringLower)
		{
			Shooter->ResetActionInput();
		}
		Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
		TestEqual(TEXT("Local Lower can finish while the old weapon is still current"), Shooter->GetCurrentWeapon(),
			static_cast<AWeaponBase*>(Pistol));
		if (PressPhase == EPressPhase::WaitingForWeapon)
		{
			Shooter->TryStartAttack();
		}
		FinishServerTimers();
		TestEqual(TEXT("Actual Lower timer equips the destination rifle"), Shooter->GetCurrentWeapon(),
			static_cast<AWeaponBase*>(Rifle));
		Shooter->UpdateLocalProceduralWeaponSwitch(0.0f);
		if (PressPhase == EPressPhase::Raise)
		{
			Shooter->TryStartAttack();
		}

		// Raise를 먼저 끝내는 경우와 서버 타이머를 먼저 끝내는 경우를 모두 연다.
		const bool bServerFirst = PressPhase == EPressPhase::Raise || PressPhase == EPressPhase::AfterCompletion;
		if (bServerFirst)
		{
			FinishServerTimers();
		}
		else
		{
			Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
		}
		TestTrue(TEXT("A single completion boundary still blocks attack"), Shooter->IsWeaponEquipInputBlocked());
		TestEqual(TEXT("No reserved shot before both completion boundaries"), Rifle->GetCurrentAmmo(), InitialRifleAmmo);
		if (bServerFirst)
		{
			Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
		}
		else
		{
			FinishServerTimers();
		}
		TestFalse(TEXT("Equip unblocks after both boundaries"), Shooter->IsWeaponEquipInputBlocked());
		TestEqual(TEXT("The previous weapon never repeats after switch acceptance"), Pistol->GetCurrentAmmo(), PistolAmmoAfterStart);

		if (PressPhase == EPressPhase::AfterCompletion)
		{
			TestEqual(TEXT("Holding the pre-equip press never fires the new weapon"), Rifle->GetCurrentAmmo(), InitialRifleAmmo);
			Shooter->HandleAttackInputReleased();
			Shooter->TryStartAttack();
		}
		else if (PressPhase == EPressPhase::InputResetDuringLower)
		{
			TestEqual(TEXT("Focus or context reset cancels the first shot as well as Hold"), Rifle->GetCurrentAmmo(), InitialRifleAmmo);
			Shooter->HandleAttackInputReleased();
			Shooter->TryStartAttack();
		}
		const bool bHeld = PressPhase != EPressPhase::TapDuringLower;
		TestEqual(TEXT("A new press starts with exactly one shot"), Rifle->GetCurrentAmmo(), InitialRifleAmmo - 1);
		TestEqual(TEXT("Only a held new press leaves an active automatic attack"), Rifle->IsAttacking(), bHeld);
		const float Interval = AttackInterval->GetPropertyValue_InContainer(Rifle);
		TickTimers(0.0f);
		TickTimers(Interval * 0.5f);
		TestEqual(TEXT("No extra shot before the normal interval"), Rifle->GetCurrentAmmo(), InitialRifleAmmo - 1);
		TickTimers(Interval * 0.6f);
		TestEqual(TEXT("Normal automatic cadence or a single buffered tap"), Rifle->GetCurrentAmmo(),
			InitialRifleAmmo - (bHeld ? 2 : 1));
		Shooter->HandleAttackInputReleased();
		const int32 AmmoAfterRelease = Rifle->GetCurrentAmmo();
		TickTimers(Interval * 2.0f);
		TestEqual(TEXT("Release stops the new weapon's repeats"), Rifle->GetCurrentAmmo(), AmmoAfterRelease);
	}

	// 상호작용 요청을 보낸 뒤 수락 응답을 받기 전의 클릭도 지급된 무기에 연결한다.
	FFixture Pickup;
	if (!Setup(Pickup, true))
	{
		return false;
	}
	ARangedWeaponBase* GrantedRifle = World->SpawnActor<ARangedWeaponBase>(RifleClass,
		Pickup.Shooter->GetActorLocation(), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Replacement pickup fixture"), GrantedRifle))
	{
		return false;
	}
	const uint32 PickupRequest = Pickup.Shooter->BeginWeaponPickupInput(GrantedRifle);
	if (!TestTrue(TEXT("Replacement pickup records a local input boundary"), PickupRequest != 0))
	{
		return false;
	}
	Pickup.Shooter->TryStartAttack();
	Pickup.Shooter->HandleAttackInputReleased();
	Pickup.Inventory->HandleEquipWeapon(GrantedRifle, PickupRequest);
	TestEqual(TEXT("Actual inventory pickup equips the granted rifle"), Pickup.Shooter->GetCurrentWeapon(),
		static_cast<AWeaponBase*>(GrantedRifle));
	InfiniteAmmo->SetPropertyValue_InContainer(GrantedRifle, false);
	MagazineSize->SetPropertyValue_InContainer(GrantedRifle, 12);
	GrantedRifle->RestoreCheckpointAmmo(12);
	Pickup.Shooter->UpdateLocalProceduralWeaponSwitch(0.0f);
	FinishServerTimers();
	TestEqual(TEXT("The pre-ack tap cannot fire while the pickup Raise is active"), GrantedRifle->GetCurrentAmmo(), 12);
	Pickup.Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
	TestEqual(TEXT("The pre-ack tap follows the request and fires the granted rifle once"), GrantedRifle->GetCurrentAmmo(), 11);
	TestFalse(TEXT("The pickup tap leaves no active automatic attack"), GrantedRifle->IsAttacking());

	// RPC 도착 순서만 제어한다. 발사와 탄약 감소는 실제 서버 무기를 사용한다.
	FFixture Remote;
	if (!Setup(Remote, false))
	{
		return false;
	}
	AShooterCharacter* Authority = Remote.Shooter;
	const uint32 OldGeneration = Authority->GetEquipActionGeneration();
	Authority->ServerStartAttack_Implementation(Remote.Pistol, OldGeneration);
	if (!TestTrue(TEXT("Remote authority had an active attack before Equip"), Remote.Pistol->IsAttacking()))
	{
		return false;
	}
	constexpr uint32 Request = 100;
	constexpr uint32 Press = 101;
	Authority->BeginWeaponEquip(Request, Remote.Rifle);
	const uint32 Generation = Authority->GetEquipActionGeneration();
	auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Authority->GetCharacterMovement());
	Movement->ApplyActionInput(10, Movement->GetMovementInputEpoch(), false, false, 0, false, false, OldGeneration);
	Authority->ServerStopAttack_Implementation(Remote.Pistol, OldGeneration);
	Authority->AFirstPersonCharacter::EquipWeapon(Remote.Rifle);
	Authority->RefreshWeaponMode();
	const int32 RemoteAmmo = Remote.Rifle->GetCurrentAmmo();
	Authority->ServerResolveEquipFire_Implementation(Remote.Rifle, Generation, Request, Press, true);
	TestEqual(TEXT("An early Ready cannot shoot or shorten the server Equip"), Remote.Rifle->GetCurrentAmmo(), RemoteAmmo);

	Authority->FinishEquipAction(Generation);
	Authority->ServerResolveEquipFire_Implementation(Remote.Pistol, Generation, Request, Press, true);
	Authority->ServerResolveEquipFire_Implementation(Remote.Rifle, OldGeneration, Request, Press, true);
	TestEqual(TEXT("Wrong weapon and stale generation cannot consume the shot"), Remote.Rifle->GetCurrentAmmo(), RemoteAmmo);
	Authority->ServerResolveEquipFire_Implementation(Remote.Rifle, Generation, Request, Press, true);
	Authority->ServerResolveEquipFire_Implementation(Remote.Rifle, Generation, Request, Press, true);
	TestEqual(TEXT("A fresh sequenced press starts once even after stale Release was rejected"),
		Remote.Rifle->GetCurrentAmmo(), RemoteAmmo - 1);
	Authority->ServerStopAttack_Implementation(Remote.Pistol, OldGeneration);
	TestTrue(TEXT("A stale Stop cannot stop the new automatic attack"), Remote.Rifle->IsAttacking());
	Authority->ServerStopAttack_Implementation(Remote.Rifle, Generation);
	TestFalse(TEXT("A current Stop ends the remote automatic attack"), Remote.Rifle->IsAttacking());

	// 수락 직전의 Release만 있었다면 준비 통보는 발사 없이 서버의 해제 대기만 풀어야 한다.
	FFixture Released;
	if (!Setup(Released, false))
	{
		return false;
	}
	Authority = Released.Shooter;
	const uint32 BeforeReleaseGeneration = Authority->GetEquipActionGeneration();
	Authority->ServerStartAttack_Implementation(Released.Pistol, BeforeReleaseGeneration);
	if (!TestTrue(TEXT("Release-only fixture starts with an active attack"), Released.Pistol->IsAttacking()))
	{
		return false;
	}
	Authority->BeginWeaponEquip(Request, Released.Rifle);
	Authority->ServerStopAttack_Implementation(Released.Pistol, BeforeReleaseGeneration);
	Authority->AFirstPersonCharacter::EquipWeapon(Released.Rifle);
	Authority->RefreshWeaponMode();
	const uint32 ReleasedGeneration = Authority->GetEquipActionGeneration();
	Authority->FinishEquipAction(ReleasedGeneration);
	const int32 ReleasedAmmo = Released.Rifle->GetCurrentAmmo();
	Authority->ServerResolveEquipFire_Implementation(Released.Rifle, ReleasedGeneration, Request, 0, false);
	TestEqual(TEXT("Transferring only Release never fires the new weapon"), Released.Rifle->GetCurrentAmmo(), ReleasedAmmo);
	Authority->ServerStartAttack_Implementation(Released.Rifle, ReleasedGeneration);
	TestEqual(TEXT("The first ordinary Press after a transferred Release really fires"), Released.Rifle->GetCurrentAmmo(), ReleasedAmmo - 1);
	Authority->ServerStopAttack_Implementation(Released.Rifle, ReleasedGeneration);

	enum class ERejectedReservation
	{
		SemiAuto,
		Cancelled,
		Expired
	};
	for (ERejectedReservation Rejection : {
		ERejectedReservation::SemiAuto, ERejectedReservation::Cancelled, ERejectedReservation::Expired})
	{
		FFixture Cancelled;
		if (!Setup(Cancelled, false, Rejection != ERejectedReservation::SemiAuto))
		{
			return false;
		}
		Authority = Cancelled.Shooter;
		Authority->BeginWeaponEquip(Request, Cancelled.Rifle);
		const uint32 CancelledGeneration = Authority->GetEquipActionGeneration();
		Authority->AFirstPersonCharacter::EquipWeapon(Cancelled.Rifle);
		Authority->RefreshWeaponMode();
		const int32 AmmoBefore = Cancelled.Rifle->GetCurrentAmmo();
		if (Rejection == ERejectedReservation::Cancelled)
		{
			// 실패 경로는 잠금을 먼저 풀더라도 대기 발사를 정상 완료로 소비하면 안 된다.
			Authority->CancelServerEquipFire();
		}
		else if (Rejection == ERejectedReservation::Expired)
		{
			// 실제 발사 조건은 그대로 두고 도착 시각만 만료 경계로 이동한다.
			Authority->ServerEquipFire.Deadline = World->GetTimeSeconds();
		}
		Authority->FinishEquipAction(CancelledGeneration);
		Authority->ServerResolveEquipFire_Implementation(Cancelled.Rifle, CancelledGeneration, Request, Press, true);
		TestEqual(TEXT("Cancelled, expired or semi-auto destination cannot consume a delayed reservation"),
			Cancelled.Rifle->GetCurrentAmmo(), AmmoBefore);
	}

	return true;
}

#endif
