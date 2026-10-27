#include "UI/InteractionLockedWidget.h"

void UInteractionLockedWidget::ShowLockedPrompt()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionLockedWidget::HideLockedPrompt()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
