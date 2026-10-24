#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "SettingButtonsWidget.generated.h"

class UButton;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingButtonsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPageRequested, int32);
	FOnPageRequested OnPageRequested;
	DECLARE_MULTICAST_DELEGATE(FOnExitRequested);
	FOnExitRequested OnExitRequested;

	void SetSelectedPage(int32 PageIndex);
	void OffsetSelection(int32 Direction);
	void FocusSelectedButton();
	void FocusSelectedPageButton();
	void FocusExitButton();
	bool HandlePointerDown(const FPointerEvent& PointerEvent);
	bool IsExitSelected() const { return SelectedButtonIndex == ExitButtonIndex; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UButton> GraphicButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UButton> SoundButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UButton> InputButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UButton> ExitButton;

private:
	UFUNCTION()
	void HandleGraphicClicked();
	UFUNCTION()
	void HandleSoundClicked();
	UFUNCTION()
	void HandleInputClicked();
	UFUNCTION()
	void HandleExitClicked();

	UButton* GetButton(int32 Index) const;
	void ApplySelection();
	TArray<FButtonStyle> OriginalButtonStyles;
	int32 SelectedPageIndex = 0;
	int32 SelectedButtonIndex = 0;
	static constexpr int32 ExitButtonIndex = 3;
	static constexpr int32 ButtonCount = 4;
};
