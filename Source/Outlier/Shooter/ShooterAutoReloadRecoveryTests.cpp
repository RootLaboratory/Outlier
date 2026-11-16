#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAutoReloadRecoveryTest,
	"Outlier.Shooter.AutoReloadRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAutoReloadRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!TestNotNull(TEXT("Concrete Shooter"), ShooterClass)
		|| !TestNotNull(TEXT("Concrete rifle"), RifleClass))
	{
		return false;
	}

	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("AutoReloadRecovery")), GetTransientPackage());
	if (!TestNotNull(TEXT("Recovery world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	// 실제 RPC/몽타주 경로를 사용하되 무관한 게임 시작 타이머는 실행하지 않는다.
	World->InitializeActorsForPlay(FURL());
	APlayerController* Controller = nullptr;
	ON_SCOPE_EXIT
	{
		if (Controller)
		{
			Controller->Player = nullptr;
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Spawn);
	ARangedWeaponBase* Weapon = World->SpawnActor<ARangedWeaponBase>(RifleClass, Spawn);
	Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Shooter"), Shooter) || !TestNotNull(TEXT("Weapon"), Weapon)
		|| !TestNotNull(TEXT("Controller"), Controller))
	{
		return false;
	}
	Controller->Possess(Shooter);
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->SetAsLocalPlayerController();
	UShooterCombatComponent* Combat = Shooter->FindComponentByClass<UShooterCombatComponent>();
	UShooterInventoryComponent* Inventory = Shooter->GetInventoryComponent();
	if (!TestNotNull(TEXT("Combat"), Combat) || !TestNotNull(TEXT("Inventory"), Inventory)
		|| !TestTrue(TEXT("Rifle presentation"), Shooter->SetSuitPresentation(true)))
	{
		return false;
	}
	Inventory->BeginPlay();
	Shooter->AFirstPersonCharacter::EquipWeapon(Weapon);
	Shooter->RefreshWeaponMode();
	Shooter->CancelLocalProceduralWeaponSwitch();
	Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
	const FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
	if (!TestNotNull(TEXT("Finite ammo setting"), InfiniteAmmo)
		|| !TestTrue(TEXT("Rifle supports partial magazines"), Weapon->GetMagazineSize() > 1))
	{
		return false;
	}
	InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
	UAnimMontage* Montage = Shooter->GetActionMontage(EShooterMontageAction::Reload, false);
	UAnimInstance* AnimInstance = Shooter->GetMesh()->GetAnimInstance();
	if (!TestNotNull(TEXT("Actual reload montage"), Montage) || !TestNotNull(TEXT("Actual AnimBP"), AnimInstance))
	{
		return false;
	}

	auto TickTimers = [World]()
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(0.01f);
	};
	auto ResetFixture = [&](int32 Ammo)
	{
		Combat->CancelReloadInternal();
		Shooter->EndActionLock(Shooter->GetActionLock());
		Combat->ResetAutoReloadRecovery();
		Combat->ClearInputIntent();
		Weapon->RestoreCheckpointAmmo(Ammo);
		Shooter->RefreshCombatState();
	};
	auto CheckStarted = [&](const TCHAR* Message)
	{
		return TestTrue(Message, Combat->IsReloading() && Weapon->IsReloading()
			&& AnimInstance->Montage_IsPlaying(Montage));
	};
	TickTimers();

	// 마지막 탄의 기존 즉시 재장전은 유지한다. 복구 추가가 정상 발사 경로를 늦추지 않는다.
	ResetFixture(1);
	Weapon->StartAttack();
	TestEqual(TEXT("Actual last shot consumes the final round"), Weapon->GetCurrentAmmo(), 0);
	if (!CheckStarted(TEXT("Actual last shot still begins immediate reload")))
	{
		return false;
	}

	// 빈 탄창 공격과 중복 완료 알림은 다음 프레임에 하나의 실제 Reload만 시작한다.
	ResetFixture(0);
	Combat->TryStartAttack();
	TestEqual(TEXT("Empty input does not consume ammunition"), Weapon->GetCurrentAmmo(), 0);
	TestFalse(TEXT("Empty input cannot fire"), Weapon->IsAttacking());
	TestFalse(TEXT("Recovery is deferred past caller cleanup"), Combat->IsReloading());
	Shooter->RefreshActionIntentsAfterEquip();
	TickTimers();
	if (!CheckStarted(TEXT("Empty attack starts authoritative reload")))
	{
		return false;
	}
	const uint32 Request = Combat->ActiveReloadRequest;
	Combat->RequestAutoReloadRecovery();
	TickTimers();
	TestEqual(TEXT("Duplicate recovery preserves the active request"), Combat->ActiveReloadRequest, Request);
	Combat->HandleReloadCommitNotify();
	Combat->FinishReloadInternal();
	TestEqual(TEXT("Only authoritative commit fills the magazine"), Weapon->GetCurrentAmmo(), Weapon->GetMagazineSize());
	TestFalse(TEXT("Successful commit clears recovery state"), Combat->bAutoReloadRecoveryPending);

	// 요청이 잠금에 막혀도 폴링하지 않고 완료 경계에서 다시 평가한다.
	ResetFixture(0);
	Shooter->BeginActionLock(EShooterActionLock::Equip);
	Combat->HandleAutoReloadRequested();
	TickTimers();
	TestTrue(TEXT("Equip keeps an empty reload pending"), Combat->bAutoReloadRecoveryPending);
	TestFalse(TEXT("Blocked recovery does not start reload"), Combat->IsReloading());
	TestFalse(TEXT("Blocked recovery does not poll"), World->GetTimerManager().TimerExists(Combat->AutoReloadRecoveryTimerHandle));
	Shooter->EndActionLock(EShooterActionLock::Equip);
	TickTimers();
	if (!CheckStarted(TEXT("Equip completion recovers the empty magazine")))
	{
		return false;
	}

	// 충전 전 중단은 한 번 복구하고, 같은 세대에서 반복 중단되면 재시작 고리를 끊는다.
	ResetFixture(0);
	Combat->HandleAutoReloadRequested();
	Combat->HandleReloadMontageEnded(Montage, true);
	TestEqual(TEXT("Interruption cannot grant ammunition"), Weapon->GetCurrentAmmo(), 0);
	TickTimers();
	if (!CheckStarted(TEXT("Interrupted empty reload recovers")))
	{
		return false;
	}
	Combat->HandleReloadMontageEnded(Montage, true);
	TickTimers();
	TestFalse(TEXT("Repeated interruption stops automatic restart"), Combat->IsReloading());
	TestTrue(TEXT("Repeated interruption is blocked for this equip generation"), Combat->bAutoReloadRecoveryBlocked);
	Combat->TryReload();
	if (!CheckStarted(TEXT("Explicit manual reload remains available after recovery failure")))
	{
		return false;
	}

	ResetFixture(1);
	Combat->TryReload();
	Combat->HandleReloadMontageEnded(Montage, true);
	TickTimers();
	TestFalse(TEXT("Interrupted partial manual reload stays canceled"), Combat->IsReloading());
	TestEqual(TEXT("Partial magazine is preserved"), Weapon->GetCurrentAmmo(), 1);

	// 세대가 바뀐 뒤 이전 예약을 전달해도 새 장착을 시작하거나 잠금을 변경하지 않는다.
	ResetFixture(0);
	Combat->RequestAutoReloadRecovery();
	Shooter->BeginActionLock(EShooterActionLock::Equip);
	TickTimers();
	TestFalse(TEXT("Old-generation recovery is discarded"), Combat->bAutoReloadRecoveryPending);
	TestFalse(TEXT("Old-generation recovery cannot reload"), Combat->IsReloading());
	TestEqual(TEXT("Old recovery preserves current Equip lock"), Shooter->GetActionLock(), EShooterActionLock::Equip);

	// Pawn이 먼저 복원되고 나중에 Possess되는 순서에서도 시작 전에는 탄약을 보존한다.
	ResetFixture(0);
	Controller->UnPossess();
	Combat->RequestAutoReloadRecovery();
	TickTimers();
	TestFalse(TEXT("Unpossessed pawn cannot start recovery"), Combat->IsReloading());
	TestTrue(TEXT("Unpossessed pawn retains recovery until possession"), Combat->bAutoReloadRecoveryPending);
	Controller->Possess(Shooter);
	TickTimers();
	if (!CheckStarted(TEXT("Possession wakes the restored empty magazine")))
	{
		return false;
	}

	// 재생 실패는 실제 AnimInstance 부재로 만들고, 같은 세대의 반복 요청을 검증한다.
	ResetFixture(0);
	TSubclassOf<UAnimInstance> SavedAnimClass = Shooter->GetMesh()->GetAnimClass();
	Shooter->GetMesh()->SetAnimInstanceClass(nullptr);
	Combat->HandleAutoReloadRequested();
	TestFalse(TEXT("Missing authoritative animation cancels reload"), Combat->IsReloading());
	TestTrue(TEXT("Missing animation blocks automatic retries"), Combat->bAutoReloadRecoveryBlocked);
	const uint32 FailedSerial = Combat->ReloadActionSerial;
	Combat->RequestAutoReloadRecovery();
	TickTimers();
	TestEqual(TEXT("Bad configuration cannot spin reload attempts"), Combat->ReloadActionSerial, FailedSerial);
	TestEqual(TEXT("Missing animation cannot fill ammo"), Weapon->GetCurrentAmmo(), 0);
	Shooter->GetMesh()->SetAnimInstanceClass(SavedAnimClass);
	AnimInstance = Shooter->GetMesh()->GetAnimInstance();

	ResetFixture(0);
	Shooter->SetRole(ROLE_AutonomousProxy);
	Combat->RequestAutoReloadRecovery();
	TestFalse(TEXT("Client cannot schedule authoritative recovery"), Combat->bAutoReloadRecoveryPending);

	// 서버 승인/완료가 로컬 Raise보다 먼저 도착해도 잃거나 늦게 되살리지 않는다.
	Shooter->BeginActionLock(EShooterActionLock::Equip);
	Shooter->bAwaitingServerEquipCompletion = true;
	Shooter->FirstPersonSwitchVisualPhase = EFirstPersonWeaponSwitchVisualPhase::Raising;
	const uint32 ClientGeneration = Shooter->GetEquipActionGeneration();
	const uint32 ClientAutoRequest = UShooterCombatComponent::ServerAutoReloadBit | 99u;
	Combat->HandleReloadResult(Weapon, ClientAutoRequest, ClientGeneration, true, false);
	TestTrue(TEXT("Server approval waits for owner Equip and Raise"), Combat->bDeferredAutoReloadPresentation);
	TestEqual(TEXT("Deferred approval preserves Equip lock"), Shooter->GetActionLock(), EShooterActionLock::Equip);
	Shooter->ClientFinishWeaponEquip_Implementation(ClientGeneration);
	TestTrue(TEXT("Server Equip completion still waits for owner Raise"), Combat->bDeferredAutoReloadPresentation);
	Shooter->FirstPersonSwitchVisualPhase = EFirstPersonWeaponSwitchVisualPhase::None;
	Shooter->RefreshActionIntentsAfterEquip();
	TestFalse(TEXT("Owner Raise completion consumes deferred approval"), Combat->bDeferredAutoReloadPresentation);
	TestTrue(TEXT("Owner displays the approved reload"), Shooter->GetFirstPersonMesh()->GetAnimInstance()->Montage_IsPlaying(
		Shooter->GetActionMontage(EShooterMontageAction::Reload, true)));
	TestEqual(TEXT("Owner recovery cannot commit ammunition"), Weapon->GetCurrentAmmo(), 0);
	Combat->HandleReloadResult(Weapon, ClientAutoRequest, ClientGeneration, true, true);
	Shooter->FirstPersonSwitchVisualPhase = EFirstPersonWeaponSwitchVisualPhase::Raising;
	Combat->HandleReloadResult(Weapon, ClientAutoRequest + 1, ClientGeneration, true, false);
	Combat->HandleReloadResult(Weapon, ClientAutoRequest + 1, ClientGeneration, true, true);
	Shooter->FirstPersonSwitchVisualPhase = EFirstPersonWeaponSwitchVisualPhase::None;
	Shooter->RefreshActionIntentsAfterEquip();
	TestFalse(TEXT("Completion before Raise cannot resurrect a finished reload"), Combat->IsReloading());
	TestFalse(TEXT("Completion clears deferred owner presentation"), Combat->bDeferredAutoReloadPresentation);
	Shooter->SetRole(ROLE_Authority);

	// 저장된 0발은 전체 Loadout 복원 뒤에만 재장전한다. 수납 슬롯도 0발로 두어
	// 복원 중 임시 장착된 무기가 함께 충전되는 회귀를 검사한다.
	ResetFixture(0);
	Combat->HandleAutoReloadRequested();
	ARangedWeaponBase* BeforeRestore = Weapon;
	TestTrue(TEXT("Same-pawn restore starts with an unfinished reload"), Weapon->IsReloading());
	FOutlierLoadoutSnapshot Snapshot;
	Snapshot.SlotSnapshots.SetNum(static_cast<int32>(EWeaponSlot::Max));
	Snapshot.CurrentSlot = EWeaponSlot::Primary;
	Snapshot.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].WeaponClass = RifleClass;
	Snapshot.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].CurrentAmmo = 0;
	Snapshot.SlotSnapshots[static_cast<int32>(EWeaponSlot::Secondary)].WeaponClass = RifleClass;
	Snapshot.SlotSnapshots[static_cast<int32>(EWeaponSlot::Secondary)].CurrentAmmo = 0;
	Inventory->RestoreLoadout(Snapshot, true);
	TestFalse(TEXT("Same-pawn restore cancels previous weapon reload"), BeforeRestore->IsReloading());
	Weapon = Cast<ARangedWeaponBase>(Shooter->GetCurrentWeapon());
	if (!TestNotNull(TEXT("Restored current weapon"), Weapon))
	{
		return false;
	}
	TestEqual(TEXT("Restore preserves saved zero ammo before recovery"), Weapon->GetCurrentAmmo(), 0);
	TickTimers();
	TestTrue(TEXT("Completed restore starts current weapon reload"), Weapon->IsReloading());
	Combat->HandleReloadCommitNotify();
	Combat->FinishReloadInternal();
	FOutlierLoadoutSnapshot AfterRestore;
	Inventory->BuildLoadoutSnapshot(AfterRestore, true);
	TestEqual(TEXT("Current restored magazine is filled by commit"), Weapon->GetCurrentAmmo(), Weapon->GetMagazineSize());
	TestEqual(TEXT("Stowed restored magazine remains empty"),
		AfterRestore.SlotSnapshots[static_cast<int32>(EWeaponSlot::Secondary)].CurrentAmmo, 0);

	// 장착 요청은 기존 빈 탄창 Reload를 취소한다. 실제 슬롯 교체로 복귀할 때 재시작한다.
	ARangedWeaponBase* RestoredWeapon = Weapon;
	Weapon->RestoreCheckpointAmmo(0);
	Combat->HandleAutoReloadRequested();
	Inventory->SelectWeaponSlot(EWeaponSlot::Secondary);
	TestFalse(TEXT("Switch cancels previous unfinished reload"), RestoredWeapon->IsReloading());
	TestEqual(TEXT("Switch cannot commit old empty magazine"), RestoredWeapon->GetCurrentAmmo(), 0);
	// Lower/Equip 완료는 실제 타이머로 진행한다. 애니메이션 Tick은 충전과 분리한다.
	for (int32 Step = 0; Step < 3; ++Step)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(3.0f);
		Shooter->UpdateLocalProceduralWeaponSwitch(3.0f);
	}
	Inventory->SelectWeaponSlot(EWeaponSlot::Primary);
	for (int32 Step = 0; Step < 3; ++Step)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(3.0f);
		Shooter->UpdateLocalProceduralWeaponSwitch(3.0f);
	}
	TickTimers();
	TestEqual(TEXT("Actual slot switch returns to previous weapon"), Shooter->GetCurrentWeapon(), static_cast<AWeaponBase*>(RestoredWeapon));
	TestTrue(TEXT("Returning to empty weapon restarts reload"), RestoredWeapon->IsReloading());
	TestEqual(TEXT("Recovery start alone cannot commit ammunition"), RestoredWeapon->GetCurrentAmmo(), 0);
	return true;
}

#endif
