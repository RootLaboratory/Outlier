#include "UI/InteractInfoWidget.h"

#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

void UInteractInfoWidget::UpdateInteractInfo(const FGameplayTag& InteractTag, const FInteractInfoRow& InteractInfo)
{
	CurrentInteractTag = InteractTag;

	if (InteractText)
	{
		InteractText->SetText(InteractInfo.DisplayText);
	}

	if (InteractSizeBox)
	{
		InteractSizeBox->SetRenderScale(FVector2D(InteractInfo.Scale));
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractInfoWidget::ClearInteractInfo()
{
	CurrentInteractTag = FGameplayTag();

	if (InteractText)
	{
		InteractText->SetText(FText::GetEmpty());
	}

	if (InteractSizeBox)
	{
		InteractSizeBox->SetRenderScale(FVector2D::UnitVector);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}
