#include "UI/GifInputWidget.h"

#include "Components/TextBlock.h"
#include "InputAction.h"
#include "UI/InputActionKeyDisplayWidget.h"

void UGifInputWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ClearInputData();
}

void UGifInputWidget::SetInputData(UInputAction* InputAction, const FText& Description)
{
	ClearInputData();
	const bool bHasKey = IsValid(InputAction) && InputBindWidgetKey;
	const bool bHasDescription = !Description.IsEmpty() && InputDisplayDescText;
	if (bHasKey)
	{
		InputBindWidgetKey->ClearTextOverride();
		InputBindWidgetKey->SetWatchedInputAction(InputAction);
		InputBindWidgetKey->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
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
	if (InputBindWidgetKey)
	{
		InputBindWidgetKey->SetWatchedInputAction(nullptr);
		InputBindWidgetKey->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (InputDisplayDescText)
	{
		InputDisplayDescText->SetText(FText::GetEmpty());
		InputDisplayDescText->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}
