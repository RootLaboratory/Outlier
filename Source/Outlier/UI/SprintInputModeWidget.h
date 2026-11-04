#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Settings/OutlierInputUserSettings.h"
#include "SprintInputModeWidget.generated.h"

class UButton;
class UTextBlock;
class ULocalPlayerSettingsSubsystem;

// 역할별 달리기 방식의 표시와 변경을 담당한다. 레이아웃은 파생 WBP에서 구성한다.
UCLASS(Blueprintable)
class OUTLIER_API USprintInputModeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSettingsContext(ULocalPlayerSettingsSubsystem* Settings, EOutlierPlayerRole Role, bool bActive);
	bool CycleMode();
	UWidget* GetKeyboardFocusTarget() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativePreConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sprint")
	TObjectPtr<UButton> SprintModeLeftButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sprint")
	TObjectPtr<UButton> SprintModeRightButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sprint")
	TObjectPtr<UTextBlock> SprintModeText;

private:
	friend struct FInputSettingsPresentationTestAccess;
	void BindButtons();
	void RefreshFromSettings();
	bool CycleMode(uint32 ExpectedRevision);
	UFUNCTION()
	void HandlePressed();
	UFUNCTION()
	void HandleClicked();
	UFUNCTION()
	void HandleModeChanged(EOutlierPlayerRole Role, EOutlierSprintInputMode Mode);

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;
	EOutlierPlayerRole DisplayedRole{};
	bool bInteractionActive = false;
	uint32 PressRevision = MAX_uint32;
};
