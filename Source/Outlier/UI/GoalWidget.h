#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "GoalWidget.generated.h"

class UTextBlock;

/** UpperWidget의 child. Goal 조회/라우팅은 이벤트 레이어가 별도로 제공한다. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UGoalWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetGoal(FGameplayTag EventTag, const FText& Text);
	void ClearGoal();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GoalText;

private:
	UPROPERTY(Transient)
	FGameplayTag CurrentEventTag;

	UPROPERTY(Transient)
	FText CurrentGoalText;
};
