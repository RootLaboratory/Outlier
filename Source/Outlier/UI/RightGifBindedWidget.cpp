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
	// GIF에는 오디오 소스가 없다: 기본 요청은 GIF DT의 표시 시간을 사용한다.
	if (Request.EndCondition == EOutlierInfoEndCondition::Source)
	{
		Request.EndCondition = EOutlierInfoEndCondition::Time;
	}
	FOutlierGifDataRow Data;
	if (!TryResolveGifData(Request, Data)) return false;
	// 이벤트만 지정한 요청을 포함해 chain은 해석된 GIF row를 키로 한다.
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
	FInputDisplayDefinition Display;
	if (CurrentGifData.InputTag.IsValid())
	{
		ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
		UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
		if (!Events || !Events->TryGetInputDisplayData(CurrentGifData.InputTag, Display)) return false;
	}
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
		InputWidget->SetInputData(Display, CurrentGifData.InputDisplayDesc);
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
