#include "UI/GoalWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/TextBlock.h"

void UGoalWidget::SetGoal(FGameplayTag EventTag, const FText& Text)
{
	CurrentEventTag = EventTag;
	CurrentGoalText = Text;
	if (EventTag.IsValid() && !Text.IsEmpty())
		OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Ingame.Tutorial.Goal"));
	if (GoalText)
	{
		GoalText->SetText(Text);
	}
}

void UGoalWidget::ClearGoal()
{
	SetGoal(FGameplayTag(), FText::GetEmpty());
}
