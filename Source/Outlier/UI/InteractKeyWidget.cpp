#include "UI/InteractKeyWidget.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"

void UInteractKeyWidget::UpdateInteractKey(const FText& InInteractKeyText)
{
	if (InteractKeyText)
	{
		InteractKeyText->SetText(InInteractKeyText);
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractKeyWidget::ClearInteractKey()
{
	if (InteractKeyText)
	{
		InteractKeyText->SetText(FText::GetEmpty());
	}

	SetVisibility(ESlateVisibility::Collapsed);
}
