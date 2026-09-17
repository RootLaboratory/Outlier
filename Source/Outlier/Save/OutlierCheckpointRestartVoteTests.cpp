#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Save/OutlierCheckpointRestartVote.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierCheckpointRestartVotePermissionTest,
	"Outlier.Save.Checkpoint.RestartVote.Permission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierCheckpointRestartVotePermissionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("Standalone can request without a peer"),
		OutlierCheckpointRestartVote::CanRequest(NM_Standalone, true));
	TestTrue(TEXT("Listen Host can request"),
		OutlierCheckpointRestartVote::CanRequest(NM_ListenServer, true));
	TestFalse(TEXT("Listen Guest cannot request"),
		OutlierCheckpointRestartVote::CanRequest(NM_ListenServer, false));
	TestTrue(TEXT("Either remote Dedicated player can request"),
		OutlierCheckpointRestartVote::CanRequest(NM_DedicatedServer, false));
	TestFalse(TEXT("A client cannot authoritatively create a vote"),
		OutlierCheckpointRestartVote::CanRequest(NM_Client, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierCheckpointRestartVoteStateTest,
	"Outlier.Save.Checkpoint.RestartVote.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierCheckpointRestartVoteStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		WorldName,
		GetTransientPackage());
	if (!TestNotNull(TEXT("A transient restart vote world is created"), World))
	{
		return false;
	}

	World->AddToRoot();
	APlayerController* Requester = World->SpawnActor<APlayerController>();
	APlayerController* Responder = World->SpawnActor<APlayerController>();
	APlayerController* Stranger = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("The requester is spawned"), Requester)
		|| !TestNotNull(TEXT("The responder is spawned"), Responder)
		|| !TestNotNull(TEXT("The unrelated controller is spawned"), Stranger))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		World->RemoveFromRoot();
		return false;
	}

	FOutlierCheckpointRestartVote Vote;
	TestTrue(TEXT("A valid request starts one vote"), Vote.Begin(Requester, Responder));
	TestEqual(TEXT("The vote enters VotePending"),
		Vote.GetState(), EOutlierCheckpointRestartVoteState::VotePending);
	TestTrue(TEXT("The requester belongs to the vote"), Vote.Contains(Requester));
	TestTrue(TEXT("The responder belongs to the vote"), Vote.Contains(Responder));
	TestFalse(TEXT("An unrelated controller does not belong to the vote"), Vote.Contains(Stranger));
	TestFalse(TEXT("A second request is rejected while pending"), Vote.Begin(Stranger, Responder));
	TestFalse(TEXT("An unrelated controller cannot respond"), Vote.Respond(Stranger, true));
	TestFalse(TEXT("The requester cannot fill the responder's vote"), Vote.Respond(Requester, true));
	TestTrue(TEXT("The responder can approve"), Vote.Respond(Responder, true));
	TestEqual(TEXT("Approval is recorded"),
		Vote.GetState(), EOutlierCheckpointRestartVoteState::Approved);
	TestTrue(TEXT("An approved vote can enter Restarting"), Vote.BeginRestart());
	TestEqual(TEXT("Restarting remains owned until reload completion"),
		Vote.GetState(), EOutlierCheckpointRestartVoteState::Restarting);
	TestFalse(TEXT("Restarting cannot be entered twice"), Vote.BeginRestart());
	TestFalse(TEXT("Restarting cannot be rejected by a late response"),
		Vote.Respond(Responder, false));
	TestFalse(TEXT("Restarting cannot be cancelled by the requester"),
		Vote.Cancel(Requester));

	Vote.Reset();
	TestEqual(TEXT("Reset returns the vote to Idle"),
		Vote.GetState(), EOutlierCheckpointRestartVoteState::Idle);
	TestTrue(TEXT("A new vote can start after reset"), Vote.Begin(Requester, Responder));
	TestTrue(TEXT("The responder can reject"), Vote.Respond(Responder, false));
	TestEqual(TEXT("Rejection is recorded"),
		Vote.GetState(), EOutlierCheckpointRestartVoteState::Rejected);

	Vote.Reset();
	TestTrue(TEXT("A cancellation scenario can start"), Vote.Begin(Requester, Responder));
	TestFalse(TEXT("The responder cannot cancel as requester"), Vote.Cancel(Responder));
	TestTrue(TEXT("The requester can cancel"), Vote.Cancel(Requester));
	TestEqual(TEXT("Requester cancellation is a rejection"),
		Vote.GetState(), EOutlierCheckpointRestartVoteState::Rejected);

	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	World->RemoveFromRoot();
	return true;
}

#endif
