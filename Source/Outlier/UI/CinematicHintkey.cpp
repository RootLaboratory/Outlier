#include "UI/CinematicHintkey.h"

#include "Components/TextBlock.h"
#include "UI/InputActionKeyDisplayWidget.h"

void UCinematicHintkey::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshHint();
}

void UCinematicHintkey::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshHint();
}

void UCinematicHintkey::SetCompleteCinematicKey(FKey Key)
{
	CompleteCinematicKey = Key;
	RefreshHint();
}

void UCinematicHintkey::SetHintText(const FText& InText)
{
	HintMessage = InText;
	RefreshHint();
}

void UCinematicHintkey::RefreshHint()
{
	if (InputDisplayWidget) InputDisplayWidget->SetDisplayedKey(CompleteCinematicKey);
	if (HintText) HintText->SetText(HintMessage);
}
