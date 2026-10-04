#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "GameOverChoiceWidget.generated.h"

class UButton;

UENUM(BlueprintType)
enum class EGameOverMenuChoice : uint8
{
	Continue,
	SelectLevel,
	MainMenu
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnGameOverChoiceConfirmed, EGameOverMenuChoice, Choice);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameOverChoiceEscape);

/** Three GameOver buttons with one selection shared by mouse and keyboard. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UGameOverChoiceWidget : public UUserWidget,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	UGameOverChoiceWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "Game Over|Choice")
	FOnGameOverChoiceConfirmed OnChoiceConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "Game Over|Choice")
	FOnGameOverChoiceEscape OnEscapeRequested;

	UFUNCTION(BlueprintPure, Category = "Game Over|Choice")
	EGameOverMenuChoice GetChosenButton() const;

	UFUNCTION(BlueprintCallable, Category = "Game Over|Choice")
	void SelectButton(EGameOverMenuChoice Choice);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ContinueButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LevelSelectButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> MainMenuButton;

private:
	UFUNCTION()
	void HandleContinueClicked();
	UFUNCTION()
	void HandleLevelSelectClicked();
	UFUNCTION()
	void HandleMainMenuClicked();
	UFUNCTION()
	void HandleContinueHovered();
	UFUNCTION()
	void HandleLevelSelectHovered();
	UFUNCTION()
	void HandleMainMenuHovered();

	void MoveSelection(int32 Step);
	void SetChosenIndex(int32 Index, bool bFocusButton);
	void ApplySelection();
	void ConfirmChoice(EGameOverMenuChoice Choice);
	UButton* GetButton(int32 Index) const;

	TArray<FButtonStyle> OriginalButtonStyles;
	int32 ChosenIndex = 0;
};
