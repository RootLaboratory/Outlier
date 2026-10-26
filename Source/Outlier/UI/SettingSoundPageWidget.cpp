#include "UI/SettingSoundPageWidget.h"
#include "UI/SettingSliderRowWidget.h"

void USettingSoundPageWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SoundVolumeRows = { TotalVolumeRow, BGMVolumeRow, SFXVolumeRow, VoiceVolumeRow };
	if (TotalVolumeRow) TotalVolumeRow->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleTotalVolumeRowChanged);
	if (BGMVolumeRow) BGMVolumeRow->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleBGMVolumeRowChanged);
	if (SFXVolumeRow) SFXVolumeRow->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleSFXVolumeRowChanged);
	if (VoiceVolumeRow) VoiceVolumeRow->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleVoiceVolumeRowChanged);
}

void USettingSoundPageWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplySoundRowPadding();
	if (IsDesignTime()) RefreshSoundSettings();
}

void USettingSoundPageWidget::ApplySoundRowPadding()
{
	// Use the bound widgets directly so padding is also visible in the WBP designer.
	if (TotalVolumeRow) TotalVolumeRow->SetPadding(TotalVolumeRowPadding);
	if (BGMVolumeRow) BGMVolumeRow->SetPadding(BGMVolumeRowPadding);
	if (SFXVolumeRow) SFXVolumeRow->SetPadding(SFXVolumeRowPadding);
	if (VoiceVolumeRow) VoiceVolumeRow->SetPadding(VoiceVolumeRowPadding);
}

void USettingSoundPageWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BoundSettingsSubsystem = GetSettingsSubsystem();
	if (BoundSettingsSubsystem) BoundSettingsSubsystem->OnSoundVolumeChanged.AddUniqueDynamic(this, &ThisClass::HandleSoundVolumeChanged);
	RefreshSoundSettings();
}

void USettingSoundPageWidget::NativeDestruct()
{
	if (BoundSettingsSubsystem) BoundSettingsSubsystem->OnSoundVolumeChanged.RemoveAll(this);
	BoundSettingsSubsystem = nullptr;
	Super::NativeDestruct();
}

void USettingSoundPageWidget::ActivatePage()
{
	RefreshSoundSettings();
	Super::ActivatePage();
}

void USettingSoundPageWidget::RebuildKeyboardFocusTargets()
{
	KeyboardFocusTargets.Reset();
	for (USettingSliderRowWidget* Row : SoundVolumeRows)
	{
		if (Row)
		{
			Row->SetIsFocusable(true);
			KeyboardFocusTargets.Add(Row);
		}
	}
}

bool USettingSoundPageWidget::HandlePageInput(ESettingPageInput Input)
{
	if ((Input == ESettingPageInput::Left || Input == ESettingPageInput::Right)
		&& KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex))
	{
		if (USettingSliderRowWidget* Row = Cast<USettingSliderRowWidget>(KeyboardFocusTargets[CurrentFocusedIndex]))
			return SetSoundVolumeIndex(SoundVolumeRows.IndexOfByKey(Row), Row->GetValue() + (Input == ESettingPageInput::Right ? 0.01f : -0.01f));
	}
	return Super::HandlePageInput(Input);
}

bool USettingSoundPageWidget::SetSoundVolumeIndex(int32 VolumeIndex, float NewValue)
{
	if (bRefreshingSoundSettings)
	{
		return false;
	}

	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem)
	{
		return false;
	}

	return SettingsSubsystem->SetSoundVolumeByIndex(VolumeIndex, NewValue);
}

void USettingSoundPageWidget::RefreshSoundSettings()
{
	SoundVolumeRows = { TotalVolumeRow, BGMVolumeRow, SFXVolumeRow, VoiceVolumeRow };
	TArray<FOutlierSoundVolumeOption> PreviewOptions;
	const TArray<FOutlierSoundVolumeOption>* Options = nullptr;
	if (IsDesignTime())
	{
		// The designer has no local-player subsystem. Show the default labels and 100% values.
		for (const TCHAR* Label : { TEXT("Total Volume"), TEXT("BGM Volume"), TEXT("SFX Volume"), TEXT("Voice Volume") })
		{
			FOutlierSoundVolumeOption& Option = PreviewOptions.AddDefaulted_GetRef();
			Option.DisplayName = FText::FromString(Label);
		}
		Options = &PreviewOptions;
	}
	else if (ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem())
	{
		Options = &SettingsSubsystem->GetSoundVolumeOptions();
	}
	if (!Options) return;

	const TArray<FOutlierSoundVolumeOption>& SoundVolumeOptions = *Options;

	bRefreshingSoundSettings = true;
	for (int32 Index = 0; Index < SoundVolumeRows.Num(); ++Index)
	{
		USettingSliderRowWidget* SoundVolumeRow = SoundVolumeRows[Index];
		if (!SoundVolumeRow || !SoundVolumeOptions.IsValidIndex(Index))
		{
			continue;
		}

		if (bOverrideSoundColumnWidths)
		{
			SoundVolumeRow->SetColumnWidths(
				SoundLabelColumnWidth,
				SoundSliderColumnWidth,
				SoundValueColumnWidth);
		}
		SoundVolumeRow->SetLabelText(SoundVolumeOptions[Index].DisplayName);
		SoundVolumeRow->SetValue(SoundVolumeOptions[Index].Value);
	}
	bRefreshingSoundSettings = false;
}

void USettingSoundPageWidget::HandleTotalVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(0, NewValue);
}

void USettingSoundPageWidget::HandleBGMVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(1, NewValue);
}

void USettingSoundPageWidget::HandleSFXVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(2, NewValue);
}

void USettingSoundPageWidget::HandleVoiceVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(3, NewValue);
}

void USettingSoundPageWidget::HandleSoundVolumeChanged(
	EOutlierAudioVolumeType VolumeType,
	float NewValue)
{
	(void)VolumeType;
	(void)NewValue;
	RefreshSoundSettings();
}
