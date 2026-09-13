#pragma once

#include "CoreMinimal.h"
#include "Audio/OutlierAudioTypes.h"
#include "InputCoreTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "LocalPlayerSettingsSubsystem.generated.h"

class UInputAction;

UENUM(BlueprintType)
enum class EOutlierResolutionPreset : uint8
{
	FHD UMETA(DisplayName = "FHD"),
	QHD UMETA(DisplayName = "QHD")
};

USTRUCT(BlueprintType)
struct FOutlierResolutionOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	EOutlierResolutionPreset Preset = EOutlierResolutionPreset::FHD;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	FIntPoint Resolution = FIntPoint(1920, 1080);
};

USTRUCT(BlueprintType)
struct FOutlierSoundVolumeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::Master;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Value = 1.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnOutlierResolutionPresetChanged,
	EOutlierResolutionPreset,
	NewPreset);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnOutlierSoundVolumeChanged,
	EOutlierAudioVolumeType,
	VolumeType,
	float,
	NewValue);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnOutlierMouseSensitivityChanged,
	float,
	NewValue);

// Setting 화면에서 특정 Input Action의 바인딩 키가 바뀌었을 때 브로드캐스트된다.
// SettingWidget 인스턴스가 살아있지 않아도(예: HintKey류 위젯) 구독할 수 있도록
// SettingWidget이 아니라 이 LocalPlayer 상시 Subsystem에서 쏜다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnOutlierInputActionKeyChanged,
	UInputAction*,
	InputAction,
	FKey,
	NewKey);

UCLASS()
class OUTLIER_API ULocalPlayerSettingsSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "Settings|Graphics")
	EOutlierResolutionPreset GetResolutionPreset() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Graphics")
	int32 GetResolutionPresetIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	bool SetResolutionPreset(EOutlierResolutionPreset NewPreset, bool bApplyImmediately = true);

	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	bool SetResolutionPresetByIndex(int32 NewIndex, bool bApplyImmediately = true);

	UFUNCTION(BlueprintPure, Category = "Settings|Graphics")
	const TArray<FOutlierResolutionOption>& GetResolutionOptions() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Sound")
	const TArray<FOutlierSoundVolumeOption>& GetSoundVolumeOptions() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Sound")
	float GetSoundVolume(EOutlierAudioVolumeType VolumeType) const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Sound")
	bool SetSoundVolume(EOutlierAudioVolumeType VolumeType, float NewValue, bool bApplyImmediately = true);

	UFUNCTION(BlueprintCallable, Category = "Settings|Sound")
	bool SetSoundVolumeByIndex(int32 VolumeIndex, float NewValue, bool bApplyImmediately = true);

	UFUNCTION(BlueprintPure, Category = "Settings|Input")
	float GetMouseSensitivity() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Input")
	bool SetMouseSensitivity(float NewValue);

	// SettingWidget이 리바인드를 커밋한 뒤 호출해서, 같은 IA를 보고 있는 모든
	// 키 표시 위젯(UInputActionKeyDisplayWidget 등)에게 갱신하라고 알린다.
	UFUNCTION(BlueprintCallable, Category = "Settings|Input")
	void NotifyInputActionKeyChanged(UInputAction* InputAction, FKey NewKey);

	UPROPERTY(BlueprintAssignable, Category = "Settings|Graphics")
	FOnOutlierResolutionPresetChanged OnResolutionPresetChanged;

	UPROPERTY(BlueprintAssignable, Category = "Settings|Sound")
	FOnOutlierSoundVolumeChanged OnSoundVolumeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Settings|Input")
	FOnOutlierMouseSensitivityChanged OnMouseSensitivityChanged;

	UPROPERTY(BlueprintAssignable, Category = "Settings|Input")
	FOnOutlierInputActionKeyChanged OnInputActionKeyChanged;

private:
	void InitializeResolutionOptions();
	void InitializeSoundVolumeOptions();
	void SyncResolutionPresetFromGameUserSettings();
	bool ApplyResolutionPreset();
	bool ApplySoundVolume(EOutlierAudioVolumeType VolumeType);
	void ApplySoundVolumeOptions();
	int32 FindResolutionPresetIndex(EOutlierResolutionPreset Preset) const;
	int32 FindResolutionIndexBySize(FIntPoint Resolution) const;
	int32 FindSoundVolumeIndex(EOutlierAudioVolumeType VolumeType) const;

	UPROPERTY(Transient)
	TArray<FOutlierResolutionOption> ResolutionOptions;

	UPROPERTY(Transient)
	TArray<FOutlierSoundVolumeOption> SoundVolumeOptions;

	UPROPERTY(Transient)
	EOutlierResolutionPreset CurrentResolutionPreset = EOutlierResolutionPreset::FHD;

	UPROPERTY(Transient)
	float MouseSensitivity = 1.0f;
};
