// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/TitleWidget.h"
#include "Engine/LocalPlayer.h"
#include "FrontendPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/CreditWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/LobbyWidget.h"
#include "UI/SettingWidget.h"
#include "UI/TitleButtonsWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerKeyHintWidget.h"


void UTitleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Construct는 레이어 재표시마다 다시 불릴 수 있어 native delegate는 초기화 때 한 번만 묶는다.
	if (TitleButtons)
	{
		TitleButtons->OnActionConfirmed.AddUObject(this, &UTitleWidget::HandleTitleButtonAction);
	}
}

void UTitleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	PushKeyHintLayer();
}

void UTitleWidget::NativeDestruct()
{
	if (KeyHintLayerHandle.IsValid())
	{
		ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
		ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
			? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
			: nullptr;
		if (LayerSubsystem)
		{
			LayerSubsystem->PopLayer(KeyHintLayerHandle);
		}
		KeyHintLayerHandle.Reset();
	}

	Super::NativeDestruct();
}

void UTitleWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
}

// 레이어 입력은 버튼 선택 상태를 가진 TitleButtons가 그대로 판단한다.
bool UTitleWidget::HandleUILayerEscape_Implementation()
{
	return TitleButtons
		&& IUILayerInputReceiver::Execute_HandleUILayerEscape(TitleButtons);
}

bool UTitleWidget::HandleUILayerConfirmed_Implementation()
{
	return TitleButtons
		&& IUILayerInputReceiver::Execute_HandleUILayerConfirmed(TitleButtons);
}

bool UTitleWidget::HandleUILayerUp_Implementation()
{
	return TitleButtons
		&& IUILayerInputReceiver::Execute_HandleUILayerUp(TitleButtons);
}

bool UTitleWidget::HandleUILayerDown_Implementation()
{
	return TitleButtons
		&& IUILayerInputReceiver::Execute_HandleUILayerDown(TitleButtons);
}

bool UTitleWidget::HandleUILayerLeft_Implementation()
{
	return TitleButtons
		&& IUILayerInputReceiver::Execute_HandleUILayerLeft(TitleButtons);
}

bool UTitleWidget::HandleUILayerRight_Implementation()
{
	return TitleButtons
		&& IUILayerInputReceiver::Execute_HandleUILayerRight(TitleButtons);
}

void UTitleWidget::HandleTitleButtonAction(ETitleButtonAction Action)
{
	switch (Action)
	{
	case ETitleButtonAction::Start:
		PushLobbyLayer();
		break;
	case ETitleButtonAction::Credit:
		PushCreditLayer();
		break;
	case ETitleButtonAction::Setting:
		PushSettingLayer();
		break;
	case ETitleButtonAction::Exit:
		RequestExit();
		break;
	}
}

void UTitleWidget::PushLobbyLayer()
{
	AFrontendPlayerController* FrontendPC = Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (!FrontendPC || !LobbyWidgetClass || LobbyLayerHandle.IsValid())
	{
		return;
	}

	FrontendPC->ServerRequestMatchmaking();

	if (!ActiveLobbyWidget)
	{
		ActiveLobbyWidget = CreateWidget<ULobbyWidget>(FrontendPC, LobbyWidgetClass);
	}

	if (!ActiveLobbyWidget)
	{
		return;
	}

	ActiveLobbyWidget->OnBackRequested.AddUniqueDynamic(
		this,
		&UTitleWidget::HandleLobbyBackRequested);

	IUILayerContextReceiver::Execute_InitializeUILayerContext(
		ActiveLobbyWidget,
		TArray<AActor*>{ FrontendPC });

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		return;
	}

	LobbyLayerHandle = LayerSubsystem->PushWidget(
		UILayerTags::GameMenu(),
		ActiveLobbyWidget,
		FrontendInputModeTags::UI(),
		this,
		EUILayerFocusTarget::Widget,
		true);

	if (!LobbyLayerHandle.IsValid())
	{
		return;
	}

	// Lobby가 같은 GameMenu 레이어에 겹쳐 그려지므로, Title 본인과 상시 떠있는
	// KeyHint(Confirm/Escape 안내)는 Lobby에 있는 동안 직접 숨겨준다.
	// (Escape로 복귀하면 HandleLobbyBackRequested에서 다시 보여준다.)
	SetVisibility(ESlateVisibility::Collapsed);
	if (ActiveKeyHintWidget)
	{
		ActiveKeyHintWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UTitleWidget::HandleLobbyBackRequested()
{
	LobbyLayerHandle.Reset();
	ActiveLobbyWidget = nullptr;

	SetVisibility(ESlateVisibility::Visible);
	if (ActiveKeyHintWidget)
	{
		ActiveKeyHintWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UTitleWidget::PushCreditLayer()
{
	APlayerController* OwningPlayer = GetOwningPlayer();
	if (!OwningPlayer || !CreditWidgetClass)
	{
		return;
	}

	FUILayerPushRequest PushRequest;
	PushRequest.WidgetClass = CreditWidgetClass;
	PushRequest.LayerTag = UILayerTags::GameMenu();
	PushRequest.InputModeTag = FrontendInputModeTags::UI();
	PushRequest.RequestOwner = OwningPlayer;
	PushRequest.ContextActors = { OwningPlayer };
	PushRequest.FocusTarget = EUILayerFocusTarget::Widget;
	PushRequest.bShowCursor = true;

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PushWidget(PushRequest);
	}
}

void UTitleWidget::PushSettingLayer()
{
	APlayerController* OwningPlayer = GetOwningPlayer();
	if (!OwningPlayer || !SettingWidgetClass)
	{
		return;
	}

	FUILayerPushRequest PushRequest;
	PushRequest.WidgetClass = SettingWidgetClass;
	PushRequest.LayerTag = UILayerTags::GameMenu();
	PushRequest.InputModeTag = FrontendInputModeTags::UI();
	PushRequest.RequestOwner = OwningPlayer;
	PushRequest.ContextActors = { OwningPlayer };
	PushRequest.FocusTarget = EUILayerFocusTarget::Widget;
	PushRequest.bShowCursor = true;

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PushWidget(PushRequest);
	}
}

void UTitleWidget::PushKeyHintLayer()
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
			FrontendInputModeTags::UI(),
			this,
			EUILayerFocusTarget::None,
			true,
			false);
	}
}

void UTitleWidget::RequestExit()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	UKismetSystemLibrary::QuitGame(
		this,
		PC,
		EQuitPreference::Quit,
		false);
}
