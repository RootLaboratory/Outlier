#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "InGameSettingWidget.generated.h"

class UInGameSettingButtonsWidget;
class UInGamePauseWidget;
class USettingWidget;
class UTextBlock;
class UUILayerKeyHintWidget;
enum class EInGameSettingButtonAction : uint8;
enum class EOutlierCheckpointRestartVoteView : uint8;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInGameSettingWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	UInGameSettingWidget(const FObjectInitializer& ObjectInitializer);
	TSubclassOf<UUILayerKeyHintWidget> GetKeyHintWidgetClass() const;

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

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "InGame Setting")
	TObjectPtr<UTextBlock> MenuText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "InGame Setting")
	TObjectPtr<UInGamePauseWidget> InGamePause;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Setting")
	TObjectPtr<UInGameSettingButtonsWidget> ButtonsWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Layer")
	TSubclassOf<UInGamePauseWidget> InGamePauseWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Layer")
	TSubclassOf<USettingWidget> SettingWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Layer")
	TSubclassOf<UUILayerKeyHintWidget> KeyHintWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGame Setting|Text")
	FText GameLeaveWaitingText = FText::FromString(TEXT("매치를 종료하는 중입니다."));

private:
	void HandleButtonAction(EInGameSettingButtonAction Action);
	void PushSettingLayer();
	void RefreshCheckpointRestartState(EOutlierCheckpointRestartVoteView VoteView);
	void RefreshMenuState();

	UPROPERTY(Transient)
	TObjectPtr<USettingWidget> ActiveSettingWidget;

	FText DefaultMenuText;
	bool bLeavingGame = false;
};
