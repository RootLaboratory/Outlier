#include "UI/InGamePauseWidget.h"

#include "FirstPerson/FirstPersonPlayerController.h"
#include "Save/OutlierCheckpointRestartVote.h"

bool UInGamePauseWidget::HandleUILayerEscape_Implementation()
{
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
		FirstPersonController
		&& FirstPersonController->GetCheckpointRestartVoteView()
			== EOutlierCheckpointRestartVoteView::ResponderPrompt)
	{
		FirstPersonController->RequestCheckpointRestartResponse(false);
	}
	else if (FirstPersonController)
	{
		FirstPersonController->RequestOpenInGameSetting();
	}
	return true;
}

bool UInGamePauseWidget::HandleUILayerConfirmed_Implementation()
{
	// 상대방의 찬성 입력만 서버에 보낸다. UI를 닫거나 재시작을 확정하는 것은 서버 투표 결과의 책임이다.
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
		FirstPersonController
		&& FirstPersonController->GetCheckpointRestartVoteView()
			== EOutlierCheckpointRestartVoteView::ResponderPrompt)
	{
		FirstPersonController->RequestCheckpointRestartResponse(true);
	}
	return true;
}

bool UInGamePauseWidget::HandleUILayerUp_Implementation()
{
	return true;
}

bool UInGamePauseWidget::HandleUILayerDown_Implementation()
{
	return true;
}

bool UInGamePauseWidget::HandleUILayerLeft_Implementation()
{
	return true;
}

bool UInGamePauseWidget::HandleUILayerRight_Implementation()
{
	return true;
}
