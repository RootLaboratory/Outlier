#include "UI/SettingGraphicPageWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void USettingGraphicPageWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (GraphicLeftButton) GraphicLeftButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleGraphicLeftButtonClicked);
	if (GraphicRightButton) GraphicRightButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleGraphicRightButtonClicked);
}

void USettingGraphicPageWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (IsDesignTime() && ResolutionText)
	{
		ResolutionText->SetText(FText::FromString(TEXT("1920x1080")));
	}
}

void USettingGraphicPageWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BoundSettingsSubsystem = GetSettingsSubsystem();
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnResolutionPresetChanged.AddUniqueDynamic(this, &ThisClass::HandleResolutionPresetChanged);
		PendingResolutionPresetIndex = BoundSettingsSubsystem->GetResolutionPresetIndex();
	}
	RefreshGraphicsSettings();
}

void USettingGraphicPageWidget::NativeDestruct()
{
	if (BoundSettingsSubsystem) BoundSettingsSubsystem->OnResolutionPresetChanged.RemoveAll(this);
	BoundSettingsSubsystem = nullptr;
	Super::NativeDestruct();
}

void USettingGraphicPageWidget::ActivatePage()
{
	RefreshGraphicsSettings();
	Super::ActivatePage();
}

void USettingGraphicPageWidget::RebuildKeyboardFocusTargets()
{
	KeyboardFocusTargets.Reset();
	if (GraphicLeftButton) KeyboardFocusTargets.Add(GraphicLeftButton);
}

bool USettingGraphicPageWidget::HandlePageInput(ESettingPageInput Input)
{
	if (Input == ESettingPageInput::Confirm) return ApplyPendingResolutionPreset();
	if (Input == ESettingPageInput::Left) return OffsetResolutionPresetIndex(-1);
	if (Input == ESettingPageInput::Right) return OffsetResolutionPresetIndex(1);
	return Super::HandlePageInput(Input);
}

bool USettingGraphicPageWidget::SetResolutionPresetIndex(int32 PresetIndex)
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem)
	{
		return false;
	}

	if (!SettingsSubsystem->GetResolutionOptions().IsValidIndex(PresetIndex))
	{
		return false;
	}

	PendingResolutionPresetIndex = PresetIndex;
	RefreshGraphicsSettings();
	return true;
}

bool USettingGraphicPageWidget::OffsetResolutionPresetIndex(int32 IndexOffset)
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem)
	{
		return false;
	}

	const TArray<FOutlierResolutionOption>& ResolutionOptions =
		SettingsSubsystem->GetResolutionOptions();
	if (ResolutionOptions.IsEmpty())
	{
		return false;
	}

	const int32 CurrentIndex = ResolutionOptions.IsValidIndex(PendingResolutionPresetIndex)
		? PendingResolutionPresetIndex
		: FMath::Max(SettingsSubsystem->GetResolutionPresetIndex(), 0);
	const int32 OffsetIndex = (CurrentIndex + IndexOffset) % ResolutionOptions.Num();
	const int32 WrappedIndex = OffsetIndex < 0
		? OffsetIndex + ResolutionOptions.Num()
		: OffsetIndex;
	return SetResolutionPresetIndex(WrappedIndex);
}

void USettingGraphicPageWidget::RefreshGraphicsSettings()
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem)
	{
		return;
	}

	const TArray<FOutlierResolutionOption>& ResolutionOptions =
		SettingsSubsystem->GetResolutionOptions();
	if (!ResolutionOptions.IsValidIndex(PendingResolutionPresetIndex))
	{
		PendingResolutionPresetIndex = SettingsSubsystem->GetResolutionPresetIndex();
	}

	if (ResolutionText && ResolutionOptions.IsValidIndex(PendingResolutionPresetIndex))
	{
		const FOutlierResolutionOption& ResolutionOption =
			ResolutionOptions[PendingResolutionPresetIndex];
		ResolutionText->SetText(FText::FromString(FString::Printf(
			TEXT("%dx%d"),
			ResolutionOption.Resolution.X,
			ResolutionOption.Resolution.Y)));
	}
}

void USettingGraphicPageWidget::HandleGraphicLeftButtonClicked()
{
	OffsetResolutionPresetIndex(-1);
}

void USettingGraphicPageWidget::HandleGraphicRightButtonClicked()
{
	OffsetResolutionPresetIndex(1);
}

bool USettingGraphicPageWidget::ApplyPendingResolutionPreset()
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem || PendingResolutionPresetIndex == INDEX_NONE)
	{
		return false;
	}

	const bool bApplied =
		SettingsSubsystem->SetResolutionPresetByIndex(PendingResolutionPresetIndex, true);
	RefreshGraphicsSettings();
	return bApplied;
}

void USettingGraphicPageWidget::HandleResolutionPresetChanged(
	EOutlierResolutionPreset NewPreset)
{
	(void)NewPreset;
	RefreshGraphicsSettings();
}
