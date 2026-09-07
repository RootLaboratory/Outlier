#include "UI/SettingSliderRowWidget.h"

#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

void USettingSliderRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (ValueSlider)
	{
		ValueSlider->SetMinValue(0.0f);
		ValueSlider->SetMaxValue(1.0f);
		ValueSlider->SetStepSize(0.01f);
		ValueSlider->OnValueChanged.AddDynamic(
			this,
			&USettingSliderRowWidget::HandleSliderValueChanged);
	}
}

void USettingSliderRowWidget::SetLabelText(const FText& NewLabelText)
{
	if (LabelText)
	{
		LabelText->SetText(NewLabelText);
	}
}

void USettingSliderRowWidget::SetValue(float NewValue)
{
	const float ClampedValue = FMath::Clamp(NewValue, 0.0f, 1.0f);

	bRefreshingValue = true;
	if (ValueSlider)
	{
		ValueSlider->SetValue(ClampedValue);
	}
	bRefreshingValue = false;

	UpdateValueText(ClampedValue);
}

void USettingSliderRowWidget::SetColumnWidths(
	float LabelWidth,
	float SliderWidth,
	float ValueWidth)
{
	if (LabelSizeBox)
	{
		LabelSizeBox->SetWidthOverride(LabelWidth);
	}

	if (SliderSizeBox)
	{
		SliderSizeBox->SetWidthOverride(SliderWidth);
	}

	if (ValueSizeBox)
	{
		ValueSizeBox->SetWidthOverride(ValueWidth);
	}
}

float USettingSliderRowWidget::GetValue() const
{
	return ValueSlider ? ValueSlider->GetValue() : 0.0f;
}

void USettingSliderRowWidget::HandleSliderValueChanged(float NewValue)
{
	UpdateValueText(NewValue);

	if (!bRefreshingValue)
	{
		OnValueChanged.Broadcast(NewValue);
	}
}

void USettingSliderRowWidget::UpdateValueText(float NewValue)
{
	if (!ValueText)
	{
		return;
	}

	const int32 PercentValue =
		FMath::RoundToInt(FMath::Clamp(NewValue, 0.0f, 1.0f) * 100.0f);
	ValueText->SetText(FText::FromString(
		FString::Printf(TEXT("%d%%"), PercentValue)));
}
