#pragma once

#include "CoreMinimal.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_TwoParams(FOnArrangeBottomTTS, const TSharedRef<SWidget>&, const FGeometry&);

/** 자막 child가 그려지기 전에 이번 프레임의 allocation으로 speaker 위치를 잡는다. */
class SBottomTTSSpeakerHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBottomTTSSpeakerHost) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnArrangeBottomTTS, OnArrangeSubtitle)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		OnArrangeSubtitle = Args._OnArrangeSubtitle;
		ChildSlot[Args._Content.Widget];
	}

	virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Children) const override
	{
		SCompoundWidget::OnArrangeChildren(Geometry, Children);
		if (Children.Num() > 0)
		{
			OnArrangeSubtitle.ExecuteIfBound(Children[0].Widget, Children[0].Geometry);
		}
	}

protected:
	// viewport 변경을 포함해 Slate invalidation 중에도 이 allocation pass를 유지한다.
	virtual bool ComputeVolatility() const override { return true; }

private:
	FOnArrangeBottomTTS OnArrangeSubtitle;
};
