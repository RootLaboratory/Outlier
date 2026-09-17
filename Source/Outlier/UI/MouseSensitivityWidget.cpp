#include "UI/MouseSensitivityWidget.h"

#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"

void UMouseSensitivityWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (MouseSensitivitySlider)
	{
		MouseSensitivitySlider->SetMinValue(0.0f);
		MouseSensitivitySlider->SetMaxValue(1.0f);
		MouseSensitivitySlider->SetStepSize(0.01f);
		MouseSensitivitySlider->OnValueChanged.AddUniqueDynamic(
			this,
			&UMouseSensitivityWidget::HandleSliderValueChanged);
	}

	UpdateValueText(PendingSensitivity);
}

void UMouseSensitivityWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);

	BoundSettingsSubsystem = GetSettingsSubsystem();
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnMouseSensitivityChanged.AddUniqueDynamic(
			this,
			&UMouseSensitivityWidget::HandleMouseSensitivityChanged);
	}

	RefreshFromSettings();
}

void UMouseSensitivityWidget::NativeDestruct()
{
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnMouseSensitivityChanged.RemoveAll(this);
	}
	BoundSettingsSubsystem = nullptr;

	Super::NativeDestruct();
}

bool UMouseSensitivityWidget::ConfirmPendingValue()
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	return SettingsSubsystem
		&& SettingsSubsystem->SetMouseSensitivity(PendingSensitivity);
}

void UMouseSensitivityWidget::OffsetPendingValue(float Delta)
{
	PendingSensitivity = FMath::Clamp(
		PendingSensitivity + Delta,
		MinSensitivity,
		MaxSensitivity);

	const float SliderValue = FMath::GetRangePct(
		MinSensitivity,
		MaxSensitivity,
		PendingSensitivity);

	bRefreshingValue = true;
	if (MouseSensitivitySlider)
	{
		MouseSensitivitySlider->SetValue(SliderValue);
	}
	bRefreshingValue = false;

	UpdateValueText(PendingSensitivity);
}

void UMouseSensitivityWidget::CancelPendingValue()
{
	RefreshFromSettings();
}

void UMouseSensitivityWidget::RefreshFromSettings()
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem)
	{
		return;
	}

	PendingSensitivity = SettingsSubsystem->GetMouseSensitivity();
	const float SliderValue = FMath::GetRangePct(
	MinSensitivity,
		MaxSensitivity,
		PendingSensitivity);

	bRefreshingValue = true;
	if (MouseSensitivitySlider)
	{
		MouseSensitivitySlider->SetValue(SliderValue);
	}
	bRefreshingValue = false;

	UpdateValueText(PendingSensitivity);
}

void UMouseSensitivityWidget::HandleSliderValueChanged(float NewSliderValue)
{
	if (bRefreshingValue)
	{
		return;
	}

	PendingSensitivity = FMath::Lerp(
		MinSensitivity,
		MaxSensitivity,
		FMath::Clamp(NewSliderValue, 0.0f, 1.0f));
	UpdateValueText(PendingSensitivity);
}

void UMouseSensitivityWidget::HandleMouseSensitivityChanged(float NewValue)
{
	(void)NewValue;
	RefreshFromSettings();
}

void UMouseSensitivityWidget::UpdateValueText(float NewValue)
{
	if (ValueText)
	{
		ValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%.2f"), NewValue)));
	}
}

ULocalPlayerSettingsSubsystem* UMouseSensitivityWidget::GetSettingsSubsystem() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerSettingsSubsystem>()
		: nullptr;
}
