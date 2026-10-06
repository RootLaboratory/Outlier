#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/InfoWidgetTypes.h"
#include "InfoQueueWidget.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnOutlierInfoItemShown, FGameplayTag /*EventTag*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOutlierInfoItemFinished, FGameplayTag /*EventTag*/, bool /*bCompleted*/);

/** One region, one FIFO queue. An idle region is collapsed and does not receive Slate ticks. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInfoQueueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UInfoQueueWidget(const FObjectInitializer& ObjectInitializer);

	bool EnqueueInfo(const FOutlierInfoWidgetRequest& Request);

	void CompleteCurrentInfo();

	bool CompleteInfo(FGameplayTag EventTag);

	bool NotifyOverlap(FGameplayTag EventTag);

	/** Completes/cancels a Source item without an independent timer. */
	void FinishSource(FGameplayTag EventTag, bool bCompleted);

	/** Removes this tag from both the active item and pending items. */
	bool CancelInfo(FGameplayTag EventTag);

	void ClearInfoQueue();

	bool HasCurrentInfo() const { return bHasCurrentInfo; }

	FGameplayTag GetCurrentEventTag() const { return bHasCurrentInfo ? CurrentRequest.EventTag : FGameplayTag(); }

	int32 GetPendingInfoCount() const { return PendingRequests.Num(); }

	float GetRemainingDuration() const { return RemainingDuration; }

	/** Uses the duration captured at enqueue time, including pending items. */
	bool IsInfoTimed(FGameplayTag EventTag) const;

	FOnOutlierInfoItemShown OnInfoItemShown;
	FOnOutlierInfoItemFinished OnInfoItemFinished;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** Resolves widget-owned DT defaults before retaining the request. */
	virtual bool PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const;
	virtual bool PresentInfo(const FOutlierInfoWidgetRequest& Request) { return true; }
	virtual void ResetInfoContent() {}
	virtual void InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted) {}
	bool TryGetInfoRequest(FGameplayTag EventTag, FOutlierInfoWidgetRequest& OutRequest) const;
	TArray<FOutlierInfoWidgetRequest> GetInfoRequests() const;

private:
	void AdvanceQueue();
	void FinishCurrentInfo(bool bCompleted);
	void HandleVisibilityChanged(ESlateVisibility Visibility);

	UPROPERTY(Transient)
	TArray<FOutlierInfoWidgetRequest> PendingRequests;

	UPROPERTY(Transient)
	FOutlierInfoWidgetRequest CurrentRequest;

	float RemainingDuration = 0.0f;
	uint32 PresentationGeneration = 0;
	bool bHasCurrentInfo = false;
	bool bAdvancingQueue = false;
	bool bFinishingCurrent = false;
	bool bClearingQueue = false;
	bool bAcceptingRequests = true;
};
