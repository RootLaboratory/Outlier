#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/InputDisplayTypes.h"
#include "GifInputWidget.generated.h"

class UHorizontalBox;
class UInputCombinationWidget;
class UTextBlock;

/** Input layout module. Displays a rebound key and its optional description. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UGifInputWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetInputData(const FInputDisplayDefinition& Display, const FText& Description);
	void ClearInputData();

	/** Preserves the existing Input HorizontalBox name inside the module WBP. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> Input;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputCombinationWidget> InputDisplay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InputDisplayDescText;

protected:
	virtual void NativeOnInitialized() override;
};
