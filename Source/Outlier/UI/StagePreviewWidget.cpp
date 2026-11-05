#include "UI/StagePreviewWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"

void UStagePreviewWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetPreviewVisible(false);
}

bool UStagePreviewWidget::SetPreviewTexture(UTexture2D* Texture)
{
	if (!PreviewImage || !Texture)
	{
		return false;
	}
	// Keep the size authored in the preview WBP.
	PreviewImage->SetBrushFromTexture(Texture, false);
	return true;
}

void UStagePreviewWidget::SetPreviewVisible(bool bVisible)
{
	// Keep the entire preview decorative so overlapping stage buttons retain hover and clicks.
	const ESlateVisibility TargetVisibility = bVisible
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed;
	SetVisibility(TargetVisibility);
	if (WidgetTree)
	{
		WidgetTree->ForEachWidgetAndDescendants([TargetVisibility](UWidget* Widget)
		{
			if (Widget) Widget->SetVisibility(TargetVisibility);
		});
	}
}
