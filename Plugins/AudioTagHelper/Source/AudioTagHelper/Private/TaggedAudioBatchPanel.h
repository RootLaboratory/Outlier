#pragma once

#include "TaggedAudioBatch.h"
#include "TaggedAudioBatchSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SMultiLineEditableTextBox;

class STaggedAudioBatchPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STaggedAudioBatchPanel) {} SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);

private:
	FReply PreviewSelection();
	FReply ApplyPreview();
	void InvalidatePreview();
	TStrongObjectPtr<UTaggedAudioBatchSettings> Settings;
	TArray<TWeakObjectPtr<UAnimSequenceBase>> PreviewTargets;
	TArray<FTaggedAudioBatchRow> PreviewRows;
	TSharedPtr<SMultiLineEditableTextBox> PreviewText;
	bool bHasPreview = false;
};
