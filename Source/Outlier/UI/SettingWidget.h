#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "SettingWidget.generated.h"

class UButton;
class UInputBindingRowWidget;
class UInputMappingContext;
class ULocalPlayerSettingsSubsystem;
class UOutlierInputBindingTable;
class UPanelWidget;
class USettingSliderRowWidget;
class UTextBlock;
class UWidgetSwitcher;
struct FGeometry;
struct FKeyEvent;
struct FPointerEvent;
struct FOutlierInputBindingTableRow;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewMouseButtonDown(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(
		const FGeometry& InGeometry,
		const FKeyEvent& InKeyEvent) override;
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UFUNCTION(BlueprintCallable, Category = "UI|Layer")
	void PopSelfFromLayer();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UButton> GraphicButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UButton> SoundButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UButton> InputButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Navigation")
	TObjectPtr<UWidgetSwitcher> SettingSwitcher;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Graphics")
	TObjectPtr<UTextBlock> ResolutionText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Graphics")
	TObjectPtr<UButton> GraphicLeftButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Graphics")
	TObjectPtr<UButton> GraphicRightButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> TotalVolumeRow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> BGMVolumeRow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> SFXVolumeRow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Setting|Sound")
	TObjectPtr<USettingSliderRowWidget> VoiceVolumeRow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Setting|Input")
	TObjectPtr<UPanelWidget> InputBindingListHost;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Setting|Input")
	TObjectPtr<UTextBlock> InputResultText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Navigation")
	int32 GraphicPageIndex = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Navigation")
	int32 SoundPageIndex = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Navigation")
	int32 InputPageIndex = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	TObjectPtr<UOutlierInputBindingTable> InputBindingTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	TSubclassOf<UInputBindingRowWidget> InputBindingRowWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	float InputBindingRowYOffset = 48.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Sound")
	float SoundLabelColumnWidth = 240.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Sound")
	float SoundSliderColumnWidth = 360.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Sound")
	float SoundValueColumnWidth = 80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText WaitingForInputText = FText::FromString(TEXT("Press a key."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText InputChangedText = FText::FromString(TEXT("Input has been changed."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText DuplicateInputText = FText::FromString(TEXT("Input key is already bound."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FText InputRebindFailedText = FText::FromString(TEXT("Input key could not be changed."));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FLinearColor InputResultDefaultColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FLinearColor InputResultSuccessColor = FLinearColor(0.2f, 1.0f, 0.2f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Input")
	FLinearColor InputResultFailureColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Setting|Navigation")
	bool SetActiveSettingPage(int32 PageIndex);

	UFUNCTION(BlueprintCallable, Category = "Setting|Graphics")
	bool SetResolutionPresetIndex(int32 PresetIndex);

	UFUNCTION(BlueprintCallable, Category = "Setting|Graphics")
	bool OffsetResolutionPresetIndex(int32 IndexOffset);

	UFUNCTION(BlueprintCallable, Category = "Setting|Graphics")
	void RefreshGraphicsSettings();

	UFUNCTION(BlueprintCallable, Category = "Setting|Sound")
	bool SetSoundVolumeIndex(int32 VolumeIndex, float NewValue);

	UFUNCTION(BlueprintCallable, Category = "Setting|Sound")
	void RefreshSoundSettings();

	UFUNCTION(BlueprintCallable, Category = "Setting|Input")
	void RebuildInputBindingList();

private:
	UFUNCTION()
	void HandleGraphicButtonClicked();

	UFUNCTION()
	void HandleSoundButtonClicked();

	UFUNCTION()
	void HandleInputButtonClicked();

	UFUNCTION()
	void HandleGraphicLeftButtonClicked();

	UFUNCTION()
	void HandleGraphicRightButtonClicked();

	UFUNCTION()
	void HandleTotalVolumeRowChanged(float NewValue);

	UFUNCTION()
	void HandleBGMVolumeRowChanged(float NewValue);

	UFUNCTION()
	void HandleSFXVolumeRowChanged(float NewValue);

	UFUNCTION()
	void HandleVoiceVolumeRowChanged(float NewValue);

	UFUNCTION()
	void HandleInputBindingRowHovered(UInputBindingRowWidget* RowWidget, int32 RowIndex);

	UFUNCTION()
	void HandleInputBindingRebindRequested(UInputBindingRowWidget* RowWidget, int32 RowIndex);

	bool ApplyPendingResolutionPreset();
	bool StartInputRebind(UInputBindingRowWidget* RowWidget);
	bool TryCommitInputRebind(FKey NewKey);
	bool IsPointerOverInputBindingRow(const FPointerEvent& InMouseEvent) const;
	void ClearInputBindingInteractionState(bool bClearResultText);
	bool TryGetUserSettingsKeyForRow(
		const FOutlierInputBindingTableRow& Row,
		FKey& OutKey) const;
	FKey ResolveInputBindingCurrentKey(
		const FOutlierInputBindingTableRow& Row) const;
	bool IsInputKeyDuplicate(FKey NewKey, const UInputBindingRowWidget* IgnoredRowWidget) const;
	void BuildInputBindingQueryContexts(TArray<UInputMappingContext*>& OutContexts) const;
	void RefreshInputBindingDisplayedKeys();
	void SetInputResult(const FText& ResultText, const FLinearColor& ResultColor);
	void CacheWidgetArrays();
	void BindSettingsSubsystem();
	void UnbindSettingsSubsystem();

	UFUNCTION()
	void HandleResolutionPresetChanged(EOutlierResolutionPreset NewPreset);

	UFUNCTION()
	void HandleSoundVolumeChanged(EOutlierAudioVolumeType VolumeType, float NewValue);

	ULocalPlayerSettingsSubsystem* GetSettingsSubsystem() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> SettingTabButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USettingSliderRowWidget>> SoundVolumeRows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputBindingRowWidget>> ActiveInputBindingRows;

	UPROPERTY(Transient)
	TObjectPtr<UInputBindingRowWidget> HoveredInputBindingRow;

	UPROPERTY(Transient)
	TObjectPtr<UInputBindingRowWidget> PendingRebindInputBindingRow;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;

	//Mapping Name으로 override 관리.
	TMap<FName, FKey> RuntimeInputKeyOverrides;

	int32 PendingResolutionPresetIndex = INDEX_NONE;
	int32 CurrentSettingPageIndex = INDEX_NONE;
	double RebindStartTimeSeconds = 0.0;
	bool bWaitingForInputRebind = false;
	bool bRefreshingSoundSettings = false;
	bool bInputBindingListBuilt = false;
};
