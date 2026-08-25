// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "OutlierGameInstance.generated.h"

/**
 * 
 */

class ULoadingWidget;
class UNetDriver;

UCLASS()
class OUTLIER_API UOutlierGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	void NotifyArenaHandoffStarted();

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

};
