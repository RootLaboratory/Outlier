#include "UI/InGameSettingWidget.h"

#include "Components/Button.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/SettingWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerKeyHintWidget.h"
#include "UI/UILayerTypes.h"
#include "Components/TextBlock.h"

void UInGameSettingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (ContinueButton)
	{
		ContinueButton->OnClicked.AddUniqueDynamic(
			this,
			&UInGameSettingWidget::HandleContinueButtonClicked);
	}

	if (SettingButton)
	{
		SettingButton->OnClicked.AddUniqueDynamic(
			this,
			&UInGameSettingWidget::HandleSettingButtonClicked);
	}

	if (RestartCheckpointButton)
	{
		RestartCheckpointButton->OnClicked.AddUniqueDynamic(
			this,
			&UInGameSettingWidget::HandleRestartCheckpointButtonClicked);
	}

	if (TitleButton)
	{
		TitleButton->OnClicked.AddUniqueDynamic(
			this,
			&UInGameSettingWidget::HandleTitleButtonClicked);
	}
}

void UInGameSettingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MenuText && DefaultMenuText.IsEmpty())
	{
		DefaultMenuText = MenuText->GetText();
	}
	bConfirmingGameExit = false;

	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->OnCheckpointRestartVoteViewChanged.AddUObject(
			this,
			&UInGameSettingWidget::RefreshCheckpointRestartState);
		RefreshCheckpointRestartState(
			FirstPersonController->GetCheckpointRestartVoteView());
	}
	PushKeyHintLayer();
}

void UInGameSettingWidget::NativeDestruct()
{
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->OnCheckpointRestartVoteViewChanged.RemoveAll(this);
	}
	PopKeyHintLayer();
	Super::NativeDestruct();
}

void UInGameSettingWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
	(void)ContextActors;
}

bool UInGameSettingWidget::HandleUILayerEscape_Implementation()
{
	// 같은 Escape라도 나가기 확인 취소 -> 투표 철회 -> 메뉴 닫기 순으로 현재 모드가 우선한다.
	if (bConfirmingGameExit)
	{
		CancelGameExitConfirmation();
		return true;
	}

	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
		FirstPersonController
		&& FirstPersonController->GetCheckpointRestartVoteView()
			== EOutlierCheckpointRestartVoteView::RequesterWaiting)
	{
		FirstPersonController->RequestCancelCheckpointRestart();
		return true;
	}

	HandleContinueButtonClicked();
	return true;
}

bool UInGameSettingWidget::HandleUILayerConfirmed_Implementation()
{
	if (bConfirmingGameExit)
	{
		ConfirmGameExit();
		return true;
	}

	if (const AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
		FirstPersonController
		&& FirstPersonController->GetCheckpointRestartVoteView()
			== EOutlierCheckpointRestartVoteView::RequesterWaiting)
	{
		return true;
	}

	HandleContinueButtonClicked();
	return true;
}

bool UInGameSettingWidget::HandleUILayerUp_Implementation()
{
	return false;
}

bool UInGameSettingWidget::HandleUILayerDown_Implementation()
{
	return false;
}

bool UInGameSettingWidget::HandleUILayerLeft_Implementation()
{
	return false;
}

bool UInGameSettingWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void UInGameSettingWidget::HandleContinueButtonClicked()
{
	if (bConfirmingGameExit)
	{
		CancelGameExitConfirmation();
		return;
	}

	PopSelfFromLayer();

	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->RequestCloseInGameSetting();
	}
}

void UInGameSettingWidget::HandleSettingButtonClicked()
{
	PushSettingLayer();
}

void UInGameSettingWidget::HandleRestartCheckpointButtonClicked()
{
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->RequestCheckpointRestart();
	}
}

void UInGameSettingWidget::HandleTitleButtonClicked()
{
	if (bConfirmingGameExit)
	{
		ConfirmGameExit();
		return;
	}

	bConfirmingGameExit = true;
	RefreshCheckpointRestartState(EOutlierCheckpointRestartVoteView::None);
}

void UInGameSettingWidget::CancelGameExitConfirmation()
{
	bConfirmingGameExit = false;
	const AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());
	RefreshCheckpointRestartState(FirstPersonController
		? FirstPersonController->GetCheckpointRestartVoteView()
		: EOutlierCheckpointRestartVoteView::None);
}

void UInGameSettingWidget::ConfirmGameExit()
{
	if (!bConfirmingGameExit)
	{
		return;
	}

	bConfirmingGameExit = false;
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FirstPersonController->RequestLeaveGame();
	}
}

void UInGameSettingWidget::PushKeyHintLayer()
{
	if (KeyHintLayerHandle.IsValid())
	{
		return;
	}

	APlayerController* OwningPlayer = GetOwningPlayer();
	if (!OwningPlayer || !KeyHintWidgetClass)
	{
		return;
	}

	ActiveKeyHintWidget = CreateWidget<UUILayerKeyHintWidget>(
		OwningPlayer,
		KeyHintWidgetClass);
	if (!ActiveKeyHintWidget)
	{
		return;
	}

	ActiveKeyHintWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		KeyHintLayerHandle = LayerSubsystem->PushWidget(
			UILayerTags::Modal(),
			ActiveKeyHintWidget,
			FirstPersonInputModeTags::UI(),
			GetOwningPlayer(),
			EUILayerFocusTarget::None,
			true,
			false);
	}
}

void UInGameSettingWidget::PopKeyHintLayer()
{
	if (!KeyHintLayerHandle.IsValid())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopLayer(KeyHintLayerHandle);
	}

	KeyHintLayerHandle.Reset();
	ActiveKeyHintWidget = nullptr;
}

void UInGameSettingWidget::PopSelfFromLayer()
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

void UInGameSettingWidget::RefreshCheckpointRestartState(
	EOutlierCheckpointRestartVoteView VoteView)
{
	const bool bWaitingForResponse =
		VoteView == EOutlierCheckpointRestartVoteView::RequesterWaiting;
	const AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer());

	if (MenuText)
	{
		MenuText->SetText(bConfirmingGameExit
			? GameExitConfirmationText
			: (bWaitingForResponse ? CheckpointWaitingText : DefaultMenuText));
	}
	if (ContinueButton)
	{
		ContinueButton->SetIsEnabled(!bWaitingForResponse);
	}
	if (SettingButton)
	{
		SettingButton->SetIsEnabled(!bWaitingForResponse && !bConfirmingGameExit);
	}
	if (RestartCheckpointButton)
	{
		RestartCheckpointButton->SetIsEnabled(
			!bWaitingForResponse
			&& !bConfirmingGameExit
			&& FirstPersonController
			&& FirstPersonController->CanRequestCheckpointRestart());
	}
	if (TitleButton)
	{
		TitleButton->SetIsEnabled(!bWaitingForResponse);
	}
}
