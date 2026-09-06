#include "AudioTagHelperPanel.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Audio/OutlierAudioTypes.h"
#include "Editor.h"
#include "Factories/DataAssetFactory.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "IAssetTools.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "SGameplayTagWidget.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAudioTagHelperPanel"

namespace AudioTagHelperPanel
{
	const FName AudioTagSourceName(TEXT("AudioTags.ini"));
	const FString AudioTypeRoot(TEXT("Audio.Type"));
	const FString AudioContextRoot(TEXT("Audio.Context"));
	const FString DefaultAssetFolder(TEXT("/Game/Audio/Definitions"));
	const FString DefaultBankFolder(TEXT("/Game/Audio/Banks"));

	FString MakeFullAudioTag(const FString& RootTag, FString RawRelativePath)
	{
		RawRelativePath.TrimStartAndEndInline();
		if (RawRelativePath.Equals(RootTag, ESearchCase::IgnoreCase))
		{
			return FString();
		}

		const FString Prefix = RootTag + TEXT(".");
		if (RawRelativePath.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			RawRelativePath.RightChopInline(Prefix.Len());
			RawRelativePath.TrimStartAndEndInline();
		}

		return RawRelativePath.IsEmpty() ? FString() : Prefix + RawRelativePath;
	}

	bool IsTagUnderRoot(const FGameplayTag& Tag, const FString& RootTag)
	{
		const FString TagString = Tag.ToString();
		return TagString.StartsWith(RootTag + TEXT("."));
	}

	bool IsTagFromAudioTagSource(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return false;
		}

		FString Comment;
		TArray<FName> SourceNames;
		bool bIsExplicit = false;
		bool bIsRestricted = false;
		bool bAllowsNonRestrictedChildren = false;
		const bool bFound = UGameplayTagsManager::Get().GetTagEditorData(
			Tag.GetTagName(),
			Comment,
			SourceNames,
			bIsExplicit,
			bIsRestricted,
			bAllowsNonRestrictedChildren);

		return bFound && bIsExplicit && SourceNames.Contains(AudioTagSourceName);
	}

	// Bank 이름은 Type 태그에서 자동 파생된다 ( 카테고리당 보통 1개, 디자이너가 직접 지을 필요 없음 ).
	FString MakeBankAssetName(const FString& FullTypeTag)
	{
		FString LeafName = FullTypeTag;
		LeafName.RemoveFromStart(AudioTypeRoot + TEXT("."));
		LeafName.ReplaceInline(TEXT("."), TEXT("_"));

		FString BaseName = TEXT("AB_") + LeafName;
		return ObjectTools::SanitizeObjectName(BaseName);
	}

	// Definition 이름. 비워두면 <TypeLeaf>_<Bank 안의 순번> 으로 자동 넘버링한다 —
	// 하나의 Type 아래 여러 Definition 이 생기는 게 정상이라 Event 때처럼 이름이 유일하지 않다.
	FString MakeDefinitionAssetName(
		const FString& RawAssetName,
		const FString& FullTypeTag,
		int32 ExistingDefinitionCountInBank)
	{
		FString BaseName = RawAssetName;
		BaseName.TrimStartAndEndInline();

		if (BaseName.IsEmpty())
		{
			FString LeafName = FullTypeTag;
			LeafName.RemoveFromStart(AudioTypeRoot + TEXT("."));
			LeafName.ReplaceInline(TEXT("."), TEXT("_"));
			BaseName = FString::Printf(TEXT("%s_%02d"), *LeafName, ExistingDefinitionCountInBank + 1);
		}

		if (!BaseName.StartsWith(TEXT("DA_")))
		{
			BaseName = TEXT("DA_") + BaseName;
		}

		return ObjectTools::SanitizeObjectName(BaseName);
	}

	void ShowResultMessage(const FText& Message)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Message);
	}
}

void SAudioTagHelperPanel::Construct(const FArguments& InArgs)
{
	SelectedTypeTags = MakeShared<FGameplayTagContainer>();
	SelectedContextTags = MakeShared<FGameplayTagContainer>();

	TArray<SGameplayTagWidget::FEditableGameplayTagContainerDatum> TypeTagContainers;
	TypeTagContainers.Emplace(nullptr, SelectedTypeTags.Get());
	TArray<SGameplayTagWidget::FEditableGameplayTagContainerDatum> ContextTagContainers;
	ContextTagContainers.Emplace(nullptr, SelectedContextTags.Get());

	ChildSlot
	[
		SNew(SBorder)
		.Padding(16.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Audio Tag Helper"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Summary", "Select existing audio tags or add new Type and Context children. Playback and network routing are selected by native call sites."))
					.AutoWrapText(true)
				]

				// Existing Type selection
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ExistingTypeLabel", "Existing Type Tag (Audio Bank category)"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SAssignNew(TypeTagWidget, SGameplayTagWidget, TypeTagContainers)
					.Filter(AudioTagHelperPanel::AudioTypeRoot)
					.TagContainerName(TEXT("AudioTagHelper.Type"))
					.MultiSelect(false)
					.GameplayTagUIMode(EGameplayTagUIMode::SelectionMode)
					.MaxHeight(180.0f)
					.ForceHideAddNewTag(true)
					.ForceHideAddNewTagSource(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
					.ContentPadding(0.0f)
					.OnClicked(this, &SAudioTagHelperPanel::ToggleTypeAddForm)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(this, &SAudioTagHelperPanel::GetTypeAddToggleText)
							.ColorAndOpacity(FLinearColor(0.35f, 1.0f, 0.15f))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AddTypeToggleLabel", "Add Type Tag"))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(SGridPanel)
					.Visibility(this, &SAudioTagHelperPanel::GetTypeAddFormVisibility)
					.FillColumn(1, 1.0f)
					+ SGridPanel::Slot(0, 0)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("TypeNameLabel", "Name:"))
					]
					+ SGridPanel::Slot(1, 0)
					.Padding(2.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("TypeParentPrefix", "Audio.Type."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(NewTypeNameTextBox, SEditableTextBox)
							.HintText(LOCTEXT("TypeNameHint", "Weapon"))
						]
					]
					+ SGridPanel::Slot(0, 1)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("TypeCommentLabel", "Comment:"))
					]
					+ SGridPanel::Slot(1, 1)
					.Padding(2.0f)
					[
						SAssignNew(NewTypeCommentTextBox, SEditableTextBox)
						.HintText(LOCTEXT("TypeCommentHint", "Comment"))
					]
					+ SGridPanel::Slot(0, 2)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("TypeSourceLabel", "Source:"))
					]
					+ SGridPanel::Slot(1, 2)
					.Padding(2.0f)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromName(AudioTagHelperPanel::AudioTagSourceName))
						.IsReadOnly(true)
					]
					+ SGridPanel::Slot(0, 3)
					.ColumnSpan(2)
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.Text(LOCTEXT("AddTypeButton", "Add New Tag"))
						.OnClicked(this, &SAudioTagHelperPanel::AddTypeTag)
					]
				]

				// Target Definition: 기존 Definition 에 Variant 를 추가할지, 새로 만들지.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TargetDefinitionLabel", "Target Data Asset (add as a new Variant, or create a new Data Asset)"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(DefinitionChoiceCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&DefinitionChoiceLabels)
						.OnComboBoxOpening(this, &SAudioTagHelperPanel::HandleDefinitionComboOpening)
						.OnGenerateWidget(this, &SAudioTagHelperPanel::GenerateDefinitionChoiceWidget)
						.OnSelectionChanged(this, &SAudioTagHelperPanel::OnDefinitionChoiceSelected)
						[
							SNew(STextBlock).Text(this, &SAudioTagHelperPanel::GetSelectedDefinitionChoiceText)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("RefreshDefinitionsButton", "Refresh"))
						.ToolTipText(LOCTEXT("RefreshDefinitionsTooltip", "Re-scan the selected Type's Bank for existing Data Assets."))
						.OnClicked(this, &SAudioTagHelperPanel::RefreshDefinitionChoices)
					]
				]

				// Existing Context selection
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ExistingContextLabel", "Context Tag for this Sound (one tag = one sound; use Weight for random variety)"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SAssignNew(ContextTagWidget, SGameplayTagWidget, ContextTagContainers)
					.Filter(AudioTagHelperPanel::AudioContextRoot)
					.TagContainerName(TEXT("AudioTagHelper.Context"))
					.MultiSelect(false)
					.GameplayTagUIMode(EGameplayTagUIMode::SelectionMode)
					.MaxHeight(220.0f)
					.ForceHideAddNewTag(true)
					.ForceHideAddNewTagSource(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
					.ContentPadding(0.0f)
					.OnClicked(this, &SAudioTagHelperPanel::ToggleContextAddForm)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(this, &SAudioTagHelperPanel::GetContextAddToggleText)
							.ColorAndOpacity(FLinearColor(0.35f, 1.0f, 0.15f))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AddContextToggleLabel", "Add Context Tag"))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(SGridPanel)
					.Visibility(this, &SAudioTagHelperPanel::GetContextAddFormVisibility)
					.FillColumn(1, 1.0f)
					+ SGridPanel::Slot(0, 0)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("ContextNameLabel", "Name:"))
					]
					+ SGridPanel::Slot(1, 0)
					.Padding(2.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("ContextParentPrefix", "Audio.Context."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(NewContextNameTextBox, SEditableTextBox)
							.HintText(LOCTEXT("ContextNameHint", "Weapon.Type.Rifle"))
						]
					]
					+ SGridPanel::Slot(0, 1)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("ContextCommentLabel", "Comment:"))
					]
					+ SGridPanel::Slot(1, 1)
					.Padding(2.0f)
					[
						SAssignNew(NewContextCommentTextBox, SEditableTextBox)
						.HintText(LOCTEXT("ContextCommentHint", "Comment"))
					]
					+ SGridPanel::Slot(0, 2)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("ContextSourceLabel", "Source:"))
					]
					+ SGridPanel::Slot(1, 2)
					.Padding(2.0f)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromName(AudioTagHelperPanel::AudioTagSourceName))
						.IsReadOnly(true)
					]
					+ SGridPanel::Slot(0, 3)
					.ColumnSpan(2)
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.Text(LOCTEXT("AddContextButton", "Add New Tag"))
						.OnClicked(this, &SAudioTagHelperPanel::AddContextTag)
					]
				]

				// Data Asset
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetNameLabel", "Data Asset Name (DA_ is added automatically; empty auto-numbers within the Bank)"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SAssignNew(AssetNameTextBox, SEditableTextBox)
					.HintText(LOCTEXT("AssetNameHint", "Empty auto-numbers using the selected Type"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetFolderLabel", "Data Asset Folder"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SAssignNew(AssetFolderTextBox, SEditableTextBox)
					.Text(FText::FromString(AudioTagHelperPanel::DefaultAssetFolder))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 20.0f)
				[
					SNew(SBorder)
					.Padding(10.0f)
					[
						SNew(STextBlock)
						.Text(this, &SAudioTagHelperPanel::GetOutputPreview)
						.AutoWrapText(true)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateAssetButton", "Create / Open Data Asset"))
					.OnClicked(this, &SAudioTagHelperPanel::CreateOrOpenDataAsset)
				]
			]
		]
	];

	RefreshDefinitionChoices();
}

FReply SAudioTagHelperPanel::ToggleTypeAddForm()
{
	bTypeAddFormExpanded = !bTypeAddFormExpanded;
	return FReply::Handled();
}

FReply SAudioTagHelperPanel::ToggleContextAddForm()
{
	bContextAddFormExpanded = !bContextAddFormExpanded;
	return FReply::Handled();
}

EVisibility SAudioTagHelperPanel::GetTypeAddFormVisibility() const
{
	return bTypeAddFormExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SAudioTagHelperPanel::GetContextAddFormVisibility() const
{
	return bContextAddFormExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SAudioTagHelperPanel::GetTypeAddToggleText() const
{
	return bTypeAddFormExpanded ? FText::FromString(TEXT("-")) : FText::FromString(TEXT("+"));
}

FText SAudioTagHelperPanel::GetContextAddToggleText() const
{
	return bContextAddFormExpanded ? FText::FromString(TEXT("-")) : FText::FromString(TEXT("+"));
}

bool SAudioTagHelperPanel::CreateOrReuseAudioTag(
	const FString& RootTag,
	const TSharedPtr<SEditableTextBox>& NameTextBox,
	const TSharedPtr<SEditableTextBox>& CommentTextBox,
	FGameplayTag& OutTag,
	FText& OutError) const
{
	FString RawName = NameTextBox.IsValid() ? NameTextBox->GetText().ToString() : FString();
	RawName.TrimStartAndEndInline();
	if (RawName.IsEmpty())
	{
		OutError = LOCTEXT("MissingTagNameError", "Enter a tag name below the fixed parent.");
		return false;
	}

	if (RawName.StartsWith(TEXT("Audio."), ESearchCase::IgnoreCase)
		&& !RawName.StartsWith(RootTag + TEXT("."), ESearchCase::IgnoreCase))
	{
		OutError = FText::Format(
			LOCTEXT("WrongTagRootError", "The tag must be a child of {0}."),
			FText::FromString(RootTag));
		return false;
	}

	const FString FullTagName = AudioTagHelperPanel::MakeFullAudioTag(RootTag, RawName);
	if (FullTagName.IsEmpty())
	{
		OutError = FText::Format(
			LOCTEXT("RootOnlyTagError", "Enter a child name below {0}; the parent itself cannot be added here."),
			FText::FromString(RootTag));
		return false;
	}

	FText TagValidationError;
	FString FixedTag;
	UGameplayTagsManager& GameplayTagsManager = UGameplayTagsManager::Get();
	if (!GameplayTagsManager.IsValidGameplayTagString(FullTagName, &TagValidationError, &FixedTag))
	{
		OutError = FText::Format(
			LOCTEXT("InvalidTagError", "Invalid tag: {0}\nSuggested value: {1}"),
			TagValidationError,
			FText::FromString(FixedTag));
		return false;
	}

	if (GameplayTagsManager.IsDictionaryTag(FName(*FullTagName)))
	{
		OutTag = GameplayTagsManager.RequestGameplayTag(FName(*FullTagName), false);
		if (!AudioTagHelperPanel::IsTagFromAudioTagSource(OutTag))
		{
			OutError = FText::Format(
				LOCTEXT("ExistingTagWrongSourceError", "'{0}' already exists, but it is not stored in AudioTags.ini."),
				FText::FromString(FullTagName));
			return false;
		}
		return true;
	}

	const FString Comment = CommentTextBox.IsValid() ? CommentTextBox->GetText().ToString() : FString();
	if (!IGameplayTagsEditorModule::Get().AddNewGameplayTagToINI(
		FullTagName,
		Comment,
		AudioTagHelperPanel::AudioTagSourceName))
	{
		OutError = FText::Format(
			LOCTEXT("AddTagFailedError", "Could not add '{0}' to AudioTags.ini. Check the Output Log and source-control checkout state."),
			FText::FromString(FullTagName));
		return false;
	}

	OutTag = GameplayTagsManager.RequestGameplayTag(FName(*FullTagName), false);
	if (!OutTag.IsValid())
	{
		OutError = FText::Format(
			LOCTEXT("TagRefreshFailedError", "'{0}' was written, but it is not available in the current tag tree."),
			FText::FromString(FullTagName));
		return false;
	}

	return true;
}

FReply SAudioTagHelperPanel::AddTypeTag()
{
	FGameplayTag TypeTag;
	FText Error;
	if (!CreateOrReuseAudioTag(
		AudioTagHelperPanel::AudioTypeRoot,
		NewTypeNameTextBox,
		NewTypeCommentTextBox,
		TypeTag,
		Error))
	{
		AudioTagHelperPanel::ShowResultMessage(Error);
		return FReply::Handled();
	}

	SelectedTypeTags->Reset();
	SelectedTypeTags->AddTag(TypeTag);
	NewTypeNameTextBox->SetText(FText());
	NewTypeCommentTextBox->SetText(FText());
	bTypeAddFormExpanded = false;
	TypeTagWidget->RefreshOnNextTick();
	RefreshDefinitionChoices();
	return FReply::Handled();
}

FReply SAudioTagHelperPanel::AddContextTag()
{
	FGameplayTag ContextTag;
	FText Error;
	if (!CreateOrReuseAudioTag(
		AudioTagHelperPanel::AudioContextRoot,
		NewContextNameTextBox,
		NewContextCommentTextBox,
		ContextTag,
		Error))
	{
		AudioTagHelperPanel::ShowResultMessage(Error);
		return FReply::Handled();
	}

	SelectedContextTags->AddTag(ContextTag);
	NewContextNameTextBox->SetText(FText());
	NewContextCommentTextBox->SetText(FText());
	bContextAddFormExpanded = false;
	ContextTagWidget->RefreshOnNextTick();
	return FReply::Handled();
}

FText SAudioTagHelperPanel::GetOutputPreview() const
{
	FString TypeName;
	if (SelectedTypeTags.IsValid() && SelectedTypeTags->Num() > 0)
	{
		TypeName = SelectedTypeTags->First().ToString();
	}
	else if (NewTypeNameTextBox.IsValid())
	{
		TypeName = AudioTagHelperPanel::MakeFullAudioTag(
			AudioTagHelperPanel::AudioTypeRoot,
			NewTypeNameTextBox->GetText().ToString());
	}

	TArray<FString> ContextNames;
	if (SelectedContextTags.IsValid())
	{
		for (const FGameplayTag& ContextTag : *SelectedContextTags)
		{
			ContextNames.Add(ContextTag.ToString());
		}
	}
	if (NewContextNameTextBox.IsValid())
	{
		const FString PendingContext = AudioTagHelperPanel::MakeFullAudioTag(
			AudioTagHelperPanel::AudioContextRoot,
			NewContextNameTextBox->GetText().ToString());
		if (!PendingContext.IsEmpty())
		{
			ContextNames.AddUnique(PendingContext + TEXT(" (pending Add New Tag)"));
		}
	}
	ContextNames.Sort();

	const FString RawAssetName = AssetNameTextBox.IsValid() ? AssetNameTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString AssetNamePreview = TypeName.IsEmpty()
		? TEXT("<select or add Type tag>")
		: (RawAssetName.IsEmpty()
			? TEXT("<auto-numbered DA_ in the Bank>")
			: AudioTagHelperPanel::MakeDefinitionAssetName(RawAssetName, TypeName, 0));

	FString AssetFolder = AssetFolderTextBox.IsValid()
		? AssetFolderTextBox->GetText().ToString().TrimStartAndEnd()
		: AudioTagHelperPanel::DefaultAssetFolder;
	while (AssetFolder.EndsWith(TEXT("/")))
	{
		AssetFolder.LeftChopInline(1);
	}

	const FString BankNamePreview = TypeName.IsEmpty()
		? TEXT("<select or add Type tag>")
		: AudioTagHelperPanel::MakeBankAssetName(TypeName);

	return FText::FromString(FString::Printf(
		TEXT("Output Preview\nType: %s\nBank: %s/%s (created if missing, reused otherwise)\nInitial Variant Contexts: %s\nData Asset: %s/%s\nTag Source: %s"),
		TypeName.IsEmpty() ? TEXT("<select or add Type tag>") : *TypeName,
		*AudioTagHelperPanel::DefaultBankFolder,
		*BankNamePreview,
		ContextNames.IsEmpty() ? TEXT("<none>") : *FString::Join(ContextNames, TEXT(", ")),
		*AssetFolder,
		*AssetNamePreview,
		*AudioTagHelperPanel::AudioTagSourceName.ToString()));
}

bool SAudioTagHelperPanel::ValidateDataAssetInputs(
	FGameplayTag& OutTypeTag,
	FString& OutRawAssetName,
	FString& OutAssetFolder,
	FText& OutError) const
{
	if (!SelectedTypeTags.IsValid() || SelectedTypeTags->Num() != 1)
	{
		OutError = LOCTEXT("SelectTypeError", "Select one existing Type tag or add a new Type tag first.");
		return false;
	}

	OutTypeTag = SelectedTypeTags->First();
	if (!AudioTagHelperPanel::IsTagUnderRoot(OutTypeTag, AudioTagHelperPanel::AudioTypeRoot))
	{
		OutError = LOCTEXT("InvalidSelectedTypeError", "The selected tag must be below Audio.Type.");
		return false;
	}
	if (!AudioTagHelperPanel::IsTagFromAudioTagSource(OutTypeTag))
	{
		OutError = LOCTEXT("SelectedTypeSourceError", "The selected Type must be explicitly stored in AudioTags.ini.");
		return false;
	}

	if (SelectedContextTags.IsValid())
	{
		for (const FGameplayTag& ContextTag : *SelectedContextTags)
		{
			if (!AudioTagHelperPanel::IsTagUnderRoot(ContextTag, AudioTagHelperPanel::AudioContextRoot))
			{
				OutError = FText::Format(
					LOCTEXT("InvalidSelectedContextError", "'{0}' must be below Audio.Context."),
					FText::FromString(ContextTag.ToString()));
				return false;
			}
			if (!AudioTagHelperPanel::IsTagFromAudioTagSource(ContextTag))
			{
				OutError = FText::Format(
					LOCTEXT("SelectedContextSourceError", "'{0}' must be explicitly stored in AudioTags.ini."),
					FText::FromString(ContextTag.ToString()));
				return false;
			}
		}
	}

	OutRawAssetName = AssetNameTextBox.IsValid() ? AssetNameTextBox->GetText().ToString() : FString();

	OutAssetFolder = AssetFolderTextBox.IsValid()
		? AssetFolderTextBox->GetText().ToString().TrimStartAndEnd()
		: FString();
	while (OutAssetFolder.EndsWith(TEXT("/")))
	{
		OutAssetFolder.LeftChopInline(1);
	}

	FText PackagePathError;
	if (!OutAssetFolder.StartsWith(TEXT("/Game/"))
		|| !FPackageName::IsValidLongPackageName(OutAssetFolder, false, &PackagePathError))
	{
		OutError = FText::Format(
			LOCTEXT("InvalidAssetFolderError", "Data Asset Folder must be a valid /Game path.\n{0}"),
			PackagePathError);
		return false;
	}

	return true;
}

UOutlierAudioBank* SAudioTagHelperPanel::FindBankForType(const FGameplayTag& TypeTag) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> BankAssets;
	AssetRegistryModule.Get().GetAssetsByClass(
		UOutlierAudioBank::StaticClass()->GetClassPathName(),
		BankAssets,
		true);

	for (const FAssetData& AssetData : BankAssets)
	{
		if (UOutlierAudioBank* Bank = Cast<UOutlierAudioBank>(AssetData.GetAsset()))
		{
			if (Bank->TypeTag == TypeTag)
			{
				return Bank;
			}
		}
	}

	return nullptr;
}

UOutlierAudioBank* SAudioTagHelperPanel::CreateBankForType(const FGameplayTag& TypeTag, FText& OutError) const
{
	const FString AssetName = AudioTagHelperPanel::MakeBankAssetName(TypeTag.ToString());
	const FString PackageName = AudioTagHelperPanel::DefaultBankFolder / AssetName;
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (FPackageName::DoesPackageExist(PackageName)
		|| FindObject<UObject>(nullptr, *ObjectPath) != nullptr
		|| AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
	{
		OutError = FText::Format(
			LOCTEXT("BankNameCollisionError", "An asset already exists at {0}, but it is not an Audio Bank for Type '{1}'. Rename or remove it first."),
			FText::FromString(ObjectPath),
			FText::FromString(TypeTag.ToString()));
		return nullptr;
	}

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UOutlierAudioBank::StaticClass();

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UOutlierAudioBank* NewBank = Cast<UOutlierAudioBank>(
		AssetToolsModule.Get().CreateAsset(
			AssetName,
			AudioTagHelperPanel::DefaultBankFolder,
			UOutlierAudioBank::StaticClass(),
			Factory));

	if (!NewBank)
	{
		OutError = FText::Format(
			LOCTEXT("BankCreationFailedError", "Audio Bank creation failed for Type '{0}'."),
			FText::FromString(TypeTag.ToString()));
		return nullptr;
	}

	NewBank->Modify();
	NewBank->TypeTag = TypeTag;
	NewBank->PostEditChange();
	NewBank->MarkPackageDirty();

	// 여기서 바로 저장하지 않는다 — 이 시점엔 Definitions 가 아직 비어있어 IsDataValid 가
	// "Definition 이 하나도 없다" 로 실패하고, 저장 시 데이터 검증 경고가 뜬다.
	// 호출부( CreateOrOpenDataAsset )가 Definitions 에 최소 1개를 채운 뒤 저장한다.
	return NewBank;
}

FReply SAudioTagHelperPanel::RefreshDefinitionChoices()
{
	DefinitionChoiceLabels.Reset();
	DefinitionChoiceObjects.Reset();

	// index 0: 항상 "새로 만들기" 센티널.
	DefinitionChoiceLabels.Add(MakeShared<FString>(TEXT("<Create New Definition>")));
	DefinitionChoiceObjects.Add(TSoftObjectPtr<UOutlierAudioEventDefinition>());

	if (SelectedTypeTags.IsValid() && SelectedTypeTags->Num() == 1)
	{
		if (UOutlierAudioBank* Bank = FindBankForType(SelectedTypeTags->First()))
		{
			for (const TSoftObjectPtr<UOutlierAudioEventDefinition>& DefinitionSoftPtr : Bank->Definitions)
			{
				const FString Label = DefinitionSoftPtr.IsNull()
					? TEXT("<invalid entry>")
					: DefinitionSoftPtr.GetAssetName();
				DefinitionChoiceLabels.Add(MakeShared<FString>(Label));
				DefinitionChoiceObjects.Add(DefinitionSoftPtr);
			}
		}
	}

	SelectedDefinitionChoiceLabel = DefinitionChoiceLabels[0];
	if (DefinitionChoiceCombo.IsValid())
	{
		DefinitionChoiceCombo->RefreshOptions();
		DefinitionChoiceCombo->SetSelectedItem(SelectedDefinitionChoiceLabel);
	}

	return FReply::Handled();
}

void SAudioTagHelperPanel::HandleDefinitionComboOpening()
{
	RefreshDefinitionChoices();
}

TSharedRef<SWidget> SAudioTagHelperPanel::GenerateDefinitionChoiceWidget(TSharedPtr<FString> Item) const
{
	return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
}

void SAudioTagHelperPanel::OnDefinitionChoiceSelected(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
	(void)SelectInfo;
	SelectedDefinitionChoiceLabel = Item;
}

FText SAudioTagHelperPanel::GetSelectedDefinitionChoiceText() const
{
	return FText::FromString(SelectedDefinitionChoiceLabel.IsValid()
		? *SelectedDefinitionChoiceLabel
		: TEXT("<Create New Definition>"));
}

void SAudioTagHelperPanel::OpenAsset(UObject* Asset) const
{
	if (GEditor && Asset)
	{
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorSubsystem->OpenEditorForAsset(Asset);
		}
	}
}

FReply SAudioTagHelperPanel::CreateOrOpenDataAsset()
{
	FGameplayTag TypeTag;
	FString RawAssetName;
	FString AssetFolder;
	FText ValidationError;
	if (!ValidateDataAssetInputs(
		TypeTag,
		RawAssetName,
		AssetFolder,
		ValidationError))
	{
		AudioTagHelperPanel::ShowResultMessage(ValidationError);
		return FReply::Handled();
	}

	UOutlierAudioBank* Bank = FindBankForType(TypeTag);
	const bool bCreatedBank = (Bank == nullptr);
	if (!Bank)
	{
		FText BankError;
		Bank = CreateBankForType(TypeTag, BankError);
		if (!Bank)
		{
			AudioTagHelperPanel::ShowResultMessage(BankError);
			return FReply::Handled();
		}
	}

	// "새로 만들기" 센티널이 아닌 기존 Definition 이 선택돼 있으면, 새 에셋을 만들지 않고
	// 그 Definition 에 Variant 하나만 추가한다 ( Context 개수만큼 DA 가 늘어나는 것을 피함 ).
	const int32 SelectedChoiceIndex = SelectedDefinitionChoiceLabel.IsValid()
		? DefinitionChoiceLabels.IndexOfByPredicate(
			[this](const TSharedPtr<FString>& Candidate) { return Candidate == SelectedDefinitionChoiceLabel; })
		: INDEX_NONE;

	// 콤보가 다른 Type 을 보던 시점에 채워졌을 수도 있으니, 실제로 지금 이 Bank 에 속한
	// 항목인지 다시 확인한다. 아니면( 즉, 없으면 ) 그냥 New 로 처리한다.
	const bool bChoiceBelongsToCurrentBank = SelectedChoiceIndex > 0
		&& DefinitionChoiceObjects.IsValidIndex(SelectedChoiceIndex)
		&& !DefinitionChoiceObjects[SelectedChoiceIndex].IsNull()
		&& Bank->Definitions.Contains(DefinitionChoiceObjects[SelectedChoiceIndex]);

	if (bChoiceBelongsToCurrentBank)
	{
		const FSoftObjectPath FailingPath = DefinitionChoiceObjects[SelectedChoiceIndex].ToSoftObjectPath();
		UOutlierAudioEventDefinition* ExistingDefinition = DefinitionChoiceObjects[SelectedChoiceIndex].LoadSynchronous();
		if (!ExistingDefinition)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[AudioTagHelper] Failed to load existing Definition at '%s'. Bank='%s' Type='%s'. The asset likely was never saved to disk (leftover from a broken earlier create) or its package file is missing/renamed."),
				*FailingPath.ToString(),
				*GetNameSafe(Bank),
				*TypeTag.ToString());
			AudioTagHelperPanel::ShowResultMessage(FText::Format(
				LOCTEXT("ExistingDefinitionLoadFailedError", "Failed to load '{0}'.\n\nThis usually means the asset was never actually saved to disk (a leftover from an earlier failed create). Check the Output Log for the exact path, then remove that entry from the Bank's Definitions list (or delete the stale asset) and try again."),
				FText::FromString(FailingPath.ToString())));
			return FReply::Handled();
		}

		ExistingDefinition->Modify();
		FOutlierAudioVariant& NewVariant = ExistingDefinition->Variants.AddDefaulted_GetRef();
		if (SelectedContextTags.IsValid() && SelectedContextTags->Num() > 0)
		{
			NewVariant.RequiredContext = SelectedContextTags->First();
		}
		NewVariant.Weight = 1.0f;
		ExistingDefinition->PostEditChange();
		ExistingDefinition->MarkPackageDirty();

		bool bVariantSaved = false;
		if (GEditor)
		{
			if (UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
			{
				bVariantSaved = EditorAssetSubsystem->SaveLoadedAsset(ExistingDefinition, false);
			}
		}

		OpenAsset(ExistingDefinition);

		AudioTagHelperPanel::ShowResultMessage(FText::Format(
			bVariantSaved
				? LOCTEXT("VariantAddedSuccessMessage", "Added a new Variant to existing Data Asset '{0}' (Type '{1}').\n\nAssign Sound in the opened Data Asset.")
				: LOCTEXT("VariantAddedUnsavedMessage", "Added a new Variant to existing Data Asset '{0}' (Type '{1}'), but automatic saving failed. Save it manually."),
			FText::FromString(GetNameSafe(ExistingDefinition)),
			FText::FromString(TypeTag.ToString())));

		return FReply::Handled();
	}

	const FString AssetName = AudioTagHelperPanel::MakeDefinitionAssetName(
		RawAssetName,
		TypeTag.ToString(),
		Bank->Definitions.Num());
	if (AssetName.IsEmpty() || AssetName == TEXT("DA_"))
	{
		AudioTagHelperPanel::ShowResultMessage(LOCTEXT("InvalidAssetNameError", "Enter a valid Data Asset name."));
		return FReply::Handled();
	}

	const FString PackageName = AssetFolder / AssetName;
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (FPackageName::DoesPackageExist(PackageName)
		|| FindObject<UObject>(nullptr, *ObjectPath) != nullptr
		|| AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
	{
		AudioTagHelperPanel::ShowResultMessage(FText::Format(
			LOCTEXT("AssetAlreadyExistsError", "An asset already exists at {0}. Choose a different name or folder."),
			FText::FromString(ObjectPath)));
		return FReply::Handled();
	}

	UDataAssetFactory* DefinitionFactory = NewObject<UDataAssetFactory>();
	DefinitionFactory->DataAssetClass = UOutlierAudioEventDefinition::StaticClass();

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UOutlierAudioEventDefinition* NewDefinition = Cast<UOutlierAudioEventDefinition>(
		AssetToolsModule.Get().CreateAsset(
			AssetName,
			AssetFolder,
			UOutlierAudioEventDefinition::StaticClass(),
			DefinitionFactory));

	if (!NewDefinition)
	{
		AudioTagHelperPanel::ShowResultMessage(FText::Format(
			LOCTEXT("AssetCreationFailedMessage", "Data Asset creation failed for Type '{0}'. Check the asset path and source-control state."),
			FText::FromString(TypeTag.ToString())));
		return FReply::Handled();
	}

	NewDefinition->Modify();
	NewDefinition->VolumeMultiplier = 1.0f;
	NewDefinition->PitchMultiplier = 1.0f;
	FOutlierAudioVariant& InitialVariant = NewDefinition->Variants.AddDefaulted_GetRef();
	if (SelectedContextTags.IsValid() && SelectedContextTags->Num() > 0)
	{
		InitialVariant.RequiredContext = SelectedContextTags->First();
	}
	InitialVariant.Weight = 1.0f;
	NewDefinition->PostEditChange();
	NewDefinition->MarkPackageDirty();

	bool bDefinitionSaved = false;
	if (GEditor)
	{
		if (UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
		{
			bDefinitionSaved = EditorAssetSubsystem->SaveLoadedAsset(NewDefinition, false);
		}
	}

	Bank->Modify();
	Bank->Definitions.Add(NewDefinition);
	Bank->PostEditChange();
	Bank->MarkPackageDirty();

	bool bBankSaved = false;
	if (GEditor)
	{
		if (UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
		{
			bBankSaved = EditorAssetSubsystem->SaveLoadedAsset(Bank, false);
		}
	}

	OpenAsset(NewDefinition);

	const FText BankWord = bCreatedBank
		? LOCTEXT("CreatedWord", "Created")
		: LOCTEXT("ReusedWord", "Reused existing");

	AudioTagHelperPanel::ShowResultMessage(FText::Format(
		(bDefinitionSaved && bBankSaved)
			? LOCTEXT("CreationSuccessMessage", "{0} Bank '{1}' for Type '{2}', and added Data Asset '{3}' to it.\n\nAssign Sound in the opened Data Asset.")
			: LOCTEXT("CreationUnsavedMessage", "{0} Bank '{1}' for Type '{2}', and added Data Asset '{3}' to it, but automatic saving failed for at least one asset. Save both manually."),
		BankWord,
		FText::FromString(Bank->GetName()),
		FText::FromString(TypeTag.ToString()),
		FText::FromString(AssetFolder / AssetName)));

	RefreshDefinitionChoices();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
