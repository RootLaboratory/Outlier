#include "UI/GoalWidget.h"

#include "Components/TextBlock.h"

void UGoalWidget::SetGoal(FGameplayTag EventTag, const FText& Text)
{
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
