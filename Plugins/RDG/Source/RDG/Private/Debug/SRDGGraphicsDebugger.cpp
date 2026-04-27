#include "Debug/SRDGGraphicsDebugger.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
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
