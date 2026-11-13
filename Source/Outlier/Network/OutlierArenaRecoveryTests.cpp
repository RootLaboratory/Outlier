#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierArenaSettings.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Shooter/ShooterCharacter.h"

namespace
{
struct FScopedArenaRecoveryTestWorld
{
	UWorld* World = nullptr;

	~FScopedArenaRecoveryTestWorld()
	{
		if (!World) return;
		World->EndPlay(EEndPlayReason::LevelTransition);
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaRecoveryWatchdogTest,
	"Outlier.Network.SingleArena.RecoveryWatchdog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaRecoveryWatchdogTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("Engine is available"), GEngine)) return false;
	FScopedArenaRecoveryTestWorld Fixture;
	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
		NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	Fixture.World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	UWorld* World = Fixture.World;
	if (!TestNotNull(TEXT("Isolated recovery world exists"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	AOutlierGameMode* Mode = ModeClass ? World->SpawnActor<AOutlierGameMode>(ModeClass) : nullptr;
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	APlayerController* First = World->SpawnActor<APlayerController>();
	APlayerController* Second = World->SpawnActor<APlayerController>();
	APlayerController* OldController = World->SpawnActor<APlayerController>();
	APlayerState* FirstState = World->SpawnActor<APlayerState>();
	APlayerState* SecondState = World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("Production GameMode exists"), Mode)
		|| !TestNotNull(TEXT("Arena subsystem exists"), Arena)
		|| !TestNotNull(TEXT("First controller exists"), First)
		|| !TestNotNull(TEXT("Second controller exists"), Second)
		|| !TestNotNull(TEXT("Old controller exists"), OldController)
		|| !TestNotNull(TEXT("First player state exists"), FirstState)
		|| !TestNotNull(TEXT("Second player state exists"), SecondState)) return false;

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const double StallLimit = FMath::Max(double(Settings->ArenaGameplayReloadStallSeconds), 1.0);
	const double TotalLimit = FMath::Max(double(Settings->ArenaGameplayReloadFailureSeconds), 1.0);
	const uint32 Generation = 41;
	int32 Failures = 0;
	EOutlierGameplayReloadFailure LastFailure = EOutlierGameplayReloadFailure::None;
	// 스트리밍 I/O와 화면 이동만 격리한다. 감시/재시도/실패 판정은 실제 서버 함수를 호출한다.
	const FDelegateHandle FailureHandle = Arena->OnArenaGameplayReloadFailed.AddLambda(
		[&](uint32 FailedGeneration, EOutlierGameplayReloadFailure Reason)
		{
			TestEqual(TEXT("Failure retains the active generation"), FailedGeneration, Generation);
			++Failures;
			LastFailure = Reason;
		});
	auto BeginReload = [&]()
	{
		const double Now = FPlatformTime::Seconds();
		Arena->PendingGameplayReload.Emplace();
		Arena->PendingGameplayReload->Generation = Generation;
		Arena->PendingGameplayReload->Phase = EOutlierGameplayReloadPhase::WaitingForClientAcks;
		Arena->PendingGameplayReload->bIsStalled = true;
		Mode->bArenaReloadInProgress = true;
		Mode->bServerArenaReloadReady = false;
		Mode->PendingGameplayGeneration = Generation;
		Mode->LastObservedReloadPhase = EOutlierGameplayReloadPhase::WaitingForClientAcks;
		Mode->ArenaReloadStartedAt = Now;
		Mode->ArenaReloadLastProgressAt = Now - StallLimit - 1.0;
		Mode->ArenaReloadLastRecoveryAt = Now - StallLimit - 1.0;
		Mode->ArenaReloadRecoveryAttempts = 0;
	};
	BeginReload();
	const double StartedAt = Mode->ArenaReloadStartedAt;
	const double ProgressAt = Mode->ArenaReloadLastProgressAt;

	First->PlayerState = FirstState;
	Second->PlayerState = SecondState;
	FGameOverWorldPauseState Pause;
	Pause.Begin({First, Second});
	const FGuid RoundId = Pause.GetRoundId();
	Pause.NotifyReady(First, RoundId, World);
	Pause.NotifyReady(Second, RoundId, World);
	TestTrue(TEXT("Recovery fixture is actually paused"), World->IsPaused());
	World->GetWorldSettings()->TimeDilation = 0.0f;
	Mode->EnsureArenaRecoveryWatchdog();
	TestTrue(TEXT("Recovery owns a core ticker while the world is paused"), Mode->ArenaRecoveryTickerHandle.IsValid());
	FTSTicker::GetCoreTicker().RemoveTicker(Mode->ArenaRecoveryTickerHandle);
	Mode->ArenaRecoveryTickerHandle.Reset();
	// 실제 시계만 과거로 배치하고 콜백을 실행하여 15초 실대기 없이 정지 월드의 경계를 검증한다.
	TestTrue(TEXT("Paused zero-dilation recovery remains active"), Mode->TickArenaRecovery(0.0f));
	TestEqual(TEXT("A stalled operation consumes one recovery"), Mode->ArenaReloadRecoveryAttempts, 1);
	TestFalse(TEXT("Recovery resumes the subsystem's stalled operation"), Arena->PendingGameplayReload->bIsStalled);
	TestEqual(TEXT("Recovery preserves the total deadline origin"), Mode->ArenaReloadStartedAt, StartedAt);
	TestEqual(TEXT("Retry delivery does not count as progress"), Mode->ArenaReloadLastProgressAt, ProgressAt);
	Mode->TickArenaRecovery(0.0f);
	TestEqual(TEXT("Repeated polling cannot immediately consume another recovery"), Mode->ArenaReloadRecoveryAttempts, 1);
	Mode->ArenaReloadLastRecoveryAt = FPlatformTime::Seconds() - StallLimit - 1.0;
	Arena->PendingGameplayReload->bIsStalled = true;
	Mode->TickArenaRecovery(0.0f);
	TestEqual(TEXT("A second stall consumes the final recovery"), Mode->ArenaReloadRecoveryAttempts, 2);
	TestEqual(TEXT("Second recovery preserves the original operation"), Mode->PendingGameplayGeneration, Generation);
	TestEqual(TEXT("Second recovery still preserves total deadline"), Mode->ArenaReloadStartedAt, StartedAt);
	Mode->ArenaReloadLastRecoveryAt = FPlatformTime::Seconds() - StallLimit - 1.0;
	AddExpectedError(TEXT("Failed Gen="), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Reload failed:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("A third stall ends the watchdog instead of retrying"), Mode->TickArenaRecovery(0.0f));
	TestEqual(TEXT("No third recovery is dispatched"), Mode->ArenaReloadRecoveryAttempts, 2);
	TestEqual(TEXT("Recovery exhaustion reports one failure"), Failures, 1);
	TestEqual(TEXT("Exhaustion identifies a worker stall"), LastFailure, EOutlierGameplayReloadFailure::WorkerStallTimeout);
	Pause.Reset(World);
	World->GetWorldSettings()->TimeDilation = 1.0f;

	BeginReload();
	Mode->ArenaReloadStartedAt = FPlatformTime::Seconds() - TotalLimit - 1.0;
	Mode->ArenaReloadLastProgressAt = FPlatformTime::Seconds();
	Mode->ArenaReloadLastRecoveryAt = FPlatformTime::Seconds();
	AddExpectedError(TEXT("Failed Gen="), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Reload failed:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Recent progress cannot extend the total timeout"), Mode->TickArenaRecovery(0.0f));
	TestEqual(TEXT("Total timeout does not spend another recovery"), Mode->ArenaReloadRecoveryAttempts, 0);
	TestEqual(TEXT("Total timeout reports its own single failure"), Failures, 2);

	BeginReload();
	Mode->PendingGameplayUnloadPlayers = {First, Second};
	TestFalse(TEXT("ACK fixture represents a remote controller"), First->IsLocalController());
	Mode->OnClientArenaGameplayUnloaded(First, Generation);
	TestTrue(TEXT("First valid unload ACK records actual progress"), Mode->ArenaReloadLastProgressAt > ProgressAt);
	const double UnloadProgressAt = Mode->ArenaReloadLastProgressAt;
	Mode->OnClientArenaGameplayUnloaded(First, Generation);
	Mode->OnClientArenaGameplayUnloaded(Second, Generation - 1);
	Mode->OnClientArenaGameplayUnloaded(OldController, Generation);
	TestEqual(TEXT("Duplicate and stale unload ACKs do not extend progress"), Mode->ArenaReloadLastProgressAt, UnloadProgressAt);
	TestEqual(TEXT("Duplicate and stale unload ACKs cannot satisfy the pair"), Mode->ReadyGameplayUnloadPlayers.Num(), 1);

	Mode->PendingGameplayUnloadPlayers.Reset();
	Mode->ReadyGameplayUnloadPlayers.Reset();
	APawn* PendingPawn = World->SpawnActor<APawn>();
	if (TestNotNull(TEXT("Pending Pawn exists"), PendingPawn))
	{
		Mode->PendingPossessions.Add(First, PendingPawn);
		Mode->PendingReconnectRequestIds.Add(First, 7);
		Mode->OnClientArenaReady(First, Generation, 7);
		TestTrue(TEXT("Valid ready ACK waits for the server and pair"), Mode->ReadyGameplayPossessPlayers.Contains(First));
		const double ReadyProgressAt = Mode->ArenaReloadLastProgressAt;
		Mode->OnClientArenaReady(First, Generation, 7);
		Mode->OnClientArenaReady(First, Generation, 6);
		Mode->OnClientArenaReady(OldController, Generation, 0);
		TestEqual(TEXT("Duplicate and obsolete ready ACKs do not extend progress"), Mode->ArenaReloadLastProgressAt, ReadyProgressAt);
		TestEqual(TEXT("Old controller cannot join the ready pair"), Mode->ReadyGameplayPossessPlayers.Num(), 1);
		TestNull(TEXT("Ready ACK does not partially possess before pair readiness"), First->GetPawn());
	}

	Arena->OnArenaGameplayReloadFailed.Remove(FailureHandle);
	Mode->PendingPossessions.Reset();
	Mode->bArenaReloadInProgress = false;
	Arena->PendingGameplayReload.Reset();
	Mode->ArenaReconnectDeadline = FPlatformTime::Seconds() + 30.0;
	TestFalse(TEXT("No pending reconnect stops the watcher"), Mode->TickArenaRecovery(0.0f));
	TestEqual(TEXT("Completed reconnect clears its deadline"), Mode->ArenaReconnectDeadline, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierReconnectPawnTeardownTest,
	"Outlier.Network.Reconnect.ControllerTeardownPreservesPawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierReconnectPawnTeardownTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedArenaRecoveryTestWorld Fixture;
	Fixture.World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	UWorld* World = Fixture.World;
	if (!TestNotNull(TEXT("Teardown world exists"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	FURL URL;
	URL.AddOption(TEXT("game=/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	World->SetGameMode(URL);
	URL.Port = 0;
	if (!TestTrue(TEXT("Listen fixture starts"), World->Listen(URL))) return false;
	AOutlierGameMode* Mode = World->GetAuthGameMode<AOutlierGameMode>();
	if (!TestNotNull(TEXT("Actual authoritative GameMode exists"), Mode)) return false;
	TestEqual(TEXT("Fixture is Listen"), World->GetNetMode(), NM_ListenServer);
	UClass* PawnClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Production Shooter class exists"), PawnClass)) return false;
	auto SpawnGuest = [&]()
	{
		AFirstPersonPlayerController* Controller = World->SpawnActor<AFirstPersonPlayerController>();
		AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
		State->SetTemporaryPlayerId(FGuid::NewGuid());
		Controller->PlayerState = State;
		Mode->ListenGuestPlayerId = State->GetTemporaryPlayerId();
		Mode->ListenGuestReconnectToken = FGuid::NewGuid();
		AShooterCharacter* Pawn = World->SpawnActor<AShooterCharacter>(PawnClass);
		Controller->Possess(Pawn);
		return Controller;
	};
	AFirstPersonPlayerController* Guest = SpawnGuest();
	APawn* Pawn = Guest->GetPawn();
	if (!TestNotNull(TEXT("Guest possesses a gameplay Pawn"), Pawn)) return false;
	TestFalse(TEXT("Guest is remote"), Guest->IsLocalController());
	// Logout만 직접 호출하면 엔진이 먼저 Pawn을 파괴하는 순서를 놓친다. 실제 Destroy를 사용한다.
	Guest->Destroy();
	TestFalse(TEXT("Controller teardown does not destroy the reconnect Pawn"), Pawn->IsActorBeingDestroyed());
	TestNull(TEXT("Preserved Pawn is detached from the old controller"), Pawn->GetController());
	TestEqual(TEXT("Listen owns the preserved Pawn"), Mode->ListenGuestReconnectPawn.Get(), Pawn);
	TestFalse(TEXT("Disconnected Pawn cannot take damage"), Pawn->CanBeDamaged());
	TestTrue(TEXT("Original damage state is retained"), Mode->ArenaWorkerReconnectDamageStates.FindRef(Mode->ListenGuestPlayerId));
	TestTrue(TEXT("Logout starts reconnect grace"), Mode->bListenGuestDisconnected);
	Mode->ClearListenReconnectPawn();
	Mode->bListenGuestDisconnected = false;
	FTSTicker::GetCoreTicker().RemoveTicker(Mode->ArenaRecoveryTickerHandle);
	Mode->ArenaRecoveryTickerHandle.Reset();

	Guest = SpawnGuest();
	Pawn = Guest->GetPawn();
	Mode->bListenHostReturnRequested = true;
	Guest->Destroy();
	TestTrue(TEXT("Explicit session exit retains normal Pawn destruction"), Pawn->IsActorBeingDestroyed());
	TestNull(TEXT("Explicit exit does not cache a Pawn"), Mode->ListenGuestReconnectPawn.Get());
	return true;
}

#endif
