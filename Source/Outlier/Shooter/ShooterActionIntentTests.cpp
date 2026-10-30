#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterActionIntentTest,
	"Outlier.Shooter.ActionIntent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterActionIntentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	if (!TestNotNull(TEXT("Concrete Shooter Blueprint"), ShooterClass)
		|| !TestNotNull(TEXT("Concrete pistol Blueprint"), PistolClass)) return false;

	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ActionIntent"),
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Action intent test world"), World))
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

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Spawn);
	ARangedWeaponBase* Pistol = World->SpawnActor<ARangedWeaponBase>(PistolClass, Spawn);
	if (!TestNotNull(TEXT("Shooter fixture"), Shooter)
		|| !TestNotNull(TEXT("Pistol fixture"), Pistol)) return false;
	auto* Movement = Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
	auto* Input = Shooter->MovementComponent.Get();
	auto* Combat = Shooter->CombatComponent.Get();
	if (!TestNotNull(TEXT("Movement simulation"), Movement)
		|| !TestNotNull(TEXT("Movement intent"), Input)
		|| !TestNotNull(TEXT("Combat intent"), Combat)) return false;
	// 포인터만 대입하면 캐릭터의 무기 타입은 Unarmed로 남는다. 실제 장착 경로로 ADS 조건을 준비한다.
	Shooter->AFirstPersonCharacter::EquipWeapon(Pistol);
	Shooter->RefreshWeaponMode();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->Velocity = FVector(100.0f, 0.0f, 0.0f);
	Input->SetSprintIntent(false);
	if (!TestEqual(TEXT("Equipped fixture keeps the character weapon type in sync"), Shooter->GetWeaponType(), Pistol->GetWeaponType())
		|| !TestTrue(TEXT("Grounded pistol fixture allows ADS before action restrictions"), Combat->CanAimInCurrentState())) return false;

	// 실제 물리 입력과 서버/재생의 결과 상태 적용은 서로 다른 경로여야 한다.
	Input->HandleSprintPressed();
	TestTrue(TEXT("First Sprint press turns the toggle on"), Input->WantsToSprint());
	Input->HandleSprintReleased();
	TestTrue(TEXT("Ordinary release preserves the toggle"), Input->WantsToSprint());
	Input->HandleSprintPressed();
	TestFalse(TEXT("Second Sprint press turns the toggle off"), Input->WantsToSprint());
	Input->HandleSprintReleased();
	TestFalse(TEXT("Release cannot reenable Sprint"), Input->WantsToSprint());

	Movement->SetMovementMode(MOVE_Falling);
	const FVector AirVelocity = Movement->Velocity;
	const float AirSpeedLimit = Movement->MaxWalkSpeed;
	Input->HandleSprintPressed();
	Input->HandleSprintReleased();
	TestTrue(TEXT("First airborne press remembers Sprint"), Input->WantsToSprint());
	TestFalse(TEXT("Airborne intent is not actual Sprint"), Input->IsSprinting());
	TestEqual(TEXT("Airborne press does not increase the speed limit"), Movement->MaxWalkSpeed, AirSpeedLimit);
	TestTrue(TEXT("Airborne press does not inject velocity"), Movement->Velocity.Equals(AirVelocity));
	Movement->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("Landing applies buffered Sprint without another press"), Input->IsSprinting());
	TestEqual(TEXT("Landing uses the configured Sprint speed"), Movement->MaxWalkSpeed, Shooter->SprintSpeed);

	Movement->SetMovementMode(MOVE_Falling);
	Input->HandleSprintPressed();
	Input->HandleSprintReleased();
	Movement->SetMovementMode(MOVE_Walking);
	TestFalse(TEXT("Airborne second press prevents Sprint on landing"), Input->IsSprinting());

	for (bool bSprintFirst : {false, true})
	{
		Input->SetSprintIntent(false);
		if (bSprintFirst) Input->HandleSprintPressed();
		Shooter->BeginActionLock(EShooterActionLock::Equip);
		if (!bSprintFirst) Input->HandleSprintPressed();
		Input->HandleSprintReleased();
		TestTrue(TEXT("Equip and Sprint work in either input order"), Input->IsSprinting());
		TestEqual(TEXT("Equip preserves the Sprint speed limit"), Movement->MaxWalkSpeed, Shooter->SprintSpeed);
		Input->HandleSprintPressed();
		Shooter->EndActionLock(EShooterActionLock::Equip);
		TestFalse(TEXT("Equip completion does not revive a toggle turned off during Equip"), Input->WantsToSprint());
	}

	Input->SetSprintIntent(true);
	Combat->bIsReloading = true;
	Shooter->BeginActionLock(EShooterActionLock::Reload);
	Input->RefreshMovementState();
	TestTrue(TEXT("Reload preserves toggle intent"), Input->WantsToSprint());
	TestFalse(TEXT("Reload still blocks actual Sprint"), Input->IsSprinting());
	Combat->bIsReloading = false;
	Shooter->EndActionLock(EShooterActionLock::Reload);
	Input->RefreshMovementState();
	TestTrue(TEXT("Sprint resumes when the temporary restriction ends"), Input->IsSprinting());

	Shooter->BeginActionLock(EShooterActionLock::Equip);
	Combat->HandleAimPressed();
	TestTrue(TEXT("Equip accepts held Aim intent"), Combat->WantsToAim());
	TestFalse(TEXT("Equip defers actual ADS"), Combat->IsAiming());
	TestFalse(TEXT("Deferred Aim cancels Sprint intent immediately"), Input->WantsToSprint());
	Shooter->EndActionLock(EShooterActionLock::Equip);
	TestTrue(TEXT("Successful Equip unlock restores ADS without another Aim press"), Combat->IsAiming());
	Combat->HandleAimReleased();
	TestFalse(TEXT("Aim release stops ADS"), Combat->IsAiming());

	Shooter->BeginActionLock(EShooterActionLock::Equip);
	Combat->HandleAimPressed();
	Combat->HandleAimReleased();
	Shooter->EndActionLock(EShooterActionLock::Equip);
	TestFalse(TEXT("Aim released during Equip is not restored"), Combat->WantsToAim() || Combat->IsAiming());

	// 착지와 교체 완료는 별도 경계다. 먼저 끝난 쪽이 나머지 제한을 우회하면 안 된다.
	for (bool bLandFirst : {false, true})
	{
		Movement->SetMovementMode(MOVE_Falling);
		Shooter->BeginActionLock(EShooterActionLock::Equip);
		Combat->HandleAimPressed();
		if (bLandFirst) Movement->SetMovementMode(MOVE_Walking);
		else Shooter->EndActionLock(EShooterActionLock::Equip);
		TestTrue(TEXT("The first completion boundary preserves held Aim"), Combat->WantsToAim());
		TestFalse(TEXT("ADS waits for both landing and Equip completion"), Combat->IsAiming());
		if (bLandFirst) Shooter->EndActionLock(EShooterActionLock::Equip);
		else Movement->SetMovementMode(MOVE_Walking);
		TestTrue(TEXT("Both completion orders restore held Aim"), Combat->IsAiming());
		Combat->HandleAimReleased();
	}

	// 공중에서 처음 켠 Toggle은 교체 순서와 무관하게 착지만 기다린다.
	for (bool bSprintFirst : {false, true})
	for (bool bLandFirst : {false, true})
	{
		Input->SetSprintIntent(false);
		Movement->SetMovementMode(MOVE_Falling);
		const FVector BeforeInput = Movement->Velocity;
		if (bSprintFirst) Input->HandleSprintPressed();
		Shooter->BeginActionLock(EShooterActionLock::Equip);
		if (!bSprintFirst) Input->HandleSprintPressed();
		Input->HandleSprintReleased();
		TestTrue(TEXT("Airborne Equip preserves the new Sprint toggle"), Input->WantsToSprint());
		TestFalse(TEXT("Airborne Equip cannot activate Sprint"), Input->IsSprinting());
		TestTrue(TEXT("Airborne Equip and Sprint cannot inject velocity"), Movement->Velocity.Equals(BeforeInput));
		if (bLandFirst)
		{
			Movement->SetMovementMode(MOVE_Walking);
			TestTrue(TEXT("Landing starts Sprint even while Equip remains active"), Input->IsSprinting());
			Shooter->EndActionLock(EShooterActionLock::Equip);
		}
		else
		{
			Shooter->EndActionLock(EShooterActionLock::Equip);
			TestFalse(TEXT("Equip completion cannot activate airborne Sprint"), Input->IsSprinting());
			Movement->SetMovementMode(MOVE_Walking);
		}
		TestTrue(TEXT("All airborne Sprint and Equip orders finish sprinting"), Input->IsSprinting());
		TestEqual(TEXT("All completion orders use the configured Sprint limit"), Movement->MaxWalkSpeed, Shooter->SprintSpeed);
		Input->SetSprintIntent(false);
	}

	// RPC와 이동 패킷이 역순으로 도착해도, 공통 입력 순번보다 오래된 의도는 적용하지 않는다.
	const uint32 Epoch = Movement->GetMovementInputEpoch();
	TestTrue(TEXT("Newer Aim request is accepted"), Movement->ApplyActionInput(20, Epoch, false, true));
	TestFalse(TEXT("Older Sprint packet is rejected"), Movement->ApplyActionInput(19, Epoch, true, false));
	TestTrue(TEXT("Rejected Sprint packet leaves Aim intact"), Combat->WantsToAim());
	TestFalse(TEXT("Rejected Sprint packet does not start Sprint"), Input->WantsToSprint());
	TestTrue(TEXT("Newer Sprint request replaces Aim"), Movement->ApplyActionInput(21, Epoch, true, false));
	TestFalse(TEXT("Accepted Sprint clears held Aim"), Combat->WantsToAim());
	TestFalse(TEXT("Repeated action request is not consumed twice"), Movement->ApplyActionInput(21, Epoch, true, false));
	TestTrue(TEXT("Repeated request cannot invert the accepted toggle"), Input->WantsToSprint());
	TestFalse(TEXT("Older Aim RPC cannot override newer Sprint"), Movement->ApplyActionInput(20, Epoch, false, true));
	TestTrue(TEXT("Explicit Sprint OFF is accepted"), Movement->ApplyActionInput(22, Epoch, false, false));
	TestFalse(TEXT("Sprint OFF does not revive canceled Aim"), Combat->WantsToAim());
	TestTrue(TEXT("New Sprint input is accepted before a delayed server cancel"),
		Movement->ApplyActionInput(31, Epoch, true, false));
	Shooter->ServerSetSprintState_Implementation(false, 30, Epoch);
	TestTrue(TEXT("Older server Sprint cancellation cannot override a newer ON"), Input->WantsToSprint());

	Shooter->NextProceduralSwitchId = 5;
	Shooter->FailedProceduralSwitchId = 5;
	TestTrue(TEXT("Failed-switch Aim request consumes its action sequence"),
		Movement->ApplyActionInput(32, Epoch, false, true, 5));
	TestFalse(TEXT("Late Aim for the failed switch is discarded"), Combat->WantsToAim());
	TestTrue(TEXT("Older-switch Aim request also consumes its action sequence"),
		Movement->ApplyActionInput(33, Epoch, false, true, 4));
	TestFalse(TEXT("Aim from a previous switch cannot survive a newer failed switch"), Combat->WantsToAim());
	Shooter->NextProceduralSwitchId = 6;
	Shooter->BeginActionLock(EShooterActionLock::Equip);
	TestTrue(TEXT("Aim for the new switch is accepted"), Movement->ApplyActionInput(34, Epoch, false, true, 6));
	TestTrue(TEXT("New-switch Aim remains pending during Equip"), Combat->WantsToAim());
	TestFalse(TEXT("Accepted new-switch Aim does not bypass Equip"), Combat->IsAiming());
	Combat->HandleAimReleased();
	Shooter->EndActionLock(EShooterActionLock::Equip);
	Shooter->NextProceduralSwitchId = 0;
	Shooter->FailedProceduralSwitchId = 0;

	FShooterMovementInput Sent;
	Sent.Epoch = Epoch;
	Sent.ActionRequest = 23;
	Sent.bAim = true;
	Sent.AimSwitchId = 6;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Sent.Serialize(Writer);
	FMemoryReader Reader(Bytes);
	FShooterMovementInput Received;
	Received.Serialize(Reader);
	TestFalse(TEXT("Action input serialization has no archive errors"), Writer.IsError() || Reader.IsError());
	TestTrue(TEXT("Packed action intent preserves order and exclusive state"), Received == Sent);

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Local input controller"), Controller)) return false;
	Controller->Possess(Shooter);
	// 테스트 월드에서 생성한 컨트롤러는 자동으로 로컬 제어자가 되지 않는다.
	Controller->SetAsLocalPlayerController();
	if (!TestTrue(TEXT("Queued input fixture is locally controlled"), Shooter->IsLocallyControlled())) return false;
	// 위의 서버 도착 순서 fixture와 별개로 새 로컬 입력 이력을 시작한다.
	Movement->ResetSlideForLifecycle();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->Velocity = FVector(100.0f, 0.0f, 0.0f);
	Combat->HandleAimPressed();
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Local Aim press enters ADS before the double toggle"), Combat->IsAiming());
	Input->HandleSprintPressed();
	Input->HandleSprintReleased();
	Input->HandleSprintPressed();
	Input->HandleSprintReleased();
	Movement->ApplyMovementInput();
	TestFalse(TEXT("Two physical presses in one move leave Sprint off"), Input->WantsToSprint());
	TestFalse(TEXT("Intermediate Sprint ON cancels Aim even when final Sprint is OFF"), Combat->WantsToAim() || Combat->IsAiming());
	Movement->ApplyMovementInput();
	TestFalse(TEXT("Reapplying the same queued move cannot flip Sprint or restore Aim"),
		Input->WantsToSprint() || Combat->WantsToAim());

	// 서버 잠금과 소유자 Raise는 독립적으로 끝난다. 어느 쪽이 먼저 끝나도 ADS는 둘을 기다린다.
	for (bool bUnlockFirst : {false, true})
	{
		Shooter->BeginActionLock(EShooterActionLock::Equip);
		Shooter->FirstPersonSwitchVisualPhase = EFirstPersonWeaponSwitchVisualPhase::Raising;
		Shooter->FirstPersonSwitchVisualElapsed = 0.0f;
		Shooter->FirstPersonSwitchRaiseStartAlpha = 1.0f;
		Combat->HandleAimPressed();
		if (bUnlockFirst)
		{
			Shooter->EndActionLock(EShooterActionLock::Equip);
			TestFalse(TEXT("Server unlock cannot show ADS during local Raise"), Combat->IsAiming());
		}
		Shooter->UpdateLocalProceduralWeaponSwitch(Shooter->FirstPersonSwitchRaiseDuration + 0.1f);
		if (!bUnlockFirst)
		{
			TestFalse(TEXT("Local Raise completion cannot bypass server Equip lock"), Combat->IsAiming());
			Shooter->EndActionLock(EShooterActionLock::Equip);
		}
		TestTrue(TEXT("ADS restores after both completion boundaries in either order"), Combat->IsAiming());
		Combat->HandleAimReleased();
	}
	Combat->bIsAiming = true;
	Combat->OnRep_IsAiming();
	TestFalse(TEXT("Late ADS replication cannot revive a released local Aim"), Combat->IsAiming());

	Shooter->BeginActionLock(EShooterActionLock::Equip);
	const uint32 OldEquipGeneration = Shooter->EquipActionGeneration;
	Shooter->BeginActionLock(EShooterActionLock::Equip);
	const uint32 NewEquipGeneration = Shooter->EquipActionGeneration;
	Shooter->FinishEquipAction(OldEquipGeneration);
	TestEqual(TEXT("Old Equip completion cannot unlock a newer Equip"), Shooter->GetActionLock(), EShooterActionLock::Equip);
	Shooter->FinishEquipAction(NewEquipGeneration);
	TestEqual(TEXT("Matching Equip completion releases its own lock"), Shooter->GetActionLock(), EShooterActionLock::None);

	// 바닥 위 실제 이동으로 정지→출발→정지를 통과한다. 직접 상태 갱신 호출로 Run 전환을 대신하지 않는다.
	AActor* Floor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Run transition floor actor"), Floor)) return false;
	UBoxComponent* FloorBox = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBox);
	FloorBox->SetBoxExtent(FVector(10000.0, 10000.0, 50.0));
	FloorBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBox->SetCollisionObjectType(ECC_WorldStatic);
	FloorBox->SetMobility(EComponentMobility::Static);
	FloorBox->SetWorldLocation(FVector(0.0, 0.0, -50.0));
	FloorBox->RegisterComponent();
	Pistol->SetActorEnableCollision(false);
	Movement->ResetSlideForLifecycle();
	Shooter->SetActorLocation(FVector(0.0, 0.0, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15));
	Movement->SetMovementMode(MOVE_Walking);
	Movement->Velocity = FVector::ZeroVector;
	Movement->bForceNextFloorCheck = true;
	Movement->PerformMovement(1.0f / 60.0f);
	if (!TestTrue(TEXT("Run transition fixture has a real walkable floor"), Movement->CurrentFloor.IsWalkableFloor())) return false;
	Input->HandleSprintPressed();
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Stationary toggle ON keeps Sprint intent"), Input->WantsToSprint());
	TestFalse(TEXT("Stationary toggle ON does not show actual Run"), Input->IsSprinting());
	Movement->MoveAutonomous(3.0f, 1.0f / 60.0f, 0, FVector(Movement->GetMaxAcceleration(), 0.0, 0.0));
	TestTrue(TEXT("Movement simulation accelerates from rest"), Movement->Velocity.SizeSquared2D() > 0.0f);
	TestTrue(TEXT("Actual movement starts Run without another toggle or direct refresh"), Input->IsSprinting());
	for (int32 Index = 0; Index < 120 && !Movement->Velocity.IsNearlyZero(); ++Index)
	{
		Movement->MoveAutonomous(3.05f + Index / 60.0f, 1.0f / 60.0f, 0, FVector::ZeroVector);
	}
	TestTrue(TEXT("Existing walking braking brings the fixture to rest"), Movement->Velocity.IsNearlyZero());
	TestFalse(TEXT("Actual simulation stop clears Run without another input edge"), Input->IsSprinting());
	TestTrue(TEXT("Simulation stop preserves the Sprint toggle for the next movement"), Input->WantsToSprint());

	const uint32 PreviousEpoch = Movement->GetMovementInputEpoch();
	Movement->ResetSlideForLifecycle();
	TestFalse(TEXT("Previous lifecycle request cannot start Sprint"), Movement->ApplyActionInput(24, PreviousEpoch, true, false));
	TestFalse(TEXT("Lifecycle cleanup leaves Sprint off"), Input->WantsToSprint());
	return true;
}

#endif
