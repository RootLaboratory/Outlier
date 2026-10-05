// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerController.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Audio/OutlierUIAudioSettings.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "FirstPersonCharacter.h"
#include "FirstPersonPlayerCameraManager.h"
#include "Input/ControllerInputConfig.h"
#include "Kismet/GameplayStatics.h"
#include "LocalPlayerUISubSystem.h"
#include "OutlierGameInstance.h"
#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Outlier.h"
#include "MainUIBase.h"
#include "UI/UILayerGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/WeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Network/OutlierArenaSubsystem.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "UI/InGamePauseWidget.h"
#include "UI/InGameSettingWidget.h"
#include "UI/GameOverWidget.h"
#include "UI/GameOverPendingWidget.h"
#include "UI/UILayerKeyHintWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"
#include "Misc/StringOutputDevice.h"
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "HAL/PlatformTime.h"

namespace
{
	bool ShouldBypassSuitPresentationForTesting(const UWorld* World)
	{
#if WITH_EDITOR
		static TAutoConsoleVariable<int32> CVarSuitPresentationBypass(
			TEXT("Outlier.SuitTransition.BypassPresentation"), 0,
			TEXT("PIE only: skip the suit screen blackout and readiness check, acknowledging each phase immediately. Set 1 to skip the presentation during gameplay testing."));
		return World && World->WorldType == EWorldType::PIE
			&& CVarSuitPresentationBypass.GetValueOnGameThread() != 0;
#else
		return false;
#endif
	}

	// 실제 화면(LocalPlayer)이 있는 Controller에만 있다. 없으면 Suit 연출도 완료 응답도 만들지 않는다.
	ULocalPlayerPostProcessSubsystem* FindSuitPostProcessSubsystem(const APlayerController* Controller)
	{
		const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>() : nullptr;
	}

	// 소유자에게 장착되어 보이고, 두 시점 메시가 모두 소유자 쪽에 붙어 있는지 확인한다.
	bool IsWeaponPresentedOn(const AWeaponBase* Weapon, const AActor* Owner)
	{
		const USkeletalMeshComponent* FirstPersonMesh = IsValid(Weapon) ? Weapon->GetFirstPersonWeaponMesh() : nullptr;
		const USkeletalMeshComponent* ThirdPersonMesh = IsValid(Weapon) ? Weapon->GetThirdPersonWeaponMesh() : nullptr;
		return FirstPersonMesh && ThirdPersonMesh && IsValid(Owner)
			&& Weapon->IsEquipped() && !Weapon->IsHidden()
			&& !FirstPersonMesh->bHiddenInGame && !ThirdPersonMesh->bHiddenInGame
			&& FirstPersonMesh->GetAttachmentRootActor() == Owner
			&& ThirdPersonMesh->GetAttachmentRootActor() == Owner;
	}

	constexpr float SuitPresentationPollInterval = 0.05f;
	// 이만큼(약 2초) 준비가 안 되면 어떤 조건이 막혔는지 한 번 남긴다. 그대로 두면 서버 timeout까지 화면이 덮여 있다.
	constexpr int32 SuitPresentationStallLogPolls = 40;
	// 취소/Pawn 교체로 남은 암전을 걷어내는 시간. 0 ↔ 1 전체 기준이라 남은 만큼만 걸린다.
	constexpr float SuitCleanupRevealDuration = 0.25f;

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
	: LocalSuitTransitionPhase(ESuitTransitionPhase::Idle)
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

	if (!LayerSubsystem->PushWidget(Request).IsValid())
	{
		return;
	}

	UInGameSettingWidget* InGameSetting = Cast<UInGameSettingWidget>(
		LayerSubsystem->FindWidgetByOwnerAndClass(Request.RequestOwner, Request.WidgetClass));
	if (!InGameSetting || !InGameSetting->GetKeyHintWidgetClass())
	{
		return;
	}

	// 메뉴와 힌트는 같은 소유자로 묶어 ClientPopInGameSettingLayer에서 함께 닫는다.
	FUILayerPushRequest HintRequest;
	HintRequest.WidgetClass = InGameSetting->GetKeyHintWidgetClass();
	HintRequest.LayerTag = UILayerTags::Modal();
	HintRequest.InputModeTag = Request.InputModeTag;
	HintRequest.RequestOwner = Request.RequestOwner;
	HintRequest.FocusTarget = EUILayerFocusTarget::None;
	HintRequest.bShowCursor = Request.bShowCursor;
	HintRequest.bReceivesInput = false;
	if (LayerSubsystem->PushWidget(HintRequest).IsValid())
	{
		if (UUserWidget* Hint = LayerSubsystem->FindWidgetByOwnerAndClass(
			Request.RequestOwner, HintRequest.WidgetClass))
		{
			Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void AFirstPersonPlayerController::ShowGameOverPendingFromServer(
	const FGameOverPendingRequest& Request,
	bool bIsRequester)
{
	if (IsLocalController())
	{
		ClientShowGameOverPending_Implementation(Request, bIsRequester);
	}
	else
	{
		ClientShowGameOverPending(Request, bIsRequester);
	}
}

void AFirstPersonPlayerController::CloseGameOverPendingFromServer()
{
	if (IsLocalController())
	{
		ClientCloseGameOverPending_Implementation();
	}
	else
	{
		ClientCloseGameOverPending();
	}
}

bool AFirstPersonPlayerController::HasGameOverPendingWidgetClass() const
{
	const UGameOverWidget* GameOverDefault = GameOverWidgetClass
		? GameOverWidgetClass->GetDefaultObject<UGameOverWidget>()
		: nullptr;
	return GameOverDefault && GameOverDefault->GetGameOverPendingWidgetClass() != nullptr;
}

void AFirstPersonPlayerController::ClientShowGameOverPending_Implementation(
	const FGameOverPendingRequest& Request,
	bool bIsRequester)
{
	if (!GameOverRoundId.IsValid() || Request.RoundId != GameOverRoundId)
	{
		return;
	}
	if (!IsLocalController())
	{
		return;
	}
	if (UGameOverWidget* GameOver = FindGameOverWidget())
	{
		GameOver->ShowPendingRequest(Request, bIsRequester);
		return;
	}
	// 사망 연출이 끝나기 전에 상대 요청이 도착할 수 있으므로 화면 생성까지 보관한다.
	QueuedGameOverPendingRequest = Request;
	bQueuedGameOverPendingRequester = bIsRequester;
	bHasQueuedGameOverPendingRequest = true;
}

void AFirstPersonPlayerController::ClientCloseGameOverPending_Implementation()
{
	bHasQueuedGameOverPendingRequest = false;
	if (UGameOverWidget* GameOver = FindGameOverWidget())
	{
		GameOver->ClosePendingRequest();
	}
}

void AFirstPersonPlayerController::BeginGameOverRespawnTransition_Implementation()
{
	GameOverRoundId.Invalidate();
	bGameOverReadySent = false;
	bHasQueuedGameOverPendingRequest = false;
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
		: nullptr)
	{
		PPSubsystem->OnDeathBlackNoiseStarted.RemoveAll(this);
		PPSubsystem->StartGameOverBlackout();
	}
}

void AFirstPersonPlayerController::ClientRestoreGameOverSelection_Implementation(const FGuid& RoundId)
{
	if (!IsLocalController() || !RoundId.IsValid())
	{
		return;
	}
	// 전환 시작 때 닫힌 UI만 복구한다. 이미 끝난 사망 연출은 다시 시작하지 않는다.
	GameOverRoundId = RoundId;
	bGameOverReadySent = false;
	bHasQueuedGameOverPendingRequest = false;
	PushGameOverWidget();
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

void AFirstPersonPlayerController::RequestLeaveGame(bool bQuitAfterLeave)
{
	if (!IsLocalController() || bExplicitLeaveRequested)
	{
		return;
	}

	// 앱 종료는 서버 확인과 복귀 Travel이 끝난 뒤 GameInstance에서 실행한다.
	bExplicitLeaveRequested = true;
	OnExplicitLeaveStateChanged.Broadcast();
	ServerRequestLeaveGame(bQuitAfterLeave);
}

void AFirstPersonPlayerController::ConfirmExplicitLeaveFromServer(
	bool bAccepted, bool bQuitAfterLeave)
{
	if (IsLocalController())
	{
		ClientConfirmExplicitLeave_Implementation(bAccepted, bQuitAfterLeave);
	}
	else
	{
		ClientConfirmExplicitLeave(bAccepted, bQuitAfterLeave);
	}
}

void AFirstPersonPlayerController::ClientConfirmExplicitLeave_Implementation(
	bool bAccepted, bool bQuitAfterLeave)
{
	bExplicitLeaveRequested = bAccepted;
	if (bAccepted)
	{
		if (UOutlierGameInstance* OutlierGameInstance =
			Cast<UOutlierGameInstance>(GetGameInstance()))
		{
			if (bQuitAfterLeave)
			{
				OutlierGameInstance->RequestQuitAfterExplicitLeave();
			}
			else
			{
				OutlierGameInstance->PrepareForExplicitLeave();
			}
		}
	}
	OnExplicitLeaveStateChanged.Broadcast();
}

void AFirstPersonPlayerController::RequestGameOverPendingChoice(
	const FGameOverPendingRequest& Request)
{
	if (IsLocalController())
	{
		if (!GameOverRoundId.IsValid())
		{
			return;
		}
		FGameOverPendingRequest CurrentRequest = Request;
		CurrentRequest.RoundId = GameOverRoundId;
		CurrentRequest.ProposalId.Invalidate();
		ServerRequestGameOverPendingChoice(CurrentRequest);
	}
}

void AFirstPersonPlayerController::RequestGameOverPendingResponse(
	const FGameOverPendingRequest& Request, bool bApprove)
{
	if (IsLocalController())
	{
		if (Request.RoundId == GameOverRoundId && Request.ProposalId.IsValid())
		{
			ServerRespondGameOverPending(Request.RoundId, Request.ProposalId, bApprove);
		}
	}
}

void AFirstPersonPlayerController::RequestCheckpointRestart()
{
	// 로컬 플래그는 UI 요청을 거르는 용도다. 실제 요청 권한과 리로드 상태는 서버 GameMode가 다시 판정한다.
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
	ClearLocalSuitTransition();
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

void AFirstPersonPlayerController::Client_ShowPresetSelect_Implementation(
	const FGuid& RoundId, bool bImmediateSelections)
{
	if (!IsLocalController() || !RoundId.IsValid() || GameOverRoundId == RoundId)
	{
		return;
	}

	GameOverRoundId = RoundId;
	bGameOverReadySent = false;
	bGameOverImmediateSelections = bImmediateSelections;
	CollapseMainWidgetForDeath();

	// 사망 연출을 먼저 돌리고, Black 패스의 Noise 텍스처가 나타나는 순간 위젯을 띄운다.
	// 연출을 못 돌리는 환경이면 기다리지 않고 바로 띄운다.
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
		: nullptr)
	{
		PPSubsystem->OnDeathBlackNoiseStarted.RemoveAll(this);
		PPSubsystem->OnDeathBlackNoiseStarted.AddUObject(
			this,
			&AFirstPersonPlayerController::HandleDeathBlackNoiseStarted);

		if (PPSubsystem->StartDeathTransition())
		{
			return;
		}

		PPSubsystem->OnDeathBlackNoiseStarted.RemoveAll(this);
	}

	PushGameOverWidget();
}

void AFirstPersonPlayerController::HandleDeathBlackNoiseStarted()
{
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
		: nullptr)
	{
		PPSubsystem->OnDeathBlackNoiseStarted.RemoveAll(this);
	}

	PushGameOverWidget();
}

void AFirstPersonPlayerController::PushGameOverWidget()
{
	if (!IsLocalController() || !GameOverRoundId.IsValid() || bGameOverReadySent)
	{
		return;
	}

	if (!GameOverWidgetClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameOver] GameOverWidgetClass is not set on %s"),
			*GetNameSafe(GetClass()));
		NotifyGameOverUIUnavailable();
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		NotifyGameOverUIUnavailable();
		return;
	}

	FUILayerPushRequest Request;
	Request.WidgetClass = GameOverWidgetClass;
	Request.LayerTag = UILayerTags::Gameplay();
	Request.InputModeTag = FirstPersonInputModeTags::UI();
	// 페어 합의가 성립하면 서버가 이 PlayerState를 소유자로 레이어를 닫는다.
	Request.RequestOwner = PlayerState;
	Request.FocusTarget = EUILayerFocusTarget::Widget;
	Request.bShowCursor = true;
	Request.bReceivesInput = true;

	if (!LayerSubsystem->PushWidget(Request).IsValid())
	{
		NotifyGameOverUIUnavailable();
		return;
	}
	if (UGameOverWidget* GameOver = FindGameOverWidget())
	{
		GameOver->SetImmediateSelectionMode(bGameOverImmediateSelections);
		if (const TSubclassOf<UUILayerKeyHintWidget> HintClass = GameOver->GetKeyHintWidgetClass())
		{
			FUILayerPushRequest HintRequest;
			HintRequest.WidgetClass = HintClass;
			HintRequest.LayerTag = UILayerTags::Modal();
			HintRequest.InputModeTag = FirstPersonInputModeTags::UI();
			HintRequest.RequestOwner = PlayerState;
			HintRequest.FocusTarget = EUILayerFocusTarget::None;
			HintRequest.bShowCursor = true;
			HintRequest.bReceivesInput = false;
			if (LayerSubsystem->PushWidget(HintRequest).IsValid())
			{
				if (UUserWidget* Hint = LayerSubsystem->FindWidgetByOwnerAndClass(PlayerState, HintClass))
				{
					Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
				}
			}
		}
	}
	if (bHasQueuedGameOverPendingRequest)
	{
		if (UGameOverWidget* GameOver = FindGameOverWidget())
		{
			if (GameOver->ShowPendingRequest(
				QueuedGameOverPendingRequest,
				bQueuedGameOverPendingRequester))
			{
				bHasQueuedGameOverPendingRequest = false;
			}
		}
	}

	// 연출 콜백만으로 준비를 알리지 않는다. 실제 위젯 생성과 입력 설정이 끝난 뒤 한 번 통보한다.
	if (FindGameOverWidget())
	{
		bGameOverReadySent = true;
		ServerNotifyGameOverReady(GameOverRoundId);
	}
	else
	{
		NotifyGameOverUIUnavailable();
	}
}

void AFirstPersonPlayerController::NotifyGameOverUIUnavailable()
{
	const FGuid RoundId = GameOverRoundId;
	GameOverRoundId.Invalidate();
	if (RoundId.IsValid())
	{
		ServerNotifyGameOverUnavailable(RoundId);
	}
}

void AFirstPersonPlayerController::ServerNotifyGameOverUnavailable_Implementation(const FGuid& RoundId)
{
	if (AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GM->OnClientGameOverUnavailable(this, RoundId);
	}
}

void AFirstPersonPlayerController::ServerNotifyGameOverReady_Implementation(const FGuid& RoundId)
{
	if (AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GM->OnClientGameOverReady(this, RoundId);
	}
}

UGameOverWidget* AFirstPersonPlayerController::FindGameOverWidget() const
{
	ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	return LayerSubsystem
		? Cast<UGameOverWidget>(LayerSubsystem->FindWidgetByOwnerAndClass(
			PlayerState, GameOverWidgetClass))
		: nullptr;
}

void AFirstPersonPlayerController::ServerRequestGameOverPendingChoice_Implementation(
	const FGameOverPendingRequest& Request)
{
	if (AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GM->RequestGameOverPendingChoice(this, Request);
	}
}

void AFirstPersonPlayerController::ServerRespondGameOverPending_Implementation(
	const FGuid& RoundId, const FGuid& ProposalId, bool bApprove)
{
	if (AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GM->RespondGameOverPending(this, RoundId, ProposalId, bApprove);
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
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>()
		: nullptr)
	{
		// LocalPlayer 서브시스템은 Controller / 월드 교체 후에도 살아남는다.
		UISubsystem->SetTransientWidgetsSuppressed(bHudHiddenForDeath);
	}

	InitializeOutlierPlayerState();
}

void AFirstPersonPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	if (IsLocalController())
	{
		// 원격 클라이언트는 새 Pawn 확인 시 기존 MainUI의 가시성을 되돌린다.
		if (bReleaseDeathTransitionOnPossess)
		{
			RestoreMainWidgetAfterDeath();
		}

		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
			? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
			: nullptr)
		{
			// 리스폰으로 새 폰을 잡은 순간 남은 PP와 사망 연출을 즉시 초기화한다.
			if (bReleaseDeathTransitionOnPossess)
			{
				bReleaseDeathTransitionOnPossess = false;
				PPSubsystem->OnDeathBlackNoiseStarted.RemoveAll(this);
				PPSubsystem->ResetAllPostProcess();
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

void AFirstPersonPlayerController::ArmDeathTransitionReleaseOnPossess_Implementation()
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
	// 리슨 서버 호스트는 서버 Possess에서 로컬 UI를 복구한다.
	if (IsLocalController() && InPawn && bReleaseDeathTransitionOnPossess)
	{
		RestoreMainWidgetAfterDeath();
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
			? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
			: nullptr)
		{
			bReleaseDeathTransitionOnPossess = false;
			PPSubsystem->OnDeathBlackNoiseStarted.RemoveAll(this);
			PPSubsystem->ResetAllPostProcess();
			PPSubsystem->SetChromaticAberrationEnabled(true);
		}
	}

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

	// only add IMCs for local player controllers
	if (!IsLocalPlayerController())
	{
		return;
	}

	// Add Input Mapping Contexts
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}

		CurrentFirstPersonInputMode = Subsystem->GetInputMode().First();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] EnhancedInputLocalPlayerSubsystem is null: %s"), *GetNameSafe(this));
	}

	// UI 입력은 폰이 아니라 컨트롤러 InputComponent에 묶어, 사망으로 폰이 없을 때도 유지한다.
	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent);
	ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || !LayerSubsystem->BindWidgetInput(EnhancedInputComponent, ControllerInputConfig))
	{
		return;
	}

	for (UInputAction* EscapeAction : { ControllerInputConfig->WidgetEscapeAction.Get(), ControllerInputConfig->InGameSettingAction.Get() })
	{
		if (EscapeAction)
		{
			EscapeAction->bTriggerWhenPaused = true;
			EnhancedInputComponent->BindAction(
				EscapeAction, ETriggerEvent::Started, this, &AFirstPersonPlayerController::HandleWidgetEscapeInput);
		}
	}
}

void AFirstPersonPlayerController::HandleWidgetEscapeInput()
{
	PlayWidgetEscapeLocal2DAudio();

	ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	const bool bHandled = LayerSubsystem && LayerSubsystem->RouteWidgetEscapeInput(false);
	const bool bGameOverVisible = FindGameOverWidget() != nullptr;
	if (bHandled)
	{
		return;
	}

	// 사망 중에는 UI 응답 외에 설정 창을 새로 열지 않는다.
	if (GetPawn() && !bGameOverVisible)
	{
		RequestOpenInGameSetting();
	}
}

void AFirstPersonPlayerController::PlayWidgetEscapeLocal2DAudio()
{
	const UOutlierUIAudioSettings* Settings = GetDefault<UOutlierUIAudioSettings>();
	if (!Settings->UITypeTag.IsValid() || !Settings->WidgetEscape.IsValid())
	{
		return;
	}

	UOutlierAudioSubsystem* AudioSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (!AudioSubsystem)
	{
		return;
	}

	FOutlierAudioPlayRequest Request;
	Request.EventTag = Settings->UITypeTag;
	Request.ContextTags.AddTag(Settings->WidgetEscape);
	Request.EmitterActor = GetPawn() ? static_cast<AActor*>(GetPawn()) : this;
	AudioSubsystem->PlayLocal2D(Request);
}




TSubclassOf<UMainUIBase> AFirstPersonPlayerController::GetMainUIClass_Implementation() const
{
	return MainUIClass;
}

void AFirstPersonPlayerController::BindMainUI()
{

}

void AFirstPersonPlayerController::Destroyed()
{
	// 엔진은 Destroyed에서 Pauser를 다른 플레이어로 넘길 수 있다. 그 전에 GameOver 소유권을 정리한다.
	if (AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr)
	{
		GM->CancelGameOverPendingForDisconnect(this);
	}
	Super::Destroyed();
}

void AFirstPersonPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GameOverRoundId.Invalidate();
	ClearLocalSuitTransition();
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

	// PostProcess 서브시스템은 LocalPlayer 소속이라 맵을 옮겨도 살아남는다.
	// 월드 이탈 시 진행 중인 모든 효과를 정리해 로비 / 타이틀까지 따라가지 않게 한다.
	// 교체돼서 이미 LocalPlayer를 넘겨준 옛 PC는 새 PC가 켠 상태를 건드리면 안 된다.
	ULocalPlayer* LP = GetLocalPlayer();
	if (LP && LP->PlayerController == this)
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->SetTransientWidgetsSuppressed(false);
		}
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
			LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->OnDeathBlackNoiseStarted.RemoveAll(this);
			PPSubsystem->ResetAllPostProcess();
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFirstPersonPlayerController::SetPawn(APawn* InPawn)
{
	// SetPawn은 서버 Possess와 클라이언트 Pawn RepNotify 양쪽에서 호출된다.
	// GetPawn은 RepNotify 진입 전에 이미 바뀔 수 있으므로 시작 당시 Pawn과 비교한다.
	if (LocalSuitTransitionId.IsValid() && LocalSuitTransitionPawn.Get() != InPawn)
	{
		ClearLocalSuitTransition();
	}
	Super::SetPawn(InPawn);
}

// 연결 흐름: 서버 단계 요청 -> 소유 클라이언트의 빈 연출 지점 -> 실제 완료 콜백 -> 서버 준비 검증.
// 로컬 ID/단계는 콜백을 구분하기 위한 값일 뿐, 슈트 획득과 단계 진행의 권위는 Shooter 서버에 있다.
void AFirstPersonPlayerController::SendSuitTransitionPhaseFromServer(
	AShooterCharacter* Shooter, const FGuid& TransitionId, ESuitTransitionPhase Phase, float Duration)
{
	if (!HasAuthority() || IsActorBeingDestroyed() || !TransitionId.IsValid())
	{
		return;
	}
	// Dedicated Server에서는 로컬 준비 응답을 만들지 않는다. Listen Host만 직접 같은 수신 경로에 진입한다.
	if (IsLocalController())
	{
		ApplyLocalSuitTransitionPhase(Shooter, TransitionId, Phase, Duration);
	}
	else
	{
		ClientSetSuitTransitionPhase(Shooter, TransitionId, Phase, Duration);
	}
}

void AFirstPersonPlayerController::ClientSetSuitTransitionPhase_Implementation(
	AShooterCharacter* Shooter, FGuid TransitionId, ESuitTransitionPhase Phase, float Duration)
{
	ApplyLocalSuitTransitionPhase(Shooter, TransitionId, Phase, Duration);
}

void AFirstPersonPlayerController::ApplyLocalSuitTransitionPhase(
	AShooterCharacter* Shooter, const FGuid& TransitionId, ESuitTransitionPhase Phase, float Duration)
{
	if (!IsLocalController() || !TransitionId.IsValid())
	{
		return;
	}
	// 종료는 Pawn/Shooter가 먼저 제거되어도 수신해야 한다. 이전 ID의 종료는 새 연출을 건드리지 않는다.
	if (Phase == ESuitTransitionPhase::Idle)
	{
		if (LocalSuitTransitionId == TransitionId)
		{
			ClearLocalSuitTransition();
		}
		return;
	}
	if (!IsValid(Shooter) || !IsValid(GetPawn()) || !FMath::IsFinite(Duration) || Duration < 0.0f)
	{
		return;
	}
	if (Phase == ESuitTransitionPhase::FadingOut)
	{
		if (LocalSuitTransitionId.IsValid())
		{
			return;
		}
		LocalSuitTransitionShooter = Shooter;
		LocalSuitTransitionPawn = GetPawn();
		LocalSuitTransitionId = TransitionId;
	}
	else
	{
		// Reliable 요청 순서를 따른다. 중복/역행 요청으로 완료 플래그를 초기화하거나 연출을 재시작하지 않는다.
		const bool bNextPhase = (Phase == ESuitTransitionPhase::Applying
			&& LocalSuitTransitionPhase == ESuitTransitionPhase::FadingOut)
			|| (Phase == ESuitTransitionPhase::FadingIn
				&& LocalSuitTransitionPhase == ESuitTransitionPhase::Applying);
		if (LocalSuitTransitionId != TransitionId || LocalSuitTransitionShooter.Get() != Shooter
			|| LocalSuitTransitionPawn.Get() != GetPawn() || !bNextPhase)
		{
			return;
		}
	}
	LocalSuitTransitionPhase = Phase;
	bLocalSuitTransitionReadySent = false;
	// 일반 실행은 실제 완료 콜백을 기다린다. 미연결 연출의 PIE 테스트 응답은 아래 기본 훅에서만 생성한다.
	switch (Phase)
	{
	case ESuitTransitionPhase::FadingOut:
		RequestSuitFadeOut(TransitionId, Duration);
		break;
	case ESuitTransitionPhase::Applying:
		RequestSuitPresentationReady(TransitionId);
		break;
	case ESuitTransitionPhase::FadingIn:
		RequestSuitFadeIn(TransitionId, Duration);
		break;
	default:
		break;
	}
}

void AFirstPersonPlayerController::NotifySuitFadeOutFinished(const FGuid& TransitionId)
{
	NotifySuitTransitionPhaseFinished(TransitionId, ESuitTransitionPhase::FadingOut);
}

void AFirstPersonPlayerController::NotifySuitPresentationReady(const FGuid& TransitionId)
{
	NotifySuitTransitionPhaseFinished(TransitionId, ESuitTransitionPhase::Applying);
}

void AFirstPersonPlayerController::NotifySuitFadeInFinished(const FGuid& TransitionId)
{
	NotifySuitTransitionPhaseFinished(TransitionId, ESuitTransitionPhase::FadingIn);
}

void AFirstPersonPlayerController::NotifySuitTransitionPhaseFinished(
	const FGuid& TransitionId, ESuitTransitionPhase Phase)
{
	AShooterCharacter* Shooter = LocalSuitTransitionShooter.Get();
	if (!IsLocalController() || !TransitionId.IsValid() || LocalSuitTransitionId != TransitionId
		|| LocalSuitTransitionPhase != Phase || bLocalSuitTransitionReadySent || !IsValid(Shooter)
		|| !LocalSuitTransitionPawn.IsValid() || LocalSuitTransitionPawn.Get() != GetPawn())
	{
		return;
	}
	// Listen Host의 즉시 응답은 서버 단계를 재진입할 수 있다. 전송 전에 중복 방지 플래그부터 기록한다.
	bLocalSuitTransitionReadySent = true;
	if (HasAuthority())
	{
		ServerNotifySuitTransitionPhaseFinished_Implementation(Shooter, TransitionId, Phase);
	}
	else
	{
		ServerNotifySuitTransitionPhaseFinished(Shooter, TransitionId, Phase);
	}
}

void AFirstPersonPlayerController::ServerNotifySuitTransitionPhaseFinished_Implementation(
	AShooterCharacter* Shooter, FGuid TransitionId, ESuitTransitionPhase Phase)
{
	if (HasAuthority() && IsValid(Shooter))
	{
		// Sender는 클라이언트 인자로 받지 않는다. 이 RPC를 소유한 Controller만 준비 신호의 발신자가 된다.
		Shooter->AcknowledgeSuitTransition(this, TransitionId, Phase);
	}
}

void AFirstPersonPlayerController::ClearLocalSuitTransition()
{
	if (!LocalSuitTransitionId.IsValid())
	{
		return;
	}
	const FGuid Id = LocalSuitTransitionId;
	// 정리 콜백이 이전 완료 콜백을 호출해도 더 이상 서버에 전달되지 않도록 먼저 무효화한다.
	LocalSuitTransitionId.Invalidate();
	LocalSuitTransitionPhase = ESuitTransitionPhase::Idle;
	LocalSuitTransitionShooter.Reset();
	LocalSuitTransitionPawn.Reset();
	bLocalSuitTransitionReadySent = false;
	RequestSuitTransitionCleanup(Id);
}

// Suit 전환 연출: 암전은 LocalPlayer PP의 Screen Blackout(Slate 이후, HUD 포함)으로 그린다.
// 다른 연출(사망/해킹)과 파라미터를 공유하지 않으므로 여기서 다른 연출을 해제하지 않는다.
// LocalPlayer가 없는 Controller는 연출도 완료 응답도 만들지 않고 서버 timeout에 맡긴다.
// 비동기 콜백은 요청 ID를 값으로 캡처한다. 늦게 온 콜백은 Notify의 현재 ID/단계 검증에서 버려진다.
void AFirstPersonPlayerController::RequestSuitFadeOut(const FGuid& TransitionId, float Duration)
{
	// 테스트도 기존 ID/소유자 검증과 서버 최소 시간을 거친다. 화면이 실제로 가려졌다는 보장은 없다.
	if (ShouldBypassSuitPresentationForTesting(GetWorld()))
	{
		UE_LOG(LogTemp, Log, TEXT("[SuitTransition] PIE presentation bypass Controller=%s Phase=FadingOut"), *GetName());
		NotifySuitFadeOutFinished(TransitionId);
		return;
	}
	ULocalPlayerPostProcessSubsystem* PPSubsystem = FindSuitPostProcessSubsystem(this);
	if (!PPSubsystem)
	{
		UE_LOG(LogTemp, Log, TEXT("[SuitTransition] No LocalPlayer presentation Controller=%s Phase=FadingOut"), *GetName());
		return;
	}
	PPSubsystem->OnScreenBlackoutCovered.RemoveAll(this);
	PPSubsystem->OnScreenBlackoutRevealed.RemoveAll(this);
	PPSubsystem->OnScreenBlackoutCovered.AddUObject(
		this, &AFirstPersonPlayerController::HandleSuitScreenBlackoutCovered, TransitionId);
	PPSubsystem->StartScreenBlackout(Duration);
}

void AFirstPersonPlayerController::RequestSuitPresentationReady(const FGuid& TransitionId)
{
	// 테스트에서는 복제/첫 포즈 완료를 기다리지 않는다.
	if (ShouldBypassSuitPresentationForTesting(GetWorld()))
	{
		UE_LOG(LogTemp, Log, TEXT("[SuitTransition] PIE presentation bypass Controller=%s Phase=Applying"), *GetName());
		NotifySuitPresentationReady(TransitionId);
		return;
	}
	if (!FindSuitPostProcessSubsystem(this))
	{
		UE_LOG(LogTemp, Log, TEXT("[SuitTransition] No LocalPlayer presentation Controller=%s Phase=Applying"), *GetName());
		return;
	}
	// 암전은 FadeOut에서 1로 올린 채 둔다. RPC 도착/BlackHold 경과가 아닌 복제된 외형과 무기 표시를 확인한다.
	bSuitPresentationReadyObserved = false;
	SuitPresentationPollCount = 0;
	GetWorldTimerManager().SetTimer(SuitPresentationPollTimer,
		FTimerDelegate::CreateUObject(this, &AFirstPersonPlayerController::PollSuitPresentationReady, TransitionId),
		SuitPresentationPollInterval, true);
}

void AFirstPersonPlayerController::RequestSuitFadeIn(const FGuid& TransitionId, float Duration)
{
	if (ShouldBypassSuitPresentationForTesting(GetWorld()))
	{
		UE_LOG(LogTemp, Log, TEXT("[SuitTransition] PIE presentation bypass Controller=%s Phase=FadingIn"), *GetName());
		NotifySuitFadeInFinished(TransitionId);
		return;
	}
	ULocalPlayerPostProcessSubsystem* PPSubsystem = FindSuitPostProcessSubsystem(this);
	if (!PPSubsystem)
	{
		UE_LOG(LogTemp, Log, TEXT("[SuitTransition] No LocalPlayer presentation Controller=%s Phase=FadingIn"), *GetName());
		return;
	}
	PPSubsystem->OnScreenBlackoutCovered.RemoveAll(this);
	PPSubsystem->OnScreenBlackoutRevealed.RemoveAll(this);
	PPSubsystem->OnScreenBlackoutRevealed.AddUObject(
		this, &AFirstPersonPlayerController::HandleSuitScreenBlackoutRevealed, TransitionId);
	PPSubsystem->StartScreenBlackoutReveal(Duration);
}

void AFirstPersonPlayerController::RequestSuitTransitionCleanup(const FGuid& TransitionId)
{
	// 정상 종료, 취소, Pawn 교체와 EndPlay가 공유한다. 정상 종료면 암전은 이미 0이라 남은 일이 없다.
	GetWorldTimerManager().ClearTimer(SuitPresentationPollTimer);
	bSuitPresentationReadyObserved = false;
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = FindSuitPostProcessSubsystem(this))
	{
		PPSubsystem->OnScreenBlackoutCovered.RemoveAll(this);
		PPSubsystem->OnScreenBlackoutRevealed.RemoveAll(this);
		// 취소로 화면이 덮인 채 끝나면 튀지 않게 남은 만큼 걷어낸다. 서브시스템은 Controller보다 오래 살아 끝까지 진행된다.
		PPSubsystem->StartScreenBlackoutReveal(SuitCleanupRevealDuration);
	}
}

void AFirstPersonPlayerController::HandleSuitScreenBlackoutCovered(FGuid TransitionId)
{
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = FindSuitPostProcessSubsystem(this))
	{
		PPSubsystem->OnScreenBlackoutCovered.RemoveAll(this);
	}
	NotifySuitFadeOutFinished(TransitionId);
}

void AFirstPersonPlayerController::HandleSuitScreenBlackoutRevealed(FGuid TransitionId)
{
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = FindSuitPostProcessSubsystem(this))
	{
		PPSubsystem->OnScreenBlackoutRevealed.RemoveAll(this);
	}
	NotifySuitFadeInFinished(TransitionId);
}

void AFirstPersonPlayerController::PollSuitPresentationReady(FGuid TransitionId)
{
	if (LocalSuitTransitionId != TransitionId || LocalSuitTransitionPhase != ESuitTransitionPhase::Applying)
	{
		GetWorldTimerManager().ClearTimer(SuitPresentationPollTimer);
		bSuitPresentationReadyObserved = false;
		return;
	}
	if (!IsSuitPresentationReady())
	{
		bSuitPresentationReadyObserved = false;
		if (++SuitPresentationPollCount == SuitPresentationStallLogPolls)
		{
			const AShooterCharacter* Shooter = LocalSuitTransitionShooter.Get();
			const APartnerCharacter* Partner = Cast<APartnerCharacter>(GetPawn());
			UE_LOG(LogTemp, Warning,
				TEXT("[SuitTransition] Presentation not ready Controller=%s ShooterSuitApplied=%d ShooterWeapon=%d PartnerWeapon=%d"),
				*GetName(),
				IsValid(Shooter) && Shooter->GetAppliedPresentation() == EShooterPresentation::Suit ? 1 : 0,
				IsValid(Shooter) && IsWeaponPresentedOn(Shooter->GetCurrentWeapon(), Shooter) ? 1 : 0,
				!Partner ? -1 : IsWeaponPresentedOn(Partner->GetCurrentWeapon(), Partner) ? 1 : 0);
		}
		return;
	}
	if (!bSuitPresentationReadyObserved)
	{
		bSuitPresentationReadyObserved = true;
		return;
	}
	GetWorldTimerManager().ClearTimer(SuitPresentationPollTimer);
	bSuitPresentationReadyObserved = false;
	NotifySuitPresentationReady(TransitionId);
}

bool AFirstPersonPlayerController::IsSuitPresentationReady() const
{
	// 두 화면 모두 Shooter의 새 외형(Mesh/ABP)과 Rifle 부착을 확인한다.
	// Shooter 포인터는 서버 RPC 인자로 받았으므로 이 클라이언트에 복제되어 있다.
	const AShooterCharacter* Shooter = LocalSuitTransitionShooter.Get();
	if (!IsValid(Shooter) || Shooter->GetAppliedPresentation() != EShooterPresentation::Suit
		|| !IsWeaponPresentedOn(Shooter->GetCurrentWeapon(), Shooter))
	{
		return false;
	}
	// Partner는 자기 Pawn에 지급된 무기도 확인한다. Shooter 화면은 Partner 무기 복제를 기다리지 않는다.
	if (const APartnerCharacter* Partner = Cast<APartnerCharacter>(GetPawn()))
	{
		return IsWeaponPresentedOn(Partner->GetCurrentWeapon(), Partner);
	}
	return true;
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

	// 오래된 리로드 요청은 위에서 거부한다. 유효한 리로드가 시작될 때만 이전 슈트 연출 콜백을 끊는다.
	ClearLocalSuitTransition();
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
	AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr;
	if (GameMode && GameMode->IsGameOverSelectionActive())
	{
		return;
	}
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

	AFirstPersonPlayerController* OtherController = ShooterController == this
		? PartnerController : ShooterController;
	if (!GameMode || !GameMode->BeginSettingsPause(this, PausingCharacter, OtherController))
	{
		return;
	}
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

}

void AFirstPersonPlayerController::ServerCloseInGameSetting_Implementation()
{
	AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr;
	// 늦게 도착한 설정 닫기 요청이 GameOver 정지를 해제하지 않도록 소유 흐름을 먼저 확인한다.
	if (OutlierGameMode && OutlierGameMode->IsGameOverSelectionActive())
	{
		return;
	}
	if (OutlierGameMode && OutlierGameMode->HandleCheckpointRestartEscape(this))
	{
		return;
	}

	if (OutlierGameMode)
	{
		OutlierGameMode->EndSettingsPause(this);
	}
}

void AFirstPersonPlayerController::ServerRequestLeaveGame_Implementation(bool bQuitAfterLeave)
{
	AOutlierGameMode* OutlierGameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr;
	if (!OutlierGameMode || !OutlierGameMode->HandleExplicitPlayerLeave(this, bQuitAfterLeave))
	{
		ConfirmExplicitLeaveFromServer(false, bQuitAfterLeave);
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
		OutlierPlayerState->SetPlayerRole(EOutlierPlayerRole::Shooter);
		OutlierPlayerState->SetShooterCharacter(ShooterCharacter);
	}
	else if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetPawn()))
	{
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

void FHudWidgetCollapseState::Apply(UWidget* Widget, bool bShow)
{
	if (!Widget)
	{
		return;
	}

	if (!bShow && !bCollapsed)
	{
		VisibilityBeforeCollapse = Widget->GetVisibility();
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		bCollapsed = true;
	}
	else if (bShow && bCollapsed)
	{
		Widget->SetVisibility(VisibilityBeforeCollapse);
		bCollapsed = false;
	}
}

void AFirstPersonPlayerController::CollapseMainWidgetForDeath()
{
	bHudHiddenForDeath = true;
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>()
		: nullptr)
	{
		UISubsystem->SetTransientWidgetsSuppressed(true);
	}
	RefreshHudVisibility();
}

void AFirstPersonPlayerController::RestoreMainWidgetAfterDeath()
{
	bHudHiddenForDeath = false;
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>()
		: nullptr)
	{
		UISubsystem->SetTransientWidgetsSuppressed(false);
	}
	RefreshHudVisibility();
}

void AFirstPersonPlayerController::RefreshHudVisibility()
{
	MainWidgetCollapseState.Apply(ShooterUIInstance, ShouldShowMainWidget());
}

bool AFirstPersonPlayerController::ShouldShowMainWidget() const
{
	return !bHudHiddenForDeath;
}

void AFirstPersonPlayerController::RefreshPostProcessState()
{
	
}
