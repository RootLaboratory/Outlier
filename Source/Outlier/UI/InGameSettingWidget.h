#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Save/OutlierCheckpointRestartVote.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "UI/UILayerTypes.h"
#include "InGameSettingWidget.generated.h"

class UButton;
class UInGamePauseWidget;
class USettingWidget;
class UTextBlock;
class UUILayerKeyHintWidget;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInGameSettingWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	TSubclassOf<UInGamePauseWidget> GetInGamePauseWidgetClass() const
	{
		return InGamePauseWidgetClass;
	}

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Setting")
	TObjectPtr<UButton> ContinueButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Setting")
	TObjectPtr<UButton> SettingButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Setting")
	TObjectPtr<UButton> RestartCheckpointButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Setting")
	TObjectPtr<UButton> TitleButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "InGame Setting")
	TObjectPtr<UTextBlock> MenuText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "InGame Setting")
	TObjectPtr<UInGamePauseWidget> InGamePause;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Layer")
	TSubclassOf<UInGamePauseWidget> InGamePauseWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Layer")
	TSubclassOf<USettingWidget> SettingWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Layer")
	TSubclassOf<UUILayerKeyHintWidget> KeyHintWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Text")
	FText CheckpointWaitingText = FText::FromString(TEXT("상대 플레이어의 응답을 기다리는 중입니다"));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Text")
	FText GameExitConfirmationText = FText::FromString(TEXT("정말 게임을 종료하시겠습니까?"));

private:
	UFUNCTION()
	void HandleContinueButtonClicked();

	UFUNCTION()
	void HandleSettingButtonClicked();

	UFUNCTION()
	void HandleRestartCheckpointButtonClicked();

	UFUNCTION()
	void HandleTitleButtonClicked();

	void PushKeyHintLayer();
	void PopKeyHintLayer();
	void PopSelfFromLayer();
	void PushSettingLayer();
	void CancelGameExitConfirmation();
	void ConfirmGameExit();
	void RefreshCheckpointRestartState(EOutlierCheckpointRestartVoteView VoteView);

	UPROPERTY(Transient)
	TObjectPtr<UUILayerKeyHintWidget> ActiveKeyHintWidget;

	UPROPERTY(Transient)
	TObjectPtr<USettingWidget> ActiveSettingWidget;

	FUILayerHandle KeyHintLayerHandle;
	FText DefaultMenuText;
	bool bConfirmingGameExit = false;
};
