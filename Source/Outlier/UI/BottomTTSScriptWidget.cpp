#include "UI/BottomTTSScriptWidget.h"

#include "Components/TextBlock.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBoxSlot.h"
#include "Engine/LocalPlayer.h"
#include "Event/OutlierEventSubsystem.h"
#include "Styling/SlateColor.h"
#include "UI/BottomTTSSpeakerLayout.h"
#include "UI/BottomTTSSpeakerHost.h"

UBottomTTSScriptWidget::UBottomTTSScriptWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// "깡통이": escaped so the literal does not depend on the source file encoding.
	SpeakerColors.Add(TEXT("깡통이"), FLinearColor::White);
	SpeakerColors.Add(TEXT("Genesis"), FLinearColor::White);
}

void UBottomTTSScriptWidget::NativeConstruct()
{
	if (Speaker)
	{
		OriginalSpeakerTranslation = Speaker->GetRenderTransform().Translation;
		// 기존 고정 폭 SizeBox 안에서도 측정되는 라벨 폭을 텍스트 폭과 같게 유지한다.
		if (USizeBoxSlot* SpeakerSlot = Cast<USizeBoxSlot>(Speaker->Slot))
		{
			SpeakerSlot->SetHorizontalAlignment(HAlign_Left);
			SpeakerSlot->SetVerticalAlignment(VAlign_Top);
		}
		for (UWidget* Widget = Speaker; Widget; Widget = Widget->GetParent())
		{
			if (UHorizontalBoxSlot* RowSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
			{
				RowSlot->SetVerticalAlignment(VAlign_Top);
				break;
			}
		}
	}
	if (ScriptText) ScriptText->SetJustification(ETextJustify::Center);
	Super::NativeConstruct();
}

void UBottomTTSScriptWidget::NativeDestruct()
{
	Super::NativeDestruct();
	if (Speaker) Speaker->SetRenderTranslation(OriginalSpeakerTranslation);
	SpeakerLayout.Reset();
}

void UBottomTTSScriptWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	SpeakerLayout.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

TSharedRef<SWidget> UBottomTTSScriptWidget::RebuildWidget()
{
	const TSharedRef<SWidget> Content = Super::RebuildWidget();
	if (IsDesignTime()) return Content;
	return SNew(SBottomTTSSpeakerHost)
		.OnArrangeSubtitle(FOnArrangeBottomTTS::CreateUObject(this, &UBottomTTSScriptWidget::UpdateSpeakerPosition))
		[Content];
}

void UBottomTTSScriptWidget::UpdateSpeakerPosition(const TSharedRef<SWidget>& Content, const FGeometry& ContentGeometry)
{
	if (!HasCurrentInfo() || !Speaker || !ScriptText || CurrentSpeaker.IsEmpty()) return;
	if (!SpeakerLayout) SpeakerLayout = MakeShared<FBottomTTSSpeakerLayout>();
	SpeakerLayout->AlignSpeakerInTree(*Speaker, *ScriptText, Content, ContentGeometry, SpeakerGap);
}

bool UBottomTTSScriptWidget::PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const
{
	// 자막 수명은 pitch 변경과 중단을 포함해 실제 음성을 따른다.
	// 재생할 수 없는 음성은 자막을 표시하지 않는다.
	Request.EndCondition = EOutlierInfoEndCondition::Source;
	Request.Duration = 0.0f;
	return Super::PrepareInfoRequest(Request);
}

bool UBottomTTSScriptWidget::PresentInfo(const FOutlierInfoWidgetRequest& Request)
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	FOutlierEventDataRow Event;
	FOutlierTTSDataRow TTS;
	if (!ScriptText || !Speaker || !Events) return false;
	FGameplayTag TTSTag = Request.TTSTag;
	if (!TTSTag.IsValid())
	{
		if (!Events->TryGetEventData(Request.EventTag, Event)) return false;
		TTSTag = Event.TTSTag;
	}
	if (!Events->TryGetTTSData(TTSTag, TTS))
	{
		return false;
	}
	CurrentScript = TTS.Script;
	CurrentSpeaker = TTS.Speaker;
	ScriptText->SetText(CurrentScript);
	Speaker->SetText(CurrentSpeaker.IsEmpty()
		? FText::GetEmpty()
		: FText::FromString(CurrentSpeaker + TEXT(":")));
	const FLinearColor* SpeakerColor = SpeakerColors.Find(CurrentSpeaker);
	Speaker->SetColorAndOpacity(FSlateColor(SpeakerColor ? *SpeakerColor : DefaultSpeakerColor));
	Speaker->SetVisibility(CurrentSpeaker.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	return true;
}

void UBottomTTSScriptWidget::ResetInfoContent()
{
	CurrentScript = FText::GetEmpty();
	CurrentSpeaker.Empty();
	if (Speaker)
	{
		Speaker->SetText(FText::GetEmpty());
		Speaker->SetColorAndOpacity(FSlateColor(DefaultSpeakerColor));
		Speaker->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ScriptText)
	{
		ScriptText->SetText(CurrentScript);
	}
}

void UBottomTTSScriptWidget::InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted)
{
	// 음성이 자연 종료됐거나 fallback 자막 시간이 끝났다: TTS chain을 이어간다.
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (UOutlierEventSubsystem* Events = bCompleted && LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr)
	{
		Events->NotifyInfoCompleted(EOutlierInfoRegion::BottomTTS, Request);
	}
}
