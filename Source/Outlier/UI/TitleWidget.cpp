// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/TitleWidget.h"
#include "Components/Button.h"
#include "Engine/LocalPlayer.h"
#include "FrontendPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/CreditWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/LobbyWidget.h"
#include "UI/SettingWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerKeyHintWidget.h"


void UTitleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (StartButton)
	{
		StartButton->OnClicked.AddUniqueDynamic(this, &UTitleWidget::HandleStartButtonEvent);
	}

	if (ExitButton)
	{
		ExitButton->OnClicked.AddUniqueDynamic(this, &UTitleWidget::HandleExitButtonEvent);
	}

	if (CreditButton)
	{
		CreditButton->OnClicked.AddUniqueDynamic(this, &UTitleWidget::HandleCreditButtonEvent);
	}

	if (SettingButton)
	{
		SettingButton->OnClicked.AddUniqueDynamic(this, &UTitleWidget::HandleSettingButtonEvent);
	}

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

bool UTitleWidget::HandleUILayerEscape_Implementation()
{
	RequestExit();
	return true;
}

bool UTitleWidget::HandleUILayerConfirmed_Implementation()
{
	HandleStartButtonEvent();
	return true;
}

bool UTitleWidget::HandleUILayerUp_Implementation()
{
	return false;
}

bool UTitleWidget::HandleUILayerDown_Implementation()
{
	return false;
}

bool UTitleWidget::HandleUILayerLeft_Implementation()
{
	return false;
}

bool UTitleWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void UTitleWidget::HandleStartButtonEvent()
{
	// TitleWidget, base에 해당 Widget을 끄고 LobbyWidget을 활성화 시키게 해댤라고 요청 해야함.
	OnStartRequested.Broadcast();
	PushLobbyLayer();
}

void UTitleWidget::HandleExitButtonEvent()
{
	// Process 종료;
	OnExitRequested.Broadcast();
	RequestExit();
}

void UTitleWidget::HandleCreditButtonEvent()
{
	OnCreditRequested.Broadcast();
	PushCreditLayer();
}

void UTitleWidget::HandleSettingButtonEvent()
{
	OnSettingRequested.Broadcast();
	PushSettingLayer();
}

void UTitleWidget::PushLobbyLayer()
{
	AFrontendPlayerController* FrontendPC = Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (!FrontendPC || !LobbyWidgetClass)
	{
		return;
	}

	FrontendPC->ServerRequestMatchmaking();

	FUILayerPushRequest PushRequest;
	PushRequest.WidgetClass = LobbyWidgetClass;
	PushRequest.LayerTag = UILayerTags::GameMenu();
	PushRequest.InputModeTag = FrontendInputModeTags::UI();
	PushRequest.RequestOwner = FrontendPC;
	PushRequest.ContextActors = { FrontendPC };
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
