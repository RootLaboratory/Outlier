#if WITH_DEV_AUTOMATION_TESTS

#include "Cinematic/OutlierCinematicSubsystem.h"
#include "OutlierEditor/Tests/GameOverPauseTestLocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Slate/SGameLayerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierInitialIntroBlackoutRecoveryTest,
	"Outlier.Cinematic.InitialIntro.BlackoutRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierInitialIntroBlackoutRecoveryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); World->RemoveFromRoot(); };
	UGameOverPauseTestLocalPlayer* Local = NewObject<UGameOverPauseTestLocalPlayer>(GEngine);
	Local->TestWorld = World;
	Local->ViewportClient = NewObject<UGameViewportClient>(GEngine);
	// 실제 플레이어별 Slate 슬롯에 붙인다. 카메라나 Controller가 없어도 화면 가림이 살아야 한다.
	TSharedRef<SGameLayerManager> Layers = SNew(SGameLayerManager);
	Local->ViewportClient->SetGameLayerManager(Layers);
	UOutlierCinematicSubsystem* Intro = NewObject<UOutlierCinematicSubsystem>(Local);
	Intro->BeginInitialIntroBlackout();
	TestTrue(TEXT("Blackout attaches without a controller or camera"), Intro->HasInitialIntroBlackout());
	const TSharedPtr<SWidget> FirstCover = Intro->InitialIntroBlackoutWidget;
	Intro->BeginInitialIntroBlackout();
	TestTrue(TEXT("Repeated begin keeps the existing viewport slot"), FirstCover == Intro->InitialIntroBlackoutWidget);
	Intro->ClearLevelCinematics();
	TestFalse(TEXT("Cancel removes the viewport cover"), Intro->HasInitialIntroBlackout());
	TestFalse(TEXT("Cancel actually detaches the Slate widget"), FirstCover->GetParentWidget().IsValid());
	Intro->ClearLevelCinematics();
	TestFalse(TEXT("Repeated cancel stays cleared"), Intro->HasInitialIntroBlackout());

	Intro->BeginInitialIntroBlackout();
	AddExpectedError(TEXT("blackout recovery test"), EAutomationExpectedErrorFlags::Contains, 1);
	Intro->FailCinematic(TEXT("blackout recovery test"));
	TestFalse(TEXT("Failure removes cover even when its controller disappeared"), Intro->HasInitialIntroBlackout());

	Intro->BeginInitialIntroBlackout();
	Intro->State = EOutlierCinematicState::Playing;
	Intro->HandleSequenceFinished();
	TestFalse(TEXT("Sequence finishing before its next tick cannot leave a cover"), Intro->HasInitialIntroBlackout());
	Intro->ClearLevelCinematics();

	Intro->BeginInitialIntroBlackout();
	Intro->ActiveIntroId = FGuid::NewGuid();
	Intro->ReleaseCinematic(FGuid::NewGuid());
	TestTrue(TEXT("Stale release cannot clear a newer transition"), Intro->HasInitialIntroBlackout());
	Intro->HandleWorldCleanup(NewObject<UWorld>(), false, false);
	TestTrue(TEXT("Unrelated world cleanup cannot clear this transition"), Intro->HasInitialIntroBlackout());
	Intro->HandleWorldCleanup(World, false, false);
	TestFalse(TEXT("Owning world cleanup removes the cover"), Intro->HasInitialIntroBlackout());
	Intro->ClearLevelCinematics();

	// Shipping의 로비→Worker 이동은 같은 월드의 Controller 교체와 다르다.
	// 타이틀 시퀀스가 바인딩된 경우에도 이전 월드 정리가 화면 가림을 지우면 안 된다.
	Intro->BeginInitialIntroBlackout();
	Intro->BeginInitialIntroBlackout(true);
	const TSharedPtr<SWidget> TravelCover = Intro->InitialIntroBlackoutWidget;
	Intro->BoundWorld = World;
	Intro->HandleWorldCleanup(World, false, false);
	TestTrue(TEXT("Lobby cleanup preserves the handoff cover"), Intro->HasInitialIntroBlackout());
	TestTrue(TEXT("Handoff retains the same attached widget"), TravelCover == Intro->InitialIntroBlackoutWidget);
	TestFalse(TEXT("Lobby sequence bindings are still cleaned"), Intro->BoundWorld.IsValid());
	Intro->ClearLevelCinematics();
	TestFalse(TEXT("Explicit cancellation still clears a handoff cover"), Intro->HasInitialIntroBlackout());
	TestFalse(TEXT("Cancellation resets travel ownership"), Intro->bInitialIntroBlackoutAcrossTravel);
	Local->TestWorld = nullptr;
	return true;
}

#endif
