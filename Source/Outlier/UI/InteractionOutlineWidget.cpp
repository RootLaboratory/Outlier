#include "UI/InteractionOutlineWidget.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"

void UInteractionOutlineWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshTexture();
}

void UInteractionOutlineWidget::SetLocked(bool bInLocked)
{
	bLocked = bInLocked;
	RefreshTexture();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionOutlineWidget::HideIndicator()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UInteractionOutlineWidget::RefreshTexture()
{
	if (StatusImage)
	{
		StatusImage->SetBrushFromTexture(bLocked ? LockedTexture : AvailableTexture);
	}
}
