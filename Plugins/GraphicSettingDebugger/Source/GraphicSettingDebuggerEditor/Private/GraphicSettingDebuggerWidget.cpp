#include "GraphicSettingDebuggerWidget.h"

#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Json.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UnrealClient.h"
#include "DynamicRHI.h"
#include "Styling/CoreStyle.h"

namespace
{
	TArray<SGraphicSettingDebuggerWidget::FSettingDefinition> BuildDefinitions()
	{
		TArray<SGraphicSettingDebuggerWidget::FSettingDefinition> Definitions;
		auto Add = [&Definitions](const TCHAR* Key, const TCHAR* Label, const TCHAR* Group,
			SGraphicSettingDebuggerWidget::FSettingDefinition::EControlType ControlType,
			const TArray<SGraphicSettingDebuggerWidget::FSettingDefinition::FOption>& Options = TArray<SGraphicSettingDebuggerWidget::FSettingDefinition::FOption>(),
			float MinValue = 0.0f, float MaxValue = 1.0f, float Delta = 1.0f, FName DependsOn = NAME_None)
		{
			SGraphicSettingDebuggerWidget::FSettingDefinition Definition;
			Definition.Key = FName(Key);
			Definition.Label = FText::FromString(Label);
			Definition.Group = FText::FromString(Group);
			Definition.ControlType = ControlType;
			Definition.Options = Options;
			Definition.MinValue = MinValue;
			Definition.MaxValue = MaxValue;
			Definition.Delta = Delta;
			Definition.DependsOn = DependsOn;
			Definitions.Add(MoveTemp(Definition));
		};
		using EControlType = SGraphicSettingDebuggerWidget::FSettingDefinition::EControlType;
		using FOption = SGraphicSettingDebuggerWidget::FSettingDefinition::FOption;

		Add(TEXT("r.Lumen.HardwareRayTracing"), TEXT("Use Hardware Ray Tracing when available"), TEXT("Lumen"), EControlType::Boolean);
		Add(TEXT("r.Lumen.HardwareRayTracing.LightingMode"), TEXT("Ray Lighting Mode"), TEXT("Lumen"), EControlType::Enum,
			{ FOption{TEXT("0"), TEXT("Surface Cache")}, FOption{TEXT("1"), TEXT("Global Tracing")} }, 0.0f, 1.0f, 1.0f, TEXT("r.Lumen.HardwareRayTracing"));
		Add(TEXT("r.Lumen.Reflections.Allow"), TEXT("Lumen Reflections"), TEXT("Lumen"), EControlType::Boolean);
		Add(TEXT("r.Lumen.Reflections.HardwareRayTracing"), TEXT("High Quality Translucency Reflections"), TEXT("Lumen"), EControlType::Boolean, {}, 0.0f, 1.0f, 1.0f, TEXT("r.Lumen.Reflections.Allow"));
		Add(TEXT("r.Lumen.Reflections.SmoothBias"), TEXT("Reflection Smooth Bias"), TEXT("Lumen"), EControlType::Numeric, {}, 0.0f, 1.0f, 0.05f, TEXT("r.Lumen.Reflections.Allow"));
		Add(TEXT("r.Lumen.ScreenProbeGather.ScreenTraces"), TEXT("Screen Tracing Source"), TEXT("Lumen"), EControlType::Enum,
			{ FOption{TEXT("0"), TEXT("Scene Color")}, FOption{TEXT("1"), TEXT("Global Tracing")} });
		Add(TEXT("r.Lumen.TranslucencyVolume.SkyLightLeaking"), TEXT("Ray Traced Translucent Refractions"), TEXT("Lumen"), EControlType::Boolean);

		Add(TEXT("r.RayTracing"), TEXT("Ray Tracing"), TEXT("Ray Tracing"), EControlType::Boolean);
		Add(TEXT("r.RayTracing.Reflections"), TEXT("Ray Traced Reflections"), TEXT("Ray Tracing"), EControlType::Boolean, {}, 0.0f, 1.0f, 1.0f, TEXT("r.RayTracing"));
		Add(TEXT("r.Translucency"), TEXT("Translucency"), TEXT("Translucency"), EControlType::Boolean);
		Add(TEXT("r.Translucency.VolumeBlur"), TEXT("Translucency Volume Blur"), TEXT("Translucency"), EControlType::Boolean, {}, 0.0f, 1.0f, 1.0f, TEXT("r.Translucency"));
		Add(TEXT("r.TranslucencyLightingVolumeDim"), TEXT("Translucency Lighting Volume Dim"), TEXT("Translucency"), EControlType::Numeric, {}, 4.0f, 128.0f, 4.0f, TEXT("r.Translucency"));
		Add(TEXT("r.ScreenPercentage"), TEXT("Screen Percentage"), TEXT("Resolution"), EControlType::Numeric, {}, 25.0f, 200.0f, 5.0f);
		Add(TEXT("r.DynamicRes.OperationMode"), TEXT("Dynamic Resolution Mode"), TEXT("Resolution"), EControlType::Enum,
			{ FOption{TEXT("0"), TEXT("Disabled")}, FOption{TEXT("1"), TEXT("Manual")}, FOption{TEXT("2"), TEXT("Auto") } });

		const TPair<const TCHAR*, const TCHAR*> ScalabilityDefinitions[] = {
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.ViewDistanceQuality"), TEXT("View Distance")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.AntiAliasingQuality"), TEXT("Anti-Aliasing")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.ShadowQuality"), TEXT("Shadows")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.GlobalIlluminationQuality"), TEXT("Global Illumination")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.ReflectionQuality"), TEXT("Reflections")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.PostProcessQuality"), TEXT("Post Process")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.TextureQuality"), TEXT("Textures")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.EffectsQuality"), TEXT("Effects")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.FoliageQuality"), TEXT("Foliage")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("sg.ShadingQuality"), TEXT("Shading"))
		};
		for (const TPair<const TCHAR*, const TCHAR*>& Entry : ScalabilityDefinitions)
		{
			Add(Entry.Key, Entry.Value, TEXT("Scalability"), EControlType::Numeric, {}, 0.0f, 4.0f, 1.0f);
		}
		return Definitions;
	}
}

void SGraphicSettingDebuggerWidget::Construct(const FArguments& InArgs)
{
	SAssignNew(ComparisonText, STextBlock)
		.Text(FText::FromString(TEXT("No comparison recorded yet.")))
		.AutoWrapText(true);

	SAssignNew(LabelBox, SEditableTextBox)
		.Text(FText::FromString(TEXT("baseline")))
		.MinDesiredWidth(160.0f);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot().AutoHeight().Padding(8.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT("Graphic Setting Debugger")))
		.Font(FCoreStyle::Get().GetFontStyle("Heading2"))
	];
	Content->AddSlot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT("Editor / PIE / Shipping 공통 CVar를 즉시 적용하고 캡처·스냅샷·비교합니다.")))
		.AutoWrapText(true)
	];
	Content->AddSlot().AutoHeight().Padding(8.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Label")))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f)
		[
			LabelBox.ToSharedRef()]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f)
		[
			SNew(SButton).Text(FText::FromString(TEXT("Record Baseline"))).OnClicked_Lambda([this]() { RecordBaseline(); return FReply::Handled(); })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
		[
			SNew(SButton).Text(FText::FromString(TEXT("Compare Current"))).OnClicked_Lambda([this]() { CompareCurrent(); return FReply::Handled(); })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
		[
			SNew(SButton).Text(FText::FromString(TEXT("Save Snapshot"))).OnClicked_Lambda([this]() { SaveSnapshot(LabelBox->GetText().ToString()); return FReply::Handled(); })]
	];
	Content->AddSlot().AutoHeight().Padding(8.0f)
	[
		SNew(SBorder)
		.Padding(8.0f)
		[
			ComparisonText.ToSharedRef()]
	];

	TSharedRef<SVerticalBox> Settings = SNew(SVerticalBox);
	const TArray<FSettingDefinition> Definitions = GetDefinitions();
	int32 DefinitionIndex = 0;
	while (DefinitionIndex < Definitions.Num())
	{
		const FText Group = Definitions[DefinitionIndex].Group;
		TSharedRef<SVerticalBox> GroupContent = SNew(SVerticalBox);
		GroupContent->AddSlot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(Group)
			.Font(FCoreStyle::Get().GetFontStyle("BoldFont"))
			.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
		];

		bool bFirstRow = true;
		while (DefinitionIndex < Definitions.Num() && Definitions[DefinitionIndex].Group.ToString() == Group.ToString())
		{
			if (!bFirstRow)
			{
				GroupContent->AddSlot().AutoHeight().Padding(8.0f, 2.0f)
				[
					SNew(SSeparator)
				];
			}
			GroupContent->AddSlot().AutoHeight().Padding(8.0f, 3.0f)
			[
				MakeSettingRow(Definitions[DefinitionIndex])
			];
			bFirstRow = false;
			++DefinitionIndex;
		}

		Settings->AddSlot().AutoHeight().Padding(8.0f, 6.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f, 0.96f))
			.Padding(4.0f)
			[
				GroupContent
			]
		];
	}

	Content->AddSlot().FillHeight(1.0f).Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()[Settings]
	];
	ChildSlot[SNew(SBorder).Padding(4.0f)[Content]];
}

TSharedRef<SWidget> SGraphicSettingDebuggerWidget::MakeSettingRow(const FSettingDefinition& Definition)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Definition.Label)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(12.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(260.0f)[
				Definition.ControlType == FSettingDefinition::EControlType::Boolean
					? MakeBooleanControl(Definition)
					: (Definition.ControlType == FSettingDefinition::EControlType::Enum ? MakeEnumControl(Definition) : MakeNumericControl(Definition))
			]
		]
		;
}

TArray<SGraphicSettingDebuggerWidget::FSettingDefinition> SGraphicSettingDebuggerWidget::GetDefinitions() const
{
	return BuildDefinitions();
}

bool SGraphicSettingDebuggerWidget::IsDefinitionEnabled(const FSettingDefinition& Definition) const
{
	if (Definition.DependsOn.IsNone())
	{
		return IConsoleManager::Get().FindConsoleVariable(*Definition.Key.ToString()) != nullptr;
	}

	const IConsoleVariable* Parent = IConsoleManager::Get().FindConsoleVariable(*Definition.DependsOn.ToString());
	return Parent && Parent->GetInt() != 0 && IConsoleManager::Get().FindConsoleVariable(*Definition.Key.ToString()) != nullptr;
}

TSharedRef<SWidget> SGraphicSettingDebuggerWidget::MakeBooleanControl(const FSettingDefinition& Definition)
{
	return SNew(SCheckBox)
		.IsChecked_Lambda([this, Key = Definition.Key]()
		{
			const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Key.ToString());
			return Variable && Variable->GetInt() != 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.IsEnabled_Lambda([this, Definition]() { return IsDefinitionEnabled(Definition); })
		.OnCheckStateChanged_Lambda([this, Key = Definition.Key](ECheckBoxState State)
		{
			ApplyValue(Key, State == ECheckBoxState::Checked ? TEXT("1") : TEXT("0"));
		});
}

TSharedRef<SWidget> SGraphicSettingDebuggerWidget::MakeEnumControl(const FSettingDefinition& Definition)
{
	TArray<TSharedPtr<FSettingDefinition::FOption>>& Options = ComboOptions.FindOrAdd(Definition.Key);
	for (const FSettingDefinition::FOption& Option : Definition.Options)
	{
		Options.Add(MakeShared<FSettingDefinition::FOption>(Option));
	}

	TSharedPtr<FSettingDefinition::FOption> InitialOption;
	const FString CurrentValue = ReadValue(Definition.Key);
	for (const TSharedPtr<FSettingDefinition::FOption>& Option : Options)
	{
		if (Option->Value == CurrentValue)
		{
			InitialOption = Option;
			break;
		}
	}
	if (!InitialOption.IsValid() && Options.Num() > 0)
	{
		InitialOption = Options[0];
	}

	return SNew(SComboBox<TSharedPtr<FSettingDefinition::FOption>>)
		.OptionsSource(&Options)
		.InitiallySelectedItem(InitialOption)
		.IsEnabled_Lambda([this, Definition]() { return IsDefinitionEnabled(Definition); })
		.OnGenerateWidget_Lambda([](TSharedPtr<FSettingDefinition::FOption> Option)
		{
			return SNew(STextBlock).Text(Option.IsValid() ? FText::FromString(Option->Label) : FText::FromString(TEXT("Unavailable")));
		})
		.OnSelectionChanged_Lambda([this, Key = Definition.Key](TSharedPtr<FSettingDefinition::FOption> Option, ESelectInfo::Type)
		{
			if (Option.IsValid())
			{
				ApplyValue(Key, Option->Value);
			}
		})
		[
			SNew(STextBlock).Text_Lambda([this, Key = Definition.Key]()
			{
				for (const TSharedPtr<FSettingDefinition::FOption>& Option : ComboOptions.FindChecked(Key))
				{
					if (Option->Value == ReadValue(Key))
					{
						return FText::FromString(Option->Label);
					}
				}
				return FText::FromString(ReadValue(Key));
			})
		];
}

TSharedRef<SWidget> SGraphicSettingDebuggerWidget::MakeNumericControl(const FSettingDefinition& Definition)
{
	return SNew(SSpinBox<float>)
		.Value_Lambda([this, Key = Definition.Key]() { return FCString::Atof(*ReadValue(Key)); })
		.MinValue(Definition.MinValue)
		.MaxValue(Definition.MaxValue)
		.Delta(Definition.Delta)
		.IsEnabled_Lambda([this, Definition]() { return IsDefinitionEnabled(Definition); })
		.OnValueChanged_Lambda([this, Key = Definition.Key](float Value)
		{
			ApplyValue(Key, FString::SanitizeFloat(Value));
		});
}

FString SGraphicSettingDebuggerWidget::ReadValue(const FName Key) const
{
	if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Key.ToString()))
	{
		return Variable->GetString();
	}
	return TEXT("N/A");
}

void SGraphicSettingDebuggerWidget::ApplyValue(const FName Key, const FString& Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Key.ToString()))
	{
		Variable->Set(*Value, ECVF_SetByCode);
	}
}

FReply SGraphicSettingDebuggerWidget::OnApplyClicked(const FName Key, TSharedRef<SEditableTextBox> ValueBox)
{
	ApplyValue(Key, ValueBox->GetText().ToString());
	return FReply::Handled();
}

void SGraphicSettingDebuggerWidget::RecordBaseline()
{
	BaselineValues.Empty();
	for (const FSettingDefinition& Definition : GetDefinitions())
	{
		BaselineValues.Add(Definition.Key, ReadValue(Definition.Key));
	}
	ComparisonText->SetText(FText::FromString(TEXT("Baseline recorded. Change values, then press Compare Current.")));
}

void SGraphicSettingDebuggerWidget::CompareCurrent()
{
	int32 Changed = 0;
	for (const FSettingDefinition& Definition : GetDefinitions())
	{
		if (const FString* Baseline = BaselineValues.Find(Definition.Key))
		{
			Changed += *Baseline != ReadValue(Definition.Key) ? 1 : 0;
		}
	}
	ComparisonText->SetText(FText::FromString(FString::Printf(TEXT("Comparison complete: %d / %d settings changed."), Changed, BaselineValues.Num())));
}

void SGraphicSettingDebuggerWidget::SaveSnapshot(const FString& Label)
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GraphicSettingDebugger"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("label"), Label);
	Root->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());
	const double DeltaTime = FApp::GetDeltaTime();
	Root->SetNumberField(TEXT("fps"), DeltaTime > SMALL_NUMBER ? 1.0 / DeltaTime : 0.0);
	Root->SetNumberField(TEXT("frameMs"), DeltaTime * 1000.0);
	Root->SetNumberField(TEXT("gpuFrameMs"), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
	TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
	for (const FSettingDefinition& Definition : GetDefinitions())
	{
		Settings->SetStringField(Definition.Key.ToString(), ReadValue(Definition.Key));
	}
	Root->SetObjectField(TEXT("settings"), Settings);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("editor_snapshot_%s_%s.json"), *Label, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	FFileHelper::SaveStringToFile(Json, *Path);
	ComparisonText->SetText(FText::FromString(FString::Printf(TEXT("Snapshot saved: %s"), *Path)));
}
