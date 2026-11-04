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
#include "Input/OutlierInputBindingSettings.h"
#include "Input/OutlierInputBindingTable.h"
#include "InputMappingContext.h"

void ULocalPlayerSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UEnhancedInputLocalPlayerSubsystem>();
	InputBindingDefinitions = GetDefault<UOutlierInputBindingSettings>()->GeneratedBindingTable.LoadSynchronous();
	if (InputBindingDefinitions)
	{
		InputBindingDefinitions->OnRowsChanged.AddUObject(this, &ThisClass::SynchronizeLinkedInputBindings);
	}
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
	UnbindInputUserSettings();
	if (InputBindingDefinitions) InputBindingDefinitions->OnRowsChanged.RemoveAll(this);
	InputBindingDefinitions = nullptr;
	LinkedInputBindingErrors.Reset();
	OnLinkedInputBindingsChanged.Clear();
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
	UnbindInputUserSettings();
	// 알림 인자는 읽기 전용이므로, 변경 가능한 설정은 소유 Subsystem의 접근자로 가져온다.
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	InputUserSettings = InputSubsystem ? Cast<UOutlierInputUserSettings>(InputSubsystem->GetUserSettings()) : nullptr;
	if (!InputUserSettings)
	{
		UE_LOG(LogTemp, Error, TEXT("Sprint settings unavailable: Enhanced Input UserSettingsClass must be OutlierInputUserSettings."));
	}
	else
	{
		InputUserSettings->OnKeyProfileChanged.AddUniqueDynamic(this, &ThisClass::HandleInputKeyProfileChanged);
		InputUserSettings->OnMappingContextRegistered.AddUniqueDynamic(this, &ThisClass::HandleInputMappingContextRegistered);
		SynchronizeLinkedInputBindings();
	}
}

namespace
{
	FString LinkedBindingId(const FOutlierInputBindingTableRow& Row)
	{
		return FString::Printf(TEXT("%s|%s|%d"), *Row.MappingContext.ToSoftObjectPath().ToString(),
			*Row.MappingName.ToString(), static_cast<int32>(Row.Slot));
	}
}

void ULocalPlayerSettingsSubsystem::UnbindInputUserSettings()
{
	if (!InputUserSettings) return;
	InputUserSettings->OnKeyProfileChanged.RemoveAll(this);
	InputUserSettings->OnMappingContextRegistered.RemoveAll(this);
}

void ULocalPlayerSettingsSubsystem::HandleInputKeyProfileChanged(const UEnhancedPlayerMappableKeyProfile* Profile)
{
	// 같은 역할이어도 저장 대상 프로필이 바뀌면 이전 프로필에서 시작한 입력 캡처를 취소한다.
	++InputSettingsContextRevision;
	OnInputSettingsContextChanged.Broadcast(InputSettingsRole);
	SynchronizeLinkedInputBindings();
}

void ULocalPlayerSettingsSubsystem::HandleInputMappingContextRegistered(const UInputMappingContext* Context)
{
	SynchronizeLinkedInputBindings();
}

FString ULocalPlayerSettingsSubsystem::GetLinkedInputBindingError(const FOutlierInputBindingTableRow& Row) const
{
	const FString* Error = LinkedInputBindingErrors.Find(LinkedBindingId(Row));
	return Error ? *Error : FString();
}

void ULocalPlayerSettingsSubsystem::NotifyLinkedInputBindingChanged(const FOutlierInputBindingTableRow& Row, FKey Key)
{
	TArray<FOutlierInputBindingTableRow> Members;
	FString Error;
	if (!InputBindingDefinitions || !InputBindingDefinitions->ResolveLinkedRows(Row, Members, Error)) return;
	TSet<UInputAction*> Notified;
	for (const auto& Member : Members)
	{
		if (Member.InputAction && !Notified.Contains(Member.InputAction))
		{
			Notified.Add(Member.InputAction);
			NotifyInputActionKeyChanged(Member.InputAction, Key);
		}
	}
}

void ULocalPlayerSettingsSubsystem::SynchronizeLinkedInputBindings()
{
	if (bSynchronizingLinkedBindings || !InputUserSettings || !InputBindingDefinitions) return;
	TGuardValue<bool> SyncGuard(bSynchronizingLinkedBindings, true);
	LinkedInputBindingErrors.Reset();
	// 화면 생성보다 먼저 전체 IMC를 등록한다. 등록 중 발생하는 재진입 알림은 Guard로 묶는다.
	InputUserSettings->RegisterBindingContexts(InputBindingDefinitions);
	bool bChanged = false;
	TArray<TPair<FOutlierInputBindingTableRow, FKey>> Changes;
	for (const auto& Row : InputBindingDefinitions->GetAllRowsSorted())
	{
		if (Row.LinkedMappings.IsEmpty()) continue;
		FKey Key;
		bool bNeedsSync = false;
		FString Error;
		if (!InputUserSettings->ChooseLinkedBindingKey(InputBindingDefinitions, Row, Key, bNeedsSync, Error))
		{
			if (Error.IsEmpty()) Error = TEXT("연결 매핑 또는 사용자 프로필이 준비되지 않았습니다.");
			LinkedInputBindingErrors.Add(LinkedBindingId(Row), Error);
			continue;
		}
		const EOutlierLinkedBindingResult Result = bNeedsSync
			? InputUserSettings->ChangeLinkedBinding(InputBindingDefinitions, Row, Key, Error)
			: InputUserSettings->ValidateLinkedBinding(InputBindingDefinitions, Row, Key, Error);
		if (Result != EOutlierLinkedBindingResult::Success)
		{
			if (Error.IsEmpty()) Error = TEXT("연결 키를 적용할 수 없습니다. 유효한 키를 지정해 주세요.");
			LinkedInputBindingErrors.Add(LinkedBindingId(Row), Error);
		}
		else
		{
			bChanged |= bNeedsSync;
			Changes.Emplace(Row, Key);
		}
	}
	if (!Changes.IsEmpty())
	{
		// 프로필 전환 때 양쪽 값이 이미 같아도 힌트는 새 프로필의 키로 갱신한다.
		// 같은 값의 재초기화에서는 저장 파일을 다시 쓰지 않는다.
		ApplyLinkedInputBindings(bChanged);
		for (const auto& Change : Changes) NotifyLinkedInputBindingChanged(Change.Key, Change.Value);
	}
	OnLinkedInputBindingsChanged.Broadcast();
}

EOutlierLinkedBindingResult ULocalPlayerSettingsSubsystem::ChangeLinkedInputBinding(
	const UOutlierInputBindingTable* Table, const FOutlierInputBindingTableRow& Row, FKey NewKey, FString& OutError)
{
	if (bSynchronizingLinkedBindings || !InputUserSettings || Table != InputBindingDefinitions)
	{
		OutError = TEXT("사용자 설정 또는 공용 입력 테이블이 준비되지 않았습니다.");
		return EOutlierLinkedBindingResult::Unavailable;
	}
	TGuardValue<bool> SyncGuard(bSynchronizingLinkedBindings, true);
	const auto Result = InputUserSettings->ChangeLinkedBinding(Table, Row, NewKey, OutError);
	if (Result != EOutlierLinkedBindingResult::Success) return Result;
	LinkedInputBindingErrors.Remove(LinkedBindingId(Row));
	ApplyLinkedInputBindings(true);
	NotifyLinkedInputBindingChanged(Row, NewKey);
	OnLinkedInputBindingsChanged.Broadcast();
	return Result;
}

void ULocalPlayerSettingsSubsystem::ApplyLinkedInputBindings(bool bSave)
{
	InputUserSettings->OnSettingsChanged.Broadcast(InputUserSettings);
	InputUserSettings->ApplySettings();
	if (auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		// 힌트 위젯은 통지 즉시 활성 키를 조회한다. 재빌드를 마친 뒤 통지하여 이전 키가 남지 않게 한다.
		FModifyContextOptions Options;
		Options.bForceImmediately = true;
		Input->RequestRebuildControlMappings(Options);
	}
	// SaveSettings는 반환값이 없으므로 디스크 저장 성공으로 단정하지 않는다.
	if (bSave) InputUserSettings->SaveSettings();
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
