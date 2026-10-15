#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"

class FSlateTextLayout;
class FSlateTextBlockLayout;
class UTextBlock;
struct FWidgetTransform;

/** 문자열 전체를 측정하지 않고 STextBlock과 같은 shaping/line-breaking 경로를 사용한다. */
class FBottomTTSSpeakerLayout
{
public:
	FBottomTTSSpeakerLayout();
	~FBottomTTSSpeakerLayout();

	bool GetFirstLineOffset(const UTextBlock& Text, const FGeometry& Geometry, FVector2D& OutOffset);

	/** 기존 트리를 즉시 배치한다. 이전 프레임의 UWidget geometry나 라벨 폭은 사용하지 않는다. */
	bool AlignSpeakerInTree(UTextBlock& Speaker, const UTextBlock& Text,
		const TSharedRef<SWidget>& Root, const FGeometry& RootGeometry, float Gap);

	static bool FindWidgetGeometry(const TSharedRef<SWidget>& Root, const FGeometry& RootGeometry,
		const SWidget* Target, FGeometry& OutGeometry, FGeometry& OutParentGeometry);

	/** 이전 프레임의 translation과 무관하게 절대 render translation을 계산한다. */
	static FVector2D GetSpeakerTranslation(const FGeometry& ScriptGeometry, const FVector2D& FirstLineOffset,
		const FGeometry& SpeakerGeometry, const FGeometry& SpeakerParentGeometry,
		const FWidgetTransform& SpeakerTransform, const FVector2D& SpeakerPivot, float Gap);

private:
	TSharedPtr<FSlateTextLayout> TextLayout;
	TUniquePtr<FSlateTextBlockLayout> LayoutCache;
	TWeakPtr<SWidget> Owner;
};
