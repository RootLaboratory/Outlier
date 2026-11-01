#include "Settings/LocalPlayerSettingsSubsystem.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "GameFramework/GameUserSettings.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "FrontendPlayerController.h"
#include "OutlierPlayerState.h"

void ULocalPlayerSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UEnhancedInputLocalPlayerSubsystem>();
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		// 입력 설정은 PlayerInput 생성 후 로드되므로 Subsystem 초기화 순서만으로 준비를 가정하지 않는다.
		InputSubsystem->OnPostUserSettingsInitialized.AddUniqueDynamic(this, &ThisClass::HandleInputUserSettingsInitialized);
		if (InputSubsystem->GetUserSettings()) HandleInputUserSettingsInitialized(InputSubsystem->GetUserSettings());
	}

	InitializeResolutionOptions();
	InitializeSoundVolumeOptions();
	SyncResolutionPresetFromGameUserSettings();
	ApplySoundVolumeOptions();
}

void ULocalPlayerSettingsSubsystem::Deinitialize()
{
	UnbindInputSettingsPlayerState();
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		InputSubsystem->OnPostUserSettingsInitialized.RemoveAll(this);
	}
	InputUserSettings = nullptr;
	Super::Deinitialize();
}

void ULocalPlayerSettingsSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	RefreshInputSettingsContext();
}

void ULocalPlayerSettingsSubsystem::UnbindInputSettingsPlayerState()
{
	if (AOutlierPlayerState* PlayerState = InputSettingsPlayerState.Get())
	{
		PlayerState->OnPlayerRoleChanged.RemoveAll(this);
		PlayerState->OnPendingLobbyStateChanged.RemoveAll(this);
	}
	InputSettingsPlayerState.Reset();
}

void ULocalPlayerSettingsSubsystem::RefreshInputSettingsContext()
{
	APlayerController* Controller = GetLocalPlayer()->PlayerController;
	AOutlierPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const bool bOwnerChanged = InputSettingsController.Get() != Controller || InputSettingsPlayerState.Get() != PlayerState;
	if (InputSettingsPlayerState.Get() != PlayerState)
	{
		UnbindInputSettingsPlayerState();
		InputSettingsPlayerState = PlayerState;
		if (PlayerState)
		{
			PlayerState->OnPlayerRoleChanged.AddUObject(this, &ThisClass::HandleInputSettingsRoleChanged);
			PlayerState->OnPendingLobbyStateChanged.AddUObject(this, &ThisClass::HandleInputSettingsRoleChanged);
		}
	}
	InputSettingsController = Controller;
	// 로비 선택 역할은 Pawn/게임 역할보다 먼저 확정된다. 표시 컨텍스트만 변경한다.
	const EOutlierPlayerRole NewRole = !PlayerState ? EOutlierPlayerRole::None
		: Controller->IsA<AFrontendPlayerController>() ? PlayerState->GetPendingLobbyRole() : PlayerState->GetPlayerRole();
	if (bOwnerChanged || InputSettingsRole != NewRole)
	{
		InputSettingsRole = NewRole;
		++InputSettingsContextRevision;
		OnInputSettingsContextChanged.Broadcast(NewRole);
	}
}

void ULocalPlayerSettingsSubsystem::HandleInputSettingsRoleChanged(AOutlierPlayerState* PlayerState)
{
	if (PlayerState == InputSettingsPlayerState.Get()) RefreshInputSettingsContext();
}

void ULocalPlayerSettingsSubsystem::HandleInputUserSettingsInitialized(const UEnhancedInputUserSettings* Settings)
{
	// 알림 인자는 읽기 전용이므로, 변경 가능한 설정은 소유 Subsystem의 접근자로 가져온다.
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	InputUserSettings = InputSubsystem ? Cast<UOutlierInputUserSettings>(InputSubsystem->GetUserSettings()) : nullptr;
	if (!InputUserSettings)
	{
		UE_LOG(LogTemp, Error, TEXT("Sprint settings unavailable: Enhanced Input UserSettingsClass must be OutlierInputUserSettings."));
	}
}

EOutlierSprintInputMode ULocalPlayerSettingsSubsystem::GetSprintInputMode(EOutlierPlayerRole Role) const
{
	return InputUserSettings ? InputUserSettings->GetSprintInputMode(Role) : EOutlierSprintInputMode::Toggle;
}

bool ULocalPlayerSettingsSubsystem::SetSprintInputMode(EOutlierPlayerRole Role, EOutlierSprintInputMode Mode)
{
	if (!InputUserSettings) return false;
	const EOutlierSprintInputMode PreviousMode = InputUserSettings->GetSprintInputMode(Role);
	if (!InputUserSettings->SetSprintInputMode(Role, Mode)) return false;
	if (PreviousMode == Mode) return true;

	// 키 설정과 동일한 저장 경로를 사용하며, 구독자는 변경된 역할만 재평가한다.
	InputUserSettings->SaveSettings();
	OnSprintInputModeChanged.Broadcast(Role, Mode);
	return true;
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

float ULocalPlayerSettingsSubsystem::GetMouseSensitivity() const
{
	return MouseSensitivity;
}

bool ULocalPlayerSettingsSubsystem::SetMouseSensitivity(float NewValue)
{
	const float ClampedValue = FMath::Clamp(NewValue, 0.05f, 2.0f);
	if (FMath::IsNearlyEqual(MouseSensitivity, ClampedValue))
	{
		return true;
	}

	MouseSensitivity = ClampedValue;
	OnMouseSensitivityChanged.Broadcast(MouseSensitivity);
	return true;
}

void ULocalPlayerSettingsSubsystem::NotifyInputActionKeyChanged(
	UInputAction* InputAction,
	FKey NewKey)
{
	if (!InputAction)
	{
		return;
	}

	OnInputActionKeyChanged.Broadcast(InputAction, NewKey);
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

	const FOutlierResolutionOption& ResolutionOption = ResolutionOptions[ResolutionIndex];
	GameUserSettings->SetScreenResolution(ResolutionOption.Resolution);
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
