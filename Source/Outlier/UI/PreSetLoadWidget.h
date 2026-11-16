#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "Upgrade/OutlierPresetStageIds.h"
#include "PreSetLoadWidget.generated.h"

class UButton;

UENUM(BlueprintType)
enum class EOutlierStage : uint8
{
	None = 0,
	Level01,
	Level02,
	Level03,
	Level04,
};

/** 스테이지 버튼을 누르면 발화한다. GameOverWidget이 이를 Pending 승인 요청으로 변환한다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPresetStageConfirmed, FName, StageId);

/** 스테이지 버튼의 hover 상태가 바뀔 때 발화. 미리보기 이미지는 이 위젯을 품은 쪽(GameOverWidget)이 그린다. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetStageHoverChanged, EOutlierStage /*Stage*/, bool /*bHovered*/);
DECLARE_MULTICAST_DELEGATE(FOnPresetMouseLeft);

/** 프리셋 레벨 선택 위젯. */
UCLASS()
class OUTLIER_API UPreSetLoadWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	/** 키보드 선택과 마우스 호버는 같은 스테이지 인덱스를 사용한다. */
	void SelectFirstStage();
	void MoveStageSelection(int32 Step);
	bool ConfirmStageSelection();
	void ResetStageSelection();
	void SetRetainSelectedStage(bool bRetain);

	/** 인덱스 0~3은 각각 Level01~Level04에 대응한다. */
	UFUNCTION(BlueprintPure, Category = "Preset|Stage")
	int32 GetChosenStageIndex() const { return ChosenStageIndex; }

	/** 가장 최근에 버튼을 클릭해 선택한 스테이지를 반환한다. */
	UFUNCTION(BlueprintPure, Category = "Preset|Stage")
	EOutlierStage GetSelectedStage() const { return SelectedStage; }

	/** GetSelectedStage()를 공용 선택 ID로 변환한다. Level04는 임시 체크포인트 테스트 선택지다. */
	UFUNCTION(BlueprintPure, Category = "Preset|Stage")
	FName GetSelectedStageId() const;

	UPROPERTY(BlueprintAssignable, Category = "Preset|Stage")
	FOnPresetStageConfirmed OnPresetStageConfirmed;

	FOnPresetStageHoverChanged OnPresetStageHoverChanged;
	FOnPresetMouseLeft OnPresetMouseLeft;

protected:
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Preset|Stage")
	TObjectPtr<UButton> UnPresetButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> Level01Button;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> Level02Button;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> Level03Button;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Preset|Stage")
	TObjectPtr<UButton> Level04Button;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Preset|Stage")
	TArray<TObjectPtr<UButton>> StageButtons;

private:
	void SetChosenStage(EOutlierStage Stage, bool bFocusButton);
	void HandleStageButtonUnhovered(EOutlierStage Stage);
	void ApplyStageSelection();
	UButton* GetStageButton(EOutlierStage Stage) const;
	TArray<FButtonStyle> OriginalStageButtonStyles;
	int32 ChosenStageIndex = 0;
	bool bShowStageSelection = false;
	bool bRetainSelectedStage = false;

	UFUNCTION()
	void HandleStageButtonClicked(EOutlierStage Stage);

	UFUNCTION()
	void HandleUnPresetButtonClicked();

	UFUNCTION()
	void HandleLevel01ButtonClicked();

	UFUNCTION()
	void HandleLevel02ButtonClicked();

	UFUNCTION()
	void HandleLevel03ButtonClicked();

	UFUNCTION()
	void HandleLevel04ButtonClicked();

	UFUNCTION()
	void HandleLevel01ButtonHovered();

	UFUNCTION()
	void HandleLevel01ButtonUnhovered();

	UFUNCTION()
	void HandleLevel02ButtonHovered();

	UFUNCTION()
	void HandleLevel02ButtonUnhovered();

	UFUNCTION()
	void HandleLevel03ButtonHovered();

	UFUNCTION()
	void HandleLevel03ButtonUnhovered();

	UFUNCTION()
	void HandleLevel04ButtonHovered();

	UFUNCTION()
	void HandleLevel04ButtonUnhovered();

	UPROPERTY(BlueprintReadOnly, Category = "Preset|Stage", meta = (AllowPrivateAccess = "true"))
	EOutlierStage SelectedStage = EOutlierStage::None;
};
