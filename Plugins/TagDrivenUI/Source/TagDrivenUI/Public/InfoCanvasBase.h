#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OutlierInfoTypes.h"
#include "InfoCanvasBase.generated.h"

/** HUD 바인딩 계약. 구체 info 위젯은 게임 모듈이 제공한다. */
UCLASS(Abstract, Blueprintable, meta = (DisableNativeTick))
class TAGDRIVENUI_API UInfoCanvasBase : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool EnqueueInfo(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request) { return false; }
	virtual bool CancelInfo(EOutlierInfoRegion Region, FGameplayTag EventTag) { return false; }
	virtual bool CompleteInfo(EOutlierInfoRegion Region, FGameplayTag EventTag) { return false; }
	virtual bool NotifyInfoOverlap(FGameplayTag EventTag) { return false; }
	virtual bool NotifyInfoOverlap(EOutlierInfoRegion Region, FGameplayTag EventTag)
	{
		return Region == EOutlierInfoRegion::Upper && NotifyInfoOverlap(EventTag);
	}

	/** 항목 종료를 소스가 할지 자체 표시 시간이 할지는 위젯이 정한다. */
	virtual void FinishInfoSource(EOutlierInfoRegion Region, FGameplayTag EventTag, bool bCompleted) {}

	virtual void ClearInfoQueues() {}
};
