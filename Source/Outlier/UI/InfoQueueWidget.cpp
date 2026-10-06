#include "UI/InfoQueueWidget.h"

UInfoQueueWidget::UInfoQueueWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

void UInfoQueueWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::Collapsed);
	OnNativeVisibilityChanged.AddUObject(this, &ThisClass::HandleVisibilityChanged);
}

void UInfoQueueWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bAcceptingRequests = true;
	AdvanceQueue();
}

void UInfoQueueWidget::NativeDestruct()
{
	bAcceptingRequests = false;
	ClearInfoQueue();
	Super::NativeDestruct();
}

bool UInfoQueueWidget::EnqueueInfo(const FOutlierInfoWidgetRequest& Request)
{
	if (!bAcceptingRequests || bClearingQueue || !Request.EventTag.IsValid())
	{
		return false;
	}
	FOutlierInfoWidgetRequest WidgetRequest = Request;
	if (!PrepareInfoRequest(WidgetRequest)) return false;
	// Repeated requests keep their original duration and position.
	if ((bHasCurrentInfo && CurrentRequest.EventTag == Request.EventTag)
		|| PendingRequests.ContainsByPredicate([&Request](const FOutlierInfoWidgetRequest& Pending)
		{
			return Pending.EventTag == Request.EventTag;
		}))
	{
		return true;
	}
	PendingRequests.Add(WidgetRequest);
	AdvanceQueue();
	return true;
}

bool UInfoQueueWidget::PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const
{
	if (!FMath::IsFinite(Request.Duration) || Request.Duration < 0.0f) return false;
	switch (Request.EndCondition)
	{
	case EOutlierInfoEndCondition::Time:
		return Request.Duration > 0.0f;
	case EOutlierInfoEndCondition::Manual:
	case EOutlierInfoEndCondition::Overlap:
	case EOutlierInfoEndCondition::Source:
		Request.Duration = 0.0f;
		return true;
	default:
		return false;
	}
}

void UInfoQueueWidget::AdvanceQueue()
{
	if (bAdvancingQueue || bFinishingCurrent || bClearingQueue || !bAcceptingRequests || bHasCurrentInfo)
	{
		return;
	}
	TGuardValue<bool> AdvancingGuard(bAdvancingQueue, true);
	while (!bHasCurrentInfo && !PendingRequests.IsEmpty() && bAcceptingRequests && !bClearingQueue)
	{
		CurrentRequest = PendingRequests[0];
		PendingRequests.RemoveAt(0);
		RemainingDuration = CurrentRequest.Duration;
		bHasCurrentInfo = true;
		const uint32 Generation = ++PresentationGeneration;
		const FGameplayTag EventTag = CurrentRequest.EventTag;
		const FOutlierInfoWidgetRequest Request = CurrentRequest;
		if (!PresentInfo(Request))
		{
			if (Generation == PresentationGeneration && bHasCurrentInfo)
			{
				FinishCurrentInfo(false);
			}
			continue;
		}
		if (Generation == PresentationGeneration && bHasCurrentInfo && bAcceptingRequests)
		{
			SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (Generation == PresentationGeneration && bHasCurrentInfo)
			{
				OnInfoItemShown.Broadcast(EventTag);
			}
		}
	}
}

void UInfoQueueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	const uint32 Generation = PresentationGeneration;
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bHasCurrentInfo || Generation != PresentationGeneration || CurrentRequest.Duration <= 0.0f)
	{
		return;
	}
	RemainingDuration = FMath::Max(0.0f, RemainingDuration - InDeltaTime);
	if (RemainingDuration <= 0.0f)
	{
		FinishCurrentInfo(true);
	}
}

void UInfoQueueWidget::CompleteCurrentInfo()
{
	FinishCurrentInfo(true);
}

bool UInfoQueueWidget::TryGetInfoRequest(FGameplayTag EventTag, FOutlierInfoWidgetRequest& OutRequest) const
{
	if (bHasCurrentInfo && CurrentRequest.EventTag == EventTag)
	{
		OutRequest = CurrentRequest;
		return true;
	}
	if (const FOutlierInfoWidgetRequest* Pending = PendingRequests.FindByPredicate(
		[EventTag](const FOutlierInfoWidgetRequest& Request) { return Request.EventTag == EventTag; }))
	{
		OutRequest = *Pending;
		return true;
	}
	return false;
}

TArray<FOutlierInfoWidgetRequest> UInfoQueueWidget::GetInfoRequests() const
{
	TArray<FOutlierInfoWidgetRequest> Requests = PendingRequests;
	if (bHasCurrentInfo) Requests.Insert(CurrentRequest, 0);
	return Requests;
}

bool UInfoQueueWidget::CompleteInfo(FGameplayTag EventTag)
{
	if (!EventTag.IsValid() || bClearingQueue) return false;
	if (bHasCurrentInfo && CurrentRequest.EventTag == EventTag)
	{
		FinishCurrentInfo(true);
		return true;
	}
	const int32 Index = PendingRequests.IndexOfByPredicate(
		[EventTag](const FOutlierInfoWidgetRequest& Request) { return Request.EventTag == EventTag; });
	if (Index == INDEX_NONE) return false;
	const FOutlierInfoWidgetRequest Request = PendingRequests[Index];
	PendingRequests.RemoveAt(Index);
	InfoFinished(Request, true);
	OnInfoItemFinished.Broadcast(EventTag, true);
	return true;
}

bool UInfoQueueWidget::NotifyOverlap(FGameplayTag EventTag)
{
	FOutlierInfoWidgetRequest Request;
	return TryGetInfoRequest(EventTag, Request) && Request.EndCondition == EOutlierInfoEndCondition::Overlap
		&& CompleteInfo(EventTag);
}

void UInfoQueueWidget::FinishSource(FGameplayTag EventTag, bool bCompleted)
{
	FOutlierInfoWidgetRequest Request;
	if (!TryGetInfoRequest(EventTag, Request) || Request.EndCondition != EOutlierInfoEndCondition::Source)
	{
		return;
	}
	if (bCompleted) CompleteInfo(EventTag);
	else CancelInfo(EventTag);
}

bool UInfoQueueWidget::IsInfoTimed(FGameplayTag EventTag) const
{
	if (bHasCurrentInfo && CurrentRequest.EventTag == EventTag)
	{
		return CurrentRequest.Duration > 0.0f;
	}
	const FOutlierInfoWidgetRequest* Pending = PendingRequests.FindByPredicate(
		[EventTag](const FOutlierInfoWidgetRequest& Request) { return Request.EventTag == EventTag; });
	return Pending && Pending->Duration > 0.0f;
}

void UInfoQueueWidget::FinishCurrentInfo(bool bCompleted)
{
	if (!bHasCurrentInfo || bClearingQueue)
	{
		return;
	}
	{
		TGuardValue<bool> FinishingGuard(bFinishingCurrent, true);
		const FOutlierInfoWidgetRequest FinishedRequest = CurrentRequest;
		const FGameplayTag EventTag = FinishedRequest.EventTag;
		bHasCurrentInfo = false;
		CurrentRequest = FOutlierInfoWidgetRequest();
		RemainingDuration = 0.0f;
		++PresentationGeneration;
		// Queue continuations while the finishing guard still prevents their presentation.
		InfoFinished(FinishedRequest, bCompleted);
		SetVisibility(ESlateVisibility::Collapsed);
		ResetInfoContent();
		OnInfoItemFinished.Broadcast(EventTag, bCompleted);
	}
	AdvanceQueue();
}

bool UInfoQueueWidget::CancelInfo(FGameplayTag EventTag)
{
	if (!EventTag.IsValid())
	{
		return false;
	}
	if (bHasCurrentInfo && CurrentRequest.EventTag == EventTag)
	{
		FinishCurrentInfo(false);
		return true;
	}
	const int32 Index = PendingRequests.IndexOfByPredicate(
		[EventTag](const FOutlierInfoWidgetRequest& Request) { return Request.EventTag == EventTag; });
	if (Index == INDEX_NONE) return false;
	const FOutlierInfoWidgetRequest Request = PendingRequests[Index];
	PendingRequests.RemoveAt(Index);
	InfoFinished(Request, false);
	OnInfoItemFinished.Broadcast(EventTag, false);
	return true;
}

void UInfoQueueWidget::ClearInfoQueue()
{
	if (bClearingQueue)
	{
		return;
	}
	TGuardValue<bool> ClearingGuard(bClearingQueue, true);
	const TArray<FOutlierInfoWidgetRequest> CancelledRequests = GetInfoRequests();
	PendingRequests.Reset();
	bHasCurrentInfo = false;
	CurrentRequest = FOutlierInfoWidgetRequest();
	RemainingDuration = 0.0f;
	++PresentationGeneration;
	SetVisibility(ESlateVisibility::Collapsed);
	ResetInfoContent();
	for (const FOutlierInfoWidgetRequest& Request : CancelledRequests)
	{
		InfoFinished(Request, false);
		OnInfoItemFinished.Broadcast(Request.EventTag, false);
	}
}

void UInfoQueueWidget::HandleVisibilityChanged(ESlateVisibility InVisibility)
{
	if (InVisibility == ESlateVisibility::Collapsed && bHasCurrentInfo && !bClearingQueue)
	{
		FinishCurrentInfo(true);
	}
}
