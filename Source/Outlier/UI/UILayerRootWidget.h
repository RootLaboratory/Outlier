#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UILayerRootWidget.generated.h"

class UCanvasPanel;
class UOverlay;
class UPanelWidget;

UCLASS(Blueprintable)
class OUTLIER_API UUILayerRootWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPanelWidget* GetGameplayLayer() const;
	UPanelWidget* GetGameMenuLayer() const;
	UPanelWidget* GetModalLayer() const;
	UPanelWidget* GetSystemLayer() const;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI Layer")
	TObjectPtr<UOverlay> LayerRoot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI Layer")
	TObjectPtr<UCanvasPanel> GameplayLayer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI Layer")
	TObjectPtr<UCanvasPanel> GameMenuLayer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI Layer")
	TObjectPtr<UCanvasPanel> ModalLayer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI Layer")
	TObjectPtr<UCanvasPanel> SystemLayer;

private:
	void BuildFallbackLayout();
};
