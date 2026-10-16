#include "UI/InfoQueueWidget.h"

namespace
{
	bool MatchesEvent(const FOutlierInfoWidgetRequest& Request, FGameplayTag Tag)
	{
		return Request.EventTag == Tag || Request.OriginEventTag == Tag;
	}
}

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
	if (!WidgetRequest.OriginEventTag.IsValid()) WidgetRequest.OriginEventTag = WidgetRequest.EventTag;
	if (!PrepareInfoRequest(WidgetRequest)) return false;
	// 반복 요청은 원래의 지속 시간과 위치를 유지한다.
	if ((bHasCurrentInfo && CurrentRequest.EventTag == WidgetRequest.EventTag
		&& CurrentRequest.OriginEventTag == WidgetRequest.OriginEventTag)
		|| PendingRequests.ContainsByPredicate([&WidgetRequest](const FOutlierInfoWidgetRequest& Pending)
		{
			return Pending.EventTag == WidgetRequest.EventTag && Pending.OriginEventTag == WidgetRequest.OriginEventTag;
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
	if (bHasCurrentInfo && MatchesEvent(CurrentRequest, EventTag))
	{
		OutRequest = CurrentRequest;
		return true;
	}
	if (const FOutlierInfoWidgetRequest* Pending = PendingRequests.FindByPredicate(
		[EventTag](const FOutlierInfoWidgetRequest& Request) { return MatchesEvent(Request, EventTag); }))
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
	// 기존 매치만 스냅샷한다: 완료 처리 중 다음 chain이 enqueue될 수 있고, 그 chain은 계속 실행되어야 한다.
	const TArray<FOutlierInfoWidgetRequest> Requests = GetInfoRequests();
	bool bCompleted = false;
	for (const FOutlierInfoWidgetRequest& Request : Requests)
	{
		if (MatchesEvent(Request, EventTag)) bCompleted |= CompleteInfoExact(Request.EventTag, Request.OriginEventTag);
	}
	return bCompleted;
}

bool UInfoQueueWidget::CompleteInfoExact(FGameplayTag RequestTag, FGameplayTag OriginEventTag)
{
	if (bHasCurrentInfo && CurrentRequest.EventTag == RequestTag && CurrentRequest.OriginEventTag == OriginEventTag)
	{
		FinishCurrentInfo(true);
		return true;
	}
	const int32 Index = PendingRequests.IndexOfByPredicate(
		[RequestTag, OriginEventTag](const FOutlierInfoWidgetRequest& Request)
		{
			return Request.EventTag == RequestTag && Request.OriginEventTag == OriginEventTag;
		});
	if (Index == INDEX_NONE) return false;
	const FOutlierInfoWidgetRequest Request = PendingRequests[Index];
	PendingRequests.RemoveAt(Index);
	InfoFinished(Request, true);
	OnInfoItemFinished.Broadcast(RequestTag, true);
	return true;
}

bool UInfoQueueWidget::NotifyOverlap(FGameplayTag EventTag)
{
	const TArray<FOutlierInfoWidgetRequest> Requests = GetInfoRequests();
	bool bCompleted = false;
	for (const FOutlierInfoWidgetRequest& Request : Requests)
	{
		if (MatchesEvent(Request, EventTag) && Request.EndCondition == EOutlierInfoEndCondition::Overlap)
			bCompleted |= CompleteInfoExact(Request.EventTag, Request.OriginEventTag);
	}
	return bCompleted;
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
	FOutlierInfoWidgetRequest Request;
	return TryGetInfoRequest(EventTag, Request) && Request.Duration > 0.0f;
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
		// finishing guard가 아직 표시를 막고 있는 동안 후속 항목을 Queue에 넣는다.
		InfoFinished(FinishedRequest, bCompleted);
		SetVisibility(ESlateVisibility::Collapsed);
		ResetInfoContent();
		OnInfoItemFinished.Broadcast(EventTag, bCompleted);
	}
	AdvanceQueue();
}

bool UInfoQueueWidget::CancelInfo(FGameplayTag EventTag)
{
	if (!EventTag.IsValid() || bClearingQueue) return false;
	const TArray<FOutlierInfoWidgetRequest> Requests = GetInfoRequests();
	bool bCancelled = false;
	// 활성 항목 취소 시 취소된 항목이 승격되지 않도록 대기 중인 매치를 먼저 제거한다.
	for (int32 Index = Requests.Num() - 1; Index >= 0; --Index)
	{
		const FOutlierInfoWidgetRequest& Request = Requests[Index];
		if (MatchesEvent(Request, EventTag)) bCancelled |= CancelInfoExact(Request.EventTag, Request.OriginEventTag);
	}
	return bCancelled;
}

bool UInfoQueueWidget::CancelInfoExact(FGameplayTag RequestTag, FGameplayTag OriginEventTag)
{
	if (bHasCurrentInfo && CurrentRequest.EventTag == RequestTag && CurrentRequest.OriginEventTag == OriginEventTag)
	{
		FinishCurrentInfo(false);
		return true;
	}
	const int32 Index = PendingRequests.IndexOfByPredicate(
		[RequestTag, OriginEventTag](const FOutlierInfoWidgetRequest& Request)
		{
			return Request.EventTag == RequestTag && Request.OriginEventTag == OriginEventTag;
		});
	if (Index == INDEX_NONE) return false;
	const FOutlierInfoWidgetRequest Request = PendingRequests[Index];
	PendingRequests.RemoveAt(Index);
	InfoFinished(Request, false);
	OnInfoItemFinished.Broadcast(RequestTag, false);
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
