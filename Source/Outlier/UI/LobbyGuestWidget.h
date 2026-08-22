#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyGuestWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

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

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> GuestText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ResultImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Guest")
	TObjectPtr<UTexture2D> DefaultTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Guest")
	TObjectPtr<UTexture2D> ConfirmedTexture;

	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby|Guest")
	void OnGuestStateChanged(ELobbyGuestWidgetState State, bool bIsLocalGuest, bool bIsConfirmed);

private:
	void RefreshText();
	void RefreshResultImage();

	int32 GuestIndex = INDEX_NONE;
	ELobbyGuestWidgetState GuestState = ELobbyGuestWidgetState::Default;
	bool bIsOwningLocalGuest = false;
	bool bConfirmed = false;
};
