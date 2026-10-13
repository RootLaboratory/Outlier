#include "UI/InputTutoWidget.h"

#include "Engine/LocalPlayer.h"
#include "Components/TextBlock.h"
#include "Event/OutlierEventSubsystem.h"
#include "UI/InputCombinationWidget.h"

bool UInputTutoWidget::SetInputEvent(FGameplayTag EventTag)
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	FOutlierUpperData Data;
	if (!Events || !Events->TryGetEventUpperData(EventTag, Data) || Data.Type != EOutlierUpperInfoType::Input)
	{
		return false;
	}
	return SetInputData(EventTag, Data.InputDisplay, Data.Text);
}

bool UInputTutoWidget::SetInputTag(FGameplayTag InputTag)
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	FOutlierUpperData Data;
	return Events && Events->TryGetUpperData(InputTag, Data) && Data.Type == EOutlierUpperInfoType::Input
		&& SetInputData(InputTag, Data.InputDisplay, Data.Text);
}

bool UInputTutoWidget::SetInputData(FGameplayTag RequestTag, const FInputDisplayDefinition& Display, const FText& Text)
{
	if (!RequestTag.IsValid() || !Display.IsValid() || !InputDisplay)
	{
		return false;
	}
	CurrentEventTag = RequestTag;
	CurrentText = Text;
	if (InstructionText)
	{
		InstructionText->SetText(Text);
	}
	InputDisplay->SetDisplayDefinition(Display);
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
	if (InputDisplay)
	{
		InputDisplay->ClearExpression();
	}
}
