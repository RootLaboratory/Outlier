#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerCombatComponent.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Data/OutlierVitalityDataRow.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "OutlierPlayerState.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "TimerManager.h"
#include "Tutorial/Actors/Tuto_Pistol.h"
#include "Weapon/RangedWeaponBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerRebootInputTest,
	"Outlier.Partner.RebootInputRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerRebootInputTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PartnerRebootInputTest")));
	if (!TestNotNull(TEXT("Reboot input test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	APartnerPlayerController* Controller = World->SpawnActor<APartnerPlayerController>();
	AOutlierPlayerState* PlayerState = World->SpawnActor<AOutlierPlayerState>();
	ON_SCOPE_EXIT
	{
		if (Partner)
		{
			Partner->GetPartnerVitalityComponent()->BeginOwnerTeardown();
			Partner->Controller = nullptr;
		}
		if (Controller)
		{
			Controller->SetPawn(nullptr);
			Controller->Player = nullptr;
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	if (!TestNotNull(TEXT("Native Partner exists"), Partner)
		|| !TestNotNull(TEXT("Partner controller exists"), Controller)
		|| !TestNotNull(TEXT("Partner PlayerState exists"), PlayerState)) return false;

	// 실제 입력/생존 콜백을 사용하되 BeginPlay의 프로덕션 DataTable과 에셋 초기화는 실행하지 않는다.
	Partner->Controller = Controller;
	Controller->SetPawn(Partner);
	Controller->SetPlayerState(PlayerState);
	Partner->SetPlayerState(PlayerState);
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Partner->AccelerateInputKeys = { EKeys::LeftShift };
	TestTrue(TEXT("Fixture is locally controlled"), Partner->IsLocallyControlled());
	UOutlierAbilitySystemComponent* AbilitySystem = Partner->GetOutlierAbilitySystemComponent();
	AbilitySystem->InitializeForPawn(Partner);
	UPartnerCombatComponent* Combat = Partner->FindComponentByClass<UPartnerCombatComponent>();
	if (!TestNotNull(TEXT("Partner combat component exists"), Combat)) return false;
	Combat->RefreshCharacterRefsFromPlayerState();
	UPartnerVitalityComponent* Vitality = Partner->GetPartnerVitalityComponent();

	UDataTable* VitalityTable = NewObject<UDataTable>();
	VitalityTable->RowStruct = FOutlierVitalityDataRow::StaticStruct();
	FOutlierVitalityDataRow VitalityRow;
	VitalityRow.MaxHealth = 120.0f;
	VitalityTable->AddRow(TEXT("Partner"), VitalityRow);
	UDataTable* SurvivalTable = NewObject<UDataTable>();
	SurvivalTable->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
	FPartnerSurvivalDataRow SurvivalRow;
	SurvivalRow.RebootTime = 0.25f;
	SurvivalTable->AddRow(TEXT("Default"), SurvivalRow);
	FDataTableRowHandle VitalityHandle;
	VitalityHandle.DataTable = VitalityTable;
	VitalityHandle.RowName = TEXT("Partner");
	FDataTableRowHandle SurvivalHandle;
	SurvivalHandle.DataTable = SurvivalTable;
	SurvivalHandle.RowName = TEXT("Default");
	if (!TestTrue(TEXT("Native vitality initializes"),
		Vitality->InitializeFromDataTables(VitalityHandle, SurvivalHandle))) return false;

	const auto Tick = [&](float Duration)
	{
		for (float Elapsed = 0.0f; Elapsed < Duration; Elapsed += 0.02f)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.02f);
		}
	};
	const auto PressBoost = [&]()
	{
		Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0),
			EKeys::LeftShift, IE_Pressed, uint64(0)));
		Partner->ToggleAccelerate();
	};
	const auto ReleaseBoost = [&]()
	{
		Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0),
			EKeys::LeftShift, IE_Released, uint64(0)));
		Partner->ReleaseAccelerate();
	};
	const auto EnterReboot = [&]()
	{
		const uint32 Epoch = Partner->GetActionInputEpoch();
		TestTrue(TEXT("Lethal damage starts the real reboot lifecycle"),
			AbilitySystem->ApplyDamageToSelf(Partner->GetVitalAttributeSet()->GetHealth(), nullptr,
				Partner, OutlierGameplayTags::Damage::Weapon()));
		TestTrue(TEXT("Reboot effect is active"), Vitality->IsRebooting());
		TestFalse(TEXT("Reboot stops actual boost"), Partner->bIsAccelerate != 0);
		TestEqual(TEXT("Reboot preserves the current input session"), Partner->GetActionInputEpoch(), Epoch);
	};
	const auto CompleteReboot = [&]()
	{
		TestTrue(TEXT("Exact effect removal completes reboot"), Vitality->RemoveRebootEffect());
		TestFalse(TEXT("Reboot no longer blocks execution"), Vitality->IsRebooting());
	};

	Partner->AccelerateInputMode = EOutlierSprintInputMode::Hold;
	Partner->ResetAccelerateInput();
	PressBoost();
	EnterReboot();
	CompleteReboot();
	TestTrue(TEXT("A held boost resumes without another Press"), Partner->bIsAccelerate != 0);
	ReleaseBoost();
	PressBoost();
	EnterReboot();
	Tick(0.35f);
	TestFalse(TEXT("Natural effect expiration completes reboot"), Vitality->IsRebooting());
	TestTrue(TEXT("Natural reboot completion also resumes held boost"), Partner->bIsAccelerate != 0);
	ReleaseBoost();
	EnterReboot();
	PressBoost();
	TestFalse(TEXT("New Press during reboot cannot execute boost"), Partner->bIsAccelerate != 0);
	CompleteReboot();
	TestTrue(TEXT("New Press held through completion resumes boost"), Partner->bIsAccelerate != 0);
	EnterReboot();
	ReleaseBoost();
	CompleteReboot();
	TestFalse(TEXT("Release during reboot cancels resumed boost"), Partner->bIsAccelerate != 0);
	EnterReboot();
	PressBoost();
	ReleaseBoost();
	CompleteReboot();
	TestFalse(TEXT("Press and Release during reboot never queue a boost"), Partner->bIsAccelerate != 0);

	Partner->AccelerateInputMode = EOutlierSprintInputMode::Toggle;
	Partner->ResetAccelerateInput();
	PressBoost();
	ReleaseBoost();
	EnterReboot();
	CompleteReboot();
	TestTrue(TEXT("Toggle ON intent survives reboot without a physical hold"), Partner->bIsAccelerate != 0);
	EnterReboot();
	PressBoost();
	ReleaseBoost();
	CompleteReboot();
	TestFalse(TEXT("Toggle OFF during reboot is retained"), Partner->bIsAccelerate != 0);

	Partner->AccelerateInputMode = EOutlierSprintInputMode::Hold;
	Partner->ResetAccelerateInput();
	PressBoost();
	EnterReboot();
	const uint32 OldEpoch = Partner->GetActionInputEpoch();
	Controller->FlushPressedKeys();
	Partner->ServerSetAccelerate_Implementation(true, 100, OldEpoch);
	CompleteReboot();
	TestFalse(TEXT("Focus Flush and stale ON cannot resume old boost"), Partner->bIsAccelerate != 0);
	Partner->ToggleAccelerate();
	TestFalse(TEXT("Synthetic Started after Flush cannot rearm boost"), Partner->bIsAccelerate != 0);
	ReleaseBoost();

	// RangedWeaponBase는 abstract다. 공격 구현을 그대로 상속하는 구체 native 권총을 사용한다.
	ARangedWeaponBase* Weapon = World->SpawnActor<ATuto_Pistol>();
	if (!TestNotNull(TEXT("Native ranged weapon exists"), Weapon)) return false;
	// 실제 서버 발사/탄약 경로를 쓰되 에셋 장착과 이펙트 설정은 생략한다.
	// 아래 세 발은 총구 에셋이 없어 FP 연출 진단을 남긴다. 이 진단만 예상하고,
	// 상태/탄약 검증 실패와 다른 오류는 그대로 테스트 실패로 처리한다.
	AddExpectedError(TEXT("[PartnerWeaponVFX][FP] Failed: no valid muzzle transform."),
		EAutomationExpectedErrorFlags::Contains, 3, false);
	Weapon->WeaponOwner = Partner;
	Weapon->bIsEquipped = true;
	Weapon->MagazineSize = 5;
	Weapon->CurrentAmmo = 0;
	Weapon->bIsAutomatic = false;
	Weapon->BurstShotCount = 0;
	Partner->CurrentWeapon = Weapon;
	Combat->ReloadDurationSeconds = 0.5f;
	Combat->StartAutoReload();
	TestTrue(TEXT("Empty magazine starts a reload before reboot"), Weapon->IsReloading());
	EnterReboot();
	TestFalse(TEXT("Reboot clears the interrupted reload lock"), Weapon->IsReloading());
	CompleteReboot();
	TestEqual(TEXT("Reboot immediately fills an empty magazine"), Weapon->GetCurrentAmmo(), 5);
	TestFalse(TEXT("Reboot completion leaves no reload timer"), World->GetTimerManager().IsTimerActive(Combat->ReloadTimerHandle));
	Weapon->CurrentAmmo = 3;
	Tick(0.6f);
	TestEqual(TEXT("Cancelled old reload cannot refill after reboot completion"), Weapon->GetCurrentAmmo(), 3);

	// 시작 전 홀드와 재부팅 도중의 최신 해제를 분리해서 확인한다.
	EnterReboot();
	Combat->AllowAttackInput();
	Combat->TryStartAttack();
	TestTrue(TEXT("Attack Press during reboot records held intent"), Combat->IsAttackInputHeld());
	TestFalse(TEXT("Attack Press during reboot does not fire"), Weapon->IsAttacking());
	CompleteReboot();
	TestEqual(TEXT("Completion refills then resumes exactly one semi-auto shot"), Weapon->GetCurrentAmmo(), 4);
	Tick(0.2f);
	TestEqual(TEXT("Held semi-auto input does not repeatedly restart accepted shots"), Weapon->GetCurrentAmmo(), 4);
	TestFalse(TEXT("A duplicate completion cannot remove an already consumed reboot effect"), Vitality->RemoveRebootEffect());
	Partner->FinishActionsAfterReboot();
	TestEqual(TEXT("Duplicate completion does not refill or fire again"), Weapon->GetCurrentAmmo(), 4);
	EnterReboot();
	Combat->TryStopAttack();
	CompleteReboot();
	TestEqual(TEXT("Release during reboot fills without firing"), Weapon->GetCurrentAmmo(), 5);
	TestFalse(TEXT("Release during reboot leaves no held attack"), Combat->IsAttackInputHeld());

	EnterReboot();
	Combat->AllowAttackInput();
	Combat->TryStartAttack();
	Combat->TryStopAttack();
	CompleteReboot();
	Tick(0.1f);
	TestEqual(TEXT("Press and Release during reboot never queue a later shot"), Weapon->GetCurrentAmmo(), 5);
	EnterReboot();
	Combat->AllowAttackInput();
	Combat->TryStartAttack();
	const uint32 OldAttackEpoch = Partner->GetActionInputEpoch();
	Controller->FlushPressedKeys();
	Combat->ServerSetAttackHeld_Implementation(true, 100, OldAttackEpoch);
	CompleteReboot();
	TestFalse(TEXT("Focus Flush and an old-session ON cannot retain attack input"), Combat->IsAttackInputHeld());
	TestEqual(TEXT("Focus loss prevents an old held attack from resuming"), Weapon->GetCurrentAmmo(), 5);
	Combat->TryStartAttack();
	TestFalse(TEXT("Synthetic attack Started after Flush cannot rearm attack"), Combat->IsAttackInputHeld());
	// 동일 세대 안에서도 최신 OFF보다 오래된 ON은 다시 행동을 시작할 수 없다.
	const uint32 AttackEpoch = Partner->GetActionInputEpoch();
	Combat->ServerSetAttackHeld_Implementation(false, 101, AttackEpoch);
	Combat->ResetAttackInput();
	Combat->ServerSetAttackHeld_Implementation(true, 100, AttackEpoch);
	TestFalse(TEXT("An older ON cannot override a newer OFF in the same session"), Combat->IsAttackInputHeld());
	Partner->ResetAccelerateInput();

	Weapon->bInfiniteAmmo = true;
	Weapon->CurrentAmmo = 2;
	EnterReboot();
	CompleteReboot();
	TestTrue(TEXT("Reboot preserves infinite ammo policy"), Weapon->bInfiniteAmmo != 0);
	TestEqual(TEXT("Reboot also refills the current infinite-ammo magazine"), Weapon->GetCurrentAmmo(), 5);
	Weapon->bInfiniteAmmo = false;

	// 발사 간격이 완료 시점에 남아 있어도 홀드 입력은 만료 후 한 번 재개한다.
	EnterReboot();
	Combat->AllowAttackInput();
	Combat->TryStartAttack();
	Weapon->PreservedAttackReadyAt = World->GetTimeSeconds() + 0.15f;
	CompleteReboot();
	TestEqual(TEXT("A remaining attack interval delays resumed fire"), Weapon->GetCurrentAmmo(), 5);
	Tick(0.3f);
	TestEqual(TEXT("Pending held input retries after the preserved interval"), Weapon->GetCurrentAmmo(), 4);
	Combat->TryStopAttack();

	// 재개 대기 중 다시 재부팅되면 이전 발사 예약은 취소되어야 한다.
	// 두 번째 재부팅에서 놓은 입력을 첫 번째 복구 타이머가 되살리지 않는지 확인한다.
	EnterReboot();
	Combat->AllowAttackInput();
	Combat->TryStartAttack();
	Weapon->PreservedAttackReadyAt = World->GetTimeSeconds() + 0.15f;
	CompleteReboot();
	TestEqual(TEXT("First reboot leaves a cooldown-delayed attack pending"), Weapon->GetCurrentAmmo(), 5);
	EnterReboot();
	Combat->TryStopAttack();
	CompleteReboot();
	Tick(0.3f);
	TestEqual(TEXT("Consecutive reboot and release cannot revive the previous pending shot"), Weapon->GetCurrentAmmo(), 5);
	TestFalse(TEXT("Consecutive reboot preserves the latest released attack intent"), Combat->IsAttackInputHeld());

	Partner->ResetAccelerateInput();
	const int32 AmmoBeforeExplicitRequest = Weapon->GetCurrentAmmo();
	Partner->StartWeaponAttack();
	TestEqual(TEXT("Explicit Ability/BP attack remains usable without a physical key Press"),
		Weapon->GetCurrentAmmo(), AmmoBeforeExplicitRequest - 1);
	Partner->StopWeaponAttack();
	return true;
}

#endif
