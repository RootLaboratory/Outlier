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
#include "UI/InGamePauseWidget.h"
#include "UI/InGameSettingWidget.h"
#include "UI/PreSetLoadWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"
#include "Components/SceneComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "Misc/StringOutputDevice.h"
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "Engine/LevelStreaming.h"

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
	if (!bWaitingForArenaStart)
	{
		ReleaseClientArenaStreamingSource();
	}
	ReportLoadedLevelsVisibilityToServer();
	TryNotifyArenaStartReady();
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
		ArenaSubsystem->OnArenaGameplayGCReady.RemoveAll(this);
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
	if (ClientArenaContentTickerHandle.IsValid()
		&& PendingReconnectRequestId != ReconnectRequestId)
	{
		// 이전 좌표의 스트리밍 안정 프레임을 새 재접속 요청의 ACK로 재사용하지 않는다.
		ClearClientArenaContentWait();
	}
	// 아직 Possess 전이라 GetPawn()이 없는 구간에서 스트리밍 소스를 어디에 둬야 할지,
	// 서버가 이미 계산해둔 실제 스폰 위치를 그대로 받아 저장해둔다 (레벨 액터 추측 금지).
	PendingArenaSpawnLocation = InSpawnLocation;
	bHasPendingArenaSpawnLocation = true;
	PendingReconnectRequestId = ReconnectRequestId;

	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;

	if (!ArenaSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[Arena] ClientArenaLoad: ArenaSubsystem is null"));
		return;
	}

	ArenaSubsystem->EnsureArenaLoaded();

	if (ArenaSubsystem->IsArenaReady())
	{
		bHasPendingArenaRequest = true;
		bWaitingForArenaStart = true;
		TryNotifyArenaStartReady();
		return;
	}

	bHasPendingArenaRequest = true;
	ArenaSubsystem->OnArenaShown.AddUObject(this, &AFirstPersonPlayerController::HandleArenaShown);
}

void AFirstPersonPlayerController::Server_RequestArenaReload_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] Server_RequestArenaReload PC=%s Auth=%d"),
		*GetNameSafe(this), HasAuthority());

	AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr;
	if (!GM)
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugReload] Bail: AuthGameMode null (not on server?)"));
		return;
	}

	GM->DebugReloadArena(this);
}

void AFirstPersonPlayerController::ApplyServerArenaSpawnLocation(const FVector& InSpawnLocation)
{
	PendingArenaSpawnLocation = InSpawnLocation;
	bHasPendingArenaSpawnLocation = true;
	bPreferServerArenaSpawnLocation = true;

	// 직전 대기에서 만든 임시 소스가 아직 살아 있으면 TryNotifyArenaStartReady가 생성 단계를
	// 건너뛰고 옛 위치를 그대로 재사용한다. 목적지가 바뀌었으므로 반드시 버리고 새로 세운다.
	ReleaseClientArenaStreamingSource();
	ClientArenaReadyStableFrames = 0;

	UE_LOG(LogTemp, Display,
		TEXT("[Arena] Reload target received SpawnLocation=%s"),
		*InSpawnLocation.ToString());
}

void AFirstPersonPlayerController::ClientArenaReload_Implementation(FVector InSpawnLocation)
{
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (!ArenaSubsystem)
	{
		return;
	}

	ApplyServerArenaSpawnLocation(InSpawnLocation);
	PendingReconnectRequestId = 0;

	// 강제 리로드라 항상 새로 스트리밍된다. 바인딩을 먼저 걸고(레이스 방지) 리로드.
	bHasPendingArenaRequest = true;
	ArenaSubsystem->OnArenaShown.AddUObject(this, &AFirstPersonPlayerController::HandleArenaShown);
	ArenaSubsystem->EnsureArenaLoaded(/*bForceReload=*/true);
}

void AFirstPersonPlayerController::ClientArenaGameplayReload_Implementation(uint32 GameplayGeneration, FVector InSpawnLocation)
{
	PendingReconnectRequestId = 0;
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (!ArenaSubsystem)
	{
		return;
	}

	if (!UOutlierArenaSubsystem::IsGameplayGenerationNewer(
		GameplayGeneration, PendingGameplayGeneration))
	{
		return;
	}
	ApplyServerArenaSpawnLocation(InSpawnLocation);

	// 아레나 LevelInstance는 건드리지 않는다. 서버에서 복제되는 Gameplay Data Layer가
	// 클라이언트도 이전 Actor의 GC를 확인해 ACK한 뒤 Activated/Streaming 준비 완료를 따로 기다린다.
	bHasPendingArenaRequest = true;
	PendingGameplayGeneration = GameplayGeneration;
	ArenaSubsystem->OnArenaGameplayGCReady.AddUObject(
		this, &AFirstPersonPlayerController::HandleArenaGameplayGCReady);
	ArenaSubsystem->OnArenaGameplayReady.AddUObject(this, &AFirstPersonPlayerController::HandleArenaGameplayReady);
	ArenaSubsystem->WaitForGameplayDataReady(GameplayGeneration);
}

void AFirstPersonPlayerController::HandleArenaGameplayGCReady(uint32 GameplayGeneration)
{
	// 이 ACK는 이전 수명 정리 완료만 뜻한다. 새 Pawn의 Possess 준비는 이후 ServerNotifyArenaReady로 알린다.
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}

	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr)
	{
		ArenaSubsystem->OnArenaGameplayGCReady.RemoveAll(this);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[Arena][DataLayer] Client GC complete Generation=%u; notifying server"), GameplayGeneration);
	ServerNotifyArenaGameplayGCReady(GameplayGeneration);
}

void AFirstPersonPlayerController::HandleArenaShown()
{
	if (!bHasPendingArenaRequest)
	{
		return;
	}

	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr)
	{
		ArenaSubsystem->OnArenaShown.RemoveAll(this);
		ArenaSubsystem->OnArenaGameplayReady.RemoveAll(this);
	}

	bWaitingForArenaStart = true;
	TryNotifyArenaStartReady();
}

void AFirstPersonPlayerController::HandleArenaGameplayReady(uint32 GameplayGeneration)
{
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}
	HandleArenaShown();
}

void AFirstPersonPlayerController::ServerNotifyArenaReady_Implementation(uint32 ReconnectRequestId)
{
	AOutlierGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr;
	if (GameMode)
	{
		GameMode->OnClientArenaReady(this, ReconnectRequestId);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Arena] ServerNotifyArenaReady: GameMode is null"));
	}
}

void AFirstPersonPlayerController::ServerNotifyArenaGameplayGCReady_Implementation(uint32 GameplayGeneration)
{
	if (AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr)
	{
		GameMode->OnClientArenaGameplayGCReady(this, GameplayGeneration);
	}
}

void AFirstPersonPlayerController::ClientRetryArenaGameplayReload_Implementation(uint32 GameplayGeneration)
{
	// 수동 재시도는 같은 Generation의 현재 안전 조건만 다시 확인한다. Timeout을 이유로
	// GC 확인이나 Data Layer 활성 단계를 강제로 넘기면 이전 세대 Actor가 새 판에 남을 수 있다.
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}

	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (!ArenaSubsystem)
	{
		return;
	}

	if (ArenaSubsystem->IsGameplayReloadStalled(GameplayGeneration))
	{
		ArenaSubsystem->RetryStalledGameplayReload(GameplayGeneration);
	}
	if (ArenaSubsystem->IsGameplayReloadGCVerified(GameplayGeneration))
	{
		ServerNotifyArenaGameplayGCReady(GameplayGeneration);
	}
}

void AFirstPersonPlayerController::ClientPrepareForArenaStart_Implementation(FVector InSpawnLocation)
{
	// ClientArenaLoad와 같은 이유로 서버가 계산해둔 스폰 위치를 먼저 저장한다.
	// 이게 없으면 ResolveClientArenaStreamingLocation이 Pawn도 위치도 못 찾고 실패하고,
	// Pawn은 셀이 스트리밍돼야 도착하므로 영원히 준비 보고를 못 한다.
	PendingArenaSpawnLocation = InSpawnLocation;
	bHasPendingArenaSpawnLocation = true;

	bHasPendingArenaRequest = true;
	PendingReconnectRequestId = 0;
	bWaitingForArenaStart = true;
	TryNotifyArenaStartReady();
}

void AFirstPersonPlayerController::TryNotifyArenaStartReady()
{
	constexpr float ClientArenaStreamingRadius = 12800.0f;

	if (!bWaitingForArenaStart || !IsLocalController() || !bHasPendingArenaRequest)
	{
		return;
	}

	if (!ClientArenaStreamingSource)
	{
		FVector StreamingLocation = FVector::ZeroVector;
		if (!ResolveClientArenaStreamingLocation(StreamingLocation))
		{
			return;
		}

		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}

		ClientArenaStreamingSourceActor = World->SpawnActor<AActor>(
			AActor::StaticClass(), StreamingLocation, FRotator::ZeroRotator);
		if (!ClientArenaStreamingSourceActor)
		{
			return;
		}
		ClientArenaStreamingSourceActor->SetFlags(RF_Transient);

		// AActor::StaticClass()로 스폰한 순수 AActor는 기본 RootComponent가 없다.
		// RootComponent가 없으면 GetActorLocation()이 항상 FVector::ZeroVector를 반환해서
		// (SpawnActor에 넘긴 위치는 저장될 곳이 없어 버려짐) 이 스트리밍 소스가 실제로는
		// 항상 월드 원점을 스트리밍하게 된다. 명시적으로 RootComponent를 만들어 위치를 박아둔다.
		if (USceneComponent* RootComp = NewObject<USceneComponent>(
			ClientArenaStreamingSourceActor,
			USceneComponent::StaticClass(),
			TEXT("ClientArenaStreamingSourceRoot")))
		{
			ClientArenaStreamingSourceActor->SetRootComponent(RootComp);
			RootComp->RegisterComponent();
			RootComp->SetWorldLocation(StreamingLocation);
		}

		ClientArenaStreamingSource = NewObject<UWorldPartitionStreamingSourceComponent>(
			ClientArenaStreamingSourceActor,
			UWorldPartitionStreamingSourceComponent::StaticClass(),
			TEXT("ClientArenaStreamingSource"));
		if (!ClientArenaStreamingSource)
		{
			ClientArenaStreamingSourceActor->Destroy();
			ClientArenaStreamingSourceActor = nullptr;
			return;
		}

		FStreamingSourceShape StreamingShape;
		StreamingShape.bUseGridLoadingRange = false;
		StreamingShape.Radius = ClientArenaStreamingRadius;
		ClientArenaStreamingSource->Shapes.Add(StreamingShape);
		ClientArenaStreamingSource->RegisterComponent();

		// 워치독이 복구할 때 되돌릴 자리. 소스를 새로 세울 때마다 대기 시간도 같이 리셋한다.
		ClientArenaSourceHomeLocation = StreamingLocation;
		ClientArenaWaitSeconds = 0.0;
		ClientArenaRecoveryHoldSeconds = 0.0;
		ClientArenaRecoveryCount = 0;
		bClientArenaSourceDisplaced = false;

		UE_LOG(LogTemp, Display,
			TEXT("[Arena] Client content wait started Location=%s Radius=%.0f"),
			*StreamingLocation.ToString(),
			ClientArenaStreamingRadius);
	}

	if (!ClientArenaContentTickerHandle.IsValid())
	{
		ClientArenaReadyStableFrames = 0;
		ClientArenaContentTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &AFirstPersonPlayerController::TickClientArenaContentReady));
	}
}

bool AFirstPersonPlayerController::TickClientArenaContentReady(float DeltaTime)
{
	constexpr int32 RequiredStableFrames = 3;
	// 진전이 없다고 판단하기까지의 시간. 콜드 로딩(패키징 클라, 저사양)이 이보다 오래 걸리면
	// 멀쩡한 로딩을 끊게 되므로 넉넉하게 잡는다.
	constexpr double WatchdogTimeoutSeconds = 10.0;
	// 소스를 치워둔 채 유지할 시간. 셀이 MakingVisible -> LoadedNotVisible 로 실제로 내려가야
	// 다음 재진입에서 새 가시화 요청이 발행된다. 한 틱으로는 부족할 수 있어 여유를 둔다.
	constexpr double RecoveryHoldSeconds = 0.25;
	constexpr int32 MaxRecoveryAttempts = 3;
	// 아레나 간격(기본 1km)보다 훨씬 멀리 — 어떤 아레나의 로딩 범위에도 안 걸리는 빈 좌표.
	constexpr double RecoveryDisplacement = 10000000.0; // 100 km

	// 복구 중에는 셀을 일부러 내려놓은 상태다. 이때 IsStreamingCompleted()는 "스트리밍할 게
	// 없어서" true가 될 수 있으므로 준비 판정을 절대 돌리지 않는다.
	if (bClientArenaSourceDisplaced)
	{
		ClientArenaRecoveryHoldSeconds += DeltaTime;
		if (ClientArenaRecoveryHoldSeconds < RecoveryHoldSeconds)
		{
			return true;
		}

		bClientArenaSourceDisplaced = false;
		ClientArenaRecoveryHoldSeconds = 0.0;
		ClientArenaWaitSeconds = 0.0;
		ClientArenaReadyStableFrames = 0;

		if (ClientArenaStreamingSourceActor)
		{
			ClientArenaStreamingSourceActor->SetActorLocation(ClientArenaSourceHomeLocation);
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[Arena][Watchdog] Source restored Location=%s Attempt=%d/%d"),
			*ClientArenaSourceHomeLocation.ToString(),
			ClientArenaRecoveryCount,
			MaxRecoveryAttempts);
		return true;
	}

	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;

	const bool bStreamingCompleted = ClientArenaStreamingSource && ClientArenaStreamingSource->IsStreamingCompleted();
	const bool bContentReady = ArenaSubsystem && ArenaSubsystem->IsArenaContentReady();
	const bool bReady = bWaitingForArenaStart
		&& IsLocalController()
		&& bHasPendingArenaRequest
		&& ClientArenaStreamingSource
		&& bStreamingCompleted
		&& ArenaSubsystem
		&& bContentReady;

	if (!bReady)
	{
		ClientArenaReadyStableFrames = 0;
		ClientArenaWaitSeconds += DeltaTime;

		if (ClientArenaWaitSeconds >= WatchdogTimeoutSeconds)
		{
			ClientArenaWaitSeconds = 0.0;
			++ClientArenaRecoveryCount;

			// 어느 조건이 안 차는지를 반드시 같이 남긴다. Streaming=0 이면 셀 가시화가 막힌 것이고,
			// Content=0 이면 셀은 왔는데 LevelInstance/Data Layer 쪽이 안 끝난 것이다.
			UE_LOG(LogTemp, Warning,
				TEXT("[Arena][Watchdog] Stalled Streaming=%d Content=%d HasSource=%d Home=%s Attempt=%d/%d"),
				bStreamingCompleted ? 1 : 0,
				bContentReady ? 1 : 0,
				ClientArenaStreamingSource ? 1 : 0,
				*ClientArenaSourceHomeLocation.ToString(),
				ClientArenaRecoveryCount,
				MaxRecoveryAttempts);

			if (ClientArenaRecoveryCount > MaxRecoveryAttempts)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Arena][Watchdog] Recovery exhausted; the arena will not finish loading on this client"));
				return true;
			}

			// 엔진에는 가시화 요청 재전송이 없다. 셀을 "필요 없음"으로 떨어뜨렸다가 다시 필요하게
			// 만드는 왕복만이 InvalidateClientPendingRequest + 새 요청을 유발한다(LevelStreaming.cpp:1163).
			if (ClientArenaStreamingSourceActor)
			{
				const FVector DisplacedLocation =
					ClientArenaSourceHomeLocation + FVector(RecoveryDisplacement, 0.0, 0.0);
				ClientArenaStreamingSourceActor->SetActorLocation(DisplacedLocation);
				bClientArenaSourceDisplaced = true;
				ClientArenaRecoveryHoldSeconds = 0.0;

				UE_LOG(LogTemp, Warning,
					TEXT("[Arena][Watchdog] Source displaced to %s for %.2fs to force a new visibility request"),
					*DisplacedLocation.ToString(),
					RecoveryHoldSeconds);
			}
		}
		return true;
	}

	if (++ClientArenaReadyStableFrames < RequiredStableFrames)
	{
		return true;
	}

	const int32 UsedRecoveryCount = ClientArenaRecoveryCount;
	bWaitingForArenaStart = false;
	bHasPendingArenaRequest = false;
	ClientArenaReadyStableFrames = 0;
	ClientArenaWaitSeconds = 0.0;
	ClientArenaRecoveryHoldSeconds = 0.0;
	ClientArenaRecoveryCount = 0;
	bPreferServerArenaSpawnLocation = false;
	ClientArenaContentTickerHandle.Reset();

	// Listen은 이 Ready를 받은 서버가 그제야 Possess한다. 그 경우 Pawn 기반 기본
	// streaming source가 생길 때까지 임시 source를 유지해 로딩 공백을 만들지 않는다.
	if (GetPawn())
	{
		ReleaseClientArenaStreamingSource();
	}

	// RecoveryCount>0 으로 끝났다면 워치독이 실제로 살려낸 것이다 — 그 왕복이 동작한다는 증거라
	// 반드시 남긴다(0이면 평소대로 통과한 것).
	UE_LOG(LogTemp, Display,
		TEXT("[Arena] Client content ready StableFrames=%d RecoveryCount=%d"),
		RequiredStableFrames,
		UsedRecoveryCount);
	ServerNotifyArenaReady(PendingReconnectRequestId);
	return false;
}

void AFirstPersonPlayerController::ClearClientArenaContentWait()
{
	if (ClientArenaContentTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ClientArenaContentTickerHandle);
		ClientArenaContentTickerHandle.Reset();
	}

	ReleaseClientArenaStreamingSource();
	ClientArenaReadyStableFrames = 0;
	bWaitingForArenaStart = false;
	bHasPendingArenaRequest = false;
	bHasPendingArenaSpawnLocation = false;
	bPreferServerArenaSpawnLocation = false;
	ClientArenaWaitSeconds = 0.0;
	ClientArenaRecoveryHoldSeconds = 0.0;
	ClientArenaRecoveryCount = 0;
	bClientArenaSourceDisplaced = false;
}

void AFirstPersonPlayerController::ReleaseClientArenaStreamingSource()
{
	if (ClientArenaStreamingSourceActor)
	{
		ClientArenaStreamingSourceActor->Destroy();
	}
	ClientArenaStreamingSource = nullptr;
	ClientArenaStreamingSourceActor = nullptr;
}

bool AFirstPersonPlayerController::ResolveClientArenaStreamingLocation(FVector& OutLocation) const
{
	// 리로드 구간에서는 Pawn을 믿으면 안 된다. 서버는 이미 옛 폰을 Destroy하고 새 스테이지에
	// 새 폰을 스폰했는데, 그 사실이 클라에 도착하기 전이면 GetPawn()은 "죽은 자리의 옛 폰"을
	// 돌려준다. 그 좌표는 이미 스트리밍이 끝난 곳이라 IsStreamingCompleted()가 즉시 참이 되고,
	// 클라는 새 목적지를 한 번도 요청하지 않은 채 준비 완료를 보고해버린다.
	if (bPreferServerArenaSpawnLocation && bHasPendingArenaSpawnLocation)
	{
		OutLocation = PendingArenaSpawnLocation;
		return true;
	}

	if (const APawn* ControlledPawn = GetPawn())
	{
		OutLocation = ControlledPawn->GetActorLocation();
		return true;
	}

	// 아직 Possess 전이라 실제 Pawn 위치를 모른다. 레벨 액터(PlayerStart 등)를 뒤져서
	// 추측하지 않고, 서버가 스폰 시점에 계산해서 넘겨준 실제 스폰 위치를 그대로 쓴다
	// (레벨에 해당 액터가 없거나 여러 개라 잘못 고르는 문제 자체를 없앤다).
	if (bHasPendingArenaSpawnLocation)
	{
		OutLocation = PendingArenaSpawnLocation;
		return true;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Arena] ResolveClientArenaStreamingLocation failed: no Pawn and no server-provided spawn location"));
	return false;
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
