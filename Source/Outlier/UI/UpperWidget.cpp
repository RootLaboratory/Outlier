#include "UI/UpperWidget.h"

#include "Engine/LocalPlayer.h"
#include "Event/OutlierEventSubsystem.h"
#include "UI/GoalWidget.h"
#include "UI/InputTutoWidget.h"

void UUpperWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ResetInfoContent();
}

bool UUpperWidget::PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	FOutlierUpperData Data;
	if (!Events || !(Request.UpperTag.IsValid()
		? Events->TryGetUpperData(Request.UpperTag, Data)
		: Events->TryGetEventUpperData(Request.EventTag, Data))) return false;
	Request.UpperTag = Data.UpperTag;
	if (Request.EndCondition == EOutlierInfoEndCondition::Time && Request.Duration == 0.0f) Request.Duration = Data.Duration;
	return Super::PrepareInfoRequest(Request);
}

bool UUpperWidget::PresentInfo(const FOutlierInfoWidgetRequest& Request)
{
	ResetInfoContent();
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	FOutlierUpperData Data;
	if (!Events || !(Request.UpperTag.IsValid()
		? Events->TryGetUpperData(Request.UpperTag, Data)
		: Events->TryGetEventUpperData(Request.EventTag, Data)))
	{
		return false;
	}
	if (Data.Type == EOutlierUpperInfoType::Input)
	{
		if (!InputTutoWidget || !InputTutoWidget->SetInputData(Request.EventTag, Data.InputDisplay, Data.Text))
		{
			return false;
		}
		InputTutoWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		bCurrentInput = true;
		return true;
	}
	if (Data.Type == EOutlierUpperInfoType::Goal && !Data.Text.IsEmpty() && GoalWidget && GoalWidget->GoalText)
	{
		GoalWidget->SetGoal(Request.EventTag, Data.Text);
		GoalWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return true;
	}
	return false;
}

void UUpperWidget::ResetInfoContent()
{
	bCurrentInput = false;
	if (InputTutoWidget)
	{
		InputTutoWidget->SetVisibility(ESlateVisibility::Collapsed);
		InputTutoWidget->ClearInputEvent();
	}
	if (GoalWidget)
	{
		GoalWidget->SetVisibility(ESlateVisibility::Collapsed);
		GoalWidget->ClearGoal();
	}
}

FGameplayTag UUpperWidget::GetActiveInputEventTag() const
{
	return bCurrentInput ? GetCurrentEventTag() : FGameplayTag();
}

bool UUpperWidget::CompleteInputInfo(FGameplayTag EventTag)
{
	FOutlierInfoWidgetRequest Request;
	if (!EventTag.IsValid() || !bCurrentInput || !TryGetInfoRequest(GetCurrentEventTag(), Request)
		|| (Request.EventTag != EventTag && Request.OriginEventTag != EventTag)) return false;
	// An input action completes only the displayed tutorial, even if another origin uses the same row.
	CompleteCurrentInfo();
	return true;
}

bool UUpperWidget::IsInputRequest(const FOutlierInfoWidgetRequest& Request) const
{
	if (Request.EventTag == GetCurrentEventTag()) return bCurrentInput;
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	FOutlierUpperData Data;
	return Events && (Request.UpperTag.IsValid() ? Events->TryGetUpperData(Request.UpperTag, Data)
		: Events->TryGetEventUpperData(Request.EventTag, Data)) && Data.Type == EOutlierUpperInfoType::Input;
}

void UUpperWidget::CancelInputInfos()
{
	TArray<FGameplayTag> Tags;
	for (const FOutlierInfoWidgetRequest& Request : GetInfoRequests())
	{
		if (IsInputRequest(Request)) Tags.Add(Request.EventTag);
	}
	// Remove pending tutorials first so cancelling the active one cannot briefly promote another.
	for (int32 Index = Tags.Num() - 1; Index >= 0; --Index) CancelInfo(Tags[Index]);
}

void UUpperWidget::InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted)
{
	if (!bCompleted) return;
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr)
	{
		Events->NotifyInfoCompleted(EOutlierInfoRegion::Upper, Request);
		if (Request.NextEventTag.IsValid()) Events->SendEvent(Request.NextEventTag);
	}
}
