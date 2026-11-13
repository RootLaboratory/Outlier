#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterEquipLifecycleTest,
	"Outlier.Shooter.Equip.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!TestNotNull(TEXT("Shooter Blueprint"), ShooterClass)
		|| !TestNotNull(TEXT("Pistol Blueprint"), PistolClass)
		|| !TestNotNull(TEXT("Rifle Blueprint"), RifleClass)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EquipLifecycle")), GetTransientPackage());
	if (!TestNotNull(TEXT("Lifecycle world"), World)) return false;
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	APlayerController* Controller = nullptr;
	ON_SCOPE_EXIT
	{
		if (Controller) Controller->Player = nullptr;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Spawn);
	ARangedWeaponBase* Pistol = World->SpawnActor<ARangedWeaponBase>(PistolClass, Spawn);
	ARangedWeaponBase* Rifle = World->SpawnActor<ARangedWeaponBase>(RifleClass, Spawn);
	if (!Shooter || !Pistol || !Rifle) return false;
	auto* Inventory = Shooter->GetInventoryComponent();
	auto* Combat = Shooter->CombatComponent.Get();
	auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
	Inventory->BeginPlay();
	auto TickTimers = [World](float Delta)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(Delta);
	};
	Inventory->HandleEquipWeapon(Pistol);
	if (!TestEqual(TEXT("Pickup equips actual pistol"), Shooter->GetCurrentWeapon(), static_cast<AWeaponBase*>(Pistol))) return false;
	TickTimers(0.0f);
	TickTimers(3.0f);
	TestEqual(TEXT("Pickup finishes through its own timer"), Shooter->GetActionLock(), EShooterActionLock::None);

	// 미완료 Reload를 준비해 실제 pickup 경로가 탄약을 채우지 않고 취소하는지 검사한다.
	const auto* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
	if (!TestNotNull(TEXT("Finite ammo fixture property"), InfiniteAmmo)) return false;
	InfiniteAmmo->SetPropertyValue_InContainer(Pistol, false);
	// 1발 탄창도 실제 미완료 Reload를 시작하도록 현재 탄창보다 한 발 적게 준비한다.
	Pistol->RestoreCheckpointAmmo(FMath::Max(0, Pistol->GetMagazineSize() - 1));
	Combat->TryReload();
	if (!TestTrue(TEXT("Previous weapon has a real unfinished Reload"), Pistol->IsReloading())) return false;
	const int32 BeforeAmmo = Pistol->GetCurrentAmmo();
	const uint32 BeforeGeneration = Shooter->GetEquipActionGeneration();
	Inventory->SelectWeaponSlot(EWeaponSlot::Secondary);
	Inventory->SelectWeaponSlot(EWeaponSlot::Primary);
	TestTrue(TEXT("Same and empty slots preserve the previous Reload"), Pistol->IsReloading());
	TestEqual(TEXT("Rejected slots preserve generation"), Shooter->GetEquipActionGeneration(), BeforeGeneration);
	Inventory->HandleEquipWeapon(Rifle);
	TestEqual(TEXT("Pickup interrupts Reload and equips rifle"), Shooter->GetCurrentWeapon(), static_cast<AWeaponBase*>(Rifle));
	TestFalse(TEXT("Previous unfinished Reload is canceled"), Pistol->IsReloading());
	TestEqual(TEXT("Canceled Reload does not grant ammo"), Pistol->GetCurrentAmmo(), BeforeAmmo);
	TestEqual(TEXT("Accepted pickup advances exactly one equip generation"), Shooter->GetEquipActionGeneration(), BeforeGeneration + 1);
	TickTimers(0.0f);
	TickTimers(3.0f);
	TestEqual(TEXT("Interrupted Reload cannot strand Equip"), Shooter->GetActionLock(), EShooterActionLock::None);

	Inventory->SelectWeaponSlot(EWeaponSlot::Secondary);
	TestEqual(TEXT("Slot starts Lower before changing the weapon"), Shooter->GetCurrentWeapon(), static_cast<AWeaponBase*>(Rifle));
	Inventory->FinishPendingWeaponSwitch(Shooter->NextProceduralSwitchId);
	TestEqual(TEXT("Early owner acknowledgement cannot bypass server Lower"), Shooter->GetCurrentWeapon(), static_cast<AWeaponBase*>(Rifle));
	TickTimers(0.0f);
	TickTimers(1.0f);
	TestEqual(TEXT("Server changes the slot without owner acknowledgement"), Shooter->GetCurrentWeapon(), static_cast<AWeaponBase*>(Pistol));
	TickTimers(0.0f);
	TickTimers(3.0f);
	TestEqual(TEXT("Server completes Raise without owner acknowledgement"), Shooter->GetActionLock(), EShooterActionLock::None);

	const uint32 Generation = Shooter->GetEquipActionGeneration();
	const uint32 Epoch = Movement->GetMovementInputEpoch();
	Movement->ApplyActionInput(100, Epoch, true, true, 0, true, true, Generation - 1);
	TestFalse(TEXT("Previous-generation movement cannot revive old actions"),
		Shooter->MovementComponent->WantsToSprint() || Combat->WantsToAim() || Combat->IsFireInputHeld());
	Movement->ApplyActionInput(101, Epoch, false, false, 0, false, false, Generation);
	Movement->ApplyActionInput(102, Epoch, true, false, 0, true, false, Generation);
	TestTrue(TEXT("New generation still accepts fresh movement input"), Shooter->MovementComponent->WantsToSprint());

	// 잘못된 Raise 설정에서도 실제 슬롯 교체의 서버 완료 타이머가 사라지지 않는다.
	for (float InvalidRaise : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()})
	{
		Shooter->FirstPersonSwitchRaiseDuration = InvalidRaise;
		Inventory->SelectWeaponSlot(Shooter->GetCurrentWeapon() == Pistol ? EWeaponSlot::Primary : EWeaponSlot::Secondary);
		TickTimers(0.0f);
		TickTimers(1.0f);
		TickTimers(0.0f);
		TickTimers(3.0f);
		TestEqual(TEXT("Invalid Raise duration has a finite authoritative completion"), Shooter->GetActionLock(), EShooterActionLock::None);
	}
	Shooter->FirstPersonSwitchRaiseDuration = 0.22f;
	Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Owning controller"), Controller)) return false;
	Controller->Possess(Shooter);
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->SetAsLocalPlayerController();
	for (bool bServerFirst : {false, true})
	{
		ARangedWeaponBase* Pickup = World->SpawnActor<ARangedWeaponBase>(PistolClass, Spawn);
		Inventory->HandleEquipWeapon(Pickup);
		const uint32 ActiveGeneration = Shooter->GetEquipActionGeneration();
		Shooter->ClientFinishWeaponEquip_Implementation(ActiveGeneration - 1);
		TestTrue(TEXT("Stale completion cannot release the current owning-client barrier"), Shooter->IsWeaponEquipInputBlocked());
		Shooter->UpdateLocalProceduralWeaponSwitch(0.0f);
		if (bServerFirst)
		{
			TickTimers(0.0f);
			TickTimers(3.0f);
			TestTrue(TEXT("Server completion still waits for local Raise"), Shooter->IsWeaponEquipInputBlocked());
		}
		Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
		if (!bServerFirst)
		{
			TestTrue(TEXT("Local Raise completion still waits for the server"), Shooter->IsWeaponEquipInputBlocked());
			TickTimers(0.0f);
			TickTimers(3.0f);
		}
		TestFalse(TEXT("Both completion boundaries release actions in either order"), Shooter->IsWeaponEquipInputBlocked());
	}

	// CurrentWeapon 복제가 시작 RPC보다 먼저 온 순서를 재현한다. 이전 무기는 RPC의 참조를 따른다.
	AWeaponBase* PreviousWeapon = Shooter->GetCurrentWeapon();
	Shooter->AFirstPersonCharacter::EquipWeapon(Rifle);
	Shooter->RefreshWeaponMode();
	Shooter->ClientBeginProceduralWeaponSwitch_Implementation(999, PreviousWeapon, Shooter->GetEquipActionGeneration());
	TestEqual(TEXT("Start RPC retains the previous weapon despite earlier replication"), Shooter->WeaponAtSwitchStart.Get(), PreviousWeapon);
	Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
	Shooter->UpdateLocalProceduralWeaponSwitch(0.0f);
	TestEqual(TEXT("Already replicated weapon proceeds directly into Raise"), Shooter->FirstPersonSwitchVisualPhase,
		EFirstPersonWeaponSwitchVisualPhase::Raising);
	Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
	TestFalse(TEXT("Earlier weapon replication cannot strand local switching"), Shooter->IsWeaponSwitchInProgress());
	return true;
}

#endif
