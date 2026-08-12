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
	const FString AudioEventRoot(TEXT("Audio.Event"));
	const FString AudioContextRoot(TEXT("Audio.Context"));
	const FString DefaultAssetFolder(TEXT("/Game/Audio/Definitions"));

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

	FString MakeAssetName(const FString& RawAssetName, const FString& FullEventTag)
	{
		FString BaseName = RawAssetName;
		BaseName.TrimStartAndEndInline();

		if (BaseName.IsEmpty())
		{
			BaseName = FullEventTag;
			BaseName.RemoveFromStart(AudioEventRoot + TEXT("."));
			BaseName.ReplaceInline(TEXT("."), TEXT("_"));
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
	SelectedEventTags = MakeShared<FGameplayTagContainer>();
	SelectedContextTags = MakeShared<FGameplayTagContainer>();

	TArray<SGameplayTagWidget::FEditableGameplayTagContainerDatum> EventTagContainers;
	EventTagContainers.Emplace(nullptr, SelectedEventTags.Get());
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
					.Text(LOCTEXT("Summary", "Select existing audio tags or add new Event and Context children. Playback and network routing are selected by native call sites."))
					.AutoWrapText(true)
				]

				// Existing Event selection
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ExistingEventLabel", "Existing Event Tag"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SAssignNew(EventTagWidget, SGameplayTagWidget, EventTagContainers)
					.Filter(AudioTagHelperPanel::AudioEventRoot)
					.TagContainerName(TEXT("AudioTagHelper.Event"))
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
					.OnClicked(this, &SAudioTagHelperPanel::ToggleEventAddForm)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(this, &SAudioTagHelperPanel::GetEventAddToggleText)
							.ColorAndOpacity(FLinearColor(0.35f, 1.0f, 0.15f))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AddEventToggleLabel", "Add Event Tag"))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(SGridPanel)
					.Visibility(this, &SAudioTagHelperPanel::GetEventAddFormVisibility)
					.FillColumn(1, 1.0f)
					+ SGridPanel::Slot(0, 0)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("EventNameLabel", "Name:"))
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
							SNew(STextBlock).Text(LOCTEXT("EventParentPrefix", "Audio.Event."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(NewEventNameTextBox, SEditableTextBox)
							.HintText(LOCTEXT("EventNameHint", "Shooter.Fire"))
						]
					]
					+ SGridPanel::Slot(0, 1)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("EventCommentLabel", "Comment:"))
					]
					+ SGridPanel::Slot(1, 1)
					.Padding(2.0f)
					[
						SAssignNew(NewEventCommentTextBox, SEditableTextBox)
						.HintText(LOCTEXT("EventCommentHint", "Comment"))
					]
					+ SGridPanel::Slot(0, 2)
					.Padding(2.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("EventSourceLabel", "Source:"))
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
						.Text(LOCTEXT("AddEventButton", "Add New Tag"))
						.OnClicked(this, &SAudioTagHelperPanel::AddEventTag)
					]
				]

				// Existing Context selection
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ExistingContextLabel", "Existing Context Tags for Initial Variant"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SAssignNew(ContextTagWidget, SGameplayTagWidget, ContextTagContainers)
					.Filter(AudioTagHelperPanel::AudioContextRoot)
					.TagContainerName(TEXT("AudioTagHelper.Context"))
					.MultiSelect(true)
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
					.Text(LOCTEXT("AssetNameLabel", "Data Asset Name (DA_ is added automatically)"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SAssignNew(AssetNameTextBox, SEditableTextBox)
					.HintText(LOCTEXT("AssetNameHint", "Empty uses the selected Event path"))
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
}

FReply SAudioTagHelperPanel::ToggleEventAddForm()
{
	bEventAddFormExpanded = !bEventAddFormExpanded;
	return FReply::Handled();
}

FReply SAudioTagHelperPanel::ToggleContextAddForm()
{
	bContextAddFormExpanded = !bContextAddFormExpanded;
	return FReply::Handled();
}

EVisibility SAudioTagHelperPanel::GetEventAddFormVisibility() const
{
	return bEventAddFormExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SAudioTagHelperPanel::GetContextAddFormVisibility() const
{
	return bContextAddFormExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SAudioTagHelperPanel::GetEventAddToggleText() const
{
	return bEventAddFormExpanded ? FText::FromString(TEXT("-")) : FText::FromString(TEXT("+"));
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

FReply SAudioTagHelperPanel::AddEventTag()
{
	FGameplayTag EventTag;
	FText Error;
	if (!CreateOrReuseAudioTag(
		AudioTagHelperPanel::AudioEventRoot,
		NewEventNameTextBox,
		NewEventCommentTextBox,
		EventTag,
		Error))
	{
		AudioTagHelperPanel::ShowResultMessage(Error);
		return FReply::Handled();
	}

	SelectedEventTags->Reset();
	SelectedEventTags->AddTag(EventTag);
	NewEventNameTextBox->SetText(FText());
	NewEventCommentTextBox->SetText(FText());
	bEventAddFormExpanded = false;
	EventTagWidget->RefreshOnNextTick();
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
	FString EventName;
	if (SelectedEventTags.IsValid() && SelectedEventTags->Num() > 0)
	{
		EventName = SelectedEventTags->First().ToString();
	}
	else if (NewEventNameTextBox.IsValid())
	{
		EventName = AudioTagHelperPanel::MakeFullAudioTag(
			AudioTagHelperPanel::AudioEventRoot,
			NewEventNameTextBox->GetText().ToString());
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

	const FString RawAssetName = AssetNameTextBox.IsValid() ? AssetNameTextBox->GetText().ToString() : FString();
	const FString AssetName = EventName.IsEmpty() && RawAssetName.TrimStartAndEnd().IsEmpty()
		? TEXT("<select or add Event tag>")
		: AudioTagHelperPanel::MakeAssetName(RawAssetName, EventName);
	FString AssetFolder = AssetFolderTextBox.IsValid()
		? AssetFolderTextBox->GetText().ToString().TrimStartAndEnd()
		: AudioTagHelperPanel::DefaultAssetFolder;
	while (AssetFolder.EndsWith(TEXT("/")))
	{
		AssetFolder.LeftChopInline(1);
	}

	return FText::FromString(FString::Printf(
		TEXT("Output Preview\nEvent: %s\nInitial Variant Contexts: %s\nData Asset: %s/%s\nTag Source: %s"),
		EventName.IsEmpty() ? TEXT("<select or add Event tag>") : *EventName,
		ContextNames.IsEmpty() ? TEXT("<none>") : *FString::Join(ContextNames, TEXT(", ")),
		*AssetFolder,
		*AssetName,
		*AudioTagHelperPanel::AudioTagSourceName.ToString()));
}

bool SAudioTagHelperPanel::ValidateDataAssetInputs(
	FGameplayTag& OutEventTag,
	FString& OutAssetName,
	FString& OutAssetFolder,
	FText& OutError) const
{
	if (!SelectedEventTags.IsValid() || SelectedEventTags->Num() != 1)
	{
		OutError = LOCTEXT("SelectEventError", "Select one existing Event tag or add a new Event tag first.");
		return false;
	}

	OutEventTag = SelectedEventTags->First();
	if (!AudioTagHelperPanel::IsTagUnderRoot(OutEventTag, AudioTagHelperPanel::AudioEventRoot))
	{
		OutError = LOCTEXT("InvalidSelectedEventError", "The selected Event must be below Audio.Event.");
		return false;
	}
	if (!AudioTagHelperPanel::IsTagFromAudioTagSource(OutEventTag))
	{
		OutError = LOCTEXT("SelectedEventSourceError", "The selected Event must be explicitly stored in AudioTags.ini.");
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

	const FString RawAssetName = AssetNameTextBox.IsValid() ? AssetNameTextBox->GetText().ToString() : FString();
	OutAssetName = AudioTagHelperPanel::MakeAssetName(RawAssetName, OutEventTag.ToString());
	if (OutAssetName.IsEmpty() || OutAssetName == TEXT("DA_"))
	{
		OutError = LOCTEXT("InvalidAssetNameError", "Enter a valid Data Asset name.");
		return false;
	}

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

UOutlierAudioEventDefinition* SAudioTagHelperPanel::FindDefinitionForEvent(const FGameplayTag& EventTag) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> DefinitionAssets;
	AssetRegistryModule.Get().GetAssetsByClass(
		UOutlierAudioEventDefinition::StaticClass()->GetClassPathName(),
		DefinitionAssets,
		true);

	for (const FAssetData& AssetData : DefinitionAssets)
	{
		if (UOutlierAudioEventDefinition* Definition = Cast<UOutlierAudioEventDefinition>(AssetData.GetAsset()))
		{
			if (Definition->EventTag == EventTag)
			{
				return Definition;
			}
		}
	}

	return nullptr;
}

void SAudioTagHelperPanel::OpenDefinition(UOutlierAudioEventDefinition* Definition) const
{
	if (GEditor && Definition)
	{
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorSubsystem->OpenEditorForAsset(Definition);
		}
	}
}

FReply SAudioTagHelperPanel::CreateOrOpenDataAsset()
{
	FGameplayTag EventTag;
	FString AssetName;
	FString AssetFolder;
	FText ValidationError;
	if (!ValidateDataAssetInputs(
		EventTag,
		AssetName,
		AssetFolder,
		ValidationError))
	{
		AudioTagHelperPanel::ShowResultMessage(ValidationError);
		return FReply::Handled();
	}

	if (UOutlierAudioEventDefinition* ExistingDefinition = FindDefinitionForEvent(EventTag))
	{
		OpenDefinition(ExistingDefinition);
		AudioTagHelperPanel::ShowResultMessage(FText::Format(
			LOCTEXT("ExistingDefinitionMessage", "'{0}' already has a Data Asset. The existing asset was opened instead."),
			FText::FromString(EventTag.ToString())));
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

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UOutlierAudioEventDefinition::StaticClass();

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UOutlierAudioEventDefinition* NewDefinition = Cast<UOutlierAudioEventDefinition>(
		AssetToolsModule.Get().CreateAsset(
			AssetName,
			AssetFolder,
			UOutlierAudioEventDefinition::StaticClass(),
			Factory));

	if (!NewDefinition)
	{
		AudioTagHelperPanel::ShowResultMessage(FText::Format(
			LOCTEXT("AssetCreationFailedMessage", "Data Asset creation failed for Event '{0}'. Check the asset path and source-control state."),
			FText::FromString(EventTag.ToString())));
		return FReply::Handled();
	}

	NewDefinition->Modify();
	NewDefinition->EventTag = EventTag;
	NewDefinition->VolumeMultiplier = 1.0f;
	NewDefinition->PitchMultiplier = 1.0f;
	FOutlierAudioVariant& InitialVariant = NewDefinition->Variants.AddDefaulted_GetRef();
	if (SelectedContextTags.IsValid())
	{
		InitialVariant.RequiredContextTags = *SelectedContextTags;
	}
	InitialVariant.Weight = 1.0f;
	NewDefinition->PostEditChange();
	NewDefinition->MarkPackageDirty();

	bool bSaved = false;
	if (GEditor)
	{
		if (UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
		{
			bSaved = EditorAssetSubsystem->SaveLoadedAsset(NewDefinition, false);
		}
	}

	OpenDefinition(NewDefinition);

	AudioTagHelperPanel::ShowResultMessage(FText::Format(
		bSaved
			? LOCTEXT("CreationSuccessMessage", "Created Data Asset '{0}' for Event '{1}'.\n\nAssign Sound in the opened Data Asset.")
			: LOCTEXT("CreationUnsavedMessage", "Created Data Asset '{0}' for Event '{1}', but automatic saving failed. Save the opened asset manually."),
		FText::FromString(AssetFolder / AssetName),
		FText::FromString(EventTag.ToString())));

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
