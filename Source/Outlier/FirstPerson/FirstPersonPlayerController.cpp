// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerController.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "InputMappingContext.h"
#include "FirstPersonPlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "LocalPlayerUISubSystem.h"
#include "OutlierGameInstance.h"
#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Outlier.h"
#include "MainUIBase.h"
#include "UI/UILayerGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "Network/OutlierArenaSubsystem.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "UI/InGamePauseWidget.h"
#include "UI/InGameSettingWidget.h"
#include "UI/PreSetLoadWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"
#include "Misc/StringOutputDevice.h"
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "Engine/LevelStreaming.h"
#include "HAL/PlatformTime.h"

namespace
{
	void ResolvePairCharactersForController(
		AFirstPersonPlayerController* Controller,
		AShooterCharacter*& OutShooterCharacter,
		APartnerCharacter*& OutPartnerCharacter)
	{
		OutShooterCharacter = nullptr;
		OutPartnerCharacter = nullptr;

		AOutlierPlayerState* RequestingPlayerState = Controller
			? Controller->GetPlayerState<AOutlierPlayerState>()
			: nullptr;
		if (!Controller || !RequestingPlayerState)
		{
			return;
		}

		OutShooterCharacter = RequestingPlayerState->GetShooterCharacter();
		OutPartnerCharacter = RequestingPlayerState->GetPartnerCharacter();

		if (!OutShooterCharacter)
		{
			OutShooterCharacter = Cast<AShooterCharacter>(Controller->GetPawn());
		}

		if (!OutPartnerCharacter)
		{
			OutPartnerCharacter = Cast<APartnerCharacter>(Controller->GetPawn());
		}

		if (OutShooterCharacter && OutPartnerCharacter)
		{
			return;
		}

		const int32 PairId = RequestingPlayerState->GetPairId();
		const AGameStateBase* GameState = Controller->GetWorld()
			? Controller->GetWorld()->GetGameState()
			: nullptr;
		if (!GameState || PairId == INDEX_NONE)
		{
			return;
		}

		for (APlayerState* RawPlayerState : GameState->PlayerArray)
		{
			const AOutlierPlayerState* CandidatePlayerState =
				Cast<AOutlierPlayerState>(RawPlayerState);
			if (!CandidatePlayerState || CandidatePlayerState->GetPairId() != PairId)
			{
				continue;
			}

			if (!OutShooterCharacter)
			{
				OutShooterCharacter = CandidatePlayerState->GetShooterCharacter();
			}

			if (!OutPartnerCharacter)
			{
				OutPartnerCharacter = CandidatePlayerState->GetPartnerCharacter();
			}

			if (OutShooterCharacter && OutPartnerCharacter)
			{
				return;
			}
		}
	}

	void PushUILayerToController(
		AFirstPersonPlayerController* Controller,
		const FUILayerPushRequest& Request)
	{
		if (!Controller)
		{
			return;
		}

		if (Controller->IsLocalController())
		{
			Controller->ClientPushUILayer_Implementation(Request);
			return;
		}

		Controller->ClientPushUILayer(Request);
	}

	void PopInGameSettingLayerFromController(
		AFirstPersonPlayerController* Controller,
		UObject* RequestOwner)
	{
		if (!Controller)
		{
			return;
		}

		if (Controller->IsLocalController())
		{
			Controller->ClientPopInGameSettingLayer_Implementation(RequestOwner);
			return;
		}

		Controller->ClientPopInGameSettingLayer(RequestOwner);
	}
}

AFirstPersonPlayerController::AFirstPersonPlayerController()
{
	// set the player camera manager
	PlayerCameraManagerClass = AFirstPersonPlayerCameraManager::StaticClass();
}

void AFirstPersonPlayerController::ServerRequestRelevantAudioAtLocation_Implementation(
	const FOutlierAudioPlayRequest& Request)
{
	UOutlierAudioSubsystem* AudioSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (AudioSubsystem)
	{
		AudioSubsystem->HandleServerRelevantAtLocationRequest(this, Request);
	}
}

void AFirstPersonPlayerController::ClientPlayResolvedAudio_Implementation(
	const FOutlierResolvedAudioPlay& ResolvedPlay)
{
	if (!IsLocalController())
	{
		return;
	}

	UOutlierAudioSubsystem* AudioSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (AudioSubsystem)
	{
		AudioSubsystem->PlayResolvedAudioLocally(ResolvedPlay);
	}
}

void AFirstPersonPlayerController::ClientStopResolvedAudio_Implementation(
	int32 AudioInstanceId)
{
	if (!IsLocalController())
	{
		return;
	}

	UOutlierAudioSubsystem* AudioSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (AudioSubsystem)
	{
		AudioSubsystem->StopLoopAudioLocally(AudioInstanceId);
	}
}

void AFirstPersonPlayerController::ClientPushUILayer_Implementation(
	const FUILayerPushRequest& Request)
{
	if (!IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		return;
	}

	LayerSubsystem->PushWidget(Request);
}

void AFirstPersonPlayerController::RequestOpenInGameSetting()
{
	if (!IsLocalController())
	{
		return;
	}

	ServerOpenInGameSetting();
}

void AFirstPersonPlayerController::RequestCloseInGameSetting()
{
	if (!IsLocalController())
	{
		return;
	}

	ServerCloseInGameSetting();
}

void AFirstPersonPlayerController::RequestLeaveGame()
{
	if (!IsLocalController() || bExplicitLeaveRequested)
	{
		return;
	}

	// 서버가 자발적 이탈을 먼저 확정해야 Logout을 장애 재접속으로 분류하지 않는다.
	// Reliable RPC를 전송한 뒤 로컬 Handoff를 지워 후속 NetworkFailure의 재접속도 막는다.
	bExplicitLeaveRequested = true;
	ServerRequestLeaveGame();
	if (UOutlierGameInstance* OutlierGameInstance =
		Cast<UOutlierGameInstance>(GetGameInstance()))
	{
		OutlierGameInstance->PrepareForExplicitLeave();
	}
}

void AFirstPersonPlayerController::RequestCheckpointRestart()
{
	// 로컬 플래그는 UI 요청을 거르는 용도다. 실제 요청 권한과 투표 가능 상태는 서버 GameMode가 다시 판정한다.
	if (!IsLocalController() || !bCanRequestCheckpointRestart)
	{
		return;
	}

	ServerRequestCheckpointRestart();
}

void AFirstPersonPlayerController::RequestCheckpointRestartResponse(bool bApprove)
{
	if (!IsLocalController()
		|| CheckpointRestartVoteView != EOutlierCheckpointRestartVoteView::ResponderPrompt)
	{
		return;
	}

	ServerRespondCheckpointRestart(bApprove);
}

void AFirstPersonPlayerController::RequestCancelCheckpointRestart()
{
	if (!IsLocalController()
		|| CheckpointRestartVoteView != EOutlierCheckpointRestartVoteView::RequesterWaiting)
	{
		return;
	}

	ServerCancelCheckpointRestart();
}

void AFirstPersonPlayerController::ClientConfigureCheckpointRestart_Implementation(
	bool bCanRequest)
{
	bCanRequestCheckpointRestart = bCanRequest;
	OnCheckpointRestartVoteViewChanged.Broadcast(CheckpointRestartVoteView);
}

void AFirstPersonPlayerController::ClientSetCheckpointRestartVoteView_Implementation(
	EOutlierCheckpointRestartVoteView VoteView)
{
	CheckpointRestartVoteView = VoteView;
	OnCheckpointRestartVoteViewChanged.Broadcast(CheckpointRestartVoteView);
}

void AFirstPersonPlayerController::ClientPrepareForArenaExit_Implementation()
{
	if (UOutlierGameInstance* OutlierGameInstance =
		Cast<UOutlierGameInstance>(GetGameInstance()))
	{
		OutlierGameInstance->PrepareForExplicitLeave();
	}
}

void AFirstPersonPlayerController::ClientConfigureListenReconnect_Implementation(FGuid ReconnectToken)
{
	if (UOutlierGameInstance* GameInstance = Cast<UOutlierGameInstance>(GetGameInstance()))
	{
		GameInstance->NotifyListenReconnectToken(ReconnectToken);
	}
}

void AFirstPersonPlayerController::ConfigureCheckpointRestartFromServer(bool bCanRequest)
{
	// Listen Host는 같은 프로세스의 로컬 UI를 즉시 갱신하고, 원격 플레이어만 Client RPC로 전달한다.
	if (IsLocalController())
	{
		ClientConfigureCheckpointRestart_Implementation(bCanRequest);
		return;
	}

	ClientConfigureCheckpointRestart(bCanRequest);
}

void AFirstPersonPlayerController::SetCheckpointRestartVoteViewFromServer(
	EOutlierCheckpointRestartVoteView VoteView)
{
	if (IsLocalController())
	{
		ClientSetCheckpointRestartVoteView_Implementation(VoteView);
		return;
	}

	ClientSetCheckpointRestartVoteView(VoteView);
}

void AFirstPersonPlayerController::CloseCheckpointRestartVoteUIFromServer(
	UObject* RequestOwner)
{
	PopInGameSettingLayerFromController(this, RequestOwner);
}

void AFirstPersonPlayerController::ClientPopInGameSettingLayer_Implementation(
	UObject* RequestOwner)
{
	if (!IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopLayersByOwner(RequestOwner);
	}
}

void AFirstPersonPlayerController::Client_ShowPresetSelect_Implementation()
{
	if (!IsLocalController() || !PresetLoadWidgetClass)
	{
		return;
	}

	// 사망 연출(Noise → Fade → Black)을 먼저 돌리고, Black 패스가 시작되는 순간 위젯을 띄운다.
	// 연출을 못 돌리는 환경이면 기다리지 않고 바로 띄운다.
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
		: nullptr)
	{
		PPSubsystem->OnDeathBlackoutStarted.RemoveAll(this);
		PPSubsystem->OnDeathBlackoutStarted.AddUObject(
			this,
			&AFirstPersonPlayerController::HandleDeathBlackoutStarted);

		if (PPSubsystem->StartDeathTransition())
		{
			return;
		}

		PPSubsystem->OnDeathBlackoutStarted.RemoveAll(this);
	}

	PushPresetSelectWidget();
}

void AFirstPersonPlayerController::HandleDeathBlackoutStarted()
{
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
		: nullptr)
	{
		PPSubsystem->OnDeathBlackoutStarted.RemoveAll(this);
	}

	PushPresetSelectWidget();
}

void AFirstPersonPlayerController::PushPresetSelectWidget()
{
	if (!IsLocalController() || !PresetLoadWidgetClass)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		return;
	}

	FUILayerPushRequest Request;
	Request.WidgetClass = PresetLoadWidgetClass;
	Request.LayerTag = UILayerTags::Gameplay();
	Request.InputModeTag = FirstPersonInputModeTags::UI();
	Request.RequestOwner = PlayerState;
	Request.FocusTarget = EUILayerFocusTarget::Widget;
	Request.bShowCursor = true;

	LayerSubsystem->PushWidget(Request);

	if (UPreSetLoadWidget* PresetWidget = Cast<UPreSetLoadWidget>(LayerSubsystem->GetTopLayerWidget()))
	{
		PresetWidget->OnPresetStageConfirmed.AddUniqueDynamic(this, &AFirstPersonPlayerController::HandleLocalPresetStageSelected);
	}
}

void AFirstPersonPlayerController::HandleLocalPresetStageSelected(FName StageId)
{
	Server_SelectPresetStage(StageId);
}

void AFirstPersonPlayerController::Server_SelectPresetStage_Implementation(FName StageId)
{
	if (AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GM->HandlePresetStageSelected(this, StageId);
	}
}

void AFirstPersonPlayerController::ServerTryActivateUpgradeNode_Implementation(
	AActor* UpgradeOwner,
	FName NodeIdOrRowName,
	UOutlierUpgradeSetData* UpgradeSetData)
{
	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	UOutlierUpgradeComponent* UpgradeComponent = UpgradeOwner
		? UpgradeOwner->FindComponentByClass<UOutlierUpgradeComponent>()
		: nullptr;
	if (!UpgradeComponent)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Upgrade] Server activate failed: component not found Controller=%s Owner=%s PlayerState=%s Node=%s"),
			*GetNameSafe(this),
			*GetNameSafe(UpgradeOwner),
			*GetNameSafe(OutlierPlayerState),
			*NodeIdOrRowName.ToString());
		return;
	}

	if (UpgradeSetData)
	{
		UpgradeComponent->SetUpgradeSetData(UpgradeSetData);
	}

	UpgradeComponent->TryActivateNodeForPlayerState(
		NodeIdOrRowName,
		OutlierPlayerState);
}

void AFirstPersonPlayerController::ClientPlayExplosionCameraShake_Implementation(
	TSubclassOf<UCameraShakeBase> CameraShakeClass,
	float Scale,
	bool bAllowInactivePawn)
{
	if (AFirstPersonPlayerCameraManager* CameraManager =
		Cast<AFirstPersonPlayerCameraManager>(PlayerCameraManager))
	{
		CameraManager->PlayExplosionCameraShake(CameraShakeClass, Scale, bAllowInactivePawn);
	}
}

bool AFirstPersonPlayerController::SetFirstPersonInputMode(FGameplayTag NewInputMode)
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
	if (CurrentInputMode.Num() == 1 && CurrentInputMode.HasTagExact(NewInputMode))
	{
		CurrentFirstPersonInputMode = NewInputMode;
		return true;
	}

	FGameplayTagContainer InputModeContainer;
	InputModeContainer.AddTag(NewInputMode);

	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Options.bForceImmediately = true;

	InputSubsystem->SetInputMode(InputModeContainer, Options);
	CurrentFirstPersonInputMode = NewInputMode;
	return true;
}

bool AFirstPersonPlayerController::TryRestoreFirstPersonDefaultInputMode(FGameplayTag ExpectedInputMode)
{
	if (!IsLocalController() || !ExpectedInputMode.IsValid())
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

	// A delayed cleanup from one ability must not replace a newer ability's input mode.
	const FGameplayTagContainer CurrentInputMode = InputSubsystem->GetInputMode();
	if (CurrentInputMode.Num() != 1 || !CurrentInputMode.HasTagExact(ExpectedInputMode))
	{
		return false;
	}

	return RestoreFirstPersonDefaultInputMode();
}

bool AFirstPersonPlayerController::RestoreFirstPersonDefaultInputMode()
{
	if (!IsLocalController())
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

	const FGameplayTagContainer& DefaultInputMode =
		GetDefault<UEnhancedInputDeveloperSettings>()->DefaultInputMode;
	if (DefaultInputMode.IsEmpty())
	{
		return false;
	}

	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Options.bForceImmediately = true;

	InputSubsystem->SetInputMode(DefaultInputMode, Options);
	CurrentFirstPersonInputMode = DefaultInputMode.First();

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	return true;
}

FGameplayTag AFirstPersonPlayerController::GetFirstPersonInputMode() const
{
	 return CurrentFirstPersonInputMode; 
}

bool AFirstPersonPlayerController::IsFirstPersonInputMode(FGameplayTag InputMode) const
{
	return CurrentFirstPersonInputMode.MatchesTagExact(InputMode);
}

void AFirstPersonPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// [입력 바인딩 디버그 — 2026-09-14 비활성화] 정상 경로 추적용. 실패 경로 경고만 남긴다.
	//UE_LOG(
	//	LogTemp,
	//	Warning,
	//	TEXT("[OutlierInputDebug] PC BeginPlay: %s Local=%d Authority=%d Pawn=%s MappingContexts=%d Role=%d PairId=%d"),
	//	*GetNameSafe(this),
	//	IsLocalPlayerController(),
	//	HasAuthority(),
	//	*GetNameSafe(GetPawn()),
	//	DefaultMappingContexts.Num(),
	//	static_cast<int32>(DefaultPlayerRole),
	//	DefaultPairId
	//);

	InitializeOutlierPlayerState();
}

void AFirstPersonPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	if (IsLocalController())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
			? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
			: nullptr)
		{
			// 리스폰으로 새 폰을 잡은 순간 사망 연출을 즉시 끊는다.
			if (bReleaseDeathTransitionOnPossess)
			{
				bReleaseDeathTransitionOnPossess = false;
				PPSubsystem->OnDeathBlackoutStarted.RemoveAll(this);
				PPSubsystem->ResetDeathTransition();
				// 새 Pawn을 실제로 잡은 경우에만 사망 연출을 해제하고,
				// Slate/backbuffer 단계의 기본 CA를 복구한다.
				PPSubsystem->SetChromaticAberrationEnabled(true);
			}

#if UE_BUILD_SHIPPING
			// 기존 렌즈 CA는 Shipping에서만, 게임 폰을 잡은 순간부터 켠다. 끄는 건 사망 연출 시작과 EndPlay.
			// 사망 연출 중(예: 연출 도중 Partner가 적을 해킹해 Possess)에는 새 CA와 겹치지 않게 켜지 않는다.
			// 리스폰 해제는 바로 위에서 끝났으므로 그 경우엔 여기서 다시 켜진다.
			if (!PPSubsystem->IsDeathTransitionActive())
			{
				PPSubsystem->SetChromaticAberrationEnabled(true);
			}
#endif
		}
	}

	ReportLoadedLevelsVisibilityToServer();
	TryNotifyArenaStartReady();
}

void AFirstPersonPlayerController::ArmDeathTransitionReleaseOnPossess()
{
	bReleaseDeathTransitionOnPossess = true;
}

void AFirstPersonPlayerController::ReportLoadedLevelsVisibilityToServer()
{
	if (!IsLocalController())
	{
		return;
	}

	// 엔진은 클라 PC가 커넥션에 묶이는 시점(UNetConnection::HandleClientPlayer)에 한 번
	// 레벨 가시성을 다시 보고한다. 그런데 그 보고는 이미 visible인 레벨만 대상이라,
	// 가시화 대기 중인 셀은 재요청되지 않는다. 게다가 엔진에는 재전송이 없어서
	// (LevelStreaming.cpp의 ClientPendingRequestIndex 대기) 보고가 한 번 유실되면
	// 그 셀은 클라 입장에서 영원히 스트리밍 미완료로 남는다.
	// Possess가 확정된 이 시점에 같은 일을 한 번 더 해서 안전하게 맞춘다.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<FUpdateLevelVisibilityLevelInfo> LevelVisibilities;
	for (ULevelStreaming* LevelStreaming : World->GetStreamingLevels())
	{
		if (!LevelStreaming)
		{
			continue;
		}

		const ULevel* Level = LevelStreaming->GetLoadedLevel();
		if (Level && Level->bIsVisible && !Level->bClientOnlyVisible)
		{
			FUpdateLevelVisibilityLevelInfo& LevelVisibility = LevelVisibilities.Emplace_GetRef(Level, true);
			LevelVisibility.PackageName = NetworkRemapPath(LevelVisibility.PackageName, false);
		}
	}

	if (LevelVisibilities.Num() > 0)
	{
		ServerUpdateMultipleLevelsVisibility(LevelVisibilities);
	}
}

void AFirstPersonPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// [입력 바인딩 디버그 — 2026-09-14 비활성화]
	//UE_LOG(
	//	LogTemp,
	//	Warning,
	//	TEXT("[OutlierInputDebug] PC OnPossess: %s Pawn=%s PawnClass=%s Authority=%d"),
	//	*GetNameSafe(this),
	//	*GetNameSafe(InPawn),
	//	InPawn ? *GetNameSafe(InPawn->GetClass()) : TEXT("None"),
	//	HasAuthority()
	//);

	InitializeOutlierPlayerState();
	RegisterCurrentPawnWithPlayerState();
}

void AFirstPersonPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	InitializeOutlierPlayerState();
}

void AFirstPersonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// [입력 바인딩 디버그 — 2026-09-14 비활성화]
	//UE_LOG(
	//	LogTemp,
	//	Warning,
	//	TEXT("[OutlierInputDebug] PC SetupInputComponent: %s Local=%d MappingContexts=%d"),
	//	*GetNameSafe(this),
	//	IsLocalPlayerController(),
	//	DefaultMappingContexts.Num()
	//);

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				// [입력 바인딩 디버그 — 2026-09-14 비활성화]
				// 매핑 컨텍스트/액션/키를 전수 덤프하던 블록. 컨텍스트 하나당 수십 줄이 나온다.
				// 입력이 안 먹는 문제를 다시 쫓게 되면 되살릴 것.
				//UE_LOG(
				//	LogTemp,
				//	Warning,
				//	TEXT("[OutlierInputDebug] AddMappingContext: PC=%s Context=%s"),
				//	*GetNameSafe(this),
				//	*GetNameSafe(CurrentContext)
				//);
				//
				//if (CurrentContext)
				//{
				//	const TArray<FEnhancedActionKeyMapping>& Mappings = CurrentContext->GetMappings();
				//	UE_LOG(
				//		LogTemp,
				//		Warning,
				//		TEXT("[OutlierInputDebug] MappingContextDump: Context=%s MappingCount=%d"),
				//		*GetNameSafe(CurrentContext),
				//		Mappings.Num()
				//	);
				//
				//	for (const FEnhancedActionKeyMapping& Mapping : Mappings)
				//	{
				//		UE_LOG(
				//			LogTemp,
				//			Warning,
				//			TEXT("[OutlierInputDebug] Mapping: Context=%s Action=%s Key=%s Triggers=%d Modifiers=%d"),
				//			*GetNameSafe(CurrentContext),
				//			*GetNameSafe(Mapping.Action),
				//			*Mapping.Key.ToString(),
				//			Mapping.Triggers.Num(),
				//			Mapping.Modifiers.Num()
				//		);
				//	}
				//}

				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			CurrentFirstPersonInputMode = Subsystem->GetInputMode().First();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] EnhancedInputLocalPlayerSubsystem is null: %s"), *GetNameSafe(this));
		}
	}

}




TSubclassOf<UMainUIBase> AFirstPersonPlayerController::GetMainUIClass_Implementation() const
{
	return MainUIClass;
}

void AFirstPersonPlayerController::BindMainUI()
{

}

void AFirstPersonPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearClientArenaContentWait();
	// 재접속이나 역할 Controller 교체로 이전 PC가 먼저 파괴될 수 있다. Delegate가 남아 있으면
	// 다음 Generation의 Ready를 폐기될 PC가 받아 서버에 잘못 보고하므로 여기서 직접 끊는다.
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr)
	{
		ArenaSubsystem->OnArenaGameplayUnloaded.RemoveAll(this);
		ArenaSubsystem->OnArenaGameplayReady.RemoveAll(this);
	}

	// AddToViewport로 붙인 Widget은 GameViewportClient가 들고 있어서 PC가 파괴돼도
	// 화면에 남는다. Role에 따라 Controller를 교체할 때 이전 Controller의 MainUI가
	// 그대로 보이므로 직접 정리한다.
	if (UMainUIBase* MainUI = ShooterUIInstance)
	{
		if (ULocalPlayer* LP = GetLocalPlayer())
		{
			if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
			{
				UISubsystem->UnregisterMainUI(MainUI);
			}

			if (ULocalPlayerUILayerSubsystem* LayerSubsystem =
				LP->GetSubsystem<ULocalPlayerUILayerSubsystem>())
			{
				LayerSubsystem->UnregisterMainUI(MainUI);
			}
		}

		MainUI->RemoveFromParent();
		ShooterUIInstance = nullptr;
	}

	// PostProcess 서브시스템은 LocalPlayer 소속이라 맵을 옮겨도 살아남는다. 월드를 떠날 때 기존 CA와
	// 사망 연출을 끄지 않으면 로비 / 타이틀까지 그대로 따라간다.
	// 교체돼서 이미 LocalPlayer를 넘겨준 옛 PC는 새 PC가 켠 상태를 건드리면 안 된다.
	ULocalPlayer* LP = GetLocalPlayer();
	if (LP && LP->PlayerController == this)
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
			LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->OnDeathBlackoutStarted.RemoveAll(this);
			PPSubsystem->ResetDeathTransition();
			PPSubsystem->SetChromaticAberrationEnabled(false);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFirstPersonPlayerController::BindPostProcessSubSystem()
{
}

void AFirstPersonPlayerController::InitializeOutlierPlayerState()
{
	if (!HasAuthority())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] InitializeOutlierPlayerState failed: PlayerState null PC=%s"), *GetNameSafe(this));
		return;
	}

	//OutlierPlayerState->SetPlayerRole(DefaultPlayerRole);
	//OutlierPlayerState->SetPairId(DefaultPairId);

	//UE_LOG(
	//	LogTemp,
	//	Warning,
	//	TEXT("[OutlierInputDebug] InitializeOutlierPlayerState: PC=%s PS=%s Role=%d PairId=%d"),
	//	*GetNameSafe(this),
	//	*GetNameSafe(OutlierPlayerState),
	//	static_cast<int32>(DefaultPlayerRole),
	//	DefaultPairId
	//);
}

void AFirstPersonPlayerController::ClientArenaLoad_Implementation(
	FVector InSpawnLocation, uint32 ReconnectRequestId)
{
	(void)ReconnectRequestId;
	ApplyServerArenaSpawnLocation(InSpawnLocation);
	PendingGameplayGeneration = 0;
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!ArenaSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaLevels] ClientArenaLoad has no subsystem"));
		return;
	}
	bHasPendingArenaRequest = true;
	ArenaSubsystem->OnArenaShown.RemoveAll(this);
	ArenaSubsystem->OnArenaShown.AddUObject(this, &AFirstPersonPlayerController::HandleArenaShown);
	ArenaSubsystem->EnsureArenaLoaded();
	if (ArenaSubsystem->IsArenaContentReady())
	{
		HandleArenaShown();
	}
}

/*
void AFirstPersonPlayerController::Server_RequestArenaReload_Implementation()
{
	if (AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GameMode->DebugReloadArena(this);
	}
}
*/

void AFirstPersonPlayerController::ApplyServerArenaSpawnLocation(const FVector& InSpawnLocation)
{
	PendingArenaSpawnLocation = InSpawnLocation;
	ClientArenaReadyStableFrames = 0;
	UE_LOG(LogTemp, Display, TEXT("[ArenaLevels] Spawn target %s"), *InSpawnLocation.ToString());
}

void AFirstPersonPlayerController::ClientArenaGameplayReload_Implementation(
	uint32 GameplayGeneration, FVector InSpawnLocation)
{
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!ArenaSubsystem || !UOutlierArenaSubsystem::IsGameplayGenerationNewer(
		GameplayGeneration, PendingGameplayGeneration))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReload][Client] RejectReloadRPC PC=%s Gen=%u PendingGen=%u Subsystem=%d"),
			*GetNameSafe(this), GameplayGeneration, PendingGameplayGeneration,
			ArenaSubsystem ? 1 : 0);
		return;
	}

	ClientGameplayReloadStartedAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReload][Client] ReloadRPC PC=%s Gen=%u Spawn=%s LevelsReady=%d"),
		*GetNameSafe(this), GameplayGeneration, *InSpawnLocation.ToString(),
		ArenaSubsystem->IsGameplayLevelsReady() ? 1 : 0);
	ApplyServerArenaSpawnLocation(InSpawnLocation);
	bReleaseDeathTransitionOnPossess = true;
	bHasPendingArenaRequest = true;
	PendingGameplayGeneration = GameplayGeneration;
	ArenaSubsystem->OnArenaGameplayUnloaded.RemoveAll(this);
	ArenaSubsystem->OnArenaGameplayReady.RemoveAll(this);
	ArenaSubsystem->OnArenaGameplayUnloaded.AddUObject(
		this, &AFirstPersonPlayerController::HandleArenaGameplayUnloaded);
	ArenaSubsystem->OnArenaGameplayReady.AddUObject(
		this, &AFirstPersonPlayerController::HandleArenaGameplayReady);
	if (!ArenaSubsystem->BeginClientGameplayReload(GameplayGeneration))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaReload][Client] BeginReloadFailed PC=%s Gen=%u Phase=%s"),
			*GetNameSafe(this), GameplayGeneration,
			*UEnum::GetValueAsString(ArenaSubsystem->GetGameplayReloadPhase()));
	}
}

void AFirstPersonPlayerController::HandleArenaGameplayUnloaded(uint32 GameplayGeneration)
{
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReload][Client] IgnoreUnloadedEvent Gen=%u PendingGen=%u"),
			GameplayGeneration, PendingGameplayGeneration);
		return;
	}
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr)
	{
		ArenaSubsystem->OnArenaGameplayUnloaded.RemoveAll(this);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReload][Client] SendUnloadACK PC=%s Gen=%u LocalUnloadAndGCMs=%.1f"),
		*GetNameSafe(this), GameplayGeneration,
		ClientGameplayReloadStartedAt > 0.0
			? (FPlatformTime::Seconds() - ClientGameplayReloadStartedAt) * 1000.0 : -1.0);
	ServerNotifyArenaGameplayUnloaded(GameplayGeneration);
}

void AFirstPersonPlayerController::ClientActivateArenaGameplayLevels_Implementation(
	uint32 GameplayGeneration)
{
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReload][Client] RejectActivateRPC Gen=%u PendingGen=%u"),
			GameplayGeneration, PendingGameplayGeneration);
		return;
	}
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReload][Client] ActivateRPC PC=%s Gen=%u LocalUnloaded=%d ElapsedMs=%.1f"),
			*GetNameSafe(this), GameplayGeneration,
			ArenaSubsystem->IsGameplayReloadUnloaded(GameplayGeneration) ? 1 : 0,
			ClientGameplayReloadStartedAt > 0.0
				? (FPlatformTime::Seconds() - ClientGameplayReloadStartedAt) * 1000.0 : -1.0);
		ArenaSubsystem->AllowGameplayLevelLoad(GameplayGeneration);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaReload][Client] ActivateRPC failed: arena subsystem missing Gen=%u"),
			GameplayGeneration);
	}
}

void AFirstPersonPlayerController::HandleArenaShown()
{
	if (!bHasPendingArenaRequest)
	{
		return;
	}
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr)
	{
		ArenaSubsystem->OnArenaShown.RemoveAll(this);
		ArenaSubsystem->OnArenaGameplayReady.RemoveAll(this);
	}
	bWaitingForArenaStart = true;
	TryNotifyArenaStartReady();
}

void AFirstPersonPlayerController::HandleArenaGameplayReady(uint32 GameplayGeneration)
{
	if (GameplayGeneration == PendingGameplayGeneration)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReload][Client] LevelsReady PC=%s Gen=%u ElapsedMs=%.1f"),
			*GetNameSafe(this), GameplayGeneration,
			ClientGameplayReloadStartedAt > 0.0
				? (FPlatformTime::Seconds() - ClientGameplayReloadStartedAt) * 1000.0 : -1.0);
		HandleArenaShown();
	}
}

/*
void AFirstPersonPlayerController::ArenaDumpClientGameplayReload()
{
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReload][Client] Dump PC=%s Gen=%u WaitingStart=%d PendingRequest=%d ElapsedMs=%.1f"),
		*GetNameSafe(this), PendingGameplayGeneration,
		bWaitingForArenaStart ? 1 : 0, bHasPendingArenaRequest ? 1 : 0,
		ClientGameplayReloadStartedAt > 0.0
			? (FPlatformTime::Seconds() - ClientGameplayReloadStartedAt) * 1000.0 : 0.0);
	if (const UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr)
	{
		ArenaSubsystem->DumpGameplayReloadState();
	}
}

void AFirstPersonPlayerController::ArenaDumpClientGameplayActors()
{
	if (const UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr)
	{
		ArenaSubsystem->DumpGameplayActorState();
	}
}
*/

void AFirstPersonPlayerController::ServerNotifyArenaReady_Implementation(uint32 GameplayGeneration)
{
	if (AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GameMode->OnClientArenaReady(this, GameplayGeneration);
	}
}

void AFirstPersonPlayerController::ServerNotifyArenaGameplayUnloaded_Implementation(
	uint32 GameplayGeneration)
{
	if (AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GameMode->OnClientArenaGameplayUnloaded(this, GameplayGeneration);
	}
}

void AFirstPersonPlayerController::ClientRetryArenaGameplayReload_Implementation(
	uint32 GameplayGeneration)
{
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!ArenaSubsystem)
	{
		return;
	}
	if (ArenaSubsystem->IsGameplayReloadStalled(GameplayGeneration))
	{
		ArenaSubsystem->RetryStalledGameplayReload(GameplayGeneration);
	}
	if (ArenaSubsystem->IsGameplayReloadUnloaded(GameplayGeneration))
	{
		ServerNotifyArenaGameplayUnloaded(GameplayGeneration);
	}
}

void AFirstPersonPlayerController::ClientPrepareForArenaStart_Implementation(
	FVector InSpawnLocation)
{
	ApplyServerArenaSpawnLocation(InSpawnLocation);
	PendingGameplayGeneration = 0;
	bHasPendingArenaRequest = true;
	bWaitingForArenaStart = true;
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr)
	{
		ArenaSubsystem->EnsureArenaLoaded();
	}
	TryNotifyArenaStartReady();
}

void AFirstPersonPlayerController::TryNotifyArenaStartReady()
{
	if (!bWaitingForArenaStart || !IsLocalController() || !bHasPendingArenaRequest
		|| ClientArenaContentTickerHandle.IsValid())
	{
		return;
	}
	ClientArenaReadyStableFrames = 0;
	ClientArenaContentTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &AFirstPersonPlayerController::TickClientArenaContentReady));
}

bool AFirstPersonPlayerController::TickClientArenaContentReady(float DeltaTime)
{
	constexpr int32 RequiredStableFrames = 3;
	constexpr double WatchdogTimeoutSeconds = 10.0;
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	const bool bReady = bWaitingForArenaStart && IsLocalController()
		&& bHasPendingArenaRequest && ArenaSubsystem
		&& ArenaSubsystem->IsArenaContentReady();
	if (!bReady)
	{
		ClientArenaReadyStableFrames = 0;
		ClientArenaWaitSeconds += DeltaTime;
		if (ClientArenaWaitSeconds >= WatchdogTimeoutSeconds)
		{
			ClientArenaWaitSeconds = 0.0;
			UE_LOG(LogTemp, Error,
				TEXT("[ArenaLevels] Client content wait stalled Generation=%u ContentReady=%d Phase=%d"),
				PendingGameplayGeneration,
				ArenaSubsystem && ArenaSubsystem->IsArenaContentReady() ? 1 : 0,
				ArenaSubsystem ? static_cast<int32>(ArenaSubsystem->GetGameplayReloadPhase()) : -1);
			if (PendingGameplayGeneration == 0 && ArenaSubsystem)
			{
				ArenaSubsystem->EnsureArenaLoaded();
			}
		}
		return true;
	}
	if (++ClientArenaReadyStableFrames < RequiredStableFrames)
	{
		return true;
	}
	bWaitingForArenaStart = false;
	bHasPendingArenaRequest = false;
	ClientArenaReadyStableFrames = 0;
	ClientArenaWaitSeconds = 0.0;
	ClientArenaContentTickerHandle.Reset();
	if (PendingGameplayGeneration != 0)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReload][Client] SendReadyACK PC=%s Gen=%u TotalMs=%.1f StableFrames=%d"),
			*GetNameSafe(this), PendingGameplayGeneration,
			ClientGameplayReloadStartedAt > 0.0
				? (FPlatformTime::Seconds() - ClientGameplayReloadStartedAt) * 1000.0 : -1.0,
			RequiredStableFrames);
	}
	ServerNotifyArenaReady(PendingGameplayGeneration);
	return false;
}

void AFirstPersonPlayerController::ClearClientArenaContentWait()
{
	if (ClientArenaContentTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ClientArenaContentTickerHandle);
		ClientArenaContentTickerHandle.Reset();
	}
	ClientArenaReadyStableFrames = 0;
	ClientArenaWaitSeconds = 0.0;
	bWaitingForArenaStart = false;
	bHasPendingArenaRequest = false;
}
void AFirstPersonPlayerController::ServerOpenInGameSetting_Implementation()
{
	if (!InGameSettingWidgetClass)
	{
		return;
	}

	AShooterCharacter* ShooterCharacter = nullptr;
	APartnerCharacter* PartnerCharacter = nullptr;
	ResolvePairCharactersForController(this, ShooterCharacter, PartnerCharacter);

	AFirstPersonPlayerController* ShooterController = ShooterCharacter
		? Cast<AFirstPersonPlayerController>(ShooterCharacter->GetController())
		: nullptr;
	AFirstPersonPlayerController* PartnerController = PartnerCharacter
		? Cast<AFirstPersonPlayerController>(PartnerCharacter->GetController())
		: nullptr;

	const AOutlierPlayerState* RequestingPlayerState = GetPlayerState<AOutlierPlayerState>();
	AActor* PausingCharacter = GetPawn();
	if (RequestingPlayerState && RequestingPlayerState->IsShooterPlayer() && ShooterCharacter)
	{
		PausingCharacter = ShooterCharacter;
	}
	else if (RequestingPlayerState && RequestingPlayerState->IsPartnerPlayer() && PartnerCharacter)
	{
		PausingCharacter = PartnerCharacter;
	}

	const UInGameSettingWidget* InGameSettingDefault =
		InGameSettingWidgetClass->GetDefaultObject<UInGameSettingWidget>();
	const TSubclassOf<UInGamePauseWidget> InGamePauseWidgetClass = InGameSettingDefault
		? InGameSettingDefault->GetInGamePauseWidgetClass()
		: nullptr;

	const AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr;
	ConfigureCheckpointRestartFromServer(
		OutlierGameMode && OutlierGameMode->CanControllerRequestCheckpointRestart(this));

	FUILayerPushRequest PushRequest;
	PushRequest.WidgetClass = InGameSettingWidgetClass;
	PushRequest.LayerTag = UILayerTags::GameMenu();
	PushRequest.InputModeTag = FirstPersonInputModeTags::UI();
	PushRequest.RequestOwner = PausingCharacter;
	PushRequest.ContextActors = { ShooterCharacter, PartnerCharacter, PausingCharacter };
	PushRequest.FocusTarget = EUILayerFocusTarget::Widget;
	PushRequest.bShowCursor = true;

	PushUILayerToController(this, PushRequest);

	AFirstPersonPlayerController* WaitingController = nullptr;
	if (RequestingPlayerState && RequestingPlayerState->IsShooterPlayer())
	{
		WaitingController = PartnerController;
	}
	else if (RequestingPlayerState && RequestingPlayerState->IsPartnerPlayer())
	{
		WaitingController = ShooterController;
	}
	else if (ShooterController && ShooterController != this)
	{
		WaitingController = ShooterController;
	}
	else if (PartnerController && PartnerController != this)
	{
		WaitingController = PartnerController;
	}

	if (WaitingController && WaitingController != this && InGamePauseWidgetClass)
	{
		FUILayerPushRequest PausePushRequest;
		PausePushRequest.WidgetClass = InGamePauseWidgetClass;
		PausePushRequest.LayerTag = UILayerTags::GameMenu();
		PausePushRequest.InputModeTag = FirstPersonInputModeTags::UI();
		PausePushRequest.RequestOwner = PausingCharacter;
		PausePushRequest.ContextActors = { ShooterCharacter, PartnerCharacter, PausingCharacter };
		PausePushRequest.FocusTarget = EUILayerFocusTarget::None;
		PausePushRequest.bShowCursor = false;
		PausePushRequest.bReceivesInput = true;

		PushUILayerToController(WaitingController, PausePushRequest);
	}

	UGameplayStatics::SetGamePaused(this, true);
}

void AFirstPersonPlayerController::ServerCloseInGameSetting_Implementation()
{
	AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr;
	if (OutlierGameMode && OutlierGameMode->HandleCheckpointRestartEscape(this))
	{
		return;
	}

	UGameplayStatics::SetGamePaused(this, false);

	AShooterCharacter* ShooterCharacter = nullptr;
	APartnerCharacter* PartnerCharacter = nullptr;
	ResolvePairCharactersForController(this, ShooterCharacter, PartnerCharacter);

	const AOutlierPlayerState* RequestingPlayerState = GetPlayerState<AOutlierPlayerState>();
	AActor* PausingCharacter = GetPawn();
	if (RequestingPlayerState && RequestingPlayerState->IsShooterPlayer() && ShooterCharacter)
	{
		PausingCharacter = ShooterCharacter;
	}
	else if (RequestingPlayerState && RequestingPlayerState->IsPartnerPlayer() && PartnerCharacter)
	{
		PausingCharacter = PartnerCharacter;
	}

	AFirstPersonPlayerController* ShooterController = ShooterCharacter
		? Cast<AFirstPersonPlayerController>(ShooterCharacter->GetController())
		: nullptr;
	AFirstPersonPlayerController* PartnerController = PartnerCharacter
		? Cast<AFirstPersonPlayerController>(PartnerCharacter->GetController())
		: nullptr;

	if (ShooterController)
	{
		PopInGameSettingLayerFromController(ShooterController, PausingCharacter);
	}

	if (PartnerController && PartnerController != ShooterController)
	{
		PopInGameSettingLayerFromController(PartnerController, PausingCharacter);
	}
}

void AFirstPersonPlayerController::ServerRequestLeaveGame_Implementation()
{
	if (AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr)
	{
		OutlierGameMode->HandleExplicitPlayerLeave(this);
	}
}

void AFirstPersonPlayerController::ServerRequestCheckpointRestart_Implementation()
{
	if (AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr)
	{
		OutlierGameMode->RequestCheckpointRestart(this);
	}
}

void AFirstPersonPlayerController::ServerRespondCheckpointRestart_Implementation(
	bool bApprove)
{
	if (AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr)
	{
		OutlierGameMode->RespondCheckpointRestart(this, bApprove);
	}
}

void AFirstPersonPlayerController::ServerCancelCheckpointRestart_Implementation()
{
	if (AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr)
	{
		OutlierGameMode->CancelCheckpointRestart(this);
	}
}

void AFirstPersonPlayerController::RegisterCurrentPawnWithPlayerState()
{
	if (!HasAuthority())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] RegisterCurrentPawnWithPlayerState failed: PlayerState null PC=%s"), *GetNameSafe(this));
		return;
	}

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetPawn()))
	{
		// [입력 바인딩 디버그 — 2026-09-14 비활성화]
		//UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] Register Shooter Pawn: %s"), *GetNameSafe(ShooterCharacter));
		OutlierPlayerState->SetPlayerRole(EOutlierPlayerRole::Shooter);
		OutlierPlayerState->SetShooterCharacter(ShooterCharacter);
	}
	else if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetPawn()))
	{
		// [입력 바인딩 디버그 — 2026-09-14 비활성화]
		//UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] Register Partner Pawn: %s"), *GetNameSafe(PartnerCharacter));
		OutlierPlayerState->SetPlayerRole(EOutlierPlayerRole::Partner);
		OutlierPlayerState->SetPartnerCharacter(PartnerCharacter);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] Register pawn skipped: Pawn=%s"), *GetNameSafe(GetPawn()));
	}

	if (AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr)
	{
		GameMode->RefreshPairLinks(OutlierPlayerState);
	}
}

void AFirstPersonPlayerController::ControlMainWidget(bool InFlag) const
{
	if (!IsLocalController())
	{
		return;
	}

	if (ShooterUIInstance)
	{
		ShooterUIInstance->ModulesControl(InFlag);
	}
}

void AFirstPersonPlayerController::RefreshPostProcessState()
{
	
}
