#include "OutlierInputMappableToolWidget.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EnhancedActionKeyMapping.h"
#include "Input/OutlierInputBindingSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SOutlierInputMappableToolWidget"

namespace
{
	const FName CheckColumnId(TEXT("Check"));
	const FName InUseColumnId(TEXT("InUse"));
	const FName ContextColumnId(TEXT("Context"));
	const FName TypeColumnId(TEXT("Type"));
	const FName KeyColumnId(TEXT("Key"));
	const FName MappingNameColumnId(TEXT("MappingName"));
	const FName DisplayNameColumnId(TEXT("DisplayName"));

	EOutlierInputMappableInputType ResolveInputType(const FKey& Key)
	{
		if (Key.IsGamepadKey())
		{
			return EOutlierInputMappableInputType::Gamepad;
		}

		if (Key.IsMouseButton())
		{
			return EOutlierInputMappableInputType::Mouse;
		}

		return EOutlierInputMappableInputType::Keyboard;
	}

	FText GetInputTypeDisplayText(EOutlierInputMappableInputType InputType)
	{
		switch (InputType)
		{
		case EOutlierInputMappableInputType::Mouse:
			return LOCTEXT("InputTypeMouse", "Mouse");
		case EOutlierInputMappableInputType::Gamepad:
			return LOCTEXT("InputTypeGamepad", "Gamepad");
		case EOutlierInputMappableInputType::Keyboard:
		default:
			return LOCTEXT("InputTypeKeyboard", "Keyboard");
		}
	}

	FText MakeTempDisplayName(int32 EntryIndex)
	{
		return FText::FromString(FString::Printf(TEXT("Temp_%d"), EntryIndex));
	}

	FString SanitizeNameToken(FString Token)
	{
		for (TCHAR& Character : Token)
		{
			if (!FChar::IsAlnum(Character))
			{
				Character = TEXT('_');
			}
		}

		while (Token.Contains(TEXT("__")))
		{
			Token.ReplaceInline(TEXT("__"), TEXT("_"));
		}

		Token.RemoveFromStart(TEXT("_"));
		Token.RemoveFromEnd(TEXT("_"));

		return Token.IsEmpty() ? TEXT("None") : Token;
	}

	FName MakeDefaultMappingName(
		const UInputAction& InputAction,
		const FEnhancedActionKeyMapping& Mapping,
		bool bActionHasMultipleMappings)
	{
		const FString ActionName = SanitizeNameToken(InputAction.GetName());
		if (!bActionHasMultipleMappings)
		{
			return FName(*ActionName);
		}

		const FString KeyName = SanitizeNameToken(Mapping.Key.ToString());
		return FName(*FString::Printf(
			TEXT("%s_%s"),
			*ActionName,
			*KeyName));
	}

	FString MakeContextActionKey(
		const UInputMappingContext& MappingContext,
		const UInputAction& InputAction)
	{
		return FString::Printf(
			TEXT("%s|%s"),
			*MappingContext.GetPathName(),
			*InputAction.GetPathName());
	}

	bool SetMappingSettingBehavior(FEnhancedActionKeyMapping& Mapping)
	{
		FProperty* SettingBehaviorProperty =
			FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(TEXT("SettingBehavior"));
		if (!SettingBehaviorProperty)
		{
			return false;
		}

		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(SettingBehaviorProperty))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProperty->ContainerPtrToValuePtr<void>(&Mapping),
				static_cast<int64>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
			return true;
		}

		if (FByteProperty* ByteProperty = CastField<FByteProperty>(SettingBehaviorProperty))
		{
			ByteProperty->SetPropertyValue_InContainer(
				&Mapping,
				static_cast<uint8>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
			return true;
		}

		return false;
	}

	bool ClearMappingSettingBehavior(FEnhancedActionKeyMapping& Mapping)
	{
		FProperty* SettingBehaviorProperty =
			FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(TEXT("SettingBehavior"));
		if (!SettingBehaviorProperty)
		{
			return false;
		}

		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(SettingBehaviorProperty))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProperty->ContainerPtrToValuePtr<void>(&Mapping),
				static_cast<int64>(EPlayerMappableKeySettingBehaviors::InheritSettingsFromAction));
			return true;
		}

		if (FByteProperty* ByteProperty = CastField<FByteProperty>(SettingBehaviorProperty))
		{
			ByteProperty->SetPropertyValue_InContainer(
				&Mapping,
				static_cast<uint8>(EPlayerMappableKeySettingBehaviors::InheritSettingsFromAction));
			return true;
		}

		return false;
	}

	// Clears the mapping-owned PlayerMappableKeySettings (if any) so the
	// mapping goes back to having no Name, i.e. undoes what Apply did.
	bool ClearMappingOwnedSettings(FEnhancedActionKeyMapping& Mapping)
	{
		FObjectPropertyBase* SettingsProperty = CastField<FObjectPropertyBase>(
			FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(
				TEXT("PlayerMappableKeySettings")));
		if (!SettingsProperty)
		{
			return false;
		}

		void* SettingsAddress = SettingsProperty->ContainerPtrToValuePtr<void>(&Mapping);
		SettingsProperty->SetObjectPropertyValue(SettingsAddress, nullptr);
		return true;
	}

	UPlayerMappableKeySettings* ResolveOrCreateMappableSettings(
		FEnhancedActionKeyMapping& Mapping,
		UObject* Outer)
	{
		FObjectPropertyBase* SettingsProperty = CastField<FObjectPropertyBase>(
			FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(
				TEXT("PlayerMappableKeySettings")));
		if (!SettingsProperty)
		{
			return nullptr;
		}

		void* SettingsAddress = SettingsProperty->ContainerPtrToValuePtr<void>(&Mapping);
		UPlayerMappableKeySettings* Settings = Cast<UPlayerMappableKeySettings>(
			SettingsProperty->GetObjectPropertyValue(SettingsAddress));
		if (!Settings)
		{
			Settings = NewObject<UPlayerMappableKeySettings>(
				Outer,
				UPlayerMappableKeySettings::StaticClass(),
				NAME_None,
				RF_Transactional);
			SettingsProperty->SetObjectPropertyValue(SettingsAddress, Settings);
		}

		return Settings;
	}

	const UPlayerMappableKeySettings* GetMappingOwnedMappableSettings(
		const FEnhancedActionKeyMapping& Mapping)
	{
		FObjectPropertyBase* SettingsProperty = CastField<FObjectPropertyBase>(
			FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(
				TEXT("PlayerMappableKeySettings")));
		if (!SettingsProperty)
		{
			return nullptr;
		}

		const void* SettingsAddress =
			SettingsProperty->ContainerPtrToValuePtr<void>(&Mapping);
		return Cast<UPlayerMappableKeySettings>(
			SettingsProperty->GetObjectPropertyValue(SettingsAddress));
	}

	bool ResolveInitialInUse(const FEnhancedActionKeyMapping& Mapping)
	{
		const UPlayerMappableKeySettings* MappingOwnedSettings =
			GetMappingOwnedMappableSettings(Mapping);
		return MappingOwnedSettings && !MappingOwnedSettings->Name.IsNone();
	}

	FText ResolveInitialDisplayName(
		const FEnhancedActionKeyMapping& Mapping,
		int32 TempIndex)
	{
		const UPlayerMappableKeySettings* MappingOwnedSettings =
			GetMappingOwnedMappableSettings(Mapping);
		if (MappingOwnedSettings && !MappingOwnedSettings->DisplayName.IsEmpty())
		{
			return MappingOwnedSettings->DisplayName;
		}

		return MakeTempDisplayName(TempIndex);
	}

	class SOutlierInputMappableTableRow
		: public SMultiColumnTableRow<TSharedPtr<FOutlierInputMappableRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SOutlierInputMappableTableRow) {}
			SLATE_ARGUMENT(TSharedPtr<FOutlierInputMappableRow>, RowItem)
		SLATE_END_ARGS()

		void Construct(
			const FArguments& InArgs,
			const TSharedRef<STableViewBase>& OwnerTableView)
		{
			RowItem = InArgs._RowItem;
			SMultiColumnTableRow<TSharedPtr<FOutlierInputMappableRow>>::Construct(
				FSuperRowType::FArguments().Padding(2.0f),
				OwnerTableView);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(
			const FName& ColumnName) override
		{
			if (!RowItem.IsValid())
			{
				return SNew(STextBlock);
			}

			if (ColumnName == CheckColumnId)
			{
				return SNew(SCheckBox)
					.IsChecked_Lambda(
						[Row = RowItem]()
						{
							return Row.IsValid() && Row->bMakeMappable
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						})
					.OnCheckStateChanged_Lambda(
						[Row = RowItem](ECheckBoxState NewState)
						{
							if (Row.IsValid())
							{
								Row->bMakeMappable = NewState == ECheckBoxState::Checked;
							}
						});
			}

			if (ColumnName == InUseColumnId)
			{
				return SNew(SCheckBox)
					.IsEnabled(false)
					.ToolTipText(LOCTEXT("InUseTooltip",
						"Already mappable on the IMC (i.e. already in the binding table) from an earlier Apply."))
					.IsChecked_Lambda(
						[Row = RowItem]()
						{
							return Row.IsValid() && Row->bInUse
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						});
			}

			if (ColumnName == ContextColumnId)
			{
				return SNew(STextBlock)
					.Text_Lambda(
						[Row = RowItem]()
						{
							const UInputMappingContext* Context = Row.IsValid()
								? Row->MappingContext.Get()
								: nullptr;
							return Context
								? FText::FromString(Context->GetName())
								: LOCTEXT("MissingContext", "Missing");
						});
			}

			if (ColumnName == TypeColumnId)
			{
				return SNew(STextBlock)
					.Text_Lambda(
						[Row = RowItem]()
						{
							return Row.IsValid()
								? GetInputTypeDisplayText(Row->InputType)
								: FText::GetEmpty();
						});
			}

			if (ColumnName == KeyColumnId)
			{
				return SNew(STextBlock)
					.Text_Lambda(
						[Row = RowItem]()
						{
							return Row.IsValid()
								? FText::FromString(Row->Key.ToString())
								: FText::GetEmpty();
						});
			}

			if (ColumnName == MappingNameColumnId)
			{
				return SNew(STextBlock)
					.Text_Lambda(
						[Row = RowItem]()
						{
							return Row.IsValid()
								? FText::FromName(Row->MappingName)
								: FText::GetEmpty();
						});
			}

			if (ColumnName == DisplayNameColumnId)
			{
				return SNew(SEditableTextBox)
					.MinDesiredWidth(160.0f)
					.Text_Lambda(
						[Row = RowItem]()
						{
							return Row.IsValid()
								? Row->DisplayName
								: FText::GetEmpty();
						})
					.OnTextCommitted_Lambda(
						[Row = RowItem](const FText& NewText, ETextCommit::Type)
						{
							if (Row.IsValid())
							{
								Row->DisplayName = NewText;
							}
						});
			}

			return SNew(STextBlock);
		}

	private:
		TSharedPtr<FOutlierInputMappableRow> RowItem;
	};
}

void SOutlierInputMappableToolWidget::Construct(const FArguments& InArgs)
{
	StatusText = LOCTEXT("InitialStatus", "Ready.");

	ChildSlot
	[
		SNew(SBorder)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ScanButton", "Scan IMCs"))
					.OnClicked(this, &SOutlierInputMappableToolWidget::ScanInputMappingContexts)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CheckAllButton", "Check All"))
					.OnClicked(this, &SOutlierInputMappableToolWidget::CheckAllRows)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("UncheckAllButton", "Uncheck All"))
					.OnClicked(this, &SOutlierInputMappableToolWidget::UncheckAllRows)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyButton", "Apply Checked"))
					.OnClicked(this, &SOutlierInputMappableToolWidget::ApplyCheckedMappings)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("FlushButton", "Flush Checked"))
					.ToolTipText(LOCTEXT("FlushButtonTooltip",
						"For checked rows: clears the IMC mapping's rebindable/mappable override, undoing Apply. Use this to remove stale mappings a previous Apply left behind."))
					.OnClicked(this, &SOutlierInputMappableToolWidget::FlushCheckedMappings)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(18.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SOutlierInputMappableToolWidget::GetShowKeyboardState)
					.OnCheckStateChanged(this, &SOutlierInputMappableToolWidget::OnShowKeyboardStateChanged)
					.Content()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ShowKeyboardCheckbox", "Show Keyboard"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SOutlierInputMappableToolWidget::GetShowMouseState)
					.OnCheckStateChanged(this, &SOutlierInputMappableToolWidget::OnShowMouseStateChanged)
					.Content()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ShowMouseCheckbox", "Show Mouse"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return StatusText; })
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(RowListView, SListView<FRowPtr>)
				.ListItemsSource(&FilteredRows)
				.OnGenerateRow(this, &SOutlierInputMappableToolWidget::GenerateRow)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(CheckColumnId)
					.FixedWidth(48.0f)
					.DefaultLabel(LOCTEXT("CheckColumn", "Use"))
					+ SHeaderRow::Column(InUseColumnId)
					.FixedWidth(56.0f)
					.DefaultLabel(LOCTEXT("InUseColumn", "In Use"))
					+ SHeaderRow::Column(ContextColumnId)
					.FillWidth(0.22f)
					.DefaultLabel(LOCTEXT("ContextColumn", "Context"))
					+ SHeaderRow::Column(TypeColumnId)
					.FixedWidth(72.0f)
					.DefaultLabel(LOCTEXT("TypeColumn", "Type"))
					+ SHeaderRow::Column(KeyColumnId)
					.FillWidth(0.14f)
					.DefaultLabel(LOCTEXT("KeyColumn", "Key"))
					+ SHeaderRow::Column(MappingNameColumnId)
					.FillWidth(0.26f)
					.DefaultLabel(LOCTEXT("MappingNameColumn", "Name"))
					+ SHeaderRow::Column(DisplayNameColumnId)
					.FillWidth(0.30f)
					.DefaultLabel(LOCTEXT("DisplayNameColumn", "Display Name")))
			]
		]
	];
}

TSharedRef<ITableRow> SOutlierInputMappableToolWidget::GenerateRow(
	FRowPtr Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SOutlierInputMappableTableRow, OwnerTable)
		.RowItem(Row);
}

FReply SOutlierInputMappableToolWidget::ScanInputMappingContexts()
{
	TArray<UInputMappingContext*> MappingContexts;
	CollectConfiguredInputMappingContexts(MappingContexts);
	MappingContexts.Sort(
		[](const UInputMappingContext& Left, const UInputMappingContext& Right)
		{
			return Left.GetPathName() < Right.GetPathName();
		});

	Rows.Reset();
	int32 TempIndex = 0;

	TMap<FString, int32> ActionMappingCountsByContext;
	for (const UInputMappingContext* MappingContext : MappingContexts)
	{
		if (!MappingContext)
		{
			continue;
		}

		for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
		{
			if (Mapping.Action && Mapping.Key.IsValid())
			{
				ActionMappingCountsByContext.FindOrAdd(
					MakeContextActionKey(*MappingContext, *Mapping.Action))++;
			}
		}
	}

	for (UInputMappingContext* MappingContext : MappingContexts)
	{
		if (!MappingContext)
		{
			continue;
		}

		const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();
		for (int32 MappingIndex = 0; MappingIndex < Mappings.Num(); ++MappingIndex)
		{
			const FEnhancedActionKeyMapping& Mapping = Mappings[MappingIndex];
			const UInputAction* InputAction = Mapping.Action.Get();
			if (!InputAction || !Mapping.Key.IsValid())
			{
				continue;
			}

			FRowPtr Row = MakeShared<FOutlierInputMappableRow>();
			Row->MappingContext = MappingContext;
			Row->InputAction = const_cast<UInputAction*>(InputAction);
			Row->Key = Mapping.Key;
			Row->MappingIndex = MappingIndex;
			Row->MappingName = MakeDefaultMappingName(
				*InputAction,
				Mapping,
				ActionMappingCountsByContext.FindRef(
					MakeContextActionKey(*MappingContext, *InputAction)) > 1);
			Row->DisplayName = ResolveInitialDisplayName(Mapping, TempIndex);
			Row->InputType = ResolveInputType(Mapping.Key);
			Row->bInUse = ResolveInitialInUse(Mapping);

			Rows.Add(Row);
			++TempIndex;
		}
	}

	RefreshFilteredRows();

	SetStatus(FText::Format(
		LOCTEXT("ScanStatus", "Scanned {0} mappings from {1} contexts."),
		FText::AsNumber(Rows.Num()),
		FText::AsNumber(MappingContexts.Num())));

	return FReply::Handled();
}

FReply SOutlierInputMappableToolWidget::ApplyCheckedMappings()
{
	int32 AppliedCount = 0;
	TSet<UInputMappingContext*> ModifiedContexts;

	for (const FRowPtr& Row : Rows)
	{
		if (!Row.IsValid() || !Row->bMakeMappable)
		{
			continue;
		}

		UInputMappingContext* MappingContext = Row->MappingContext.Get();
		if (!MappingContext)
		{
			continue;
		}

		const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();
		if (!Mappings.IsValidIndex(Row->MappingIndex))
		{
			continue;
		}

		FEnhancedActionKeyMapping& Mapping = MappingContext->GetMapping(Row->MappingIndex);
		if (Row->InputAction.IsValid() && Mapping.Action.Get() != Row->InputAction.Get())
		{
			continue;
		}

		if (Mapping.Key != Row->Key)
		{
			continue;
		}

		MappingContext->Modify();
		if (!SetMappingSettingBehavior(Mapping))
		{
			continue;
		}

		UPlayerMappableKeySettings* Settings =
			ResolveOrCreateMappableSettings(Mapping, MappingContext);
		if (!Settings)
		{
			continue;
		}

		Settings->Modify();
		Settings->Name = Row->MappingName.IsNone() && Row->InputAction.IsValid()
			? Row->InputAction->GetFName()
			: Row->MappingName;
		Settings->DisplayName = Row->DisplayName.IsEmpty()
			? MakeTempDisplayName(AppliedCount)
			: Row->DisplayName;

		MappingContext->MarkPackageDirty();
		ModifiedContexts.Add(MappingContext);
		Row->bInUse = !Settings->Name.IsNone();
		++AppliedCount;
	}

	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}

	SetStatus(FText::Format(
		LOCTEXT("ApplyStatus", "Applied {0} mappings. Modified {1} contexts. Save changed IMCs."),
		FText::AsNumber(AppliedCount),
		FText::AsNumber(ModifiedContexts.Num())));

	return FReply::Handled();
}

FReply SOutlierInputMappableToolWidget::FlushCheckedMappings()
{
	int32 FlushedCount = 0;
	TSet<UInputMappingContext*> ModifiedContexts;

	for (const FRowPtr& Row : Rows)
	{
		if (!Row.IsValid() || !Row->bMakeMappable)
		{
			continue;
		}

		UInputMappingContext* MappingContext = Row->MappingContext.Get();
		if (!MappingContext)
		{
			continue;
		}

		const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();
		if (!Mappings.IsValidIndex(Row->MappingIndex))
		{
			continue;
		}

		FEnhancedActionKeyMapping& Mapping = MappingContext->GetMapping(Row->MappingIndex);
		if (Row->InputAction.IsValid() && Mapping.Action.Get() != Row->InputAction.Get())
		{
			continue;
		}

		if (Mapping.Key != Row->Key)
		{
			continue;
		}

		MappingContext->Modify();
		ClearMappingSettingBehavior(Mapping);
		ClearMappingOwnedSettings(Mapping);

		MappingContext->MarkPackageDirty();
		ModifiedContexts.Add(MappingContext);
		Row->bInUse = false;
		++FlushedCount;
	}

	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}

	SetStatus(FText::Format(
		LOCTEXT("FlushStatus",
			"Flushed {0} mappings back to not-mappable. Modified {1} contexts. Save changed IMCs, then Rebuild the table."),
		FText::AsNumber(FlushedCount),
		FText::AsNumber(ModifiedContexts.Num())));

	return FReply::Handled();
}

FReply SOutlierInputMappableToolWidget::CheckAllRows()
{
	for (const FRowPtr& Row : FilteredRows)
	{
		if (Row.IsValid())
		{
			Row->bMakeMappable = true;
		}
	}

	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}

	return FReply::Handled();
}

FReply SOutlierInputMappableToolWidget::UncheckAllRows()
{
	for (const FRowPtr& Row : FilteredRows)
	{
		if (Row.IsValid())
		{
			Row->bMakeMappable = false;
		}
	}

	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}

	return FReply::Handled();
}

void SOutlierInputMappableToolWidget::SetStatus(const FText& NewStatusText)
{
	StatusText = NewStatusText;
}

ECheckBoxState SOutlierInputMappableToolWidget::GetShowKeyboardState() const
{
	return bShowKeyboardInputs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOutlierInputMappableToolWidget::OnShowKeyboardStateChanged(ECheckBoxState NewState)
{
	bShowKeyboardInputs = NewState == ECheckBoxState::Checked;
	RefreshFilteredRows();
}

ECheckBoxState SOutlierInputMappableToolWidget::GetShowMouseState() const
{
	return bShowMouseInputs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOutlierInputMappableToolWidget::OnShowMouseStateChanged(ECheckBoxState NewState)
{
	bShowMouseInputs = NewState == ECheckBoxState::Checked;
	RefreshFilteredRows();
}

void SOutlierInputMappableToolWidget::RefreshFilteredRows()
{
	FilteredRows.Reset();
	for (const FRowPtr& Row : Rows)
	{
		if (!Row.IsValid())
		{
			continue;
		}

		// Gamepad mappings are always shown; the Key/Mouse filters only split
		// out the two input types the user asked to view separately.
		if (Row->InputType == EOutlierInputMappableInputType::Keyboard && !bShowKeyboardInputs)
		{
			continue;
		}

		if (Row->InputType == EOutlierInputMappableInputType::Mouse && !bShowMouseInputs)
		{
			continue;
		}

		FilteredRows.Add(Row);
	}

	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

void SOutlierInputMappableToolWidget::CollectConfiguredInputMappingContexts(
	TArray<UInputMappingContext*>& OutMappingContexts) const
{
	const UOutlierInputBindingSettings* InputBindingSettings =
		GetDefault<UOutlierInputBindingSettings>();
	if (!InputBindingSettings)
	{
		return;
	}

	for (const TSoftObjectPtr<UInputMappingContext>& ContextPtr
		: InputBindingSettings->InputMappingContextsToScan)
	{
		if (UInputMappingContext* MappingContext = ContextPtr.LoadSynchronous())
		{
			OutMappingContexts.AddUnique(MappingContext);
		}
	}

	const FString DirectoryPath = InputBindingSettings->ContentInputDirectory.Path;
	if (!DirectoryPath.IsEmpty() && FPackageName::IsValidLongPackageName(DirectoryPath))
	{
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FAssetData> ContextAssets;
		AssetRegistry.GetAssetsByPath(FName(*DirectoryPath), ContextAssets, true);
		for (const FAssetData& ContextAsset : ContextAssets)
		{
			if (UInputMappingContext* MappingContext =
				Cast<UInputMappingContext>(ContextAsset.GetAsset()))
			{
				OutMappingContexts.AddUnique(MappingContext);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
