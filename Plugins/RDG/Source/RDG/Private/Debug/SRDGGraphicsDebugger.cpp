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
