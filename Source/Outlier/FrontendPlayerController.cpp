// Fill out your copyright notice in the Description page of Project Settings.


#include "FrontendPlayerController.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Cinematic/OutlierCinematicSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameModeBase.h" //디버깅
#include "Input/ControllerInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Network/OutlierMatchmakingSubsystem.h"
#include "OutlierGameInstance.h"
#include "TimerManager.h"
#include "UI/LoadingWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/TitleWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"


void AFrontendPlayerController::BeginPlay()
{
	Super::BeginPlay();
	RefreshInputSettingsContext();

	/*UE_LOG(LogTemp, Warning,
		TEXT("[TitleDiag] FrontendPC BeginPlay PC=%s Local=%d TitleWidgetClass=%s GameMode=%s Map=%s"),
		*GetNameSafe(this),
		IsLocalController(),
		*GetNameSafe(TitleWidgetClass),
		*GetNameSafe(GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr),
		GetWorld() ? *GetWorld()->GetMapName() : TEXT("None"));*/

	if (IsLocalController())
	{
		if (UOutlierGameInstance* Instance = Cast<UOutlierGameInstance>(GetGameInstance()))
		{
			FGuid LocalId;
			if (Instance->GetLocalPlayerId(LocalId))
			{
				ServerRegisterLocalPlayerId(LocalId);
			}
		}
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

		// 타이틀 UI가 화면에 표시된 후 배경 재생을 시작한다.
		if (TitleLayerHandle.IsValid())
		{
			if (UOutlierCinematicSubsystem* Cinematic = GetLocalPlayer()->GetSubsystem<UOutlierCinematicSubsystem>())
			{
				Cinematic->PlayTitleCinematic();
			}
		}
	}

	TryStartNetworkMvpSmoke();
}

void AFrontendPlayerController::RefreshInputSettingsContext()
{
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer(); LocalPlayer && LocalPlayer->PlayerController == this)
	{
		if (ULocalPlayerSettingsSubsystem* Settings = LocalPlayer->GetSubsystem<ULocalPlayerSettingsSubsystem>())
		{
			Settings->RefreshInputSettingsContext();
		}
	}
}

void AFrontendPlayerController::InitPlayerState()
{
	Super::InitPlayerState();
	RefreshInputSettingsContext();
}

void AFrontendPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshInputSettingsContext();
}

void AFrontendPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TitleDiag] FrontendPC AcknowledgePossession Pawn=%s TitleWidget=%s KeepTitleLayer=1"),
		*GetNameSafe(P),
		*GetNameSafe(TitleWidget));
}

void AFrontendPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	//UE_LOG(LogTemp, Warning,
	//	TEXT("[FrontendPC] EndPlay PC=%s Local=%d Auth=%d Pawn=%s"),
	//	*GetNameSafe(this),
	//	IsLocalController(),
	//	HasAuthority(),
	//	*GetNameSafe(GetPawn()));

	GetWorldTimerManager().ClearTimer(NetworkMvpSmokeTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AFrontendPlayerController::TryStartNetworkMvpSmoke()
{
	if (!IsLocalController() || GetNetMode() != NM_Client)
	{
		return;
	}

	FString RoleText;
	if (!FParse::Value(
		FCommandLine::Get(),
		TEXT("OutlierNetworkSmokeRole="),
		RoleText))
	{
		return;
	}

	if (RoleText.Equals(TEXT("Shooter"), ESearchCase::IgnoreCase))
	{
		NetworkMvpSmokeRole = EOutlierPlayerRole::Shooter;
	}
	else if (RoleText.Equals(TEXT("Partner"), ESearchCase::IgnoreCase))
	{
		NetworkMvpSmokeRole = EOutlierPlayerRole::Partner;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[NetworkMVP] Unsupported Smoke role: %s"),
			*RoleText);
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[NetworkMVP] Lobby Smoke driver started Role=%s"),
		*RoleText);
	GetWorldTimerManager().SetTimer(
		NetworkMvpSmokeTimerHandle,
		this,
		&AFrontendPlayerController::DriveNetworkMvpSmoke,
		0.25f,
		true,
		0.25f);
}

void AFrontendPlayerController::DriveNetworkMvpSmoke()
{
	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState || NetworkMvpSmokeRole == EOutlierPlayerRole::None)
	{
		return;
	}

	if (OutlierPlayerState->GetPendingLobbyMatchId() == INDEX_NONE)
	{
		if (!bNetworkMvpSmokeMatchmakingRequested)
		{
			bNetworkMvpSmokeMatchmakingRequested = true;
			UE_LOG(LogTemp, Display,
				TEXT("[NetworkMVP] Requesting Lobby matchmaking"));
			ServerRequestMatchmaking();
		}
		return;
	}

	if (OutlierPlayerState->GetPendingLobbyRole() != NetworkMvpSmokeRole)
	{
		if (!bNetworkMvpSmokeRoleRequested)
		{
			bNetworkMvpSmokeRoleRequested = true;
			UE_LOG(LogTemp, Display,
				TEXT("[NetworkMVP] Requesting Lobby role %d"),
				static_cast<int32>(NetworkMvpSmokeRole));
			RequestSelectLobbyRole(NetworkMvpSmokeRole);
		}
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[NetworkMVP] Lobby role selection confirmed"));
	GetWorldTimerManager().ClearTimer(NetworkMvpSmokeTimerHandle);
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
	ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || !LayerSubsystem->BindWidgetInput(EnhancedInputComponent, ControllerInputConfig))
	{
		return;
	}

	// InGameSettingAction은 인게임 전용이라 Frontend에서는 바인딩하지 않는다.
	if (ControllerInputConfig->WidgetEscapeAction)
	{
		EnhancedInputComponent->BindAction(
			ControllerInputConfig->WidgetEscapeAction, ETriggerEvent::Started,
			this, &AFrontendPlayerController::HandleWidgetEscapeInput);
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

void AFrontendPlayerController::RequestCreateParty()
{
	ServerRequestCreateParty();
}

void AFrontendPlayerController::RequestJoinParty(const FString& PartyCode)
{
	ServerRequestJoinParty(PartyCode);
}

void AFrontendPlayerController::RequestLeaveParty()
{
	ServerRequestLeaveParty();
}

void AFrontendPlayerController::ServerRequestCreateParty_Implementation()
{
	if (UOutlierMatchmakingSubsystem* Matchmaking = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>()
		: nullptr)
	{
		Matchmaking->CreateParty(this);
	}
}

void AFrontendPlayerController::ServerRequestJoinParty_Implementation(const FString& PartyCode)
{
	if (UOutlierMatchmakingSubsystem* Matchmaking = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>()
		: nullptr)
	{
		Matchmaking->JoinParty(this, PartyCode);
	}
}

void AFrontendPlayerController::ServerRequestLeaveParty_Implementation()
{
	if (UOutlierMatchmakingSubsystem* Matchmaking = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>()
		: nullptr)
	{
		Matchmaking->LeaveParty(this);
	}
}

void AFrontendPlayerController::ClientNotifyPartyResult_Implementation(
	EOutlierPartyRequestResult Result,
	const FString& PartyCode)
{
	switch (Result)
	{
	case EOutlierPartyRequestResult::Created:
	case EOutlierPartyRequestResult::MemberJoined:
	case EOutlierPartyRequestResult::Joined:
		CurrentPartyCode = PartyCode;
		break;

	case EOutlierPartyRequestResult::Left:
	case EOutlierPartyRequestResult::Disbanded:
		CurrentPartyCode.Reset();
		break;

	default:
		break;
	}

	OnPartyRequestResult.Broadcast(Result, PartyCode);
}

void AFrontendPlayerController::RequestSelectLobbyRole(EOutlierPlayerRole DesiredRole)
{
	ServerRequestSelectLobbyRole(DesiredRole);
}

void AFrontendPlayerController::RequestPreviewLobbyRole(EOutlierPlayerRole DesiredRole)
{
	ServerRequestPreviewLobbyRole(DesiredRole);
}

void AFrontendPlayerController::ServerRequestPreviewLobbyRole_Implementation(EOutlierPlayerRole DesiredRole)
{
	if (UOutlierMatchmakingSubsystem* Matchmaking = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>() : nullptr)
	{
		Matchmaking->PreviewRoleInPendingMatch(this, DesiredRole);
	}
}

void AFrontendPlayerController::RequestStartPendingMatch()
{
	ServerRequestStartPendingMatch();
}

bool AFrontendPlayerController::HasLocalSave() const
{
	const UOutlierGameInstance* Instance = Cast<UOutlierGameInstance>(GetGameInstance());
	FGuid SaveId;
	FGuid ResumeKey;
	return Instance && Instance->GetLocalSaveCredentials(SaveId, ResumeKey);
}

void AFrontendPlayerController::RequestContinuePendingMatch()
{
	const UOutlierGameInstance* Instance = Cast<UOutlierGameInstance>(GetGameInstance());
	FGuid SaveId;
	FGuid ResumeKey;
	if (Instance && Instance->GetLocalSaveCredentials(SaveId, ResumeKey))
	{
		ServerRequestContinuePendingMatch(SaveId, ResumeKey);
	}
}

void AFrontendPlayerController::ServerRegisterLocalPlayerId_Implementation(FGuid PlayerId)
{
	if (PlayerId.IsValid() && !RegisteredLocalPlayerId.IsValid())
	{
		RegisteredLocalPlayerId = PlayerId;
	}
}

void AFrontendPlayerController::ServerRequestContinuePendingMatch_Implementation(
	FGuid SaveId, FGuid ResumeKey)
{
	bool bAccepted = false;
	if (UOutlierMatchmakingSubsystem* Matchmaking = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>() : nullptr)
	{
		bAccepted = Matchmaking->TryContinuePendingMatch(this, SaveId, ResumeKey);
	}
	ClientNotifyContinueSaveResult(bAccepted);
}

void AFrontendPlayerController::ClientNotifyContinueSaveResult_Implementation(bool bAccepted)
{
	OnContinueSaveResult.Broadcast(bAccepted);
}

void AFrontendPlayerController::ClientReceiveSaveCredentials_Implementation(
	FGuid SaveId, FGuid ResumeKey)
{
	if (UOutlierGameInstance* Instance = Cast<UOutlierGameInstance>(GetGameInstance()))
	{
		if (!Instance->StoreLocalSaveCredentials(SaveId, ResumeKey))
		{
			UE_LOG(LogTemp, Error, TEXT("[Checkpoint] Failed to store local resume credentials"));
		}
	}
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
	PrepareForMatch();
}

void AFrontendPlayerController::PrepareForMatch()
{
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr)
	{
		LayerSubsystem->ClearAllLayers();
	}

	TitleLayerHandle.Reset();
	TitleWidget = nullptr;

	// Finish removing frontend UI before stopping its persistent music.
	// Remote server-side controllers share the listen host's GameInstance.
	if (IsLocalController())
	{
		// 아레나가 이 월드로 스트리밍되므로 프런트엔드 종료 시 반복 재생 중인 타이틀 카메라도 정리한다.
		if (UOutlierCinematicSubsystem* Cinematic = GetLocalPlayer()
			? GetLocalPlayer()->GetSubsystem<UOutlierCinematicSubsystem>()
			: nullptr)
		{
			Cinematic->ClearLevelCinematics();
		}
		// 타이틀 카메라가 빠지면 FirstPersonPC가 LocalPlayer를 넘겨받기 전까지 이 PC 시점으로
		// 아레나 스트리밍이 노출된다. 시네마틱 상태 복원 뒤에 암전해 이후 LevelLoad 암전과 잇는다.
		// 이 PC는 교체로 파괴되므로 해제하지 않는다.
		if (PlayerCameraManager)
		{
			PlayerCameraManager->SetManualCameraFade(1.0f, FLinearColor::Black, false);
		}
		if (UOutlierAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>())
		{
			Audio->EnterGameplayMusic();
		}
	}

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}

void AFrontendPlayerController::ClientHandoffToArena_Implementation(const FString& ArenaUrl)
{
	if (ArenaUrl.IsEmpty())
	{
		return;
	}

	if (UOutlierGameInstance* OutlierGameInstance = Cast<UOutlierGameInstance>(GetGameInstance()))
	{
		OutlierGameInstance->NotifyArenaHandoffStarted(ArenaUrl);
	}

	PrepareForMatch();
	ClientTravel(ArenaUrl, TRAVEL_Absolute);
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
