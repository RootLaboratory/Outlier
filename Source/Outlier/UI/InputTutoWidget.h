#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "InputTutoWidget.generated.h"

class UInputActionKeyDisplayWidget;
class UInputAction;
class UTextBlock;

/** UpperWidget child. Uses the Upper DT's InputAction to display the current rebound key. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UInputTutoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool SetInputEvent(FGameplayTag EventTag);
	bool SetInputData(FGameplayTag EventTag, UInputAction* InputAction, const FText& Text);
	void ClearInputEvent();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputActionKeyDisplayWidget> InputBindWidgetKey;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> InstructionText;

private:
	UPROPERTY(Transient)
	FText CurrentText;

	UPROPERTY(Transient)
	FGameplayTag CurrentEventTag;
};
