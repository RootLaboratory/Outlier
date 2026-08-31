#include "Settings/LocalPlayerSettingsSubsystem.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "GameFramework/GameUserSettings.h"

void ULocalPlayerSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	InitializeResolutionOptions();
	InitializeSoundVolumeOptions();
	SyncResolutionPresetFromGameUserSettings();
	ApplySoundVolumeOptions();
}

EOutlierResolutionPreset ULocalPlayerSettingsSubsystem::GetResolutionPreset() const
{
	return CurrentResolutionPreset;
}

int32 ULocalPlayerSettingsSubsystem::GetResolutionPresetIndex() const
{
	return FindResolutionPresetIndex(CurrentResolutionPreset);
}

bool ULocalPlayerSettingsSubsystem::SetResolutionPreset(
	EOutlierResolutionPreset NewPreset,
	bool bApplyImmediately)
{
	if (FindResolutionPresetIndex(NewPreset) == INDEX_NONE)
	{
		return false;
	}

	if (CurrentResolutionPreset == NewPreset)
	{
		return true;
	}

	CurrentResolutionPreset = NewPreset;

	if (bApplyImmediately && !ApplyResolutionPreset())
	{
		return false;
	}

	OnResolutionPresetChanged.Broadcast(CurrentResolutionPreset);
	return true;
}

bool ULocalPlayerSettingsSubsystem::SetResolutionPresetByIndex(
	int32 NewIndex,
	bool bApplyImmediately)
{
	if (!ResolutionOptions.IsValidIndex(NewIndex))
	{
		return false;
	}

	return SetResolutionPreset(
		ResolutionOptions[NewIndex].Preset,
		bApplyImmediately);
}

const TArray<FOutlierResolutionOption>& ULocalPlayerSettingsSubsystem::GetResolutionOptions() const
{
	return ResolutionOptions;
}

const TArray<FOutlierSoundVolumeOption>& ULocalPlayerSettingsSubsystem::GetSoundVolumeOptions() const
{
	return SoundVolumeOptions;
}

float ULocalPlayerSettingsSubsystem::GetSoundVolume(
	EOutlierAudioVolumeType VolumeType) const
{
	const int32 VolumeIndex = FindSoundVolumeIndex(VolumeType);
	return SoundVolumeOptions.IsValidIndex(VolumeIndex)
		? SoundVolumeOptions[VolumeIndex].Value
		: 1.0f;
}

bool ULocalPlayerSettingsSubsystem::SetSoundVolume(
	EOutlierAudioVolumeType VolumeType,
	float NewValue,
	bool bApplyImmediately)
{
	const int32 VolumeIndex = FindSoundVolumeIndex(VolumeType);
	if (!SoundVolumeOptions.IsValidIndex(VolumeIndex))
	{
		return false;
	}

	const float ClampedValue = FMath::Clamp(NewValue, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(SoundVolumeOptions[VolumeIndex].Value, ClampedValue))
	{
		return true;
	}

	SoundVolumeOptions[VolumeIndex].Value = ClampedValue;

	if (bApplyImmediately && !ApplySoundVolume(VolumeType))
	{
		return false;
	}

	OnSoundVolumeChanged.Broadcast(VolumeType, ClampedValue);
	return true;
}

bool ULocalPlayerSettingsSubsystem::SetSoundVolumeByIndex(
	int32 VolumeIndex,
	float NewValue,
	bool bApplyImmediately)
{
	if (!SoundVolumeOptions.IsValidIndex(VolumeIndex))
	{
		return false;
	}

	return SetSoundVolume(
		SoundVolumeOptions[VolumeIndex].VolumeType,
		NewValue,
		bApplyImmediately);
}

void ULocalPlayerSettingsSubsystem::InitializeResolutionOptions()
{
	if (!ResolutionOptions.IsEmpty())
	{
		return;
	}

	FOutlierResolutionOption FHDOption;
	FHDOption.Preset = EOutlierResolutionPreset::FHD;
	FHDOption.DisplayName = FText::FromString(TEXT("FHD"));
	FHDOption.Resolution = FIntPoint(1920, 1080);
	ResolutionOptions.Add(FHDOption);

	FOutlierResolutionOption QHDOption;
	QHDOption.Preset = EOutlierResolutionPreset::QHD;
	QHDOption.DisplayName = FText::FromString(TEXT("QHD"));
	QHDOption.Resolution = FIntPoint(2560, 1440);
	ResolutionOptions.Add(QHDOption);
}

void ULocalPlayerSettingsSubsystem::InitializeSoundVolumeOptions()
{
	if (!SoundVolumeOptions.IsEmpty())
	{
		return;
	}

	auto AddSoundVolumeOption = [this](
		EOutlierAudioVolumeType VolumeType,
		const TCHAR* DisplayName)
	{
		FOutlierSoundVolumeOption Option;
		Option.VolumeType = VolumeType;
		Option.DisplayName = FText::FromString(DisplayName);
		Option.Value = 1.0f;
		SoundVolumeOptions.Add(Option);
	};

	AddSoundVolumeOption(EOutlierAudioVolumeType::Master, TEXT("Total Volume"));
	AddSoundVolumeOption(EOutlierAudioVolumeType::BGM, TEXT("BGM Volume"));
	AddSoundVolumeOption(EOutlierAudioVolumeType::SFX, TEXT("SFX Volume"));
	AddSoundVolumeOption(EOutlierAudioVolumeType::Voice, TEXT("Voice Volume"));
}

void ULocalPlayerSettingsSubsystem::SyncResolutionPresetFromGameUserSettings()
{
	UGameUserSettings* GameUserSettings = GEngine
		? GEngine->GetGameUserSettings()
		: nullptr;
	if (!GameUserSettings)
	{
		return;
	}

	const int32 ResolutionIndex =
		FindResolutionIndexBySize(GameUserSettings->GetScreenResolution());
	if (ResolutionOptions.IsValidIndex(ResolutionIndex))
	{
		CurrentResolutionPreset = ResolutionOptions[ResolutionIndex].Preset;
	}
}

bool ULocalPlayerSettingsSubsystem::ApplyResolutionPreset()
{
	const int32 ResolutionIndex = GetResolutionPresetIndex();
	if (!ResolutionOptions.IsValidIndex(ResolutionIndex))
	{
		return false;
	}

	UGameUserSettings* GameUserSettings = GEngine
		? GEngine->GetGameUserSettings()
		: nullptr;
	if (!GameUserSettings)
	{
		return false;
	}

	GameUserSettings->SetScreenResolution(ResolutionOptions[ResolutionIndex].Resolution);
	GameUserSettings->ApplySettings(false);
	GameUserSettings->SaveSettings();
	return true;
}

bool ULocalPlayerSettingsSubsystem::ApplySoundVolume(
	EOutlierAudioVolumeType VolumeType)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameInstance* GameInstance = LocalPlayer
		? LocalPlayer->GetGameInstance()
		: nullptr;
	if (!GameInstance)
	{
		return false;
	}

	UOutlierAudioSubsystem* AudioSubsystem =
		GameInstance->GetSubsystem<UOutlierAudioSubsystem>();
	if (!AudioSubsystem)
	{
		return false;
	}

	AudioSubsystem->SetVolumeMultiplier(VolumeType, GetSoundVolume(VolumeType));
	return true;
}

void ULocalPlayerSettingsSubsystem::ApplySoundVolumeOptions()
{
	for (const FOutlierSoundVolumeOption& Option : SoundVolumeOptions)
	{
		ApplySoundVolume(Option.VolumeType);
	}
}

int32 ULocalPlayerSettingsSubsystem::FindResolutionPresetIndex(
	EOutlierResolutionPreset Preset) const
{
	return ResolutionOptions.IndexOfByPredicate(
		[Preset](const FOutlierResolutionOption& Option)
		{
			return Option.Preset == Preset;
		});
}

int32 ULocalPlayerSettingsSubsystem::FindResolutionIndexBySize(FIntPoint Resolution) const
{
	return ResolutionOptions.IndexOfByPredicate(
		[Resolution](const FOutlierResolutionOption& Option)
		{
			return Option.Resolution == Resolution;
		});
}

int32 ULocalPlayerSettingsSubsystem::FindSoundVolumeIndex(
	EOutlierAudioVolumeType VolumeType) const
{
	return SoundVolumeOptions.IndexOfByPredicate(
		[VolumeType](const FOutlierSoundVolumeOption& Option)
		{
			return Option.VolumeType == VolumeType;
		});
}
