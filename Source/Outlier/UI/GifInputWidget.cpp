#include "UI/GifInputWidget.h"

#include "Components/TextBlock.h"
#include "UI/InputCombinationWidget.h"

void UGifInputWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ClearInputData();
}

void UGifInputWidget::SetInputData(const FInputDisplayDefinition& Display, const FText& Description)
{
	ClearInputData();
	const bool bHasKey = Display.IsValid() && InputDisplay;
	const bool bHasDescription = !Description.IsEmpty() && InputDisplayDescText;
	if (bHasKey)
	{
		InputDisplay->SetDisplayDefinition(Display);
		InputDisplay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (bHasDescription)
	{
		InputDisplayDescText->SetText(Description);
		InputDisplayDescText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	SetVisibility(bHasKey || bHasDescription ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UGifInputWidget::ClearInputData()
{
	if (InputDisplay)
	{
		InputDisplay->ClearExpression();
		InputDisplay->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (InputDisplayDescText)
	{
		InputDisplayDescText->SetText(FText::GetEmpty());
		InputDisplayDescText->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}
