#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingSliderRowWidget.generated.h"

class USlider;
class USizeBox;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnSettingSliderRowValueChanged,
	float,
	NewValue);

UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingSliderRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	UFUNCTION(BlueprintCallable, Category = "Setting|Slider Row")
	void SetLabelText(const FText& NewLabelText);

	UFUNCTION(BlueprintCallable, Category = "Setting|Slider Row")
	void SetValue(float NewValue);

	UFUNCTION(BlueprintCallable, Category = "Setting|Slider Row")
	void SetColumnWidths(float LabelWidth, float SliderWidth, float ValueWidth);

	UFUNCTION(BlueprintPure, Category = "Setting|Slider Row")
	float GetValue() const;

	UPROPERTY(BlueprintAssignable, Category = "Setting|Slider Row")
	FOnSettingSliderRowValueChanged OnValueChanged;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Setting|Slider Row")
	TObjectPtr<USizeBox> LabelSizeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Setting|Slider Row")
	TObjectPtr<USizeBox> SliderSizeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Setting|Slider Row")
	TObjectPtr<USizeBox> ValueSizeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Slider Row")
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Slider Row")
	TObjectPtr<USlider> ValueSlider;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Slider Row")
	TObjectPtr<UTextBlock> ValueText;

private:
	UFUNCTION()
	void HandleSliderValueChanged(float NewValue);

	void UpdateValueText(float NewValue);

	bool bRefreshingValue = false;
};
