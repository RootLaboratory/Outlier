// Fill out your copyright notice in the Description page of Project Settings.


#include "FrontendPlayerController.h"
#include "Network/OutlierMatchmakingSubsystem.h"
#include "UI/LoadingWidget.h"
#include "UI/TitleMainWidget.h"


void AFrontendPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		if (TitleWidgetClass)
		{
			TitleWidget = CreateWidget<UTitleMainWidget>(this, TitleWidgetClass);
		}

		if (TitleWidget)
		{
			TitleWidget->AddToViewport();
		}

		FInputModeUIOnly InputMode;
		SetInputMode(InputMode);
		bShowMouseCursor = true;
	}
}

void AFrontendPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
}

void AFrontendPlayerController::ServerRequestMatchmaking_Implementation()
{
	UOutlierMatchmakingSubsystem* Matchmaking =
		GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>();

	if (Matchmaking)
	{
		Matchmaking->EnqueueForPairThenRolePick(this);
	}
}

void AFrontendPlayerController::RequestSelectLobbyRole(EOutlierPlayerRole DesiredRole)
{
	ServerRequestSelectLobbyRole(DesiredRole);
}

void AFrontendPlayerController::RequestStartPendingMatch()
{
	ServerRequestStartPendingMatch();
}


void AFrontendPlayerController::ServerRequestSelectLobbyRole_Implementation(EOutlierPlayerRole DesiredRole)
{
	UOutlierMatchmakingSubsystem* MatchmakingSubsystem =
		GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>()
		: nullptr;
	if (!MatchmakingSubsystem)
	{
		return;
	}

	MatchmakingSubsystem->SelectRoleInPendingMatch(this, DesiredRole);
}

void AFrontendPlayerController::ServerRequestStartPendingMatch_Implementation()
{
	UOutlierMatchmakingSubsystem* MatchmakingSubsystem =
		GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>()
		: nullptr;
	if (!MatchmakingSubsystem)
	{
		return;
	}

	MatchmakingSubsystem->TryStartPendingMatch(this);
}
