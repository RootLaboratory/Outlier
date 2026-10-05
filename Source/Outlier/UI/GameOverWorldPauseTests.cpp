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
	TestNull(TEXT("One ready UI leaves the world running"), Settings->GetPauserPlayerState());
	TestFalse(TEXT("Duplicate readiness cannot count as the other client"),
		Pause.NotifyReady(First, FirstRound, World));
	TestTrue(TEXT("Both ready UIs pause the world"), Pause.NotifyReady(Second, FirstRound, World));
	TestTrue(TEXT("The native world pause is active"), World->IsPaused());
	TestEqual(TEXT("The pause uses a replicated player state"), Settings->GetPauserPlayerState(), SecondState);
	TestFalse(TEXT("Readiness after pausing is idempotent"), Pause.NotifyReady(Second, FirstRound, World));

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
	Settings->SetPauserPlayerState(FirstState);
	Pause.Reset(World);
	TestEqual(TEXT("Cleanup preserves a different pause owner"), Settings->GetPauserPlayerState(), FirstState);
	TestTrue(TEXT("A round can begin while another pause exists"), Pause.Begin({First, Second}));
	const FGuid ThirdRound = Pause.GetRoundId();
	Pause.NotifyReady(First, ThirdRound, World);
	TestFalse(TEXT("GameOver does not take over an existing pause"), Pause.NotifyReady(Second, ThirdRound, World));
	Pause.Reset(World);
	TestEqual(TEXT("Unowned pause survives GameOver cleanup"), Settings->GetPauserPlayerState(), FirstState);
	Settings->SetPauserPlayerState(nullptr);

	TestTrue(TEXT("A remaining single target can start a round"), Pause.Begin({First}));
	TestTrue(TEXT("A single UI target can pause once ready"), Pause.NotifyReady(First, Pause.GetRoundId(), World));
	Pause.Reset(World);
	TestNull(TEXT("Final cleanup leaves no pause"), Settings->GetPauserPlayerState());
	TestTrue(TEXT("Disconnect before UI readiness can start a round"), Pause.Begin({First, Second}));
	const FGuid DisconnectRound = Pause.GetRoundId();
	TestTrue(TEXT("Disconnect cleanup can identify a participant before native destruction"), Pause.Contains(Second));
	Pause.Reset(World);
	TestFalse(TEXT("The remaining client cannot pause after disconnect cleanup"),
		Pause.NotifyReady(First, DisconnectRound, World));

	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	World->RemoveFromRoot();
	return true;
}

#endif
