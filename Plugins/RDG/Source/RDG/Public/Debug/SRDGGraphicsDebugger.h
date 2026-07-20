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
	ECheckBoxState GetADSBlurDebugPassEnabledCheckState() const;
	void OnADSBlurDebugPassEnabledChanged(ECheckBoxState NewState);
	ECheckBoxState GetADSBlurGpuStatScopesCheckState() const;
	void OnADSBlurGpuStatScopesChanged(ECheckBoxState NewState);
	TOptional<int32> GetADSBlurWeaponStencilValue() const;
	void OnADSBlurWeaponStencilChanged(int32 NewValue);
	TOptional<float> GetADSBlurRampInValue() const;
	void OnADSBlurRampInChanged(float NewValue);
	TOptional<float> GetADSBlurRampOutValue() const;
	void OnADSBlurRampOutChanged(float NewValue);

	TOptional<float> GetADSBlurFocusDistanceWorldValue() const;
	void OnADSBlurFocusDistanceWorldChanged(float NewValue);
	TOptional<float> GetADSBlurSightDistanceThresholdValue() const;
	void OnADSBlurSightDistanceThresholdChanged(float NewValue);
	TOptional<float> GetADSBlurSightMaskDilateRadiusValue() const;
	void OnADSBlurSightMaskDilateRadiusChanged(float NewValue);
	TOptional<float> GetADSBlurSightMaskSoftnessValue() const;
	void OnADSBlurSightMaskSoftnessChanged(float NewValue);
	ECheckBoxState GetADSBlurUseSoftSightMaskCheckState() const;
	void OnADSBlurUseSoftSightMaskChanged(ECheckBoxState NewState);

	// ADS Depth of Field (UE Diaphragm DOF, driven on the registered post-process volume).
	ECheckBoxState GetADSDoFEnabledCheckState() const;
	void OnADSDoFEnabledChanged(ECheckBoxState NewState);
	float GetADSDoFIntensitySliderValue() const;
	TOptional<float> GetADSDoFIntensityValue() const;
	void OnADSDoFIntensityChanged(float NewValue);
	TOptional<float> GetADSDoFFocalDistanceValue() const;
	void OnADSDoFFocalDistanceChanged(float NewValue);
	TOptional<float> GetADSDoFMaxBlurClampValue() const;
	void OnADSDoFMaxBlurClampChanged(float NewValue);
	TOptional<float> GetADSDoFSensorWidthValue() const;
	void OnADSDoFSensorWidthChanged(float NewValue);
	TOptional<float> GetADSDoFHipFStopValue() const;
	void OnADSDoFHipFStopChanged(float NewValue);
	TOptional<float> GetADSDoFFocalRegionValue() const;
	void OnADSDoFFocalRegionChanged(float NewValue);
	TOptional<float> GetADSDoFFarTransitionRegionValue() const;
	void OnADSDoFFarTransitionRegionChanged(float NewValue);

	ECheckBoxState GetPixelSortingEnabledCheckState() const;
	void OnPixelSortingEnabledChanged(ECheckBoxState NewState);
	TOptional<int32> GetPixelSortingModeValue() const;
	void OnPixelSortingModeChanged(int32 NewValue);
	TOptional<int32> GetPixelSortingCurveValue() const;
	void OnPixelSortingCurveChanged(int32 NewValue);
	TOptional<int32> GetPixelSortingMinThresholdValue() const;
	void OnPixelSortingMinThresholdChanged(int32 NewValue);
	TOptional<float> GetPixelSortingScaleValue() const;
	void OnPixelSortingScaleChanged(float NewValue);
	ECheckBoxState GetPixelSortingRowsCheckState() const;
	void OnPixelSortingRowsChanged(ECheckBoxState NewState);
	ECheckBoxState GetPixelSortingColumnsCheckState() const;
	void OnPixelSortingColumnsChanged(ECheckBoxState NewState);

	ECheckBoxState GetDatamoshEnabledCheckState() const;
	void OnDatamoshEnabledChanged(ECheckBoxState NewState);
	float GetDatamoshProgressSliderValue() const;
	TOptional<float> GetDatamoshProgressValue() const;
	void OnDatamoshProgressChanged(float NewValue);

	mutable TWeakObjectPtr<ULocalPlayerPostProcessSubsystem> CachedSubsystem;
};
