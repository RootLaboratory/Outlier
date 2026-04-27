#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"

class ULocalPlayerPostProcessSubsystem;

class SRDGGraphicsDebugger : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRDGGraphicsDebugger) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	ULocalPlayerPostProcessSubsystem* ResolvePostProcessSubsystem() const;

	ECheckBoxState GetChromaticEnabledCheckState() const;
	void OnChromaticEnabledChanged(ECheckBoxState NewState);

	float GetChromaticStartOffsetSliderValue() const;
	TOptional<float> GetChromaticStartOffsetValue() const;
	void OnChromaticStartOffsetSliderChanged(float NewValue);

	TOptional<float> GetChromaticIntensityValue() const;
	void OnChromaticIntensityChanged(float NewValue);

	mutable TWeakObjectPtr<ULocalPlayerPostProcessSubsystem> CachedSubsystem;
};
