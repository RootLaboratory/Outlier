#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "OutlierGameMode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierSettingsPauseTest,
	"Outlier.UI.InGameSettings.PairPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSettingsPauseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("Engine"), GEngine)) return false;
	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
		NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	// 기존 GameOver 테스트와 동일하게 실제 프로젝트 파생 GameMode를 사용한다.
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	if (!TestNotNull(TEXT("Project game mode"), ModeClass)) return false;
	AOutlierGameMode* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	AFirstPersonPlayerController* First = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Second = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Stranger = World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestNotNull(TEXT("Game mode"), Mode)
		|| !TestNotNull(TEXT("First controller"), First)
		|| !TestNotNull(TEXT("Second controller"), Second)
		|| !TestNotNull(TEXT("Unrelated controller"), Stranger)) return false;
	First->PlayerState = World->SpawnActor<APlayerState>();
	Second->PlayerState = World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("First player state"), First->PlayerState.Get())
		|| !TestNotNull(TEXT("Second player state"), Second->PlayerState.Get())) return false;

	// 시작 순서를 바꿔도 최초 Pauser가 먼저 닫았다는 이유로 게임이 재개되지 않아야 한다.
	for (int32 Round = 0; Round < 2; ++Round)
	{
		AFirstPersonPlayerController* Opener = Round == 0 ? First : Second;
		AFirstPersonPlayerController* Peer = Round == 0 ? Second : First;
		TestTrue(TEXT("First menu starts the pause"), Mode->BeginSettingsPause(Opener, Opener, Peer));
		TestTrue(TEXT("World pauses"), World->IsPaused());
		TestEqual(TEXT("Only the opener uses settings"), Mode->SettingsMenuUsers.Num(), 1);
		Mode->EndSettingsPause(Peer);
		TestTrue(TEXT("Closing a waiting screen cannot resume"), World->IsPaused());
		TestFalse(TEXT("Unrelated player cannot join"), Mode->BeginSettingsPause(Stranger, Stranger, Opener));
		TestTrue(TEXT("Peer opens settings during pause"), Mode->BeginSettingsPause(Peer, Peer, Opener));
		TestFalse(TEXT("Duplicate open is ignored"), Mode->BeginSettingsPause(Peer, Peer, Opener));
		TestEqual(TEXT("Both menus are tracked once"), Mode->SettingsMenuUsers.Num(), 2);
		TestEqual(TEXT("Layer owner remains stable"), Mode->SettingsPauseLayerOwner.Get(), static_cast<AActor*>(Opener));

		Mode->EndSettingsPause(Opener);
		TestTrue(TEXT("Closing the original menu keeps the world paused"), World->IsPaused());
		TestFalse(TEXT("Original opener now waits"), Mode->SettingsMenuUsers.Contains(Opener));
		TestTrue(TEXT("Peer keeps using settings"), Mode->SettingsMenuUsers.Contains(Peer));
		Mode->EndSettingsPause(Opener);
		Mode->EndSettingsPause(Stranger);
		TestTrue(TEXT("Duplicate and unrelated closes retain pause"), World->IsPaused());
		TestTrue(TEXT("Waiting player can reopen"), Mode->BeginSettingsPause(Opener, Opener, Peer));
		Mode->EndSettingsPause(Peer);
		TestTrue(TEXT("Closing the second menu retains reopened settings"), World->IsPaused());
		Mode->EndSettingsPause(Opener);
		TestFalse(TEXT("Last close resumes the world"), World->IsPaused());
		TestTrue(TEXT("Last close clears users"), Mode->SettingsMenuUsers.IsEmpty());
		TestFalse(TEXT("Last close clears layer owner"), Mode->SettingsPauseLayerOwner.IsValid());
		Mode->EndSettingsPause(Opener);
		TestFalse(TEXT("Late close leaves world running"), World->IsPaused());
	}

	TestTrue(TEXT("Single-menu round opens"), Mode->BeginSettingsPause(First, First, Second));
	Mode->EndSettingsPause(First);
	TestFalse(TEXT("Only menu closing resumes immediately"), World->IsPaused());
	TestTrue(TEXT("Cleanup round opens"), Mode->BeginSettingsPause(First, First, Second));
	Mode->BeginSettingsPause(Second, Second, First);
	Mode->CancelGameOverPendingForDisconnect(Second);
	TestFalse(TEXT("Disconnect releases settings pause"), World->IsPaused());
	TestTrue(TEXT("Disconnect clears menu users"), Mode->SettingsMenuUsers.IsEmpty());

	// 다른 정지 사유가 소유한 Pauser는 설정 종료로 해제하지 않는다.
	TestTrue(TEXT("Foreign pause round opens"), Mode->BeginSettingsPause(First, First, Second));
	World->GetWorldSettings()->SetPauserPlayerState(Second->PlayerState);
	Mode->EndSettingsPause(First);
	TestEqual(TEXT("Foreign pause owner is preserved"),
		World->GetWorldSettings()->GetPauserPlayerState(), Second->PlayerState.Get());
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Mode->bArenaReloadInProgress = true;
	TestFalse(TEXT("Reload rejects settings entry"), Mode->BeginSettingsPause(First, First, Second));
	Mode->bArenaReloadInProgress = false;
	return true;
}

#endif
