#pragma once

#include "CoreMinimal.h"
#include "UI/SettingPageWidget.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "SettingGraphicPageWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingGraphicPageWidget : public USettingPageWidget
{
	GENERATED_BODY()

public:
	virtual void ActivatePage() override;
	virtual bool HandlePageInput(ESettingPageInput Input) override;

	UFUNCTION(BlueprintCallable, Category = "Setting|Graphics")
	bool SetResolutionPresetIndex(int32 PresetIndex);
	UFUNCTION(BlueprintCallable, Category = "Setting|Graphics")
	bool OffsetResolutionPresetIndex(int32 IndexOffset);
	UFUNCTION(BlueprintCallable, Category = "Setting|Graphics")
	void RefreshGraphicsSettings();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void RebuildKeyboardFocusTargets() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Graphics")
	TObjectPtr<UTextBlock> ResolutionText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Graphics")
	TObjectPtr<UButton> GraphicLeftButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Graphics")
	TObjectPtr<UButton> GraphicRightButton;

private:
	UFUNCTION()
	void HandleGraphicLeftButtonClicked();
	UFUNCTION()
	void HandleGraphicRightButtonClicked();
	UFUNCTION()
	void HandleResolutionPresetChanged(EOutlierResolutionPreset NewPreset);
	bool ApplyPendingResolutionPreset();

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;
	int32 PendingResolutionPresetIndex = INDEX_NONE;
};
