// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Audio/OutlierAudioTypes.h"
#include "OutlierPlayerState.h"
#include "PlayerUIProvider.h"
#include "Save/OutlierCheckpointRestartVote.h"
#include "UI/UILayerTypes.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "Containers/Ticker.h"
#include "FirstPersonPlayerController.generated.h"

class UInputMappingContext;
class UInGameSettingWidget;
class UCameraShakeBase;
class UOutlierUpgradeSetData;
class UPreSetLoadWidget;
class UWorldPartitionStreamingSourceComponent;
class AActor;

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
	
public:
	AFirstPersonPlayerController();

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
	void ClientArenaLoad(FVector InSpawnLocation);

	UFUNCTION(Client, Reliable)
	void ClientPushUILayer(const FUILayerPushRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "UI|InGame Setting")
	void RequestOpenInGameSetting();

	UFUNCTION(BlueprintCallable, Category = "UI|InGame Setting")
	void RequestCloseInGameSetting();

	void RequestLeaveGame();

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
	void ServerNotifyArenaReady();

	UFUNCTION(Server, Reliable)
	void ServerNotifyArenaGameplayGCReady(uint32 GameplayGeneration);

	UFUNCTION(Client, Reliable)
	void ClientRetryArenaGameplayReload(uint32 GameplayGeneration);

	UFUNCTION(Server, Reliable)
	void ServerOpenInGameSetting();

	UFUNCTION(Server, Reliable)
	void ServerCloseInGameSetting();

	UFUNCTION(Server, Reliable)
	void ServerRequestLeaveGame();

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

	// Listen Host의 로컬 Controller에는 Client RPC가 전송되지 않으므로 서버가 같은 적용 함수를 직접 호출한다.
	void ConfigureCheckpointRestartFromServer(bool bCanRequest);
	void SetCheckpointRestartVoteViewFromServer(EOutlierCheckpointRestartVoteView VoteView);
	void CloseCheckpointRestartVoteUIFromServer(UObject* RequestOwner);

	// ArenaWorker(dedi)는 서버에서 즉시 Possess하고 이 RPC로 클라 준비를 시작시킨다.
	// Possess 직후라 클라에는 아직 Pawn이 복제되지 않았고, Pawn이 들어있는 WP 셀을
	// 스트리밍해야 Pawn이 관련(relevant)해지는 순환이 생긴다. ClientArenaLoad와 동일하게
	// 서버가 이미 계산해둔 스폰 위치를 같이 넘겨서 그 순환을 끊는다.
	UFUNCTION(Client, Reliable)
	void ClientPrepareForArenaStart(FVector InSpawnLocation);

	// 서버에서 계산한 폭발 충격을 소유 클라이언트의 CameraManager에 전달한다.
	UFUNCTION(Client, Unreliable)
	void ClientPlayExplosionCameraShake(
		TSubclassOf<UCameraShakeBase> CameraShakeClass,
		float Scale,
		bool bAllowInactivePawn);

	// 디버그: 요청한 페어의 arena를 서버 권위로 리로드
	UFUNCTION(Server, Reliable)
	void Server_RequestArenaReload();

	// 리로드 RPC도 최초 진입(ClientArenaLoad/ClientPrepareForArenaStart)과 동일하게 스폰 위치를
	// 같이 받는다. 위젯이 서버로 보내는 건 FName StageId 하나뿐이고, 그걸 APresetPlayerStart의
	// 좌표로 바꾸는 건 서버의 ResolvePresetStageSpawn이다 — 클라에는 그 결과가 오는 창구가 없어서,
	// 안 보내면 PendingArenaSpawnLocation에 최초 진입 때 값이 그대로 남아 엉뚱한 곳을 스트리밍한다.
	// 클라가 직접 StageId로 액터를 찾게 하는 방법도 있지만, 그러면 클라 준비 판정이 레벨 배치
	// (해당 액터가 그 순간 로드돼 있는가)에 묶인다. 좌표는 값이라 그런 의존이 없다.

	// 디버그: 클라 arena 스트리밍 인스턴스를 강제 언로드 후 재로드
	UFUNCTION(Client, Reliable)
	void ClientArenaReload(FVector InSpawnLocation);

	UFUNCTION(Client, Reliable)
	void ClientArenaGameplayReload(uint32 GameplayGeneration, FVector InSpawnLocation);

	// 사망 시 프리셋 스테이지 선택 팝업을 띄운다 (페어 양쪽 컨트롤러에 각각 호출됨).
	UFUNCTION(Client, Reliable)
	void Client_ShowPresetSelect();

	// 위젯에서 고른 스테이지를 서버에 보고한다. 즉시 확정이 아니라 페어 상대와 같은
	// 스테이지가 모일 때까지 GameMode가 대기하는 후보 제출일 뿐이다.
	UFUNCTION(Server, Reliable)
	void Server_SelectPresetStage(FName StageId);

	UFUNCTION()
	void HandleArenaShown();
	void HandleArenaGameplayReady(uint32 GameplayGeneration);
	void HandleArenaGameplayGCReady(uint32 GameplayGeneration);
	void ControlMainWidget(bool InFlag) const;

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

	virtual TSubclassOf<UMainUIBase> GetMainUIClass_Implementation() const;

	virtual void BindMainUI();
	virtual void BindPostProcessSubSystem();

	void InitializeOutlierPlayerState();
	void RegisterCurrentPawnWithPlayerState();
	// SwapPlayerControllers 타이밍에 최초 ServerUpdateLevelVisibility 보고가 씹히는 경우를
	// 대비해, Possess 확정 후 현재 로드된 서브레벨 visibility를 서버에 다시 보고한다.
	void ReportLoadedLevelsVisibilityToServer();
	void TryNotifyArenaStartReady();
	// 리로드 RPC가 실어 보낸 새 스폰 위치를 적용한다(옛 임시 소스 폐기 포함).
	void ApplyServerArenaSpawnLocation(const FVector& InSpawnLocation);
	bool TickClientArenaContentReady(float DeltaTime);
	void ClearClientArenaContentWait();
	void ReleaseClientArenaStreamingSource();
	bool ResolveClientArenaStreamingLocation(FVector& OutLocation) const;
	virtual void RefreshPostProcessState();

	UFUNCTION()
	void HandleLocalPresetStageSelected(FName StageId);


protected:

	bool bHasPendingArenaRequest = false;
	uint32 PendingGameplayGeneration = 0;
	// 서버가 이미 계산해둔 실제 스폰 위치. Possess 전이라 GetPawn()이 아직 없을 때
	// ResolveClientArenaStreamingLocation이 레벨 액터를 추측해서 찾는 대신 이 값을 그대로 쓴다.
	FVector PendingArenaSpawnLocation = FVector::ZeroVector;
	bool bHasPendingArenaSpawnLocation = false;
	// 리로드 중에는 GetPawn()보다 이 값이 우선이다. 리로드 시점의 Pawn은 서버가 이미 Destroy한
	// "죽은 자리의 옛 폰"이라, 그걸로 스트리밍 소스를 세우면 새 스테이지가 아니라 직전 위치를
	// 스트리밍하고는 즉시 준비 완료로 판정해버린다.
	bool bPreferServerArenaSpawnLocation = false;
	bool bWaitingForArenaStart = false;
	int32 ClientArenaReadyStableFrames = 0;
	FTSTicker::FDelegateHandle ClientArenaContentTickerHandle;

	// 클라 콘텐츠 대기 워치독.
	// 엔진의 레벨 가시화 요청에는 재전송이 없다(ULevelStreaming::ShouldWaitForServerAckBeforeChangingVisibilityState는
	// 요청을 한 번 보내고 bHasClientPendingRequest를 소진한 뒤 오지 않을 ack을 영원히 기다린다).
	// 한 번 유실되면 그 셀은 영구 미완료이고, TickClientArenaContentReady는 타임아웃이 없어
	// 클라는 로딩에서, 서버는 PendingPossessions에서 각각 무한 대기한다(로그도 안 남는다).
	//
	// 복구 레버는 하나뿐이다 — 셀을 "필요 없음"으로 떨어뜨렸다가 다시 필요하게 만드는 것.
	//   LevelStreaming.cpp:676  MakingVisible 에서 ShouldBeVisible()==false 면 LoadedNotVisible 로 하강
	//   LevelStreaming.cpp:1163 LoadedNotVisible -> MakingVisible 재진입 시
	//                           InvalidateClientPendingRequest() + BeginClientNetVisibilityRequest(true)
	// 즉 죽은 요청이 폐기되고 새 요청이 발행된다. 임시 소스 액터를 멀리 옮겼다 되돌려 그 왕복을 만든다
	// (스트리밍 소스 위치는 소유 액터 트랜스폼에서 나온다 — WorldPartitionStreamingSourceComponent.cpp:88).
	double ClientArenaWaitSeconds = 0.0;
	double ClientArenaRecoveryHoldSeconds = 0.0;
	int32 ClientArenaRecoveryCount = 0;
	bool bClientArenaSourceDisplaced = false;
	FVector ClientArenaSourceHomeLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ClientArenaStreamingSourceActor;

	UPROPERTY(Transient)
	TObjectPtr<UWorldPartitionStreamingSourceComponent> ClientArenaStreamingSource;

	UPROPERTY()
	TObjectPtr<UMainUIBase> ShooterUIInstance;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UMainUIBase> MainUIClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|InGame Setting")
	TSubclassOf<UInGameSettingWidget> InGameSettingWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Preset")
	TSubclassOf<UPreSetLoadWidget> PresetLoadWidgetClass;

	bool bCanRequestCheckpointRestart = false;
	bool bExplicitLeaveRequested = false;
	EOutlierCheckpointRestartVoteView CheckpointRestartVoteView =
		EOutlierCheckpointRestartVoteView::None;
};
