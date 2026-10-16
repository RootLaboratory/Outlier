#pragma once

#include "CoreMinimal.h"
#include "UI/InfoQueueWidget.h"
#include "UpperWidget.generated.h"

class UInputTutoWidget;
class UGoalWidget;

/** Input 튜토리얼과 goal은 Upper 영역의 FIFO Queue를 공유한다. */
UCLASS(Blueprintable)
class OUTLIER_API UUpperWidget : public UInfoQueueWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputTutoWidget> InputTutoWidget;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UGoalWidget> GoalWidget;

protected:
	virtual void NativeOnInitialized() override;
	virtual bool PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const override;
	virtual bool PresentInfo(const FOutlierInfoWidgetRequest& Request) override;
	virtual void ResetInfoContent() override;
	virtual void InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted) override;
};
