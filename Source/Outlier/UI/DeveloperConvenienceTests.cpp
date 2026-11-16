#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/DeveloperConvenienceTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Misc/ScopeExit.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"
#include "UObject/UnrealType.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameStateBase.h"
#include "UI/DeveloperConvenienceWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceDefaultsTest,
	"Outlier.UI.DeveloperConvenience.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDeveloperConvenienceSnapshot Snapshot;
	TestFalse(TEXT("Invincible starts disabled"), Snapshot.Options.bInvincible);
	TestTrue(TEXT("Purchases consume currency by default"), Snapshot.Options.bConsumeCurrency);
	TestFalse(TEXT("Fast cooldown starts disabled"), Snapshot.Options.bFastCooldown);
	TestFalse(TEXT("No menu session before S3 opens it"), Snapshot.SessionId.IsValid());
	TestFalse(TEXT("No client starts as the operator"), Snapshot.bIsOperator);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceStateTest,
	"Outlier.UI.DeveloperConvenience.AuthorityAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("Engine"), GEngine)) return false;
	const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(),
		NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
	if (!TestNotNull(TEXT("World"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	if (!TestNotNull(TEXT("Project mode class"), ModeClass)) return false;
	AOutlierGameMode* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	AFirstPersonPlayerController* Shooter = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Partner = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Stranger = World->SpawnActor<AFirstPersonPlayerController>();
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!TestNotNull(TEXT("Mode"), Mode) || !TestNotNull(TEXT("Shooter"), Shooter)
		|| !TestNotNull(TEXT("Partner"), Partner) || !TestNotNull(TEXT("Stranger"), Stranger)
		|| !TestNotNull(TEXT("Arena"), Arena)) return false;
	const auto AddState = [World](AFirstPersonPlayerController* Controller, EOutlierPlayerRole Role)
	{
		AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
		if (State)
		{
			Controller->PlayerState = State;
			State->SetOwner(Controller);
			State->SetPairId(7);
			State->SetPlayerRole(Role);
		}
		return State;
	};
	AOutlierPlayerState* ShooterState = AddState(Shooter, EOutlierPlayerRole::Shooter);
	AOutlierPlayerState* PartnerState = AddState(Partner, EOutlierPlayerRole::Partner);
	AOutlierPlayerState* StrangerState = AddState(Stranger, EOutlierPlayerRole::Shooter);
	if (!ShooterState || !PartnerState || !StrangerState) return false;
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	TestTrue(TEXT("A match has a stable identity"), Shooter->GetDeveloperConvenienceState().MatchId.IsValid());
	TestTrue(TEXT("Both peers receive the same initial state"),
		Shooter->GetDeveloperConvenienceState() == Partner->GetDeveloperConvenienceState());

	FDeveloperConvenienceOptionRequest Request;
	const auto UseSnapshot = [&Request](const FDeveloperConvenienceSnapshot& Snapshot)
	{
		Request.RequestId = FGuid::NewGuid();
		Request.MatchId = Snapshot.MatchId;
		Request.SessionId = Snapshot.SessionId;
		Request.GameplayGeneration = Snapshot.GameplayGeneration;
	};
	UseSnapshot(Shooter->GetDeveloperConvenienceState());
	TestEqual(TEXT("S2 does not implicitly open a menu"), Mode->RequestDeveloperConvenienceOption(Shooter, Request),
		EDeveloperConvenienceRequestResult::NoSession);

	// S3만 세션을 열게 된다. 테스트는 입력/정지 없이 서버 문맥을 준비해 S2 권한 경계를 검증한다.
	Mode->DeveloperConvenienceState.SessionId = FGuid::NewGuid();
	Mode->DeveloperConvenienceOperator = Shooter;
	Mode->PublishDeveloperConvenienceState();
	UseSnapshot(Shooter->GetDeveloperConvenienceState());
	Request.bEnabled = true;
	TestEqual(TEXT("A same-PairId outsider cannot request changes"), Mode->RequestDeveloperConvenienceOption(Stranger, Request),
		EDeveloperConvenienceRequestResult::InvalidPair);
	TestEqual(TEXT("The waiting peer cannot change options"), Mode->RequestDeveloperConvenienceOption(Partner, Request),
		EDeveloperConvenienceRequestResult::NotOperator);
	TestTrue(TEXT("Only the operator sees its authority"), Shooter->GetDeveloperConvenienceState().bIsOperator);
	TestFalse(TEXT("Waiting peer sees no operator authority"), Partner->GetDeveloperConvenienceState().bIsOperator);
	TestEqual(TEXT("Operator can change an option"), Mode->RequestDeveloperConvenienceOption(Shooter, Request),
		EDeveloperConvenienceRequestResult::Accepted);
	TestTrue(TEXT("Both UI snapshots observe the change"), Partner->GetDeveloperConvenienceState().Options.bInvincible);
	const int64 Revision = Partner->GetDeveloperConvenienceState().Revision;
	TestEqual(TEXT("Repeated desired value is idempotent"), Mode->RequestDeveloperConvenienceOption(Shooter, Request),
		EDeveloperConvenienceRequestResult::Accepted);
	TestEqual(TEXT("No repeated state notification revision"), Partner->GetDeveloperConvenienceState().Revision, Revision);
	TestEqual(TEXT("Same revision reaches both owners"), Shooter->GetDeveloperConvenienceState().Revision, Revision);
	Request.Option = EDeveloperConvenienceOption::ConsumeCurrency;
	Request.bEnabled = false;
	Mode->RequestDeveloperConvenienceOption(Shooter, Request);
	Request.Option = EDeveloperConvenienceOption::FastCooldown;
	Request.bEnabled = true;
	Mode->RequestDeveloperConvenienceOption(Shooter, Request);
	const FDeveloperConvenienceOptions ChangedOptions = Mode->GetDeveloperConvenienceOptions();
	TestFalse(TEXT("Currency option can be disabled"), ChangedOptions.bConsumeCurrency);
	TestTrue(TEXT("Fast cooldown option can be enabled"), ChangedOptions.bFastCooldown);

	FDeveloperConvenienceOptionRequest StaleRequest = Request;
	StaleRequest.MatchId = FGuid::NewGuid();
	TestEqual(TEXT("Old match identity is rejected"), Mode->RequestDeveloperConvenienceOption(Shooter, StaleRequest),
		EDeveloperConvenienceRequestResult::StaleContext);
	StaleRequest = Request;
	StaleRequest.SessionId = FGuid::NewGuid();
	TestEqual(TEXT("Old menu session is rejected"), Mode->RequestDeveloperConvenienceOption(Shooter, StaleRequest),
		EDeveloperConvenienceRequestResult::StaleContext);
	StaleRequest = Request;
	++StaleRequest.GameplayGeneration;
	TestEqual(TEXT("Old gameplay generation is rejected"), Mode->RequestDeveloperConvenienceOption(Shooter, StaleRequest),
		EDeveloperConvenienceRequestResult::StaleContext);
	StaleRequest = Request;
	StaleRequest.Option = static_cast<EDeveloperConvenienceOption>(255);
	TestEqual(TEXT("Invalid wire option is rejected"), Mode->RequestDeveloperConvenienceOption(Shooter, StaleRequest),
		EDeveloperConvenienceRequestResult::InvalidOption);
	Mode->DeveloperConvenienceState.bTransitioning = true;
	TestEqual(TEXT("Transition locks option changes"), Mode->RequestDeveloperConvenienceOption(Shooter, Request),
		EDeveloperConvenienceRequestResult::Transitioning);
	Mode->DeveloperConvenienceState.bTransitioning = false;
	ShooterState->SetPairId(99);
	TestEqual(TEXT("Live Pair membership is rechecked"), Mode->RequestDeveloperConvenienceOption(Shooter, Request),
		EDeveloperConvenienceRequestResult::InvalidPair);
	ShooterState->SetPairId(7);

	Mode->InvalidateDeveloperConvenienceSession();
	TestTrue(TEXT("Closing a session preserves options"), Mode->GetDeveloperConvenienceOptions() == ChangedOptions);
	TestFalse(TEXT("Closing clears both clients' session"), Partner->GetDeveloperConvenienceState().SessionId.IsValid());
	TestEqual(TEXT("A late request cannot reopen a session"), Mode->RequestDeveloperConvenienceOption(Shooter, Request),
		EDeveloperConvenienceRequestResult::NoSession);
	const FGuid MatchId = Mode->DeveloperConvenienceState.MatchId;
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	TestEqual(TEXT("Pair refresh/Pawn relink preserves match identity"), Mode->DeveloperConvenienceState.MatchId, MatchId);
	TestTrue(TEXT("Pawn-independent options survive pair relink"), Mode->GetDeveloperConvenienceOptions() == ChangedOptions);
	// 실제 스트리밍 I/O 대신 이전 세대의 캐시를 준비하고 현재 Arena 세대로 재연결한다.
	Mode->DeveloperConvenienceState.GameplayGeneration = static_cast<int64>(Arena->GetGameplayGeneration()) + 1;
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	TestTrue(TEXT("New generation preserves options"), Mode->GetDeveloperConvenienceOptions() == ChangedOptions);
	TestEqual(TEXT("New generation reaches both owners"), Partner->GetDeveloperConvenienceState().GameplayGeneration,
		static_cast<int64>(Arena->GetGameplayGeneration()));

	// Controller가 교체되어도 옵션은 GameMode에 남는다. 이전 PC는 다시 조작자로 인정하지 않는다.
	AFirstPersonPlayerController* ReconnectedPartner = World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestNotNull(TEXT("Reconnected peer"), ReconnectedPartner)) return false;
	AOutlierPlayerState* ReconnectedState = AddState(ReconnectedPartner, EOutlierPlayerRole::Partner);
	if (!TestNotNull(TEXT("Reconnected state"), ReconnectedState)) return false;
	Mode->RefreshDeveloperConveniencePair(ShooterState, ReconnectedState);
	TestTrue(TEXT("Reconnect receives retained options"), ReconnectedPartner->GetDeveloperConvenienceState().Options == ChangedOptions);
	TestFalse(TEXT("Reconnect does not inherit a menu session"), ReconnectedPartner->GetDeveloperConvenienceState().SessionId.IsValid());
	Mode->ResetDeveloperConvenienceState();
	TestTrue(TEXT("Match end restores defaults"), Mode->GetDeveloperConvenienceOptions() == FDeveloperConvenienceOptions());
	TestFalse(TEXT("Match end clears client match identity"), Shooter->GetDeveloperConvenienceState().MatchId.IsValid());
	Mode->RefreshDeveloperConveniencePair(ShooterState, ReconnectedState);
	TestTrue(TEXT("Reusing PairId starts a distinct match"), Shooter->GetDeveloperConvenienceState().MatchId != MatchId);
	TestTrue(TEXT("The next match starts with defaults"), ReconnectedPartner->GetDeveloperConvenienceState().Options == FDeveloperConvenienceOptions());

	const FProperty* Property = FindFProperty<FProperty>(AFirstPersonPlayerController::StaticClass(), TEXT("DeveloperConvenienceState"));
	TestTrue(TEXT("Snapshot is a replicated property with notification"), Property
		&& Property->HasAllPropertyFlags(CPF_Net | CPF_RepNotify));
	// 결과 RPC는 이전 세션의 응답이어도 조회 상태를 덮어쓰지 않는다.
	const FDeveloperConvenienceSnapshot BeforeResult = Shooter->GetDeveloperConvenienceState();
	Shooter->ClientDeveloperConvenienceOptionResult_Implementation(FDeveloperConvenienceOptionResult());
	TestTrue(TEXT("A late result does not mutate current state"), BeforeResult == Shooter->GetDeveloperConvenienceState());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceSessionTest,
	"Outlier.UI.DeveloperConvenience.Session",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceSessionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	if (!TestNotNull(TEXT("Mode class"), ModeClass)) return false;
	auto* Defaults = ModeClass->GetDefaultObject<AOutlierGameMode>();
	const bool PreviousDirectTest = Defaults->bDirectPairTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
	if (!TestNotNull(TEXT("World"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->DefaultGameMode = ModeClass;
	Defaults->bDirectPairTest = true;
	World->WorldType = EWorldType::PIE;
	ON_SCOPE_EXIT
	{
		Defaults->bDirectPairTest = PreviousDirectTest;
		World->WorldType = EWorldType::Game;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	auto* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	auto* Shooter = World->SpawnActor<AFirstPersonPlayerController>();
	auto* Partner = World->SpawnActor<AFirstPersonPlayerController>();
	if (!Mode || !Shooter || !Partner) return false;
	const auto AddState = [World](AFirstPersonPlayerController* PC, EOutlierPlayerRole Role)
	{
		auto* State = World->SpawnActor<AOutlierPlayerState>();
		State->SetOwner(PC);
		PC->PlayerState = State;
		State->SetPairId(7);
		State->SetPlayerRole(Role);
		PC->Possess(World->SpawnActor<APawn>());
		PC->DeveloperConvenienceWidgetClass = UDeveloperConvenienceWidget::StaticClass();
		PC->DeveloperConvenienceWaitingWidgetClass = UDeveloperConvenienceWidget::StaticClass();
		return State;
	};
	auto* ShooterState = AddState(Shooter, EOutlierPlayerRole::Shooter);
	auto* PartnerState = AddState(Partner, EOutlierPlayerRole::Partner);
	Mode->ActiveMatchPairId = 7;
	Mode->GameState = World->SpawnActor<AGameStateBase>();
	Mode->GameState->AddPlayerState(ShooterState);
	Mode->GameState->AddPlayerState(PartnerState);
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	const auto Request = [](const FDeveloperConvenienceSnapshot& State, bool bOpen)
	{
		FDeveloperConvenienceSessionRequest Result;
		Result.RequestId = FGuid::NewGuid();
		Result.MatchId = State.MatchId;
		Result.SessionId = State.SessionId;
		Result.GameplayGeneration = State.GameplayGeneration;
		Result.Revision = State.Revision;
		Result.bOpen = bOpen;
		return Result;
	};
	for (int32 Role = 0; Role < 2; ++Role)
	{
		auto* Opener = Role == 0 ? Shooter : Partner;
		auto* Peer = Role == 0 ? Partner : Shooter;
		auto Open = Request(Opener->GetDeveloperConvenienceState(), true);
		TestEqual(TEXT("Either role opens"), Mode->RequestDeveloperConvenienceSession(Opener, Open),
			EDeveloperConvenienceRequestResult::Accepted);
		TestTrue(TEXT("World pauses"), World->IsPaused());
		TestTrue(TEXT("First requester is operator"), Opener->GetDeveloperConvenienceState().bIsOperator);
		TestFalse(TEXT("Peer only waits"), Peer->GetDeveloperConvenienceState().bIsOperator);
		TestFalse(TEXT("Settings cannot open over convenience"), Mode->BeginSettingsPause(Peer, Peer, Opener));
		TestEqual(TEXT("Peer cannot close"), Mode->RequestDeveloperConvenienceSession(Peer,
			Request(Peer->GetDeveloperConvenienceState(), false)), EDeveloperConvenienceRequestResult::NotOperator);
		const FGuid Session = Opener->GetDeveloperConvenienceState().SessionId;
		Mode->RequestDeveloperConvenienceSession(Peer, Open);
		TestEqual(TEXT("Concurrent open cannot replace session"), Opener->GetDeveloperConvenienceState().SessionId, Session);
		auto Close = Request(Opener->GetDeveloperConvenienceState(), false);
		TestEqual(TEXT("Operator closes"), Mode->RequestDeveloperConvenienceSession(Opener, Close),
			EDeveloperConvenienceRequestResult::Accepted);
		TestFalse(TEXT("World resumes"), World->IsPaused());
		TestFalse(TEXT("Both leave the session"), Peer->GetDeveloperConvenienceState().SessionId.IsValid());
		Mode->RequestDeveloperConvenienceSession(Opener, Request(Opener->GetDeveloperConvenienceState(), true));
		TestEqual(TEXT("Late close cannot close new session"), Mode->RequestDeveloperConvenienceSession(Opener, Close),
			EDeveloperConvenienceRequestResult::StaleContext);
		Mode->OnDeveloperConvenienceUIUnavailable(Peer, Peer->GetDeveloperConvenienceState().SessionId);
		TestFalse(TEXT("Failed peer UI releases pause"), World->IsPaused());
	}
	Mode->RequestDeveloperConvenienceSession(Shooter, Request(Shooter->GetDeveloperConvenienceState(), true));
	World->GetWorldSettings()->SetPauserPlayerState(Partner->PlayerState);
	Mode->InvalidateDeveloperConvenienceSession();
	TestEqual(TEXT("Foreign pause survives cleanup"), World->GetWorldSettings()->GetPauserPlayerState(), Partner->PlayerState.Get());
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Mode->RequestDeveloperConvenienceSession(Shooter, Request(Shooter->GetDeveloperConvenienceState(), true));
	Mode->CancelGameOverPendingForDisconnect(Partner);
	TestFalse(TEXT("Disconnect releases pause"), World->IsPaused());
	Mode->bArenaReloadInProgress = true;
	TestEqual(TEXT("Reload rejects open"), Mode->RequestDeveloperConvenienceSession(Shooter,
		Request(Shooter->GetDeveloperConvenienceState(), true)), EDeveloperConvenienceRequestResult::Transitioning);
	Mode->bArenaReloadInProgress = false;
	ShooterState->BeginStatAllocatorSession(PartnerState);
	TestEqual(TEXT("Active upgrade session rejects menu entry"), Mode->RequestDeveloperConvenienceSession(Shooter,
		Request(Shooter->GetDeveloperConvenienceState(), true)), EDeveloperConvenienceRequestResult::Transitioning);
	const uint32 UpgradeSession = ShooterState->GetStatAllocatorSessionId();
	const uint32 Generation = static_cast<uint32>(Shooter->GetDeveloperConvenienceState().GameplayGeneration);
	ShooterState->RequestStatAllocatorExitPending(UpgradeSession, Generation, true);
	PartnerState->RequestStatAllocatorExitPending(UpgradeSession, Generation, true);
	TestFalse(TEXT("Finalized upgrade is no longer active"), ShooterState->IsStatAllocatorUIOpenForGeneration(Generation));
	ShooterState->BeginStatAllocatorSession(PartnerState);
	TestTrue(TEXT("Reopened upgrade in the same generation stays protected"), ShooterState->IsStatAllocatorUIOpenForGeneration(Generation));
	ShooterState->RequestStatAllocatorExitPending(ShooterState->GetStatAllocatorSessionId(), Generation, true);
	PartnerState->RequestStatAllocatorExitPending(PartnerState->GetStatAllocatorSessionId(), Generation, true);
	Partner->DeveloperConvenienceWaitingWidgetClass = nullptr;
	TestEqual(TEXT("Missing class rejects without pause"), Mode->RequestDeveloperConvenienceSession(Shooter,
		Request(Shooter->GetDeveloperConvenienceState(), true)), EDeveloperConvenienceRequestResult::UIUnavailable);
	TestFalse(TEXT("Missing class leaves world running"), World->IsPaused());
	return true;
}

#endif
