#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "UI/InputDisplayTypes.h"
#include "InputTutoWidget.generated.h"

class UInputCombinationWidget;
class UTextBlock;

/** Displays tutorial text and a resolved definition from the role's InputDisplay DT. */
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
