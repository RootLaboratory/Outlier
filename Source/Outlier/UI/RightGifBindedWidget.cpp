#include "UI/RightGifBindedWidget.h"

#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Event/OutlierEventSubsystem.h"
#include "Outlier.h"
#include "UI/GifInputWidget.h"
#include "UI/GifSubjectWidget.h"

void URightGifBindedWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ResetInfoContent();
}

bool URightGifBindedWidget::TryResolveGifData(const FOutlierInfoWidgetRequest& Request, FOutlierGifDataRow& OutData) const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	if (!Events) return false;
	return Request.GifTag.IsValid()
		? Events->TryGetGifData(Request.GifTag, OutData)
		: Events->TryGetEventGifData(Request.EventTag, OutData);
}

bool URightGifBindedWidget::PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const
{
	// GIF has no audio source: default requests use the GIF DT's display time.
	if (Request.EndCondition == EOutlierInfoEndCondition::Source)
	{
		Request.EndCondition = EOutlierInfoEndCondition::Time;
	}
	FOutlierGifDataRow Data;
	if (!TryResolveGifData(Request, Data)) return false;
	// Chains key on the resolved GIF row, including requests that named only the event.
	Request.GifTag = Data.Tag;
	if (Request.EndCondition == EOutlierInfoEndCondition::Time && Request.Duration == 0.0f)
	{
		Request.Duration = Data.Time;
	}
	return Super::PrepareInfoRequest(Request);
}

bool URightGifBindedWidget::PresentInfo(const FOutlierInfoWidgetRequest& Request)
{
	ResetInfoContent();
	if (!TryResolveGifData(Request, CurrentGifData)) return false;
	if (DescriptionText) DescriptionText->SetText(CurrentGifData.Description);
	if (CurrentGifData.bSubjectIconExist && !IsValid(CurrentGifData.SubjectIconTexture))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GIF UI] '%s' enables Subject Icon Exist without a valid SubjectIconTexture."), *CurrentGifData.Tag.ToString());
	}
	if (SubjectWidget)
	{
		SubjectWidget->SetSubjectData(CurrentGifData.Subject, CurrentGifData.bSubjectIconExist, CurrentGifData.SubjectIconTexture);
	}
	if (InputWidget)
	{
		InputWidget->SetInputData(CurrentGifData.InputAction, CurrentGifData.InputDisplayDesc);
	}
	return true;
}

void URightGifBindedWidget::ResetInfoContent()
{
	CurrentGifData = FOutlierGifDataRow();
	if (DescriptionText) DescriptionText->SetText(FText::GetEmpty());
	if (SubjectWidget) SubjectWidget->ClearSubjectData();
	if (InputWidget) InputWidget->ClearInputData();
}

void URightGifBindedWidget::InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted)
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (UOutlierEventSubsystem* Events = bCompleted && LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr)
	{
		Events->NotifyInfoCompleted(EOutlierInfoRegion::Right, Request);
	}
}
