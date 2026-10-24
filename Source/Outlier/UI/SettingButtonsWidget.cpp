#include "UI/SettingButtonsWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/Button.h"

namespace
{
// Menu order: Graphic, Input, Sound. Switcher page order: Graphic, Sound, Input.
constexpr int32 PageIndicesInMenuOrder[] = { 0, 2, 1 };

int32 GetPageIndexForButton(int32 ButtonIndex)
{
	return ButtonIndex >= 0 && ButtonIndex < UE_ARRAY_COUNT(PageIndicesInMenuOrder)
		? PageIndicesInMenuOrder[ButtonIndex] : INDEX_NONE;
}

int32 GetButtonIndexForPage(int32 PageIndex)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PageIndicesInMenuOrder); ++Index)
	{
		if (PageIndicesInMenuOrder[Index] == PageIndex) return Index;
	}
	return INDEX_NONE;
}
}

void USettingButtonsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (GraphicButton) GraphicButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleGraphicClicked);
	if (SoundButton) SoundButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSoundClicked);
	if (InputButton) InputButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleInputClicked);
	if (ExitButton) ExitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleExitClicked);
	OriginalButtonStyles.SetNum(ButtonCount);
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		if (UButton* Button = GetButton(Index)) OriginalButtonStyles[Index] = Button->GetStyle();
	}
	SetSelectedPage(0);
}

UButton* USettingButtonsWidget::GetButton(int32 Index) const
{
	switch (Index)
	{
	case 0: return GraphicButton;
	case 1: return InputButton;
	case 2: return SoundButton;
	case ExitButtonIndex: return ExitButton;
	default: return nullptr;
	}
}

void USettingButtonsWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (IsDesignTime())
	{
		if (OriginalButtonStyles.IsEmpty())
		{
			OriginalButtonStyles.SetNum(ButtonCount);
			for (int32 Index = 0; Index < ButtonCount; ++Index)
			{
				if (UButton* Button = GetButton(Index)) OriginalButtonStyles[Index] = Button->GetStyle();
			}
		}
		SetSelectedPage(SelectedPageIndex);
	}
}

void USettingButtonsWidget::SetSelectedPage(int32 PageIndex)
{
	const int32 ButtonIndex = GetButtonIndexForPage(PageIndex);
	if (ButtonIndex == INDEX_NONE || !GetButton(ButtonIndex)) return;
	SelectedPageIndex = PageIndex;
	SelectedButtonIndex = ButtonIndex;
	ApplySelection();
}

void USettingButtonsWidget::ApplySelection()
{
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		if (UButton* Button = GetButton(Index); Button && OriginalButtonStyles.IsValidIndex(Index))
		{
			FButtonStyle Style = OriginalButtonStyles[Index];
			if (Index == SelectedButtonIndex)
				Style.SetNormal(Style.Hovered);
			Button->SetStyle(Style);
		}
	}
}

void USettingButtonsWidget::OffsetSelection(int32 Direction)
{
	SelectedButtonIndex = (SelectedButtonIndex + Direction + ButtonCount) % ButtonCount;
	if (!IsExitSelected()) OnPageRequested.Broadcast(GetPageIndexForButton(SelectedButtonIndex));
	ApplySelection();
	FocusSelectedButton();
}

void USettingButtonsWidget::FocusSelectedButton()
{
	if (UButton* Button = GetButton(SelectedButtonIndex))
	{
		if (APlayerController* Player = GetOwningPlayer()) Button->SetUserFocus(Player);
	}
}

void USettingButtonsWidget::FocusSelectedPageButton()
{
	SelectedButtonIndex = GetButtonIndexForPage(SelectedPageIndex);
	ApplySelection();
	FocusSelectedButton();
}

void USettingButtonsWidget::FocusExitButton()
{
	SelectedButtonIndex = ExitButtonIndex;
	ApplySelection();
	FocusSelectedButton();
}

bool USettingButtonsWidget::HandlePointerDown(const FPointerEvent& PointerEvent)
{
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		UButton* Button = GetButton(Index);
		if (Button && Button->IsVisible() && Button->GetIsEnabled()
			&& Button->GetCachedGeometry().IsUnderLocation(PointerEvent.GetScreenSpacePosition()))
		{
			SelectedButtonIndex = Index;
			ApplySelection();
			return true;
		}
	}
	return false;
}

void USettingButtonsWidget::HandleGraphicClicked() { OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Click")); OnPageRequested.Broadcast(0); }
void USettingButtonsWidget::HandleSoundClicked() { OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Click")); OnPageRequested.Broadcast(1); }
void USettingButtonsWidget::HandleInputClicked() { OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Click")); OnPageRequested.Broadcast(2); }
void USettingButtonsWidget::HandleExitClicked() { OnExitRequested.Broadcast(); }
