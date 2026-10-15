#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/InputDisplayTypes.h"
#include "GifInputWidget.generated.h"

class UHorizontalBox;
class UInputCombinationWidget;
class UTextBlock;

/** Input 레이아웃 모듈. 리바인드된 키와 선택적 설명을 표시한다. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UGifInputWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetInputData(const FInputDisplayDefinition& Display, const FText& Description);
	void ClearInputData();

	/** 모듈 WBP 내부의 기존 Input HorizontalBox 이름을 유지한다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> Input;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputCombinationWidget> InputDisplay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InputDisplayDescText;

protected:
	virtual void NativeOnInitialized() override;
};
