#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/GameOverPauseTestLocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierGameOverPostProcessPauseTest,
	"Outlier.RDG.GameOver.WorldPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGameOverPostProcessPauseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("Engine outer is available"), GEngine)) return false;
	// 실제 월드 Outer에서 고유성을 확인한다. 이름 충돌은 기존 Scene의 강제 파괴를 유발한다.
	const FName Name = MakeUniqueObjectName(
		GetTransientPackage(), UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
	if (!TestNotNull(TEXT("Post process test world"), World)) return false;
	World->AddToRoot();
	APlayerState* Owner = World->SpawnActor<APlayerState>();
	AWorldSettings* Settings = World->GetWorldSettings();
	if (!TestNotNull(TEXT("Native pause owner"), Owner) || !TestNotNull(TEXT("World settings"), Settings))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		World->RemoveFromRoot();
		return false;
	}
	// LocalPlayer는 Within=Engine이므로 테스트용 파생 클래스도 Engine을 Outer로 사용한다.
	UGameOverPauseTestLocalPlayer* Player = NewObject<UGameOverPauseTestLocalPlayer>(GEngine);
	Player->TestWorld = World;
	ULocalPlayerPostProcessSubsystem* PostProcess = NewObject<ULocalPlayerPostProcessSubsystem>(Player);
	PostProcess->SetOverlayGoalValue(1.0f);
	PostProcess->SetOverlayEnabled(true);
	PostProcess->Tick(0.1f);
	const float OverlayBeforePause = PostProcess->GetUIPostProcessStrcture().Overlay.AccumulatedValue;
	TestTrue(TEXT("Normal effect advances before pause"), OverlayBeforePause > 0.0f);
	Settings->SetPauserPlayerState(Owner);
	TestTrue(TEXT("The test uses actual native world pause"), World->IsPaused());
	PostProcess->Tick(0.1f);
	TestEqual(TEXT("Normal effect stays frozen during pause"),
		PostProcess->GetUIPostProcessStrcture().Overlay.AccumulatedValue, OverlayBeforePause);

	// 리소스 준비 여부는 렌더링 검증 대상이다. 여기서는 동일 타임라인을 직접 시작해 Tick 분기만 검증한다.
	PostProcess->DeathTransition.Start(PostProcess->PostProcessParameters, PostProcess->UIPostProcessParameters);
	const float FadeTime = PostProcess->PostProcessParameters.DeathFade.ElapsedTime;
	PostProcess->Tick(0.1f);
	TestTrue(TEXT("Death presentation advances while the world is paused"),
		PostProcess->PostProcessParameters.DeathFade.ElapsedTime > FadeTime);
	TestTrue(TEXT("Death noise advances while the world is paused"),
		PostProcess->PostProcessParameters.DeathNoise.Time > 0.0f);
	TestEqual(TEXT("Death presentation does not advance normal effects"),
		PostProcess->GetUIPostProcessStrcture().Overlay.AccumulatedValue, OverlayBeforePause);
	PostProcess->StartGameOverBlackout();
	PostProcess->Tick(0.1f);
	TestEqual(TEXT("Transition blackout stays fully covered during pause"),
		PostProcess->GetPostProcessStrcture().ZoomBlur.BlackFlushAlpha, 1.0f);
	PostProcess->ResetAllPostProcess();
	PostProcess->SetOverlayGoalValue(1.0f);
	PostProcess->SetOverlayEnabled(true);
	Settings->SetPauserPlayerState(nullptr);
	PostProcess->Tick(0.1f);
	TestTrue(TEXT("Normal effects resume after pause release"),
		PostProcess->GetUIPostProcessStrcture().Overlay.AccumulatedValue > 0.0f);
	Player->TestWorld = nullptr;
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	World->RemoveFromRoot();
	return true;
}

#endif
