#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Notifications/SProgressBar.h"

class UGhostProgressBar;

/** UMG 위젯은 하나로 유지하면서 기존 Slate ProgressBar의 Mask/Scale 그리기를 재사용한다. */
class SGhostProgressBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGhostProgressBar) {}
		SLATE_ARGUMENT(UGhostProgressBar*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SProgressBar> GetMainBar() const { return MainBar; }
	void RefreshAppearance();
	void SetGhostPercent(float InPercent);
	void StartAnimation();
	void StopAnimation();

private:
	EActiveTimerReturnType AdvanceAnimation(double CurrentTime, float DeltaTime);
	bool IsVisibleInHierarchy() const;

	TWeakObjectPtr<UGhostProgressBar> Owner;
	TSharedPtr<SProgressBar> MainBar;
	TSharedPtr<SProgressBar> GhostBar;
	TWeakPtr<FActiveTimerHandle> AnimationTimer;
	FSlateBrush NoBackground;
	FProgressBarStyle StaticStyle;
	bool bInAnimationCallback = false;
};
