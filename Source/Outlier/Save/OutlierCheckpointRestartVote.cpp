#include "Save/OutlierCheckpointRestartVote.h"

#include "GameFramework/PlayerController.h"

bool FOutlierCheckpointRestartVote::Begin(
	APlayerController* InRequester,
	APlayerController* InResponder)
{
	if (State != EOutlierCheckpointRestartVoteState::Idle
		|| !InRequester
		|| !InResponder
		|| InRequester == InResponder)
	{
		return false;
	}

	Requester = InRequester;
	Responder = InResponder;
	State = EOutlierCheckpointRestartVoteState::VotePending;
	return true;
}

bool FOutlierCheckpointRestartVote::Respond(
	APlayerController* Controller,
	bool bApprove)
{
	if (State != EOutlierCheckpointRestartVoteState::VotePending
		|| !Controller
		|| Controller != Responder.Get())
	{
		return false;
	}

	State = bApprove
		? EOutlierCheckpointRestartVoteState::Approved
		: EOutlierCheckpointRestartVoteState::Rejected;
	return true;
}

bool FOutlierCheckpointRestartVote::Cancel(APlayerController* Controller)
{
	if (State != EOutlierCheckpointRestartVoteState::VotePending
		|| !Controller
		|| Controller != Requester.Get())
	{
		return false;
	}

	State = EOutlierCheckpointRestartVoteState::Rejected;
	return true;
}

bool FOutlierCheckpointRestartVote::BeginRestart()
{
	if (State != EOutlierCheckpointRestartVoteState::Approved)
	{
		return false;
	}

	State = EOutlierCheckpointRestartVoteState::Restarting;
	return true;
}

bool FOutlierCheckpointRestartVote::Contains(const APlayerController* Controller) const
{
	return Controller
		&& (Controller == Requester.Get() || Controller == Responder.Get());
}

void FOutlierCheckpointRestartVote::Reset()
{
	State = EOutlierCheckpointRestartVoteState::Idle;
	Requester.Reset();
	Responder.Reset();
}

bool OutlierCheckpointRestartVote::CanRequest(
	ENetMode NetMode,
	bool bIsLocalController)
{
	switch (NetMode)
	{
	case NM_Standalone:
	case NM_DedicatedServer:
		return true;
	case NM_ListenServer:
		return bIsLocalController;
	case NM_Client:
	default:
		return false;
	}
}
