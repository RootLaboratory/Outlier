#include "UI/UILayerRootWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"

void UUILayerRootWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildFallbackLayout();
}

UPanelWidget* UUILayerRootWidget::GetGameplayLayer() const
{
	return GameplayLayer;
}

UPanelWidget* UUILayerRootWidget::GetGameMenuLayer() const
{
	return GameMenuLayer;
}

UPanelWidget* UUILayerRootWidget::GetModalLayer() const
{
	return ModalLayer;
}

UPanelWidget* UUILayerRootWidget::GetSystemLayer() const
{
	return SystemLayer;
}

void UUILayerRootWidget::BuildFallbackLayout()
{
	if (!WidgetTree)
	{
		return;
	}

	if (!LayerRoot)
	{
		LayerRoot = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(),
			TEXT("LayerRoot"));
		WidgetTree->RootWidget = LayerRoot;
	}

	auto EnsureCanvasLayer = [this](
		TObjectPtr<UCanvasPanel>& Layer,
		const FName LayerName)
	{
		if (Layer || !LayerRoot)
		{
			return;
		}

		Layer = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(),
			LayerName);
		Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		if (UOverlaySlot* Slot = LayerRoot->AddChildToOverlay(Layer))
		{
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetVerticalAlignment(VAlign_Fill);
		}
	};

	EnsureCanvasLayer(GameplayLayer, TEXT("GameplayLayer"));
	EnsureCanvasLayer(GameMenuLayer, TEXT("GameMenuLayer"));
	EnsureCanvasLayer(ModalLayer, TEXT("ModalLayer"));
	EnsureCanvasLayer(SystemLayer, TEXT("SystemLayer"));
}
