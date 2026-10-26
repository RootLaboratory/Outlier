#pragma once

#include "CoreMinimal.h"
#include "UI/SettingPageWidget.h"
#include "SettingInputPageWidget.generated.h"

class UMouseSensitivityWidget;
class UOutlierInputBindingTable;
class UInputBindingRowWidget;
class UInputMappingContext;
class UPanelWidget;
class UTextBlock;
class IInputProcessor;
struct FOutlierInputBindingTableRow;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingInputPageWidget : public USettingPageWidget
{
	GENERATED_BODY()

public:
	USettingInputPageWidget(const FObjectInitializer& ObjectInitializer);

	virtual void ActivatePage() override;
	virtual void DeactivatePage() override;
	virtual bool HandlePageInput(ESettingPageInput Input) override;
	virtual bool HandlePageKeyDown(const FKeyEvent& KeyEvent) override;
	virtual void HandlePagePointerDown(const FPointerEvent& PointerEvent) override;
	virtual bool IsCapturingKeyboardInput() const override { return bWaitingForInputRebind; }
	UFUNCTION(BlueprintCallable, Category = "Setting|Input")
	void RebuildInputBindingList();

	UFUNCTION(BlueprintCallable, Category = "Setting|Input")
	void SetInputBindingRowPadding(FMargin NewPadding);

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeDestruct() override;
	virtual void RebuildKeyboardFocusTargets() override;
	virtual void FocusCurrentKeyboardTarget() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Input")
	TObjectPtr<UMouseSensitivityWidget> MouseSensitivityWidget;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Input")
	TObjectPtr<UPanelWidget> InputBindingListHost;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Input")
	TObjectPtr<UTextBlock> InputResultText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	TObjectPtr<UOutlierInputBindingTable> InputBindingTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	TSubclassOf<UInputBindingRowWidget> InputBindingRowWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	float InputBindingRowYOffset = 48.0f;

	// Extra space around every generated binding row, in addition to the Canvas Y offset.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting|Input")
	FMargin InputBindingRowPadding = FMargin(0.0f);

	// Shown when entering/leaving the page or cancelling a rebind.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText InputResultInitialText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText WaitingForInputText = FText::FromString(TEXT("Press a key."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText InputChangedText = FText::FromString(TEXT("Input has been changed."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText DuplicateInputText = FText::FromString(TEXT("Input key is already bound."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText InputRebindFailedText = FText::FromString(TEXT("Input key could not be changed."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText InputRequiredText = NSLOCTEXT("SettingInput", "InputRequired", "입력을 지정해 주세요.");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText AxisInputRequiredText = NSLOCTEXT("SettingInput", "AxisInputRequired", "해당 동작에 맞는 축 입력을 지정해 주세요.");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FLinearColor InputResultDefaultColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FLinearColor InputResultSuccessColor = FLinearColor(0.2f, 1.0f, 0.2f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FLinearColor InputResultFailureColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

private:
	friend class FSettingInputRebindPreProcessor;

	void ApplyInputBindingRowPadding();

	UFUNCTION()
	void HandleInputBindingRowHovered(UInputBindingRowWidget* RowWidget, int32 RowIndex);
	UFUNCTION()
	void HandleInputBindingRebindRequested(UInputBindingRowWidget* RowWidget, int32 RowIndex);
	bool StartInputRebind(UInputBindingRowWidget* RowWidget);
	bool TryCommitInputRebind(FKey NewKey);
	bool HandleInputRebindPointerKey(FKey Key);
	void EndInputRebind();
	bool IsPointerOverInputBindingRow(const FPointerEvent& InMouseEvent) const;
	void ClearInputBindingInteractionState(bool bClearResultText);
	bool TryGetUserSettingsKeyForRow(
		const FOutlierInputBindingTableRow& Row,
		FKey& OutKey) const;
	FKey ResolveInputBindingCurrentKey(
		const FOutlierInputBindingTableRow& Row) const;
	bool IsInputKeyDuplicate(FKey NewKey, const UInputBindingRowWidget* IgnoredRowWidget) const;
	void BuildInputBindingQueryContexts(TArray<UInputMappingContext*>& OutContexts) const;
	void RefreshInputBindingDisplayedKeys();
	void SetInputResult(const FText& ResultText, const FLinearColor& ResultColor);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputBindingRowWidget>> ActiveInputBindingRows;
	UPROPERTY(Transient)
	TObjectPtr<UInputBindingRowWidget> HoveredInputBindingRow;
	UPROPERTY(Transient)
	TObjectPtr<UInputBindingRowWidget> PendingRebindInputBindingRow;
	TMap<FName, FKey> RuntimeInputKeyOverrides;
	TSharedPtr<IInputProcessor> InputRebindPreProcessor;
	double RebindStartTimeSeconds = 0.0;
	bool bWaitingForInputRebind = false;
	bool bInputBindingListBuilt = false;
};
