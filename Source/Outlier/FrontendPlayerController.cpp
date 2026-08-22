// Fill out your copyright notice in the Description page of Project Settings.


#include "FrontendPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Network/OutlierMatchmakingSubsystem.h"
#include "UI/LoadingWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/TitleWidget.h"
#include "UI/UILayerGameplayTags.h"


void AFrontendPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		if (TitleWidgetClass)
		{
			TitleWidget = CreateWidget<UTitleWidget>(this, TitleWidgetClass);
		}

		if (TitleWidget)
		{
			ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
				? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
				: nullptr;
			if (LayerSubsystem)
			{
				TitleLayerHandle = LayerSubsystem->PushWidget(
					UILayerTags::GameMenu(),
					TitleWidget,
					DefaultFrontendInputMode.IsValid()
						? DefaultFrontendInputMode
						: FrontendInputModeTags::UI(),
					this,
					EUILayerFocusTarget::Widget,
					true);
			}
		}
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

	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr)
	{
		LayerSubsystem->ClearAllLayers();
	}

	TitleLayerHandle.Reset();
	TitleWidget = nullptr;

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
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

void AFrontendPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
		{
			if (CurrentContext)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}
	}

	UEnhancedInputComponent* EnhancedInputComponent =
		Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInputComponent)
	{
		return;
	}

	if (WidgetEscapeAction)
	{
		EnhancedInputComponent->BindAction(
			WidgetEscapeAction,
			ETriggerEvent::Started,
			this,
			&AFrontendPlayerController::HandleWidgetEscapeInput);
	}

	if (WidgetConfirmedAction)
	{
		EnhancedInputComponent->BindAction(
			WidgetConfirmedAction,
			ETriggerEvent::Started,
			this,
			&AFrontendPlayerController::HandleWidgetConfirmedInput);
	}

	if (WidgetUpAction)
	{
		EnhancedInputComponent->BindAction(
			WidgetUpAction,
			ETriggerEvent::Started,
			this,
			&AFrontendPlayerController::HandleWidgetUpInput);
	}

	if (WidgetDownAction)
	{
		EnhancedInputComponent->BindAction(
			WidgetDownAction,
			ETriggerEvent::Started,
			this,
			&AFrontendPlayerController::HandleWidgetDownInput);
	}

	if (WidgetLeftAction)
	{
		EnhancedInputComponent->BindAction(
			WidgetLeftAction,
			ETriggerEvent::Started,
			this,
			&AFrontendPlayerController::HandleWidgetLeftInput);
	}

	if (WidgetRightAction)
	{
		EnhancedInputComponent->BindAction(
			WidgetRightAction,
			ETriggerEvent::Started,
			this,
			&AFrontendPlayerController::HandleWidgetRightInput);
	}
}

bool AFrontendPlayerController::SetFrontendInputMode(FGameplayTag NewInputMode)
{
	if (!IsLocalController() || !NewInputMode.IsValid())
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		return false;
	}

	const FGameplayTagContainer CurrentInputMode = InputSubsystem->GetInputMode();
	FGameplayTagContainer InputModeContainer;
	InputModeContainer.AddTag(NewInputMode);

	if (CurrentInputMode.HasAllExact(InputModeContainer)
		&& InputModeContainer.HasAllExact(CurrentInputMode))
	{
		CurrentFrontendInputMode = NewInputMode;
		return true;
	}

	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Options.bForceImmediately = true;

	InputSubsystem->SetInputMode(InputModeContainer, Options);
	CurrentFrontendInputMode = NewInputMode;
	return true;
}

bool AFrontendPlayerController::RestoreFrontendDefaultInputMode()
{
	const FGameplayTag RestoreInputMode = DefaultFrontendInputMode.IsValid()
		? DefaultFrontendInputMode
		: FrontendInputModeTags::UI();
	if (!SetFrontendInputMode(RestoreInputMode))
	{
		return false;
	}

	FInputModeGameAndUI InputMode;
	if (TitleWidget)
	{
		InputMode.SetWidgetToFocus(TitleWidget->TakeWidget());
	}
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	return true;
}

void AFrontendPlayerController::HandleWidgetEscapeInput()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->RouteWidgetEscapeInput();
	}
}

void AFrontendPlayerController::HandleWidgetConfirmedInput()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->RouteWidgetConfirmedInput();
	}
}

void AFrontendPlayerController::HandleWidgetUpInput()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->RouteWidgetUpInput();
	}
}

void AFrontendPlayerController::HandleWidgetDownInput()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->RouteWidgetDownInput();
	}
}

void AFrontendPlayerController::HandleWidgetLeftInput()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->RouteWidgetLeftInput();
	}
}

void AFrontendPlayerController::HandleWidgetRightInput()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->RouteWidgetRightInput();
	}
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

void AFrontendPlayerController::RequestCancelMatchmaking()
{
	ServerRequestCancelMatchmaking();
}

void AFrontendPlayerController::ServerRequestCancelMatchmaking_Implementation()
{
	UOutlierMatchmakingSubsystem* Matchmaking = GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>();
	if (Matchmaking)
	{
		Matchmaking->Cancel(this);
	}
}

void AFrontendPlayerController::ClientPrepareForMatch_Implementation()
{
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr)
	{
		LayerSubsystem->ClearAllLayers();
	}

	TitleLayerHandle.Reset();
	TitleWidget = nullptr;

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
