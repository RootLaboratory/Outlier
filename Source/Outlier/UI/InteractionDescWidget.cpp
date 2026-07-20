#include "UI/InteractionDescWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UInteractionDescWidget::UpdateInteractionDesc(const FGameplayTag& InteractTag, const FInteractInfoRow& InteractInfo, float InProgress)
{
	CurrentInteractTag = InteractTag;

	if (DescText)
	{
		DescText->SetText(InteractInfo.DisplayText);
	}

	SetProgress(InProgress);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionDescWidget::SetProgress(float InProgress)
{
	if (ProgressBar)
	{
		ProgressBar->SetPercent(FMath::Clamp(InProgress, 0.0f, 1.0f));
	}
}

void UInteractionDescWidget::ClearInteractionDesc()
{
	CurrentInteractTag = FGameplayTag();

	if (DescText)
	{
		DescText->SetText(FText::GetEmpty());
	}

	SetProgress(0.0f);
	SetVisibility(ESlateVisibility::Collapsed);
}
