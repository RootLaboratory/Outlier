#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Styling/SlateTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "OutlierEndingWidget.generated.h"

class UButton;
class UUILayerKeyHintWidget;

/** 엔딩 화면이다. 마우스와 레이어 확인 입력은 기존 게임 나가기 흐름을 공유한다. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UOutlierEndingWidget : public UUserWidget, public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	UOutlierEndingWidget(const FObjectInitializer& ObjectInitializer);
	TSubclassOf<UUILayerKeyHintWidget> GetKeyHintWidgetClass() const { return KeyHintWidgetClass; }
	bool HasEndingButton() const { return EndingButton != nullptr; }
	FGameplayTag GetEndingAudioTag() const { return EndingAudioTag; }
	FGameplayTag GetEndingAudioType() const { return EndingAudioType; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerEscape_Implementation() override { return true; }
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Ending")
	TObjectPtr<UButton> EndingButton;

	/** 타이틀 복귀 동작에는 확인 입력만 표시하는 힌트 WBP를 사용한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Ending|Layer")
	TSubclassOf<UUILayerKeyHintWidget> KeyHintWidgetClass;

	/** 선택적 오디오 컨텍스트다. 엔딩 사운드를 Audio Bank에 추가한 뒤 할당한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Ending|Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag EndingAudioTag;

	/** 미설정 시 Audio.Type.BGM을 사용한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Ending|Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag EndingAudioType;

private:
	UFUNCTION()
	void HandleEndingClicked();
	UFUNCTION()
	void HandleEndingHovered();
	UFUNCTION()
	void HandleEndingUnhovered();
	bool SelectEndingFromKeyboard();
	void SetEndingSelected(bool bSelected);
	FButtonStyle OriginalEndingStyle;
	bool bEndingSelected = false;
};
