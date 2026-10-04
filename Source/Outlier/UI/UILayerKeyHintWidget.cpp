#include "UI/UILayerKeyHintWidget.h"

#include "Engine/LocalPlayer.h"
#include "Input/ControllerInputConfig.h"
#include "InputAction.h"
#include "UI/InputActionKeyDisplayWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/HintKeyDisplayWidget.h"

void UUILayerKeyHintWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshKeyTexts();
}

void UUILayerKeyHintWidget::RefreshKeyTexts()
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	const UControllerInputConfig* InputConfig = LayerSubsystem
		? LayerSubsystem->GetWidgetInputConfig()
		: nullptr;

	if (ConfirmedKeyDisplay)
	{
		ConfirmedKeyDisplay->SetWatchedInputAction(
			InputConfig ? InputConfig->WidgetConfirmedAction.Get() : nullptr);
	}

	if (EscapeKeyDisplay)
	{
		EscapeKeyDisplay->SetWatchedInputAction(
			InputConfig ? InputConfig->WidgetEscapeAction.Get() : nullptr);
	}
}

void UUILayerKeyHintWidget::SetConfirmedHintText(const FText& InHintText)
{
	if (ConfirmedKeyDisplay)
	{
		ConfirmedKeyDisplay->SetTextOverride(InHintText);
	}
}

void UUILayerKeyHintWidget::SetEscapeHintText(const FText& InHintText)
{
	if (EscapeKeyDisplay)
	{
		EscapeKeyDisplay->SetTextOverride(InHintText);
	}
}

void UUILayerKeyHintWidget::ClearHintTextOverrides()
{
	if (ConfirmedKeyDisplay)
	{
		ConfirmedKeyDisplay->ClearTextOverride();
	}

	if (EscapeKeyDisplay)
	{
		EscapeKeyDisplay->ClearTextOverride();
	}
}
