// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OutlierPlayerState.h"
#include "LobbyWidget.generated.h"

/**
 * 
 */
class UButton;
class UImage;

// Button에 PC 정보를 받아서, PlayerSTATE에 ENUM BIND;
// 

UCLASS()
class OUTLIER_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> ShooterSelect;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> PartnerSelect;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ShooterImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> PartnerImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ShooterSelectedImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> PartnerSelectedImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> BackButton;

private:
	UFUNCTION()
	void HandleShooterSelectClicked();

	UFUNCTION()
	void HandlePartnerSelectClicked();

	UFUNCTION()
	void HandleStartButtonClicked();

	UFUNCTION()
	void HandleBackButtonEvent();

	void HandlePendingLobbyStateChanged(AOutlierPlayerState* ChangedPS);
	void RequestRole(EOutlierPlayerRole DesiredRole);
	void BindLobbyPlayerStateDelegates();
	void UnbindLobbyPlayerStateDelegates();
	AOutlierPlayerState* GetLocalOutlierPlayerState() const;
	void RefreshRoleSelection();

private:
	//Widget delegate 설정이 PS array 촉기화보다 일러서 Timer로 지연 delegate 초기화;
	FTimerHandle LobbyRefreshTimerHandle;
	void StartLobbyRefreshTimer();
	void StopLobbyRefreshTimer();
};
