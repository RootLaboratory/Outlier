#pragma once

#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SGameplayTagWidget;
class UOutlierAudioBank;
class UOutlierAudioEventDefinition;
template <typename OptionType> class SComboBox;

class SAudioTagHelperPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAudioTagHelperPanel)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply ToggleTypeAddForm();
	FReply ToggleContextAddForm();
	FReply AddTypeTag();
	FReply AddContextTag();
	FReply CreateOrOpenDataAsset();

	EVisibility GetTypeAddFormVisibility() const;
	EVisibility GetContextAddFormVisibility() const;
	FText GetTypeAddToggleText() const;
	FText GetContextAddToggleText() const;
	FText GetOutputPreview() const;

	bool CreateOrReuseAudioTag(
		const FString& RootTag,
		const TSharedPtr<SEditableTextBox>& NameTextBox,
		const TSharedPtr<SEditableTextBox>& CommentTextBox,
		FGameplayTag& OutTag,
		FText& OutError) const;

	// Type 선택/Context 선택/에셋 폴더까지만 검증한다. 실제 Definition 이름은 Bank 를
	// 찾거나 만든 뒤(그 안의 기존 개수를 알아야 자동 넘버링이 가능하므로) 별도로 확정한다.
	bool ValidateDataAssetInputs(
		FGameplayTag& OutTypeTag,
		FString& OutRawAssetName,
		FString& OutAssetFolder,
		FText& OutError) const;

	UOutlierAudioBank* FindBankForType(const FGameplayTag& TypeTag) const;
	UOutlierAudioBank* CreateBankForType(const FGameplayTag& TypeTag, FText& OutError) const;
	void OpenAsset(UObject* Asset) const;

	// 선택된 Type 이 속한 Bank 의 기존 Definition 목록을 다시 읽어 콤보를 채운다.
	// index 0 은 항상 "<Create New Definition>" 센티널이다.
	FReply RefreshDefinitionChoices();
	// 콤보를 열 때마다 호출된다 ( SGameplayTagWidget 의 선택 변경 콜백을 신뢰할 수 없어
	// 대신 여기서 매번 다시 채워, 다른 Type 의 stale 목록이 보이는 일을 막는다 ).
	void HandleDefinitionComboOpening();
	TSharedRef<class SWidget> GenerateDefinitionChoiceWidget(TSharedPtr<FString> Item) const;
	void OnDefinitionChoiceSelected(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);
	FText GetSelectedDefinitionChoiceText() const;

	TSharedPtr<FGameplayTagContainer> SelectedTypeTags;
	TSharedPtr<FGameplayTagContainer> SelectedContextTags;
	TSharedPtr<SGameplayTagWidget> TypeTagWidget;
	TSharedPtr<SGameplayTagWidget> ContextTagWidget;

	TSharedPtr<SEditableTextBox> NewTypeNameTextBox;
	TSharedPtr<SEditableTextBox> NewTypeCommentTextBox;
	TSharedPtr<SEditableTextBox> NewContextNameTextBox;
	TSharedPtr<SEditableTextBox> NewContextCommentTextBox;
	TSharedPtr<SEditableTextBox> AssetNameTextBox;
	TSharedPtr<SEditableTextBox> AssetFolderTextBox;

	// index 0 은 항상 "<Create New Definition>" 센티널이고, 이후는 DefinitionChoiceObjects 와 1:1 대응.
	TArray<TSharedPtr<FString>> DefinitionChoiceLabels;
	TArray<TSoftObjectPtr<UOutlierAudioEventDefinition>> DefinitionChoiceObjects;
	TSharedPtr<FString> SelectedDefinitionChoiceLabel;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> DefinitionChoiceCombo;

	bool bTypeAddFormExpanded = false;
	bool bContextAddFormExpanded = false;
};
