#include "UI/GoalWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/TextBlock.h"
#include "PopupRetainerBox.h"

void UGoalWidget::SetGoal(FGameplayTag EventTag, const FText& Text)
{
	if (PopupRetainerBox)
	{
		PopupRetainerBox->bPauseWithWorld = true;
		PopupRetainerBox->ResetPopup();
		// Keep Goal content in layout during Upper's transparent warmup.
		PopupRetainerBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	CurrentEventTag = EventTag;
	CurrentGoalText = Text;
	if (GoalText)
	{
		GoalText->SetText(Text);
	}
}

void UGoalWidget::ClearGoal()
{
	SetGoal(FGameplayTag(), FText::GetEmpty());
}

void UGoalWidget::PlayGoalPopup()
{
	if (CurrentEventTag.IsValid() && !CurrentGoalText.IsEmpty() && PopupRetainerBox)
	{
		OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Ingame.Tutorial.Goal"));
		PopupRetainerBox->PlayOpen();
	}
}
