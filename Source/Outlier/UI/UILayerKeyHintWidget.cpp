#include "UI/UILayerKeyHintWidget.h"

#include "FrontendPlayerController.h"
#include "UI/InputActionKeyDisplayWidget.h"

void UUILayerKeyHintWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshKeyTexts();
}

void UUILayerKeyHintWidget::RefreshKeyTexts()
{
	const AFrontendPlayerController* FrontendPlayerController =
		Cast<AFrontendPlayerController>(GetOwningPlayer());

	if (ConfirmedKeyDisplay)
	{
		ConfirmedKeyDisplay->SetWatchedInputAction(
			FrontendPlayerController
				? FrontendPlayerController->GetWidgetConfirmedAction()
				: nullptr);
	}

	if (EscapeKeyDisplay)
	{
		EscapeKeyDisplay->SetWatchedInputAction(
			FrontendPlayerController
				? FrontendPlayerController->GetWidgetEscapeAction()
				: nullptr);
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
