#include "UI/SettingWidget.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Input/OutlierInputBindingTable.h"
#include "InputCoreTypes.h"
#include "InputMappingQuery.h"
#include "InputMappingContext.h"
#include "UI/InputBindingRowWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/SettingSliderRowWidget.h"
#include "UserSettings/EnhancedInputUserSettings.h"

void USettingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	CacheWidgetArrays();

	if (GraphicButton)
	{
		GraphicButton->OnClicked.AddUniqueDynamic(
			this,
			&USettingWidget::HandleGraphicButtonClicked);
	}

	if (SoundButton)
	{
		SoundButton->OnClicked.AddUniqueDynamic(
			this,
			&USettingWidget::HandleSoundButtonClicked);
	}

	if (InputButton)
	{
		InputButton->OnClicked.AddUniqueDynamic(
			this,
			&USettingWidget::HandleInputButtonClicked);
	}

	if (GraphicLeftButton)
	{
		GraphicLeftButton->OnClicked.AddUniqueDynamic(
			this,
			&USettingWidget::HandleGraphicLeftButtonClicked);
	}

	if (GraphicRightButton)
	{
		GraphicRightButton->OnClicked.AddUniqueDynamic(
			this,
			&USettingWidget::HandleGraphicRightButtonClicked);
	}

	if (TotalVolumeRow)
	{
		TotalVolumeRow->OnValueChanged.AddDynamic(
			this,
			&USettingWidget::HandleTotalVolumeRowChanged);
	}

	if (BGMVolumeRow)
	{
		BGMVolumeRow->OnValueChanged.AddDynamic(
			this,
			&USettingWidget::HandleBGMVolumeRowChanged);
	}

	if (SFXVolumeRow)
	{
		SFXVolumeRow->OnValueChanged.AddDynamic(
			this,
			&USettingWidget::HandleSFXVolumeRowChanged);
	}

	if (VoiceVolumeRow)
	{
		VoiceVolumeRow->OnValueChanged.AddDynamic(
			this,
			&USettingWidget::HandleVoiceVolumeRowChanged);
	}
}

void USettingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);

	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
		{
			if (UOutlierAudioSubsystem* AudioSubsystem =
				GameInstance->GetSubsystem<UOutlierAudioSubsystem>())
			{
				AudioSubsystem->StopAllLocalAudio();
			}
		}
	}

	BindSettingsSubsystem();
	if (ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem())
	{
		PendingResolutionPresetIndex = SettingsSubsystem->GetResolutionPresetIndex();
	}

	SetActiveSettingPage(GraphicPageIndex);
	RefreshGraphicsSettings();
	RefreshSoundSettings();
}

void USettingWidget::NativeDestruct()
{
	UnbindSettingsSubsystem();
	ClearInputBindingInteractionState(false);

	Super::NativeDestruct();
}

FReply USettingWidget::NativeOnPreviewMouseButtonDown(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (CurrentSettingPageIndex == InputPageIndex
		&& !IsPointerOverInputBindingRow(InMouseEvent))
	{
		ClearInputBindingInteractionState(true);
	}

	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

FReply USettingWidget::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (bWaitingForInputRebind)
	{
		if (FPlatformTime::Seconds() - RebindStartTimeSeconds < 0.05)
		{
			return FReply::Handled();
		}

		TryCommitInputRebind(InKeyEvent.GetKey());
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USettingWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
}

bool USettingWidget::HandleUILayerEscape_Implementation()
{
	PopSelfFromLayer();
	return true;
}

bool USettingWidget::HandleUILayerConfirmed_Implementation()
{
	if (CurrentSettingPageIndex == GraphicPageIndex)
	{
		return ApplyPendingResolutionPreset();
	}

	if (CurrentSettingPageIndex == SoundPageIndex)
	{
		return false;
	}

	if (CurrentSettingPageIndex == InputPageIndex)
	{
		return StartInputRebind(HoveredInputBindingRow);
	}

	return false;
}

bool USettingWidget::HandleUILayerUp_Implementation()
{
	return false;
}

bool USettingWidget::HandleUILayerDown_Implementation()
{
	return false;
}

bool USettingWidget::HandleUILayerLeft_Implementation()
{
	return false;
}

bool USettingWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void USettingWidget::PopSelfFromLayer()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopWidget(this);
	}
}

bool USettingWidget::SetActiveSettingPage(int32 PageIndex)
{
	if (!SettingSwitcher
		|| PageIndex < 0
		|| PageIndex >= SettingSwitcher->GetNumWidgets())
	{
		return false;
	}

	SettingSwitcher->SetActiveWidgetIndex(PageIndex);
	CurrentSettingPageIndex = PageIndex;
	if (PageIndex == InputPageIndex)
	{
		RebuildInputBindingList();
	}

	return true;
}

bool USettingWidget::SetResolutionPresetIndex(int32 PresetIndex)
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem)
	{
		return false;
	}

	PendingResolutionPresetIndex = PresetIndex;
	RefreshGraphicsSettings();
	return true;
}

bool USettingWidget::OffsetResolutionPresetIndex(int32 IndexOffset)
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

	const int32 CurrentIndex = FMath::Max(
		SettingsSubsystem->GetResolutionPresetIndex(),
		0);
	const int32 WrappedIndex =
		(CurrentIndex + IndexOffset + ResolutionOptions.Num()) % ResolutionOptions.Num();
	return SetResolutionPresetIndex(WrappedIndex);
}

bool USettingWidget::SetSoundVolumeIndex(int32 VolumeIndex, float NewValue)
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

void USettingWidget::RefreshGraphicsSettings()
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
			TEXT("%s - %d x %d"),
			*ResolutionOption.DisplayName.ToString(),
			ResolutionOption.Resolution.X,
			ResolutionOption.Resolution.Y)));
	}
}

void USettingWidget::RefreshSoundSettings()
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (!SettingsSubsystem)
	{
		return;
	}

	const TArray<FOutlierSoundVolumeOption>& SoundVolumeOptions =
		SettingsSubsystem->GetSoundVolumeOptions();

	bRefreshingSoundSettings = true;
	for (int32 Index = 0; Index < SoundVolumeRows.Num(); ++Index)
	{
		USettingSliderRowWidget* SoundVolumeRow = SoundVolumeRows[Index];
		if (!SoundVolumeRow || !SoundVolumeOptions.IsValidIndex(Index))
		{
			continue;
		}

		SoundVolumeRow->SetColumnWidths(
			SoundLabelColumnWidth,
			SoundSliderColumnWidth,
			SoundValueColumnWidth);
		SoundVolumeRow->SetLabelText(SoundVolumeOptions[Index].DisplayName);
		SoundVolumeRow->SetValue(SoundVolumeOptions[Index].Value);
	}
	bRefreshingSoundSettings = false;
}

void USettingWidget::RebuildInputBindingList()
{
	if (bInputBindingListBuilt && !ActiveInputBindingRows.IsEmpty())
	{
		RefreshInputBindingDisplayedKeys();
		return;
	}

	if (!InputBindingListHost)
	{
		return;
	}

	InputBindingListHost->ClearChildren();
	ActiveInputBindingRows.Reset();

	if (!InputBindingTable || !InputBindingRowWidgetClass)
	{
		return;
	}

	APlayerController* OwningPlayer = GetOwningPlayer();
	if (!OwningPlayer)
	{
		return;
	}

	const TArray<FOutlierInputBindingTableRow> VisibleRows =
		InputBindingTable->GetVisibleRowsSorted();

	for (int32 RowIndex = 0; RowIndex < VisibleRows.Num(); ++RowIndex)
	{
		UInputBindingRowWidget* RowWidget = CreateWidget<UInputBindingRowWidget>(
			OwningPlayer,
			InputBindingRowWidgetClass);
		if (!RowWidget)
		{
			continue;
		}

		RowWidget->InitializeInputBindingRow(VisibleRows[RowIndex], RowIndex);
		RowWidget->SetDisplayedKey(ResolveInputBindingCurrentKey(VisibleRows[RowIndex]));
		RowWidget->OnRowHovered.AddUniqueDynamic(
			this,
			&USettingWidget::HandleInputBindingRowHovered);
		RowWidget->OnRebindRequested.AddUniqueDynamic(
			this,
			&USettingWidget::HandleInputBindingRebindRequested);
		ActiveInputBindingRows.Add(RowWidget);

		if (UCanvasPanel* CanvasPanel = Cast<UCanvasPanel>(InputBindingListHost))
		{
			if (UCanvasPanelSlot* CanvasSlot = CanvasPanel->AddChildToCanvas(RowWidget))
			{
				CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
				CanvasSlot->SetAlignment(FVector2D::ZeroVector);
				CanvasSlot->SetPosition(FVector2D(0.0f, InputBindingRowYOffset * RowIndex));
				CanvasSlot->SetAutoSize(true);
			}
			continue;
		}

		InputBindingListHost->AddChild(RowWidget);
	}

	bInputBindingListBuilt = !ActiveInputBindingRows.IsEmpty();
}

void USettingWidget::HandleGraphicButtonClicked()
{
	SetActiveSettingPage(GraphicPageIndex);
}

void USettingWidget::HandleSoundButtonClicked()
{
	SetActiveSettingPage(SoundPageIndex);
}

void USettingWidget::HandleInputButtonClicked()
{
	SetActiveSettingPage(InputPageIndex);
}

void USettingWidget::HandleGraphicLeftButtonClicked()
{
	OffsetResolutionPresetIndex(-1);
}

void USettingWidget::HandleGraphicRightButtonClicked()
{
	OffsetResolutionPresetIndex(1);
}

void USettingWidget::HandleTotalVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(0, NewValue);
}

void USettingWidget::HandleBGMVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(1, NewValue);
}

void USettingWidget::HandleSFXVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(2, NewValue);
}

void USettingWidget::HandleVoiceVolumeRowChanged(float NewValue)
{
	SetSoundVolumeIndex(3, NewValue);
}

void USettingWidget::HandleInputBindingRowHovered(
	UInputBindingRowWidget* RowWidget,
	int32 RowIndex)
{
	(void)RowIndex;
	HoveredInputBindingRow = RowWidget;
}

void USettingWidget::HandleInputBindingRebindRequested(
	UInputBindingRowWidget* RowWidget,
	int32 RowIndex)
{
	(void)RowIndex;
	StartInputRebind(RowWidget);
}

bool USettingWidget::ApplyPendingResolutionPreset()
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

bool USettingWidget::StartInputRebind(UInputBindingRowWidget* RowWidget)
{
	if (!RowWidget)
	{
		return false;
	}

	const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
	if (!Row.bRebindable)
	{
		return false;
	}

	PendingRebindInputBindingRow = RowWidget;
	bWaitingForInputRebind = true;
	RebindStartTimeSeconds = FPlatformTime::Seconds();
	SetKeyboardFocus();
	SetInputResult(WaitingForInputText, InputResultDefaultColor);
	return true;
}

bool USettingWidget::TryCommitInputRebind(FKey NewKey)
{
	UInputBindingRowWidget* RowWidget = PendingRebindInputBindingRow.Get();
	if (!RowWidget || !NewKey.IsValid() || NewKey == EKeys::AnyKey)
	{
		SetInputResult(InputRebindFailedText, InputResultFailureColor);
		bWaitingForInputRebind = false;
		PendingRebindInputBindingRow = nullptr;
		return false;
	}

	const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
	const bool bRestoringDefaultKey = NewKey == Row.DefaultKey;

	if (IsInputKeyDuplicate(NewKey, RowWidget))
	{
		SetInputResult(DuplicateInputText, InputResultFailureColor);
		bWaitingForInputRebind = false;
		PendingRebindInputBindingRow = nullptr;
		return false;
	}

	bool bMapped = false;
	if (!Row.bRebindable)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Input rebind rejected before query. Action=%s Context=%s NewKey=%s Reason=NotRebindable"),
			*GetNameSafe(Row.InputAction),
			*Row.MappingContext.ToSoftObjectPath().ToString(),
			*NewKey.ToString());
	}
	else if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			UEnhancedInputUserSettings* UserSettings =
				InputSubsystem->GetUserSettings();
			if (UserSettings)
			{
				if (UInputMappingContext* MappingContext =
					Row.MappingContext.LoadSynchronous())
				{
					if (!bRestoringDefaultKey)
					{
						TArray<UInputMappingContext*> QueryContexts;
						BuildInputBindingQueryContexts(QueryContexts);
						QueryContexts.AddUnique(MappingContext);

						TArray<FMappingQueryIssue> QueryIssues;
						const EMappingQueryResult QueryResult =
							InputSubsystem->QueryMapKeyInContextSet(
								QueryContexts,
							MappingContext,
							Row.InputAction,
							NewKey,
							QueryIssues,
							EMappingQueryIssue::ReservedByAction);
						if (QueryResult == EMappingQueryResult::NotMappable)
						{
							UE_LOG(
								LogTemp,
								Warning,
								TEXT("Input rebind query rejected. MappingName=%s Action=%s NewKey=%s IssueCount=%d"),
								*Row.MappingName.ToString(),
								*GetNameSafe(Row.InputAction),
								*NewKey.ToString(),
								QueryIssues.Num());

							SetInputResult(DuplicateInputText, InputResultFailureColor);
							bWaitingForInputRebind = false;
							PendingRebindInputBindingRow = nullptr;
							return false;
						}

						if (QueryResult != EMappingQueryResult::MappingAvailable)
						{
							UE_LOG(
								LogTemp,
								Warning,
								TEXT("Input rebind query failed. MappingName=%s Action=%s NewKey=%s QueryResult=%d"),
								*Row.MappingName.ToString(),
								*GetNameSafe(Row.InputAction),
								*NewKey.ToString(),
								static_cast<int32>(QueryResult));

							SetInputResult(InputRebindFailedText, InputResultFailureColor);
							bWaitingForInputRebind = false;
							PendingRebindInputBindingRow = nullptr;
							return false;
						}
					}

					UserSettings->RegisterInputMappingContext(MappingContext);
				}
				else
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("Input rebind failed before map. MappingName=%s Action=%s Context=%s NewKey=%s Reason=MissingMappingContext"),
						*Row.MappingName.ToString(),
						*GetNameSafe(Row.InputAction),
						*Row.MappingContext.ToSoftObjectPath().ToString(),
						*NewKey.ToString());
				}

				if (Row.MappingName.IsNone())
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("Input rebind failed before map. Action=%s Context=%s NewKey=%s Reason=MissingPlayerMappableMappingName"),
						*GetNameSafe(Row.InputAction),
						*Row.MappingContext.ToSoftObjectPath().ToString(),
						*NewKey.ToString());
				}
				else
				{
					FMapPlayerKeyArgs MapArgs;
					MapArgs.MappingName = Row.MappingName;
					MapArgs.Slot = EPlayerMappableKeySlot::First;
					MapArgs.NewKey = NewKey;
					MapArgs.bCreateMatchingSlotIfNeeded = true;

					FGameplayTagContainer FailureReason;
					if (bRestoringDefaultKey)
					{
						UserSettings->ResetAllPlayerKeysInRow(MapArgs, FailureReason);
					}
					else
					{
						UserSettings->MapPlayerKey(MapArgs, FailureReason);
					}
					bMapped = FailureReason.IsEmpty();
					if (bMapped)
					{
						UserSettings->ApplySettings();
						UserSettings->SaveSettings();
					}
					else
					{
						UE_LOG(
							LogTemp,
							Warning,
							TEXT("Input rebind failed. MappingName=%s Action=%s NewKey=%s FailureReason=%s"),
							*Row.MappingName.ToString(),
							*GetNameSafe(Row.InputAction),
							*NewKey.ToString(),
							*FailureReason.ToStringSimple());
					}
				}
			}
			else
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("Input rebind failed before query. MappingName=%s Action=%s NewKey=%s Reason=MissingEnhancedInputUserSettings"),
					*Row.MappingName.ToString(),
					*GetNameSafe(Row.InputAction),
					*NewKey.ToString());
			}
		}
		else
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Input rebind failed before query. MappingName=%s Action=%s NewKey=%s Reason=MissingEnhancedInputLocalPlayerSubsystem"),
				*Row.MappingName.ToString(),
				*GetNameSafe(Row.InputAction),
				*NewKey.ToString());
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Input rebind failed before query. MappingName=%s Action=%s NewKey=%s Reason=MissingOwningLocalPlayer"),
			*Row.MappingName.ToString(),
			*GetNameSafe(Row.InputAction),
			*NewKey.ToString());
	}

	if (!bMapped)
	{
		SetInputResult(InputRebindFailedText, InputResultFailureColor);
		bWaitingForInputRebind = false;
		PendingRebindInputBindingRow = nullptr;
		return false;
	}

	if (bRestoringDefaultKey)
	{
		RuntimeInputKeyOverrides.Remove(Row.MappingName);
	}
	else
	{
		RuntimeInputKeyOverrides.Add(Row.MappingName, NewKey);
	}
	RowWidget->SetDisplayedKey(NewKey);
	SetInputResult(InputChangedText, InputResultSuccessColor);
	bWaitingForInputRebind = false;
	PendingRebindInputBindingRow = nullptr;
	return true;
}

bool USettingWidget::IsPointerOverInputBindingRow(
	const FPointerEvent& InMouseEvent) const
{
	const FVector2D ScreenSpacePosition = InMouseEvent.GetScreenSpacePosition();
	for (const TObjectPtr<UInputBindingRowWidget>& RowWidgetPtr : ActiveInputBindingRows)
	{
		const UInputBindingRowWidget* RowWidget = RowWidgetPtr.Get();
		if (RowWidget && RowWidget->GetCachedGeometry().IsUnderLocation(ScreenSpacePosition))
		{
			return true;
		}
	}

	return false;
}

void USettingWidget::ClearInputBindingInteractionState(bool bClearResultText)
{
	bWaitingForInputRebind = false;
	PendingRebindInputBindingRow = nullptr;
	HoveredInputBindingRow = nullptr;

	if (bClearResultText)
	{
		SetInputResult(FText::GetEmpty(), InputResultDefaultColor);
	}
}

bool USettingWidget::TryGetUserSettingsKeyForRow(
	const FOutlierInputBindingTableRow& Row,
	FKey& OutKey) const
{
	if (Row.MappingName.IsNone())
	{
		return false;
	}

	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	UEnhancedInputUserSettings* UserSettings = InputSubsystem
		? InputSubsystem->GetUserSettings()
		: nullptr;
	if (!UserSettings)
	{
		return false;
	}

	if (UInputMappingContext* MappingContext = Row.MappingContext.LoadSynchronous())
	{
		UserSettings->RegisterInputMappingContext(MappingContext);
	}

	const FPlayerKeyMapping* CurrentMapping =
		UserSettings->FindCurrentMappingForSlot(
			Row.MappingName,
			EPlayerMappableKeySlot::First);
	if (!CurrentMapping || !CurrentMapping->IsValid())
	{
		return false;
	}

	OutKey = CurrentMapping->GetCurrentKey();
	return OutKey.IsValid() && OutKey != EKeys::AnyKey;
}

FKey USettingWidget::ResolveInputBindingCurrentKey(
	const FOutlierInputBindingTableRow& Row) const
{
	if (const FKey* OverrideKey = RuntimeInputKeyOverrides.Find(Row.MappingName))
	{
		return *OverrideKey;
	}

	FKey UserSettingsKey;
	if (TryGetUserSettingsKeyForRow(Row, UserSettingsKey))
	{
		return UserSettingsKey;
	}

	return Row.DefaultKey;
}

void USettingWidget::BuildInputBindingQueryContexts(
	TArray<UInputMappingContext*>& OutContexts) const
{
	OutContexts.Reset();

	if (!InputBindingTable)
	{
		return;
	}

	const TArray<FOutlierInputBindingTableRow> VisibleRows =
		InputBindingTable->GetVisibleRowsSorted();
	for (const FOutlierInputBindingTableRow& Row : VisibleRows)
	{
		if (UInputMappingContext* MappingContext = Row.MappingContext.LoadSynchronous())
		{
			OutContexts.AddUnique(MappingContext);
		}
	}
}

bool USettingWidget::IsInputKeyDuplicate(
	FKey NewKey,
	const UInputBindingRowWidget* IgnoredRowWidget) const
{
	if (!NewKey.IsValid() || NewKey == EKeys::AnyKey)
	{
		return false;
	}

	const FName IgnoredMappingName = IgnoredRowWidget
		? IgnoredRowWidget->GetInputBindingRow().MappingName
		: NAME_None;

	for (const TObjectPtr<UInputBindingRowWidget>& RowWidgetPtr : ActiveInputBindingRows)
	{
		const UInputBindingRowWidget* RowWidget = RowWidgetPtr.Get();
		if (!RowWidget || RowWidget == IgnoredRowWidget)
		{
			continue;
		}

		const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
		if (Row.MappingName == IgnoredMappingName)
		{
			continue;
		}

		if (IgnoredRowWidget
			&& Row.ConflictGroup != IgnoredRowWidget->GetInputBindingRow().ConflictGroup)
		{
			continue;
		}

		const FKey ExistingKey = ResolveInputBindingCurrentKey(Row);
		if (!ExistingKey.IsValid() || ExistingKey == EKeys::AnyKey)
		{
			continue;
		}

		if (ExistingKey == NewKey)
		{
			return true;
		}
	}

	return false;
}

void USettingWidget::RefreshInputBindingDisplayedKeys()
{
	for (const TObjectPtr<UInputBindingRowWidget>& RowWidgetPtr : ActiveInputBindingRows)
	{
		UInputBindingRowWidget* RowWidget = RowWidgetPtr.Get();
		if (!RowWidget)
		{
			continue;
		}

		const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
		RowWidget->SetDisplayedKey(ResolveInputBindingCurrentKey(Row));
	}
}

void USettingWidget::SetInputResult(
	const FText& ResultText,
	const FLinearColor& ResultColor)
{
	if (!InputResultText)
	{
		return;
	}

	InputResultText->SetText(ResultText);
	InputResultText->SetColorAndOpacity(FSlateColor(ResultColor));
}

void USettingWidget::CacheWidgetArrays()
{
	SettingTabButtons = {
		GraphicButton,
		SoundButton,
		InputButton
	};

	SoundVolumeRows = {
		TotalVolumeRow,
		BGMVolumeRow,
		SFXVolumeRow,
		VoiceVolumeRow
	};
}

void USettingWidget::BindSettingsSubsystem()
{
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem();
	if (BoundSettingsSubsystem == SettingsSubsystem)
	{
		return;
	}

	UnbindSettingsSubsystem();
	BoundSettingsSubsystem = SettingsSubsystem;

	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnResolutionPresetChanged.AddUniqueDynamic(
			this,
			&USettingWidget::HandleResolutionPresetChanged);
		BoundSettingsSubsystem->OnSoundVolumeChanged.AddUniqueDynamic(
			this,
			&USettingWidget::HandleSoundVolumeChanged);
	}
}

void USettingWidget::UnbindSettingsSubsystem()
{
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnResolutionPresetChanged.RemoveAll(this);
		BoundSettingsSubsystem->OnSoundVolumeChanged.RemoveAll(this);
	}

	BoundSettingsSubsystem = nullptr;
}

void USettingWidget::HandleResolutionPresetChanged(
	EOutlierResolutionPreset NewPreset)
{
	(void)NewPreset;
	RefreshGraphicsSettings();
}

void USettingWidget::HandleSoundVolumeChanged(
	EOutlierAudioVolumeType VolumeType,
	float NewValue)
{
	(void)VolumeType;
	(void)NewValue;
	RefreshSoundSettings();
}

ULocalPlayerSettingsSubsystem* USettingWidget::GetSettingsSubsystem() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerSettingsSubsystem>()
		: nullptr;
}
