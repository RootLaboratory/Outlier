#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/LocalPlayer.h"
#include "Components/SceneComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "TimerManager.h"
#include "CoreGlobals.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"

namespace
{
struct FScopedGameOverTestWorld
{
	UWorld* World = nullptr;

	bool Initialize(FAutomationTestBase& Test)
	{
		if (!Test.TestNotNull(TEXT("Engine is available"), GEngine)) return false;
		// CreateWorld와 같은 Outer에서 이름을 확인해야 에디터의 기존 월드를 덮어쓰지 않는다.
		const FName Name = MakeUniqueObjectName(
			GetTransientPackage(), UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
		World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Test world is created"), World)) return false;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		return true;
	}

	~FScopedGameOverTestWorld()
	{
		if (World)
		{
			// Actor뿐 아니라 서브시스템과 월드의 BegunPlay 상태도 공식 종료 경로에서 정리한다.
			World->EndPlay(EEndPlayReason::LevelTransition);
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGameOverWorldPauseTest,
	"Outlier.UI.GameOver.WorldPause.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGameOverWorldPauseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("A transient GameOver world is created"), World))
	{
		return false;
	}
	World->AddToRoot();
	APlayerController* First = World->SpawnActor<APlayerController>();
	APlayerController* Second = World->SpawnActor<APlayerController>();
	APlayerController* Stranger = World->SpawnActor<APlayerController>();
	APlayerState* FirstState = World->SpawnActor<APlayerState>();
	APlayerState* SecondState = World->SpawnActor<APlayerState>();
	AWorldSettings* Settings = World->GetWorldSettings();
	if (!TestNotNull(TEXT("First controller"), First)
		|| !TestNotNull(TEXT("Second controller"), Second)
		|| !TestNotNull(TEXT("Unrelated controller"), Stranger)
		|| !TestNotNull(TEXT("First player state"), FirstState)
		|| !TestNotNull(TEXT("Second player state"), SecondState)
		|| !TestNotNull(TEXT("World settings"), Settings))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		World->RemoveFromRoot();
		return false;
	}
	First->PlayerState = FirstState;
	Second->PlayerState = SecondState;

	FGameOverWorldPauseState Pause;
	TestFalse(TEXT("No UI targets cannot start a round"), Pause.Begin({}));
	TestFalse(TEXT("Null UI targets cannot start a round"), Pause.Begin({nullptr}));
	TestTrue(TEXT("Both targets are registered before notifying clients"), Pause.Begin({First, Second}));
	const FGuid FirstRound = Pause.GetRoundId();
	TestFalse(TEXT("An active round cannot be replaced"), Pause.Begin({First}));
	TestFalse(TEXT("Missing world cannot acknowledge readiness"), Pause.NotifyReady(First, FirstRound, nullptr));
	TestFalse(TEXT("Null client cannot acknowledge readiness"), Pause.NotifyReady(nullptr, FirstRound, World));
	TestFalse(TEXT("An unrelated client cannot acknowledge readiness"),
		Pause.NotifyReady(Stranger, FirstRound, World));
	TestFalse(TEXT("A mismatched round cannot acknowledge readiness"),
		Pause.NotifyReady(Second, FGuid::NewGuid(), World));
	TestTrue(TEXT("The first UI readiness is recorded"),
		Pause.NotifyReady(First, FirstRound, World));
	TestFalse(TEXT("Selection waits for both UI targets"), Pause.CanSelect(First, FirstRound));
	TestNull(TEXT("One ready UI leaves the world running"), Settings->GetPauserPlayerState());
	TestFalse(TEXT("Duplicate readiness cannot count as the other client"),
		Pause.NotifyReady(First, FirstRound, World));
	TestTrue(TEXT("The second UI readiness is recorded"), Pause.NotifyReady(Second, FirstRound, World));
	TestFalse(TEXT("Both ready UIs still wait for required streaming"), Pause.TryPause(World));
	TestFalse(TEXT("Missing streaming keeps the world running"), World->IsPaused());
	TestTrue(TEXT("Selection is allowed while streaming is incomplete"), Pause.CanSelect(First, FirstRound));
	Pause.SetServerStreamingReady(7, true);
	TestFalse(TEXT("A different gameplay generation is rejected"), Pause.SetPlayerStreamingReady(First, FirstRound, 6, true));
	TestFalse(TEXT("A different round is rejected"), Pause.SetPlayerStreamingReady(First, FGuid::NewGuid(), 7, true));
	TestFalse(TEXT("An unrelated streaming sender is rejected"), Pause.SetPlayerStreamingReady(Stranger, FirstRound, 7, true));
	Pause.SetPlayerStreamingReady(First, FirstRound, 7, true);
	Pause.SetPlayerStreamingReady(First, FirstRound, 7, true);
	TestFalse(TEXT("Duplicates cannot account for the other player"), Pause.TryPause(World));
	Pause.SetPlayerStreamingReady(Second, FirstRound, 7, true);
	Pause.SetPlayerStreamingReady(Second, FirstRound, 7, false);
	TestFalse(TEXT("A withdrawal blocks the pause"), Pause.TryPause(World));
	Pause.SetPlayerStreamingReady(Second, FirstRound, 7, true);
	Pause.SetServerStreamingReady(7, false);
	TestFalse(TEXT("The server must still be ready immediately before pausing"), Pause.TryPause(World));
	Pause.SetServerStreamingReady(7, true);
	TestTrue(TEXT("All streaming and both UIs permit pause"), Pause.TryPause(World));
	TestFalse(TEXT("The same round cannot pause twice"), Pause.TryPause(World));
	TestTrue(TEXT("The native world pause is active"), World->IsPaused());
	TestTrue(TEXT("The pause uses a participant's replicated player state"),
		Settings->GetPauserPlayerState() == FirstState || Settings->GetPauserPlayerState() == SecondState);
	TestFalse(TEXT("Readiness after pausing is idempotent"), Pause.NotifyReady(Second, FirstRound, World));
	TestTrue(TEXT("Current participants can select once both UIs are ready"), Pause.CanSelect(First, FirstRound));
	TestFalse(TEXT("Unrelated controllers cannot select"), Pause.CanSelect(Stranger, FirstRound));
	TestFalse(TEXT("Previous rounds cannot select"), Pause.CanSelect(First, FGuid::NewGuid()));
	TestTrue(TEXT("A confirmed selection begins one transition"), Pause.BeginTransition(World));
	TestNull(TEXT("Pause is released before transition work"), Settings->GetPauserPlayerState());
	TestTrue(TEXT("Transition preserves targets for failure recovery"), Pause.Contains(Second));
	TestFalse(TEXT("A simultaneous second selection is rejected"), Pause.CanSelect(Second, FirstRound));
	TestFalse(TEXT("A transition cannot begin twice"), Pause.BeginTransition(World));
	TestFalse(TEXT("Late readiness cannot pause a transition"), Pause.NotifyReady(First, FirstRound, World));
	TestFalse(TEXT("Late streaming cannot pause a transition"), Pause.SetPlayerStreamingReady(First, FirstRound, 7, true));
	TestTrue(TEXT("A recoverable failure restores selection"), Pause.RestoreSelection());
	const FGuid RecoveryRound = Pause.GetRoundId();
	TestTrue(TEXT("Recovery gets a fresh round identifier"), RecoveryRound != FirstRound);
	TestFalse(TEXT("Pre-failure selections cannot affect recovery"), Pause.CanSelect(First, FirstRound));
	TestFalse(TEXT("Recovery waits for the rebuilt UIs"), Pause.CanSelect(First, RecoveryRound));
	Pause.SetServerStreamingReady(8, true);
	Pause.SetPlayerStreamingReady(First, RecoveryRound, 8, true);
	Pause.SetPlayerStreamingReady(Second, RecoveryRound, 8, true);
	Pause.NotifyReady(First, RecoveryRound, World);
	TestFalse(TEXT("Ready streaming waits for the remaining UI"), Pause.TryPause(World));
	Pause.NotifyReady(Second, RecoveryRound, World);
	TestTrue(TEXT("Both rebuilt UIs and new streaming restore the pause"), Pause.TryPause(World));
	TestTrue(TEXT("Recovery allows a new selection"), Pause.CanSelect(First, RecoveryRound));

	Pause.Reset(World);
	TestFalse(TEXT("Transition/disconnect cleanup invalidates the round"), Pause.IsActive());
	TestFalse(TEXT("Cleanup removes old participants"), Pause.Contains(First));
	TestNull(TEXT("Cleanup releases its own pause"), Settings->GetPauserPlayerState());
	TestFalse(TEXT("Late readiness after cleanup cannot pause"), Pause.NotifyReady(First, FirstRound, World));
	TestTrue(TEXT("A repeated death starts a fresh round"), Pause.Begin({First, Second}));
	const FGuid SecondRound = Pause.GetRoundId();
	TestTrue(TEXT("Repeated death has a distinct identifier"), FirstRound != SecondRound);
	TestFalse(TEXT("Previous death cannot acknowledge the new round"),
		Pause.NotifyReady(Second, FirstRound, World));
	Pause.NotifyReady(First, SecondRound, World);
	TestNull(TEXT("Stale readiness did not complete the new round"), Settings->GetPauserPlayerState());
	Pause.NotifyReady(Second, SecondRound, World);
	Pause.SetServerStreamingReady(8, true);
	Pause.SetPlayerStreamingReady(First, SecondRound, 8, true);
	Pause.SetPlayerStreamingReady(Second, SecondRound, 8, true);
	Pause.SetServerStreamingReady(9, true);
	TestFalse(TEXT("A new generation invalidates both reports"), Pause.TryPause(World));
	Pause.SetPlayerStreamingReady(First, SecondRound, 9, true);
	Pause.SetPlayerStreamingReady(Second, SecondRound, 9, true);
	TestTrue(TEXT("Current readiness completes the new round"), Pause.TryPause(World));

	// 정지 소유권이 다른 흐름으로 넘어간 경우 GameOver 정리로 해제하지 않는다.
	APlayerState* OtherOwner = Settings->GetPauserPlayerState() == FirstState ? SecondState : FirstState;
	Settings->SetPauserPlayerState(OtherOwner);
	TestFalse(TEXT("Transition cannot release another pause owner"), Pause.BeginTransition(World));
	Pause.Reset(World);
	TestEqual(TEXT("Cleanup preserves a different pause owner"), Settings->GetPauserPlayerState(), OtherOwner);
	TestTrue(TEXT("A round can begin while another pause exists"), Pause.Begin({First, Second}));
	const FGuid ThirdRound = Pause.GetRoundId();
	Pause.NotifyReady(First, ThirdRound, World);
	Pause.NotifyReady(Second, ThirdRound, World);
	Pause.SetServerStreamingReady(0, true);
	Pause.SetPlayerStreamingReady(First, ThirdRound, 0, true);
	Pause.SetPlayerStreamingReady(Second, ThirdRound, 0, true);
	TestFalse(TEXT("GameOver does not take over an existing pause"), Pause.TryPause(World));
	TestFalse(TEXT("Foreign pause prevents transition"), Pause.BeginTransition(World));
	TestEqual(TEXT("Unowned pause survives failed transition"), Settings->GetPauserPlayerState(), OtherOwner);
	Settings->SetPauserPlayerState(nullptr);
	TestTrue(TEXT("Pause ownership can be retried after the foreign owner releases"), Pause.TryPause(World));
	Pause.Reset(World);

	TestTrue(TEXT("A remaining single target can start a round"), Pause.Begin({First}));
	Pause.NotifyReady(First, Pause.GetRoundId(), World);
	Pause.SetServerStreamingReady(0, true);
	Pause.SetPlayerStreamingReady(First, Pause.GetRoundId(), 0, true);
	TestFalse(TEXT("A single target cannot pause a pair round"), Pause.TryPause(World));
	TestTrue(TEXT("A single target remains selectable"), Pause.CanSelect(First, Pause.GetRoundId()));
	Pause.Reset(World);
	TestNull(TEXT("Final cleanup leaves no pause"), Settings->GetPauserPlayerState());
	TestTrue(TEXT("Disconnect before UI readiness can start a round"), Pause.Begin({First, Second}));
	const FGuid DisconnectRound = Pause.GetRoundId();
	TestTrue(TEXT("Disconnect cleanup can identify a participant before native destruction"), Pause.Contains(Second));
	Pause.RemoveDisconnectedController(Second, World);
	Pause.NotifyReady(First, DisconnectRound, World);
	TestFalse(TEXT("The remaining client cannot pause after disconnect cleanup"), Pause.TryPause(World));
	TestNull(TEXT("Disconnect leaves the world running"), Settings->GetPauserPlayerState());
	TestFalse(TEXT("The departed client cannot select"), Pause.CanSelect(Second, DisconnectRound));
	TestTrue(TEXT("The remaining client may choose without pausing"), Pause.CanSelect(First, DisconnectRound));
	TestFalse(TEXT("Pause retries cannot freeze the world after disconnect"), Pause.TryPause(World));
	TestTrue(TEXT("A remaining client's confirmed selection can transition"), Pause.BeginTransition(World));
	Pause.Reset(World);
	TestTrue(TEXT("A disconnect while paused can start a round"), Pause.Begin({First, Second}));
	const FGuid PausedDisconnectRound = Pause.GetRoundId();
	Pause.NotifyReady(First, PausedDisconnectRound, World);
	Pause.NotifyReady(Second, PausedDisconnectRound, World);
	Pause.SetServerStreamingReady(0, true);
	Pause.SetPlayerStreamingReady(First, PausedDisconnectRound, 0, true);
	Pause.SetPlayerStreamingReady(Second, PausedDisconnectRound, 0, true);
	TestTrue(TEXT("The disconnect fixture owns the pause"), Pause.TryPause(World));
	Pause.RemoveDisconnectedController(Second, World);
	TestNull(TEXT("Disconnect releases the current GameOver pause"), Settings->GetPauserPlayerState());
	TestFalse(TEXT("Late departed readiness is ignored"), Pause.NotifyReady(Second, PausedDisconnectRound, World));
	Pause.Reset(World);
	TestTrue(TEXT("A new round can wait until a streaming timeout"), Pause.Begin({First, Second}));
	const FGuid TimeoutRound = Pause.GetRoundId();
	Pause.NotifyReady(First, TimeoutRound, World);
	Pause.NotifyReady(Second, TimeoutRound, World);
	Pause.CancelStreamingWait();
	TestFalse(TEXT("A late report cannot restart a timed-out wait"), Pause.SetPlayerStreamingReady(First, TimeoutRound, 0, true));
	TestFalse(TEXT("A timed-out round cannot pause"), Pause.TryPause(World));
	TestTrue(TEXT("A timed-out round remains selectable"), Pause.CanSelect(First, TimeoutRound));
	Pause.Reset(World);
	TestTrue(TEXT("UI failure reporting can start a round"), Pause.Begin({First, Second}));
	const FGuid UnavailableRound = Pause.GetRoundId();
	TestFalse(TEXT("Previous death cannot report UI failure"), Pause.NotifyUnavailable(First, FirstRound));
	TestFalse(TEXT("Unrelated client cannot report UI failure"), Pause.NotifyUnavailable(Stranger, UnavailableRound));
	TestTrue(TEXT("First target can report actual UI failure"), Pause.NotifyUnavailable(First, UnavailableRound));
	TestFalse(TEXT("One failure cannot trigger the no-UI fallback"), Pause.AreAllUIsUnavailable());
	TestFalse(TEXT("Duplicate failure does not count as another target"), Pause.NotifyUnavailable(First, UnavailableRound));
	TestTrue(TEXT("Second target can report actual UI failure"), Pause.NotifyUnavailable(Second, UnavailableRound));
	TestTrue(TEXT("Only failure from every target permits the no-UI fallback"), Pause.AreAllUIsUnavailable());
	TestFalse(TEXT("Missing UIs never start a pause"), Pause.TryPause(World));
	TestTrue(TEXT("The no-UI fallback can transition without readiness"), Pause.BeginTransition(World));
	Pause.Reset(World);

	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGameOverProposalIdentityTest,
	"Outlier.UI.GameOver.WorldPause.ProposalIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGameOverProposalIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGameOverPendingRequest Request;
	TestFalse(TEXT("An uninitialized proposal cannot match a response"), Request.MatchesProposal({}, {}));
	Request.RoundId = FGuid::NewGuid();
	Request.ProposalId = FGuid::NewGuid();
	TestTrue(TEXT("The current proposal matches its response"),
		Request.MatchesProposal(Request.RoundId, Request.ProposalId));
	TestFalse(TEXT("A previous death cannot approve this proposal"),
		Request.MatchesProposal(FGuid::NewGuid(), Request.ProposalId));
	const FGuid OldProposal = Request.ProposalId;
	Request.ProposalId = FGuid::NewGuid();
	TestFalse(TEXT("A previous proposal in the same death cannot approve the new choice"),
		Request.MatchesProposal(Request.RoundId, OldProposal));
	TestTrue(TEXT("The replacement proposal accepts only its new identity"),
		Request.MatchesProposal(Request.RoundId, Request.ProposalId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierGameOverSimulationPauseTest,
	"Outlier.UI.GameOver.WorldPause.Simulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGameOverSimulationPauseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedGameOverTestWorld TestWorld;
	if (!TestWorld.Initialize(*this)) return false;
	UWorld* World = TestWorld.World;
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
	if (!TestTrue(TEXT("A neutral authority game mode is created"), World->SetGameMode(URL))) return false;
	World->InitializeActorsForPlay(URL);
	World->BeginPlay();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	APlayerController* Partner = World->SpawnActor<APlayerController>();
	AActor* MovingActor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Pause participant"), Controller) || !TestNotNull(TEXT("Pause partner"), Partner)
		|| !TestNotNull(TEXT("Movement probe"), MovingActor)) return false;
	Controller->PlayerState = World->SpawnActor<APlayerState>();
	Partner->PlayerState = World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("Participant player state"), Controller->PlayerState.Get())
		|| !TestNotNull(TEXT("Partner player state"), Partner->PlayerState.Get()))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(MovingActor);
	MovingActor->SetRootComponent(Root);
	Root->RegisterComponent();
	URotatingMovementComponent* Movement = NewObject<URotatingMovementComponent>(MovingActor);
	Movement->RotationRate = FRotator(0.0f, 90.0f, 0.0f);
	Movement->SetUpdatedComponent(Root);
	Movement->RegisterComponent();

	int32 TimerCount = 0;
	FTimerHandle Timer;
	World->GetTimerManager().SetTimer(Timer, [&TimerCount]() { ++TimerCount; }, 0.01f, true);
	// TimerManager는 같은 엔진 프레임에서 중복 Tick하지 않는다. 테스트 프레임도 함께 전진시킨다.
	auto TickWorld = [World]()
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.05f);
	};
	TickWorld();
	TickWorld();
	TestTrue(TEXT("Gameplay movement runs before pause"), !Root->GetComponentRotation().IsNearlyZero());
	TestTrue(TEXT("Gameplay timer runs before pause"), TimerCount > 0);
	FGameOverWorldPauseState Pause;
	Pause.Begin({Controller, Partner});
	const FGuid Round = Pause.GetRoundId();
	Pause.NotifyReady(Controller, Round, World);
	Pause.NotifyReady(Partner, Round, World);
	Pause.SetServerStreamingReady(0, true);
	Pause.SetPlayerStreamingReady(Controller, Round, 0, true);
	Pause.SetPlayerStreamingReady(Partner, Round, 0, true);
	TestTrue(TEXT("Ready streaming and UI pause the simulation"), Pause.TryPause(World));
	const FRotator PausedRotation = Root->GetComponentRotation();
	const int32 PausedTimerCount = TimerCount;
	const double PausedWorldTime = World->GetTimeSeconds();
	TickWorld();
	TickWorld();
	TestTrue(TEXT("Gameplay component tick is frozen"), Root->GetComponentRotation().Equals(PausedRotation));
	TestEqual(TEXT("Gameplay timer is frozen"), TimerCount, PausedTimerCount);
	TestEqual(TEXT("Gameplay world time is frozen"), World->GetTimeSeconds(), PausedWorldTime);
	TestTrue(TEXT("Confirmed transition releases simulation"), Pause.BeginTransition(World));
	TickWorld();
	TickWorld();
	TestTrue(TEXT("Gameplay movement resumes"), !Root->GetComponentRotation().Equals(PausedRotation));
	TestTrue(TEXT("Gameplay timer resumes"), TimerCount > PausedTimerCount);
	World->GetTimerManager().ClearTimer(Timer);
	Pause.Reset(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierGameOverPendingFailureTest,
	"Outlier.UI.GameOver.WorldPause.PendingFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGameOverPendingFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedGameOverTestWorld TestWorld;
	if (!TestWorld.Initialize(*this)) return false;
	// 추상 GameMode는 프로젝트의 실제 파생 클래스로 생성하되 맵/매치 시작은 실행하지 않는다.
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	if (!TestNotNull(TEXT("Project game mode class"), ModeClass)) return false;
	AOutlierGameMode* Mode = TestWorld.World->SpawnActor<AOutlierGameMode>(ModeClass);
	AFirstPersonPlayerController* First = TestWorld.World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Second = TestWorld.World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Stranger = TestWorld.World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestNotNull(TEXT("Game mode"), Mode) || !TestNotNull(TEXT("Requester"), First)
		|| !TestNotNull(TEXT("Responder"), Second) || !TestNotNull(TEXT("Unrelated controller"), Stranger)) return false;
	for (AFirstPersonPlayerController* Controller : { First, Second, Stranger })
	{
		AOutlierPlayerState* State = TestWorld.World->SpawnActor<AOutlierPlayerState>();
		if (!TestNotNull(TEXT("Pair player state"), State)) return false;
		State->SetPairId(1);
		Controller->PlayerState = State;
	}
	Mode->GameOverWorldPause.Begin({First, Second});
	const FGuid Round = Mode->GameOverWorldPause.GetRoundId();
	Mode->GameOverWorldPause.NotifyReady(First, Round, TestWorld.World);
	Mode->GameOverWorldPause.NotifyReady(Second, Round, TestWorld.World);
	Mode->GameOverWorldPause.SetServerStreamingReady(0, true);
	Mode->GameOverWorldPause.SetPlayerStreamingReady(First, Round, 0, true);
	Mode->GameOverWorldPause.SetPlayerStreamingReady(Second, Round, 0, true);
	TestTrue(TEXT("Proposal fixture owns the GameOver pause"), Mode->GameOverWorldPause.TryPause(TestWorld.World));
	Mode->GameOverActivePairs.Add(1);
	FGameOverPendingServerState Pending;
	Pending.Requester = First;
	Pending.Responder = Second;
	Pending.Request.RoundId = Round;
	Pending.Request.ProposalId = FGuid::NewGuid();
	Mode->GameOverPendingByPair.Add(1, Pending);
	AddExpectedMessage(TEXT("[GameOver] Pending UI unavailable; proposal cancelled"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2, false);
	Mode->OnClientGameOverPendingUnavailable(Stranger, Round, Pending.Request.ProposalId);
	TestTrue(TEXT("Unrelated failure cannot cancel a proposal"), Mode->GameOverPendingByPair.Contains(1));
	Mode->OnClientGameOverPendingUnavailable(First, FGuid::NewGuid(), Pending.Request.ProposalId);
	Mode->OnClientGameOverPendingUnavailable(First, Round, FGuid::NewGuid());
	TestTrue(TEXT("Stale failure cannot cancel a proposal"), Mode->GameOverPendingByPair.Contains(1));
	Mode->OnClientGameOverPendingUnavailable(Second, Round, Pending.Request.ProposalId);
	TestFalse(TEXT("Actual display failure cancels the proposal"), Mode->GameOverPendingByPair.Contains(1));
	TestTrue(TEXT("Display failure retains GameOver pause"), TestWorld.World->IsPaused());
	TestTrue(TEXT("Display failure retains selectable round"), Mode->GameOverWorldPause.CanSelect(First, Round));
	const FGuid OldProposal = Pending.Request.ProposalId;
	Pending.Request.ProposalId = FGuid::NewGuid();
	Mode->GameOverPendingByPair.Add(1, Pending);
	Mode->OnClientGameOverPendingUnavailable(First, Round, OldProposal);
	TestTrue(TEXT("Late duplicate failure preserves the next proposal"), Mode->GameOverPendingByPair.Contains(1));
	Mode->OnClientGameOverPendingUnavailable(First, Round, Pending.Request.ProposalId);
	TestFalse(TEXT("Requester can also report display failure"), Mode->GameOverPendingByPair.Contains(1));
	FGameOverPendingRequest InvalidSelection;
	InvalidSelection.RoundId = Round;
	InvalidSelection.Choice = EGameOverPendingChoice::PresetLevel;
	InvalidSelection.LevelIndex = 0;
	TestFalse(TEXT("Invalid selection is rejected"), Mode->RequestGameOverPendingChoice(First, InvalidSelection));
	TestTrue(TEXT("Selection validation failure retains pause"), TestWorld.World->IsPaused());
	Pending.Request.ProposalId = FGuid::NewGuid();
	Mode->GameOverPendingByPair.Add(1, Pending);
	TestTrue(TEXT("The responder can reject the current proposal"),
		Mode->RespondGameOverPending(Second, Round, Pending.Request.ProposalId, false));
	TestFalse(TEXT("Rejection clears only the pending proposal"), Mode->GameOverPendingByPair.Contains(1));
	TestTrue(TEXT("Rejection retains pause and selection"),
		TestWorld.World->IsPaused() && Mode->GameOverWorldPause.CanSelect(First, Round));
	TestFalse(TEXT("Duplicate rejection is ignored"),
		Mode->RespondGameOverPending(Second, Round, Pending.Request.ProposalId, false));
	// 초기화하지 않은 GameInstance에는 복원 스냅샷이 없다. 이어하기가 리로드 검증 경로를 타는지 확인한다.
	Mode->ShooterClass = AShooterCharacter::StaticClass();
	Mode->PartnerClass = APartnerCharacter::StaticClass();
	FGameOverPendingRequest Continue;
	Continue.RoundId = Round;
	Continue.Choice = EGameOverPendingChoice::Continue;
	AddExpectedError(TEXT("[Checkpoint.Restart] Preflight failed"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestFalse(TEXT("Continue rejects an unavailable checkpoint restore"),
		Mode->ExecuteGameOverSelection(First, Second, Continue));
	TestTrue(TEXT("Continue preflight failure preserves pause and round"),
		TestWorld.World->IsPaused() && Mode->GameOverWorldPause.CanSelect(First, Round));
	TestFalse(TEXT("Continue preflight failure clears restart-in-progress"), Mode->bCheckpointRestartInProgress);
	Mode->FinishGameOverFlow();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierGameOverStreamingReadinessTest,
	"Outlier.UI.GameOver.WorldPause.StreamingReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGameOverStreamingReadinessTest::RunTest(const FString& Parameters)
{
	FGameOverStreamingReadiness Readiness;
	FGameOverStreamingStatus Status;
	Status.RequiredContent.Add(TEXT("Gameplay"));
	TestFalse(TEXT("One ready frame is insufficient"), Readiness.Observe(1, Status, 100));
	TestFalse(TEXT("An immediate recheck does not advance the frame count"), Readiness.Observe(1, Status, 100));
	TestFalse(TEXT("Two ready frames are insufficient"), Readiness.Observe(1, Status, 101));
	TestTrue(TEXT("Three ready frames qualify"), Readiness.Observe(1, Status, 102));
	Status.RequiredContent.Add(TEXT("ArenaCell"));
	TestFalse(TEXT("A scope change restarts the count even if already loaded"), Readiness.Observe(1, Status, 103));
	Status.PendingContent.Add(TEXT("ArenaCell"));
	TestFalse(TEXT("A pending required cell invalidates readiness"), Readiness.Observe(1, Status, 104));
	Status.PendingContent.Reset();
	TestFalse(TEXT("One new ready frame is insufficient"), Readiness.Observe(1, Status, 105));
	TestFalse(TEXT("Two new ready frames are insufficient"), Readiness.Observe(1, Status, 106));
	TestTrue(TEXT("The new scope becomes ready after three frames"), Readiness.Observe(1, Status, 107));
	TestFalse(TEXT("A generation change invalidates the old scope"), Readiness.Observe(2, Status, 108));
	TestFalse(TEXT("A missing frame breaks the consecutive run"), Readiness.Observe(2, Status, 110));
	TestFalse(TEXT("The frame after a gap needs another sample"), Readiness.Observe(2, Status, 111));
	TestTrue(TEXT("Three frames after a gap qualify"), Readiness.Observe(2, Status, 112));

	FGameOverStreamingWait Wait(100.0);
	Wait.Observe(110.0, true);
	TestTrue(TEXT("Ten seconds of streaming wait triggers diagnostics"), Wait.ShouldWarn());
	Wait.Observe(160.0, false);
	TestEqual(TEXT("Waiting for UI consumes no streaming time"), Wait.GetWaitSeconds(), 10.0);
	Wait.Observe(179.0, false);
	TestFalse(TEXT("The remaining wait is available after withdrawal"), Wait.HasExpired());
	Wait.Observe(180.0, false);
	TestTrue(TEXT("Thirty cumulative streaming seconds expire the wait"), Wait.HasExpired());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierGameOverStreamingVisibilityTest,
	"Outlier.UI.GameOver.WorldPause.StreamingVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGameOverStreamingVisibilityTest::RunTest(const FString& Parameters)
{
	FScopedGameOverTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}
	UWorld* World = TestWorld.World;
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr, TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	if (!TestNotNull(TEXT("Gameplay mode class"), ModeClass))
	{
		return false;
	}
	AOutlierGameMode* Defaults = ModeClass->GetDefaultObject<AOutlierGameMode>();
	const bool bPreviousDirectPair = Defaults->bDirectPairTest;
	ON_SCOPE_EXIT
	{
		Defaults->bDirectPairTest = bPreviousDirectPair;
		World->WorldType = EWorldType::Game;
	};
	Defaults->bDirectPairTest = true;
	World->WorldType = EWorldType::PIE;
	World->GetWorldSettings()->DefaultGameMode = ModeClass;
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	Arena->EnsureArenaLoaded();
	TestTrue(TEXT("The fixture's Arena starts ready"), Arena->GetGameOverStreamingStatus({}).IsReady());

	bool bRequested = false;
	ULevelStreamingDynamic* OptionalLevel = nullptr;
	{
		// 테스트는 DirectPair 판정에만 PIE가 필요하다. 레벨 로더는 실제 PIE 복제 없이 실행한다.
		TGuardValue<TEnumAsByte<EWorldType::Type>> GameWorld(World->WorldType, EWorldType::Game);
		OptionalLevel = ULevelStreamingDynamic::LoadLevelInstance(World,
			TEXT("/Game/Maps/Title"), FVector::ZeroVector, FRotator::ZeroRotator, bRequested);
	}
	if (!TestTrue(TEXT("A real sublevel was requested"), bRequested && OptionalLevel))
	{
		return false;
	}
	auto FlushFixtureStreaming = [World]()
	{
		TGuardValue<TEnumAsByte<EWorldType::Type>> GameWorld(World->WorldType, EWorldType::Game);
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	};
	OptionalLevel->SetShouldBeVisible(false);
	FlushFixtureStreaming();
	TestTrue(TEXT("The package can load while its actors are hidden"), OptionalLevel->IsLevelLoaded());
	TestFalse(TEXT("The loaded sublevel starts hidden"), OptionalLevel->IsLevelVisible());
	TestTrue(TEXT("A hidden level outside the requested scope does not block"), Arena->GetGameOverStreamingStatus({}).IsReady());
	const FName Package = OptionalLevel->GetWorldAssetPackageFName();

	AOutlierGameMode* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	AFirstPersonPlayerController* First = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Second = World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestNotNull(TEXT("Match mode"), Mode) || !TestNotNull(TEXT("First controller"), First)
		|| !TestNotNull(TEXT("Second controller"), Second))
	{
		return false;
	}
	for (AFirstPersonPlayerController* Controller : {First, Second})
	{
		AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
		State->SetOwner(Controller);
		State->SetPairId(1);
		Controller->PlayerState = State;
		Controller->Player = NewObject<ULocalPlayer>(GEngine);
	}
	Mode->GameOverActivePairs.Add(1);
	Mode->GameOverWorldPause.Begin({First, Second});
	Mode->StartGameOverStreamingMonitor();
	ON_SCOPE_EXIT { Mode->FinishGameOverFlow(); };
	const FGuid Round = Mode->GameOverWorldPause.GetRoundId();
	const FGuid OldScope = Mode->GameOverStreamingScopeId;
	const uint32 Generation = Arena->GetGameplayGeneration();
	Mode->OnClientGameOverStreaming(First, Round, OldScope, Generation, true, false, {});
	Mode->OnClientGameOverStreaming(Second, Round, OldScope, Generation, true, false, {});

	OptionalLevel->SetShouldBeVisible(true);
	const FGameOverStreamingStatus Pending = Arena->GetGameOverStreamingStatus({});
	TestFalse(TEXT("A requested but hidden level blocks readiness"), Pending.IsReady());
	TestTrue(TEXT("The in-flight sublevel is identified by package"), Pending.PendingContent.Contains(Package.ToString()));
	++GFrameCounter;
	Mode->TickGameOverStreaming(0.0f);
	const FGuid Scope = Mode->GameOverStreamingScopeId;
	TestTrue(TEXT("A different authority scope invalidates in-flight reports"), Scope != OldScope);
	Mode->OnClientGameOverReady(First, Round);
	Mode->OnClientGameOverReady(Second, Round);
	Mode->OnClientGameOverStreaming(First, Round, Scope, Generation, true, false, {});
	Mode->OnClientGameOverStreaming(Second, Round, OldScope, Generation, true, false, {});
	TestFalse(TEXT("Ready UIs cannot pause with missing authority visibility"), World->IsPaused());
	FlushFixtureStreaming();
	const ULevel* Shown = OptionalLevel->GetLoadedLevel();
	TestTrue(TEXT("The actual requested level is visible"), Shown && Shown->bIsVisible);
	if (Shown)
	{
		for (const AActor* Actor : Shown->Actors)
		{
			if (IsValid(Actor) && !Actor->IsA<AWorldSettings>())
			{
				AddInfo(FString::Printf(TEXT("[GameOverVisibility] Package=%s Actor=%s VisibleAt=%.3f"),
					*Package.ToString(), *Actor->GetPathName(), FPlatformTime::Seconds()));
				break;
			}
		}
	}
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		++GFrameCounter;
		Mode->TickGameOverStreaming(0.0f);
		TestEqual(TEXT("The authority needs three stable visibility frames"), Mode->GameOverServerStreaming.IsReady(), Frame == 2);
		TestFalse(TEXT("An old-scope peer report cannot pause"), World->IsPaused());
	}
	Mode->OnClientGameOverStreaming(Second, Round, Scope, Generation, true, false, {});
	TestTrue(TEXT("The final current-scope peer report permits pause"), World->IsPaused());
	AddInfo(FString::Printf(TEXT("[GameOverVisibility] PausedAt=%.3f"), FPlatformTime::Seconds()));
	return true;
}

#endif
