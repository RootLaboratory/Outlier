#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"

class FSlateTextLayout;
class FSlateTextBlockLayout;
class UTextBlock;
struct FWidgetTransform;

/** Uses the same shaping and line-breaking path as STextBlock, rather than measuring the entire string. */
class FBottomTTSSpeakerLayout
{
public:
	FBottomTTSSpeakerLayout();
	~FBottomTTSSpeakerLayout();

	bool GetFirstLineOffset(const UTextBlock& Text, const FGeometry& Geometry, FVector2D& OutOffset);

	/** Arranges the existing tree now; never uses previous-frame UWidget geometry or label width. */
	bool AlignSpeakerInTree(UTextBlock& Speaker, const UTextBlock& Text,
		const TSharedRef<SWidget>& Root, const FGeometry& RootGeometry, float Gap);

	static bool FindWidgetGeometry(const TSharedRef<SWidget>& Root, const FGeometry& RootGeometry,
		const SWidget* Target, FGeometry& OutGeometry, FGeometry& OutParentGeometry);

	/** Computes an absolute render translation, independent of the translation used by the previous frame. */
	static FVector2D GetSpeakerTranslation(const FGeometry& ScriptGeometry, const FVector2D& FirstLineOffset,
		const FGeometry& SpeakerGeometry, const FGeometry& SpeakerParentGeometry,
		const FWidgetTransform& SpeakerTransform, const FVector2D& SpeakerPivot, float Gap);

private:
	TSharedPtr<FSlateTextLayout> TextLayout;
	TUniquePtr<FSlateTextBlockLayout> LayoutCache;
	TWeakPtr<SWidget> Owner;
};
