#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "TitleButtonsWidget.generated.h"

class UButton;

// 방향키 이동 순서와 같다. WBP의 위→아래 배치 순서와 맞춘다.
UENUM()
enum class ETitleButtonAction : uint8
{
	Start,
	Setting,
	Credit,
	Exit
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnTitleButtonActionConfirmed, ETitleButtonAction);

// Title 버튼의 선택·클릭·레이어 입력을 한곳에서 처리한다. 확정된 액션만 알리고 레이어 전환은 소유 레이어가 맡는다.
UCLASS(Abstract)
class OUTLIER_API UTitleButtonsWidget : public UUserWidget,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	FOnTitleButtonActionConfirmed OnActionConfirmed;

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
	TObjectPtr<UButton> StartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CreditButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SettingButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ExitButton;

private:
	UFUNCTION()
	void HandleStartClicked();
	UFUNCTION()
	void HandleCreditClicked();
	UFUNCTION()
	void HandleSettingClicked();
	UFUNCTION()
	void HandleExitClicked();
	UFUNCTION()
	void HandleStartHovered();
	UFUNCTION()
	void HandleCreditHovered();
	UFUNCTION()
	void HandleSettingHovered();
	UFUNCTION()
	void HandleExitHovered();

	void MoveSelection(int32 Step);
	void SelectAction(ETitleButtonAction Action);
	void ApplySelection();
	void ConfirmAction(ETitleButtonAction Action);
	UButton* GetButton(ETitleButtonAction Action) const;

	// 선택 강조는 매번 원본에서 다시 만든다. 강조된 스타일이 원본을 덮어쓰지 않게 초기화 때 한 번만 저장한다.
	TArray<FButtonStyle> OriginalButtonStyles;
	ETitleButtonAction SelectedAction = ETitleButtonAction::Start;
};
