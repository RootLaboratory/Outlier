#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "UI/UILayerTypes.h"
#include "TitleWidget.generated.h"

class UButton;
class UCreditWidget;
class ULobbyWidget;
class USettingWidget;
class UUILayerKeyHintWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTitleStartRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTitleExitRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTitleCreditRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTitleSettingRequested);



UCLASS()
class OUTLIER_API UTitleWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

public:
	UFUNCTION()
	void HandleStartButtonEvent();
	UFUNCTION()
	void HandleExitButtonEvent();
	UFUNCTION()
	void HandleCreditButtonEvent();
	UFUNCTION()
	void HandleSettingButtonEvent();

	UPROPERTY(BlueprintAssignable)
	FOnTitleStartRequested OnStartRequested;

	UPROPERTY(BlueprintAssignable)
	FOnTitleExitRequested OnExitRequested;

	UPROPERTY(BlueprintAssignable)
	FOnTitleCreditRequested OnCreditRequested;

	UPROPERTY(BlueprintAssignable)
	FOnTitleSettingRequested OnSettingRequested;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> ExitButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> CreditButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> SettingButton;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<ULobbyWidget> LobbyWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<UCreditWidget> CreditWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<USettingWidget> SettingWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<UUILayerKeyHintWidget> KeyHintWidgetClass;

private:
	void PushLobbyLayer();
	void PushCreditLayer();
	void PushSettingLayer();
	void PushKeyHintLayer();
	void RequestExit();

	UPROPERTY(Transient)
	TObjectPtr<UUILayerKeyHintWidget> ActiveKeyHintWidget;

	FUILayerHandle KeyHintLayerHandle;
};
