#include "UI/InGameSettingWidget.h"

#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "UI/InGameSettingButtonsWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/SettingWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerKeyHintWidget.h"
#include "UI/UILayerTypes.h"

TSubclassOf<UUILayerKeyHintWidget> UInGameSettingWidget::GetKeyHintWidgetClass() const
{
	return KeyHintWidgetClass;
}

void UInGameSettingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (ButtonsWidget)
	{
		ButtonsWidget->OnActionConfirmed.AddUObject(
			this, &ThisClass::HandleButtonAction);
	}
}

void UInGameSettingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MenuText && DefaultMenuText.IsEmpty())
	{
		DefaultMenuText = MenuText->GetText();
	}
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->OnExplicitLeaveStateChanged.AddUObject(this, &ThisClass::RefreshMenuState);
		FirstPersonController->OnCheckpointRestartVoteViewChanged.AddUObject(
			this, &ThisClass::RefreshCheckpointRestartState);
	}
	RefreshMenuState();
}

void UInGameSettingWidget::NativeDestruct()
{
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->OnExplicitLeaveStateChanged.RemoveAll(this);
		FirstPersonController->OnCheckpointRestartVoteViewChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UInGameSettingWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
	(void)ContextActors;
}

bool UInGameSettingWidget::HandleUILayerEscape_Implementation()
{
	return bLeavingGame || (ButtonsWidget
		&& IUILayerInputReceiver::Execute_HandleUILayerEscape(ButtonsWidget));
}

bool UInGameSettingWidget::HandleUILayerConfirmed_Implementation()
{
	return bLeavingGame || (ButtonsWidget
		&& IUILayerInputReceiver::Execute_HandleUILayerConfirmed(ButtonsWidget));
}

bool UInGameSettingWidget::HandleUILayerUp_Implementation()
{
	return bLeavingGame || (ButtonsWidget
		&& IUILayerInputReceiver::Execute_HandleUILayerUp(ButtonsWidget));
}

bool UInGameSettingWidget::HandleUILayerDown_Implementation()
{
	return bLeavingGame || (ButtonsWidget
		&& IUILayerInputReceiver::Execute_HandleUILayerDown(ButtonsWidget));
}

bool UInGameSettingWidget::HandleUILayerLeft_Implementation()
{
	return bLeavingGame || (ButtonsWidget
		&& IUILayerInputReceiver::Execute_HandleUILayerLeft(ButtonsWidget));
}

bool UInGameSettingWidget::HandleUILayerRight_Implementation()
{
	return bLeavingGame || (ButtonsWidget
		&& IUILayerInputReceiver::Execute_HandleUILayerRight(ButtonsWidget));
}

void UInGameSettingWidget::HandleButtonAction(EInGameSettingButtonAction Action)
{
	if (bLeavingGame)
	{
		return;
	}
	AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
	switch (Action)
	{
	case EInGameSettingButtonAction::Continue:
		if (FirstPersonController)
		{
			FirstPersonController->RequestCloseInGameSetting();
		}
		break;
	case EInGameSettingButtonAction::RestartCheckpoint:
		if (FirstPersonController)
		{
			FirstPersonController->RequestCheckpointRestart();
		}
		break;
	case EInGameSettingButtonAction::Setting:
		PushSettingLayer();
		break;
	case EInGameSettingButtonAction::Title:
		if (FirstPersonController)
		{
			FirstPersonController->RequestLeaveGame(false);
		}
		break;
	case EInGameSettingButtonAction::Exit:
		if (FirstPersonController)
		{
			FirstPersonController->RequestLeaveGame(true);
		}
		break;
	}
}

void UInGameSettingWidget::RefreshCheckpointRestartState(EOutlierCheckpointRestartVoteView VoteView)
{
	(void)VoteView;
	RefreshMenuState();
}

void UInGameSettingWidget::PushSettingLayer()
{
	APlayerController* OwningPlayer = GetOwningPlayer();
	if (!OwningPlayer || !SettingWidgetClass)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		return;
	}

	if (!ActiveSettingWidget)
	{
		ActiveSettingWidget = CreateWidget<USettingWidget>(
			OwningPlayer,
			SettingWidgetClass);
	}

	if (!ActiveSettingWidget)
	{
		return;
	}

	TArray<AActor*> ContextActors;
	ContextActors.Add(OwningPlayer);
	IUILayerContextReceiver::Execute_InitializeUILayerContext(
		ActiveSettingWidget,
		ContextActors);

	LayerSubsystem->PushWidget(
		UILayerTags::GameMenu(),
		ActiveSettingWidget,
		FirstPersonInputModeTags::UI(),
		OwningPlayer,
		EUILayerFocusTarget::Widget,
		true);
}

void UInGameSettingWidget::RefreshMenuState()
{
	const AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
	bLeavingGame = FirstPersonController && FirstPersonController->HasRequestedExplicitLeave();
	if (MenuText)
	{
		MenuText->SetText(bLeavingGame ? GameLeaveWaitingText : DefaultMenuText);
	}
	if (ButtonsWidget)
	{
		ButtonsWidget->SetActionEnabled(EInGameSettingButtonAction::Continue, !bLeavingGame);
		ButtonsWidget->SetActionEnabled(EInGameSettingButtonAction::RestartCheckpoint,
			!bLeavingGame && FirstPersonController && FirstPersonController->CanRequestCheckpointRestart());
		ButtonsWidget->SetActionEnabled(EInGameSettingButtonAction::Setting,
			!bLeavingGame);
		ButtonsWidget->SetActionEnabled(EInGameSettingButtonAction::Title, !bLeavingGame);
		ButtonsWidget->SetActionEnabled(EInGameSettingButtonAction::Exit,
			!bLeavingGame);
	}
}
