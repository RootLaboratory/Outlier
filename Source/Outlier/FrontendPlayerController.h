// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "OutlierPlayerState.h"
#include "FrontendPlayerController.generated.h"

/**
 * 
 */

class UTitleMainWidget;
class ULoadingWidget;
UCLASS()
class OUTLIER_API AFrontendPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	virtual void BeginPlay() override;
	virtual void AcknowledgePossession(APawn* P) override;

public:
	UFUNCTION(Server, Reliable)
	void ServerRequestMatchmaking();

	UFUNCTION(BlueprintCallable)
	void RequestSelectLobbyRole(EOutlierPlayerRole DesiredRole);

	UFUNCTION(BlueprintCallable)
	void RequestStartPendingMatch();

	UFUNCTION(Server, Reliable)
	void ServerRequestSelectLobbyRole(EOutlierPlayerRole DesiredRole);

	UFUNCTION(Server, Reliable)
	void ServerRequestStartPendingMatch();
public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr< UTitleMainWidget> TitleWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UTitleMainWidget> TitleWidgetClass;
};
