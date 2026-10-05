#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "OutlierGameMode.h"

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
	TestFalse(TEXT("The first ready client does not pause the other death transition"),
		Pause.NotifyReady(First, FirstRound, World));
	TestFalse(TEXT("Selection waits for both UI targets"), Pause.CanSelect(First, FirstRound));
	TestNull(TEXT("One ready UI leaves the world running"), Settings->GetPauserPlayerState());
	TestFalse(TEXT("Duplicate readiness cannot count as the other client"),
		Pause.NotifyReady(First, FirstRound, World));
	TestTrue(TEXT("Both ready UIs pause the world"), Pause.NotifyReady(Second, FirstRound, World));
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
	TestTrue(TEXT("A recoverable failure restores selection"), Pause.RestoreSelection());
	const FGuid RecoveryRound = Pause.GetRoundId();
	TestTrue(TEXT("Recovery gets a fresh round identifier"), RecoveryRound != FirstRound);
	TestFalse(TEXT("Pre-failure selections cannot affect recovery"), Pause.CanSelect(First, FirstRound));
	TestFalse(TEXT("Recovery waits for the rebuilt UIs"), Pause.CanSelect(First, RecoveryRound));
	Pause.NotifyReady(First, RecoveryRound, World);
	TestTrue(TEXT("Both rebuilt UIs restore the pause"), Pause.NotifyReady(Second, RecoveryRound, World));
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
	TestTrue(TEXT("Current readiness completes the new round"), Pause.NotifyReady(Second, SecondRound, World));

	// 정지 소유권이 다른 흐름으로 넘어간 경우 GameOver 정리로 해제하지 않는다.
	APlayerState* OtherOwner = Settings->GetPauserPlayerState() == FirstState ? SecondState : FirstState;
	Settings->SetPauserPlayerState(OtherOwner);
	TestFalse(TEXT("Transition cannot release another pause owner"), Pause.BeginTransition(World));
	Pause.Reset(World);
	TestEqual(TEXT("Cleanup preserves a different pause owner"), Settings->GetPauserPlayerState(), OtherOwner);
	TestTrue(TEXT("A round can begin while another pause exists"), Pause.Begin({First, Second}));
	const FGuid ThirdRound = Pause.GetRoundId();
	Pause.NotifyReady(First, ThirdRound, World);
	TestFalse(TEXT("GameOver does not take over an existing pause"), Pause.NotifyReady(Second, ThirdRound, World));
	TestFalse(TEXT("Foreign pause prevents transition"), Pause.BeginTransition(World));
	TestEqual(TEXT("Unowned pause survives failed transition"), Settings->GetPauserPlayerState(), OtherOwner);
	Settings->SetPauserPlayerState(nullptr);
	TestTrue(TEXT("Pause ownership can be retried after the foreign owner releases"), Pause.TryPause(World));
	Pause.Reset(World);

	TestTrue(TEXT("A remaining single target can start a round"), Pause.Begin({First}));
	TestTrue(TEXT("A single UI target can pause once ready"), Pause.NotifyReady(First, Pause.GetRoundId(), World));
	Pause.Reset(World);
	TestNull(TEXT("Final cleanup leaves no pause"), Settings->GetPauserPlayerState());
	TestTrue(TEXT("Disconnect before UI readiness can start a round"), Pause.Begin({First, Second}));
	const FGuid DisconnectRound = Pause.GetRoundId();
	TestTrue(TEXT("Disconnect cleanup can identify a participant before native destruction"), Pause.Contains(Second));
	Pause.RemoveDisconnectedController(Second, World);
	TestFalse(TEXT("The remaining client cannot pause after disconnect cleanup"),
		Pause.NotifyReady(First, DisconnectRound, World));
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
	Pause.RemoveDisconnectedController(Second, World);
	TestNull(TEXT("Disconnect releases the current GameOver pause"), Settings->GetPauserPlayerState());
	TestFalse(TEXT("Late departed readiness is ignored"), Pause.NotifyReady(Second, PausedDisconnectRound, World));
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

#endif
