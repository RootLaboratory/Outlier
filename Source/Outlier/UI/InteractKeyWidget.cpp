#include "UI/InteractKeyWidget.h"

#include "UI/InputActionKeyDisplayWidget.h"

void UInteractKeyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (KeyDisplay)
	{
		// 생성 직후 조회 결과도 전달되도록 IA 지정 전에 구독한다.
		KeyDisplay->OnMissingKeyChanged.RemoveAll(this);
		KeyDisplay->OnMissingKeyChanged.AddWeakLambda(this, [this](bool)
		{
			OnDisplayableKeyChanged.Broadcast();
		});
		KeyDisplay->SetWatchedInputAction(InteractionAction);
	}
}

void UInteractKeyWidget::NativeDestruct()
{
	if (KeyDisplay)
	{
		KeyDisplay->OnMissingKeyChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

bool UInteractKeyWidget::HasDisplayableKey() const
{
	return !KeyDisplay || !KeyDisplay->IsMissingKey();
}

void UInteractKeyWidget::UpdateInteractKey()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractKeyWidget::ClearInteractKey()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
