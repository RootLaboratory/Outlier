#include "UI/SettingWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/SettingButtonsWidget.h"
#include "UI/SettingGraphicPageWidget.h"
#include "UI/SettingSoundPageWidget.h"
#include "UI/SettingInputPageWidget.h"

void USettingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (SettingButtons)
	{
		SettingButtons->OnPageRequested.AddUObject(this, &ThisClass::HandlePageRequested);
		SettingButtons->OnExitRequested.AddUObject(this, &ThisClass::PopSelfFromLayer);
	}
	if (InputPage) InputPage->OnKeyboardCaptureRequested.AddUObject(this, &ThisClass::HandleKeyboardCaptureRequested);
}

void USettingWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
#if WITH_EDITORONLY_DATA
	if (IsDesignTime() && SettingSwitcher)
	{
		UWidget* PreviewPage = nullptr;
		switch (PreviewPageIndex)
		{
		case 0: PreviewPage = GraphicPage; break;
		case 1: PreviewPage = SoundPage; break;
		case 2: PreviewPage = InputPage; break;
		}
		if (PreviewPage && SettingSwitcher->GetChildIndex(PreviewPage) != INDEX_NONE)
		{
			SettingSwitcher->SetActiveWidget(PreviewPage);
			if (SettingButtons) SettingButtons->SetSelectedPage(PreviewPageIndex);
		}
	}
#endif
}

void USettingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// In-game settings must preserve paused voices and feedback for Continue.
	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
		LocalPlayer && (!GetWorld() || !GetWorld()->IsPaused()))
	{
		if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
		{
			if (UOutlierAudioSubsystem* Audio = GameInstance->GetSubsystem<UOutlierAudioSubsystem>())
				Audio->StopTransientLocalAudio();
		}
	}
	SetActiveSettingPage(0);
	FocusArea = EFocusArea::Buttons;
	if (SettingButtons) SettingButtons->FocusSelectedPageButton();
}

void USettingWidget::NativeDestruct()
{
	if (ActivePage) ActivePage->DeactivatePage();
	ActivePage = nullptr;
	Super::NativeDestruct();
}

void USettingWidget::InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors) {}

bool USettingWidget::SetActiveSettingPage(int32 PageIndex)
{
	USettingPageWidget* Page = nullptr;
	switch (PageIndex)
	{
	case 0: Page = GraphicPage; break;
	case 1: Page = SoundPage; break;
	case 2: Page = InputPage; break;
	default: return false;
	}
	if (!SettingSwitcher || !Page || SettingSwitcher->GetChildIndex(Page) == INDEX_NONE) return false;
	if (ActivePage) ActivePage->DeactivatePage();
	SettingSwitcher->SetActiveWidget(Page);
	ActivePage = Page;
	ActivePage->ActivatePage();
	if (SettingButtons) SettingButtons->SetSelectedPage(PageIndex);
	return true;
}

void USettingWidget::HandlePageRequested(int32 PageIndex)
{
	if (ActivePage && ActivePage->IsCapturingKeyboardInput()) return;
	if (SetActiveSettingPage(PageIndex))
	{
		FocusArea = EFocusArea::Buttons;
		if (SettingButtons) SettingButtons->FocusSelectedButton();
	}
}

void USettingWidget::HandleKeyboardCaptureRequested()
{
	FocusArea = EFocusArea::Page;
	SetKeyboardFocus();
}

void USettingWidget::FocusActivePage(bool bLastTarget)
{
	FocusArea = EFocusArea::Page;
	if (ActivePage) ActivePage->FocusPage(bLastTarget);
}

bool USettingWidget::RoutePageInput(ESettingPageInput Input)
{
	return ActivePage && ActivePage->HandlePageInput(Input);
}

bool USettingWidget::HandleUILayerEscape_Implementation()
{
	if (RoutePageInput(ESettingPageInput::Escape)) return true;
	PopSelfFromLayer();
	return true;
}

bool USettingWidget::HandleUILayerConfirmed_Implementation()
{
	if (ActivePage && ActivePage->IsCapturingKeyboardInput()) return RoutePageInput(ESettingPageInput::Confirm);
	if (FocusArea == EFocusArea::Buttons)
	{
		if (SettingButtons && SettingButtons->IsExitSelected()) PopSelfFromLayer();
		return true;
	}
	RoutePageInput(ESettingPageInput::Confirm);
	return true;
}

bool USettingWidget::HandleUILayerUp_Implementation()
{
	if (ActivePage && ActivePage->IsCapturingKeyboardInput()) return RoutePageInput(ESettingPageInput::Up);
	if (FocusArea == EFocusArea::Buttons && SettingButtons)
		SettingButtons->OffsetSelection(-1);
	else if (FocusArea == EFocusArea::Page && !RoutePageInput(ESettingPageInput::Up))
	{
		FocusArea = EFocusArea::Buttons;
		if (SettingButtons) SettingButtons->FocusSelectedPageButton();
	}
	return true;
}

bool USettingWidget::HandleUILayerDown_Implementation()
{
	if (ActivePage && ActivePage->IsCapturingKeyboardInput()) return RoutePageInput(ESettingPageInput::Down);
	if (FocusArea == EFocusArea::Buttons)
	{
		if (SettingButtons) SettingButtons->OffsetSelection(1);
	}
	else if (FocusArea == EFocusArea::Page && !RoutePageInput(ESettingPageInput::Down))
	{
		FocusArea = EFocusArea::Buttons;
		if (SettingButtons) SettingButtons->FocusExitButton();
	}
	return true;
}

bool USettingWidget::HandleUILayerLeft_Implementation()
{
	if (ActivePage && ActivePage->IsCapturingKeyboardInput()) return RoutePageInput(ESettingPageInput::Left);
	if (FocusArea == EFocusArea::Page && !RoutePageInput(ESettingPageInput::Left))
	{
		FocusArea = EFocusArea::Buttons;
		if (SettingButtons) SettingButtons->FocusSelectedPageButton();
	}
	return true;
}

bool USettingWidget::HandleUILayerRight_Implementation()
{
	if (ActivePage && ActivePage->IsCapturingKeyboardInput()) return RoutePageInput(ESettingPageInput::Right);
	if (FocusArea == EFocusArea::Buttons && SettingButtons && !SettingButtons->IsExitSelected()) FocusActivePage();
	else if (FocusArea == EFocusArea::Page) RoutePageInput(ESettingPageInput::Right);
	return true;
}

FReply USettingWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ActivePage && ActivePage->HandlePageKeyDown(InKeyEvent)) return FReply::Handled();
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (ULocalPlayerUILayerSubsystem* Layers = LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>())
		{
			if (Layers->RouteWidgetPreviewKeyDown(this, InKeyEvent)) return FReply::Handled();
		}
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply USettingWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D Position = InMouseEvent.GetScreenSpacePosition();
	if (ActivePage) ActivePage->HandlePagePointerDown(InMouseEvent);
	if (SettingButtons && SettingButtons->HandlePointerDown(InMouseEvent))
	{
		FocusArea = EFocusArea::Buttons;
	}
	else if (ActivePage && ActivePage->GetCachedGeometry().IsUnderLocation(Position)) FocusArea = EFocusArea::Page;
	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

void USettingWidget::PopSelfFromLayer()
{
	if (IsInViewport() || GetParent()) OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Back"));
	if (ActivePage) ActivePage->DeactivatePage();
	ActivePage = nullptr;
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (ULocalPlayerUILayerSubsystem* Layers = LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>())
			Layers->PopWidget(this);
	}
}
