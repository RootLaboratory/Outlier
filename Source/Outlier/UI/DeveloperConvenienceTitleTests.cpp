#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "OutlierGameInstance.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceTitleTest,
	"Outlier.UI.DeveloperConvenience.TitleAuthorityAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceTitleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Title request world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	auto* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	auto* Shooter = World->SpawnActor<AFirstPersonPlayerController>();
	auto* Partner = World->SpawnActor<AFirstPersonPlayerController>();
	auto* ShooterState = World->SpawnActor<AOutlierPlayerState>();
	auto* PartnerState = World->SpawnActor<AOutlierPlayerState>();
	if (!TestTrue(TEXT("Pair fixture"), Mode && Shooter && Partner && ShooterState && PartnerState))
	{
		return false;
	}
	Shooter->PlayerState = ShooterState;
	Partner->PlayerState = PartnerState;
	ShooterState->SetOwner(Shooter);
	PartnerState->SetOwner(Partner);
	ShooterState->SetPairId(7);
	PartnerState->SetPairId(7);
	ShooterState->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PartnerState->SetPlayerRole(EOutlierPlayerRole::Partner);
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	Mode->DeveloperConvenienceState.SessionId = FGuid::NewGuid();
	Mode->DeveloperConvenienceOperator = Shooter;
	Mode->DeveloperConvenienceState.Options.bInvincible = true;
	Mode->DeveloperConvenienceState.Options.bConsumeCurrency = false;
	Mode->DeveloperConvenienceState.Options.bFastCooldown = true;
	FDeveloperConvenienceTitleRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.MatchId = Mode->DeveloperConvenienceState.MatchId;
	Request.SessionId = Mode->DeveloperConvenienceState.SessionId;
	Request.GameplayGeneration = Mode->DeveloperConvenienceState.GameplayGeneration;
	TestEqual(TEXT("Waiting peer cannot request title"), Mode->RequestDeveloperConvenienceReturnToTitle(Partner, Request),
		EDeveloperConvenienceRequestResult::NotOperator);
	++Request.GameplayGeneration;
	TestEqual(TEXT("Previous generation cannot end the match"), Mode->RequestDeveloperConvenienceReturnToTitle(Shooter, Request),
		EDeveloperConvenienceRequestResult::StaleContext);
	--Request.GameplayGeneration;
	const FGuid SessionId = Request.SessionId;
	Request.SessionId = FGuid::NewGuid();
	TestEqual(TEXT("Previous menu cannot end the match"), Mode->RequestDeveloperConvenienceReturnToTitle(Shooter, Request),
		EDeveloperConvenienceRequestResult::StaleContext);
	Request.SessionId = SessionId;
	TestEqual(TEXT("Operator can end without a vote"), Mode->RequestDeveloperConvenienceReturnToTitle(Shooter, Request),
		EDeveloperConvenienceRequestResult::Accepted);
	TestTrue(TEXT("Accepted exit locks the menu immediately"), Mode->DeveloperConvenienceState.bTransitioning);
	TestEqual(TEXT("Duplicate request cannot schedule another exit"), Mode->RequestDeveloperConvenienceReturnToTitle(Shooter, Request),
		EDeveloperConvenienceRequestResult::Transitioning);
	TestFalse(TEXT("Travel waits until the response can be sent"), Mode->bListenHostReturnRequested);
	// GameSession이 없는 격리 월드에서 실제 종료 함수를 실행하고 맵 I/O만 제외한다.
	++GFrameCounter;
	World->GetTimerManager().Tick(0.01f);
	TestTrue(TEXT("Accepted request executes the existing standalone exit"), Mode->bListenHostReturnRequested);
	TestFalse(TEXT("Title clears the menu session"), Mode->DeveloperConvenienceState.SessionId.IsValid());
	TestTrue(TEXT("Title restores default options"), Mode->DeveloperConvenienceState.Options == FDeveloperConvenienceOptions());
	TestFalse(TEXT("Title consumes the queued exit"), Mode->PendingDeveloperTitleRequestId.IsValid());
	TestFalse(TEXT("Title clears pending upgrade presentation"), Mode->PendingPresetUpgradeRequestId.IsValid());
#if WITH_EDITOR
	// 실제 소켓 없이 일반 Dedicated 분기도 실행한다. 서버는 살아 있고 클라이언트만 복귀해야 한다.
	UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
	if (!TestNotNull(TEXT("Dedicated test driver class"), DriverClass))
	{
		return false;
	}
	auto* Driver = NewObject<UNetDriver>(World, DriverClass);
	Driver->SetWorld(World);
	World->SetNetDriver(Driver);
	GEngine->GetWorldContextFromWorldChecked(World).RunAsDedicated = true;
	ON_SCOPE_EXIT
	{
		World->SetNetDriver(nullptr);
		Driver->SetWorld(nullptr);
	};
	TestEqual(TEXT("Fixture exercises ordinary dedicated mode"), Mode->GetNetMode(), NM_DedicatedServer);
	Mode->bListenHostReturnRequested = false;
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	Mode->DeveloperConvenienceState.SessionId = FGuid::NewGuid();
	Mode->DeveloperConvenienceOperator = Partner;
	Request.RequestId = FGuid::NewGuid();
	Request.MatchId = Mode->DeveloperConvenienceState.MatchId;
	Request.SessionId = Mode->DeveloperConvenienceState.SessionId;
	Request.GameplayGeneration = Mode->DeveloperConvenienceState.GameplayGeneration;
	TestEqual(TEXT("Partner operator can end dedicated match"), Mode->RequestDeveloperConvenienceReturnToTitle(Partner, Request),
		EDeveloperConvenienceRequestResult::Accepted);
	++GFrameCounter;
	World->GetTimerManager().Tick(0.01f);
	TestFalse(TEXT("Dedicated does not stop its host session"), Mode->bListenHostReturnRequested);
	TestFalse(TEXT("Dedicated clears the menu"), Mode->DeveloperConvenienceState.SessionId.IsValid());
	TestEqual(TEXT("Dedicated releases the active pair"), Mode->ActiveMatchPairId, INDEX_NONE);
	TestFalse(TEXT("Dedicated preserves existing generic-leave rejection"), Mode->HandleExplicitPlayerLeave(Partner));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceTitleTravelTest,
	"Outlier.UI.DeveloperConvenience.TitleTravelLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceTitleTravelTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Travel world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	auto* Instance = NewObject<UOutlierGameInstance>(GEngine);
	Instance->WorldContext = &Context;
	Context.OwningGameInstance = Instance;
	World->SetGameInstance(Instance);
	ON_SCOPE_EXIT
	{
		Instance->PrepareForExplicitLeave();
		Instance->WorldContext = nullptr;
		World->DestroyWorld(false);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	Instance->LastArenaHandoffUrl = TEXT("127.0.0.1:7777?ArenaTicket=old");
	Instance->LastListenReconnectUrl = TEXT("127.0.0.1:7777?ListenReconnect=old");
	Instance->bArenaHandoffActive = true;
	Instance->bArenaReconnectActive = true;
	Instance->bQuitAfterExplicitLeave = true;
	Instance->PrepareForTitleReturn(true);
	TestTrue(TEXT("Worker/lobby travel keeps final title intent"), Instance->bTitleReturnPending);
	TestTrue(TEXT("Explicit title clears both reconnect destinations"),
		Instance->LastArenaHandoffUrl.IsEmpty() && Instance->LastListenReconnectUrl.IsEmpty());
	TestFalse(TEXT("Title cannot become application quit"), Instance->bQuitAfterExplicitLeave);
	TestFalse(TEXT("Intentional disconnect does not reconnect"), Instance->bArenaReconnectActive);
	TestTrue(TEXT("Command-line Connect cannot rejoin after title"), Instance->bTriedConnect);
	TestTrue(TEXT("Waiting for server travel has a watchdog"), Instance->ArenaReconnectTickerHandle.IsValid());
	const double Deadline = Instance->TitleReturnDeadlineSeconds;
	Instance->PrepareForTitleReturn(true);
	TestEqual(TEXT("Duplicate RPC cannot extend the timeout"), Instance->TitleReturnDeadlineSeconds, Deadline);
	// 실제 엔진 복귀 경로가 예약한 URL만 확인한다. 테스트 중 맵을 교체하지 않는다.
	Instance->HandlePostLoadMap(World);
	TestTrue(TEXT("Lobby arrival requests final local title"), Instance->bTitleReturnTravelRequested);
	TestEqual(TEXT("Uses engine disconnect/default-map travel"), Context.TravelURL, FString(TEXT("?closed")));
	TestFalse(TEXT("No reconnect ticker remains after title travel starts"), Instance->ArenaReconnectTickerHandle.IsValid());
	Context.TravelURL = TEXT("already queued");
	Instance->HandleNetworkFailure(World, nullptr, ENetworkFailure::ConnectionLost, TEXT("late disconnect"));
	Instance->HandleTravelFailure(World, ETravelFailure::ClientTravelFailure, TEXT("late lobby failure"));
	TestEqual(TEXT("Late failures cannot queue duplicate title travel"), Context.TravelURL, FString(TEXT("already queued")));

	Instance->PrepareForExplicitLeave();
	Instance->PrepareForTitleReturn(true);
	Instance->HandleNetworkFailure(World, nullptr, ENetworkFailure::ConnectionLost, TEXT("server exited first"));
	TestEqual(TEXT("Disconnect before lobby falls back to title"), Context.TravelURL, FString(TEXT("?closed")));
	Instance->PrepareForExplicitLeave();
	Instance->PrepareForTitleReturn(false);
	TestTrue(TEXT("Ordinary dedicated clients request title immediately"), Instance->bTitleReturnTravelRequested);
	Instance->PrepareForExplicitLeave();
	Instance->PrepareForTitleReturn(true);
	FTSTicker::GetCoreTicker().RemoveTicker(Instance->ArenaReconnectTickerHandle);
	Instance->TitleReturnDeadlineSeconds = FPlatformTime::Seconds() - 1.0;
	TestFalse(TEXT("Missing server travel ends the watchdog"), Instance->HandleArenaReconnectTick(0.0f));
	TestTrue(TEXT("Timeout falls back to title"), Instance->bTitleReturnTravelRequested);
	FString DefaultMap;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), DefaultMap, GEngineIni);
	const FString TitlePackageName = FSoftObjectPath(DefaultMap).GetLongPackageName();
	if (!TestFalse(TEXT("Default title destination is configured"), TitlePackageName.IsEmpty()))
	{
		return false;
	}
	UWorld* TitleWorld = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, CreatePackage(*TitlePackageName));
	if (!TestNotNull(TEXT("Title arrival world"), TitleWorld))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		TitleWorld->DestroyWorld(false);
		TitleWorld->SetPhysicsScene(nullptr);
		TitleWorld->RemoveFromRoot();
	};
	TitleWorld->SetGameInstance(Instance);
	Instance->HandlePostLoadMap(TitleWorld);
	TestFalse(TEXT("Final title arrival consumes the intent"), Instance->bTitleReturnPending);
	TestFalse(TEXT("Final title arrival clears travel state"), Instance->bTitleReturnTravelRequested);
	TestTrue(TEXT("Title arrival keeps Connect auto-join disabled"), Instance->bTriedConnect);
	Instance->PrepareForTitleReturn(true);
	Instance->PrepareForExplicitLeave();
	TestFalse(TEXT("Other explicit leave clears title intent"), Instance->bTitleReturnPending);
	TestFalse(TEXT("Other explicit leave clears title travel state"), Instance->bTitleReturnTravelRequested);
	return true;
}

#endif
