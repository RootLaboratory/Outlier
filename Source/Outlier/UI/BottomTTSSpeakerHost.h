#pragma once

#include "CoreMinimal.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_TwoParams(FOnArrangeBottomTTS, const TSharedRef<SWidget>&, const FGeometry&);

/** Positions the speaker using this frame's allocation before any subtitle child is painted. */
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
	// Keep this allocation pass active under Slate invalidation, including when the viewport changes.
	virtual bool ComputeVolatility() const override { return true; }

private:
	FOnArrangeBottomTTS OnArrangeSubtitle;
};
