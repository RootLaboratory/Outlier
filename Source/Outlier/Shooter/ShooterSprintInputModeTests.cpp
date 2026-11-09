#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterPlayerController.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "OutlierPlayerState.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Weapon/RangedWeaponBase.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSprintInputModeTest,
	"Outlier.Shooter.SprintInputMode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSprintInputModeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	if (!TestNotNull(TEXT("Concrete Shooter Blueprint"), ShooterClass)
		|| !TestNotNull(TEXT("Concrete pistol Blueprint"), PistolClass)) return false;

	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SprintInputMode"),
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Sprint input test world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
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
	ARangedWeaponBase* Pistol = World->SpawnActor<ARangedWeaponBase>(PistolClass, Spawn);
	Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Shooter fixture"), Shooter)
		|| !TestNotNull(TEXT("Pistol fixture"), Pistol)
		|| !TestNotNull(TEXT("Local controller fixture"), Controller)) return false;
	Controller->Possess(Shooter);
	// NetDriver가 없는 테스트 월드도 실제 LocalPlayer 연결로 로컬 입력 경로를 사용한다.
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->SetAsLocalPlayerController();
	if (!TestTrue(TEXT("Physical input fixture is locally controlled"), Shooter->IsLocallyControlled())) return false;

	auto* Movement = Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
	auto* Input = Shooter->MovementComponent.Get();
	auto* Combat = Shooter->CombatComponent.Get();
	if (!TestNotNull(TEXT("Movement simulation"), Movement)
		|| !TestNotNull(TEXT("Movement intent"), Input)
		|| !TestNotNull(TEXT("Combat intent"), Combat)) return false;
	Shooter->AFirstPersonCharacter::EquipWeapon(Pistol);
	Shooter->RefreshWeaponMode();

	const auto Reset = [&](EOutlierSprintInputMode Mode)
	{
		Combat->ClearInputIntent();
		Combat->bIsReloading = false;
		Shooter->EndActionLock(EShooterActionLock::Reload);
		Shooter->EndActionLock(EShooterActionLock::Equip);
		Movement->ResetSlideForLifecycle();
		Movement->SetSprintInputMode(Mode);
		Movement->SetMovementMode(MOVE_Walking);
		Movement->Velocity = FVector(100.0f, 0.0f, 0.0f);
		// 컨트롤러의 실제 키 이벤트와 Enhanced Input의 Started를 구분한다.
		Movement->NotifyFreshSprintPress();
	};
	const auto Press = [&]()
	{
		Input->HandleSprintPressed();
		Movement->ApplyMovementInput();
	};
	const auto Release = [&]()
	{
		Input->HandleSprintReleased();
		Movement->ApplyMovementInput();
	};

	Reset(EOutlierSprintInputMode::Toggle);
	Press();
	TestTrue(TEXT("Toggle Press enables Sprint"), Input->IsSprinting());
	const uint32 FirstPressRequest = Movement->GetActionInputRequest();
	Press();
	TestEqual(TEXT("Repeated Started while held does not create another request"),
		Movement->GetActionInputRequest(), FirstPressRequest);
	TestTrue(TEXT("Repeated Started cannot turn the Toggle off"), Input->WantsToSprint());
	Release();
	TestTrue(TEXT("Toggle Release preserves Sprint"), Input->WantsToSprint());
	Press();
	TestFalse(TEXT("A second Toggle Press disables Sprint"), Input->WantsToSprint());
	Release();

	Reset(EOutlierSprintInputMode::Hold);
	Press();
	TestTrue(TEXT("Hold Press enables ground Sprint"), Input->IsSprinting());
	TestTrue(TEXT("Hold policy is attached to the accepted movement input"), Movement->IsHoldSprintInput());
	Release();
	TestFalse(TEXT("Hold Release clears Sprint intent"), Input->WantsToSprint());
	TestFalse(TEXT("Hold Release stops actual Sprint"), Input->IsSprinting());

	for (bool bReleaseBeforeLanding : {false, true})
	{
		Reset(EOutlierSprintInputMode::Hold);
		Movement->SetMovementMode(MOVE_Falling);
		const FVector AirVelocity = Movement->Velocity;
		const float AirSpeedLimit = Movement->MaxWalkSpeed;
		Press();
		TestTrue(TEXT("First airborne Hold Press stores intent"), Input->WantsToSprint());
		TestFalse(TEXT("Airborne Hold does not activate actual Sprint"), Input->IsSprinting());
		TestEqual(TEXT("Airborne Hold preserves the air speed limit"), Movement->MaxWalkSpeed, AirSpeedLimit);
		TestTrue(TEXT("Airborne Hold does not inject velocity"), Movement->Velocity.Equals(AirVelocity));
		if (bReleaseBeforeLanding) Release();
		Movement->SetMovementMode(MOVE_Walking);
		TestEqual(TEXT("Landing uses the remaining Hold intent"), Input->IsSprinting(), !bReleaseBeforeLanding);
		Release();
	}

	Reset(EOutlierSprintInputMode::Hold);
	Press();
	Combat->HandleAimPressed();
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Aim suspends Hold without consuming Sprint intent"), Input->WantsToSprint());
	TestFalse(TEXT("Aim has priority over actual Hold Sprint"), Input->IsSprinting());
	TestTrue(TEXT("Aim can enter ADS while Sprint is held"), Combat->IsAiming());
	Input->RefreshMovementState();
	TestTrue(TEXT("Hold reevaluation does not cancel the held Aim"), Combat->WantsToAim());
	Combat->HandleAimReleased();
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Aim Release resumes the still-held Sprint"), Input->IsSprinting());
	Release();

	// 같은 이동에 묶인 Hold Press도 뒤따르는 전투 입력보다 먼저 정책이 확정되어야 한다.
	Reset(EOutlierSprintInputMode::Hold);
	Input->HandleSprintPressed();
	Combat->HandleAimPressed();
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Queued Hold Press survives Aim before movement consumption"), Input->WantsToSprint());
	TestTrue(TEXT("Aim enters ADS when queued after Hold in the same move"), Combat->IsAiming());
	TestFalse(TEXT("Queued Hold cannot bypass same-move Aim"), Input->IsSprinting());
	Combat->HandleAimReleased();
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Same-move Aim Release resumes queued Hold"), Input->IsSprinting());
	Release();

	Reset(EOutlierSprintInputMode::Hold);
	Input->HandleSprintPressed();
	Combat->UpdateFireInput(true);
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Queued Hold Press survives Fire before movement consumption"), Input->WantsToSprint());
	TestTrue(TEXT("Same-move Fire preserves the physical held restriction"), Combat->IsFireInputHeld());
	TestFalse(TEXT("Queued Hold cannot bypass same-move Fire"), Input->IsSprinting());
	Combat->UpdateFireInput(false);
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Same-move Fire Release resumes queued Hold"), Input->IsSprinting());
	Release();

	for (bool bReleaseDuringReload : {false, true})
	{
		Reset(EOutlierSprintInputMode::Hold);
		Press();
		Combat->bIsReloading = true;
		Shooter->BeginActionLock(EShooterActionLock::Reload);
		Input->RefreshMovementState();
		TestTrue(TEXT("Reload preserves Hold Sprint intent"), Input->WantsToSprint());
		TestFalse(TEXT("Reload suspends actual Hold Sprint"), Input->IsSprinting());
		if (bReleaseDuringReload) Release();
		Combat->FinishReloadInternal();
		TestEqual(TEXT("Reload completion respects a Release during the restriction"),
			Input->IsSprinting(), !bReleaseDuringReload);
		Release();
	}

	// Reload가 끝나도 Aim이 남아 있으면 제한 전체가 해소된 것이 아니다.
	Reset(EOutlierSprintInputMode::Hold);
	Press();
	Combat->bIsReloading = true;
	Shooter->BeginActionLock(EShooterActionLock::Reload);
	Combat->HandleAimPressed();
	Movement->ApplyMovementInput();
	Combat->FinishReloadInternal();
	TestTrue(TEXT("Reload-to-Aim keeps held Sprint intent"), Input->WantsToSprint());
	TestFalse(TEXT("Reload completion cannot bypass the remaining Aim restriction"), Input->IsSprinting());
	TestTrue(TEXT("Reload completion restores the held Aim"), Combat->IsAiming());
	Combat->HandleAimReleased();
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Sprint resumes after the final restriction ends"), Input->IsSprinting());
	Release();

	// 연사 간격에 무기가 Idle이어도 발사 키를 누른 동안은 Hold Sprint를 재개하지 않는다.
	Reset(EOutlierSprintInputMode::Hold);
	Press();
	Movement->RecordFireInput(true);
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Fire preserves Hold Sprint intent"), Input->WantsToSprint());
	TestTrue(TEXT("Recorded fire input preserves the physical held state"), Combat->IsFireInputHeld());
	TestFalse(TEXT("Held Fire blocks Sprint between shots"), Input->IsSprinting());
	Input->RefreshMovementState();
	TestFalse(TEXT("An idle weapon cannot resume Sprint while Fire is held"), Input->IsSprinting());
	Movement->RecordFireInput(false);
	Movement->ApplyMovementInput();
	TestTrue(TEXT("Fire Release resumes held Sprint after the attack ends"), Input->IsSprinting());
	Release();

	Reset(EOutlierSprintInputMode::Toggle);
	Press();
	Movement->RecordFireInput(true);
	Movement->ApplyMovementInput();
	TestFalse(TEXT("Fire still consumes Toggle Sprint intent"), Input->WantsToSprint());
	Movement->RecordFireInput(false);
	Movement->ApplyMovementInput();
	TestFalse(TEXT("Fire Release cannot revive consumed Toggle Sprint"), Input->WantsToSprint());
	Release();

	for (bool bSprintFirst : {false, true})
	{
		Reset(EOutlierSprintInputMode::Hold);
		if (bSprintFirst) Press();
		Shooter->BeginActionLock(EShooterActionLock::Equip);
		if (!bSprintFirst) Press();
		TestTrue(TEXT("Hold Sprint and Equip work in both input orders"), Input->IsSprinting());
		Release();
		TestFalse(TEXT("Release during Equip clears Hold intent"), Input->WantsToSprint());
		Shooter->EndActionLock(EShooterActionLock::Equip);
		TestFalse(TEXT("Equip completion cannot revive released Hold"), Input->IsSprinting());
	}

	for (EOutlierSprintInputMode NewMode : {EOutlierSprintInputMode::Toggle, EOutlierSprintInputMode::Hold})
	{
		Reset(NewMode == EOutlierSprintInputMode::Hold
			? EOutlierSprintInputMode::Toggle : EOutlierSprintInputMode::Hold);
		Press();
		Movement->SetSprintInputMode(NewMode);
		Movement->ApplyMovementInput();
		TestFalse(TEXT("A mode change clears the previous Sprint intent"), Input->WantsToSprint());
		Press();
		TestFalse(TEXT("Synthetic Started cannot resume Sprint after a mode change"), Input->WantsToSprint());
		Release();
		Press();
		TestFalse(TEXT("Synthetic Release and Started cannot satisfy the physical rearm boundary"), Input->WantsToSprint());
		Movement->NotifyFreshSprintPress();
		Press();
		TestTrue(TEXT("A genuine new Press uses the changed Sprint mode"), Input->WantsToSprint());
		Release();
		TestEqual(TEXT("Changed mode defines the new Release behavior"), Input->WantsToSprint(),
			NewMode == EOutlierSprintInputMode::Toggle);
	}

	FShooterMovementInput Sent;
	Sent.Epoch = Movement->GetMovementInputEpoch();
	Sent.ActionRequest = Movement->GetActionInputRequest() + 10;
	Sent.bSprint = true;
	Sent.bHoldSprint = true;
	Sent.bFire = true;
	Sent.bAim = true;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Sent.Serialize(Writer);
	FMemoryReader Reader(Bytes);
	FShooterMovementInput Received;
	Received.Serialize(Reader);
	TestFalse(TEXT("Hold input serialization has no archive errors"), Writer.IsError() || Reader.IsError());
	TestTrue(TEXT("Packed Hold input preserves simultaneous suspended intents"), Received == Sent);
	FShooterSlideSnapshot SentSnapshot;
	SentSnapshot.AppliedInput = Sent;
	SentSnapshot.bSprintHeld = true;
	SentSnapshot.bHoldSprint = true;
	SentSnapshot.bFireHeld = true;
	SentSnapshot.bAimHeld = true;
	TArray<uint8> SnapshotBytes;
	FMemoryWriter SnapshotWriter(SnapshotBytes);
	SentSnapshot.Serialize(SnapshotWriter);
	FMemoryReader SnapshotReader(SnapshotBytes);
	FShooterSlideSnapshot ReceivedSnapshot;
	ReceivedSnapshot.Serialize(SnapshotReader);
	TestFalse(TEXT("Hold correction serialization has no archive errors"),
		SnapshotWriter.IsError() || SnapshotReader.IsError());
	TestTrue(TEXT("Correction preserves the applied Hold move"), ReceivedSnapshot.AppliedInput == Sent);
	TestTrue(TEXT("Correction preserves suspended Hold, Aim, and Fire"),
		ReceivedSnapshot.bSprintHeld && ReceivedSnapshot.bHoldSprint
		&& ReceivedSnapshot.bAimHeld && ReceivedSnapshot.bFireHeld);
	ReceivedSnapshot.ProcessedActionRequest = Sent.ActionRequest;
	Movement->RestoreSlideSnapshot(ReceivedSnapshot);
	TestTrue(TEXT("Correction restores Hold policy and Sprint intent"),
		Movement->IsHoldSprintInput() && Input->WantsToSprint());
	TestTrue(TEXT("Correction restores the physical Fire restriction"), Combat->IsFireInputHeld());
	TestFalse(TEXT("Correction cannot resume Sprint through restored combat restrictions"), Input->IsSprinting());

	// 과거 이동은 현재 Toggle 설정을 다시 읽지 않고 저장된 Hold 결과로 판정한다.
	Reset(EOutlierSprintInputMode::Toggle);
	const uint32 Epoch = Movement->GetMovementInputEpoch();
	uint32 Request = Movement->GetActionInputRequest() + 10;
	TestTrue(TEXT("Saved Hold policy is accepted under a current Toggle preference"),
		Movement->ApplyActionInput(Request++, Epoch, true, true, 0, true));
	TestTrue(TEXT("Saved Hold input preserves Sprint behind Aim"), Input->WantsToSprint());
	TestFalse(TEXT("Saved Hold input cannot bypass Aim"), Input->IsSprinting());
	TestTrue(TEXT("Saved Aim Release is accepted"), Movement->ApplyActionInput(Request++, Epoch, true, false, 0, true));
	TestTrue(TEXT("Saved Hold input resumes from the recorded result"), Input->IsSprinting());
	TestTrue(TEXT("Saved Hold Release is accepted"), Movement->ApplyActionInput(Request++, Epoch, false, false, 0, true));
	TestFalse(TEXT("Saved Hold Release stops Sprint under the current Toggle preference"), Input->WantsToSprint());
	TestFalse(TEXT("Older Hold ON cannot override the recorded Release"),
		Movement->ApplyActionInput(Request - 2, Epoch, true, false, 0, true));
	TestFalse(TEXT("Rejected old Hold input cannot revive Sprint"), Input->WantsToSprint());
	Combat->ApplyFireInput(true);
	const uint32 PreviousEpoch = Epoch;
	Movement->ResetSlideForLifecycle();
	TestFalse(TEXT("Lifecycle cleanup clears the physical Fire restriction"), Combat->IsFireInputHeld());
	TestFalse(TEXT("Lifecycle correction cannot advertise an old held Fire"), Movement->CaptureSlideSnapshot().bFireHeld);
	TestFalse(TEXT("Previous lifecycle Hold input is rejected"),
		Movement->ApplyActionInput(Request++, PreviousEpoch, true, false, 0, true));
	TestFalse(TEXT("Lifecycle cleanup cannot restore the old Hold intent"), Input->WantsToSprint());
	// 설정 변경 알림부터 Controller의 실제 Press/Flush까지 연결한다.
	// 사용자 설정은 소유 LocalPlayer 없이 생성해 실제 세이브 파일에 기록하지 않는다.
	UClass* ControllerClass = LoadClass<AShooterPlayerController>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterPlayerController.BP_ShooterPlayerController_C"));
	if (!TestNotNull(TEXT("Concrete Shooter controller Blueprint"), ControllerClass)) return false;
	AShooterPlayerController* InputController = World->SpawnActor<AShooterPlayerController>(ControllerClass, Spawn);
	AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Input controller"), InputController) || !TestNotNull(TEXT("Input PlayerState"), State)) return false;
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	ULocalPlayerSettingsSubsystem* Settings = NewObject<ULocalPlayerSettingsSubsystem>(LocalPlayer);
	UOutlierInputUserSettings* SavedSettings = NewObject<UOutlierInputUserSettings>();
	// Exercise an actual Toggle -> Hold change independently of the new-user default.
	SavedSettings->SetSprintInputMode(EOutlierPlayerRole::Shooter, EOutlierSprintInputMode::Toggle);
	Settings->InputUserSettings = SavedSettings;
	LocalPlayer->PlayerController = InputController;
	InputController->Player = LocalPlayer;
	InputController->PlayerState = State;
	InputController->SetPawn(Shooter);
	Shooter->Controller = InputController;
	InputController->SetAsLocalPlayerController();
	ON_SCOPE_EXIT
	{
		Shooter->UnbindSprintSettings();
		Settings->UnbindInputSettingsPlayerState();
		InputController->SetPawn(nullptr);
		InputController->Player = nullptr;
		LocalPlayer->PlayerController = nullptr;
		Shooter->Controller = Controller;
	};
	State->SetPlayerRole(EOutlierPlayerRole::Shooter);
	Settings->RefreshInputSettingsContext();
	Shooter->SprintSettingsSubsystem = Settings;
	Settings->OnSprintInputModeChanged.AddUniqueDynamic(Shooter, &AShooterCharacter::HandleSprintInputModeChanged);
	Settings->OnInputSettingsContextChanged.AddUObject(Shooter, &AShooterCharacter::HandleSprintSettingsContextChanged);
	Shooter->SprintInputKeys = { EKeys::LeftShift };
	const auto Key = [&](FKey KeyName, EInputEvent Event)
	{
		FInputKeyEventArgs Args;
		Args.Key = KeyName;
		Args.Event = Event;
		InputController->InputKey(Args);
	};
	const auto PhysicalPress = [&]()
	{
		Key(EKeys::LeftShift, IE_Pressed);
		Press();
	};
	Reset(EOutlierSprintInputMode::Toggle);
	// 이 월드는 ActorsInitialized 전이므로 AActor::ProcessEvent가 동적 델리게이트를 차단한다.
	// 통합 구간에서만 실제 UFUNCTION 호출을 허용하고, 종료 시 에디터 실행 정책을 복원한다.
	TGuardValue<bool> AllowSettingsActorCallbacks(GAllowActorScriptExecutionInEditor, true);
	if (!TestTrue(TEXT("Settings integration Pawn is locally controlled"), Shooter->IsLocallyControlled())
		|| !TestTrue(TEXT("Settings integration retains its Shooter listener"),
			Settings->OnSprintInputModeChanged.IsAlreadyBound(Shooter, &AShooterCharacter::HandleSprintInputModeChanged))) return false;
	// 3회 실제 방식 변경만 SaveSettings를 호출한다. ownerless 설정은 경고 후 쓰기를 건너뛴다.
	AddExpectedMessagePlain(TEXT("Attempting to save Enhanced Input User settings without an owning local player!"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
	TestTrue(TEXT("Subsystem changes current role to Hold"), Settings->SetSprintInputMode(EOutlierPlayerRole::Shooter, EOutlierSprintInputMode::Hold));
	TestEqual(TEXT("Settings delegate applies Hold to the controlled Pawn"), Movement->GetSprintInputMode(), EOutlierSprintInputMode::Hold);
	Press();
	TestFalse(TEXT("Settings notification cannot synthesize a new Sprint Press"), Input->WantsToSprint());
	PhysicalPress();
	TestTrue(TEXT("Controller physical Press starts configured Hold"), Input->IsSprinting());
	TestTrue(TEXT("Inactive role setting can change independently"), Settings->SetSprintInputMode(EOutlierPlayerRole::Partner, EOutlierSprintInputMode::Hold));
	TestTrue(TEXT("Inactive role notification preserves current Sprint"), Input->IsSprinting());
	InputController->FlushPressedKeys();
	TestFalse(TEXT("Controller Flush clears Sprint immediately"), Input->WantsToSprint());
	Release();
	Key(EKeys::SpaceBar, IE_Pressed);
	Press();
	TestFalse(TEXT("Unrelated key and synthetic Started cannot rearm after Flush"), Input->WantsToSprint());
	Key(EKeys::LeftShift, IE_Released);
	Release();
	PhysicalPress();
	TestTrue(TEXT("Physical release and press recover Hold after Flush"), Input->IsSprinting());
	TestTrue(TEXT("Subsystem switches the current role to Toggle"), Settings->SetSprintInputMode(EOutlierPlayerRole::Shooter, EOutlierSprintInputMode::Toggle));
	TestFalse(TEXT("Changing current role mode immediately stops Sprint"), Input->WantsToSprint());
	Release();
	Press();
	TestFalse(TEXT("Mode change requires a physical Press, not action callbacks"), Input->WantsToSprint());
	Key(EKeys::LeftShift, IE_Released);
	Release();
	PhysicalPress();
	Release();
	TestTrue(TEXT("Configured Toggle persists after Release"), Input->WantsToSprint());
	InputController->FlushPressedKeys();
	Press();
	TestFalse(TEXT("Toggle also rejects synthetic Started after Controller Flush"), Input->WantsToSprint());
	Key(EKeys::LeftShift, IE_Repeat);
	Press();
	TestFalse(TEXT("Key repeat cannot rearm Toggle after Controller Flush"), Input->WantsToSprint());
	Key(EKeys::LeftShift, IE_Released);
	Release();
	PhysicalPress();
	TestTrue(TEXT("Fresh physical Press recovers Toggle after Controller Flush"), Input->IsSprinting());
	State->SetPlayerRole(EOutlierPlayerRole::Partner);
	TestFalse(TEXT("Actual role notification clears the previous Pawn intent"), Input->WantsToSprint());
	State->SetPlayerRole(EOutlierPlayerRole::Shooter);
	Press();
	TestFalse(TEXT("Returning to Shooter cannot reuse the previous key hold"), Input->WantsToSprint());
	// 재구독은 기존 바인딩을 정리한 뒤 연결한다. 중복 구독으로 초기화가 누적되지 않는다.
	Shooter->UnbindSprintSettings();
	Shooter->SprintSettingsSubsystem = Settings;
	Settings->OnSprintInputModeChanged.AddUniqueDynamic(Shooter, &AShooterCharacter::HandleSprintInputModeChanged);
	Settings->OnSprintInputModeChanged.AddUniqueDynamic(Shooter, &AShooterCharacter::HandleSprintInputModeChanged);
	TestEqual(TEXT("Repeated subscription keeps one Shooter settings listener"),
		Settings->OnSprintInputModeChanged.GetAllObjects().Num(), 1);
	SavedSettings->SetSprintInputMode(EOutlierPlayerRole::Shooter, EOutlierSprintInputMode::Hold);
	Shooter->HandleSprintSettingsLoaded(SavedSettings);
	TestEqual(TEXT("Loaded settings apply the saved mode without delegate ordering assumptions"), Movement->GetSprintInputMode(), EOutlierSprintInputMode::Hold);
	Press();
	TestFalse(TEXT("Loading a different mode does not restore the previous held input"), Input->WantsToSprint());
	TArray<uint8> SettingsBytes;
	TestTrue(TEXT("Integrated role settings serialize in memory"), UGameplayStatics::SaveGameToMemory(SavedSettings, SettingsBytes));
	const auto* ReloadedSettings = Cast<UOutlierInputUserSettings>(UGameplayStatics::LoadGameFromMemory(SettingsBytes));
	if (TestNotNull(TEXT("Integrated role settings reload"), ReloadedSettings))
	{
		TestEqual(TEXT("Shooter preference survives integrated notifications"), ReloadedSettings->GetSprintInputMode(EOutlierPlayerRole::Shooter), EOutlierSprintInputMode::Hold);
		TestEqual(TEXT("Partner preference remains independent"), ReloadedSettings->GetSprintInputMode(EOutlierPlayerRole::Partner), EOutlierSprintInputMode::Hold);
	}
	TestEqual(TEXT("Another local user's settings remain independent"), NewObject<UOutlierInputUserSettings>()->GetSprintInputMode(EOutlierPlayerRole::Partner), EOutlierSprintInputMode::Toggle);
	return true;
}

#endif
