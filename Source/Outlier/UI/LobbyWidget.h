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

class UImage;
class UTexture2D;

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
	virtual void NativeOnInitialized() override;
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

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> GuestTargetUp;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> GuestTargetDown;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ShooterLobbyImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> PartnerLobbyImage;

	// 좌(Shooter)/우(Partner) 입력으로 해당 역할에 포커스됐을 때 역할 이미지에 넣을 텍스처.
	// 비워두면 디자이너에 지정된 기본 이미지를 그대로 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Role Image")
	TObjectPtr<UTexture2D> ShooterFocusedImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Role Image")
	TObjectPtr<UTexture2D> PartnerFocusedImage;

	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby|Guest")
	void OnGuestWidgetStateChanged(int32 GuestIndex, ELobbyGuestWidgetState State, bool bIsLocalGuest);

	// 두 Guest 중 누군가 Shooter/Partner에 있는지가 바뀐 직후 호출된다.
	// 이미지 교체 외 연출(애니메이션, 사운드 등)을 BP에서 붙이는 지점.
	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby|Role Image")
	void OnRoleFocusChanged(bool bShooterFocused, bool bPartnerFocused);

	// 역할별 포커스 여부에 맞춰 역할 이미지를 갈아끼운다. 교체 규칙을 바꾸려면 BP에서 오버라이드한다.
	UFUNCTION(BlueprintNativeEvent, Category = "Lobby|Role Image")
	void ApplyRoleFocusImages(bool bShooterFocused, bool bPartnerFocused);

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
	FVector2D GetRoleOffset(int32 GuestIndex) const;
	void CacheDefaultRoleImageBrushes();
	void RefreshRoleFocusImages(const ELobbyGuestWidgetState (&GuestStates)[2]);
	static void SetRoleImageFocused(UImage* RoleImage, const FSlateBrush& DefaultBrush, UTexture2D* FocusedTexture, bool bFocused);

private:
	//Widget delegate 설정이 PS array 촉기화보다 일러서 Timer로 지연 delegate 초기화;
	FTimerHandle LobbyRefreshTimerHandle;
	void StartLobbyRefreshTimer();
	void StopLobbyRefreshTimer();

	ELobbyGuestWidgetState LocalGuestPreviewState = ELobbyGuestWidgetState::Default;
	int32 LocalPreviewMatchId = INDEX_NONE;
	bool bLocalPreviewEdited = false;
	bool bLocalClearConfirmationPreview = false;

	// 디자이너에서 지정한 역할 이미지 원본. 포커스가 풀리면 이 브러시로 되돌린다.
	UPROPERTY(Transient)
	FSlateBrush DefaultShooterLobbyBrush;

	UPROPERTY(Transient)
	FSlateBrush DefaultPartnerLobbyBrush;

	bool bRoleImageBrushesCached = false;

	// 0.1초 갱신 타이머에서 매번 브러시를 다시 넣지 않도록 마지막으로 적용한 포커스를 기억한다.
	bool bRoleFocusApplied = false;
	bool bAppliedShooterFocused = false;
	bool bAppliedPartnerFocused = false;
};
