#include "UI/InGamePauseWidget.h"

#include "Components/TextBlock.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "OutlierPlayerState.h"

void UInGamePauseWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->OnCheckpointRestartVoteViewChanged.AddUObject(
			this,
			&UInGamePauseWidget::RefreshCheckpointRestartState);
		RefreshCheckpointRestartState(
			FirstPersonController->GetCheckpointRestartVoteView());
		return;
	}

	if (CurrentPauseText.IsEmpty())
	{
		SetPauseText(UnknownPausedText);
	}
	else
	{
		SetPauseText(CurrentPauseText);
	}
}

void UInGamePauseWidget::NativeDestruct()
{
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->OnCheckpointRestartVoteViewChanged.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void UInGamePauseWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
	AActor* PauserActor = ContextActors.IsValidIndex(2)
		? ContextActors[2]
		: nullptr;
	SetPauseTextFromPauser(PauserActor);
}

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
	return true;
}

bool UInGamePauseWidget::HandleUILayerConfirmed_Implementation()
{
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

void UInGamePauseWidget::SetPauseTextFromPauser(AActor* PauserActor)
{
	const APawn* PauserPawn = Cast<APawn>(PauserActor);
	const AOutlierPlayerState* PauserPlayerState = PauserPawn
		? PauserPawn->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (PauserPlayerState && PauserPlayerState->IsShooterPlayer())
	{
		PauserPauseText = ShooterPausedText;
	}
	else if (PauserPlayerState && PauserPlayerState->IsPartnerPlayer())
	{
		PauserPauseText = PartnerPausedText;
	}
	else
	{
		PauserPauseText = UnknownPausedText;
	}

	const AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
	RefreshCheckpointRestartState(FirstPersonController
		? FirstPersonController->GetCheckpointRestartVoteView()
		: EOutlierCheckpointRestartVoteView::None);
}

void UInGamePauseWidget::SetPauseText(const FText& NewPauseText)
{
	CurrentPauseText = NewPauseText;

	if (PauseText)
	{
		PauseText->SetText(NewPauseText);
	}
}

void UInGamePauseWidget::RefreshCheckpointRestartState(
	EOutlierCheckpointRestartVoteView VoteView)
{
	if (VoteView == EOutlierCheckpointRestartVoteView::ResponderPrompt)
	{
		SetPauseText(CheckpointRestartPromptText);
		return;
	}

	SetPauseText(PauserPauseText.IsEmpty() ? UnknownPausedText : PauserPauseText);
}
