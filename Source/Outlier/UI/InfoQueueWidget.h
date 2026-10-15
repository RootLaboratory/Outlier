#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/InfoWidgetTypes.h"
#include "InfoQueueWidget.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnOutlierInfoItemShown, FGameplayTag /*EventTag*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOutlierInfoItemFinished, FGameplayTag /*EventTag*/, bool /*bCompleted*/);

/** 영역 하나당 FIFO Queue 하나. 유휴 영역은 Collapsed되어 Slate tick을 받지 않는다. */
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

	/** 독립 타이머 없이 Source 항목을 완료/취소한다. */
	void FinishSource(FGameplayTag EventTag, bool bCompleted);

	/** 활성 항목과 대기 항목 모두에서 이 태그를 제거한다. */
	bool CancelInfo(FGameplayTag EventTag);

	void ClearInfoQueue();

	bool HasCurrentInfo() const { return bHasCurrentInfo; }

	FGameplayTag GetCurrentEventTag() const { return bHasCurrentInfo ? CurrentRequest.EventTag : FGameplayTag(); }

	int32 GetPendingInfoCount() const { return PendingRequests.Num(); }

	float GetRemainingDuration() const { return RemainingDuration; }

	/** 대기 항목을 포함해 enqueue 시점에 캡처한 지속 시간을 사용한다. */
	bool IsInfoTimed(FGameplayTag EventTag) const;

	FOnOutlierInfoItemShown OnInfoItemShown;
	FOnOutlierInfoItemFinished OnInfoItemFinished;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 요청을 보관하기 전에 위젯이 소유한 DT 기본값을 해석한다. */
	virtual bool PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const;
	virtual bool PresentInfo(const FOutlierInfoWidgetRequest& Request) { return true; }
	virtual void ResetInfoContent() {}
	virtual void InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted) {}
	bool TryGetInfoRequest(FGameplayTag EventTag, FOutlierInfoWidgetRequest& OutRequest) const;
	TArray<FOutlierInfoWidgetRequest> GetInfoRequests() const;

private:
	bool CompleteInfoExact(FGameplayTag RequestTag, FGameplayTag OriginEventTag);
	bool CancelInfoExact(FGameplayTag RequestTag, FGameplayTag OriginEventTag);
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
