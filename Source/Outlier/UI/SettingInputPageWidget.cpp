#include "UI/SettingInputPageWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Input/OutlierInputBindingTable.h"
#include "InputMappingQuery.h"
#include "InputMappingContext.h"
#include "UI/InputBindingRowWidget.h"
#include "UI/MouseSensitivityWidget.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"

// Capture before UMG buttons/scroll boxes consume the event or cancel the pending row.
class FSettingInputRebindPreProcessor : public IInputProcessor
{
public:
	explicit FSettingInputRebindPreProcessor(USettingInputPageWidget* InPage) : Page(InPage) {}
	virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
	{
		return Page.IsValid() && Page->HandleInputRebindPointerKey(Event.GetEffectingButton());
	}
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& App, const FPointerEvent& Event) override
	{
		return HandleMouseButtonDownEvent(App, Event);
	}
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& Event,
		const FPointerEvent*) override
	{
		if (Event.GetWheelDelta() == 0.0f) return false;
		return Page.IsValid() && Page->HandleInputRebindPointerKey(
			Event.GetWheelDelta() > 0.0f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
	}
private:
	TWeakObjectPtr<USettingInputPageWidget> Page;
};

USettingInputPageWidget::USettingInputPageWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PageScrollBarThumbLength = 166.0f;
}

void USettingInputPageWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (IsDesignTime()) RebuildInputBindingList();
}

void USettingInputPageWidget::ActivatePage()
{
	ClearInputBindingInteractionState(true);
	RebuildInputBindingList();
	Super::ActivatePage();
}

void USettingInputPageWidget::DeactivatePage()
{
	if (MouseSensitivityWidget) MouseSensitivityWidget->ConfirmPendingValue();
	ClearInputBindingInteractionState(true);
	Super::DeactivatePage();
}

void USettingInputPageWidget::NativeDestruct()
{
	ClearInputBindingInteractionState(false);
	Super::NativeDestruct();
}

void USettingInputPageWidget::RebuildKeyboardFocusTargets()
{
	KeyboardFocusTargets.Reset();
	if (MouseSensitivityWidget) KeyboardFocusTargets.Add(MouseSensitivityWidget);
	for (UInputBindingRowWidget* Row : ActiveInputBindingRows)
	{
		if (Row && Row->GetInputBindingRow().bRebindable)
		{
			Row->SetIsFocusable(true);
			KeyboardFocusTargets.Add(Row);
		}
	}
}

void USettingInputPageWidget::FocusCurrentKeyboardTarget()
{
	HoveredInputBindingRow = KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex)
		? Cast<UInputBindingRowWidget>(KeyboardFocusTargets[CurrentFocusedIndex]) : nullptr;
	Super::FocusCurrentKeyboardTarget();
}

bool USettingInputPageWidget::HandlePageInput(ESettingPageInput Input)
{
	if (bWaitingForInputRebind)
	{
		if (Input == ESettingPageInput::Escape) ClearInputBindingInteractionState(true);
		return true;
	}
	if (Input == ESettingPageInput::Confirm)
	{
		if (KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex)
			&& KeyboardFocusTargets[CurrentFocusedIndex] == MouseSensitivityWidget)
			return MouseSensitivityWidget->ConfirmPendingValue();
		return StartInputRebind(HoveredInputBindingRow);
	}
	if ((Input == ESettingPageInput::Left || Input == ESettingPageInput::Right)
		&& KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex)
		&& KeyboardFocusTargets[CurrentFocusedIndex] == MouseSensitivityWidget && MouseSensitivityWidget)
	{
		MouseSensitivityWidget->OffsetPendingValue(Input == ESettingPageInput::Right ? 0.01f : -0.01f);
		return true;
	}
	return Super::HandlePageInput(Input);
}

bool USettingInputPageWidget::HandlePageKeyDown(const FKeyEvent& KeyEvent)
{
	if (!bWaitingForInputRebind) return false;
	if (FPlatformTime::Seconds() - RebindStartTimeSeconds < 0.05) return true;
	if (KeyEvent.GetKey() == EKeys::Escape) ClearInputBindingInteractionState(true);
	else TryCommitInputRebind(KeyEvent.GetKey());
	return true;
}

void USettingInputPageWidget::HandlePagePointerDown(const FPointerEvent& PointerEvent)
{
	Super::HandlePagePointerDown(PointerEvent);
	if (!IsPointerOverInputBindingRow(PointerEvent)) ClearInputBindingInteractionState(true);
	else if (KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex))
		HoveredInputBindingRow = Cast<UInputBindingRowWidget>(KeyboardFocusTargets[CurrentFocusedIndex]);
}

void USettingInputPageWidget::RebuildInputBindingList()
{
	if (!IsDesignTime() && bInputBindingListBuilt && !ActiveInputBindingRows.IsEmpty())
	{
		ApplyInputBindingRowPadding();
		RefreshInputBindingDisplayedKeys();
		return;
	}

	if (!InputBindingListHost)
	{
		return;
	}

	InputBindingListHost->ClearChildren();
	ActiveInputBindingRows.Reset();
	bInputBindingListBuilt = false;

	if (!InputBindingTable || !InputBindingRowWidgetClass)
	{
		return;
	}

	APlayerController* OwningPlayer = GetOwningPlayer();
	if (!IsDesignTime() && !OwningPlayer)
	{
		return;
	}

	const TArray<FOutlierInputBindingTableRow> VisibleRows =
		InputBindingTable->GetVisibleRowsSorted();

	for (int32 RowIndex = 0; RowIndex < VisibleRows.Num(); ++RowIndex)
	{
		// Owning the preview rows through this widget propagates designer flags without a player.
		UInputBindingRowWidget* RowWidget = IsDesignTime()
			? CreateWidget<UInputBindingRowWidget>(this, InputBindingRowWidgetClass)
			: CreateWidget<UInputBindingRowWidget>(OwningPlayer, InputBindingRowWidgetClass);
		if (!RowWidget)
		{
			continue;
		}

		RowWidget->InitializeInputBindingRow(VisibleRows[RowIndex], RowIndex);
		RowWidget->SetDisplayedKey(ResolveInputBindingCurrentKey(VisibleRows[RowIndex]));
		if (!IsDesignTime())
		{
			RowWidget->OnRowHovered.AddUniqueDynamic(
				this,
				&USettingInputPageWidget::HandleInputBindingRowHovered);
			RowWidget->OnRebindRequested.AddUniqueDynamic(
				this,
				&USettingInputPageWidget::HandleInputBindingRebindRequested);
		}
		ActiveInputBindingRows.Add(RowWidget);

		if (UCanvasPanel* CanvasPanel = Cast<UCanvasPanel>(InputBindingListHost))
		{
			if (UCanvasPanelSlot* CanvasSlot = CanvasPanel->AddChildToCanvas(RowWidget))
			{
				CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
				CanvasSlot->SetAlignment(FVector2D::ZeroVector);
				CanvasSlot->SetAutoSize(true);
			}
			continue;
		}

		InputBindingListHost->AddChild(RowWidget);
	}

	bInputBindingListBuilt = !IsDesignTime() && !ActiveInputBindingRows.IsEmpty();
	ApplyInputBindingRowPadding();
}

void USettingInputPageWidget::SetInputBindingRowPadding(FMargin NewPadding)
{
	InputBindingRowPadding = NewPadding;
	ApplyInputBindingRowPadding();
}

void USettingInputPageWidget::ApplyInputBindingRowPadding()
{
	const float CanvasRowStride = InputBindingRowYOffset
		+ InputBindingRowPadding.Top + InputBindingRowPadding.Bottom;
	for (UInputBindingRowWidget* RowWidget : ActiveInputBindingRows)
	{
		if (!RowWidget)
		{
			continue;
		}

		RowWidget->SetPadding(InputBindingRowPadding);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RowWidget->Slot))
		{
			// Canvas children use fixed positions, so include padding in the stride too.
			CanvasSlot->SetPosition(FVector2D(
				0.0f, CanvasRowStride * RowWidget->GetInputBindingRowIndex()));
		}
	}
}

void USettingInputPageWidget::HandleInputBindingRowHovered(
	UInputBindingRowWidget* RowWidget,
	int32 RowIndex)
{
	(void)RowIndex;
	HoveredInputBindingRow = RowWidget;
	CurrentFocusedIndex = KeyboardFocusTargets.IndexOfByKey(RowWidget);
}

void USettingInputPageWidget::HandleInputBindingRebindRequested(
	UInputBindingRowWidget* RowWidget,
	int32 RowIndex)
{
	(void)RowIndex;
	StartInputRebind(RowWidget);
}

bool USettingInputPageWidget::StartInputRebind(UInputBindingRowWidget* RowWidget)
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
	HoveredInputBindingRow = RowWidget;
	CurrentFocusedIndex = KeyboardFocusTargets.IndexOfByKey(RowWidget);
	bWaitingForInputRebind = true;
	RebindStartTimeSeconds = FPlatformTime::Seconds();
	if (!InputRebindPreProcessor && FSlateApplication::IsInitialized())
	{
		InputRebindPreProcessor = MakeShared<FSettingInputRebindPreProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputRebindPreProcessor, 0);
	}
	OnKeyboardCaptureRequested.Broadcast();
	SetInputResult(WaitingForInputText, InputResultDefaultColor);
	return true;
}

bool USettingInputPageWidget::HandleInputRebindPointerKey(FKey Key)
{
	if (!bWaitingForInputRebind) return false;
	// The opening click must never become the replacement key.
	if (FPlatformTime::Seconds() - RebindStartTimeSeconds < 0.05) return true;
	TryCommitInputRebind(Key);
	return true;
}

void USettingInputPageWidget::EndInputRebind()
{
	bWaitingForInputRebind = false;
	PendingRebindInputBindingRow = nullptr;
	if (InputRebindPreProcessor && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputRebindPreProcessor);
	}
	InputRebindPreProcessor.Reset();
}

bool USettingInputPageWidget::TryCommitInputRebind(FKey NewKey)
{
	UInputBindingRowWidget* RowWidget = PendingRebindInputBindingRow.Get();
	if (!RowWidget)
	{
		SetInputResult(InputRebindFailedText, InputResultFailureColor);
		EndInputRebind();
		return false;
	}

	const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
	const EOutlierInputBindingValidation Validation = Row.ValidateReplacementKey(NewKey);
	if (Validation != EOutlierInputBindingValidation::Valid)
	{
		SetInputResult(Validation == EOutlierInputBindingValidation::AxisInputRequired
			? AxisInputRequiredText : InputRequiredText, InputResultFailureColor);
		EndInputRebind();
		return false;
	}
	const bool bRestoringDefaultKey = NewKey == Row.DefaultKey;

	if (IsInputKeyDuplicate(NewKey, RowWidget))
	{
		SetInputResult(DuplicateInputText, InputResultFailureColor);
		EndInputRebind();
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
							EndInputRebind();
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
							EndInputRebind();
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
		EndInputRebind();
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

	if (ULocalPlayerSettingsSubsystem* SettingsSubsystem = GetSettingsSubsystem())
	{
		SettingsSubsystem->NotifyInputActionKeyChanged(Row.InputAction, NewKey);
	}

	SetInputResult(InputChangedText, InputResultSuccessColor);
	EndInputRebind();
	return true;
}

bool USettingInputPageWidget::IsPointerOverInputBindingRow(
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

void USettingInputPageWidget::ClearInputBindingInteractionState(bool bClearResultText)
{
	EndInputRebind();
	HoveredInputBindingRow = nullptr;

	if (bClearResultText)
	{
		SetInputResult(InputResultInitialText, InputResultDefaultColor);
	}
}

bool USettingInputPageWidget::TryGetUserSettingsKeyForRow(
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

FKey USettingInputPageWidget::ResolveInputBindingCurrentKey(
	const FOutlierInputBindingTableRow& Row) const
{
	if (IsDesignTime()) return Row.DefaultKey;

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

void USettingInputPageWidget::BuildInputBindingQueryContexts(
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

bool USettingInputPageWidget::IsInputKeyDuplicate(
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

void USettingInputPageWidget::RefreshInputBindingDisplayedKeys()
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

void USettingInputPageWidget::SetInputResult(
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
