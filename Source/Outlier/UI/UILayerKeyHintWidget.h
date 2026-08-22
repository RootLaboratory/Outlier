#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UILayerKeyHintWidget.generated.h"

class UInputAction;
class UTextBlock;

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

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> ConfirmedKeyText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> EscapeKeyText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input")
	FText MissingKeyText;

	FText ResolveActionKeyText(const UInputAction* InputAction) const;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Input")
	FText ConfirmedHintTextOverride;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Input")
	FText EscapeHintTextOverride;
};
