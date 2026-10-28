#include "TaggedAudioBatchPanel.h"

#include "TaggedAudioBatchSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void STaggedAudioBatchPanel::Construct(const FArguments& InArgs)
{
	Settings.Reset(NewObject<UTaggedAudioBatchSettings>());
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	auto Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
	Details->SetObject(Settings.Get());
	Details->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&) { InvalidatePreview(); });
	ChildSlot
	[
		SNew(SBorder).Padding(10.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Tagged Audio 노티파이 일괄 배치")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT(
					"Content Browser에서 대상 Sequence/Montage를 여러 개 선택하세요. 발걸음은 기본 설정으로 3P에 배치합니다. "
					"재장전은 Query Surface를 끄고 메시 정책을 설정하세요. 점프·착지·발사·슬라이딩은 상태 코드가 처리합니다. "
					"기존 AI 소음·탄창 처리·카메라 Notify는 변경하지 않습니다. 복사는 TaggedAudio 설정과 시점만 사용합니다.")))
			]
			+ SVerticalBox::Slot().AutoHeight()[Details]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(FText::FromString(TEXT("선택 대상 미리보기"))).OnClicked(this, &STaggedAudioBatchPanel::PreviewSelection)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
				[SNew(SButton).Text(FText::FromString(TEXT("미리본 항목 적용 (Undo 가능)")))
					.IsEnabled_Lambda([this] { return bHasPreview; }).OnClicked(this, &STaggedAudioBatchPanel::ApplyPreview)]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(220)
				[SAssignNew(PreviewText, SMultiLineEditableTextBox).IsReadOnly(true)
					.Text(FText::FromString(TEXT("미리보기를 생성하면 대상·시간·추가/건너뜀 사유를 표시합니다. 자동 저장하지 않습니다.")))]
			]
		]
	];
}

void STaggedAudioBatchPanel::InvalidatePreview()
{
	bHasPreview = false;
	PreviewRows.Reset();
	PreviewTargets.Reset();
	if (PreviewText) PreviewText->SetText(FText::FromString(TEXT("설정이 변경되었습니다. 선택 대상 미리보기를 다시 생성하세요.")));
}

FReply STaggedAudioBatchPanel::PreviewSelection()
{
	TArray<FAssetData> SelectedAssets;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(SelectedAssets);
	TArray<UAnimSequenceBase*> Targets;
	for (const FAssetData& Asset : SelectedAssets)
	{
		// 선택한 에셋만 로드한다. 애니메이션 전체 검색이나 런타임 탐색은 하지 않는다.
		if (!Asset.IsInstanceOf(UAnimSequence::StaticClass()) && !Asset.IsInstanceOf(UAnimMontage::StaticClass())) continue;
		if (auto* Target = Cast<UAnimSequenceBase>(Asset.GetAsset())) Targets.Add(Target);
	}
	PreviewTargets.Reset();
	for (UAnimSequenceBase* Target : Targets) PreviewTargets.Add(Target);
	PreviewRows = TaggedAudioBatch::Preview(*Settings, Targets);
	PreviewText->SetText(FText::FromString(TaggedAudioBatch::Describe(PreviewRows)));
	bHasPreview = !PreviewRows.IsEmpty();
	return FReply::Handled();
}

FReply STaggedAudioBatchPanel::ApplyPreview()
{
	TArray<UAnimSequenceBase*> Targets;
	for (auto Target : PreviewTargets) if (Target.IsValid()) Targets.Add(Target.Get());
	TArray<FTaggedAudioBatchRow> CurrentRows = TaggedAudioBatch::Preview(*Settings, Targets);
	// 미리보기 이후 에셋이 편집됐으면 곧바로 적용하지 않고 새 결과를 먼저 보여준다.
	if (TaggedAudioBatch::Describe(CurrentRows) != TaggedAudioBatch::Describe(PreviewRows))
	{
		PreviewRows = MoveTemp(CurrentRows);
		PreviewText->SetText(FText::FromString(TEXT("대상 또는 기준 애니메이션이 변경되었습니다. 아래 결과를 확인한 뒤 다시 적용하세요.\n")
			+ TaggedAudioBatch::Describe(PreviewRows)));
		return FReply::Handled();
	}
	const int32 Added = TaggedAudioBatch::Apply(*Settings, CurrentRows);
	PreviewText->SetText(FText::FromString(FString::Printf(TEXT("%d개 추가했습니다. Ctrl+Z로 되돌릴 수 있습니다. 변경된 애니메이션은 확인 후 저장하세요.\n"), Added)
		+ TaggedAudioBatch::Describe(CurrentRows)));
	bHasPreview = false;
	return FReply::Handled();
}
