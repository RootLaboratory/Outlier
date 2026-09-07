// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OutlierPlayerState.h"
#include "UI/LobbyGuestWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "LobbyWidget.generated.h"

/**
 * 
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLobbyBackRequested);

// Button에 PC 정보를 받아서, PlayerSTATE에 ENUM BIND;
// 

UCLASS()
class OUTLIER_API ULobbyWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()
public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintAssignable)
	FOnLobbyBackRequested OnBackRequested;

protected:
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<ULobbyGuestWidget> Guest1Widget;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<ULobbyGuestWidget> Guest2Widget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Guest", meta = (ClampMin = "0.0"))
	float RoleOffsetViewportScale = 0.32f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby|Guest")
	void OnGuestWidgetStateChanged(int32 GuestIndex, ELobbyGuestWidgetState State, bool bIsLocalGuest);

	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby|Guest")
	void OnLobbyRoleConfirmRejected(EOutlierPlayerRole RejectedRole);

private:
	void HandlePendingLobbyStateChanged(AOutlierPlayerState* ChangedPS);
	void RequestRole(EOutlierPlayerRole DesiredRole);
	void RequestCancelMatchmaking();
	void PopSelfFromLayer();
	void BindLobbyPlayerStateDelegates();
	void UnbindLobbyPlayerStateDelegates();
	AOutlierPlayerState* GetLocalOutlierPlayerState() const;
	void RefreshRoleSelection();
	void RefreshGuestWidgets();
	void SetLocalGuestPreviewState(ELobbyGuestWidgetState NewState);
	void MoveLocalGuestPreview(int32 Direction);
	bool TryConfirmLocalGuestPreview();
	bool TryClearLocalGuestConfirmation();
	bool IsRoleTakenByOther(EOutlierPlayerRole DesiredRole) const;
	EOutlierPlayerRole ConvertGuestStateToRole(ELobbyGuestWidgetState State) const;
	ELobbyGuestWidgetState ConvertRoleToGuestState(EOutlierPlayerRole Role) const;
	ULobbyGuestWidget* GetGuestWidgetByIndex(int32 GuestIndex) const;
	void ApplyGuestWidgetState(
		int32 GuestIndex,
		ELobbyGuestWidgetState State,
		bool bIsLocalGuest,
		bool bIsConfirmed);
	float GetRoleOffsetPixels() const;

private:
	//Widget delegate 설정이 PS array 촉기화보다 일러서 Timer로 지연 delegate 초기화;
	FTimerHandle LobbyRefreshTimerHandle;
	void StartLobbyRefreshTimer();
	void StopLobbyRefreshTimer();

	ELobbyGuestWidgetState LocalGuestPreviewState = ELobbyGuestWidgetState::Default;
	bool bLocalClearConfirmationPreview = false;
};
