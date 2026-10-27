#include "SGhostProgressBar.h"

#include "GhostProgressBar.h"
#include "Brushes/SlateNoResource.h"
#include "Widgets/SOverlay.h"

void SGhostProgressBar::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	NoBackground = FSlateNoResource();
	UGhostProgressBar* Bar = Owner.Get();
	check(Bar);
	StaticStyle = Bar->GetWidgetStyle();
	StaticStyle.SetEnableFillAnimation(false);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(GhostBar, SProgressBar)
			.Style(&StaticStyle)
			.Percent(0.0f)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(MainBar, SProgressBar)
			.Style(&StaticStyle)
			.BackgroundImage(&NoBackground)
			.Percent(0.0f)
		]
	];
	RefreshAppearance();
}

void SGhostProgressBar::RefreshAppearance()
{
	const UGhostProgressBar* Bar = Owner.Get();
	if (!Bar) return;

	// 배경은 뒤쪽 바에서 한 번만 그린다. 앞쪽 바는 Main Fill만 그린다.
	// SProgressBar 자체의 Fill 애니메이션 타이머는 끈다. 잔상 보간 중에만 전용 타이머를 사용한다.
	StaticStyle = Bar->GetWidgetStyle();
	StaticStyle.SetEnableFillAnimation(false);
	GhostBar->SetStyle(&StaticStyle);
	MainBar->SetStyle(&StaticStyle);
	GhostBar->SetFillImage(Bar->GhostFillBrush.GetResourceObject()
		? &Bar->GhostFillBrush : &Bar->GetWidgetStyle().FillImage);
	GhostBar->SetFillColorAndOpacity(Bar->bEnableGhost ? Bar->GhostFillColorAndOpacity : FLinearColor::Transparent);
	GhostBar->SetBarFillType(Bar->GetBarFillType());
	GhostBar->SetBarFillStyle(Bar->GetBarFillStyle());
	GhostBar->SetBorderPadding(Bar->GetBorderPadding());
	MainBar->SetBackgroundImage(&NoBackground);
}

void SGhostProgressBar::SetGhostPercent(float InPercent)
{
	GhostBar->SetPercent(InPercent);
}

void SGhostProgressBar::StartAnimation()
{
	if (!AnimationTimer.IsValid())
	{
		AnimationTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SGhostProgressBar::AdvanceAnimation));
	}
}

void SGhostProgressBar::StopAnimation()
{
	// 콜백 안에서는 반환값 Stop으로 해제한다. 실행 중인 핸들을 중복 해제하지 않는다.
	if (!bInAnimationCallback)
	{
		if (TSharedPtr<FActiveTimerHandle> Timer = AnimationTimer.Pin())
		{
			UnRegisterActiveTimer(Timer.ToSharedRef());
		}
		AnimationTimer.Reset();
	}
}

bool SGhostProgressBar::IsVisibleInHierarchy() const
{
	if (!GetVisibility().IsVisible()) return false;
	// ActiveTimer는 숨겨진 조상 아래에서도 호출될 수 있으므로 Slate 부모의 표시 상태를 확인한다.
	for (TSharedPtr<SWidget> Parent = GetParentWidget(); Parent.IsValid(); Parent = Parent->GetParentWidget())
	{
		if (!Parent->GetVisibility().IsVisible()) return false;
	}
	return true;
}

EActiveTimerReturnType SGhostProgressBar::AdvanceAnimation(double CurrentTime, float DeltaTime)
{
	UGhostProgressBar* Bar = Owner.Get();
	if (!Bar)
	{
		AnimationTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}

	bInAnimationCallback = true;
	if (!IsVisibleInHierarchy()) Bar->ResetGhostToCurrent();
	else Bar->TickGhostFromGameTime();
	bInAnimationCallback = false;
	if (!Bar->IsGhostAnimating())
	{
		AnimationTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}
