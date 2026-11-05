#include "UI/SettingInputPageWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBar.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "UI/SprintInputModeWidget.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/Character.h"
#include "OutlierPlayerState.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Input/OutlierInputBindingTable.h"
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
	RefreshSprintInputModeRow();
}

void USettingInputPageWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BoundSettingsSubsystem = GetSettingsSubsystem();
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnInputSettingsContextChanged.AddUObject(this, &ThisClass::HandleInputSettingsContextChanged);
		BoundSettingsSubsystem->OnLinkedInputBindingsChanged.AddUObject(this, &ThisClass::RefreshInputBindingDisplayedKeys);
		BoundSettingsSubsystem->RefreshInputSettingsContext();
		DisplayedRole = BoundSettingsSubsystem->GetInputSettingsRole();
	}
	if (InputBindingTable) InputBindingTable->OnRowsChanged.AddUObject(this, &ThisClass::RebuildInputBindingList);
	RefreshInputScrollBarVisibility();
	RefreshSprintInputModeRow();
}

void USettingInputPageWidget::ActivatePage()
{
	bInputPageActive = true;
	ClearInputBindingInteractionState(true);
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->RefreshInputSettingsContext();
		DisplayedRole = BoundSettingsSubsystem->GetInputSettingsRole();
	}
	if (!bInputBindingListBuilt) RebuildInputBindingList();
	else RefreshDisplayedInputRows();
	RefreshSprintInputModeRow();
	Super::ActivatePage();
}

void USettingInputPageWidget::DeactivatePage()
{
	bInputPageActive = false;
	RefreshSprintInputModeRow();
	if (MouseSensitivityWidget) MouseSensitivityWidget->ConfirmPendingValue();
	ClearInputBindingInteractionState(true);
	Super::DeactivatePage();
}

void USettingInputPageWidget::NativeDestruct()
{
	bInputPageActive = false;
	if (SprintModeRow) SprintModeRow->SetSettingsContext(nullptr, EOutlierPlayerRole::None, false);
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnInputSettingsContextChanged.RemoveAll(this);
		BoundSettingsSubsystem->OnLinkedInputBindingsChanged.RemoveAll(this);
	}
	if (InputBindingTable) InputBindingTable->OnRowsChanged.RemoveAll(this);
	BoundSettingsSubsystem = nullptr;
	ClearInputBindingInteractionState(false);
	Super::NativeDestruct();
}

void USettingInputPageWidget::RebuildKeyboardFocusTargets()
{
	KeyboardFocusTargets.Reset();
	if (MouseSensitivityWidget) KeyboardFocusTargets.Add(MouseSensitivityWidget);
	if (SprintModeRow)
	{
		if (UWidget* Target = SprintModeRow->GetKeyboardFocusTarget()) KeyboardFocusTargets.Add(Target);
	}
	for (UInputBindingRowWidget* Row : DisplayedInputBindingRows)
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
	if (Input == ESettingPageInput::Confirm || Input == ESettingPageInput::Left || Input == ESettingPageInput::Right)
	{
		UWidget* Target = KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex)
			? KeyboardFocusTargets[CurrentFocusedIndex].Get() : nullptr;
		if (Target && SprintModeRow && Target == SprintModeRow->GetKeyboardFocusTarget())
			return SprintModeRow->CycleMode();
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
	// 설정 열기/닫기 그룹은 Escape가 기본 키다. 이 행에서는 Escape 입력을 복원으로 사용하고,
	// 다른 행의 Escape 취소 동작은 그대로 유지한다.
	const bool bRestoringEscape = PendingRebindInputBindingRow
		&& PendingRebindInputBindingRow->GetInputBindingRow().DefaultKey == EKeys::Escape;
	if (KeyEvent.GetKey() == EKeys::Escape && !bRestoringEscape) ClearInputBindingInteractionState(true);
	else TryCommitInputRebind(KeyEvent.GetKey());
	return true;
}

void USettingInputPageWidget::HandlePagePointerDown(const FPointerEvent& PointerEvent)
{
	Super::HandlePagePointerDown(PointerEvent);
	if (SprintModeRow && SprintModeRow->IsVisible()
		&& SprintModeRow->GetCachedGeometry().IsUnderLocation(PointerEvent.GetScreenSpacePosition()))
	{
		CurrentFocusedIndex = KeyboardFocusTargets.IndexOfByKey(SprintModeRow->GetKeyboardFocusTarget());
	}
	if (!IsPointerOverInputBindingRow(PointerEvent)) ClearInputBindingInteractionState(true);
	else if (KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex))
		HoveredInputBindingRow = Cast<UInputBindingRowWidget>(KeyboardFocusTargets[CurrentFocusedIndex]);
}

void USettingInputPageWidget::RebuildInputBindingList()
{
	if (!InputBindingListHost || !WidgetTree) return;
	ClearInputBindingInteractionState(true);
	InputBindingListHost->ClearChildren();
	CachedInputBindingRows.Reset();
	DisplayedInputBindingRows.Reset();
	bInputBindingListBuilt = false;

	if (InputBindingTable && InputBindingRowWidgetClass && (IsDesignTime() || GetOwningPlayer()))
	{
		const TArray<FOutlierInputBindingTableRow> Rows = InputBindingTable->GetVisibleRowsSorted();
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			UInputBindingRowWidget* Row = IsDesignTime()
				? CreateWidget<UInputBindingRowWidget>(this, InputBindingRowWidgetClass)
				: CreateWidget<UInputBindingRowWidget>(GetOwningPlayer(), InputBindingRowWidgetClass);
			if (!Row) continue;
			Row->InitializeInputBindingRow(Rows[Index], Index);
			Row->SetDisplayedKey(ResolveInputBindingCurrentKey(Rows[Index]));
			if (!IsDesignTime())
			{
				Row->OnRowHovered.AddUniqueDynamic(this, &ThisClass::HandleInputBindingRowHovered);
				Row->OnRebindRequested.AddUniqueDynamic(this, &ThisClass::HandleInputBindingRebindRequested);
			}
			CachedInputBindingRows.Add(Row);
		}
	}
	bInputBindingListBuilt = true;
	RefreshDisplayedInputRows();
}

void USettingInputPageWidget::RefreshDisplayedInputRows()
{
	if (!InputBindingListHost) return;
	UWidget* PreviousFocus = KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex)
		? KeyboardFocusTargets[CurrentFocusedIndex].Get() : nullptr;
	InputBindingListHost->ClearChildren();
	DisplayedInputBindingRows.Reset();

	// 제거하는 것은 표시 Host의 자식 관계뿐이다. 숨긴 행도 캐시와 키 충돌 검사에는 남긴다.
	for (UInputBindingRowWidget* Row : CachedInputBindingRows)
	{
		if (Row && Row->GetInputBindingRow().IsVisibleForRole(DisplayedRole))
		{
			InputBindingListHost->AddChild(Row);
			DisplayedInputBindingRows.Add(Row);
		}
	}
	ApplyInputBindingRowPadding();
	RefreshInputScrollBarVisibility();
	RefreshSprintInputModeRow();
	RefreshInputBindingDisplayedKeys();
	RebuildKeyboardFocusTargets();
	CurrentFocusedIndex = KeyboardFocusTargets.IndexOfByKey(PreviousFocus);
	if (CurrentFocusedIndex == INDEX_NONE && !KeyboardFocusTargets.IsEmpty()) CurrentFocusedIndex = 0;
	if (bInputPageActive) FocusCurrentKeyboardTarget();
}

void USettingInputPageWidget::RefreshInputScrollBarVisibility()
{
	const bool bNeedsScrollBar = InputBindingListHost
		&& InputBindingListHost->GetChildrenCount() > InputBindingRowsWithoutScrollBar;
	if (PageScrollBar)
	{
		PageScrollBar->SetIsEnabled(bNeedsScrollBar);
		PageScrollBar->SetVisibility(bNeedsScrollBar ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!bNeedsScrollBar && PageScrollBox)
	{
		PageScrollBox->EndInertialScrolling();
		PageScrollBox->SetScrollOffset(0.0f);
	}
}

void USettingInputPageWidget::SetInputBindingRowPadding(FMargin NewPadding)
{
	InputBindingRowPadding = NewPadding;
	ApplyInputBindingRowPadding();
}

void USettingInputPageWidget::ApplyInputBindingRowPadding()
{
	if (!InputBindingListHost) return;
	const float Stride = InputBindingRowYOffset + InputBindingRowPadding.Top + InputBindingRowPadding.Bottom;
	for (int32 Index = 0; Index < InputBindingListHost->GetChildrenCount(); ++Index)
	{
		UWidget* Row = InputBindingListHost->GetChildAt(Index);
		if (UInputBindingRowWidget* BindingRow = Cast<UInputBindingRowWidget>(Row)) BindingRow->SetPadding(InputBindingRowPadding);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Row->Slot))
		{
			// 원본 행 번호 대신 현재 표시 순서를 사용해 필터 뒤의 빈 공간을 없앤다.
			CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			CanvasSlot->SetAlignment(FVector2D::ZeroVector);
			CanvasSlot->SetPosition(FVector2D(0.0f, Stride * Index));
			CanvasSlot->SetAutoSize(true);
		}
	}
}

void USettingInputPageWidget::RefreshSprintInputModeRow()
{
	if (SprintModeRow) SprintModeRow->SetSettingsContext(BoundSettingsSubsystem, DisplayedRole, bInputPageActive);
}
void USettingInputPageWidget::HandleInputSettingsContextChanged(EOutlierPlayerRole Role)
{
	ClearInputBindingInteractionState(true);
	DisplayedRole = Role;
	if (bInputBindingListBuilt) RefreshDisplayedInputRows();
	else RefreshSprintInputModeRow();
}

void USettingInputPageWidget::HandleInputBindingRowHovered(
	UInputBindingRowWidget* RowWidget,
	int32 RowIndex)
{
	(void)RowIndex;
	if (!DisplayedInputBindingRows.Contains(RowWidget)) return;
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
	if (!RowWidget || !bInputPageActive || !DisplayedInputBindingRows.Contains(RowWidget))
	{
		return false;
	}

	const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
	if (!Row.bRebindable)
	{
		return false;
	}

	PendingRebindInputBindingRow = RowWidget;
	RebindContextRevision = BoundSettingsSubsystem ? BoundSettingsSubsystem->GetInputSettingsContextRevision() : 0;
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
	if (!RowWidget || !bInputPageActive || !DisplayedInputBindingRows.Contains(RowWidget)
		|| (BoundSettingsSubsystem && RebindContextRevision != BoundSettingsSubsystem->GetInputSettingsContextRevision()))
	{
		SetInputResult(InputRebindFailedText, InputResultFailureColor);
		EndInputRebind();
		return false;
	}

	const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
	// 그룹 검증/복구/저장은 로컬 설정 Subsystem이 소유한다. 위젯은 결과만 표시한다.
	FString Error;
	const EOutlierLinkedBindingResult Result = BoundSettingsSubsystem
		? BoundSettingsSubsystem->ChangeLinkedInputBinding(InputBindingTable, Row, NewKey, Error)
		: EOutlierLinkedBindingResult::Unavailable;
	if (Result != EOutlierLinkedBindingResult::Success)
	{
		const FText Message = Result == EOutlierLinkedBindingResult::Conflict ? DuplicateInputText
			: Result == EOutlierLinkedBindingResult::AxisInputRequired ? AxisInputRequiredText
			: Result == EOutlierLinkedBindingResult::InputRequired ? InputRequiredText : InputRebindFailedText;
		SetInputResult(Message, InputResultFailureColor);
		if (!Error.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("Input rebind rejected: %s"), *Error);
		EndInputRebind();
		return false;
	}
	RefreshInputBindingDisplayedKeys();

	SetInputResult(InputChangedText, InputResultSuccessColor);
	EndInputRebind();
	return true;
}

bool USettingInputPageWidget::IsPointerOverInputBindingRow(
	const FPointerEvent& InMouseEvent) const
{
	const FVector2D ScreenSpacePosition = InMouseEvent.GetScreenSpacePosition();
	for (const TObjectPtr<UInputBindingRowWidget>& RowWidgetPtr : DisplayedInputBindingRows)
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
			Row.Slot);
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

	FKey UserSettingsKey;
	if (TryGetUserSettingsKeyForRow(Row, UserSettingsKey))
	{
		return UserSettingsKey;
	}

	return Row.DefaultKey;
}

void USettingInputPageWidget::RefreshInputBindingDisplayedKeys()
{
	for (const TObjectPtr<UInputBindingRowWidget>& RowWidgetPtr : CachedInputBindingRows)
	{
		UInputBindingRowWidget* RowWidget = RowWidgetPtr.Get();
		if (!RowWidget)
		{
			continue;
		}

		const FOutlierInputBindingTableRow& Row = RowWidget->GetInputBindingRow();
		RowWidget->SetDisplayedKey(ResolveInputBindingCurrentKey(Row));
		RowWidget->SetBindingError(BoundSettingsSubsystem
			? FText::FromString(BoundSettingsSubsystem->GetLinkedInputBindingError(Row)) : FText::GetEmpty());
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
