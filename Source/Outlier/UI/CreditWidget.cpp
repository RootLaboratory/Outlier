#include "UI/CreditWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Engine/LocalPlayer.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

void UCreditWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
}

bool UCreditWidget::HandleUILayerEscape_Implementation()
{
	PopSelfFromLayer();
	return true;
}

bool UCreditWidget::HandleUILayerConfirmed_Implementation()
{
	return false;
}

bool UCreditWidget::HandleUILayerUp_Implementation()
{
	return false;
}

bool UCreditWidget::HandleUILayerDown_Implementation()
{
	return false;
}

bool UCreditWidget::HandleUILayerLeft_Implementation()
{
	return false;
}

bool UCreditWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void UCreditWidget::PopSelfFromLayer()
{
	OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Back"));
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopWidget(this);
	}
}
