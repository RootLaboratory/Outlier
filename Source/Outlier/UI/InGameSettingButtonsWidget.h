#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "InGameSettingButtonsWidget.generated.h"

class UButton;

// WBP menu order, from top to bottom.
UENUM()
enum class EInGameSettingButtonAction : uint8
{
	Continue,
	RestartCheckpoint,
	Setting,
	Title,
	Exit
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnInGameSettingButtonActionConfirmed, EInGameSettingButtonAction);

// Owns button selection and input. The containing menu owns the resulting actions.
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInGameSettingButtonsWidget : public UUserWidget,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	FOnInGameSettingButtonActionConfirmed OnActionConfirmed;

	void ResetSelection();
	void SetActionEnabled(EInGameSettingButtonAction Action, bool bEnabled);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ContinueButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RestartCheckpointButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SettingButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> TitleButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ExitButton;

private:
	UFUNCTION()
	void HandleContinueClicked();
	UFUNCTION()
	void HandleRestartCheckpointClicked();
	UFUNCTION()
	void HandleSettingClicked();
	UFUNCTION()
	void HandleTitleClicked();
	UFUNCTION()
	void HandleExitClicked();
	UFUNCTION()
	void HandleContinueHovered();
	UFUNCTION()
	void HandleRestartCheckpointHovered();
	UFUNCTION()
	void HandleSettingHovered();
	UFUNCTION()
	void HandleTitleHovered();
	UFUNCTION()
	void HandleExitHovered();

	UButton* GetButton(EInGameSettingButtonAction Action) const;
	bool IsActionEnabled(EInGameSettingButtonAction Action) const;
	void MoveSelection(int32 Step);
	void SelectAction(EInGameSettingButtonAction Action);
	void ApplySelection();
	void ConfirmAction(EInGameSettingButtonAction Action);

	TArray<FButtonStyle> OriginalButtonStyles;
	EInGameSettingButtonAction SelectedAction = EInGameSettingButtonAction::Continue;
};
