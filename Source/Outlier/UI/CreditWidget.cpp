#include "UI/CreditWidget.h"

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
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopWidget(this);
	}
}
