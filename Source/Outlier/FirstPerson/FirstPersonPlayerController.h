// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Audio/OutlierAudioTypes.h"
#include "OutlierPlayerState.h"
#include "PlayerUIProvider.h"
#include "Save/OutlierCheckpointRestartVote.h"
#include "UI/UILayerTypes.h"
#include "UI/GameOverPendingTypes.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "Containers/Ticker.h"
#include "FirstPersonPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UControllerInputConfig;
class UInGameSettingWidget;
class UCameraShakeBase;
class UOutlierUpgradeSetData;
class UGameOverWidget;
class UGameOverPendingWidget;
class UWidget;
class AActor;
class AShooterCharacter;
enum class ESuitTransitionPhase : uint8;

// HUD 위젯을 숨길 사유가 생기는 순간의 Visibility를 저장했다가, 사유가 모두 풀리면 그 값으로 되돌린다.
struct FHudWidgetCollapseState
{
	ESlateVisibility VisibilityBeforeCollapse = ESlateVisibility::Visible;
	bool bCollapsed = false;

	void Apply(UWidget* Widget, bool bShow);
	void Reset() { bCollapsed = false; }
};

namespace FirstPersonInputModeTags
{
	inline FGameplayTag EMP()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Input.Mode.EMP")));
		return Tag;
	}

	inline FGameplayTag Hack()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Input.Mode.Hack")));
		return Tag;
	}

	inline FGameplayTag UI()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Input.Mode.UI")));
		return Tag;
	}
}

DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnCheckpointRestartVoteViewChanged,
	EOutlierCheckpointRestartVoteView);

/**
 * 
 */
UCLASS()
class OUTLIER_API AFirstPersonPlayerController : public APlayerController ,  public IPlayerUIProvider
{
	GENERATED_BODY()
	friend class FOutlierSuitTransitionTest;
	
public:
	AFirstPersonPlayerController();
	virtual void SetPawn(APawn* InPawn) override;

	// 서버가 시작 당시 확보한 두 Controller에만 전달한다. Listen Host도 같은 로컬 처리 함수를 사용한다.
	void SendSuitTransitionPhaseFromServer(AShooterCharacter* Shooter, const FGuid& TransitionId,
		ESuitTransitionPhase Phase, float Duration);

	// 담당자의 실제 완료 콜백에서 요청받은 ID를 전달한다. 요청 수신/타이머만으로 호출하지 않는다.
	UFUNCTION(BlueprintCallable, Category = "Suit|Transition")
	void NotifySuitFadeOutFinished(const FGuid& TransitionId);

	UFUNCTION(BlueprintCallable, Category = "Suit|Transition")
	void NotifySuitPresentationReady(const FGuid& TransitionId);

	UFUNCTION(BlueprintCallable, Category = "Suit|Transition")
	void NotifySuitFadeInFinished(const FGuid& TransitionId);

	/** Client-owned transport for relevant AtLocation audio requests. */
	UFUNCTION(Server, Unreliable)
	void ServerRequestRelevantAudioAtLocation(const FOutlierAudioPlayRequest& Request);

	/** Delivers an already resolved Owner/Relevant sound to this client. */
	UFUNCTION(Client, Unreliable)
	void ClientPlayResolvedAudio(const FOutlierResolvedAudioPlay& ResolvedPlay);

	/** Stops a server-started persistent audio instance on this client. */
	UFUNCTION(Client, Unreliable)
	void ClientStopResolvedAudio(int32 AudioInstanceId);

	UFUNCTION(Client, Reliable)
	void ClientArenaLoad(FVector InSpawnLocation, uint32 ReconnectRequestId);

	UFUNCTION(Client, Reliable)
	void ClientPushUILayer(const FUILayerPushRequest& Request);

	UFUNCTION(Client, Reliable)
	void ClientShowGameOverPending(const FGameOverPendingRequest& Request, bool bIsRequester);

	UFUNCTION(Client, Reliable)
	void ClientCloseGameOverPending();

	UFUNCTION(Client, Reliable)
	void BeginGameOverRespawnTransition();

	UFUNCTION(BlueprintCallable, Category = "UI|InGame Setting")
	void RequestOpenInGameSetting();

	UFUNCTION(BlueprintCallable, Category = "UI|InGame Setting")
	void RequestCloseInGameSetting();

	void RequestLeaveGame(bool bQuitAfterLeave = false);
	bool HasRequestedExplicitLeave() const { return bExplicitLeaveRequested; }
	FSimpleMulticastDelegate OnExplicitLeaveStateChanged;
	void ConfirmExplicitLeaveFromServer(bool bAccepted, bool bQuitAfterLeave);

	void RequestGameOverPendingChoice(const FGameOverPendingRequest& Request);
	void RequestGameOverPendingResponse(bool bApprove);
	void ShowGameOverPendingFromServer(const FGameOverPendingRequest& Request, bool bIsRequester);
	void CloseGameOverPendingFromServer();
	bool HasGameOverPendingWidgetClass() const;

	void RequestCheckpointRestart();
	void RequestCheckpointRestartResponse(bool bApprove);
	void RequestCancelCheckpointRestart();
	bool CanRequestCheckpointRestart() const { return bCanRequestCheckpointRestart; }
	EOutlierCheckpointRestartVoteView GetCheckpointRestartVoteView() const
	{
		return CheckpointRestartVoteView;
	}

	FOnCheckpointRestartVoteViewChanged OnCheckpointRestartVoteViewChanged;

	UFUNCTION(Client, Reliable)
	void ClientPopInGameSettingLayer(UObject* RequestOwner);

	UFUNCTION(Server, Reliable)
	void ServerTryActivateUpgradeNode(
		AActor* UpgradeOwner,
		FName NodeIdOrRowName,
		UOutlierUpgradeSetData* UpgradeSetData);

	UFUNCTION(Server, Reliable)
	void ServerNotifyArenaReady(uint32 GameplayGeneration);

	UFUNCTION(Server, Reliable)
	void ServerNotifyArenaGameplayUnloaded(uint32 GameplayGeneration);

	UFUNCTION(Client, Reliable)
	void ClientActivateArenaGameplayLevels(uint32 GameplayGeneration);

	UFUNCTION(Client, Reliable)
	void ClientRetryArenaGameplayReload(uint32 GameplayGeneration);

	UFUNCTION(Server, Reliable)
	void ServerOpenInGameSetting();

	UFUNCTION(Server, Reliable)
	void ServerCloseInGameSetting();

	UFUNCTION(Server, Reliable)
	void ServerRequestLeaveGame(bool bQuitAfterLeave);

	UFUNCTION(Client, Reliable)
	void ClientConfirmExplicitLeave(bool bAccepted, bool bQuitAfterLeave);

	UFUNCTION(Server, Reliable)
	void ServerRequestCheckpointRestart();

	UFUNCTION(Server, Reliable)
	void ServerRespondCheckpointRestart(bool bApprove);

	UFUNCTION(Server, Reliable)
	void ServerCancelCheckpointRestart();

	UFUNCTION(Client, Reliable)
	void ClientConfigureCheckpointRestart(bool bCanRequest);

	UFUNCTION(Client, Reliable)
	void ClientSetCheckpointRestartVoteView(EOutlierCheckpointRestartVoteView VoteView);

	UFUNCTION(Client, Reliable)
	void ClientPrepareForArenaExit();

	UFUNCTION(Client, Reliable)
	void ClientConfigureListenReconnect(FGuid ReconnectToken);

	// Listen Host의 로컬 Controller에는 Client RPC가 전송되지 않으므로 서버가 같은 적용 함수를 직접 호출한다.
	void ConfigureCheckpointRestartFromServer(bool bCanRequest);
	void SetCheckpointRestartVoteViewFromServer(EOutlierCheckpointRestartVoteView VoteView);
	void CloseCheckpointRestartVoteUIFromServer(UObject* RequestOwner);

	// ArenaWorker(dedi)는 서버에서 Possess한 뒤 클라이언트가 설정된 서브레벨을
	// 표시할 때까지 기다린다. 스폰 좌표는 준비 완료 뒤의 위치 확인에도 사용한다.
	UFUNCTION(Client, Reliable)
	void ClientPrepareForArenaStart(FVector InSpawnLocation);

	// 서버에서 계산한 폭발 충격을 소유 클라이언트의 CameraManager에 전달한다.
	UFUNCTION(Client, Unreliable)
	void ClientPlayExplosionCameraShake(
		TSubclassOf<UCameraShakeBase> CameraShakeClass,
		float Scale,
		bool bAllowInactivePawn);

	// 디버그: 요청한 페어의 arena를 서버 권위로 리로드
	// UFUNCTION(Server, Reliable)
	// void Server_RequestArenaReload();

	// 리로드 RPC에는 서버가 선택한 실제 리스폰 위치를 함께 전달한다.

	UFUNCTION(Client, Reliable)
	void ClientArenaGameplayReload(uint32 GameplayGeneration, FVector InSpawnLocation);

	// 클라이언트 콘솔에서 현재 스트리밍 상태와 레벨별 액터를 확인한다.
	// UFUNCTION(Exec)
	// void ArenaDumpClientGameplayReload();

	// UFUNCTION(Exec)
	// void ArenaDumpClientGameplayActors();

	// 사망 시 게임오버 화면을 띄운다 (페어 양쪽 컨트롤러에 각각 호출됨).
	UFUNCTION(Client, Reliable)
	void Client_ShowPresetSelect(bool bImmediateSelections = false);

	UFUNCTION(Server, Reliable)
	void ServerRequestGameOverPendingChoice(const FGameOverPendingRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerRespondGameOverPending(bool bApprove);

	UFUNCTION()
	void HandleArenaShown();
	void HandleArenaGameplayReady(uint32 GameplayGeneration);
	void HandleArenaGameplayUnloaded(uint32 GameplayGeneration);
	void ControlMainWidget(bool InFlag) const;
	// 사망 사유를 켜고 끈다. 실제 표시 여부는 RefreshHudVisibility가 다른 사유와 합쳐서 정한다.
	void CollapseMainWidgetForDeath();
	void RestoreMainWidgetAfterDeath();
	// 서버가 소유 클라이언트에 예약한다. 실제 암전/HUD 해제는 새 Pawn을 잡을 때 실행한다.
	UFUNCTION(Client, Reliable)
	void ArmDeathTransitionReleaseOnPossess();

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool SetFirstPersonInputMode(FGameplayTag NewInputMode);

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool TryRestoreFirstPersonDefaultInputMode(FGameplayTag ExpectedInputMode);

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool RestoreFirstPersonDefaultInputMode();

	UFUNCTION(BlueprintPure, Category = "Input|Input Mode")
	FGameplayTag GetFirstPersonInputMode()const; 

	UFUNCTION(BlueprintPure, Category = "Input|Input Mode")
	bool IsFirstPersonInputMode(FGameplayTag InputMode) const;
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UControllerInputConfig> ControllerInputConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair")
	EOutlierPlayerRole DefaultPlayerRole = EOutlierPlayerRole::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair")
	int32 DefaultPairId = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Input|Input Mode")
	FGameplayTag CurrentFirstPersonInputMode;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void AcknowledgePossession(APawn* P) override;

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnRep_PlayerState() override;

	// Input Mapping Context Setup
	virtual void SetupInputComponent() override;
	void HandleWidgetEscapeInput();
	void PlayWidgetEscapeLocal2DAudio();

	virtual TSubclassOf<UMainUIBase> GetMainUIClass_Implementation() const;

	virtual void BindMainUI();
	virtual void BindPostProcessSubSystem();

	void InitializeOutlierPlayerState();
	void RegisterCurrentPawnWithPlayerState();
	// SwapPlayerControllers 타이밍에 최초 ServerUpdateLevelVisibility 보고가 씹히는 경우를
	// 대비해, Possess 확정 후 현재 로드된 서브레벨 visibility를 서버에 다시 보고한다.
	void ReportLoadedLevelsVisibilityToServer();
	void TryNotifyArenaStartReady();
	// 리로드 RPC가 실어 보낸 새 스폰 위치를 기록한다.
	void ApplyServerArenaSpawnLocation(const FVector& InSpawnLocation);
	bool TickClientArenaContentReady(float DeltaTime);
	void ClearClientArenaContentWait();
	virtual void RefreshPostProcessState();

	// 숨김 사유(사망, Partner 빙의)를 합쳐 HUD 표시 여부를 다시 맞춘다. 사유가 바뀔 때마다 호출한다.
	virtual void RefreshHudVisibility();
	virtual bool ShouldShowMainWidget() const;
	bool IsHudHiddenForDeath() const { return bHudHiddenForDeath; }

	// 사망 연출의 Black Noise 텍스처가 나타날 때 GameOverWidget을 띄운다.
	void HandleDeathBlackNoiseStarted();
	void PushGameOverWidget();
	UGameOverWidget* FindGameOverWidget() const;

	// 암전 담당자 연결 지점. PIE에서는 테스트용 완료 응답을 보내며, 일반 실행에서는 실제 연출 연결을 기다린다.
	// 실제 콜백 검증 시 Outlier.SuitTransition.BypassPresentation 0으로 테스트 우회를 끈다.
	virtual void RequestSuitFadeOut(const FGuid& TransitionId, float Duration);
	virtual void RequestSuitPresentationReady(const FGuid& TransitionId);
	virtual void RequestSuitFadeIn(const FGuid& TransitionId, float Duration);
	virtual void RequestSuitTransitionCleanup(const FGuid& TransitionId);

private:
	UFUNCTION(Client, Reliable)
	void ClientSetSuitTransitionPhase(AShooterCharacter* Shooter, FGuid TransitionId,
		ESuitTransitionPhase Phase, float Duration);

	UFUNCTION(Server, Reliable)
	void ServerNotifySuitTransitionPhaseFinished(AShooterCharacter* Shooter, FGuid TransitionId,
		ESuitTransitionPhase Phase);

	void ApplyLocalSuitTransitionPhase(AShooterCharacter* Shooter, const FGuid& TransitionId,
		ESuitTransitionPhase Phase, float Duration);
	void NotifySuitTransitionPhaseFinished(const FGuid& TransitionId, ESuitTransitionPhase Phase);
	void ClearLocalSuitTransition();

	TWeakObjectPtr<AShooterCharacter> LocalSuitTransitionShooter;
	TWeakObjectPtr<APawn> LocalSuitTransitionPawn;
	FGuid LocalSuitTransitionId;
	ESuitTransitionPhase LocalSuitTransitionPhase;
	bool bLocalSuitTransitionReadySent = false;


protected:

	bool bHasPendingArenaRequest = false;
	uint32 PendingGameplayGeneration = 0;
	double ClientGameplayReloadStartedAt = 0.0;
	// 서버가 계산한 실제 스폰 위치. 클라이언트 준비 진단에도 사용한다.
	FVector PendingArenaSpawnLocation = FVector::ZeroVector;
	bool bWaitingForArenaStart = false;
	int32 ClientArenaReadyStableFrames = 0;
	FTSTicker::FDelegateHandle ClientArenaContentTickerHandle;

	// 서브레벨 로드 정체 진단. 완료로 강제 전환하지 않는다.
	double ClientArenaWaitSeconds = 0.0;

	UPROPERTY()
	TObjectPtr<UMainUIBase> ShooterUIInstance;
	FHudWidgetCollapseState MainWidgetCollapseState;
	bool bHudHiddenForDeath = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UMainUIBase> MainUIClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|InGame Setting")
	TSubclassOf<UInGameSettingWidget> InGameSettingWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Game Over")
	TSubclassOf<UGameOverWidget> GameOverWidgetClass;

	FGameOverPendingRequest QueuedGameOverPendingRequest;
	bool bHasQueuedGameOverPendingRequest = false;
	bool bGameOverImmediateSelections = false;
	bool bQueuedGameOverPendingRequester = false;

	bool bCanRequestCheckpointRestart = false;
	bool bExplicitLeaveRequested = false;

	// 리로드(리스폰) RPC를 받은 뒤 첫 Possess에서 사망 연출을 즉시 끊는다. Possess만으로 판단하면
	// 연출 도중의 다른 Possess(Partner의 적 해킹 등)까지 연출을 끊어서 위젯이 안 뜨게 된다.
	bool bReleaseDeathTransitionOnPossess = false;
	EOutlierCheckpointRestartVoteView CheckpointRestartVoteView =
		EOutlierCheckpointRestartVoteView::None;
};
