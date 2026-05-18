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

	//UE_LOG(LogTemp, Warning, TEXT("[FrontendPC] AcknowledgePossession: Pawn=%s"), *GetNameSafe(P));

	if (!P)
	{
		return;
	}

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

	if (TitleWidget)
	{
		TitleWidget->RemoveFromParent();
		TitleWidget = nullptr;
	}
}

void AFrontendPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	//UE_LOG(LogTemp, Warning,
	//	TEXT("[FrontendPC] EndPlay PC=%s Local=%d Auth=%d Pawn=%s"),
	//	*GetNameSafe(this),
	//	IsLocalController(),
	//	HasAuthority(),
	//	*GetNameSafe(GetPawn()));

	Super::EndPlay(EndPlayReason);
}

void AFrontendPlayerController::ServerRequestMatchmaking_Implementation()
{
	//UE_LOG(LogTemp, Warning, TEXT("[PC] ServerRequestMatchmaking_Implementation called on %s"),
	//	HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"));

	UOutlierMatchmakingSubsystem* Matchmaking =
		GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>();

	if (!Matchmaking)
	{
		UE_LOG(LogTemp, Error, TEXT("[PC] MatchmakingSubsystem is NULL"));
		return;
	}

	/*UE_LOG(LogTemp, Warning, TEXT("[PC] Calling EnqueueForPairThenRolePick"));*/
	Matchmaking->EnqueueForPairThenRolePick(this);
}

void AFrontendPlayerController::RequestSelectLobbyRole(EOutlierPlayerRole DesiredRole)
{
	ServerRequestSelectLobbyRole(DesiredRole);
}

void AFrontendPlayerController::RequestStartPendingMatch()
{
	ServerRequestStartPendingMatch();
}

void AFrontendPlayerController::ClientPrepareForMatch_Implementation()
{
	if (TitleWidget)
	{
		TitleWidget->RemoveFromParent();
		TitleWidget = nullptr;
	}

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
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

	//UE_LOG(LogTemp, Warning,
	//	TEXT("[LobbyRPC-Server] Arrived PC=%s Local=%d Auth=%d Role=%d"),
	//	*GetNameSafe(this),
	//	IsLocalController(),
	//	HasAuthority(),
	//	(int32)DesiredRole);

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
