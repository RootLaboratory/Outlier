#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "GoalWidget.generated.h"

class UTextBlock;

/** UpperWidget child. Goal lookup/routing is supplied separately by the event layer. */
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
