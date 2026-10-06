#pragma once

#include "CoreMinimal.h"
#include "UI/InfoQueueWidget.h"
#include "UpperWidget.generated.h"

class UInputTutoWidget;
class UGoalWidget;

/** Input tutorial and goal share the upper region's FIFO queue. */
UCLASS(Blueprintable)
class OUTLIER_API UUpperWidget : public UInfoQueueWidget
{
	GENERATED_BODY()

public:
	FGameplayTag GetActiveInputEventTag() const;
	bool CompleteInputInfo(FGameplayTag EventTag);
	void CancelInputInfos();

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

private:
	bool IsInputRequest(const FOutlierInfoWidgetRequest& Request) const;
	bool bCurrentInput = false;
};
