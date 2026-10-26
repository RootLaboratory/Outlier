#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "SettingWidget.generated.h"

class USettingButtonsWidget;
class USettingPageWidget;
class USettingGraphicPageWidget;
class USettingSoundPageWidget;
class USettingInputPageWidget;
class UWidgetSwitcher;
enum class ESettingPageInput : uint8;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UFUNCTION(BlueprintCallable, Category = "UI|Layer")
	void PopSelfFromLayer();

	// WBP_SettingWidget contains SettingButtons (including ExitButton) and SettingSwitcher.
	// GraphicPage, SoundPage, and InputPage are nested WBP instances, each deriving
	// from its corresponding USetting*PageWidget and placed directly in the switcher.
	// Move InputBindingTable / InputBindingRowWidgetClass defaults to the Input WBP.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<USettingButtonsWidget> SettingButtons;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UWidgetSwitcher> SettingSwitcher;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<USettingGraphicPageWidget> GraphicPage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<USettingSoundPageWidget> SoundPage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<USettingInputPageWidget> InputPage;

	// PageIndex is the tab index: Graphic=0, Sound=1, Input=2.
	UFUNCTION(BlueprintCallable, Category = "Setting|Navigation")
	bool SetActiveSettingPage(int32 PageIndex);

#if WITH_EDITORONLY_DATA
	// Select which switcher page to show in the WBP designer: Graphic=0, Sound=1, Input=2.
	UPROPERTY(EditAnywhere, Category = "Setting|Preview", meta = (ClampMin = "0", ClampMax = "2"))
	int32 PreviewPageIndex = 0;
#endif

private:
	enum class EFocusArea : uint8 { Buttons, Page };
	bool RoutePageInput(ESettingPageInput Input);
	void HandlePageRequested(int32 PageIndex);
	void HandleKeyboardCaptureRequested();
	void FocusActivePage(bool bLastTarget = false);

	UPROPERTY(Transient)
	TObjectPtr<USettingPageWidget> ActivePage;
	EFocusArea FocusArea = EFocusArea::Page;
};
