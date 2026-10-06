#include "UI/InputTutoWidget.h"

#include "Engine/LocalPlayer.h"
#include "Components/TextBlock.h"
#include "Event/OutlierEventSubsystem.h"
#include "InputAction.h"
#include "UI/InputActionKeyDisplayWidget.h"

bool UInputTutoWidget::SetInputEvent(FGameplayTag EventTag)
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	FOutlierUpperData Data;
	if (!Events || !Events->TryGetEventUpperData(EventTag, Data) || Data.Type != EOutlierUpperInfoType::Input)
	{
		return false;
	}
	return SetInputData(EventTag, Data.InputAction, Data.Text);
}

bool UInputTutoWidget::SetInputData(FGameplayTag EventTag, UInputAction* InputAction, const FText& Text)
{
	if (!EventTag.IsValid() || !IsValid(InputAction) || !InputBindWidgetKey)
	{
		return false;
	}
	CurrentEventTag = EventTag;
	CurrentText = Text;
	if (InstructionText)
	{
		InstructionText->SetText(Text);
	}
	InputBindWidgetKey->ClearTextOverride();
	InputBindWidgetKey->SetWatchedInputAction(InputAction);
	return true;
}

void UInputTutoWidget::ClearInputEvent()
{
	CurrentEventTag = FGameplayTag();
	CurrentText = FText::GetEmpty();
	if (InstructionText)
	{
		InstructionText->SetText(FText::GetEmpty());
	}
	if (InputBindWidgetKey)
	{
		InputBindWidgetKey->SetWatchedInputAction(nullptr);
	}
}
