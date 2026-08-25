// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "OutlierGameInstance.generated.h"

/**
 * 
 */

class ULoadingWidget;

UCLASS()
class OUTLIER_API UOutlierGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

private:

	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandlePreLoadMap(const FString& MapName);
	void TryBootstrapArenaWorker(UWorld* LoadedWorld);

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<ULoadingWidget> LoadingWidgetClass;

	UPROPERTY()
	TObjectPtr<ULoadingWidget> LoadingWidget;
private:

	bool bTriedConnect = false;
	bool bArenaWorkerTravelRequested = false;

};
