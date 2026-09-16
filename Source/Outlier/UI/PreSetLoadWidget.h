// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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

/** 버튼을 누를 때마다(=선택을 서버에 보고할 때마다) 발화. 즉시 확정/닫힘을 의미하지 않는다 —
 *  페어 상대가 같은 스테이지를 고를 때까지는 위젯이 계속 떠 있다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPresetStageConfirmed, FName, StageId);

/** Preset stage selection widget. */
UCLASS()
class OUTLIER_API UPreSetLoadWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	/** Returns the stage selected by the most recent button click. */
	UFUNCTION(BlueprintPure, Category = "Preset|Stage")
	EOutlierStage GetSelectedStage() const { return SelectedStage; }

	/** GetSelectedStage()를 OutlierPresetStageIds.h 의 공용 FName으로 변환한다. */
	UFUNCTION(BlueprintPure, Category = "Preset|Stage")
	FName GetSelectedStageId() const;

	UPROPERTY(BlueprintAssignable, Category = "Preset|Stage")
	FOnPresetStageConfirmed OnPresetStageConfirmed;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> UnPresetButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> Level01Button;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> Level02Button;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> Level03Button;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Preset|Stage")
	TObjectPtr<UButton> Level04Button;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Preset|Stage")
	TArray<TObjectPtr<UButton>> StageButtons;

private:
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

	UPROPERTY(BlueprintReadOnly, Category = "Preset|Stage", meta = (AllowPrivateAccess = "true"))
	EOutlierStage SelectedStage = EOutlierStage::None;
};
