#pragma once

#include "CoreMinimal.h"
#include "InfoCanvasBase.h"
#include "UI/InfoWidgetTypes.h"
#include "InfoCanvasWidget.generated.h"

class UInfoQueueWidget;
class UUpperWidget;
class URightGifBindedWidget;
class UBottomTTSScriptWidget;

/** MainUIBase.InfoLayer: three independent presentation queues. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UInfoCanvasWidget : public UInfoCanvasBase
{
	GENERATED_BODY()

public:
	virtual bool EnqueueInfo(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request) override;

	virtual bool CancelInfo(EOutlierInfoRegion Region, FGameplayTag EventTag) override;
	virtual bool CompleteInfo(EOutlierInfoRegion Region, FGameplayTag EventTag) override;
	virtual bool CompleteInputInfo(FGameplayTag EventTag) override;
	virtual bool NotifyInfoOverlap(FGameplayTag EventTag) override;
	virtual bool NotifyInfoOverlap(EOutlierInfoRegion Region, FGameplayTag EventTag) override;
	virtual void CancelInputInfos() override;
	virtual FGameplayTag GetActiveInputEventTag() const override;
	virtual void FinishInfoSource(EOutlierInfoRegion Region, FGameplayTag EventTag, bool bCompleted) override;

	UInfoQueueWidget* GetInfoRegion(EOutlierInfoRegion Region) const;

	virtual void ClearInfoQueues() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUpperWidget> UpperWidget;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URightGifBindedWidget> RightGifBindedWidget;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBottomTTSScriptWidget> BottomTTSScriptWidget;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
};
