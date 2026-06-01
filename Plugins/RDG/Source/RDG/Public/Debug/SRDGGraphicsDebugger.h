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

	ECheckBoxState GetExplosionVolumeEnabledCheckState() const;
	void OnExplosionVolumeEnabledChanged(ECheckBoxState NewState);
	ECheckBoxState GetExplosionVolumeVisualizeCheckState() const;
	void OnExplosionVolumeVisualizeChanged(ECheckBoxState NewState);

	ECheckBoxState GetChromaticEnabledCheckState() const;
	void OnChromaticEnabledChanged(ECheckBoxState NewState);
	float GetChromaticStartOffsetSliderValue() const;
	TOptional<float> GetChromaticStartOffsetValue() const;
	void OnChromaticStartOffsetSliderChanged(float NewValue);
	TOptional<float> GetChromaticIntensityValue() const;
	void OnChromaticIntensityChanged(float NewValue);

	ECheckBoxState GetMotionBlurEnabledCheckState() const;
	void OnMotionBlurEnabledChanged(ECheckBoxState NewState);
	float GetMotionBlurBlendWeightSliderValue() const;
	TOptional<float> GetMotionBlurBlendWeightValue() const;
	void OnMotionBlurBlendWeightChanged(float NewValue);
	TOptional<float> GetMotionBlurIntensityValue() const;
	void OnMotionBlurIntensityChanged(float NewValue);
	TOptional<float> GetMotionBlurVelocityScaleValue() const;
	void OnMotionBlurVelocityScaleChanged(float NewValue);

	ECheckBoxState GetDualKawaseEnabledCheckState() const;
	void OnDualKawaseEnabledChanged(ECheckBoxState NewState);
	TOptional<float> GetDualKawaseBlurRadiusValue() const;
	void OnDualKawaseBlurRadiusChanged(float NewValue);
	float GetDualKawaseBlendWeightSliderValue() const;
	TOptional<float> GetDualKawaseBlendWeightValue() const;
	void OnDualKawaseBlendWeightChanged(float NewValue);
	TOptional<int32> GetDualKawaseDownsampleCountValue() const;
	void OnDualKawaseDownsampleCountChanged(int32 NewValue);

	ECheckBoxState GetDatamoshEnabledCheckState() const;
	void OnDatamoshEnabledChanged(ECheckBoxState NewState);
	float GetDatamoshProgressSliderValue() const;
	TOptional<float> GetDatamoshProgressValue() const;
	void OnDatamoshProgressChanged(float NewValue);

	mutable TWeakObjectPtr<ULocalPlayerPostProcessSubsystem> CachedSubsystem;
};
