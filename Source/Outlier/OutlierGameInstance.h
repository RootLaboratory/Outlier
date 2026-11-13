// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Containers/Ticker.h"
#include "OutlierGameInstance.generated.h"

/**
 * 
 */

class ULoadingWidget;
class UNetDriver;
class UWorld;
class UGameViewportClient;
class SWidget;

UCLASS()
class OUTLIER_API UOutlierGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	void NotifyArenaHandoffStarted(const FString& ArenaUrl);
	void NotifyListenReconnectToken(const FGuid& Token);
	void PrepareForExplicitLeave();
	void HandleArenaRecoveryFailure(const FString& Reason);
	void SetArenaRecoveryFailureReason(const FString& Reason);
	void ReturnAfterArenaRecoveryFailure(bool bReturnToLobby);
	bool ShouldReturnToLobbyAfterRecoveryFailure() const { return bArenaHandoffActive; }
	bool GetLocalPlayerId(FGuid& OutPlayerId) const;
	bool GetLocalSaveCredentials(FGuid& OutSaveId, FGuid& OutResumeKey) const;
	bool StoreLocalSaveCredentials(const FGuid& SaveId, const FGuid& ResumeKey);
	// 서버가 매치 종료를 확정한 뒤 호출한다. Travel로 Controller가 바뀌어도 종료 의도를 유지한다.
	void RequestQuitAfterExplicitLeave();

private:
	friend class FOutlierArenaReturnLifecycleTest;
	friend class FOutlierReconnectWatchdogTest;

	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandlePreLoadMap(const FString& MapName);
	void HandleNetworkFailure(
		UWorld* World,
		UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType,
		const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void EnsureArenaReconnectTicker();
	void CompleteArenaReconnectAttempt();
	void ShowArenaRecoveryFailureNotice();
	void RemoveArenaRecoveryFailureNotice();
	void TryBootstrapArenaWorker(UWorld* LoadedWorld);
	bool HandleArenaWorkerBootstrapTick(float DeltaTime);
	bool HandleArenaReconnectTick(float DeltaTime);
	void ScheduleArenaReconnect();
	bool TryQueueLobbyRecovery();
	bool TravelToLobby(UWorld* World);
	void ResetArenaHandoffState();
	void ResetListenReconnectState();

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<ULoadingWidget> LoadingWidgetClass;

	UPROPERTY()
	TObjectPtr<ULoadingWidget> LoadingWidget;
private:

	bool bTriedConnect = false;
	bool bQuitAfterExplicitLeave = false;
	bool bArenaWorkerTravelRequested = false;
	bool bArenaHandoffActive = false;
	bool bLobbyRecoveryQueued = false;
	bool bLobbyRecoveryAttempted = false;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FTSTicker::FDelegateHandle ArenaWorkerBootstrapTickerHandle;
	FTSTicker::FDelegateHandle ArenaReconnectTickerHandle;
	TWeakObjectPtr<UWorld> ArenaWorkerBootstrapWorld;
	TWeakObjectPtr<UNetDriver> ArenaReconnectPreviousNetDriver;
	int32 ArenaWorkerReadyStableFrames = 0;
	// Worker 재접속은 최초 Handoff의 신원 옵션이 포함된 URL을 그대로 재사용한다.
	FString LastArenaHandoffUrl;
	FString LastListenReconnectUrl;
	double ArenaReconnectDeadlineSeconds = 0.0;
	double ArenaReconnectNextAttemptSeconds = 0.0;
	double LobbyReturnDeadlineSeconds = 0.0;
	bool bArenaReconnectActive = false;
	bool bArenaReconnectTravelInFlight = false;
	bool bLobbyReturnInFlight = false;
	bool bArenaRecoveryExitRequested = false;
	FString ArenaRecoveryFailureReason;
	TSharedPtr<SWidget> ArenaRecoveryFailureNotice;
	TWeakObjectPtr<UGameViewportClient> ArenaRecoveryFailureViewport;
	FGuid LocalPlayerId;
	FGuid LocalSaveId;
	FGuid LocalResumeKey;
	bool SaveLocalIdentity() const;

};
