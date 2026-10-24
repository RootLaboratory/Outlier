#pragma once

#include "CoreMinimal.h"
#include "UI/SettingPageWidget.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "SettingSoundPageWidget.generated.h"

class USettingSliderRowWidget;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingSoundPageWidget : public USettingPageWidget
{
	GENERATED_BODY()

public:
	virtual void ActivatePage() override;
	virtual bool HandlePageInput(ESettingPageInput Input) override;
	UFUNCTION(BlueprintCallable, Category = "Setting|Sound")
	bool SetSoundVolumeIndex(int32 VolumeIndex, float NewValue);
	UFUNCTION(BlueprintCallable, Category = "Setting|Sound")
	void RefreshSoundSettings();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void RebuildKeyboardFocusTargets() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> TotalVolumeRow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> BGMVolumeRow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> SFXVolumeRow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> VoiceVolumeRow;

	// Keep the row WBP's authored sizes unless an explicit page-wide override is enabled.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Sound")
	bool bOverrideSoundColumnWidths = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Sound", meta = (EditCondition = "bOverrideSoundColumnWidths", ClampMin = "0.0"))
	float SoundLabelColumnWidth = 240.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Sound", meta = (EditCondition = "bOverrideSoundColumnWidths", ClampMin = "0.0"))
	float SoundSliderColumnWidth = 360.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Sound", meta = (EditCondition = "bOverrideSoundColumnWidths", ClampMin = "0.0"))
	float SoundValueColumnWidth = 80.0f;

	// Set Top to add space before a row, or Bottom to add space after it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting|Sound|Row Padding")
	FMargin TotalVolumeRowPadding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting|Sound|Row Padding")
	FMargin BGMVolumeRowPadding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting|Sound|Row Padding")
	FMargin SFXVolumeRowPadding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting|Sound|Row Padding")
	FMargin VoiceVolumeRowPadding = FMargin(0.0f);

private:
	void ApplySoundRowPadding();

	UFUNCTION()
	void HandleTotalVolumeRowChanged(float NewValue);
	UFUNCTION()
	void HandleBGMVolumeRowChanged(float NewValue);
	UFUNCTION()
	void HandleSFXVolumeRowChanged(float NewValue);
	UFUNCTION()
	void HandleVoiceVolumeRowChanged(float NewValue);
	UFUNCTION()
	void HandleSoundVolumeChanged(EOutlierAudioVolumeType VolumeType, float NewValue);

	UPROPERTY(Transient)
	TArray<TObjectPtr<USettingSliderRowWidget>> SoundVolumeRows;
	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;
	bool bRefreshingSoundSettings = false;
};
