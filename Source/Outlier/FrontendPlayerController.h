// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "OutlierPlayerState.h"
#include "Network/OutlierMatchRequest.h"
#include "UI/UILayerTypes.h"
#include "FrontendPlayerController.generated.h"

/**
 * 
 */

class UInputAction;
class UInputMappingContext;
class ULocalPlayerUILayerSubsystem;
class ULoadingWidget;
class UTitleWidget;

namespace FrontendInputModeTags
{
	inline FGameplayTag UI()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Input.Mode.UI")));
		return Tag;
	}
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnPartyRequestResult,
	EOutlierPartyRequestResult, Result,
	const FString&, PartyCode);

UCLASS()
class OUTLIER_API AFrontendPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	virtual void BeginPlay() override;
	virtual void AcknowledgePossession(APawn* P) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	UFUNCTION(Server, Reliable)
	void ServerRequestMatchmaking();

	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestCreateParty();

	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestJoinParty(const FString& PartyCode);

	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestLeaveParty();

	UFUNCTION(Server, Reliable)
	void ServerRequestCreateParty();

	UFUNCTION(Server, Reliable)
	void ServerRequestJoinParty(const FString& PartyCode);

	UFUNCTION(Server, Reliable)
	void ServerRequestLeaveParty();

	UFUNCTION(Client, Reliable)
	void ClientNotifyPartyResult(
		EOutlierPartyRequestResult Result,
		const FString& PartyCode);

	UFUNCTION(BlueprintPure, Category = "Party")
	const FString& GetCurrentPartyCode() const { return CurrentPartyCode; }

	UFUNCTION(BlueprintCallable)
	void RequestSelectLobbyRole(EOutlierPlayerRole DesiredRole);

	UFUNCTION(BlueprintCallable)
	void RequestStartPendingMatch();

	UFUNCTION(Server, Reliable)
	void ServerRequestSelectLobbyRole(EOutlierPlayerRole DesiredRole);

	UFUNCTION(Server, Reliable)
	void ServerRequestStartPendingMatch();

	UFUNCTION(Client, Reliable)
	void ClientPrepareForMatch();

	UFUNCTION(Client, Reliable)
	void ClientHandoffToArena(const FString& ArenaUrl);

	UFUNCTION(BlueprintCallable)
	void RequestCancelMatchmaking();

	UFUNCTION(Server, Reliable)
	void ServerRequestCancelMatchmaking();

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool SetFrontendInputMode(FGameplayTag NewInputMode);

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool RestoreFrontendDefaultInputMode();

	UFUNCTION(BlueprintPure, Category = "Input|Input Mode")
	FGameplayTag GetFrontendInputMode() const { return CurrentFrontendInputMode; }

	const TArray<TObjectPtr<UInputMappingContext>>& GetDefaultMappingContexts() const { return DefaultMappingContexts; }
	UInputAction* GetWidgetEscapeAction() const { return WidgetEscapeAction; }
	UInputAction* GetWidgetConfirmedAction() const { return WidgetConfirmedAction; }

protected:
	virtual void SetupInputComponent() override;

	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<TObjectPtr<UInputMappingContext>> DefaultMappingContexts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> WidgetEscapeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> WidgetConfirmedAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> WidgetUpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> WidgetDownAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> WidgetLeftAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> WidgetRightAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Input Mode")
	FGameplayTag DefaultFrontendInputMode;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Input|Input Mode")
	FGameplayTag CurrentFrontendInputMode;

	void HandleWidgetEscapeInput();
	void HandleWidgetConfirmedInput();
	void HandleWidgetUpInput();
	void HandleWidgetDownInput();
	void HandleWidgetLeftInput();
	void HandleWidgetRightInput();

public:
	UPROPERTY(BlueprintAssignable, Category = "Party")
	FOnPartyRequestResult OnPartyRequestResult;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UTitleWidget> TitleWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UTitleWidget> TitleWidgetClass;

private:
	void PrepareForMatch();
	void TryStartNetworkMvpSmoke();
	void DriveNetworkMvpSmoke();

	UPROPERTY(Transient)
	FString CurrentPartyCode;

	EOutlierPlayerRole NetworkMvpSmokeRole = EOutlierPlayerRole::None;
	bool bNetworkMvpSmokeMatchmakingRequested = false;
	bool bNetworkMvpSmokeRoleRequested = false;
	FTimerHandle NetworkMvpSmokeTimerHandle;
	FUILayerHandle TitleLayerHandle;
};
