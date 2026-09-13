#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MouseSensitivityWidget.generated.h"

class USlider;
class UTextBlock;
class ULocalPlayerSettingsSubsystem;

/** Owns the pending mouse sensitivity value and commits it on confirmation. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UMouseSensitivityWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "Setting|Mouse Sensitivity")
	bool ConfirmPendingValue();

	UFUNCTION(BlueprintCallable, Category = "Setting|Mouse Sensitivity")
	void OffsetPendingValue(float Delta);

	UFUNCTION(BlueprintCallable, Category = "Setting|Mouse Sensitivity")
	void CancelPendingValue();

	UFUNCTION(BlueprintCallable, Category = "Setting|Mouse Sensitivity")
	void RefreshFromSettings();

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Mouse Sensitivity")
	TObjectPtr<USlider> MouseSensitivitySlider;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Mouse Sensitivity")
	TObjectPtr<UTextBlock> ValueText;

private:
	UFUNCTION()
	void HandleSliderValueChanged(float NewSliderValue);

	UFUNCTION()
	void HandleMouseSensitivityChanged(float NewValue);

	void UpdateValueText(float NewValue);

	ULocalPlayerSettingsSubsystem* GetSettingsSubsystem() const;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;

	float PendingSensitivity = 1.0f;
	bool bRefreshingValue = false;

	static constexpr float MinSensitivity = 0.05f;
	static constexpr float MaxSensitivity = 2.0f;
};
