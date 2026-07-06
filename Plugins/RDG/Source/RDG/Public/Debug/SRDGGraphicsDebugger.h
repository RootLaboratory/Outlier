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

	ECheckBoxState GetADSBlurPreviewAimingCheckState() const;
	void OnADSBlurPreviewAimingChanged(ECheckBoxState NewState);
	TOptional<int32> GetADSBlurWeaponStencilValue() const;
	void OnADSBlurWeaponStencilChanged(int32 NewValue);
	TOptional<float> GetADSBlurRampInValue() const;
	void OnADSBlurRampInChanged(float NewValue);
	TOptional<float> GetADSBlurRampOutValue() const;
	void OnADSBlurRampOutChanged(float NewValue);
	TOptional<float> GetADSBlurMinRadiusValue() const;
	void OnADSBlurMinRadiusChanged(float NewValue);
	TOptional<float> GetADSBlurMaxRadiusValue() const;
	void OnADSBlurMaxRadiusChanged(float NewValue);
	TOptional<int32> GetADSBlurPassCountValue() const;
	void OnADSBlurPassCountChanged(int32 NewValue);
	float GetADSBlurInnerPreserveSliderValue() const;
	TOptional<float> GetADSBlurInnerPreserveValue() const;
	void OnADSBlurInnerPreserveChanged(float NewValue);
	TOptional<float> GetADSBlurDepthBlurStartValue() const;
	void OnADSBlurDepthBlurStartChanged(float NewValue);
	TOptional<float> GetADSBlurDepthBlurEndValue() const;
	void OnADSBlurDepthBlurEndChanged(float NewValue);
	TOptional<float> GetADSBlurDepthBlurPowerValue() const;
	void OnADSBlurDepthBlurPowerChanged(float NewValue);
	TOptional<float> GetADSBlurDepthFocusBiasValue() const;
	void OnADSBlurDepthFocusBiasChanged(float NewValue);
	TOptional<float> GetADSBlurFocusDistanceValue() const;
	void OnADSBlurFocusDistanceChanged(float NewValue);
	TOptional<int32> GetADSBlurGatherSampleCountValue() const;
	void OnADSBlurGatherSampleCountChanged(int32 NewValue);
	TOptional<float> GetADSBlurReachSoftnessValue() const;
	void OnADSBlurReachSoftnessChanged(float NewValue);
	TOptional<float> GetADSBlurMinMaskDilateValue() const;
	void OnADSBlurMinMaskDilateChanged(float NewValue);
	TOptional<float> GetADSBlurMaxMaskDilateValue() const;
	void OnADSBlurMaxMaskDilateChanged(float NewValue);
	TOptional<float> GetADSBlurMinMaskSoftnessValue() const;
	void OnADSBlurMinMaskSoftnessChanged(float NewValue);
	TOptional<float> GetADSBlurMaxMaskSoftnessValue() const;
	void OnADSBlurMaxMaskSoftnessChanged(float NewValue);

	ECheckBoxState GetDatamoshEnabledCheckState() const;
	void OnDatamoshEnabledChanged(ECheckBoxState NewState);
	float GetDatamoshProgressSliderValue() const;
	TOptional<float> GetDatamoshProgressValue() const;
	void OnDatamoshProgressChanged(float NewValue);

	mutable TWeakObjectPtr<ULocalPlayerPostProcessSubsystem> CachedSubsystem;
};
