#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OutlierInfoTypes.h"
#include "InfoCanvasBase.generated.h"

/** HUD binding contract. The game module provides the concrete info widgets. */
UCLASS(Abstract, Blueprintable, meta = (DisableNativeTick))
class TAGDRIVENUI_API UInfoCanvasBase : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool EnqueueInfo(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request) { return false; }
	virtual bool CancelInfo(EOutlierInfoRegion Region, FGameplayTag EventTag) { return false; }
	virtual bool CompleteInfo(EOutlierInfoRegion Region, FGameplayTag EventTag) { return false; }
	virtual bool CompleteInputInfo(FGameplayTag EventTag) { return false; }
	virtual bool NotifyInfoOverlap(FGameplayTag EventTag) { return false; }
	virtual bool NotifyInfoOverlap(EOutlierInfoRegion Region, FGameplayTag EventTag)
	{
		return Region == EOutlierInfoRegion::Upper && NotifyInfoOverlap(EventTag);
	}
	virtual void CancelInputInfos() {}
	virtual FGameplayTag GetActiveInputEventTag() const { return FGameplayTag(); }

	/** The widget decides whether the source or its own display duration ends the item. */
	virtual void FinishInfoSource(EOutlierInfoRegion Region, FGameplayTag EventTag, bool bCompleted) {}

	virtual void ClearInfoQueues() {}
};
