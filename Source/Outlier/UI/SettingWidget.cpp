#include "UI/SettingWidget.h"

#include "Engine/LocalPlayer.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

void USettingWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
}

bool USettingWidget::HandleUILayerEscape_Implementation()
{
	PopSelfFromLayer();
	return true;
}

bool USettingWidget::HandleUILayerConfirmed_Implementation()
{
	return false;
}

bool USettingWidget::HandleUILayerUp_Implementation()
{
	return false;
}

bool USettingWidget::HandleUILayerDown_Implementation()
{
	return false;
}

bool USettingWidget::HandleUILayerLeft_Implementation()
{
	return false;
}

bool USettingWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void USettingWidget::PopSelfFromLayer()
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
