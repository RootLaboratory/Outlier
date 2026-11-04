#include "UI/InteractionDescWidget.h"

#include "Audio/OutlierAudioFeedback.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "PopupRetainerBox.h"

void UInteractionDescWidget::UpdateInteractionDesc(const FGameplayTag& InteractTag, const FInteractInfoRow& InteractInfo, float InProgress)
{
	CurrentInteractTag = InteractTag;

	if (DescText)
	{
		DescText->SetText(InteractInfo.DisplayText);
	}
	SetProgress(InProgress);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();

	if (PopupRetainerBox)
	{
		PopupRetainerBox->RequestRender();
	}
}

void UInteractionDescWidget::SetProgress(float InProgress)
{
	if (ProgressBar)
	{
		ProgressBar->SetPercent(FMath::Clamp(InProgress, 0.0f, 1.0f));
	}
}

void UInteractionDescWidget::ClearInteractionDesc()
{
	CurrentInteractTag = FGameplayTag();

	if (DescText)
	{
		DescText->SetText(FText::GetEmpty());
	}

	SetProgress(0.0f);
	SetVisibility(ESlateVisibility::Collapsed);
	SetPopupAudioOpen(false);
}

void UInteractionDescWidget::PlayPopUp(bool InFlag)
{
	if (PopupRetainerBox)
	{
		PopupRetainerBox->PlayPopup(InFlag);
	}
	// InFlag가 false면 열기, true면 닫기 연출이다.
	SetPopupAudioOpen(!InFlag);
}

void UInteractionDescWidget::NativeDestruct()
{
	// Desc 액터가 파괴되며 닫히는 경우에도 닫힘음을 낸다. 월드 정리 중에는 생략한다.
	const UWorld* World = GetWorld();
	if (World && !World->bIsTearingDown)
	{
		SetPopupAudioOpen(false);
	}
	Super::NativeDestruct();
}

void UInteractionDescWidget::SetPopupAudioOpen(bool bOpen)
{
	if (bPopupAudioOpen == bOpen)
	{
		return;
	}
	bPopupAudioOpen = bOpen;
	OutlierAudioFeedback::Local(this, bOpen ? TEXT("Audio.Context.UI.Ingame.Open") : TEXT("Audio.Context.UI.Ingame.Close"));
}
