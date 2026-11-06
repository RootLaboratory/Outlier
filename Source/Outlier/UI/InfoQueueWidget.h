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
	bool IsInfoPaused() const;
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 요청을 보관하기 전에 위젯이 소유한 DT 기본값을 해석한다. */
	virtual bool PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const;
	virtual bool PresentInfo(const FOutlierInfoWidgetRequest& Request) { return true; }
	/** ScaleBox/줄바꿈의 초기 배치를 투명한 상태에서 준비할 프레임 수. */
	virtual int32 GetPresentationWarmupFrames() const { return 0; }
	/** 투명한 레이아웃 준비가 끝난 뒤 표시 효과를 시작한다. */
	virtual void StartInfoPresentation() {}
	virtual void ResetInfoContent() {}
	virtual void InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted) {}
	/** 현재 항목에 Fade in/out을 적용할지 결정한다. PresentInfo 이후에 호출된다. */
	virtual bool ShouldFadeCurrentInfo() const { return false; }
	bool TryGetInfoRequest(FGameplayTag EventTag, FOutlierInfoWidgetRequest& OutRequest) const;
	TArray<FOutlierInfoWidgetRequest> GetInfoRequests() const;

	/** 표시 시작 시 투명도를 0에서 원래 값까지 올리는 시간. 0이면 즉시 표시한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info|Fade", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInDuration = 0.25f;

	/** 완료 시 투명도를 0까지 내린 뒤 다음 항목으로 넘어가는 시간. 0이면 즉시 종료한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info|Fade", meta = (ClampMin = "0.0", Units = "s"))
	float FadeOutDuration = 0.25f;

private:
	enum class EInfoFadePhase : uint8 { None, In, Out };

	/** 완료 요청 진입점. 페이드가 가능하면 Fade out 후 FinishCurrentInfo를 호출한다. */
	void RequestFinishCurrentInfo(bool bCompleted);
	void BeginFadeIn();
	/** Fade 진행을 처리한다. 현재 항목이 끝났으면 true를 반환한다. */
	bool TickFade(float DeltaTime);
	bool IsFadingOut() const { return FadePhase == EInfoFadePhase::Out; }

	bool CompleteInfoExact(FGameplayTag RequestTag, FGameplayTag OriginEventTag);
	bool CancelInfoExact(FGameplayTag RequestTag, FGameplayTag OriginEventTag);
	void AdvanceQueue();
	void FinishCurrentInfo(bool bCompleted);
	void ResetPresentationWarmup();
	void HandleVisibilityChanged(ESlateVisibility Visibility);

	UPROPERTY(Transient)
	TArray<FOutlierInfoWidgetRequest> PendingRequests;

	UPROPERTY(Transient)
	FOutlierInfoWidgetRequest CurrentRequest;

	float RemainingDuration = 0.0f;
	uint32 PresentationGeneration = 0;
	int32 PresentationWarmupFramesRemaining = 0;
	uint64 LastPresentationWarmupFrame = 0;
	float PresentationRenderOpacity = 1.0f;
	EInfoFadePhase FadePhase = EInfoFadePhase::None;
	float FadeElapsed = 0.0f;
	float FadeOutStartOpacity = 1.0f;
	bool bFadeOutCompleted = false;
	bool bHasCurrentInfo = false;
	bool bAdvancingQueue = false;
	bool bFinishingCurrent = false;
	bool bClearingQueue = false;
	bool bAcceptingRequests = true;
};
