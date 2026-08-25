#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Network/OutlierMatchRequest.h"
#include "Network/OutlierMatchmakingSubsystem.h"
#include "OutlierLobbyIdentitySubsystem.h"
#include "OutlierPlayerState.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FOutlierMatchAssignment MakeValidAssignment()
{
	FOutlierMatchAssignment Assignment;
	Assignment.MatchId = FGuid::NewGuid();
	Assignment.Shooter.PlayerId = FGuid::NewGuid();
	Assignment.Shooter.Role = EOutlierPlayerRole::Shooter;
	Assignment.Partner.PlayerId = FGuid::NewGuid();
	Assignment.Partner.Role = EOutlierPlayerRole::Partner;
	return Assignment;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierMatchAssignmentContractTest,
	"Outlier.Network.MatchAssignment.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMatchAssignmentContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FOutlierMatchAssignment EmptyAssignment;
	TestFalse(TEXT("A default assignment is invalid"), EmptyAssignment.IsValid());

	const FOutlierMatchAssignment ValidAssignment = MakeValidAssignment();
	TestTrue(TEXT("Distinct players with canonical roles form a valid assignment"), ValidAssignment.IsValid());

	FOutlierMatchAssignment DuplicatePlayerAssignment = ValidAssignment;
	DuplicatePlayerAssignment.Partner.PlayerId = DuplicatePlayerAssignment.Shooter.PlayerId;
	TestFalse(TEXT("The same player cannot fill both roles"), DuplicatePlayerAssignment.IsValid());

	FOutlierMatchAssignment SwappedRoleAssignment = ValidAssignment;
	SwappedRoleAssignment.Shooter.Role = EOutlierPlayerRole::Partner;
	SwappedRoleAssignment.Partner.Role = EOutlierPlayerRole::Shooter;
	TestFalse(TEXT("Shooter and partner fields require their canonical roles"), SwappedRoleAssignment.IsValid());

	FOutlierMatchAssignment MissingMatchIdAssignment = ValidAssignment;
	MissingMatchIdAssignment.MatchId = FGuid();
	TestFalse(TEXT("An assignment requires a valid match ID"), MissingMatchIdAssignment.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierMatchAssignmentLifecycleTest,
	"Outlier.Network.MatchAssignment.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMatchAssignmentLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UOutlierMatchmakingSubsystem* Matchmaking = NewObject<UOutlierMatchmakingSubsystem>(GameInstance);
	if (!TestNotNull(TEXT("The matchmaking subsystem can be created for a storage test"), Matchmaking))
	{
		return false;
	}

	constexpr int32 PairId = 41;
	const FOutlierMatchAssignment Assignment = MakeValidAssignment();
	Matchmaking->ActiveMatchAssignments.Add(Assignment.MatchId, Assignment);
	Matchmaking->ActivePairMatchIds.Add(PairId, Assignment.MatchId);

	FOutlierMatchAssignment FoundAssignment;
	TestTrue(TEXT("A stored assignment can be queried by match ID"), Matchmaking->TryGetMatchAssignment(Assignment.MatchId, FoundAssignment));
	TestTrue(TEXT("The queried assignment preserves its match ID"), FoundAssignment.MatchId == Assignment.MatchId);

	FOutlierMatchAssignment MissingAssignment = Assignment;
	TestFalse(TEXT("An unknown match ID is not found"), Matchmaking->TryGetMatchAssignment(FGuid::NewGuid(), MissingAssignment));
	TestFalse(TEXT("A failed lookup clears the output assignment"), MissingAssignment.IsValid());

	// No arena mapping is added here: assignment cleanup must not depend on arena ownership.
	Matchmaking->ReleaseMatch(PairId);

	FOutlierMatchAssignment ReleasedAssignment;
	TestFalse(TEXT("Releasing a pair removes its match assignment"), Matchmaking->TryGetMatchAssignment(Assignment.MatchId, ReleasedAssignment));
	TestFalse(TEXT("Releasing a pair removes its pair-to-match lookup"), Matchmaking->ActivePairMatchIds.Contains(PairId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierPartyContractTest,
	"Outlier.Network.Party.CodeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierPartyContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString GeneratedCode = OutlierPartyCode::Generate();
	TestEqual(TEXT("A generated party code has the configured length"), GeneratedCode.Len(), OutlierPartyCode::Length);
	TestTrue(TEXT("A generated party code uses the accepted alphabet"), OutlierPartyCode::IsValid(GeneratedCode));

	const FString NormalizedCode = OutlierPartyCode::Normalize(TEXT("ab-cd 23"));
	TestEqual(TEXT("Party code input ignores separators and case"), NormalizedCode, FString(TEXT("ABCD23")));
	TestTrue(TEXT("A normalized code is accepted"), OutlierPartyCode::IsValid(NormalizedCode));
	TestFalse(TEXT("Ambiguous characters are rejected"), OutlierPartyCode::IsValid(TEXT("ABCDO1")));
	TestFalse(TEXT("A short code is rejected"), OutlierPartyCode::IsValid(TEXT("ABC23")));

	FOutlierPendingRolePickMatch SoloMatch;
	TestTrue(TEXT("A solo match requeues the remaining player when cancelled"), SoloMatch.ShouldRequeueRemainingPlayer());

	FOutlierPendingRolePickMatch PartyMatch;
	PartyMatch.Source = EOutlierPendingMatchSource::PremadeParty;
	TestFalse(TEXT("A premade party does not requeue the remaining player when cancelled"), PartyMatch.ShouldRequeueRemainingPlayer());

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UOutlierMatchmakingSubsystem* Matchmaking = NewObject<UOutlierMatchmakingSubsystem>(GameInstance);
	Matchmaking->PendingPartiesByCode.Add(GeneratedCode, FOutlierPendingParty());
	const FString NextCode = Matchmaking->GenerateUniquePartyCode();
	TestTrue(TEXT("A second generated code is valid"), OutlierPartyCode::IsValid(NextCode));
	TestNotEqual(TEXT("A generated code does not reuse an active code"), NextCode, GeneratedCode);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierPartyLifecycleContractTest,
	"Outlier.Network.Party.CancelLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierPartyLifecycleContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("A transient party lifecycle world is created"), World))
	{
		return false;
	}

	World->AddToRoot();
	APlayerController* FirstController = World->SpawnActor<APlayerController>();
	APlayerController* SecondController = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("The first test controller is spawned"), FirstController)
		|| !TestNotNull(TEXT("The second test controller is spawned"), SecondController))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		World->RemoveFromRoot();
		return false;
	}

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UOutlierMatchmakingSubsystem* Matchmaking = NewObject<UOutlierMatchmakingSubsystem>(GameInstance);
	if (!TestNotNull(TEXT("The matchmaking subsystem is created"), Matchmaking))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		World->RemoveFromRoot();
		return false;
	}

	FOutlierPendingRolePickMatch SoloMatch;
	SoloMatch.FirstController = FirstController;
	SoloMatch.SecondController = SecondController;
	SoloMatch.Source = EOutlierPendingMatchSource::SoloQueue;
	Matchmaking->PendingRolePickMatches.Add(SoloMatch);
	Matchmaking->Cancel(FirstController);
	TestEqual(TEXT("Cancelling a solo pair removes its pending role match"), Matchmaking->PendingRolePickMatches.Num(), 0);
	TestEqual(TEXT("The remaining solo player returns to matchmaking"), Matchmaking->WaitingPlayers.Num(), 1);
	TestTrue(TEXT("The requeued solo player is the other controller"), Matchmaking->WaitingPlayers[0].Controller == SecondController);

	Matchmaking->WaitingPlayers.Reset();
	FOutlierPendingRolePickMatch PartyMatch;
	PartyMatch.FirstController = FirstController;
	PartyMatch.SecondController = SecondController;
	PartyMatch.Source = EOutlierPendingMatchSource::PremadeParty;
	Matchmaking->PendingRolePickMatches.Add(PartyMatch);
	Matchmaking->Cancel(FirstController);
	TestEqual(TEXT("Cancelling a premade pair removes its pending role match"), Matchmaking->PendingRolePickMatches.Num(), 0);
	TestEqual(TEXT("The remaining party member is not placed in solo matchmaking"), Matchmaking->WaitingPlayers.Num(), 0);

	const FString PartyCode = OutlierPartyCode::Generate();
	FOutlierPendingParty PendingParty;
	PendingParty.PartyCode = PartyCode;
	PendingParty.LeaderPlayerId = FGuid::NewGuid();
	PendingParty.LeaderController = FirstController;
	Matchmaking->PendingPartiesByCode.Add(PartyCode, PendingParty);
	Matchmaking->Cancel(FirstController);
	TestFalse(TEXT("Leaving removes the host's pending party code"), Matchmaking->PendingPartiesByCode.Contains(PartyCode));

	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierPartyJoinIntegrationTest,
	"Outlier.Network.Party.JoinIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierPartyJoinIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AddExpectedError(
		TEXT("DA_Sample_PipeDrop.uasset: Asset has been saved with empty engine version"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	GameInstance->InitializeStandalone(WorldName);

	UWorld* World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("A standalone party integration world is created"), World))
	{
		GameInstance->Shutdown();
		return false;
	}
	World->AddToRoot();

	auto CleanupWorld = [GameInstance, World]()
	{
		GameInstance->Shutdown();
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	APlayerController* LeaderController = World->SpawnActor<APlayerController>();
	APlayerController* MemberController = World->SpawnActor<APlayerController>();
	AOutlierPlayerState* LeaderState = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* MemberState = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("The party leader controller is spawned"), LeaderController)
		|| !TestNotNull(TEXT("The party member controller is spawned"), MemberController)
		|| !TestNotNull(TEXT("The party leader state is spawned"), LeaderState)
		|| !TestNotNull(TEXT("The party member state is spawned"), MemberState))
	{
		CleanupWorld();
		return false;
	}

	LeaderController->SetPlayerState(LeaderState);
	MemberController->SetPlayerState(MemberState);

	UOutlierLobbyIdentitySubsystem* Identity =
		GameInstance->GetSubsystem<UOutlierLobbyIdentitySubsystem>();
	UOutlierMatchmakingSubsystem* Matchmaking =
		GameInstance->GetSubsystem<UOutlierMatchmakingSubsystem>();
	if (!TestNotNull(TEXT("The lobby identity subsystem is available"), Identity)
		|| !TestNotNull(TEXT("The matchmaking subsystem is available"), Matchmaking))
	{
		CleanupWorld();
		return false;
	}

	TestTrue(TEXT("The leader receives a temporary player ID"), Identity->RegisterPlayer(LeaderController).IsValid());
	TestTrue(TEXT("The member receives a temporary player ID"), Identity->RegisterPlayer(MemberController).IsValid());

	Matchmaking->CreateParty(LeaderController);
	if (!TestEqual(TEXT("Creating a party registers one pending code"), Matchmaking->PendingPartiesByCode.Num(), 1))
	{
		CleanupWorld();
		return false;
	}

	const FString PartyCode = Matchmaking->PendingPartiesByCode.CreateConstIterator().Key();
	TestTrue(TEXT("The registered party code is valid"), OutlierPartyCode::IsValid(PartyCode));

	Matchmaking->JoinParty(MemberController, PartyCode.ToLower());
	TestEqual(TEXT("A joined code is consumed immediately"), Matchmaking->PendingPartiesByCode.Num(), 0);
	if (!TestEqual(TEXT("Joining creates one pending role pick match"), Matchmaking->PendingRolePickMatches.Num(), 1))
	{
		CleanupWorld();
		return false;
	}

	const FOutlierPendingRolePickMatch& PendingMatch = Matchmaking->PendingRolePickMatches[0];
	TestTrue(TEXT("The joined pair is marked as a premade party"), PendingMatch.Source == EOutlierPendingMatchSource::PremadeParty);
	TestEqual(TEXT("Both party members share one pending match ID"), LeaderState->GetPendingLobbyMatchId(), MemberState->GetPendingLobbyMatchId());
	TestEqual(TEXT("The leader owns the first lobby slot"), LeaderState->GetPendingLobbySlotIndex(), 0);
	TestEqual(TEXT("The member owns the second lobby slot"), MemberState->GetPendingLobbySlotIndex(), 1);

	Matchmaking->Cancel(LeaderController);
	TestEqual(TEXT("Leaving disbands the premade role pick match"), Matchmaking->PendingRolePickMatches.Num(), 0);
	TestEqual(TEXT("The remaining member is not moved to solo matchmaking"), Matchmaking->WaitingPlayers.Num(), 0);
	TestEqual(TEXT("The member lobby state is cleared"), MemberState->GetPendingLobbyMatchId(), INDEX_NONE);

	CleanupWorld();
	return true;
}

#endif
