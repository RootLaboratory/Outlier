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

UCLASS()
class OUTLIER_API UOutlierGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	void NotifyArenaHandoffStarted(const FString& ArenaUrl);
	void PrepareForExplicitLeave();

private:
	friend class FOutlierArenaReturnLifecycleTest;

	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandlePreLoadMap(const FString& MapName);
	void HandleNetworkFailure(
		UWorld* World,
		UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType,
		const FString& ErrorString);
	void TryBootstrapArenaWorker(UWorld* LoadedWorld);
	bool HandleArenaWorkerBootstrapTick(float DeltaTime);
	bool HandleArenaReconnectTick(float DeltaTime);
	void ScheduleArenaReconnect();
	bool TryQueueLobbyRecovery();
	bool TravelToLobby(UWorld* World);
	void ResetArenaHandoffState();

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<ULoadingWidget> LoadingWidgetClass;

	UPROPERTY()
	TObjectPtr<ULoadingWidget> LoadingWidget;
private:

	bool bTriedConnect = false;
	bool bArenaWorkerTravelRequested = false;
	bool bArenaHandoffActive = false;
	bool bLobbyRecoveryQueued = false;
	bool bLobbyRecoveryAttempted = false;
	FDelegateHandle NetworkFailureHandle;
	FTSTicker::FDelegateHandle ArenaWorkerBootstrapTickerHandle;
	FTSTicker::FDelegateHandle ArenaReconnectTickerHandle;
	TWeakObjectPtr<UWorld> ArenaWorkerBootstrapWorld;
	int32 ArenaWorkerReadyStableFrames = 0;
	// Worker 재접속은 최초 Handoff의 신원 옵션이 포함된 URL을 그대로 재사용한다.
	FString LastArenaHandoffUrl;
	double ArenaReconnectDeadlineSeconds = 0.0;
	bool bArenaReconnectActive = false;

};
