#include "UI/InGamePauseWidget.h"

#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "OutlierPlayerState.h"

void UInGamePauseWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (CurrentPauseText.IsEmpty())
	{
		SetPauseText(UnknownPausedText);
	}
	else
	{
		SetPauseText(CurrentPauseText);
	}
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
	return true;
}

bool UInGamePauseWidget::HandleUILayerConfirmed_Implementation()
{
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
		SetPauseText(ShooterPausedText);
		return;
	}

	if (PauserPlayerState && PauserPlayerState->IsPartnerPlayer())
	{
		SetPauseText(PartnerPausedText);
		return;
	}

	SetPauseText(UnknownPausedText);
}

void UInGamePauseWidget::SetPauseText(const FText& NewPauseText)
{
	CurrentPauseText = NewPauseText;

	if (PauseText)
	{
		PauseText->SetText(NewPauseText);
	}
}
