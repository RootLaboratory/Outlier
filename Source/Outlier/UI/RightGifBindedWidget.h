#pragma once

#include "CoreMinimal.h"
#include "Event/OutlierEventTypes.h"
#include "UI/InfoQueueWidget.h"
#include "RightGifBindedWidget.generated.h"

class UGifSubjectWidget;
class UGifInputWidget;
class UTextBlock;

/** 오른쪽 영역 Queue, GIF 메타데이터, 선택적 입력 키를 소유한다. GIF 렌더링은 예약 상태다. */
UCLASS(Blueprintable)
class OUTLIER_API URightGifBindedWidget : public UInfoQueueWidget
{
	GENERATED_BODY()

public:
	/** Subject 레이아웃과 내부 아이콘/텍스트 바인딩은 이 모듈의 WBP가 담당한다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UGifSubjectWidget> SubjectWidget;

	/** Input 레이아웃과 내부 키/설명 바인딩은 이 모듈의 WBP가 담당한다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UGifInputWidget> InputWidget;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DescriptionText;

protected:
	virtual void NativeOnInitialized() override;
	virtual bool PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const override;
	virtual bool PresentInfo(const FOutlierInfoWidgetRequest& Request) override;
	virtual void ResetInfoContent() override;
	virtual void InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted) override;

private:
	bool TryResolveGifData(const FOutlierInfoWidgetRequest& Request, FOutlierGifDataRow& OutData) const;

	UPROPERTY(Transient)
	FOutlierGifDataRow CurrentGifData;
};
