#include "UI/InfoCanvasWidget.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/OverlaySlot.h"
#include "UI/BottomTTSScriptWidget.h"
#include "UI/RightGifBindedWidget.h"
#include "UI/UpperWidget.h"

void UInfoCanvasWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Regions anchor to the full screen at every resolution, whatever slot MainUI placed this in.
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		CanvasSlot->SetOffsets(FMargin(0.0f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	}
	else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
		OverlaySlot->SetPadding(FMargin(0.0f));
	}
}

UInfoQueueWidget* UInfoCanvasWidget::GetInfoRegion(EOutlierInfoRegion Region) const
{
	switch (Region)
	{
	case EOutlierInfoRegion::Upper: return UpperWidget;
	case EOutlierInfoRegion::Right: return RightGifBindedWidget;
	case EOutlierInfoRegion::BottomTTS: return BottomTTSScriptWidget;
	default: return nullptr;
	}
}

bool UInfoCanvasWidget::EnqueueInfo(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request)
{
	UInfoQueueWidget* Widget = GetInfoRegion(Region);
	return Widget && Widget->EnqueueInfo(Request);
}

bool UInfoCanvasWidget::CancelInfo(EOutlierInfoRegion Region, FGameplayTag EventTag)
{
	UInfoQueueWidget* Widget = GetInfoRegion(Region);
	return Widget && Widget->CancelInfo(EventTag);
}

bool UInfoCanvasWidget::CompleteInfo(EOutlierInfoRegion Region, FGameplayTag EventTag)
{
	UInfoQueueWidget* Widget = GetInfoRegion(Region);
	return Widget && Widget->CompleteInfo(EventTag);
}

bool UInfoCanvasWidget::CompleteInputInfo(FGameplayTag EventTag)
{
	return UpperWidget && UpperWidget->CompleteInputInfo(EventTag);
}

bool UInfoCanvasWidget::NotifyInfoOverlap(FGameplayTag EventTag)
{
	return NotifyInfoOverlap(EOutlierInfoRegion::Upper, EventTag);
}

bool UInfoCanvasWidget::NotifyInfoOverlap(EOutlierInfoRegion Region, FGameplayTag EventTag)
{
	UInfoQueueWidget* Widget = GetInfoRegion(Region);
	return Widget && Widget->NotifyOverlap(EventTag);
}

void UInfoCanvasWidget::CancelInputInfos()
{
	if (UpperWidget) UpperWidget->CancelInputInfos();
}

FGameplayTag UInfoCanvasWidget::GetActiveInputEventTag() const
{
	return UpperWidget ? UpperWidget->GetActiveInputEventTag() : FGameplayTag();
}

void UInfoCanvasWidget::FinishInfoSource(EOutlierInfoRegion Region, FGameplayTag EventTag, bool bCompleted)
{
	if (UInfoQueueWidget* Widget = GetInfoRegion(Region)) Widget->FinishSource(EventTag, bCompleted);
}

void UInfoCanvasWidget::ClearInfoQueues()
{
	if (UpperWidget) UpperWidget->ClearInfoQueue();
	if (RightGifBindedWidget) RightGifBindedWidget->ClearInfoQueue();
	if (BottomTTSScriptWidget) BottomTTSScriptWidget->ClearInfoQueue();
}

void UInfoCanvasWidget::NativeDestruct()
{
	ClearInfoQueues();
	Super::NativeDestruct();
}
