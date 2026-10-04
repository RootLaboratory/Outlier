#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/PreSetLoadWidget.h"
#include "UI/GameOverChoiceWidget.h"
#include "UI/GameOverPendingTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "GameOverWidget.generated.h"

class UButton;
class UImage;
class UTexture2D;
class UGameOverPendingWidget;
class UUILayerKeyHintWidget;
class UCanvasPanel;

// 사망 연출이 끝날 때 LocalPlayerUILayerSubsystem으로 Push되는 게임오버 화면.
// 레벨 선택은 별도 레이어 없이 안에 넣어 둔 PreSetLoadWidget을 Visible/Collapsed로 토글한다.
UCLASS(Abstract)
class OUTLIER_API UGameOverWidget : public UUserWidget, public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	UGameOverWidget(const FObjectInitializer& ObjectInitializer);
	TSubclassOf<UGameOverPendingWidget> GetGameOverPendingWidgetClass() const;
	TSubclassOf<UUILayerKeyHintWidget> GetKeyHintWidgetClass() const;
	bool ShowPendingRequest(const FGameOverPendingRequest& Request, bool bIsRequester);
	void ClosePendingRequest();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	// 지정하지 않으면 루트 CanvasPanel을 사용한다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> GameOverCanvas;

	// Replace the legacy three-button group with a WBP based on GameOverChoiceWidget.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UGameOverChoiceWidget> ChoiceMenu;

	// Kept optional while WBP_GameOver is migrated to ChoiceMenu.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ContinueButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> LevelSelectButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> MainMenuButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPreSetLoadWidget> PresetLoad;

	// PresetLoad의 스테이지 버튼에 hover하는 동안만 보인다. 위치와 크기는 WBP 배치를 따른다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> StagePreviewImage;

	// hover된 스테이지에 맞춰 StagePreviewImage 브러시에 넣을 텍스처.
	UPROPERTY(EditDefaultsOnly, Category = "Game Over|Stage Preview")
	TMap<EOutlierStage, TObjectPtr<UTexture2D>> StagePreviewTextures;

	// GameOver WBP에서 승인 대기 화면의 WBP 클래스를 지정한다.
	UPROPERTY(EditDefaultsOnly, Category = "Game Over|Pending")
	TSubclassOf<UGameOverPendingWidget> GameOverPendingWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Game Over|Layer")
	TSubclassOf<UUILayerKeyHintWidget> KeyHintWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UGameOverPendingWidget> ActivePendingWidget;

private:
	UFUNCTION()
	void HandleChoiceConfirmed(EGameOverMenuChoice Choice);
	UFUNCTION()
	void HandleChoiceEscape();

	UFUNCTION()
	void HandleContinueButtonClicked();

	UFUNCTION()
	void HandleLevelSelectButtonClicked();

	UFUNCTION()
	void HandleMainMenuButtonClicked();

	UFUNCTION()
	void HandlePresetStageConfirmed(FName StageId);

	void HandlePresetStageHoverChanged(EOutlierStage Stage, bool bHovered);
	void ShowStagePreview(EOutlierStage Stage);
	void HideStagePreview();
	void ClosePresetLoad();
	void SubmitPendingChoice(EGameOverPendingChoice Choice, int32 LevelIndex = 0);

	EOutlierStage PreviewStage = EOutlierStage::None;
};
