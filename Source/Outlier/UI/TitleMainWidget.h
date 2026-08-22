// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerTypes.h"
#include "TitleMainWidget.generated.h"

/**
 * 
 */

class UTitleWidget;
class UCreditWidget;
class ULobbyWidget;
class USettingWidget;
class UUILayerKeyHintWidget;

UCLASS()
class OUTLIER_API UTitleMainWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

public:

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTitleWidget> TitleWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<ULobbyWidget> LobbyWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<UCreditWidget> CreditWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<USettingWidget> SettingWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Layer")
	TSubclassOf<UUILayerKeyHintWidget> KeyHintWidgetClass;

private:
	UFUNCTION()
	void StartPressed();

	UFUNCTION()
	void ExitPressed();

	UFUNCTION()
	void ShowLobby();

	UFUNCTION()
	void ShowTitle();

	UFUNCTION()
	void RequestExit();

	UFUNCTION()
	void HandleLobbyBackRequested();

	UFUNCTION()
	void HandleCreditRequested();

	UFUNCTION()
	void HandleSettingRequested();

	void PopLobbyLayer();
	void PushCreditLayer();
	void PushSettingLayer();
	void PushKeyHintLayer();

	UPROPERTY(Transient)
	TObjectPtr<ULobbyWidget> ActiveLobbyWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCreditWidget> ActiveCreditWidget;

	UPROPERTY(Transient)
	TObjectPtr<USettingWidget> ActiveSettingWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUILayerKeyHintWidget> ActiveKeyHintWidget;

	FUILayerHandle LobbyLayerHandle;
	FUILayerHandle KeyHintLayerHandle;
};
