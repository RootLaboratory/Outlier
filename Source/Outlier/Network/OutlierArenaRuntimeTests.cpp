#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Network/OutlierMatchRequest.h"
#include "OutlierArenaSettings.h"
#include "OutlierGameInstance.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#endif

namespace
{
FOutlierArenaHandoffRequest MakeHandoffRequest(
	const FGuid& MatchId,
	const FGuid& PlayerId,
	EOutlierPlayerRole Role)
{
	FOutlierArenaHandoffRequest Request;
	Request.MatchId = MatchId;
	Request.PlayerId = PlayerId;
	Request.Role = Role;
	return Request;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaGameplayGenerationContractTest,
	"Outlier.Network.SingleArena.GameplayGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaGameplayGenerationContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("The next generation is accepted"),
		UOutlierArenaSubsystem::IsGameplayGenerationNewer(2, 1));
	TestFalse(TEXT("A duplicate generation is rejected"),
		UOutlierArenaSubsystem::IsGameplayGenerationNewer(2, 2));
	TestFalse(TEXT("A delayed previous generation is rejected"),
		UOutlierArenaSubsystem::IsGameplayGenerationNewer(1, 2));
	TestFalse(TEXT("Generation zero is reserved as invalid"),
		UOutlierArenaSubsystem::IsGameplayGenerationNewer(0, 2));
	TestTrue(TEXT("Generation rollover remains ordered"),
		UOutlierArenaSubsystem::IsGameplayGenerationNewer(1, MAX_uint32));
	TestFalse(TEXT("A reload has not stalled before the timeout boundary"),
		UOutlierArenaSubsystem::HasGameplayReloadTimedOut(14.999, 15.0));
	TestTrue(TEXT("A reload stalls at the timeout boundary"),
		UOutlierArenaSubsystem::HasGameplayReloadTimedOut(15.0, 15.0));
	TestFalse(TEXT("A disabled timeout cannot report a stall"),
		UOutlierArenaSubsystem::HasGameplayReloadTimedOut(60.0, 0.0));

	TArray<TWeakObjectPtr<AActor>> OldActors;
	OldActors.Add(AActor::StaticClass()->GetDefaultObject<AActor>());
	TestFalse(TEXT("A live old Actor blocks Ready even after level visibility returns"),
		UOutlierArenaSubsystem::CanCompleteGameplayReload(OldActors, true, true, true));
	OldActors.Reset();
	TestFalse(TEXT("A missing unload completion blocks Ready"),
		UOutlierArenaSubsystem::CanCompleteGameplayReload(OldActors, false, true, true));
	TestFalse(TEXT("Missing client unload ACKs block Ready"),
		UOutlierArenaSubsystem::CanCompleteGameplayReload(OldActors, true, false, true));
	TestFalse(TEXT("Gameplay levels must be shown before Ready"),
		UOutlierArenaSubsystem::CanCompleteGameplayReload(OldActors, true, true, false));
	TestTrue(TEXT("Ready requires purge, unload, ACK authorization and level visibility"),
		UOutlierArenaSubsystem::CanCompleteGameplayReload(OldActors, true, true, true));

	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaMapContractTest,
	"Outlier.Network.ArenaWorker.MapContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaMapContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UOutlierArenaSettings* Settings = NewObject<UOutlierArenaSettings>();
	if (!TestNotNull(TEXT("Arena settings can be created for the contract test"), Settings))
	{
		return false;
	}

	Settings->ArenaLevel = TSoftObjectPtr<UWorld>(
		FSoftObjectPath(TEXT("/Game/Maps/TestArena.TestArena")));

	TestEqual(
		TEXT("The configured soft object resolves to a map package name"),
		Settings->GetArenaPackageName(),
		FString(TEXT("/Game/Maps/TestArena")));
	TestTrue(
		TEXT("The configured persistent map matches the arena"),
		Settings->MatchesArenaPackageName(TEXT("/Game/Maps/TestArena")));
	TestTrue(
		TEXT("A PIE-prefixed package still matches the configured arena"),
		Settings->MatchesArenaPackageName(TEXT("/Game/Maps/UEDPIE_3_TestArena")));
	TestFalse(
		TEXT("A different persistent map is not the configured arena"),
		Settings->MatchesArenaPackageName(TEXT("/Game/Maps/OtherArena")));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaGenerationTransitionTest,
	"Outlier.Network.SingleArena.GenerationTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaGenerationTransitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("GenerationTransition")));
	if (!TestNotNull(TEXT("Isolated server world exists"), World)) return false;
	World->AddToRoot();
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	FObjectPropertyBase* LoadedLevelProperty = FindFProperty<FObjectPropertyBase>(
		ULevelStreaming::StaticClass(), TEXT("LoadedLevel"));
	if (!TestNotNull(TEXT("Arena subsystem exists"), Arena)
		|| !TestNotNull(TEXT("Streaming fixture can supply loaded levels"), LoadedLevelProperty))
	{
		World->DestroyWorld(false);
		World->RemoveFromRoot();
		return false;
	}

	// 스트리밍 I/O만 격리하고 예약/확정/실패 및 GameMode 진입은 실제 함수를 사용한다.
	ULevelStreamingDynamic* Base = NewObject<ULevelStreamingDynamic>(World);
	ULevelStreamingDynamic* Gameplay = NewObject<ULevelStreamingDynamic>(World);
	ULevel* GameplayLevel = NewObject<ULevel>(World);
	GameplayLevel->bIsVisible = true;
	LoadedLevelProperty->SetObjectPropertyValue_InContainer(Base, World->PersistentLevel);
	LoadedLevelProperty->SetObjectPropertyValue_InContainer(Gameplay, GameplayLevel);
	Arena->Arena.StreamingLevel = Base;
	Arena->GameplayStreamingLevels = { Gameplay };
	Arena->GameplaySublevels = { TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Tests/GenerationGameplay.GenerationGameplay"))) };
	Arena->bGameplayLevelsResolved = true;
	Arena->bInitialGameplayLevelsRequested = true;
	int32 Starts = 0;
	int32 Failures = 0;
	uint32 ObservedGeneration = 0;
	Arena->OnArenaGameplayReloadStarted.AddLambda([&](uint32 Generation)
	{
		++Starts;
		ObservedGeneration = Generation;
		TestEqual(TEXT("Listeners see the committed generation"), Arena->GetGameplayGeneration(), Generation);
	});
	Arena->OnArenaGameplayReloadFailed.AddLambda([&](uint32, EOutlierGameplayReloadFailure) { ++Failures; });

	const uint32 Cancelled = Arena->ReserveGameplayGeneration();
	TestTrue(TEXT("Ready world can reserve"), Cancelled != 0);
	TestEqual(TEXT("Reservation preserves the live generation"), Arena->GetGameplayGeneration(), 0u);
	TestEqual(TEXT("Reservation does not notify consumers"), Starts, 0);
	TestEqual(TEXT("Overlapping reservation is rejected"), Arena->ReserveGameplayGeneration(), 0u);
	Arena->CancelGameplayGeneration(Cancelled);
	TestFalse(TEXT("Cancelled reservation cannot commit"), Arena->CommitGameplayGeneration(Cancelled));

	// 실제 페어 진입의 veto가 저장 적용과 세대 전환 전에 끝나는지 검증한다.
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	AOutlierGameMode* Mode = ModeClass ? World->SpawnActor<AOutlierGameMode>(ModeClass) : nullptr;
	AOutlierPlayerState* ShooterState = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerState = World->SpawnActor<AOutlierPlayerState>();
	AFirstPersonPlayerController* ShooterController = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* PartnerController = World->SpawnActor<AFirstPersonPlayerController>();
	if (TestNotNull(TEXT("Production GameMode exists"), Mode)
		&& TestNotNull(TEXT("Shooter state exists"), ShooterState)
		&& TestNotNull(TEXT("Partner state exists"), PartnerState)
		&& TestNotNull(TEXT("Shooter controller exists"), ShooterController)
		&& TestNotNull(TEXT("Partner controller exists"), PartnerController))
	{
		Mode->ShooterClass = AShooterCharacter::StaticClass();
		Mode->PartnerClass = APartnerCharacter::StaticClass();
		ShooterState->SetOwner(ShooterController);
		PartnerState->SetOwner(PartnerController);
		TestFalse(TEXT("Abstract pawn class is rejected before reservation"),
			Mode->ReloadArenaAndRespawnPair(ShooterState, PartnerState,
				FTransform::Identity, FTransform::Identity, true));
		TestEqual(TEXT("Invalid class does not consume a generation token"), Arena->LastReservedGameplayGeneration, Cancelled);
		Mode->ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		bool bApplied = false;
		bool bVetoChecked = false;
		TestFalse(TEXT("GameMode rejects a failed pre-transition callback"),
			Mode->ReloadArenaAndRespawnPair(ShooterState, PartnerState,
				FTransform::Identity, FTransform::Identity, true, INDEX_NONE,
				[&]() { bVetoChecked = true; return false; }, [&]() { bApplied = true; }));
		TestTrue(TEXT("Valid preflight reaches the veto callback"), bVetoChecked);
		TestFalse(TEXT("Rejected transition does not apply the snapshot"), bApplied);
		TestFalse(TEXT("Rejected transition does not enter reload"), Mode->bArenaReloadInProgress);
		TestEqual(TEXT("Rejected transition releases its reservation"), Arena->ReservedGameplayGeneration, 0u);
		TestEqual(TEXT("Rejected transition preserves the live generation"), Arena->GetGameplayGeneration(), 0u);
	}

	const uint32 Committed = Arena->ReserveGameplayGeneration();
	TestTrue(TEXT("Cancelled tokens are not reused"), Committed > Cancelled);
	Arena->CancelGameplayGeneration(Cancelled);
	TestTrue(TEXT("Stale cancellation preserves the current reservation"), Arena->CommitGameplayGeneration(Committed));
	TestEqual(TEXT("Consumers transition exactly once"), Starts, 1);
	TestEqual(TEXT("Consumer and Arena generations agree"), ObservedGeneration, Committed);
	TestFalse(TEXT("Duplicate commit is rejected"), Arena->CommitGameplayGeneration(Committed));
	TestFalse(TEXT("Commit does not start streaming before restore"), Arena->PendingGameplayReload->bUnloadRequested);
	Arena->TickArenaLevels(0.0f);
	TestEqual(TEXT("Preparation cannot advance the unload phase"),
		Arena->GetGameplayReloadPhase(), EOutlierGameplayReloadPhase::WaitingForUnload);
	TestTrue(TEXT("Committed execution starts unload"), Arena->ReloadGameplayLevels(Committed, true));
	AddExpectedError(TEXT("Reject server reload"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Duplicate unload start is rejected"), Arena->ReloadGameplayLevels(Committed, true));
	Arena->AllowGameplayLevelLoad(Cancelled);
	TestFalse(TEXT("Stale authorization cannot unlock the new execution"), Arena->PendingGameplayReload->bCanLoad);
	Arena->FailGameplayReload(Cancelled, EOutlierGameplayReloadFailure::InvalidRuntime);
	TestEqual(TEXT("Stale failure cannot terminate the new execution"), Failures, 0);
	AddExpectedError(TEXT("Reload failed:"), EAutomationExpectedErrorFlags::Contains, 1);
	Arena->FailGameplayReload(Committed, EOutlierGameplayReloadFailure::InvalidRuntime);
	Arena->FailGameplayReload(Committed, EOutlierGameplayReloadFailure::InvalidRuntime);
	TestEqual(TEXT("Committed failure is notified once"), Failures, 1);
	TestEqual(TEXT("Committed failure never rewinds the generation"), Arena->GetGameplayGeneration(), Committed);

	// 완료 후 늦은 실패는 pending을 부활시키지 않으며 다음 실행은 새 토큰을 사용한다.
	Arena->PendingGameplayReload.Reset();
	Arena->FailGameplayReload(Committed, EOutlierGameplayReloadFailure::InvalidRuntime);
	TestFalse(TEXT("Late failure does not recreate a completed reload"), Arena->PendingGameplayReload.IsSet());
	const uint32 Next = Arena->ReserveGameplayGeneration();
	TestTrue(TEXT("Next execution advances past committed tokens"), UOutlierArenaSubsystem::IsGameplayGenerationNewer(Next, Committed));
	TestFalse(TEXT("Old committed token cannot replace the new reservation"), Arena->CommitGameplayGeneration(Committed));
	TestTrue(TEXT("Next execution commits"), Arena->CommitGameplayGeneration(Next));
	TestTrue(TEXT("Next execution requests unload"), Arena->ReloadGameplayLevels(Next, true));
	int32 GCReady = 0;
	Arena->OnArenaGameplayGCReady.AddLambda([&](uint32) { ++GCReady; });
	// unload 완료 통보에서 실패하는 재진입을 실제 poll 경로에 주입한다.
	Arena->OnArenaGameplayUnloaded.AddLambda([&](uint32 Generation)
	{
		Arena->FailGameplayReload(Generation, EOutlierGameplayReloadFailure::InvalidRuntime);
	});
	Arena->PendingGameplayReload->Phase = EOutlierGameplayReloadPhase::WaitingForGCPurge;
	AddExpectedError(TEXT("Reload failed:"), EAutomationExpectedErrorFlags::Contains, 1);
	Arena->TickArenaLevels(0.0f);
	TestEqual(TEXT("Failure during unload notification suppresses GCReady"), GCReady, 0);
	TestEqual(TEXT("Each failed execution notifies once"), Failures, 2);
	Arena->OnArenaGameplayUnloaded.Clear();
	Arena->OnArenaGameplayGCReady.Clear();

	// GameMode의 실제 복원 콜백이 확정 이후에 실행되고, 콜백 중 실패하면 스폰 전에 멈춘다.
	Arena->PendingGameplayReload.Reset();
	if (Mode && ShooterState && PartnerState && ShooterController && PartnerController)
	{
		FObjectPropertyBase* AuthorityModeProperty = FindFProperty<FObjectPropertyBase>(
			UWorld::StaticClass(), TEXT("AuthorityGameMode"));
		if (TestNotNull(TEXT("Fixture can supply the authoritative GameMode"), AuthorityModeProperty))
		{
			AuthorityModeProperty->SetObjectPropertyValue_InContainer(World, Mode);
			Mode->PendingGameplayGeneration = Next;
			Mode->bArenaReloadInProgress = true;
			Mode->bServerArenaReloadReady = true;
			const FDelegateHandle FailureHandle = Arena->OnArenaGameplayReloadFailed.AddUObject(
				Mode, &AOutlierGameMode::HandleArenaGameplayReloadFailed);
			AddExpectedError(TEXT("Reload failed:"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedError(TEXT("Gameplay reload failed. Generation="), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedError(TEXT("Unrecoverable arena state; leaving arena"), EAutomationExpectedErrorFlags::Contains, 1);
			Arena->FailGameplayReload(Next, EOutlierGameplayReloadFailure::RequiredClientDisconnected);
			TestFalse(TEXT("Pair-wait failure remains valid after server streaming completes"), Mode->bArenaReloadInProgress);
			Arena->OnArenaGameplayReloadFailed.Remove(FailureHandle);
			Arena->PendingGameplayReload.Reset();
			Arena->FailGameplayReload(Next, EOutlierGameplayReloadFailure::InvalidRuntime);
			TestFalse(TEXT("Finished pair cannot be failed again"), Arena->PendingGameplayReload.IsSet());
			AuthorityModeProperty->SetObjectPropertyValue_InContainer(World, nullptr);
		}
		bool bAppliedAfterCommit = false;
		AddExpectedError(TEXT("Reload failed:"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("Gameplay reload failed. Generation="), EAutomationExpectedErrorFlags::Contains, 1);
		TestTrue(TEXT("Failure after commit is an accepted transition, not a preflight rejection"),
			Mode->ReloadArenaAndRespawnPair(ShooterState, PartnerState,
				FTransform::Identity, FTransform::Identity, true, INDEX_NONE, {}, [&]()
				{
					bAppliedAfterCommit = Mode->IsGameplayReloadInProgress(Arena->GetGameplayGeneration())
						&& ObservedGeneration == Arena->GetGameplayGeneration();
					Arena->FailGameplayReload(Arena->GetGameplayGeneration(), EOutlierGameplayReloadFailure::InvalidRuntime);
				}));
		TestTrue(TEXT("Snapshot application observes the committed lifecycle"), bAppliedAfterCommit);
		TestFalse(TEXT("Failure during restore stops GameMode reload"), Mode->bArenaReloadInProgress);
		TestNull(TEXT("Failed restore does not spawn a new shooter"), ShooterState->GetShooterCharacter());
		TestNull(TEXT("Failed restore does not spawn a new partner"), PartnerState->GetPartnerCharacter());
	}
	Arena->OnArenaGameplayReloadStarted.Clear();
	Arena->OnArenaGameplayReloadFailed.Clear();
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaGameplayPlacementTest,
	"Outlier.Network.SingleArena.GameplayPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaGameplayPlacementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	UWorld* ArenaMap = Settings ? Settings->ArenaLevel.LoadSynchronous() : nullptr;
	if (!TestNotNull(TEXT("Configured Arena map loads"), ArenaMap))
	{
		return false;
	}

	TestFalse(TEXT("Arena Persistent is not a reloadable Gameplay level"),
		UOutlierArenaSubsystem::IsGameplaySublevelPackage(
			ArenaMap, Settings->GetArenaPackageName()));
	TestFalse(TEXT("An unrelated map is not a Gameplay level"),
		UOutlierArenaSubsystem::IsGameplaySublevelPackage(
			ArenaMap, TEXT("/Game/Maps/Unrelated")));

	const ULevelStreaming* GameplayLevel = nullptr;
	for (const ULevelStreaming* Level : ArenaMap->GetStreamingLevels())
	{
		if (Level && Level->IsA<ULevelStreamingDynamic>())
		{
			GameplayLevel = Level;
			break;
		}
	}
	if (!TestNotNull(TEXT("Arena declares a Gameplay streaming sublevel"), GameplayLevel))
	{
		return false;
	}

	const FString GameplayPackage = UWorld::RemovePIEPrefix(
		GameplayLevel->GetWorldAssetPackageName());
	TestTrue(TEXT("Declared Gameplay level is reloadable"),
		UOutlierArenaSubsystem::IsGameplaySublevelPackage(ArenaMap, GameplayPackage));
	const FString PIEPackage = FPackageName::GetLongPackagePath(GameplayPackage)
		+ TEXT("/UEDPIE_1_") + FPackageName::GetShortName(GameplayPackage);
	TestTrue(TEXT("PIE package resolves to the same Gameplay level"),
		UOutlierArenaSubsystem::IsGameplaySublevelPackage(ArenaMap, PIEPackage));
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaHandoffUrlContractTest,
	"Outlier.Network.ArenaWorker.HandoffUrl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaHandoffUrlContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGuid MatchId(1, 2, 3, 4);
	const FGuid PlayerId(5, 6, 7, 8);
	const FOutlierArenaHandoffRequest Original = MakeHandoffRequest(
		MatchId,
		PlayerId,
		EOutlierPlayerRole::Shooter);

	const FString Url = OutlierArenaHandoff::BuildTravelUrl(
		TEXT("127.0.0.1:7780"),
		Original);
	TestTrue(TEXT("A valid handoff produces a travel URL"), !Url.IsEmpty());
	TestTrue(
		TEXT("Whitespace around the configured address is ignored"),
		OutlierArenaHandoff::BuildTravelUrl(TEXT(" 127.0.0.1:7780 "), Original)
			== Url);
	TestTrue(
		TEXT("An empty arena address cannot produce a travel URL"),
		OutlierArenaHandoff::BuildTravelUrl(TEXT(" "), Original).IsEmpty());

	const int32 OptionsStart = Url.Find(TEXT("?"));
	if (!TestTrue(TEXT("The travel URL contains options"), OptionsStart != INDEX_NONE))
	{
		return false;
	}

	FOutlierArenaHandoffRequest Parsed;
	FString Error;
	TestTrue(
		TEXT("The generated URL options can be parsed"),
		OutlierArenaHandoff::TryParseOptions(Url.Mid(OptionsStart), Parsed, Error));
	TestTrue(TEXT("The parsed MatchId is preserved"), Parsed.MatchId == MatchId);
	TestTrue(TEXT("The parsed PlayerId is preserved"), Parsed.PlayerId == PlayerId);
	TestEqual(
		TEXT("The parsed role is preserved"),
		Parsed.Role,
		EOutlierPlayerRole::Shooter);

	TestFalse(
		TEXT("A missing MatchId is rejected"),
		OutlierArenaHandoff::TryParseOptions(
			FString::Printf(
				TEXT("?PlayerId=%s?Role=Shooter"),
				*PlayerId.ToString(EGuidFormats::DigitsWithHyphens)),
			Parsed,
			Error));
	TestFalse(
		TEXT("An unknown role is rejected"),
		OutlierArenaHandoff::TryParseOptions(
			FString::Printf(
				TEXT("?MatchId=%s?PlayerId=%s?Role=Unknown"),
				*MatchId.ToString(EGuidFormats::DigitsWithHyphens),
				*PlayerId.ToString(EGuidFormats::DigitsWithHyphens)),
			Parsed,
			Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaHandoffModeContractTest,
	"Outlier.Network.ArenaWorker.HandoffMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaHandoffModeContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UOutlierArenaSettings* Settings = NewObject<UOutlierArenaSettings>();
	if (!TestNotNull(TEXT("Arena settings can be created for the handoff mode test"), Settings))
	{
		return false;
	}

	TestTrue(
		TEXT("A Dedicated Lobby uses the external Arena Worker handoff"),
		Settings->ShouldUseExternalArenaHandoff(NM_DedicatedServer));
	TestFalse(
		TEXT("A PIE Listen Server keeps the in-process ArenaSubsystem path"),
		Settings->ShouldUseExternalArenaHandoff(NM_ListenServer));
	TestFalse(
		TEXT("A standalone session keeps the in-process ArenaSubsystem path"),
		Settings->ShouldUseExternalArenaHandoff(NM_Standalone));
	TestFalse(
		TEXT("A client does not own the external Arena handoff"),
		Settings->ShouldUseExternalArenaHandoff(NM_Client));

	Settings->bUseStaticArenaHandoff = false;
	TestFalse(
		TEXT("Disabling static handoff also disables it for a Dedicated Lobby"),
		Settings->ShouldUseExternalArenaHandoff(NM_DedicatedServer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaAdmissionContractTest,
	"Outlier.Network.ArenaWorker.Admission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaAdmissionContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGuid MatchId(1, 2, 3, 4);
	const FGuid OtherMatchId(9, 10, 11, 12);
	const FGuid ShooterId(5, 6, 7, 8);
	const FGuid PartnerId(13, 14, 15, 16);
	const FOutlierArenaHandoffRequest Shooter = MakeHandoffRequest(
		MatchId,
		ShooterId,
		EOutlierPlayerRole::Shooter);
	const FOutlierArenaHandoffRequest Partner = MakeHandoffRequest(
		MatchId,
		PartnerId,
		EOutlierPlayerRole::Partner);

	FOutlierArenaAdmissionState Admission;
	FString Error;
	TestTrue(TEXT("The first Shooter can reserve an empty worker"), Admission.Commit(Shooter, Error));
	TestFalse(TEXT("One participant is not a complete pair"), Admission.IsReady());

	FOutlierArenaHandoffRequest DifferentMatch = Partner;
	DifferentMatch.MatchId = OtherMatchId;
	TestFalse(TEXT("A different MatchId is rejected"), Admission.CanAccept(DifferentMatch, Error));

	FOutlierArenaHandoffRequest DuplicatePlayer = Partner;
	DuplicatePlayer.PlayerId = ShooterId;
	TestFalse(TEXT("A duplicate PlayerId is rejected"), Admission.CanAccept(DuplicatePlayer, Error));

	FOutlierArenaHandoffRequest DuplicateRole = Partner;
	DuplicateRole.Role = EOutlierPlayerRole::Shooter;
	TestFalse(TEXT("An occupied role is rejected"), Admission.CanAccept(DuplicateRole, Error));

	TestTrue(TEXT("The complementary Partner can join"), Admission.Commit(Partner, Error));
	TestTrue(TEXT("Shooter and Partner complete the worker pair"), Admission.IsReady());

	Admission.bPairStarted = true;
	TestTrue(TEXT("The original Shooter can reconnect to a started worker"), Admission.CanAccept(Shooter, Error));
	TestTrue(TEXT("The original Partner can reconnect to a started worker"), Admission.CanAccept(Partner, Error));
	TestTrue(TEXT("Recommitting the original Shooter preserves the reserved slot"), Admission.Commit(Shooter, Error));
	FOutlierArenaHandoffRequest ThirdPlayer = MakeHandoffRequest(
		MatchId,
		FGuid(17, 18, 19, 20),
		EOutlierPlayerRole::Partner);
	TestFalse(TEXT("A started worker rejects additional participants"), Admission.CanAccept(ThirdPlayer, Error));
	TestEqual(
		TEXT("The started-worker rejection wins over slot details"),
		Error,
		FString(TEXT("Arena match already started")));

	FOutlierArenaAdmissionState ReleasedAdmission;
	TestTrue(TEXT("A participant can be committed before PostLogin"), ReleasedAdmission.Commit(Shooter, Error));
	ReleasedAdmission.Release(ShooterId);
	TestFalse(TEXT("Releasing the only participant clears the worker match"), ReleasedAdmission.MatchId.IsValid());
	TestTrue(TEXT("The released role can be admitted again"), ReleasedAdmission.CanAccept(Shooter, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaReturnLifecycleTest,
	"Outlier.Network.ArenaWorker.ReturnLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaReturnLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UOutlierArenaSettings* Settings = NewObject<UOutlierArenaSettings>();
	if (!TestNotNull(TEXT("Arena settings can be created for the return contract test"), Settings))
	{
		return false;
	}

	TestEqual(
		TEXT("The default Lobby address targets the local Lobby server"),
		Settings->LobbyAddress,
		FString(TEXT("127.0.0.1:7777")));
	TestTrue(
		TEXT("Arena workers return players to the Lobby by default"),
		Settings->bReturnToLobbyOnMatchEnd);
	TestTrue(
		TEXT("Static cross-process Handoff is the default Network MVP path"),
		Settings->bUseStaticArenaHandoff);
	TestEqual(
		TEXT("Arena workers have a disconnect fallback timeout"),
		Settings->ArenaWorkerExitTimeoutSeconds,
		5.0f);
	TestEqual(
		TEXT("Arena workers wait thirty seconds for a disconnected player to reconnect"),
		Settings->ArenaWorkerReconnectGraceSeconds,
		30.0f);
	TestEqual(
		TEXT("Disconnected arena clients retry every two seconds"),
		Settings->ArenaWorkerReconnectRetrySeconds,
		2.0f);
	TestEqual(
		TEXT("Arena gameplay starts one second after both clients are ready"),
		Settings->ArenaMatchStartDelaySeconds,
		1.0f);
	TestEqual(
		TEXT("Gameplay reload reports a stall after fifteen seconds"),
		Settings->ArenaGameplayReloadStallSeconds,
		15.0f);
	TestEqual(
		TEXT("Arena workers fail a persistent gameplay reload stall after sixty seconds"),
		Settings->ArenaGameplayReloadFailureSeconds,
		60.0f);

	UOutlierGameInstance* GameInstance = NewObject<UOutlierGameInstance>();
	if (!TestNotNull(TEXT("The game instance can be created for the return lifecycle test"), GameInstance))
	{
		return false;
	}

	GameInstance->NotifyArenaHandoffStarted(TEXT("127.0.0.1:7780?MatchId=Test"));
	TestTrue(TEXT("Arena handoff activates Lobby recovery"), GameInstance->bArenaHandoffActive);
	TestEqual(
		TEXT("Arena handoff keeps the last URL for reconnect"),
		GameInstance->LastArenaHandoffUrl,
		FString(TEXT("127.0.0.1:7780?MatchId=Test")));
	TestFalse(TEXT("Lobby recovery is initially not queued"), GameInstance->bLobbyRecoveryQueued);
	TestFalse(TEXT("Lobby recovery is initially not attempted"), GameInstance->bLobbyRecoveryAttempted);

	GameInstance->ScheduleArenaReconnect();
	TestTrue(TEXT("A network failure keeps the reconnect grace path active"), GameInstance->bArenaReconnectActive);
	TestTrue(TEXT("A network failure schedules the reconnect ticker"), GameInstance->ArenaReconnectTickerHandle.IsValid());
	TestTrue(
		TEXT("A network failure creates a reconnect deadline"),
		GameInstance->ArenaReconnectDeadlineSeconds > FPlatformTime::Seconds());

	GameInstance->PrepareForExplicitLeave();
	TestFalse(TEXT("Explicit leave clears the arena handoff"), GameInstance->bArenaHandoffActive);
	TestFalse(TEXT("Explicit leave stops reconnect attempts"), GameInstance->bArenaReconnectActive);
	TestFalse(TEXT("Explicit leave removes the reconnect ticker"), GameInstance->ArenaReconnectTickerHandle.IsValid());
	TestTrue(TEXT("Explicit leave forgets the previous Worker URL"), GameInstance->LastArenaHandoffUrl.IsEmpty());

	GameInstance->LastListenReconnectUrl = TEXT("127.0.0.1:7777?ListenReconnect=Test");
	GameInstance->ScheduleArenaReconnect();
	TestTrue(TEXT("Listen guest uses the bounded reconnect retry"), GameInstance->bArenaReconnectActive);
	TestTrue(TEXT("Listen retry schedules the shared reconnect ticker"),
		GameInstance->ArenaReconnectTickerHandle.IsValid());
	GameInstance->PrepareForExplicitLeave();
	TestTrue(TEXT("Explicit leave forgets the Listen reconnect route"),
		GameInstance->LastListenReconnectUrl.IsEmpty());
	TestFalse(TEXT("Explicit leave cancels Listen retry"), GameInstance->bArenaReconnectActive);
	TestFalse(TEXT("Explicit leave removes Listen retry ticker"),
		GameInstance->ArenaReconnectTickerHandle.IsValid());

	GameInstance->NotifyArenaHandoffStarted(TEXT("127.0.0.1:7780?MatchId=RecoveryTest"));

	TestTrue(TEXT("The first network failure queues Lobby recovery"), GameInstance->TryQueueLobbyRecovery());
	TestTrue(TEXT("Lobby recovery is queued after the first failure"), GameInstance->bLobbyRecoveryQueued);
	TestTrue(TEXT("The first recovery attempt is recorded"), GameInstance->bLobbyRecoveryAttempted);
	TestFalse(TEXT("A second network failure cannot queue another recovery"), GameInstance->TryQueueLobbyRecovery());

	GameInstance->ResetArenaHandoffState();
	TestFalse(TEXT("Completing Lobby travel clears the handoff state"), GameInstance->bArenaHandoffActive);
	TestFalse(TEXT("Completing Lobby travel clears queued recovery"), GameInstance->bLobbyRecoveryQueued);
	TestFalse(TEXT("Completing Lobby travel clears the recovery attempt"), GameInstance->bLobbyRecoveryAttempted);

	return true;
}

#endif
