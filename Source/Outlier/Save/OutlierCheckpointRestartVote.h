#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/PlayerController.h"
#include "OutlierCheckpointRestartVote.generated.h"

UENUM()
enum class EOutlierCheckpointRestartVoteState : uint8
{
	Idle,
	VotePending,
	Approved,
	Rejected
};

UENUM()
enum class EOutlierCheckpointRestartVoteView : uint8
{
	None,
	RequesterWaiting,
	ResponderPrompt
};

/** 서버가 소유하는 한 건의 체크포인트 재시작 투표 상태. */
struct OUTLIER_API FOutlierCheckpointRestartVote
{
	bool Begin(APlayerController* InRequester, APlayerController* InResponder);
	bool Respond(APlayerController* Controller, bool bApprove);
	bool Cancel(APlayerController* Controller);
	bool Contains(const APlayerController* Controller) const;
	void Reset();

	EOutlierCheckpointRestartVoteState GetState() const { return State; }
	APlayerController* GetRequester() const { return Requester.Get(); }
	APlayerController* GetResponder() const { return Responder.Get(); }

private:
	EOutlierCheckpointRestartVoteState State = EOutlierCheckpointRestartVoteState::Idle;
	TWeakObjectPtr<APlayerController> Requester;
	TWeakObjectPtr<APlayerController> Responder;
};

namespace OutlierCheckpointRestartVote
{
	OUTLIER_API bool CanRequest(ENetMode NetMode, bool bIsLocalController);
}
