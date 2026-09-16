#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SGraphicSettingDebuggerWidget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGraphicSettingDebuggerWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	struct FSettingDefinition
	{
		struct FOption
		{
			FString Value;
			FString Label;
		};

		enum class EControlType : uint8
		{
			Boolean,
			Enum,
			Numeric
		};

		FName Key;
		FText Label;
		FText Group;
		EControlType ControlType = EControlType::Boolean;
		TArray<FOption> Options;
		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		float Delta = 1.0f;
		FName DependsOn;
	};

private:

	TSharedRef<SWidget> MakeSettingRow(const FSettingDefinition& Definition);
	FString ReadValue(FName Key) const;
	void ApplyValue(FName Key, const FString& Value);
	void RecordBaseline();
	void CompareCurrent();
	void SaveSnapshot(const FString& Label);
	FReply OnApplyClicked(FName Key, TSharedRef<SEditableTextBox> ValueBox);
	bool IsDefinitionEnabled(const FSettingDefinition& Definition) const;
	TArray<FSettingDefinition> GetDefinitions() const;
	TSharedRef<SWidget> MakeEnumControl(const FSettingDefinition& Definition);
	TSharedRef<SWidget> MakeNumericControl(const FSettingDefinition& Definition);
	TSharedRef<SWidget> MakeBooleanControl(const FSettingDefinition& Definition);

	TMap<FName, FString> BaselineValues;
	TMap<FName, TArray<TSharedPtr<FSettingDefinition::FOption>>> ComboOptions;
	TSharedPtr<STextBlock> ComparisonText;
	TSharedPtr<SEditableTextBox> LabelBox;
};
