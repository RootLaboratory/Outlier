#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UILayerKeyHintWidget.generated.h"

class UInputActionKeyDisplayWidget;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UUILayerKeyHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void RefreshKeyTexts();

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetConfirmedHintText(const FText& InHintText);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetEscapeHintText(const FText& InHintText);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void ClearHintTextOverrides();

protected:
	virtual void NativeConstruct() override;

	// 각각 Confirm/Escape용 IA를 물고 있다가, 리바인드가 일어나면 스스로
	// 표시를 갱신하는 공용 부품(UInputActionKeyDisplayWidget)이다.
	// 실제 IA는 RefreshKeyTexts에서 FrontendPlayerController로부터 얻어와
	// 매 프레임이 아니라 필요할 때만 꽂아준다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UInputActionKeyDisplayWidget> ConfirmedKeyDisplay;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UInputActionKeyDisplayWidget> EscapeKeyDisplay;
};
