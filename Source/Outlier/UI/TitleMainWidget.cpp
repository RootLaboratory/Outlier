// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/TitleMainWidget.h"
#include "UI/TitleWidget.h"
#include "UI/CreditWidget.h"
#include "UI/LobbyWidget.h"
#include "UI/SettingWidget.h"
#include "UI/UILayerKeyHintWidget.h"
#include "FrontendPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerGameplayTags.h"

void UTitleMainWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleWidget)
	{
		TitleWidget->OnStartRequested.AddUniqueDynamic(this, &UTitleMainWidget::StartPressed);
		TitleWidget->OnExitRequested.AddUniqueDynamic(this, &UTitleMainWidget::RequestExit);
		TitleWidget->OnCreditRequested.AddUniqueDynamic(this, &UTitleMainWidget::HandleCreditRequested);
		TitleWidget->OnSettingRequested.AddUniqueDynamic(this, &UTitleMainWidget::HandleSettingRequested);
	}

	PushKeyHintLayer();
}

void UTitleMainWidget::StartPressed()
{
	
	AFrontendPlayerController* PC = Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (PC)

	{
		PC->ServerRequestMatchmaking();
		ShowLobby();
	}
}

void UTitleMainWidget::ExitPressed()
{
}

void UTitleMainWidget::ShowLobby()
{
	APlayerController* OwningPlayer = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = OwningPlayer ? OwningPlayer->GetLocalPlayer() : nullptr;
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || LobbyLayerHandle.IsValid())
	{
		return;
	}

	if (!ActiveLobbyWidget)
	{
		ActiveLobbyWidget = LobbyWidgetClass
			? CreateWidget<ULobbyWidget>(OwningPlayer, LobbyWidgetClass)
			: nullptr;
	}

	if (!ActiveLobbyWidget)
	{
		return;
	}

	ActiveLobbyWidget->OnBackRequested.AddUniqueDynamic(
		this,
		&UTitleMainWidget::HandleLobbyBackRequested);

	TArray<AActor*> ContextActors;
	if (OwningPlayer)
	{
		ContextActors.Add(OwningPlayer);
	}
	IUILayerContextReceiver::Execute_InitializeUILayerContext(
		ActiveLobbyWidget,
		ContextActors);

	LobbyLayerHandle = LayerSubsystem->PushWidget(
		UILayerTags::GameMenu(),
		ActiveLobbyWidget,
		FrontendInputModeTags::UI(),
		this,
		EUILayerFocusTarget::Widget,
		true);

	if (LobbyLayerHandle.IsValid() && TitleWidget)
	{
		TitleWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UTitleMainWidget::ShowTitle()
{
	if (TitleWidget)
	{
		TitleWidget->SetVisibility(ESlateVisibility::Visible);
	}
}

void UTitleMainWidget::RequestExit()
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
		false
	);


	UE_LOG(LogTemp, Error, TEXT("QuitGame"));
}

void UTitleMainWidget::HandleLobbyBackRequested()
{
	AFrontendPlayerController* PC = Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PC->RequestCancelMatchmaking();
	}
	PopLobbyLayer();
	ShowTitle();
}

void UTitleMainWidget::HandleCreditRequested()
{
	PushCreditLayer();
}

void UTitleMainWidget::HandleSettingRequested()
{
	PushSettingLayer();
}

void UTitleMainWidget::PopLobbyLayer()
{
	if (!LobbyLayerHandle.IsValid())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopLayer(LobbyLayerHandle);
	}

	LobbyLayerHandle.Reset();
	ActiveLobbyWidget = nullptr;
}

void UTitleMainWidget::PushCreditLayer()
{
	APlayerController* OwningPlayer = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = OwningPlayer ? OwningPlayer->GetLocalPlayer() : nullptr;
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || !CreditWidgetClass)
	{
		return;
	}

	if (!ActiveCreditWidget)
	{
		ActiveCreditWidget = CreateWidget<UCreditWidget>(
			OwningPlayer,
			CreditWidgetClass);
	}

	if (!ActiveCreditWidget)
	{
		return;
	}

	TArray<AActor*> ContextActors;
	if (OwningPlayer)
	{
		ContextActors.Add(OwningPlayer);
	}
	IUILayerContextReceiver::Execute_InitializeUILayerContext(
		ActiveCreditWidget,
		ContextActors);

	LayerSubsystem->PushWidget(
		UILayerTags::GameMenu(),
		ActiveCreditWidget,
		FrontendInputModeTags::UI(),
		this,
		EUILayerFocusTarget::Widget,
		true);
}

void UTitleMainWidget::PushSettingLayer()
{
	APlayerController* OwningPlayer = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = OwningPlayer ? OwningPlayer->GetLocalPlayer() : nullptr;
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || !SettingWidgetClass)
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
	if (OwningPlayer)
	{
		ContextActors.Add(OwningPlayer);
	}
	IUILayerContextReceiver::Execute_InitializeUILayerContext(
		ActiveSettingWidget,
		ContextActors);

	LayerSubsystem->PushWidget(
		UILayerTags::GameMenu(),
		ActiveSettingWidget,
		FrontendInputModeTags::UI(),
		this,
		EUILayerFocusTarget::Widget,
		true);
}

void UTitleMainWidget::PushKeyHintLayer()
{
	if (KeyHintLayerHandle.IsValid())
	{
		return;
	}

	APlayerController* OwningPlayer = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = OwningPlayer ? OwningPlayer->GetLocalPlayer() : nullptr;
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || !KeyHintWidgetClass)
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

	KeyHintLayerHandle = LayerSubsystem->PushWidget(
		UILayerTags::Modal(),
		ActiveKeyHintWidget,
		FrontendInputModeTags::UI(),
		this,
		EUILayerFocusTarget::None,
		true,
		false);
}
