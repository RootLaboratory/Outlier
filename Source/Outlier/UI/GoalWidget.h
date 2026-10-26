#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "GoalWidget.generated.h"

class UTextBlock;
class UPopupRetainerBox;

/** UpperWidget의 child. Goal 조회/라우팅은 이벤트 레이어가 별도로 제공한다. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UGoalWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetGoal(FGameplayTag EventTag, const FText& Text);
	void ClearGoal();
	void PlayGoalPopup();

	/** WBP에서 GoalText를 감싸는 Popup Retainer Box. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI|Info|Popup")
	TObjectPtr<UPopupRetainerBox> PopupRetainerBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GoalText;

private:
	UPROPERTY(Transient)
	FGameplayTag CurrentEventTag;

	UPROPERTY(Transient)
	FText CurrentGoalText;
};
