#include "Debug/SRDGGraphicsDebugger.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "HAL/IConsoleManager.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace RDGGraphicsDebugger
{
	static const FMargin RowPadding(0.0f, 6.0f, 0.0f, 0.0f);

	// ADS DoF intensity <-> aim f-stop mapping. Intensity 0 = f-stop high enough that DOF
	// reads as effectively off; Intensity 1 = strong background blur. Hip f-stop is left at
	// its default (DoF fully off while not aiming) and isn't exposed here.
	static constexpr float kADSDoFIntensityMinFStop = 16.0f;
	static constexpr float kADSDoFIntensityMaxFStop = 0.7f;
}

void SRDGGraphicsDebugger::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.Padding(16.0f)
		[
			SNew(SBox)
			.WidthOverride(420.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("RDG Graphics Debugger")))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SRDGGraphicsDebugger::GetPixelSortingEnabledCheckState)
							.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnPixelSortingEnabledChanged)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Pixel Sorting")))
						]
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Mode (0 White, 1 Black, 2 Bright, 3 Dark)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<int32>)
								.AllowSpin(true)
								.MinValue(0)
								.MaxValue(3)
								.MinSliderValue(0)
								.MaxSliderValue(3)
								.Value(this, &SRDGGraphicsDebugger::GetPixelSortingModeValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnPixelSortingModeChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Curve (0 Linear, 1 Cubic Ease-In)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<int32>)
								.AllowSpin(true)
								.MinValue(0)
								.MaxValue(1)
								.MinSliderValue(0)
								.MaxSliderValue(1)
								.Value(this, &SRDGGraphicsDebugger::GetPixelSortingCurveValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnPixelSortingCurveChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Min Threshold (0-255)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(140.0f)
							[
								SNew(SNumericEntryBox<int32>)
								.AllowSpin(true)
								.MinValue(0)
								.MaxValue(255)
								.MinSliderValue(0)
								.MaxSliderValue(255)
								.Value(this, &SRDGGraphicsDebugger::GetPixelSortingMinThresholdValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnPixelSortingMinThresholdChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Scale (Threshold levels / second)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(1000.0f)
								.Value(this, &SRDGGraphicsDebugger::GetPixelSortingScaleValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnPixelSortingScaleChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetPixelSortingColumnsCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnPixelSortingColumnsChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Sort Columns")))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetPixelSortingRowsCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnPixelSortingRowsChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Sort Rows")))
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Explosion Volume")))
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetExplosionVolumeEnabledCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnExplosionVolumeEnabledChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Generate 3D Volume")))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetExplosionVolumeVisualizeCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnExplosionVolumeVisualizeChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Visualize Volume")))
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SRDGGraphicsDebugger::GetMotionBlurEnabledCheckState)
							.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnMotionBlurEnabledChanged)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Motion Blur")))
						]
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Blend Weight (0-1)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.Value(this, &SRDGGraphicsDebugger::GetMotionBlurBlendWeightSliderValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnMotionBlurBlendWeightChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(72.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(false)
									.MinValue(0.0f)
									.MaxValue(1.0f)
									.Value(this, &SRDGGraphicsDebugger::GetMotionBlurBlendWeightValue)
									.OnValueChanged(this, &SRDGGraphicsDebugger::OnMotionBlurBlendWeightChanged)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Intensity")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(8.0f)
								.Value(this, &SRDGGraphicsDebugger::GetMotionBlurIntensityValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnMotionBlurIntensityChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Velocity Scale")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(8.0f)
								.Value(this, &SRDGGraphicsDebugger::GetMotionBlurVelocityScaleValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnMotionBlurVelocityScaleChanged)
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SRDGGraphicsDebugger::GetChromaticEnabledCheckState)
							.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnChromaticEnabledChanged)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Chromatic Aberration")))
						]
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Start Offset (0-1)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.Value(this, &SRDGGraphicsDebugger::GetChromaticStartOffsetSliderValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnChromaticStartOffsetSliderChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(72.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(false)
									.MinValue(0.0f)
									.MaxValue(1.0f)
									.Value(this, &SRDGGraphicsDebugger::GetChromaticStartOffsetValue)
									.OnValueChanged(this, &SRDGGraphicsDebugger::OnChromaticStartOffsetSliderChanged)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Intensity")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(5.0f)
								.Value(this, &SRDGGraphicsDebugger::GetChromaticIntensityValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnChromaticIntensityChanged)
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SRDGGraphicsDebugger::GetDualKawaseEnabledCheckState)
							.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnDualKawaseEnabledChanged)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Dual Kawase Blur")))
						]
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Blur Radius")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(8.0f)
								.Value(this, &SRDGGraphicsDebugger::GetDualKawaseBlurRadiusValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnDualKawaseBlurRadiusChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Blend Weight (0-1)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.Value(this, &SRDGGraphicsDebugger::GetDualKawaseBlendWeightSliderValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnDualKawaseBlendWeightChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(72.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(false)
									.MinValue(0.0f)
									.MaxValue(1.0f)
									.Value(this, &SRDGGraphicsDebugger::GetDualKawaseBlendWeightValue)
									.OnValueChanged(this, &SRDGGraphicsDebugger::OnDualKawaseBlendWeightChanged)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Downsample Count")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<int32>)
								.AllowSpin(true)
								.MinValue(1)
								.MaxValue(6)
								.MinSliderValue(1)
								.MaxSliderValue(6)
								.Value(this, &SRDGGraphicsDebugger::GetDualKawaseDownsampleCountValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnDualKawaseDownsampleCountChanged)
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SRDGGraphicsDebugger::GetADSBlurPreviewAimingCheckState)
							.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnADSBlurPreviewAimingChanged)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("ADS Weapon Blur Preview")))
						]
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetADSBlurDebugPassEnabledCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnADSBlurDebugPassEnabledChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Enable ADS Blur Pass")))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetADSBlurGpuStatScopesCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnADSBlurGpuStatScopesChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Enable ADS GPU Stat Scopes")))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SExpandableArea)
							.InitiallyCollapsed(false)
							.HeaderContent()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Core")))
							]
							.BodyContent()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(RDGGraphicsDebugger::RowPadding)
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("Weapon Stencil")))]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<int32>)
								.AllowSpin(true)
								.MinValue(0)
								.MaxValue(255)
								.MinSliderValue(0)
								.MaxSliderValue(255)
								.Value(this, &SRDGGraphicsDebugger::GetADSBlurWeaponStencilValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSBlurWeaponStencilChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Ramp In / Out")))]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SBox)
								.WidthOverride(96.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(true)
									.MinValue(0.0f)
									.MinSliderValue(0.0f)
									.MaxSliderValue(2.0f)
									.Value(this, &SRDGGraphicsDebugger::GetADSBlurRampInValue)
									.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSBlurRampInChanged)
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(96.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(true)
									.MinValue(0.0f)
									.MinSliderValue(0.0f)
									.MaxSliderValue(2.0f)
									.Value(this, &SRDGGraphicsDebugger::GetADSBlurRampOutValue)
									.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSBlurRampOutChanged)
								]
							]
						]
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(true)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Sight (Hook A/B, CoC)")))
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Focus Distance World")))]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(50.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSBlurFocusDistanceWorldValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSBlurFocusDistanceWorldChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Sight Distance Threshold")))]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(10.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSBlurSightDistanceThresholdValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSBlurSightDistanceThresholdChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Sight Mask Dilate Radius")))]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(120.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSBlurSightMaskDilateRadiusValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSBlurSightMaskDilateRadiusChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Sight Mask Softness")))]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(32.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSBlurSightMaskSoftnessValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSBlurSightMaskSoftnessChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetADSBlurUseSoftSightMaskCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnADSBlurUseSoftSightMaskChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Use Soft Sight Mask (Hard vs Soft)")))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SRDGGraphicsDebugger::GetADSDoFEnabledCheckState)
								.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnADSDoFEnabledChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("ADS Depth of Field (UE DOF)")))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Intensity (0-1)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSDoFIntensitySliderValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFIntensityChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(72.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(false)
									.MinValue(0.0f)
									.MaxValue(1.0f)
									.Value(this, &SRDGGraphicsDebugger::GetADSDoFIntensityValue)
									.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFIntensityChanged)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Focal Distance (cm) — overwritten live while actually aiming")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(2000.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSDoFFocalDistanceValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFFocalDistanceChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Max Blur Clamp (min f-stop, 0 = uncapped)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(8.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSDoFMaxBlurClampValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFMaxBlurClampChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Sensor Width (mm)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(1.0f)
								.MinSliderValue(1.0f)
								.MaxSliderValue(70.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSDoFSensorWidthValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFSensorWidthChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Hip F-Stop (DOF strength while not aiming)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.1f)
								.MinSliderValue(0.1f)
								.MaxSliderValue(32.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSDoFHipFStopValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFHipFStopChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Focal Region (cm, fully sharp around focus)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(500.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSDoFFocalRegionValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFFocalRegionChanged)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Far Transition Region (cm, distance to reach max blur)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(96.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(2000.0f)
								.Value(this, &SRDGGraphicsDebugger::GetADSDoFFarTransitionRegionValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnADSDoFFarTransitionRegionChanged)
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(RDGGraphicsDebugger::RowPadding)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SRDGGraphicsDebugger::GetDatamoshEnabledCheckState)
							.OnCheckStateChanged(this, &SRDGGraphicsDebugger::OnDatamoshEnabledChanged)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Datamoshing")))
						]
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(RDGGraphicsDebugger::RowPadding)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Death Progress (0-1)")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.Value(this, &SRDGGraphicsDebugger::GetDatamoshProgressSliderValue)
								.OnValueChanged(this, &SRDGGraphicsDebugger::OnDatamoshProgressChanged)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(72.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(false)
									.MinValue(0.0f)
									.MaxValue(1.0f)
									.Value(this, &SRDGGraphicsDebugger::GetDatamoshProgressValue)
									.OnValueChanged(this, &SRDGGraphicsDebugger::OnDatamoshProgressChanged)
								]
							]
						]
					]
				]
			]
		]
	];
}

ULocalPlayerPostProcessSubsystem* SRDGGraphicsDebugger::ResolvePostProcessSubsystem() const
{
	if (CachedSubsystem.IsValid())
	{
		return CachedSubsystem.Get();
	}

	if (!GEngine)
	{
		return nullptr;
	}

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!World || !(World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::GamePreview))
		{
			continue;
		}

		UGameInstance* GameInstance = World->GetGameInstance();
		if (!GameInstance)
		{
			continue;
		}

		ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
		if (!LocalPlayer)
		{
			continue;
		}

		if (ULocalPlayerPostProcessSubsystem* Subsystem = LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			CachedSubsystem = Subsystem;
			return Subsystem;
		}
	}

	return nullptr;
}

ECheckBoxState SRDGGraphicsDebugger::GetExplosionVolumeEnabledCheckState() const
{
	if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("rdg.ExplosionVolume.Enable")))
	{
		return CVar->GetInt() != 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnExplosionVolumeEnabledChanged(ECheckBoxState NewState)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("rdg.ExplosionVolume.Enable")))
	{
		CVar->Set(NewState == ECheckBoxState::Checked ? 1 : 0, ECVF_SetByCode);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetExplosionVolumeVisualizeCheckState() const
{
	if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("rdg.ExplosionVolume.Visualize")))
	{
		return CVar->GetInt() != 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnExplosionVolumeVisualizeChanged(ECheckBoxState NewState)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("rdg.ExplosionVolume.Visualize")))
	{
		CVar->Set(NewState == ECheckBoxState::Checked ? 1 : 0, ECVF_SetByCode);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetChromaticEnabledCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetUIPostProcessStrcture().ChromaticAberration.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnChromaticEnabledChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetChromaticAberrationEnabled(NewState == ECheckBoxState::Checked);
	}
}

float SRDGGraphicsDebugger::GetChromaticStartOffsetSliderValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetUIPostProcessStrcture().ChromaticAberration.StartOffset;
	}

	return 0.0f;
}

TOptional<float> SRDGGraphicsDebugger::GetChromaticStartOffsetValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetUIPostProcessStrcture().ChromaticAberration.StartOffset;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnChromaticStartOffsetSliderChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetChromaticAberrationStartOffset(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetChromaticIntensityValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetUIPostProcessStrcture().ChromaticAberration.Intensity;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnChromaticIntensityChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetChromaticAberrationIntensity(NewValue);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetMotionBlurEnabledCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().MotionBlur.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnMotionBlurEnabledChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetMotionBlurEnabled(NewState == ECheckBoxState::Checked);
	}
}

float SRDGGraphicsDebugger::GetMotionBlurBlendWeightSliderValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().MotionBlur.BlendWeight;
	}

	return 0.0f;
}

TOptional<float> SRDGGraphicsDebugger::GetMotionBlurBlendWeightValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().MotionBlur.BlendWeight;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnMotionBlurBlendWeightChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetMotionBlurBlendWeight(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetMotionBlurIntensityValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().MotionBlur.Intensity;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnMotionBlurIntensityChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetMotionBlurIntensity(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetMotionBlurVelocityScaleValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().MotionBlur.VelocityScale;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnMotionBlurVelocityScaleChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetMotionBlurVelocityScale(NewValue);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetDualKawaseEnabledCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().DualKawaseBlur.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnDualKawaseEnabledChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetDualKawaseBlurEnabled(NewState == ECheckBoxState::Checked);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetDualKawaseBlurRadiusValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().DualKawaseBlur.BlurRadius;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnDualKawaseBlurRadiusChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetDualKawaseBlurRadius(NewValue);
	}
}

float SRDGGraphicsDebugger::GetDualKawaseBlendWeightSliderValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().DualKawaseBlur.BlendWeight;
	}

	return 0.0f;
}

TOptional<float> SRDGGraphicsDebugger::GetDualKawaseBlendWeightValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().DualKawaseBlur.BlendWeight;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnDualKawaseBlendWeightChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetDualKawaseBlurBlendWeight(NewValue);
	}
}

TOptional<int32> SRDGGraphicsDebugger::GetDualKawaseDownsampleCountValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().DualKawaseBlur.DownsampleCount;
	}

	return TOptional<int32>();
}

void SRDGGraphicsDebugger::OnDualKawaseDownsampleCountChanged(int32 NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetDualKawaseBlurDownsampleCount(NewValue);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetADSBlurPreviewAimingCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->IsADSBlurAiming() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnADSBlurPreviewAimingChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		const int32 StencilValue = Subsystem->GetPostProcessStrcture().ADSBlur.WeaponStencilValue;
		Subsystem->SetADSBlurAiming(NewState == ECheckBoxState::Checked, StencilValue);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetADSBlurDebugPassEnabledCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->IsADSBlurDebugPassEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnADSBlurDebugPassEnabledChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurDebugPassEnabled(NewState == ECheckBoxState::Checked);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetADSBlurGpuStatScopesCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().ADSBlur.bEnableGpuStatScopes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnADSBlurGpuStatScopesChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurGpuStatScopesEnabled(NewState == ECheckBoxState::Checked);
	}
}

TOptional<int32> SRDGGraphicsDebugger::GetADSBlurWeaponStencilValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().ADSBlur.WeaponStencilValue;
	}

	return TOptional<int32>();
}

void SRDGGraphicsDebugger::OnADSBlurWeaponStencilChanged(int32 NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurAiming(Subsystem->IsADSBlurAiming(), NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSBlurRampInValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSBlurRampInTime();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSBlurRampInChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurRampTimes(NewValue, Subsystem->GetADSBlurRampOutTime());
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSBlurRampOutValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSBlurRampOutTime();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSBlurRampOutChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurRampTimes(Subsystem->GetADSBlurRampInTime(), NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSBlurSightDistanceThresholdValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().ADSBlur.SightDistanceThreshold;
	}

	return TOptional<float>();
}

TOptional<float> SRDGGraphicsDebugger::GetADSBlurFocusDistanceWorldValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().ADSBlur.FocusDistanceWorld;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSBlurFocusDistanceWorldChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurFocusDistanceWorld(NewValue);
	}
}

void SRDGGraphicsDebugger::OnADSBlurSightDistanceThresholdChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurSightDistanceThreshold(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSBlurSightMaskDilateRadiusValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().ADSBlur.SightMaskDilateRadius;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSBlurSightMaskDilateRadiusChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurSightMaskDilateRadius(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSBlurSightMaskSoftnessValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().ADSBlur.SightMaskSoftness;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSBlurSightMaskSoftnessChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurSightMaskSoftness(NewValue);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetADSBlurUseSoftSightMaskCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().ADSBlur.bUseSoftSightMask ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnADSBlurUseSoftSightMaskChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSBlurUseSoftSightMask(NewState == ECheckBoxState::Checked);
	}
}

// --- ADS Depth of Field (UE Diaphragm DOF) ---
ECheckBoxState SRDGGraphicsDebugger::GetADSDoFEnabledCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->IsADSDoFEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnADSDoFEnabledChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSDoFEnabled(NewState == ECheckBoxState::Checked);
	}
}

float SRDGGraphicsDebugger::GetADSDoFIntensitySliderValue() const
{
	return GetADSDoFIntensityValue().Get(0.0f);
}

TOptional<float> SRDGGraphicsDebugger::GetADSDoFIntensityValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		const float FStop = Subsystem->GetADSDoFApertureAim();
		const float Alpha = (RDGGraphicsDebugger::kADSDoFIntensityMinFStop - FStop)
			/ (RDGGraphicsDebugger::kADSDoFIntensityMinFStop - RDGGraphicsDebugger::kADSDoFIntensityMaxFStop);
		return FMath::Clamp(Alpha, 0.0f, 1.0f);
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSDoFIntensityChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		const float FStop = FMath::Lerp(
			RDGGraphicsDebugger::kADSDoFIntensityMinFStop,
			RDGGraphicsDebugger::kADSDoFIntensityMaxFStop,
			FMath::Clamp(NewValue, 0.0f, 1.0f));
		Subsystem->SetADSDoFApertureRange(FStop, Subsystem->GetADSDoFApertureHip());
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSDoFFocalDistanceValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSSocketDistance();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSDoFFocalDistanceChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSSocketDistance(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSDoFMaxBlurClampValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSDoFMaxBlurClamp();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSDoFMaxBlurClampChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSDoFMaxBlurClamp(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSDoFSensorWidthValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSDoFSensorWidth();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSDoFSensorWidthChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSDoFSensorWidth(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSDoFHipFStopValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSDoFApertureHip();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSDoFHipFStopChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSDoFApertureRange(Subsystem->GetADSDoFApertureAim(), NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSDoFFocalRegionValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSDoFFocalRegion();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSDoFFocalRegionChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSDoFFocalRegion(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetADSDoFFarTransitionRegionValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetADSDoFFarTransitionRegion();
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnADSDoFFarTransitionRegionChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetADSDoFFarTransitionRegion(NewValue);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetPixelSortingEnabledCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().PixelSorting.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnPixelSortingEnabledChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetPixelSortingEnabled(NewState == ECheckBoxState::Checked);
	}
}

TOptional<int32> SRDGGraphicsDebugger::GetPixelSortingModeValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().PixelSorting.Mode;
	}
	return TOptional<int32>();
}

void SRDGGraphicsDebugger::OnPixelSortingModeChanged(int32 NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetPixelSortingMode(NewValue);
	}
}

TOptional<int32> SRDGGraphicsDebugger::GetPixelSortingCurveValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().PixelSorting.Curve;
	}
	return TOptional<int32>();
}

void SRDGGraphicsDebugger::OnPixelSortingCurveChanged(int32 NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetPixelSortingCurve(NewValue);
	}
}

TOptional<int32> SRDGGraphicsDebugger::GetPixelSortingMinThresholdValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().PixelSorting.MinThreshold;
	}
	return TOptional<int32>();
}

void SRDGGraphicsDebugger::OnPixelSortingMinThresholdChanged(int32 NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetPixelSortingMinThreshold(NewValue);
	}
}

TOptional<float> SRDGGraphicsDebugger::GetPixelSortingScaleValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().PixelSorting.Scale;
	}
	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnPixelSortingScaleChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetPixelSortingScale(NewValue);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetPixelSortingRowsCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().PixelSorting.bSortRows ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnPixelSortingRowsChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetPixelSortingRowsEnabled(NewState == ECheckBoxState::Checked);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetPixelSortingColumnsCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().PixelSorting.bSortColumns ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnPixelSortingColumnsChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetPixelSortingColumnsEnabled(NewState == ECheckBoxState::Checked);
	}
}

ECheckBoxState SRDGGraphicsDebugger::GetDatamoshEnabledCheckState() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().Datamoshing.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

void SRDGGraphicsDebugger::OnDatamoshEnabledChanged(ECheckBoxState NewState)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetDatamoshingEnabled(NewState == ECheckBoxState::Checked);
	}
}

float SRDGGraphicsDebugger::GetDatamoshProgressSliderValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().Datamoshing.Progress;
	}

	return 0.0f;
}

TOptional<float> SRDGGraphicsDebugger::GetDatamoshProgressValue() const
{
	if (const ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		return Subsystem->GetPostProcessStrcture().Datamoshing.Progress;
	}

	return TOptional<float>();
}

void SRDGGraphicsDebugger::OnDatamoshProgressChanged(float NewValue)
{
	if (ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem())
	{
		Subsystem->SetDatamoshingProgress(NewValue);
	}
}
