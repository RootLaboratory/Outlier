// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/TitleMainWidget.h"
#include "UI/TitleWidget.h"
#include "UI/LobbyWidget.h"
#include "FrontendPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

void UTitleMainWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleWidget)
	{
		TitleWidget->OnStartRequested.AddDynamic(this, &UTitleMainWidget::StartPressed);
		TitleWidget->OnExitRequested.AddDynamic(this, &UTitleMainWidget::RequestExit);
	}
	if (LobbyWidget)
	{
		LobbyWidget->OnBackRequested.AddDynamic(this, &UTitleMainWidget::HandleLobbyBackRequested);
		LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
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
	UE_LOG(LogTemp, Warning,
		TEXT("[TitleMain] ShowLobby TitleMain=%p LobbyWidget=%p OwningPC=%s Before=%d"),
		this,
		LobbyWidget.Get(),
		*GetNameSafe(GetOwningPlayer()),
		LobbyWidget ? (int32)LobbyWidget->GetVisibility() : -1);

	TitleWidget->SetVisibility(ESlateVisibility::Collapsed);
	LobbyWidget->SetVisibility(ESlateVisibility::Visible);

	UE_LOG(LogTemp, Warning,
		TEXT("[TitleMain] ShowLobby After=%d IsVisible=%d"),
		LobbyWidget ? (int32)LobbyWidget->GetVisibility() : -1,
		LobbyWidget ? LobbyWidget->IsVisible() : 0);
}

void UTitleMainWidget::ShowTitle()
{
	TitleWidget->SetVisibility(ESlateVisibility::Visible);
	LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
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
	ShowTitle();
}

