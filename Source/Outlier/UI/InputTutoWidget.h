#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "UI/InputDisplayTypes.h"
#include "InputTutoWidget.generated.h"

class UInputCombinationWidget;
class UTextBlock;

/** 튜토리얼 텍스트와 역할별 InputDisplay DT에서 해석한 정의를 표시한다. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UInputTutoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool SetInputEvent(FGameplayTag EventTag);
	bool SetInputTag(FGameplayTag InputTag);
	bool SetInputData(FGameplayTag RequestTag, const FInputDisplayDefinition& Display, const FText& Text);
	void ClearInputEvent();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputCombinationWidget> InputDisplay;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> InstructionText;

private:
	UPROPERTY(Transient)
	FText CurrentText;

	UPROPERTY(Transient)
	FGameplayTag CurrentEventTag;
};
