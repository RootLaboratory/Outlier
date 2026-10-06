#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GifInputWidget.generated.h"

class UHorizontalBox;
class UInputAction;
class UInputActionKeyDisplayWidget;
class UTextBlock;

/** Input layout module. Displays a rebound key and its optional description. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UGifInputWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetInputData(UInputAction* InputAction, const FText& Description);
	void ClearInputData();

	/** Preserves the existing Input HorizontalBox name inside the module WBP. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> Input;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputActionKeyDisplayWidget> InputBindWidgetKey;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InputDisplayDescText;

protected:
	virtual void NativeOnInitialized() override;
};
