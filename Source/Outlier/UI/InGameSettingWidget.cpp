#include "UI/InGameSettingWidget.h"

#include "Components/Button.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/SettingWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerKeyHintWidget.h"
#include "UI/UILayerTypes.h"

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
	PushKeyHintLayer();
}

void UInGameSettingWidget::NativeDestruct()
{
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
	HandleContinueButtonClicked();
	return true;
}

bool UInGameSettingWidget::HandleUILayerConfirmed_Implementation()
{
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
	// Checkpoint restart behavior is intentionally left for the next pass.
}

void UInGameSettingWidget::HandleTitleButtonClicked()
{
	// Title transition behavior is intentionally left for the next pass.
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
