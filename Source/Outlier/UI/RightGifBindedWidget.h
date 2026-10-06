#pragma once

#include "CoreMinimal.h"
#include "Event/OutlierEventTypes.h"
#include "UI/InfoQueueWidget.h"
#include "RightGifBindedWidget.generated.h"

class UGifSubjectWidget;
class UGifInputWidget;
class UTextBlock;

/** Owns the right-region queue, GIF metadata and optional input key. GIF rendering is reserved. */
UCLASS(Blueprintable)
class OUTLIER_API URightGifBindedWidget : public UInfoQueueWidget
{
	GENERATED_BODY()

public:
	/** Subject layout and internal icon/text bindings belong to this module's WBP. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UGifSubjectWidget> SubjectWidget;

	/** Input layout and internal key/description bindings belong to this module's WBP. */
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
