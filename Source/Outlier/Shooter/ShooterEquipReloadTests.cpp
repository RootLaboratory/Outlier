#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterEquipReloadTest,
	"Outlier.Shooter.EquipReload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipReloadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	if (!ShooterClass || !WeaponClass) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EquipReload")), GetTransientPackage());
	if (!TestNotNull(TEXT("Reload lifecycle world"), World)) return false;
	World->AddToRoot();
	Context.SetCurrentWorld(World);
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
	ARangedWeaponBase* Weapon = World->SpawnActor<ARangedWeaponBase>(WeaponClass, Spawn);
	if (!Shooter || !Weapon) return false;
	Controller = World->SpawnActor<APlayerController>();
	if (!Controller) return false;
	Controller->Possess(Shooter);
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->SetAsLocalPlayerController();
	UShooterCombatComponent* Combat = Shooter->FindComponentByClass<UShooterCombatComponent>();
	if (!Combat) return false;
	Shooter->AFirstPersonCharacter::EquipWeapon(Weapon);
	Shooter->RefreshWeaponMode();
	Shooter->CancelLocalProceduralWeaponSwitch();
	Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
	const uint32 Generation = Shooter->GetEquipActionGeneration();

	// 패킷 순서만 제어하고 취소/커밋은 실제 Combat/Weapon 경로로 검증한다.
	auto PredictReload = [&](uint32 Request)
	{
		Combat->ActiveReloadRequest = Request;
		Combat->ActiveReloadWeapon = Weapon;
		Combat->ActiveReloadEquipGeneration = Generation;
		Combat->bIsReloading = true;
		Shooter->BeginActionLock(EShooterActionLock::Reload);
	};
	PredictReload(11);
	Combat->HandleReloadResult(Weapon, 10, Generation, false);
	TestTrue(TEXT("Old rejection preserves new predicted reload"), Combat->IsReloading());
	Combat->HandleReloadResult(Weapon, 11, Generation + 1, false);
	TestTrue(TEXT("Wrong equip generation preserves current reload"), Combat->IsReloading());
	Combat->HandleReloadResult(Weapon, 11, Generation, false);
	TestFalse(TEXT("Explicit rejection clears prediction without a bool replication edge"), Combat->IsReloading());
	TestEqual(TEXT("Rejection consumes request"), Combat->ActiveReloadRequest, 0u);
	TestTrue(TEXT("Rejection releases only Reload lock"), Shooter->GetActionLock() != EShooterActionLock::Reload);

	PredictReload(12);
	Combat->HandleReloadResult(Weapon, 11, Generation, true, true);
	TestTrue(TEXT("Late completion preserves next reload"), Combat->IsReloading());
	Combat->HandleReloadResult(Weapon, 12, Generation, true, true);
	TestFalse(TEXT("Matching completion clears prediction"), Combat->IsReloading());
	TestEqual(TEXT("Completion consumes request"), Combat->ActiveReloadRequest, 0u);

	const FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
	if (!InfiniteAmmo) return false;
	InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
	Weapon->RestoreCheckpointAmmo(0);
	Weapon->BeginReload();
	TestTrue(TEXT("Real weapon begins unfinished reload"), Weapon->IsReloading());
	PredictReload(13);
	Combat->bFireInputHeld = true;
	Combat->bWantsToAim = true;
	Combat->PrepareForWeaponEquip();
	TestFalse(TEXT("Equip ends unfinished weapon reload"), Weapon->IsReloading());
	TestFalse(TEXT("Equip clears Combat reload"), Combat->IsReloading());
	TestEqual(TEXT("Equip does not commit unfinished ammo"), Weapon->GetCurrentAmmo(), 0);
	Combat->HandleReloadCommitNotify();
	TestEqual(TEXT("Late reload commit cannot refill after cleanup"), Weapon->GetCurrentAmmo(), 0);
	Combat->ApplyFireInput(true);
	Combat->ApplyAimInput(true);
	TestFalse(TEXT("Old Fire hold cannot restore after equip cleanup"), Combat->IsFireInputHeld());
	TestFalse(TEXT("Old Aim hold cannot restore after equip cleanup"), Combat->WantsToAim());
	Combat->ApplyFireInput(false);
	Combat->ApplyAimInput(false);
	Combat->ApplyFireInput(true);
	Combat->ApplyAimInput(true);
	TestFalse(TEXT("Synthetic Fire false cannot release local equip latch"), Combat->IsFireInputHeld());
	TestFalse(TEXT("Synthetic Aim false cannot release local equip latch"), Combat->WantsToAim());
	Combat->TryStopAttack();
	Combat->HandleAimReleased();
	Combat->ApplyFireInput(true);
	TestTrue(TEXT("Fresh Fire press after Release becomes valid intent"), Combat->IsFireInputHeld());
	Combat->ClearInputIntent();
	Weapon->BeginReload();
	PredictReload(14);
	Combat->HandleReloadCommitNotify();
	const int32 CommittedAmmo = Weapon->GetCurrentAmmo();
	TestTrue(TEXT("Reload commit transfers ammo before equip"), CommittedAmmo > 0);
	Combat->PrepareForWeaponEquip();
	TestEqual(TEXT("Equip preserves already committed ammo"), Weapon->GetCurrentAmmo(), CommittedAmmo);
	Weapon->RestoreCheckpointAmmo(0);

	Shooter->SetRole(ROLE_AutonomousProxy);
	ON_SCOPE_EXIT { Shooter->SetRole(ROLE_Authority); };
	// TimerManager는 프레임당 한 번만 진행한다. 등록 전 prime과 만료 검사를 서로 다른 프레임에서 실행한다.
	++GFrameCounter;
	World->GetTimerManager().Tick(0.0f);
	Combat->TryReload();
	TestTrue(TEXT("Client predicts a valid reload"), Combat->IsReloading());
	TestTrue(TEXT("Prediction owns a finite timer"), World->GetTimerManager().IsTimerActive(Combat->ReloadPredictionTimerHandle));
	++GFrameCounter;
	World->GetTimerManager().Tick(31.0f);
	TestFalse(TEXT("Missing response cannot leave predicted reload locked"), Combat->IsReloading());
	TestEqual(TEXT("Timeout consumes prediction request"), Combat->ActiveReloadRequest, 0u);
	TestEqual(TEXT("Prediction timeout never commits ammo"), Weapon->GetCurrentAmmo(), 0);

	const uint32 FirstAuto = UShooterCombatComponent::ServerAutoReloadBit | 1u;
	const uint32 SecondAuto = UShooterCombatComponent::ServerAutoReloadBit | 2u;
	Combat->HandleReloadResult(Weapon, FirstAuto, Generation, true, false);
	TestEqual(TEXT("Server auto reload has a distinct nonzero identity"), Combat->ActiveReloadRequest, FirstAuto);
	Combat->HandleReloadResult(Weapon, SecondAuto, Generation, true, false);
	Combat->HandleReloadResult(Weapon, FirstAuto, Generation, true, true);
	TestEqual(TEXT("Old auto completion preserves next auto reload"), Combat->ActiveReloadRequest, SecondAuto);
	Combat->HandleReloadResult(Weapon, FirstAuto, Generation, true, false);
	TestEqual(TEXT("Old auto start cannot replace latest auto reload"), Combat->ActiveReloadRequest, SecondAuto);
	Combat->HandleReloadResult(Weapon, 0, Generation, true, true);
	TestEqual(TEXT("Unidentified old completion cannot end auto reload"), Combat->ActiveReloadRequest, SecondAuto);
	Combat->HandleReloadResult(Weapon, SecondAuto, Generation, true, true);
	TestFalse(TEXT("Matching auto completion ends local reload"), Combat->IsReloading());
	return true;
}

#endif
