#include "UI/InteractKeyWidget.h"

#include "UI/InputActionKeyDisplayWidget.h"

void UInteractKeyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (KeyDisplay)
	{
		KeyDisplay->SetWatchedInputAction(InteractionAction);
	}
}

void UInteractKeyWidget::UpdateInteractKey()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractKeyWidget::ClearInteractKey()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
