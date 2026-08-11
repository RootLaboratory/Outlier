#pragma once

#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SGameplayTagWidget;
class UOutlierAudioEventDefinition;

class SAudioTagHelperPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAudioTagHelperPanel)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply ToggleEventAddForm();
	FReply ToggleContextAddForm();
	FReply AddEventTag();
	FReply AddContextTag();
	FReply CreateOrOpenDataAsset();

	EVisibility GetEventAddFormVisibility() const;
	EVisibility GetContextAddFormVisibility() const;
	FText GetEventAddToggleText() const;
	FText GetContextAddToggleText() const;
	FText GetOutputPreview() const;

	bool CreateOrReuseAudioTag(
		const FString& RootTag,
		const TSharedPtr<SEditableTextBox>& NameTextBox,
		const TSharedPtr<SEditableTextBox>& CommentTextBox,
		FGameplayTag& OutTag,
		FText& OutError) const;

	bool ValidateDataAssetInputs(
		FGameplayTag& OutEventTag,
		FString& OutAssetName,
		FString& OutAssetFolder,
		FText& OutError) const;

	UOutlierAudioEventDefinition* FindDefinitionForEvent(const FGameplayTag& EventTag) const;
	void OpenDefinition(UOutlierAudioEventDefinition* Definition) const;

	TSharedPtr<FGameplayTagContainer> SelectedEventTags;
	TSharedPtr<FGameplayTagContainer> SelectedContextTags;
	TSharedPtr<SGameplayTagWidget> EventTagWidget;
	TSharedPtr<SGameplayTagWidget> ContextTagWidget;

	TSharedPtr<SEditableTextBox> NewEventNameTextBox;
	TSharedPtr<SEditableTextBox> NewEventCommentTextBox;
	TSharedPtr<SEditableTextBox> NewContextNameTextBox;
	TSharedPtr<SEditableTextBox> NewContextCommentTextBox;
	TSharedPtr<SEditableTextBox> AssetNameTextBox;
	TSharedPtr<SEditableTextBox> AssetFolderTextBox;

	bool bEventAddFormExpanded = false;
	bool bContextAddFormExpanded = false;
};
