#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyGuestWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;
class UButton;
class UInputActionKeyDisplayWidget;
class UMaterialInstanceDynamic;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLobbyGuestMoveRequested, int32 /* Direction */);

UENUM(BlueprintType)
enum class ELobbyGuestWidgetState : uint8
{
	Default,
	Shooter,
	Partner
};

UCLASS(Abstract, Blueprintable)
class OUTLIER_API ULobbyGuestWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	FOnLobbyGuestMoveRequested OnMoveRequested;

	UFUNCTION(BlueprintCallable, Category = "Lobby|Guest")
	void SetGuestIndex(int32 InGuestIndex);

	UFUNCTION(BlueprintCallable, Category = "Lobby|Guest")
	void SetGuestState(ELobbyGuestWidgetState InState, bool bInIsLocalGuest);

	UFUNCTION(BlueprintCallable, Category = "Lobby|Guest")
	void SetConfirmed(bool bInConfirmed);

	UFUNCTION(BlueprintPure, Category = "Lobby|Guest")
	int32 GetGuestIndex() const { return GuestIndex; }

	UFUNCTION(BlueprintPure, Category = "Lobby|Guest")
	ELobbyGuestWidgetState GetGuestState() const { return GuestState; }

	UFUNCTION(BlueprintPure, Category = "Lobby|Guest")
	bool IsConfirmed() const { return bConfirmed; }

	bool GetFrameCenterInLocalSpace(FVector2D& OutCenter) const;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> LeftButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> RightButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Frame;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> GuestText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ResultImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UInputActionKeyDisplayWidget> DefaultKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Guest")
	TObjectPtr<UTexture2D> ConfirmedTexture;

	// 양쪽 버튼이 공유하는 누적 재생 시간. 하나라도 Hover 중이면 값을 유지한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Guest|Material")
	FName ButtonAnimationTimeParameterName = TEXT("LobbyButtonTime");

	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby|Guest")
	void OnGuestStateChanged(ELobbyGuestWidgetState State, bool bIsLocalGuest, bool bIsConfirmed);

private:
	UFUNCTION()
	void HandleLeftButtonClicked();

	UFUNCTION()
	void HandleRightButtonClicked();

	UMaterialInstanceDynamic* InitializeButtonMaterial(UButton* Button);
	void UpdateButtonAnimations(float DeltaTime);
	void SetArrowButtonVisible(UButton* Button, bool bVisible) const;

	void RefreshText();
	void RefreshResultImage();
	void RefreshPresentation();

	int32 GuestIndex = INDEX_NONE;
	ELobbyGuestWidgetState GuestState = ELobbyGuestWidgetState::Default;
	bool bIsOwningLocalGuest = false;
	bool bConfirmed = false;
	float ButtonAnimationTime = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LeftButtonMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RightButtonMaterial;
};
